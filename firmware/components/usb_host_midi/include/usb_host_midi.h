// USB host role for the S3's one USB port: instead of presenting a USB MIDI device to a computer, the box reads
// one. A digital piano with neither 5-pin DIN nor Bluetooth is a USB MIDI device (its USB TO HOST port); with the
// port in host role the box enumerates it, and its notes, pedals and controllers arrive on PORT_USB like any other
// source while BLE and Wi-Fi carry them out. One port, one role: this build has no USB console and no USB MIDI
// output, and the console stays on UART0 (the dev kit's USB-UART bridge), so plug that port to see any log.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool mounted;                  // a MIDI device is enumerated and its interface is open
    uint8_t rx_cables, tx_cables;  // virtual cables it offers, either direction
    uint32_t rx_packets, tx_packets, tx_dropped;
} usb_host_midi_stats_t;

// Raise VBUS, bring up the host stack and the MIDI host class driver (the S3 only). Takes its own task.
void usb_host_midi_start(void);
void usb_host_midi_get_stats(usb_host_midi_stats_t *out);