#include "airmidi/captive.h"

#include <stdbool.h>
#include <string.h>

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8), p[1] = (uint8_t)v; }

// Where the first question ends: past the name's labels, the root byte, the type and the class. 0 if it does not
// parse inside `len`.
static size_t question_end(const uint8_t *in, size_t len) {
    size_t p = 12;
    while (p < len && in[p]) {
        if (in[p] > 63 || p + 1 + in[p] > len) return 0;  // a label longer than 63, or one that runs past the end
        p += 1 + in[p];
    }
    return p + 5 <= len ? p + 5 : 0;  // the root byte, qtype, qclass
}

size_t captive_dns_reply(const uint8_t *in, size_t len, uint8_t *out, size_t cap, uint32_t ip) {
    if (len < 12 || cap < 12) return 0;
    if (in[2] & 0x80) return 0;         // QR set: a reply, not a query
    if ((in[2] & 0x78) != 0) return 0;  // opcode other than QUERY
    if (be16(in + 4) < 1) return 0;     // no question to answer
    size_t qend = question_end(in, len);
    if (!qend) return 0;
    uint16_t qtype = be16(in + qend - 4);
    bool answer = qtype == 1 || qtype == 255;  // A, or ANY: the box's own address
    size_t need = qend + (answer ? 16 : 0);
    if (need > cap || need > CAPTIVE_DNS_MAX) return 0;

    memcpy(out, in, 2);                                     // the client's transaction id
    out[2] = (uint8_t)(0x84 | (in[2] & 0x01)), out[3] = 0;  // QR, AA, and the recursion-desired bit echoed back
    put16(out + 4, 1);                                      // one question, the one echoed below
    put16(out + 6, answer ? 1 : 0);
    put16(out + 8, 0), put16(out + 10, 0);
    memcpy(out + 12, in + 12, qend - 12);
    if (!answer) return qend;

    uint8_t *a = out + qend;
    put16(a, 0xC00C);                   // the question's name, by pointer to offset 12
    put16(a + 2, 1);                    // A
    put16(a + 4, 1);                    // IN
    a[6] = a[7] = a[8] = 0, a[9] = 60;  // 60 s: nothing here needs caching
    put16(a + 10, 4);
    a[12] = (uint8_t)(ip >> 24), a[13] = (uint8_t)(ip >> 16);
    a[14] = (uint8_t)(ip >> 8), a[15] = (uint8_t)ip;
    return need;
}
