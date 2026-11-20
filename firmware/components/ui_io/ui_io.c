#include "ui_io.h"

#include "board.h"
#include "bus.h"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "esp_timer.h"

#define PERIOD_US 10000

// A WS2812 takes its levels as a bit stream at 800 kHz: 0.3 us high is a 0, 0.9 us is a 1, 1.2 us a bit. The RMT
// channel runs at 10 MHz, so a tick is 0.1 us and a bit is 12 of them.
#define RGB_TICKS 12
#define RGB_ONE_HIGH 9  // the high half of a 1, and so the low half of a 0
// A quarter of full brightness is plenty for a status light: one WS2812 at full white is ~60 mA, and this keeps the
// 5 V rail and the dark stage alike calm.
#define RGB_LEVEL 64  // of 255

static button_t button;
static void (*handler)(button_event_t);
static volatile led_state_t led = LED_ADVERTISING;

static rmt_channel_handle_t rgb_chan;
static rmt_encoder_handle_t rgb_encoder;
static rmt_symbol_word_t rgb_bits[24];
static led_rgb_t rgb_sent;
static bool rgb_sent_valid;
static volatile uint32_t flash_until;  // ms, 0: no flash

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

// A colour at a level (0 or 255 today), scaled down to the cap above.
static led_rgb_t rgb_scaled(led_rgb_t c, uint8_t level) {
    uint32_t l = (uint32_t)level * RGB_LEVEL / 255;
    return (led_rgb_t){.r = (uint8_t)(c.r * l / 255), .g = (uint8_t)(c.g * l / 255), .b = (uint8_t)(c.b * l / 255)};
}

static void tick(void *arg) {
    const board_t *b = board();
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    button_event_t ev = button_update(&button, gpio_get_level(b->button) == 0, now);
    if (ev && handler) handler(ev);
    // A second press held at 2 s is the armed ESP-NOW toggle: white until the release that fires it.
    bool armed = button_armed(&button);
    bool flash = flash_until != 0 && (int32_t)(now - flash_until) < 0;
    if (!flash) flash_until = 0;
    uint8_t level = armed || flash || led_level(led, now, bus_last_activity_ms()) ? 255 : 0;
    if (b->led == GPIO_NUM_NC) return;
    led_rgb_t colour = armed || flash ? (led_rgb_t){255, 255, 255} : led_color(led);
    if (b->led_rgb)
        rgb_send(rgb_scaled(colour, level));
    else
        gpio_set_level(b->led, level);
}

void ui_start(void (*on_button)(button_event_t ev)) {
    handler = on_button;
    button_init(&button);
    if (board()->led != GPIO_NUM_NC && board()->led_rgb) rgb_init(board()->led);
    const esp_timer_create_args_t args = {.callback = tick, .name = "ui"};
    esp_timer_handle_t t;
    ESP_ERROR_CHECK(esp_timer_create(&args, &t));
    ESP_ERROR_CHECK(esp_timer_start_periodic(t, PERIOD_US));
    ui_flash(400);  // boot: white says the light itself works, before the state language starts
}

void ui_set_led(led_state_t s) { led = s; }

void ui_flash(uint32_t ms) { flash_until = (uint32_t)(esp_timer_get_time() / 1000) + ms; }
