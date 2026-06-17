#ifndef CARDPUTER_INPUT_H
#define CARDPUTER_INPUT_H

#include <Arduino.h>
#include <M5Cardputer.h>

enum CardputerVirtualKey : uint8_t {
    CARDPUTER_VK_UP,
    CARDPUTER_VK_DOWN,
    CARDPUTER_VK_LEFT,
    CARDPUTER_VK_RIGHT,
    CARDPUTER_VK_ESC,
    CARDPUTER_VK_DEL,
    CARDPUTER_VK_HOME,
    CARDPUTER_VK_AUTO,
};

bool cardputer_input_pressed(CardputerVirtualKey key);
bool cardputer_input_any_pressed(void);
uint8_t cardputer_input_function_index(void);
uint8_t cardputer_input_shift_symbol_index(void);

#endif
