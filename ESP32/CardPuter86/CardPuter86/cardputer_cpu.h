#ifndef CARDPUTER_CPU_H
#define CARDPUTER_CPU_H

#include <Arduino.h>

void cardputer_cpu_init(void);
uint8_t cardputer_cpu_profile(void);
bool cardputer_cpu_set_profile(uint8_t profile);
uint8_t cardputer_cpu_profile_count(void);
const char *cardputer_cpu_profile_label(uint8_t profile);
void cardputer_cpu_throttle(uint32_t instructions);

#endif
