#ifndef CARDPUTER_SETTINGS_H
#define CARDPUTER_SETTINGS_H

#include <Arduino.h>

void cardputer_settings_init(void);
bool cardputer_settings_post_sound_enabled(void);
bool cardputer_settings_set_post_sound_enabled(bool enabled);
uint32_t cardputer_settings_sleep_timeout_seconds(void);
bool cardputer_settings_set_sleep_timeout_seconds(uint32_t seconds);

#endif
