// Cardputer keyboard driver using M5Cardputer.Keyboard API.
// Fills keymap[256] with PS/2 scancodes for the emulator.

#include "cardputer_kbd.h"
#include "hardware.h"
#include "gbConfig.h"
#include "gbGlobals.h"
#include "keys.h"
#include "PS2KeyCode.h"
#include <M5Cardputer.h>
#include <Arduino.h>

static uint8_t osd_cache[256];
static bool osd_cache_inited = false;

void cardputer_kbd_init(void) {
    // Keyboard already initialized by M5Cardputer.begin(true)
    memset((void *)keymap, 1, sizeof(keymap));
    memset(osd_cache, 1, sizeof(osd_cache));
    osd_cache_inited = true;
}

void cardputer_kbd_scan(void) { /* no-op */ }

// char → PS/2 scancode
static uint8_t char_to_sc(char c) {
    // accept both upper and lower case
    if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
    switch (c) {
        case 'a': return PS2_KC_A;
        case 'b': return PS2_KC_B;
        case 'c': return PS2_KC_C;
        case 'd': return PS2_KC_D;
        case 'e': return PS2_KC_E;
        case 'f': return PS2_KC_F;
        case 'g': return PS2_KC_G;
        case 'h': return PS2_KC_H;
        case 'i': return PS2_KC_I;
        case 'j': return PS2_KC_J;
        case 'k': return PS2_KC_K;
        case 'l': return PS2_KC_L;
        case 'm': return PS2_KC_M;
        case 'n': return PS2_KC_N;
        case 'o': return PS2_KC_O;
        case 'p': return PS2_KC_P;
        case 'q': return PS2_KC_Q;
        case 'r': return PS2_KC_R;
        case 's': return PS2_KC_S;
        case 't': return PS2_KC_T;
        case 'u': return PS2_KC_U;
        case 'v': return PS2_KC_V;
        case 'w': return PS2_KC_W;
        case 'x': return PS2_KC_X;
        case 'y': return PS2_KC_Y;
        case 'z': return PS2_KC_Z;
        case '0': return PS2_KC_0;
        case '1': return PS2_KC_1;
        case '2': return PS2_KC_2;
        case '3': return PS2_KC_3;
        case '4': return PS2_KC_4;
        case '5': return PS2_KC_5;
        case '6': return PS2_KC_6;
        case '7': return PS2_KC_7;
        case '8': return PS2_KC_8;
        case '9': return PS2_KC_9;
        case '-': case '_': return PS2_KC_MINUS;
        case '=': case '+': return PS2_KC_EQUAL;
        case '[': case '{': return PS2_KC_OPEN_SQ;
        case ']': case '}': return PS2_KC_CLOSE_SQ;
        case '\\':case '|': return PS2_KC_BACK;
        case ';': case ':': return PS2_KC_SEMI;
        case '\'':case '"': return PS2_KC_APOS;
        case ',': case '<': return PS2_KC_COMMA;
        case '.': case '>': return PS2_KC_DOT;
        case '/': case '?': return PS2_KC_DIV;
        case '`': case '~': return PS2_KC_SINGLE;
        default: return 0;
    }
}

void cardputer_kbd_fill_keymap(void) {
    memset((void *)keymap, 1, sizeof(keymap));

    if (!M5Cardputer.Keyboard.isPressed()) return;

    Keyboard_Class::KeysState s = M5Cardputer.Keyboard.keysState();

    // Modifiers
    if (s.shift) keymap[PS2_KC_L_SHIFT] = 0;
    if (s.ctrl)  keymap[PS2_KC_CTRL]    = 0;
    if (s.alt)   keymap[PS2_KC_ALT]     = 0;

    // Special keys
    if (s.enter) keymap[PS2_KC_ENTER]   = 0;
    if (s.del)   keymap[PS2_KC_BS]      = 0;
    if (s.tab)   keymap[PS2_KC_TAB]     = 0;
    if (s.space) keymap[PS2_KC_SPACE]   = 0;

    // Printable chars from state.word
    for (char c : s.word) {
        uint8_t sc = char_to_sc(c);
        if (sc) keymap[sc] = 0;
    }

    // FN layer overrides — scan with isKeyPressed for non-printable combos
    // The library tracks FN state internally in state.fn
    // When FN is held, the keysState() already reports shifted values for F-keys etc.
    // But the library doesn't differentiate arrow keys. We fake them below:
    if (s.fn) {
        if (M5Cardputer.Keyboard.isKeyPressed('/')) {
            keymap[KEY_CURSOR_RIGHT] = 0;
        }
        if (M5Cardputer.Keyboard.isKeyPressed('.')) {
            keymap[KEY_CURSOR_DOWN] = 0;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(',')) {
            keymap[KEY_CURSOR_LEFT] = 0;
        }
        if (M5Cardputer.Keyboard.isKeyPressed(';')) {
            keymap[KEY_CURSOR_UP] = 0;
        }
        // FN+1..0 → F1..F10
        if (M5Cardputer.Keyboard.isKeyPressed('1')) keymap[PS2_KC_F1] = 0;
        if (M5Cardputer.Keyboard.isKeyPressed('2')) keymap[PS2_KC_F2] = 0;
        if (M5Cardputer.Keyboard.isKeyPressed('3')) keymap[PS2_KC_F3] = 0;
        if (M5Cardputer.Keyboard.isKeyPressed('4')) keymap[PS2_KC_F4] = 0;
        if (M5Cardputer.Keyboard.isKeyPressed('5')) keymap[PS2_KC_F5] = 0;
        if (M5Cardputer.Keyboard.isKeyPressed('6')) keymap[PS2_KC_F6] = 0;
        if (M5Cardputer.Keyboard.isKeyPressed('7')) keymap[PS2_KC_F7] = 0;
        if (M5Cardputer.Keyboard.isKeyPressed('8')) keymap[PS2_KC_F8] = 0;
        if (M5Cardputer.Keyboard.isKeyPressed('9')) keymap[PS2_KC_F9] = 0;
        if (M5Cardputer.Keyboard.isKeyPressed('0')) keymap[PS2_KC_F10] = 0;
        // FN + backtick → ESC
        if (M5Cardputer.Keyboard.isKeyPressed('`')) keymap[PS2_KC_ESC] = 0;
        // FN + backspace → DEL
        keymap[PS2_KC_DELETE] = 0; // actually this needs proper detection
    }
}

boolean checkAndCleanKey(uint8_t scancode) {
    if (!osd_cache_inited) { memset(osd_cache, 1, 256); osd_cache_inited = true; }
    bool cur  = (keymap[scancode] == 0);
    bool prev = (osd_cache[scancode] == 0);
    osd_cache[scancode] = keymap[scancode];
    return (cur && !prev);
}

boolean checkKey(uint8_t scancode) {
    return (keymap[scancode] == 0);
}
