#include "cardputer_input.h"
#include "hardware.h"

static bool previous_virtual_down[10];
static bool previous_char_down[128];

static bool key_down(char key) {
    return M5Cardputer.Keyboard.isKeyPressed(key);
}

bool cardputer_input_pressed(CardputerVirtualKey key) {
    const Keyboard_Class::KeysState state = M5Cardputer.Keyboard.keysState();
    switch (key) {
        case CARDPUTER_VK_UP:
            return state.fn && key_down(';');
        case CARDPUTER_VK_DOWN:
            return state.fn && key_down('.');
        case CARDPUTER_VK_LEFT:
            return state.fn && key_down(',');
        case CARDPUTER_VK_RIGHT:
            return state.fn && key_down('/');
        case CARDPUTER_VK_ESC:
            return state.fn && key_down('`');
        case CARDPUTER_VK_DEL:
            return state.fn && state.del;
        case CARDPUTER_VK_HOME:
            return state.fn && key_down('\'');
        case CARDPUTER_VK_AUTO:
            return state.fn && state.space;
        case CARDPUTER_VK_ENTER:
            return state.enter;
        case CARDPUTER_VK_BACKSPACE:
            return state.del;
    }
    return false;
}

bool cardputer_input_consume(CardputerVirtualKey key) {
    const uint8_t index = (uint8_t)key;
    if (index >= sizeof(previous_virtual_down)) return false;
    const bool down = cardputer_input_pressed(key);
    const bool fired = down && !previous_virtual_down[index];
    previous_virtual_down[index] = down;
    return fired;
}

bool cardputer_input_consume_char(char key) {
    const uint8_t index = (uint8_t)key;
    if (index >= sizeof(previous_char_down)) return false;
    const bool down = key_down(key);
    const bool fired = down && !previous_char_down[index];
    previous_char_down[index] = down;
    return fired;
}

char cardputer_input_consume_digit(void) {
    for (char key = '0'; key <= '9'; key++) {
        if (cardputer_input_consume_char(key)) return key;
    }
    return 0;
}

bool cardputer_input_any_pressed(void) {
    return M5Cardputer.Keyboard.isPressed();
}

uint8_t cardputer_input_function_index(void) {
    const Keyboard_Class::KeysState state = M5Cardputer.Keyboard.keysState();
    if (!state.fn) return 0xFF;
    static const char keys[] = "1234567890-=";
    for (uint8_t i = 0; i < 12; i++) {
        if (key_down(keys[i])) return i;
    }
    return 0xFF;
}

uint8_t cardputer_input_shift_symbol_index(void) {
    const Keyboard_Class::KeysState state = M5Cardputer.Keyboard.keysState();
    if (!state.shift) return 0xFF;
    static const char keys[] = "`1234567890-=";
    for (uint8_t i = 0; i < 13; i++) {
        if (key_down(keys[i])) return i;
    }
    return 0xFF;
}
