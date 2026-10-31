// The setup access point's DNS answer: everything resolves to the box. A phone that joins the open network then
// reaches the web UI by opening any name at all, and the portal (below) sends it on to the setup page, so nobody
// has to be told the address. RFC 1035, questions only: a query's first question is echoed, an A question is
// answered with one record, an AAAA question is answered with none (an empty answer is quicker than a timeout),
// and anything that is not a plain query is dropped.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The largest reply: header, one question and one answer. A datagram bigger than this is dropped.
#define CAPTIVE_DNS_MAX 512

// Build the reply for one received datagram. `ip` is the answer as a host-order IPv4 address (0xC0A80401 is
// 192.168.4.1). Returns the reply's length, or 0 when the datagram gets no answer: a reply rather than a query, a
// name that does not parse, no question, or a reply that would not fit in `cap`.
size_t captive_dns_reply(const uint8_t *in, size_t len, uint8_t *out, size_t cap, uint32_t ip);

#ifdef __cplusplus
}
#endif
