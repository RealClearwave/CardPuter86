#include "cardputer_rtc.h"
#include <esp_sleep.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

static const int64_t DEFAULT_EPOCH = 441763200LL; // 1984-01-01 00:00:00 UTC
static uint8_t rtc_selected_register = 0;

static bool is_leap(uint16_t year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static uint8_t days_in_month(uint16_t year, uint8_t month) {
    static const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month == 2 && is_leap(year)) return 29;
    if (month < 1 || month > 12) return 31;
    return days[month - 1];
}

static int64_t to_epoch(const CardputerRtcTime &time) {
    int64_t days = 0;
    for (uint16_t y = 1970; y < time.year; y++) days += is_leap(y) ? 366 : 365;
    for (uint8_t m = 1; m < time.month; m++) days += days_in_month(time.year, m);
    days += time.day - 1;
    return days * 86400LL + time.hour * 3600LL + time.minute * 60LL + time.second;
}

static CardputerRtcTime from_epoch(int64_t epoch) {
    if (epoch < 0) epoch = 0;
    int64_t days = epoch / 86400LL;
    uint32_t rem = epoch % 86400LL;
    CardputerRtcTime time = {};
    time.hour = rem / 3600;
    rem %= 3600;
    time.minute = rem / 60;
    time.second = rem % 60;
    time.year = 1970;
    while (true) {
        const uint16_t year_days = is_leap(time.year) ? 366 : 365;
        if (days < year_days) break;
        days -= year_days;
        time.year++;
    }
    time.month = 1;
    while (true) {
        const uint8_t month_days = days_in_month(time.year, time.month);
        if (days < month_days) break;
        days -= month_days;
        time.month++;
    }
    time.day = days + 1;
    return time;
}

static uint8_t to_bcd(uint8_t value) {
    return ((value / 10) << 4) | (value % 10);
}

void cardputer_rtc_init(void) {
    int64_t saved_epoch = DEFAULT_EPOCH;
    if (nvs_flash_init() != ESP_OK) return;
    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READONLY, &handle) == ESP_OK) {
        nvs_get_i64(handle, "rtc_epoch", &saved_epoch);
        nvs_close(handle);
    }

    timeval now = {};
    gettimeofday(&now, nullptr);
    if (now.tv_sec < 315532800LL) {
        timeval restored = {};
        restored.tv_sec = saved_epoch;
        restored.tv_usec = 0;
        settimeofday(&restored, nullptr);
    }
}

CardputerRtcTime cardputer_rtc_now(void) {
    timeval now = {};
    gettimeofday(&now, nullptr);
    if (now.tv_sec < 0) now.tv_sec = DEFAULT_EPOCH;
    return from_epoch(now.tv_sec);
}

bool cardputer_rtc_set(const CardputerRtcTime &time) {
    const int64_t epoch = to_epoch(time);
    timeval now = {};
    now.tv_sec = epoch;
    now.tv_usec = 0;
    settimeofday(&now, nullptr);

    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t result = nvs_set_i64(handle, "rtc_epoch", epoch);
    const esp_err_t commit_result = result == ESP_OK ? nvs_commit(handle) : result;
    nvs_close(handle);
    return result == ESP_OK && commit_result == ESP_OK;
}

bool cardputer_rtc_persist_now(void) {
    const int64_t epoch = to_epoch(cardputer_rtc_now());
    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return false;
    const esp_err_t result = nvs_set_i64(handle, "rtc_epoch", epoch);
    const esp_err_t commit_result = result == ESP_OK ? nvs_commit(handle) : result;
    nvs_close(handle);
    return result == ESP_OK && commit_result == ESP_OK;
}

void cardputer_rtc_select_register(uint8_t reg) {
    rtc_selected_register = reg & 0x7F;
}

uint8_t cardputer_rtc_read_selected(void) {
    const CardputerRtcTime time = cardputer_rtc_now();
    switch (rtc_selected_register) {
        case 0x00: return to_bcd(time.second);
        case 0x02: return to_bcd(time.minute);
        case 0x04: return to_bcd(time.hour);
        case 0x07: return to_bcd(time.day);
        case 0x08: return to_bcd(time.month);
        case 0x09: return to_bcd(time.year % 100);
        case 0x0A: return 0x26;
        case 0x0B: return 0x02; // 24-hour, BCD
        case 0x0C: return 0x00;
        case 0x0D: return 0x80;
        case 0x32: return to_bcd(time.year / 100);
        default: return 0x00;
    }
}

bool cardputer_rtc_parse_yyyymmddhhmmss(const char *text, CardputerRtcTime *out) {
    if (!text || !out || strlen(text) != 14) return false;
    for (uint8_t i = 0; i < 14; i++) {
        if (text[i] < '0' || text[i] > '9') return false;
    }
    CardputerRtcTime time = {};
    time.year = (text[0] - '0') * 1000 + (text[1] - '0') * 100 +
                (text[2] - '0') * 10 + (text[3] - '0');
    time.month = (text[4] - '0') * 10 + (text[5] - '0');
    time.day = (text[6] - '0') * 10 + (text[7] - '0');
    time.hour = (text[8] - '0') * 10 + (text[9] - '0');
    time.minute = (text[10] - '0') * 10 + (text[11] - '0');
    time.second = (text[12] - '0') * 10 + (text[13] - '0');
    if (time.year < 1980 || time.year > 2099) return false;
    if (time.month < 1 || time.month > 12) return false;
    if (time.day < 1 || time.day > days_in_month(time.year, time.month)) return false;
    if (time.hour > 23 || time.minute > 59 || time.second > 59) return false;
    *out = time;
    return true;
}

void cardputer_rtc_format(const CardputerRtcTime &time, char *buffer, size_t size) {
    if (!buffer || size == 0) return;
    snprintf(buffer, size, "%04u-%02u-%02u %02u:%02u:%02u",
             time.year, time.month, time.day, time.hour, time.minute, time.second);
}
