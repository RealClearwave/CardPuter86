#include "cardputer_power.h"
#include "cardputer_rtc.h"
#include "cardputer_settings.h"
#include "hardware.h"
#include <M5Cardputer.h>
#include <esp_sleep.h>

static uint32_t last_activity_ms = 0;

static void display_power_off(void) {
    auto &display = M5Cardputer.Display;
    display.fillScreen(TFT_BLACK);
    display.setBrightness(0);
    display.sleep();
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);
}

static void display_power_on(void) {
    auto &display = M5Cardputer.Display;
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    display.wakeup();
    display.setBrightness(128);
    display.fillScreen(TFT_BLACK);
}

static void enter_light_sleep(void) {
    auto &display = M5Cardputer.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextSize(1);
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.setCursor(8, 50);
    display.print("CardPuter86 sleeping");
    display.setCursor(8, 66);
    display.print("Press any key to wake");
    delay(250);

    cardputer_rtc_persist_now();
    display_power_off();

    while (true) {
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_sleep_enable_timer_wakeup(100000ULL);
        esp_light_sleep_start();
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isPressed()) break;
    }

    display_power_on();
    last_activity_ms = millis();
}

void cardputer_power_init(void) {
    pinMode(USER_BTN_PIN, INPUT_PULLUP);
    last_activity_ms = millis();
}

void cardputer_power_note_activity(void) {
    last_activity_ms = millis();
}

void cardputer_power_poll(void) {
    const uint32_t timeout_seconds = cardputer_settings_sleep_timeout_seconds();
    if (timeout_seconds == 0) return;
    if ((uint32_t)(millis() - last_activity_ms) >= timeout_seconds * 1000UL) {
        enter_light_sleep();
    }
}
