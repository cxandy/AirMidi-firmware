#include "uart_midi.h"

#include <stdio.h>

#include "airmidi/midi1.h"
#include "board.h"
#include "bus.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "trs";

#define MIDI_UART UART_NUM_1  // UART0's pins (D6/D7) through the GPIO matrix; the console is on USB
#define RX_BUF 1024
#define TX_BUF 8192  // 2.6 s of MIDI at 31250 baud: a BLE burst arrives much faster than the wire
#define RUNNING_STATUS_IDLE_MS 1000

static midi1_in_t parser;
static midi1_out_t encoder;
static uart_midi_stats_t stats;
static uint32_t last_tx_ms;

static void rx_task(void *arg) {
    esp_task_wdt_add(NULL);
    uint8_t buf[64];
    for (;;) {
        // uart_read_bytes() waits until it has the full length, so ask for one byte, then take whatever else is
        // already buffered (spike S0: reading 256 at a time added up to the whole timeout to every message).
        int n = uart_read_bytes(MIDI_UART, buf, 1, pdMS_TO_TICKS(1000));
        esp_task_wdt_reset();
        if (n <= 0) continue;
        size_t more = 0;
        uart_get_buffered_data_len(MIDI_UART, &more);
        if (more > sizeof buf - 1) more = sizeof buf - 1;
        if (more) n += uart_read_bytes(MIDI_UART, buf + 1, more, 0);
        stats.rx_bytes += n;
        for (int i = 0; i < n; i++) {
            ump_t u[2];
            int k = midi1_in_feed(&parser, buf[i], u);
            for (int j = 0; j < k; j++) bus_send(PORT_TRS, &u[j]);
        }
        stats.rx_dropped_bytes = parser.dropped;
    }
}

// Called from the bus task.
static void trs_write(void *ctx, const ump_t *u) {
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    // After a quiet second, send the status byte again: a device plugged in meanwhile has no running status.
    if (now - last_tx_ms > RUNNING_STATUS_IDLE_MS) midi1_out_reset_running(&encoder);
    uint8_t bytes[MIDI1_OUT_MAX];
    size_t n = midi1_out_encode(&encoder, u, bytes);
    if (!n) return;
    size_t room = 0;
    uart_get_tx_buffer_free_size(MIDI_UART, &room);
    if (room < n) {  // cannot happen while the bus respects trs_room(); never block the bus task anyway
        stats.tx_dropped++;
        midi1_out_reset_running(&encoder);  // the next message must not rely on a status that was dropped
        return;
    }
    uart_write_bytes(MIDI_UART, bytes, n);
    stats.tx_bytes += n;
    last_tx_ms = now;
}

static size_t trs_room(void *ctx) {
    size_t room = 0;
    uart_get_tx_buffer_free_size(MIDI_UART, &room);
    return room;
}

static const bus_output_ops_t ops = {.write = trs_write, .room = trs_room, .byte_stream = true};

