#include "cardputer_power.h"
#include "cardputer_rtc.h"
#include "cardputer_settings.h"
#include "hardware.h"
#include <M5Cardputer.h>

static uint32_t last_activity_ms = 0;
static bool g0_was_pressed = false;
static bool g0_seen_released = false;
static uint32_t g0_press_started_ms = 0;

static bool g0_pressed(void) {
    return digitalRead(USER_BTN_PIN) == LOW;
}

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

static bool wait_for_g0_release(uint32_t timeout_ms) {
    const uint32_t start = millis();
    while (g0_pressed()) {
        M5Cardputer.update();
        if ((uint32_t)(millis() - start) >= timeout_ms) return false;
        delay(20);
    }
    g0_seen_released = true;
    return true;
}

static void enter_panel_sleep(void) {
    auto &display = M5Cardputer.Display;
    display.fillScreen(TFT_BLACK);
    display.setTextSize(1);
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.setCursor(8, 50);
    display.print("CardPuter86 sleeping");
    display.setCursor(8, 66);
    display.print("Press any key or G0");
    delay(250);

    if (!wait_for_g0_release(1500)) {
        display.fillScreen(TFT_BLACK);
        last_activity_ms = millis();
        return;
    }

    cardputer_rtc_persist_now();
    display_power_off();

    while (true) {
        M5Cardputer.update();
        if (g0_pressed() || M5Cardputer.Keyboard.isPressed()) break;
        delay(25);
    }

    display_power_on();
    last_activity_ms = millis();
    g0_was_pressed = g0_pressed();
    if (!g0_was_pressed) g0_seen_released = true;
    wait_for_g0_release(1500);
}

void cardputer_power_init(void) {
    pinMode(USER_BTN_PIN, INPUT_PULLUP);
    g0_seen_released = !g0_pressed();
    g0_was_pressed = g0_pressed();
    g0_press_started_ms = 0;
    last_activity_ms = millis();
}

void cardputer_power_note_activity(void) {
    last_activity_ms = millis();
}

void cardputer_power_poll(void) {
    const bool pressed = g0_pressed();
    if (!pressed) {
        g0_seen_released = true;
        g0_press_started_ms = 0;
    } else if (g0_seen_released && !g0_was_pressed) {
        g0_press_started_ms = millis();
    }

    if (pressed && g0_seen_released && g0_press_started_ms != 0 &&
        (uint32_t)(millis() - g0_press_started_ms) >= 800) {
        g0_was_pressed = true;
        enter_panel_sleep();
        return;
    }
    g0_was_pressed = pressed;

    const uint32_t timeout_seconds = cardputer_settings_sleep_timeout_seconds();
    if (timeout_seconds == 0) return;
    if ((uint32_t)(millis() - last_activity_ms) >= timeout_seconds * 1000UL) {
        enter_panel_sleep();
    }
}
