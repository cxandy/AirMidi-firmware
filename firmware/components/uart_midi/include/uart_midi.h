// TRS MIDI: J3 IN -> UART RX -> midi1_in -> bus (PORT_TRS), and bus -> midi1_out -> UART TX -> J2 OUT.
#pragma once

#include <stdint.h>

void uart_midi_start(void);

typedef struct {
    uint32_t rx_bytes, rx_dropped_bytes;  // dropped: stray data bytes / undefined statuses (midi1_in)
    uint32_t tx_bytes, tx_dropped;        // dropped: UART TX buffer full
} uart_midi_stats_t;

void uart_midi_get_stats(uart_midi_stats_t *out);

// Hardware bring-up: is the TRS port's input really on its output? Holds the OUT tip low (a run of 0x00 bytes) and
// samples the IN pin with the chip's pull-down on, then prints which part of the loop (jack, cable, RING, 3.3 V, the
// 6N138's pull-up) is missing. The box prints the verdict; nothing needs to be plugged in but the loopback cable.
void uart_midi_loop_test(void);
