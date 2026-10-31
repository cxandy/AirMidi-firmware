#include <string.h>

#include "airmidi/captive.h"
#include "unity.h"

void setUp(void) {}
void tearDown(void) {}

static const uint32_t box = 0xC0A80401;  // 192.168.4.1, network byte order

// Query for `name`, type `qtype`: header (id 0x1234, RD set, one question), the name as labels, type and class.
static size_t query(uint8_t *out, const char *name, uint16_t qtype) {
    size_t n = 0;
    out[n++] = 0x12, out[n++] = 0x34, out[n++] = 0x01, out[n++] = 0x00;
    out[n++] = 0, out[n++] = 1, out[n++] = 0, out[n++] = 0, out[n++] = 0, out[n++] = 0, out[n++] = 0, out[n++] = 0;
    const char *p = name;
    while (*p) {
        const char *dot = strchr(p, '.');
        size_t l = dot ? (size_t)(dot - p) : strlen(p);
        out[n++] = (uint8_t)l;
        memcpy(out + n, p, l), n += l;
        p += l + (dot ? 1 : 0);
    }
    out[n++] = 0;
    out[n++] = (uint8_t)(qtype >> 8), out[n++] = (uint8_t)qtype;
    out[n++] = 0, out[n++] = 1;  // class IN
    return n;
}

static void test_a_query_is_answered_with_the_box(void) {
    uint8_t in[128], out[CAPTIVE_DNS_MAX];
    size_t len = query(in, "captive.apple.com", 1);
    size_t n = captive_dns_reply(in, len, out, sizeof out, box);
    TEST_ASSERT_EQUAL_size_t(len + 16, n);
    TEST_ASSERT_EQUAL_HEX8(0x12, out[0]);  // the client's id, back
    TEST_ASSERT_EQUAL_HEX8(0x34, out[1]);
    TEST_ASSERT_BITS(0x80, 0x80, out[2]);  // QR
    TEST_ASSERT_BITS(0x04, 0x04, out[2]);  // AA
    TEST_ASSERT_BITS(0x01, 0x01, out[2]);  // RD echoed
    TEST_ASSERT_EQUAL_HEX8(0, out[3]);
    TEST_ASSERT_EQUAL_HEX8(0, out[4]), TEST_ASSERT_EQUAL_HEX8(1, out[5]);  // one question
    TEST_ASSERT_EQUAL_HEX8(0, out[6]), TEST_ASSERT_EQUAL_HEX8(1, out[7]);  // one answer
    TEST_ASSERT_EQUAL_HEX8(0, out[8]), TEST_ASSERT_EQUAL_HEX8(0, out[9]);
    TEST_ASSERT_EQUAL_HEX8(0, out[10]), TEST_ASSERT_EQUAL_HEX8(0, out[11]);
    TEST_ASSERT_EQUAL_MEMORY(in + 12, out + 12, len - 12);  // the question, unchanged
    const uint8_t answer[] = {0xC0, 0x0C, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
                              0x00, 0x3C, 0x00, 0x04, 0xC0, 0xA8, 0x04, 0x01};
    TEST_ASSERT_EQUAL_MEMORY(answer, out + len, sizeof answer);
}

static void test_any_is_answered_too(void) {
    uint8_t in[64], out[CAPTIVE_DNS_MAX];
    size_t len = query(in, "box", 255);
    TEST_ASSERT_EQUAL_size_t(len + 16, captive_dns_reply(in, len, out, sizeof out, box));
}

static void test_aaaa_gets_an_empty_answer(void) {
    uint8_t in[64], out[CAPTIVE_DNS_MAX];
    size_t len = query(in, "box.local", 28);
    size_t n = captive_dns_reply(in, len, out, sizeof out, box);
    TEST_ASSERT_EQUAL_size_t(len, n);                                      // the question alone
    TEST_ASSERT_EQUAL_HEX8(0, out[6]), TEST_ASSERT_EQUAL_HEX8(0, out[7]);  // no answer
}

