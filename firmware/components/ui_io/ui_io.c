#include "ui_io.h"

#include <string.h>

#include "board.h"
#include "bus.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/rmt_tx.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

#define PERIOD_US 10000

// A WS2812 takes its levels as a bit stream at 800 kHz: 0.3 us high is a 0, 0.9 us is a 1, 1.2 us a bit. The RMT
// channel runs at 10 MHz, so a tick is 0.1 us and a bit is 12 of them.
#define RGB_TICKS 12
#define RGB_ONE_HIGH 9  // the high half of a 1, and so the low half of a 0
// About 9% of full brightness is plenty for a status light: a WS2812 at full white is ~60 mA, and 24/255 keeps the
// 5 V rail and the dark stage alike calm. Raise it for daylight, not for taste.
#define RGB_LEVEL 24  // of 255
// White lights all three channels at once, so at the link cap it reads far brighter than one channel. The white
// flashes (boot, short press, the armed hold) get a third of it: the same total drive as a link colour at full.
#define RGB_WHITE_LEVEL 8  // of 255

static button_t buttons[PANEL_KEYS];
static bool
    swallow[PANEL_KEYS];  // a press that began before ui_start (the hold that woke the box): wait for its release
static int n_buttons;
static gpio_num_t pins[PANEL_KEYS];
static void (*handler)(int key, button_event_t ev);
static volatile led_state_t led = LED_ADVERTISING;
static volatile bool dark;

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static panel_state_t panel;  // from main (ui_set_panel), plus what only this file knows: hold times, sweep, bar
static uint32_t bar_until, sweep_from;

static rmt_channel_handle_t rgb_chan;
static rmt_encoder_handle_t rgb_encoder;
static rmt_symbol_word_t rgb_bits[24];
static led_rgb_t rgb_sent;
static bool rgb_sent_valid;
static volatile uint32_t flash_until;  // ms, 0: no flash

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

static void panel_leds_init(const board_t *b) {
    // XTAL clock: steady while frequency scaling moves the APB clock (power mode 1).
    ledc_timer_config_t t = {.speed_mode = LEDC_LOW_SPEED_MODE,
                             .duty_resolution = LEDC_TIMER_8_BIT,
                             .timer_num = LEDC_TIMER_0,
                             .freq_hz = 2000,
                             .clk_cfg = LEDC_USE_XTAL_CLK};
    ESP_ERROR_CHECK(ledc_timer_config(&t));
    for (int k = 0; k < PANEL_KEYS; k++) {
        ledc_channel_config_t c = {.gpio_num = b->panel_led[k],
                                   .speed_mode = LEDC_LOW_SPEED_MODE,
                                   .channel = (ledc_channel_t)k,
                                   .timer_sel = LEDC_TIMER_0,
                                   .duty = 0};
        ESP_ERROR_CHECK(ledc_channel_config(&c));
    }
}

static void panel_show(uint32_t now) {
    uint8_t level[PANEL_KEYS] = {0};
    if (!dark) {
        panel_state_t s;
        taskENTER_CRITICAL(&lock);
        s = panel;
        taskEXIT_CRITICAL(&lock);
        for (int k = 0; k < PANEL_KEYS; k++) s.held_ms[k] = swallow[k] ? 0 : button_held_ms(&buttons[k], now);
        s.bar_until = bar_until;
        s.sweep_from = sweep_from;
        s.last_activity_ms = bus_last_activity_ms();
        panel_leds(&s, now, level);
    }
    const board_t *b = board();
    for (int k = 0; k < PANEL_KEYS; k++) {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)k, level[k] * b->panel_led_max[k] / 255);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, (ledc_channel_t)k);
    }
}

// One bit per symbol: high for the one's width, then low for the rest of the 1.2 us.
static void rgb_send(led_rgb_t c) {
    if (rgb_sent_valid && c.r == rgb_sent.r && c.g == rgb_sent.g && c.b == rgb_sent.b) return;
    // WS2812 order: green, then red, then blue, most significant bit first.
    uint32_t v = ((uint32_t)c.g << 16) | ((uint32_t)c.r << 8) | c.b;
    for (int i = 0; i < 24; i++) {
        bool one = (v >> (23 - i)) & 1u;
        rgb_bits[i].level0 = 1;
        rgb_bits[i].duration0 = one ? RGB_ONE_HIGH : RGB_TICKS - RGB_ONE_HIGH;
        rgb_bits[i].level1 = 0;
        rgb_bits[i].duration1 = one ? RGB_TICKS - RGB_ONE_HIGH : RGB_ONE_HIGH;
    }
    const rmt_transmit_config_t tx = {.loop_count = 0};
    if (rmt_transmit(rgb_chan, rgb_encoder, rgb_bits, sizeof rgb_bits, &tx) != ESP_OK) return;  // again next tick
    rmt_tx_wait_all_done(rgb_chan, 20);   // ~30 us of bits: over long before the next 10 ms tick
    rgb_sent = c, rgb_sent_valid = true;  // only now: a refused frame must be retried
}

