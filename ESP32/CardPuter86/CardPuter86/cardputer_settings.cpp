#include "cardputer_settings.h"
#include <nvs.h>
#include <nvs_flash.h>

static bool post_sound_enabled = false;
static uint32_t sleep_timeout_seconds = 120;

void cardputer_settings_init(void) {
    post_sound_enabled = false;
    sleep_timeout_seconds = 120;
    if (nvs_flash_init() != ESP_OK) return;

    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t saved = 0;
    if (nvs_get_u8(handle, "post_sound", &saved) == ESP_OK) {
        post_sound_enabled = saved != 0;
    }
    uint32_t saved_sleep = 120;
    if (nvs_get_u32(handle, "sleep_sec", &saved_sleep) == ESP_OK &&
        saved_sleep <= 3600) {
        sleep_timeout_seconds = saved_sleep;
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

uint32_t cardputer_settings_sleep_timeout_seconds(void) {
    return sleep_timeout_seconds;
}

bool cardputer_settings_set_sleep_timeout_seconds(uint32_t seconds) {
    if (seconds > 3600) return false;
    sleep_timeout_seconds = seconds;

    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t result = nvs_set_u32(handle, "sleep_sec", seconds);
    const esp_err_t commit_result = result == ESP_OK ? nvs_commit(handle) : result;
    nvs_close(handle);
    return result == ESP_OK && commit_result == ESP_OK;
}
