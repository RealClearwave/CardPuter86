#include "cardputer_power.h"
#include "cardputer_rtc.h"
#include "cardputer_settings.h"
#include "hardware.h"
#include <M5Cardputer.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

static uint32_t last_activity_ms = 0;
static bool g0_was_pressed = false;

static void enter_deep_sleep(void) {
    auto &display = M5Cardputer.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextSize(1);
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.setCursor(8, 50);
    display.print("CardPuter86 sleeping");
    display.setCursor(8, 66);
    display.print("Press G0 to wake");
    delay(250);

    while (digitalRead(USER_BTN_PIN) == LOW) {
        M5Cardputer.update();
        delay(20);
    }

    cardputer_rtc_persist_now();
    display.setBrightness(0);
    display.sleep();

    rtc_gpio_deinit((gpio_num_t)USER_BTN_PIN);
    rtc_gpio_init((gpio_num_t)USER_BTN_PIN);
    rtc_gpio_set_direction((gpio_num_t)USER_BTN_PIN, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)USER_BTN_PIN);
    rtc_gpio_pulldown_dis((gpio_num_t)USER_BTN_PIN);

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BTN_PIN, 0);
    esp_deep_sleep_start();
}

void cardputer_power_init(void) {
    pinMode(USER_BTN_PIN, INPUT_PULLUP);
    last_activity_ms = millis();
}

void cardputer_power_note_activity(void) {
    last_activity_ms = millis();
}

void cardputer_power_poll(void) {
    const bool g0_pressed = digitalRead(USER_BTN_PIN) == LOW;
    if (g0_pressed && !g0_was_pressed) {
        g0_was_pressed = g0_pressed;
        enter_deep_sleep();
        return;
    }
    g0_was_pressed = g0_pressed;

    const uint32_t timeout_seconds = cardputer_settings_sleep_timeout_seconds();
    if (timeout_seconds == 0) return;
    if ((uint32_t)(millis() - last_activity_ms) >= timeout_seconds * 1000UL) {
        enter_deep_sleep();
    }
}