static void test_other_types_are_dropped(void) {
    uint8_t in[64], out[CAPTIVE_DNS_MAX];
    size_t len = query(in, "box", 16);  // TXT: nothing this box can say
    TEST_ASSERT_EQUAL_size_t(len, captive_dns_reply(in, len, out, sizeof out, box));
    TEST_ASSERT_EQUAL_size_t(0, captive_dns_reply(in, 11, out, sizeof out, box));  // shorter than a header
}

static void test_replies_and_other_opcodes_are_dropped(void) {
    uint8_t in[64], out[CAPTIVE_DNS_MAX];
    size_t len = query(in, "box", 1);
    in[2] |= 0x80;  // QR: this is a reply
    TEST_ASSERT_EQUAL_size_t(0, captive_dns_reply(in, len, out, sizeof out, box));
    in[2] = 0x21;  // opcode 2 (status)
    TEST_ASSERT_EQUAL_size_t(0, captive_dns_reply(in, len, out, sizeof out, box));
    in[2] = 0x01, in[5] = 0;  // no question
    TEST_ASSERT_EQUAL_size_t(0, captive_dns_reply(in, len, out, sizeof out, box));
}

static void test_a_name_that_does_not_parse_is_dropped(void) {
    uint8_t in[128], out[CAPTIVE_DNS_MAX];
    size_t len = query(in, "captive.apple.com", 1);
    in[12] = 64;  // a label longer than 63
    TEST_ASSERT_EQUAL_size_t(0, captive_dns_reply(in, len, out, sizeof out, box));
    in[12] = 200;  // runs past the end
    TEST_ASSERT_EQUAL_size_t(0, captive_dns_reply(in, len, out, sizeof out, box));
    in[12] = 0x01, in[13] = 0xC0;  // no root byte before the end
    TEST_ASSERT_EQUAL_size_t(0, captive_dns_reply(in, len, out, sizeof out, box));
}

static void test_a_reply_that_does_not_fit_is_dropped(void) {
    uint8_t in[128], out[CAPTIVE_DNS_MAX];
    size_t len = query(in, "captive.apple.com", 1);
    TEST_ASSERT_EQUAL_size_t(0, captive_dns_reply(in, len, out, len + 15, box));
    TEST_ASSERT_EQUAL_size_t(len + 16, captive_dns_reply(in, len, out, len + 16, box));
}

// Every prefix of a real query, and random datagrams: no answer that is longer than the buffer, nothing else.
static void test_nothing_parses_past_the_buffer(void) {
    uint8_t in[128], out[CAPTIVE_DNS_MAX];
    size_t len = query(in, "connectivitycheck.gstatic.com", 1);
    for (size_t i = 0; i <= len; i++) {
        size_t n = captive_dns_reply(in, i, out, sizeof out, box);
        TEST_ASSERT_TRUE(n == 0 || n <= sizeof out);
        TEST_ASSERT_TRUE(i >= len || n == 0);
    }
    uint32_t seed = 12345;
    for (int round = 0; round < 2000; round++) {
        size_t n = (size_t)(round % 100);
        for (size_t i = 0; i < n; i++) seed = seed * 1103515245u + 12345u, in[i] = (uint8_t)(seed >> 16);
        size_t r = captive_dns_reply(in, n, out, sizeof out, box);
        TEST_ASSERT_TRUE(r <= sizeof out);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_query_is_answered_with_the_box);
    RUN_TEST(test_any_is_answered_too);
    RUN_TEST(test_aaaa_gets_an_empty_answer);
    RUN_TEST(test_other_types_are_dropped);
    RUN_TEST(test_replies_and_other_opcodes_are_dropped);
    RUN_TEST(test_a_name_that_does_not_parse_is_dropped);
    RUN_TEST(test_a_reply_that_does_not_fit_is_dropped);
    RUN_TEST(test_nothing_parses_past_the_buffer);
    return UNITY_END();
}
