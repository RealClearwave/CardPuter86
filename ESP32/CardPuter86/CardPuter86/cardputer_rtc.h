#ifndef CARDPUTER_RTC_H
#define CARDPUTER_RTC_H

#include <Arduino.h>

struct CardputerRtcTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
};

void cardputer_rtc_init(void);
CardputerRtcTime cardputer_rtc_now(void);
bool cardputer_rtc_set(const CardputerRtcTime &time);
bool cardputer_rtc_persist_now(void);
void cardputer_rtc_select_register(uint8_t reg);
uint8_t cardputer_rtc_read_selected(void);
bool cardputer_rtc_parse_yyyymmddhhmmss(const char *text, CardputerRtcTime *out);
void cardputer_rtc_format(const CardputerRtcTime &time, char *buffer, size_t size);

#endif
