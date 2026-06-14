#ifndef CARDPUTER_GUEST_MEMORY_H
#define CARDPUTER_GUEST_MEMORY_H

#include <Arduino.h>

bool guest_memory_init(void);
bool guest_memory_set_512k_enabled(bool enabled);
bool guest_memory_512k_enabled(void);
uint32_t guest_memory_size(void);
void guest_memory_clear(void);
uint8_t guest_memory_read(uint32_t address);
void guest_memory_write(uint32_t address, uint8_t value);

#endif
