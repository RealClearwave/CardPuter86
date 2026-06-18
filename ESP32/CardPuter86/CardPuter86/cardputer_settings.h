#ifndef CARDPUTER_SETTINGS_H
#define CARDPUTER_SETTINGS_H

#include <Arduino.h>

void cardputer_settings_init(void);
uint32_t cardputer_settings_sleep_timeout_seconds(void);
bool cardputer_settings_set_sleep_timeout_seconds(uint32_t seconds);
uint8_t cardputer_settings_usb_mode(void);
bool cardputer_settings_set_usb_mode(uint8_t mode);
bool cardputer_settings_sleep_led_enabled(void);
bool cardputer_settings_set_sleep_led_enabled(bool enabled);
bool cardputer_settings_audio_enabled(void);
bool cardputer_settings_set_audio_enabled(bool enabled);
bool cardputer_settings_com1_enabled(void);
bool cardputer_settings_set_com1_enabled(bool enabled);

#endif
