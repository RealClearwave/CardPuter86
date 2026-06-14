#include "cardputer_cpu.h"
#include <esp_timer.h>
#include <nvs.h>
#include <nvs_flash.h>

struct CpuProfile {
    const char *label;
    uint32_t clock_hz;
};

static const CpuProfile CPU_PROFILES[] = {
    {"4.77 MHz", 4770000},
    {"8 MHz", 8000000},
    {"10 MHz", 10000000},
    {"12 MHz", 12000000},
    {"Unlimited", 0},
};

static const uint8_t DEFAULT_PROFILE = 4;
static const uint32_t APPROX_CLOCKS_PER_INSTRUCTION = 4;
static uint8_t current_profile = DEFAULT_PROFILE;
static int64_t next_batch_deadline_us = 0;

void cardputer_cpu_init(void) {
    if (nvs_flash_init() != ESP_OK) return;
    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READONLY, &handle) != ESP_OK) return;
    uint8_t saved = DEFAULT_PROFILE;
    if (nvs_get_u8(handle, "cpu_profile", &saved) == ESP_OK &&
        saved < cardputer_cpu_profile_count()) {
        current_profile = saved;
    }
    nvs_close(handle);
}

uint8_t cardputer_cpu_profile(void) {
    return current_profile;
}

bool cardputer_cpu_set_profile(uint8_t profile) {
    if (profile >= cardputer_cpu_profile_count()) return false;
    current_profile = profile;
    next_batch_deadline_us = 0;

    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t result = nvs_set_u8(handle, "cpu_profile", profile);
    const esp_err_t commit_result = result == ESP_OK ? nvs_commit(handle) : result;
    nvs_close(handle);
    return result == ESP_OK && commit_result == ESP_OK;
}

uint8_t cardputer_cpu_profile_count(void) {
    return sizeof(CPU_PROFILES) / sizeof(CPU_PROFILES[0]);
}

const char *cardputer_cpu_profile_label(uint8_t profile) {
    if (profile >= cardputer_cpu_profile_count()) profile = DEFAULT_PROFILE;
    return CPU_PROFILES[profile].label;
}

void cardputer_cpu_throttle(uint32_t instructions) {
    const uint32_t clock_hz = CPU_PROFILES[current_profile].clock_hz;
    if (clock_hz == 0) {
        next_batch_deadline_us = 0;
        return;
    }

    const int64_t now = esp_timer_get_time();
    if (next_batch_deadline_us == 0 || now - next_batch_deadline_us > 100000) {
        next_batch_deadline_us = now;
    }
    next_batch_deadline_us +=
        ((int64_t)instructions * APPROX_CLOCKS_PER_INSTRUCTION * 1000000LL) /
        clock_hz;

    const int64_t wait_us = next_batch_deadline_us - esp_timer_get_time();
    if (wait_us > 0) delayMicroseconds((uint32_t)wait_us);
}
