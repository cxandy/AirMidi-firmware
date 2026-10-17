#include "usb_host_midi.h"

#include "airmidi/midi1.h"
#include "bus.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "tusb.h"

static const char *TAG = "usbhost";

#define RHPORT 0   // the S3's only host port
#define RX_BUF 48  // tuh_midi_stream_read works in chunks; a bulk IN transfer holds several messages

#ifndef CONFIG_AIRMIDI_USB_HOST_VBUS_GPIO
#error "CONFIG_AIRMIDI_USB_HOST_VBUS_GPIO must name the GPIO that enables the VBUS switch: without VBUS the \
piano's USB transceiver does not even power up"
#endif

static midi1_in_t parser;  // piano -> bus
static midi1_out_t encoder;  // bus -> piano
static usb_host_midi_stats_t stats;
static uint8_t ifc;  // the one MIDI interface the host driver supports, set when a device mounts
static bool mounted;

// The bus task hands messages over, the host task sends them: TinyUSB is not thread-safe and its driver belongs to
// the task that runs tuh_task(). One producer, one consumer, no lock needed.
#define OUTQ 32
static ump_t outq[OUTQ];
static volatile uint16_t out_head, out_tail;

static void usb_host_task(void *arg) {
    (void) arg;
    while (1) {
        tuh_task();
        while (out_head != out_tail) {
            ump_t u = outq[out_tail];
            out_tail = (out_tail + 1) % OUTQ;
            uint8_t buf[MIDI1_OUT_MAX];
            size_t n = midi1_out_encode(&encoder, &u, buf);
            if (mounted && n) {
                if (tuh_midi_stream_write(ifc, 0, buf, n) == n) {
                    stats.tx_packets++;
                } else {
                    stats.tx_dropped++;
                }
            }
        }
        // The stack also needs feeding when nothing is queued, so this cannot be a blocking wait.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ---- receive (host task) -------------------------------------------------------------------------------------

static void feed_piano(uint8_t *buf, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        ump_t u[2];
        int m = midi1_in_feed(&parser, buf[i], u);
        for (int j = 0; j < m; j++) bus_send(PORT_USB, &u[j]);
    }
}

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *data) {
    ifc = idx;
    stats.rx_cables = data->rx_cable_count;
    stats.tx_cables = data->tx_cable_count;
    mounted = true;
    stats.mounted = true;
    midi1_in_init(&parser, 0);
    // USB MIDI 1.0 has no running status: every status byte has to travel, or the device reads the data bytes as
    // a status. The TRS output asks for running status because the wire allows it.
    midi1_out_init(&encoder, false);
    bus_set_up(PORT_USB, true);
    ESP_LOGI(TAG, "MIDI device mounted, %u rx / %u tx cables", stats.rx_cables, stats.tx_cables);
}

void tuh_midi_umount_cb(uint8_t idx) {
    (void) idx;
    mounted = false;
    stats.mounted = false;
    bus_set_up(PORT_USB, false);
    ESP_LOGI(TAG, "MIDI device unmounted");
}

void tuh_midi_rx_cb(uint8_t idx, uint32_t xferred) {
    (void) idx;
    if (!xferred) return;
    uint8_t buf[RX_BUF];
    uint8_t c;
    uint32_t n;
    while ((n = tuh_midi_stream_read(ifc, &c, buf, sizeof buf)) > 0) {
        // One transfer can carry several virtual cables; the box speaks for cable 0 and ignores the rest.
        if (c != 0) continue;
        stats.rx_packets++;
        feed_piano(buf, n);
    }
}

void tuh_midi_tx_cb(uint8_t idx, uint32_t xferred) {
    (void) idx;
    (void) xferred;  // counted where the message was queued, which is the useful place
}

// ---- send (bus task) -----------------------------------------------------------------------------------------

static void write_piano(void *ctx, const ump_t *u) {
    (void) ctx;
    uint16_t next = (out_head + 1) % OUTQ;
    if (next == out_tail) {
        stats.tx_dropped++;  // the host task has not drained the last burst yet
        return;
    }
    outq[out_head] = *u;
    out_head = next;
}

static const bus_output_ops_t ops = {
    .write = write_piano,
    .byte_stream = true,  // a MIDI 1.0 wire: the bus merges its sources, running status included
};

// ---- start ---------------------------------------------------------------------------------------------------

void usb_host_midi_start(void) {
    gpio_config_t vbus = {.pin_bit_mask = 1ULL << CONFIG_AIRMIDI_USB_HOST_VBUS_GPIO,
                          .mode = GPIO_MODE_OUTPUT};
    gpio_config(&vbus);
    gpio_set_level(CONFIG_AIRMIDI_USB_HOST_VBUS_GPIO, 1);
    ESP_LOGI(TAG, "VBUS on GPIO%d", CONFIG_AIRMIDI_USB_HOST_VBUS_GPIO);

    // Register before the stack comes up: a device can enumerate the moment tusb_init() runs.
    bus_register_output(PORT_USB, &ops, NULL);

    const tusb_rhport_init_t init = {.role = TUSB_ROLE_HOST, .speed = TUSB_SPEED_AUTO};
    ESP_ERROR_CHECK(tusb_init(RHPORT, &init));
    xTaskCreate(usb_host_task, "usbhost", 4096, NULL, 5, NULL);
}

void usb_host_midi_get_stats(usb_host_midi_stats_t *out) { *out = stats; }