void uart_midi_start(void) {
    const board_t *b = board();
    uart_config_t uc = {.baud_rate = 31250,
                        .data_bits = UART_DATA_8_BITS,
                        .parity = UART_PARITY_DISABLE,
                        .stop_bits = UART_STOP_BITS_1,
                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                        // The crystal: unaffected by frequency scaling, and with the BLE low-power clock on the
                        // main crystal it keeps running in light sleep.
                        .source_clk = UART_SCLK_XTAL};
    ESP_ERROR_CHECK(uart_driver_install(MIDI_UART, RX_BUF, TX_BUF, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MIDI_UART, &uc));
    ESP_ERROR_CHECK(uart_set_pin(MIDI_UART, b->midi_tx, b->midi_rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    // Hand received bytes to the driver after 1 byte-time of silence (default: 10), so a message is routed as
    // soon as it ends, not when the line has been quiet for 3 ms.
    uart_set_rx_timeout(MIDI_UART, 1);
    // In automatic light sleep, a few edges on RX wake the chip (3 is the minimum threshold).
    ESP_ERROR_CHECK(uart_set_wakeup_threshold(MIDI_UART, 3));
    ESP_ERROR_CHECK(esp_sleep_enable_uart_wakeup(MIDI_UART));
    midi1_in_init(&parser, 0);
    midi1_out_init(&encoder, true);
    bus_register_output(PORT_TRS, &ops, NULL);
    bus_set_up(PORT_TRS, true);  // a cable may or may not be in J2; there is no way to tell, so always up
    xTaskCreate(rx_task, "trs_rx", 3072, NULL, 12, NULL);
    ESP_LOGI(TAG, "UART1 at 31250 baud, TX GPIO%d (J2 OUT), RX GPIO%d (J3 IN)", b->midi_tx, b->midi_rx);
}

void uart_midi_get_stats(uart_midi_stats_t *out) { *out = stats; }

// Nothing is needed but the loopback cable. The OUT tip is made to sink current (a run of 0x00 bytes holds it low 9
// bit-times out of 10) while the TIP and the IN pin are sampled, with the chip's pull-down on the IN pin so a pin
// that is not on the 6N138 cannot read high by accident:
//   TIP dips          -> the UART's bytes reach the pin: the OUT side is alive;
//   RING reads 1      -> the Type A current source is on the ring, not shorted to ground;
//   IN high at idle   -> the opto's 1k pull-up is there: the IN pin is connected;
//   IN dips while low -> the opto conducts: OUT tip -> jack -> cable -> jack -> opto -> IN pin works.
// Anything else names the break.
void uart_midi_loop_test(void) {
    const board_t *b = board();
    if (b->midi_tx == GPIO_NUM_NC || b->midi_rx == GPIO_NUM_NC) {
        printf("trs loop: this board has no TRS pins\n");
        return;
    }
    // gpio_config(GPIO_MODE_OUTPUT) leaves the pad's input path off, so gpio_get_level() reads 0 for the two OUT
    // pins no matter what they drive. Turn the input path on; the output side is not touched.
    gpio_input_enable(b->midi_tx);
    if (b->trs_ring != GPIO_NUM_NC) gpio_input_enable(b->trs_ring);
    vTaskDelay(pdMS_TO_TICKS(2));

    int ring = b->trs_ring == GPIO_NUM_NC ? -1 : gpio_get_level(b->trs_ring);
    int tip_idle = gpio_get_level(b->midi_tx);

    gpio_set_pull_mode(b->midi_rx, GPIO_PULLDOWN_ONLY);  // the UART keeps the pin; only its pull changes
    vTaskDelay(pdMS_TO_TICKS(2));
    int in_idle = gpio_get_level(b->midi_rx);

    static const uint8_t zeros[32] = {0};
    uart_write_bytes(MIDI_UART, zeros, sizeof zeros);  // ~10 ms with the OUT tip low 9 bit-times out of 10
    int tip_low = 0, in_low = 0;
    int64_t until = esp_timer_get_time() + 12000;
    while (esp_timer_get_time() < until) {
        tip_low += !gpio_get_level(b->midi_tx);
        in_low += !gpio_get_level(b->midi_rx);
    }
    uart_wait_tx_done(MIDI_UART, pdMS_TO_TICKS(100));
    gpio_set_pull_mode(b->midi_rx, GPIO_PULLUP_ONLY);  // back to what uart_set_pin() left on the pin
    vTaskDelay(pdMS_TO_TICKS(1));

    if (b->trs_ring != GPIO_NUM_NC)
        printf("trs loop: RING GPIO%d reads %d (1 = the Type A current source is on the ring)\n", b->trs_ring, ring);
    printf("trs loop: TIP GPIO%d idle %d, dipped %d time(s) while sending\n", b->midi_tx, tip_idle, tip_low);
    printf("trs loop: IN GPIO%d idle %d, dipped %d time(s) while the TIP was low\n", b->midi_rx, in_idle, in_low);

    if (!tip_low)
        printf("trs loop: FAIL - the UART's bytes do not reach GPIO%d\n", b->midi_tx);
    else if (!in_idle)
        printf(
            "trs loop: FAIL - no 1k pull-up on GPIO%d: the pin is off the 6N138, or the 3.3 V it pulls up to is "
            "missing\n",
            b->midi_rx);
    else if (!in_low)
        printf(
            "trs loop: FAIL - the opto never conducts: no current path (the cable's RING, or the ring not at 3.3 V)\n");
    else
        printf("trs loop: PASS - OUT tip -> jack -> cable -> jack -> opto -> GPIO%d\n", b->midi_rx);
}

// Console 'l': the same loop, but the OUT tip is held at a steady low instead of pulsed, so a meter reads the loop
// rather than an average of a pulse train: the tip should read 0 V, the opto's anode about 2 V (3.3 V minus the LED)
// and its cathode about 0 V, while the IN pin stays low for the whole window. A tip that is not 0 V is the pin, an
// anode at 3.3 V is no current (an open loop or a dead LED), and a low anode with the IN pin still high is a dead
// opto. The UART's TX is detached from the pad for the window (the driver keeps draining its buffer into nothing)
// and put back afterwards.
void uart_midi_hold_low(int seconds) {
    const board_t *b = board();
    if (b->midi_tx == GPIO_NUM_NC || b->midi_rx == GPIO_NUM_NC) {
        printf("hold low: this board has no TRS pins\n");
        return;
    }
    uart_wait_tx_done(MIDI_UART, pdMS_TO_TICKS(200));  // never cut a byte in half
    gpio_reset_pin(b->midi_tx);
    gpio_set_direction(b->midi_tx, GPIO_MODE_OUTPUT);
    gpio_set_level(b->midi_tx, 0);
    gpio_input_enable(b->midi_tx);  // an output pad's input path is off: without this the read-back is always 0
    gpio_set_pull_mode(b->midi_rx, GPIO_PULLDOWN_ONLY);  // a pin off the 6N138 must not read high by accident
    vTaskDelay(pdMS_TO_TICKS(2));

    printf("hold low: TIP GPIO%d reads %d for %d s, measure now\n", b->midi_tx, gpio_get_level(b->midi_tx), seconds);
    int samples = 0, low = 0;
    int64_t until = esp_timer_get_time() + (int64_t)seconds * 1000000;
    while (esp_timer_get_time() < until) {
        low += !gpio_get_level(b->midi_rx);
        samples++;
        vTaskDelay(1);
    }
    printf("hold low: IN GPIO%d read low in %d of %d samples\n", b->midi_rx, low, samples);

    gpio_set_pull_mode(b->midi_rx, GPIO_PULLUP_ONLY);  // back to what uart_set_pin() left on the pin
    uart_set_pin(MIDI_UART, b->midi_tx, b->midi_rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    printf("hold low: done, the UART has the TIP back\n");
}
