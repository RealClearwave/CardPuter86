#ifndef CARDPUTER_POWER_H
#define CARDPUTER_POWER_H

#include <Arduino.h>

void cardputer_power_init(void);
void cardputer_power_note_activity(void);
void cardputer_power_poll(void);

#endif
