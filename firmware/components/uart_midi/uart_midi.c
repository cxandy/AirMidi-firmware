#include "uart_midi.h"

#include "airmidi/midi1.h"
#include "board.h"
#include "bus.h"
#include "driver/gpio.h"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_rom_gpio.h"
#include "esp_sleep.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/gpio_sig_map.h"
#include "soc/uart_periph.h"

static const char *TAG = "trs";

#define MIDI_UART UART_NUM_1  // UART0's pins (D6/D7) through the GPIO matrix; the console is on USB
#define RX_BUF 1024
#define TX_BUF 8192  // 2.6 s of MIDI at 31250 baud: a BLE burst arrives much faster than the wire
#define RUNNING_STATUS_IDLE_MS 1000

static midi1_in_t parser;
static midi1_out_t encoder;
static uart_midi_stats_t stats;
static uint32_t last_tx_ms;
static trs_type_t out_setting = TRS_TYPE_AUTO;  // what the user chose
static bool out_type_b;                         // the wiring in use: false = Type A
static SemaphoreHandle_t out_lock;              // the pin switch vs. a write from the bus task
static size_t tx_free_when_empty;               // the TX ring buffer's free size with nothing queued

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

// Called from the bus task. The lock keeps a write from landing while uart_midi_set_out_type moves the TX pin.
static void trs_write(void *ctx, const ump_t *u) {
    uint8_t bytes[MIDI1_OUT_MAX];
    xSemaphoreTake(out_lock, portMAX_DELAY);
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    // After a quiet second, send the status byte again: a device plugged in meanwhile has no running status.
    if (now - last_tx_ms > RUNNING_STATUS_IDLE_MS) midi1_out_reset_running(&encoder);
    size_t n = midi1_out_encode(&encoder, u, bytes);
    if (n) {
        size_t room = 0;
        uart_get_tx_buffer_free_size(MIDI_UART, &room);
        if (room < n) {  // cannot happen while the bus respects trs_room(); never block the bus task anyway
            stats.tx_dropped++;
            midi1_out_reset_running(&encoder);  // the next message must not rely on a status that was dropped
        } else {
            uart_write_bytes(MIDI_UART, bytes, n);
            stats.tx_bytes += n;
            last_tx_ms = now;
        }
    }
    xSemaphoreGive(out_lock);
}

static size_t trs_room(void *ctx) {
    size_t room = 0;
    uart_get_tx_buffer_free_size(MIDI_UART, &room);
    return room;
}

static const bus_output_ops_t ops = {.write = trs_write, .room = trs_room, .byte_stream = true};

