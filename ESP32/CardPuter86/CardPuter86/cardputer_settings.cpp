#include "cardputer_settings.h"
#include <nvs.h>
#include <nvs_flash.h>

static bool post_sound_enabled = false;
static uint32_t sleep_timeout_seconds = 120;
static uint8_t usb_mode = 0; // 0 charge only, 1 CDC serial, 2 USB disk
static bool sleep_led_enabled = true;

void cardputer_settings_init(void) {
    post_sound_enabled = false;
    sleep_timeout_seconds = 120;
    usb_mode = 0;
    sleep_led_enabled = true;
    if (nvs_flash_init() != ESP_OK) return;

    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t saved = 0;
    if (nvs_get_u8(handle, "post_sound", &saved) == ESP_OK) {
        post_sound_enabled = saved != 0;
    }
    uint32_t saved_sleep = 120;
    if (nvs_get_u32(handle, "sleep_sec", &saved_sleep) == ESP_OK &&
        (saved_sleep == 0 || saved_sleep == 30 || saved_sleep == 120 ||
         saved_sleep == 300 || saved_sleep == 600)) {
        sleep_timeout_seconds = saved_sleep;
    }
    uint8_t saved_usb = 0;
    if (nvs_get_u8(handle, "usb_mode", &saved_usb) == ESP_OK && saved_usb <= 2) {
        usb_mode = saved_usb;
    }
    uint8_t saved_sleep_led = 1;
    if (nvs_get_u8(handle, "sleep_led", &saved_sleep_led) == ESP_OK) {
        sleep_led_enabled = saved_sleep_led != 0;
    }
    nvs_close(handle);
}

bool cardputer_settings_post_sound_enabled(void) {
    return post_sound_enabled;
}

bool cardputer_settings_set_post_sound_enabled(bool enabled) {
    post_sound_enabled = enabled;

    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t result = nvs_set_u8(handle, "post_sound", enabled ? 1 : 0);
    const esp_err_t commit_result = result == ESP_OK ? nvs_commit(handle) : result;
    nvs_close(handle);
    return result == ESP_OK && commit_result == ESP_OK;
}

bool cardputer_settings_sleep_led_enabled(void) {
    return sleep_led_enabled;
}

bool cardputer_settings_set_sleep_led_enabled(bool enabled) {
    sleep_led_enabled = enabled;

    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t result = nvs_set_u8(handle, "sleep_led", enabled ? 1 : 0);
    const esp_err_t commit_result = result == ESP_OK ? nvs_commit(handle) : result;
    nvs_close(handle);
    return result == ESP_OK && commit_result == ESP_OK;
}

uint8_t cardputer_settings_usb_mode(void) {
    return usb_mode;
}

bool cardputer_settings_set_usb_mode(uint8_t mode) {
    if (mode > 2) return false;
    usb_mode = mode;

    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t result = nvs_set_u8(handle, "usb_mode", mode);
    const esp_err_t commit_result = result == ESP_OK ? nvs_commit(handle) : result;
    nvs_close(handle);
    return result == ESP_OK && commit_result == ESP_OK;
}

uint32_t cardputer_settings_sleep_timeout_seconds(void) {
    return sleep_timeout_seconds;
}

bool cardputer_settings_set_sleep_timeout_seconds(uint32_t seconds) {
    if (!(seconds == 0 || seconds == 30 || seconds == 120 ||
          seconds == 300 || seconds == 600)) {
        return false;
    }
    sleep_timeout_seconds = seconds;

    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t result = nvs_set_u32(handle, "sleep_sec", seconds);
    const esp_err_t commit_result = result == ESP_OK ? nvs_commit(handle) : result;
    nvs_close(handle);
    return result == ESP_OK && commit_result == ESP_OK;
}