static void rgb_init(gpio_num_t gpio) {
    const rmt_tx_channel_config_t chan = {
        .gpio_num = gpio,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
        .mem_block_symbols = 48,  // a whole block: 24 bits fit with room to spare
        .trans_queue_depth = 1,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&chan, &rgb_chan));
    const rmt_copy_encoder_config_t copy = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy, &rgb_encoder));
    ESP_ERROR_CHECK(rmt_enable(rgb_chan));
}

// A colour scaled to a per-channel cap (0..255).
static led_rgb_t rgb_scaled(led_rgb_t c, uint8_t cap) {
    return (led_rgb_t){
        .r = (uint8_t)(c.r * cap / 255), .g = (uint8_t)(c.g * cap / 255), .b = (uint8_t)(c.b * cap / 255)};
}

static void tick(void *arg) {
    const board_t *b = board();
    uint32_t now = now_ms();
    for (int k = 0; k < n_buttons; k++) {
        bool pressed = gpio_get_level(pins[k]) == 0;
        if (swallow[k]) {
            if (!pressed) swallow[k] = false;
            continue;
        }
        button_event_t ev = button_update(&buttons[k], pressed, now);
        if (ev && handler) handler(k, ev);
    }
    if (b->panel) {
        panel_show(now);
        return;
    }
    if (b->led == GPIO_NUM_NC) return;
    bool flash = flash_until != 0 && (int32_t)(now - flash_until) < 0;
    if (!flash) flash_until = 0;
    // A second press held at 2 s is the armed ESP-NOW toggle: white until the release that fires it. Only the
    // addressable LED shows the flash; a plain one keeps its pattern.
    bool white = b->led_rgb && !dark && (button_armed(&buttons[0]) || flash);
    bool on = !dark && (white || led_level(led, now, bus_last_activity_ms()));
    if (!b->led_rgb) {
        gpio_set_level(b->led, on);
        return;
    }
    if (white)
        rgb_send(rgb_scaled((led_rgb_t){255, 255, 255}, RGB_WHITE_LEVEL));
    else
        rgb_send(rgb_scaled(led_color(led), on ? RGB_LEVEL : 0));
}

void ui_start(void (*on_button)(int key, button_event_t ev)) {
    const board_t *b = board();
    handler = on_button;
    if (b->panel) {
        n_buttons = PANEL_KEYS;
        memcpy(pins, b->panel_button, sizeof pins);
        uint64_t mask = 0;
        for (int k = 0; k < PANEL_KEYS; k++) mask |= 1ULL << pins[k];
        gpio_config_t in = {.pin_bit_mask = mask, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
        ESP_ERROR_CHECK(gpio_config(&in));
        panel_leds_init(b);
        sweep_from = now_ms();
    } else if (b->button != GPIO_NUM_NC) {
        n_buttons = 1;
        pins[0] = b->button;  // board_init set it up
    }
    for (int k = 0; k < n_buttons; k++) {
        button_init(&buttons[k]);
        buttons[k].fast_double = b->panel;  // panic at the second press (one button: rev A's gestures as they were)
        swallow[k] = gpio_get_level(pins[k]) == 0;
    }
    if (b->led_rgb) {
        rgb_init(b->led);
        ui_flash(400);  // boot: white says the light itself works, before the state language starts
    }
    const esp_timer_create_args_t args = {.callback = tick, .name = "ui"};
    esp_timer_handle_t t;
    ESP_ERROR_CHECK(esp_timer_create(&args, &t));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t, PERIOD_US));
}

void ui_set_led(led_state_t s) { led = s; }

void ui_set_panel(const panel_state_t *s) {
    taskENTER_CRITICAL(&lock);
    panel = *s;
    taskEXIT_CRITICAL(&lock);
}

void ui_show_battery(void) { bar_until = now_ms() + PANEL_BAR_MS; }

void ui_dark(void) { dark = true; }

bool ui_button_down(int key) { return key < n_buttons && gpio_get_level(pins[key]) == 0; }

void ui_flash(uint32_t ms) { flash_until = now_ms() + ms; }
