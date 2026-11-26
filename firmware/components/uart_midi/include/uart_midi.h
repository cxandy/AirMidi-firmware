// TRS MIDI: J3 IN -> UART RX -> midi1_in -> bus (PORT_TRS), and bus -> midi1_out -> UART TX -> J2 OUT.
// A board whose RING is on a GPIO (s3direct) can send either TRS type: the UART's TX moves between the TIP and the
// RING, and the other pin is held high as the current source.
#pragma once

#include <stdint.h>

#include "airmidi/settings.h"

// out_type: the setting the box starts with (TRS_TYPE_AUTO follows the type heard on IN; a board with a bridge input
// never hears one, so auto is Type A there). Ignored on boards wired for Type A only.
void uart_midi_start(trs_type_t out_type);

// Change the OUT type while running (a no-op on boards wired for Type A only).
void uart_midi_set_out_type(trs_type_t t);

// The setting in use, and the next one (auto -> A -> B -> auto): the console's 't' cycles it for a quick test.
trs_type_t uart_midi_out_setting(void);
void uart_midi_cycle_out_type(void);

typedef struct {
    uint32_t rx_bytes, rx_dropped_bytes;  // dropped: stray data bytes / undefined statuses (midi1_in)
    uint32_t tx_bytes, tx_dropped;        // dropped: UART TX buffer full
    char out_type;                        // 'A' or 'B' (the wiring in use); 0 on a board with Type A wired
} uart_midi_stats_t;

void uart_midi_get_stats(uart_midi_stats_t *out);