// Type A: the data is on the TIP and the RING is the current source; Type B the other way round. The caller holds
// out_lock and has let the UART's TX buffer drain.
static void out_apply(bool type_b) {
    const board_t *b = board();
    gpio_num_t data = type_b ? b->trs_ring : b->midi_tx;
    gpio_num_t source = type_b ? b->midi_tx : b->trs_ring;
    // The old data pin becomes the source: hold its GPIO output latch high, then take the UART's TX signal off it.
    // The pin goes straight to high, not through a low pulse a receiver could take for a start bit.
    gpio_set_level(source, 1);
    gpio_set_direction(source, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(source, SIG_GPIO_OUT_IDX, false, false);
    // The new data pin takes the UART's TX: its idle level is high, the same as the source it was, so no glitch.
    gpio_set_direction(data, GPIO_MODE_OUTPUT);
    esp_rom_gpio_connect_out_signal(data, UART_PERIPH_SIGNAL(MIDI_UART, SOC_UART_TX_PIN_IDX), false, false);
    out_type_b = type_b;
    stats.out_type = type_b ? 'B' : 'A';
    midi1_out_reset_running(&encoder);  // a receiver on the new line has no running status yet
}

// The wiring the setting asks for. Auto follows the type heard on IN; s3direct's input is a bridge that accepts both
// types and never reveals one, so auto stays Type A.
static bool wanted_type_b(void) { return out_setting == TRS_TYPE_B; }

void uart_midi_start(trs_type_t out_type) {
    const board_t *b = board();
    out_setting = out_type;
    out_lock = xSemaphoreCreateMutex();
    uart_config_t uc = {.baud_rate = 31250,
                        .data_bits = UART_DATA_8_BITS,
                        .parity = UART_PARITY_DISABLE,
                        .stop_bits = UART_STOP_BITS_1,
                        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                        // The crystal: unaffected by frequency scaling, and with the BLE low-power clock on the
                        // main crystal it keeps running in light sleep.
                        .source_clk = UART_SCLK_XTAL};
    ESP_ERROR_CHECK(uart_driver_install(MIDI_UART, RX_BUF, TX_BUF, 0, NULL, 0));
    uart_get_tx_buffer_free_size(MIDI_UART, &tx_free_when_empty);  // empty: the most it ever reports
    ESP_ERROR_CHECK(uart_param_config(MIDI_UART, &uc));
    ESP_ERROR_CHECK(uart_set_pin(MIDI_UART, b->midi_tx, b->midi_rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    midi1_in_init(&parser, 0);
    midi1_out_init(&encoder, true);
    if (b->trs_ring != GPIO_NUM_NC) {  // a board that can switch: board_init held the RING high as Type A
        out_apply(wanted_type_b());
        ESP_LOGI(TAG, "OUT: Type %c (%s)", out_type_b ? 'B' : 'A', trs_type_name(out_setting));
    }
    // Hand received bytes to the driver after 1 byte-time of silence (default: 10), so a message is routed as
    // soon as it ends, not when the line has been quiet for 3 ms.
    uart_set_rx_timeout(MIDI_UART, 1);
    // In automatic light sleep, a few edges on RX wake the chip (3 is the minimum threshold).
    ESP_ERROR_CHECK(uart_set_wakeup_threshold(MIDI_UART, 3));
    ESP_ERROR_CHECK(esp_sleep_enable_uart_wakeup(MIDI_UART));
    bus_register_output(PORT_TRS, &ops, NULL);
    bus_set_up(PORT_TRS, true);  // a cable may or may not be in J2; there is no way to tell, so always up
    xTaskCreate(rx_task, "trs_rx", 3072, NULL, 12, NULL);
    ESP_LOGI(TAG, "UART1 at 31250 baud, TX GPIO%d (J2 OUT), RX GPIO%d (J3 IN)",
             b->trs_ring != GPIO_NUM_NC && out_type_b ? b->trs_ring : b->midi_tx, b->midi_rx);
}

void uart_midi_set_out_type(trs_type_t t) {
    if (!out_lock) {  // before uart_midi_start: keep the choice for it
        out_setting = t;
        return;
    }
    const board_t *b = board();
    xSemaphoreTake(out_lock, portMAX_DELAY);
    out_setting = t;
    bool type_b = wanted_type_b();
    if (b->trs_ring != GPIO_NUM_NC && type_b != out_type_b) {
        // Let what is already queued go out on the old wiring. uart_wait_tx_done() only covers the hardware FIFO,
        // not the 2.6 s TX ring buffer, so wait for the ring to drain first; with the lock held no write can enter.
        size_t free = 0;
        for (int i = 0; i < 300; i++) {  // 3 s: a full buffer is 2.6 s of MIDI
            if (uart_get_tx_buffer_free_size(MIDI_UART, &free) != ESP_OK || free >= tx_free_when_empty) break;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (free < tx_free_when_empty) ESP_LOGW(TAG, "TX buffer still busy; switching the TRS type anyway");
        if (uart_wait_tx_done(MIDI_UART, pdMS_TO_TICKS(1000)) != ESP_OK)
            ESP_LOGW(TAG, "TX still busy; switching the TRS type anyway");
        out_apply(type_b);
    }
    if (b->trs_ring != GPIO_NUM_NC)  // also when auto and A are the same wiring here: the console needs an answer
        ESP_LOGI(TAG, "OUT: Type %c (%s)", out_type_b ? 'B' : 'A', trs_type_name(t));
    xSemaphoreGive(out_lock);
}

trs_type_t uart_midi_out_setting(void) { return out_setting; }

void uart_midi_cycle_out_type(void) { uart_midi_set_out_type((trs_type_t)((out_setting + 1) % TRS_TYPE_COUNT)); }

void uart_midi_get_stats(uart_midi_stats_t *out) { *out = stats; }
