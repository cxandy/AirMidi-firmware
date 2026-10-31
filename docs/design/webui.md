# Web UI (`web/`, `firmware/components/webui`, `lib/captive`)

The browser is the only way to configure the box: set Wi-Fi, read the meters, change settings, upload firmware. It is
plain HTML/CSS/JS (no framework, ~18 KB) gzipped into the firmware and served by `esp_http_server`, plus a WebSocket
for live MIDI between the page and the jacks. This note covers the one awkward moment in that flow — a phone joining
the setup access point — and how the UI helps.

## The setup portal: nobody is told the address

Setup mode opens an open access point named after the box at `192.168.4.1`. A phone that joins it has to reach the
config page, but it was never told the address, and iOS in particular will not auto-open a captive portal it cannot
verify. Two small pieces fix this, both active only while the access point is up (`net_state() == NET_AP`):

- **A DNS answer that resolves everything to the box** (`lib/captive.c`). A phone's first move after joining is a
  connectivity check — iOS asks for `captive.apple.com`, Android for `connectivitycheck.gstatic.com` — over UDP 53.
  The portal's DNS task (`net.c`, `dns_task`) answers every A/ANY question with `192.168.4.1` and answers AAAA with
  an empty (not NXDOMAIN) reply, so the check resolves rather than timing out. It drops anything that is not a plain
  query (a reply, a non-QUERY opcode, no question, or a name that does not parse), and never answers for a station:
  the socket lives only in `NET_AP`. 8 host tests cover the reply shape, the drop cases and a fuzz of random
  datagrams.
- **A 302 from the web server's 404 handler** (`webui.c`, `portal_redirect`). With a name now pointing at the box, the
  phone fetches its connectivity URL (`/hotspot-detect.html` and the like), which does not exist here. The handler
  answers `NET_AP` requests with `302 Found` → `http://192.168.4.1/` instead of a 404, and the phone opens the setup
  page on its own. In Network mode a 404 stays a 404.

The result: a phone that joins the setup network pops the config page within a second or two, with no address to type
and no "this network has no internet" dead end to fall through.

## The status page points at itself during setup

In `NET_AP` the status page shows a portal card (`#portal` in `web/index.html`): the box's own network name, the
address `http://192.168.4.1/` as a link, a copy button (with an `execCommand` fallback, since `http://` is not a
secure context and the Clipboard API may be absent), and that address as a **QR code drawn on a canvas** from a
constant table in `web/app.js` — version 2, error correction M. It is a picture of a constant (the address never
changes), so it is drawn, not encoded, on the device. This is for a second phone: the first one is already on the
network; the second scans the code to join.

## Phones: the web console is the path, not network MIDI

Both iOS and Android reach the box's controls through the browser — the status page and the play page (faders, pads
and the synth's own controls drawn from MIDI-CI) go over the WebSocket at measured ~3.9 ms page → TRS. No app is
needed, and this works whether the box is in Setup or Network mode.

Network MIDI is different. iOS has no system "network MIDI" panel like macOS's Audio MIDI Setup, so a session has to
be opened by an app, and the box only ever *answers* AppleMIDI invitations (`rtp_midi` registers handlers but never
calls `rtpmidi_invite`). So, on a phone, network MIDI is only reachable through an app that actively connects to an
AppleMIDI host (TouchOSC, midimittr in advertise-and-be-connected mode, AUM), and even then the box will not
initiate. The README tells users plainly: **phones use the web console; network MIDI needs a Mac (native) or Windows
rtpMIDI.** This one line answers most "why can't my iPhone connect over Wi-Fi" tickets.

## What is tested where

- `lib/captive.c`: 8 Unity tests (`test/test_captive.c`) — the A/ANY answer, the empty AAAA answer, drops for other
  types, replies, bad opcodes, an unparseable name, and a fuzz that no datagram produces an over-long reply.
- The portal end to end is a hardware-in-the-loop check: a phone joining the AP should auto-open the page. There is
  no host test for it (it needs a real radio and a real phone), so `tools/hil/` covers it on the bench.
