// CardPuter86 - 8086 PC emulator for M5Stack Cardputer
//Original: ESP32TinyFake86 by ackerman (TTGO VGA32)
//Cardputer port: 8086 PC emulator on ESP32-S3 with ST7789 TFT

#include <Arduino.h>
#include "gbConfig.h"
#include "gbGlobals.h"
#include "fake86.h"
#include "hardware.h"
#include "driver/timer.h"
#include "soc/timer_group_struct.h"
#include "input.h"
#include "i8253.h"
#include "i8259.h"
#include "i8237.h"
#include "cpu.h"
#include "render.h"
#include "video.h"
#include "timing.h"
#include "disk.h"
#include "PS2KeyCode.h"
#include "keys.h"
#include "dataFlash/com/comdigger.h"
#include "dataFlash/gbcom.h"
#include "gb_sdl_font8x8.h"
#include "osd.h"

// Cardputer drivers
#include "cardputer_display.h"
#include "cardputer_kbd.h"
#include "cardputer_speaker.h"
#include "cardputer_storage.h"

// ===============================================
// Global variables (defined here, extern in gbGlobals.h)
// ===============================================
unsigned char gb_invert_color = 0;
unsigned char gb_silence = 0;

unsigned char gb_delay_tick_cpu_milis = use_lib_delay_tick_cpu_milis;
unsigned char gb_auto_delay_cpu = use_lib_delay_tick_cpu_auto;
unsigned char gb_vga_poll_milis = use_lib_vga_poll_milis;
unsigned char gb_keyboard_poll_milis = use_lib_keyboard_poll_milis;
unsigned char gb_timers_poll_milis = use_lib_timers_poll_milis;

unsigned char gb_frec_speaker_low = 0;
unsigned char gb_frec_speaker_high = 0;
unsigned char gb_cont_frec_speaker = 0;
volatile int gb_frecuencia01 = 0;
volatile int gb_volumen01 = 0;

unsigned char gb_use_remap_cartdridge = 0;
unsigned char gb_use_snarare_madmix = 0;

unsigned char gb_font_8x8 = 1;

unsigned int lasttick;
unsigned char gb_reset = 0;
unsigned char gb_id_cur_com = 0;

unsigned char tiempo_vga = 0;
unsigned int jj_ini_cpu, jj_end_cpu, jj_ini_vga, jj_end_vga;
unsigned int gb_cpu_timer_before, gb_cpu_timer_cur;
unsigned int gb_max_cpu_ticks, gb_min_cpu_ticks, gb_cur_cpu_ticks;
unsigned int gb_max_vga_ticks, gb_min_vga_ticks, gb_cur_vga_ticks;
unsigned long tickgap = 0;
unsigned char port3da = 0;

unsigned char gb_key_cur[80];
unsigned char gb_key_before[80];

// CGA palette (raw 6-bit VGA color values)
const unsigned char palettecga[16] = {
    0x00, 0xAA, 0x00, 0xAA,
    0x00, 0xAA, 0x00, 0xAA,
    0x55, 0xFF, 0x55, 0xFF,
    0x55, 0xFF, 0x55, 0xFF
};

unsigned char gbKeepAlive = 0;

#define uint8_t unsigned char

unsigned char vidmode = 5;
unsigned char gb_force_set_cga = 0;
unsigned char gb_force_load_com = 0;
unsigned char gb_force_boot = 0;

unsigned char gb_portramTiny[51];
void *gb_portTiny_write_callback[5];
void *gb_portTiny_read_callback[5];
unsigned char cf;
unsigned char hdcount = 0;
unsigned char running = 0;

// RAM banks
unsigned char *gb_ram_00 = NULL;
unsigned char *gb_ram_01 = NULL;
unsigned char *gb_ram_02 = NULL;
unsigned char *gb_ram_03 = NULL;
unsigned char *gb_ram_04 = NULL;
unsigned char *gb_ram_bank[5];

// CGA video memory
unsigned char gb_video_cga[16384];

unsigned char slowsystem = 0;
unsigned short int constantw = 0, constanth = 0;
unsigned char bootdrive = 0;
unsigned char speakerenabled = 0;
unsigned char renderbenchmark = 0;
unsigned int gb_keyboard_time_before, gb_keyboard_time_cur;
unsigned int gb_ini_vga;
unsigned int gb_cur_vga;
unsigned char scrmodechange = 0;

unsigned int x, y;
unsigned short int segregs[4];

// Keymap for Cardputer keyboard (same format as PS/2 keymap)
volatile unsigned char keymap[256];
volatile unsigned char oldKeymap[256];

// Forward declarations
extern void VideoThreadPoll(void);
extern void draw(void);
extern void doscrmodechange();

// totalexec and totalframes defined in cpu.cpp and render.cpp
extern uint64_t totalexec, totalframes;
uint64_t starttick, endtick;
uint32_t speed = 0;

// ===============================================
// Load COM file from Flash array into emulated RAM
// ===============================================
void LoadCOMFlash(const unsigned char *ptr, int auxSize, int seg_load) {
    int dir_load = seg_load * 16;
    for (int i = 0; i < auxSize; i++) {
        write86((dir_load + 0x100 + i), ptr[i]);
    }
    SetRegCS(seg_load);
    SetRegDS(seg_load);
    SetRegES(seg_load);
    SetRegSS(seg_load);
    SetRegIP(0x100);
    SetRegSP(0);
    SetRegBP(0);
    SetRegSI(0);
    SetRegDI(0);
    SetCF(0);
}

// ===============================================
// Initialize emulated hardware chipset
// ===============================================
void inithardware() {
#ifdef use_lib_log_serial
    Serial.printf("Initializing emulated hardware:\n");
#endif
#ifndef use_lib_not_use_callback_port
    memset(gb_portramTiny, 0, sizeof(gb_portramTiny));
    memset(gb_portTiny_write_callback, 0, sizeof(gb_portTiny_write_callback));
    memset(gb_portTiny_read_callback, 0, sizeof(gb_portTiny_read_callback));
#endif
#ifdef use_lib_log_serial
    Serial.printf("  - Intel 8253 timer: ");
#endif
    init8253();
#ifdef use_lib_log_serial
    Serial.printf("OK\n");
    Serial.printf("  - Intel 8259 interrupt controller: ");
#endif
    init8259();
#ifdef use_lib_log_serial
    Serial.printf("OK\n");
    Serial.printf("  - Intel 8237 DMA controller: ");
#endif
    init8237();
#ifdef use_lib_log_serial
    Serial.printf("OK\n");
#endif
    initVideoPorts();
    inittiming();
    initscreen();
}

// ===============================================
// Process pending actions (reset, load COM, etc.)
// ===============================================
void ProcesarAcciones() {
    if (gb_reset == 1) {
        gb_reset = 0;
        ClearRAM();
        memset(gb_video_cga, 0, 16384);
        memset(gb_key_cur, 1, sizeof(gb_key_cur));
        memset(gb_key_before, 1, sizeof(gb_key_before));
        running = 1;
        reset86();
        inithardware();
        return;
    }
    if (gb_force_load_com == 1) {
        gb_force_load_com = 0;
        int auxOffs = 0;
        if (gb_list_seg_load[gb_id_cur_com] == 0)
            auxOffs = 0x07C0;
        else
            auxOffs = 0x0051;
        LoadCOMFlash(gb_list_com_data[gb_id_cur_com], gb_list_com_size[gb_id_cur_com], auxOffs);
        return;
    }
}

// ===============================================
// Clear all emulated RAM
// ===============================================
void ClearRAM() {
    for (int i = 0; i < gb_max_ram; i++) {
        write86(i, 0);
    }
}

// ===============================================
// Allocate RAM banks for emulator
// ===============================================
bool CreateRAM() {
    gb_ram_00 = (unsigned char *)malloc(32768);
    gb_ram_01 = (unsigned char *)malloc(32768);
    gb_ram_02 = (unsigned char *)malloc(32768);
    gb_ram_03 = (unsigned char *)malloc(32768);

    if (!gb_ram_00 || !gb_ram_01 || !gb_ram_02 || !gb_ram_03) {
        free(gb_ram_00);
        free(gb_ram_01);
        free(gb_ram_02);
        free(gb_ram_03);
        gb_ram_00 = gb_ram_01 = gb_ram_02 = gb_ram_03 = NULL;
        return false;
    }

    memset(gb_ram_00, 0, 32768);
    memset(gb_ram_01, 0, 32768);
    memset(gb_ram_02, 0, 32768);
    memset(gb_ram_03, 0, 32768);

    gb_ram_bank[0] = gb_ram_00;
    gb_ram_bank[1] = gb_ram_01;
    gb_ram_bank[2] = gb_ram_02;
    gb_ram_bank[3] = gb_ram_03;
    gb_ram_bank[4] = NULL;
    return true;
}

// ===============================================
// Keyboard scancode translation
// ===============================================
unsigned char TraduceTecla(unsigned char aux) {
    unsigned char aRet = 0;
    switch (aux) {
        case PS2_KC_A: aRet = 30; break;
        case PS2_KC_B: aRet = 48; break;
        case PS2_KC_C: aRet = 46; break;
        case PS2_KC_D: aRet = 32; break;
        case PS2_KC_E: aRet = 18; break;
        case PS2_KC_F: aRet = 33; break;
        case PS2_KC_G: aRet = 34; break;
        case PS2_KC_H: aRet = 35; break;
        case PS2_KC_I: aRet = 23; break;
        case PS2_KC_J: aRet = 36; break;
        case PS2_KC_K: aRet = 37; break;
        case PS2_KC_L: aRet = 38; break;
        case PS2_KC_M: aRet = 50; break;
        case PS2_KC_N: aRet = 49; break;
        case PS2_KC_O: aRet = 24; break;
        case PS2_KC_P: aRet = 25; break;
        case PS2_KC_Q: aRet = 16; break;
        case PS2_KC_R: aRet = 19; break;
        case PS2_KC_S: aRet = 31; break;
        case PS2_KC_T: aRet = 20; break;
        case PS2_KC_U: aRet = 22; break;
        case PS2_KC_V: aRet = 47; break;
        case PS2_KC_W: aRet = 17; break;
        case PS2_KC_X: aRet = 45; break;
        case PS2_KC_Y: aRet = 21; break;
        case PS2_KC_Z: aRet = 44; break;
        case PS2_KC_0: aRet = 11; break;
        case PS2_KC_1: aRet = 2; break;
        case PS2_KC_2: aRet = 3; break;
        case PS2_KC_3: aRet = 4; break;
        case PS2_KC_4: aRet = 5; break;
        case PS2_KC_5: aRet = 6; break;
        case PS2_KC_6: aRet = 7; break;
        case PS2_KC_7: aRet = 8; break;
        case PS2_KC_8: aRet = 9; break;
        case PS2_KC_9: aRet = 10; break;
        case PS2_KC_ENTER: aRet = 28; break;
        case PS2_KC_SPACE: aRet = 57; break;
        case PS2_KC_BS: aRet = 14; break;
        case PS2_KC_TAB: aRet = 15; break;
        case PS2_KC_ESC: aRet = 1; break;
        case PS2_KC_L_SHIFT: aRet = 42; break;
        case PS2_KC_R_SHIFT: aRet = 54; break;
        case PS2_KC_CTRL: aRet = 29; break;
        case PS2_KC_ALT: aRet = 56; break;
        case PS2_KC_MINUS: aRet = 12; break;
        case PS2_KC_EQUAL: aRet = 13; break;
        case PS2_KC_OPEN_SQ: aRet = 26; break;
        case PS2_KC_CLOSE_SQ: aRet = 27; break;
        case PS2_KC_SEMI: aRet = 39; break;
        case PS2_KC_APOS: aRet = 40; break;
        case PS2_KC_SINGLE: aRet = 41; break;
        case PS2_KC_BACK: aRet = 43; break;
        case PS2_KC_COMMA: aRet = 51; break;
        case PS2_KC_DOT: aRet = 52; break;
        case PS2_KC_DIV: aRet = 53; break;
        case PS2_KC_DELETE: aRet = 83; break;
        case KEY_CURSOR_LEFT: aRet = 75; break;
        case KEY_CURSOR_RIGHT: aRet = 77; break;
        case KEY_CURSOR_UP: aRet = 72; break;
        case KEY_CURSOR_DOWN: aRet = 80; break;
        case PS2_KC_F1: aRet = 59; break;
        case PS2_KC_F2: aRet = 60; break;
        case PS2_KC_F3: aRet = 61; break;
        case PS2_KC_F4: aRet = 62; break;
        case PS2_KC_F5: aRet = 63; break;
        case PS2_KC_F6: aRet = 64; break;
        case PS2_KC_F7: aRet = 65; break;
        case PS2_KC_F8: aRet = 66; break;
        case PS2_KC_F9: aRet = 67; break;
        case PS2_KC_F10: aRet = 68; break;
        case PS2_KC_F11: aRet = 87; break;
        case PS2_KC_F12: aRet = 88; break;
        default: aRet = 0; break;
    }
    return aRet;
}

// ===============================================
// Send key event to emulated PC
// ===============================================
void CheckTeclaSDL(unsigned char codigo, unsigned char pulsado) {
    if (codigo == 0) return;

    if (pulsado == 0) {
        gb_portramTiny[fast_tiny_port_0x60] = codigo;
        gb_portramTiny[fast_tiny_port_0x64] |= 2;
        doirq(1);
    } else {
        gb_portramTiny[fast_tiny_port_0x60] = codigo | 0x80;
        gb_portramTiny[fast_tiny_port_0x64] |= 2;
        doirq(1);
    }
}

// ===============================================
// Translate key array index to BIOS scancode
// ===============================================
unsigned char TraduceCodigoTecla(int aux) {
    unsigned char aRet = 0;
    switch (aux) {
        case 0: aRet = 30; break;   // a
        case 1: aRet = 48; break;   // b
        case 2: aRet = 46; break;   // c
        case 3: aRet = 32; break;   // d
        case 4: aRet = 18; break;   // e
        case 5: aRet = 33; break;   // f
        case 6: aRet = 34; break;   // g
        case 7: aRet = 35; break;   // h
        case 8: aRet = 23; break;   // i
        case 9: aRet = 36; break;   // j
        case 10: aRet = 37; break;  // k
        case 11: aRet = 38; break;  // l
        case 12: aRet = 50; break;  // m
        case 13: aRet = 49; break;  // n
        case 14: aRet = 24; break;  // o
        case 15: aRet = 25; break;  // p
        case 16: aRet = 16; break;  // q
        case 17: aRet = 19; break;  // r
        case 18: aRet = 31; break;  // s
        case 19: aRet = 20; break;  // t
        case 20: aRet = 22; break;  // u
        case 21: aRet = 47; break;  // v
        case 22: aRet = 17; break;  // w
        case 23: aRet = 45; break;  // x
        case 24: aRet = 21; break;  // y
        case 25: aRet = 44; break;  // z
        case 26: aRet = 11; break;  // 0
        case 27: aRet = 2; break;   // 1
        case 28: aRet = 3; break;   // 2
        case 29: aRet = 4; break;   // 3
        case 30: aRet = 5; break;   // 4
        case 31: aRet = 6; break;   // 5
        case 32: aRet = 7; break;   // 6
        case 33: aRet = 8; break;   // 7
        case 34: aRet = 9; break;   // 8
        case 35: aRet = 10; break;  // 9
        case 36: aRet = 28; break;  // ENTER
        case 37: aRet = 57; break;  // Space
        case 38: aRet = 14; break;  // Backspace
        case 39: aRet = 1; break;   // ESC
        case 40: aRet = 75; break;  // Left
        case 41: aRet = 77; break;  // Right
        case 42: aRet = 72; break;  // Up
        case 43: aRet = 80; break;  // Down
        case 44: aRet = 59; break;  // F1
        default: aRet = 0; break;
    }
    return aRet;
}

// ===============================================
// Handle keyboard input (Cardputer → emulated PC)
// ===============================================
void handleinput() {
    static const uint8_t monitored_keys[] = {
        PS2_KC_A, PS2_KC_B, PS2_KC_C, PS2_KC_D, PS2_KC_E, PS2_KC_F,
        PS2_KC_G, PS2_KC_H, PS2_KC_I, PS2_KC_J, PS2_KC_K, PS2_KC_L,
        PS2_KC_M, PS2_KC_N, PS2_KC_O, PS2_KC_P, PS2_KC_Q, PS2_KC_R,
        PS2_KC_S, PS2_KC_T, PS2_KC_U, PS2_KC_V, PS2_KC_W, PS2_KC_X,
        PS2_KC_Y, PS2_KC_Z,
        PS2_KC_0, PS2_KC_1, PS2_KC_2, PS2_KC_3, PS2_KC_4,
        PS2_KC_5, PS2_KC_6, PS2_KC_7, PS2_KC_8, PS2_KC_9,
        PS2_KC_ENTER, PS2_KC_SPACE, PS2_KC_BS, PS2_KC_TAB, PS2_KC_ESC,
        PS2_KC_L_SHIFT, PS2_KC_CTRL, PS2_KC_ALT,
        PS2_KC_MINUS, PS2_KC_EQUAL, PS2_KC_OPEN_SQ, PS2_KC_CLOSE_SQ,
        PS2_KC_SEMI, PS2_KC_APOS, PS2_KC_SINGLE, PS2_KC_BACK,
        PS2_KC_COMMA, PS2_KC_DOT, PS2_KC_DIV, PS2_KC_DELETE,
        KEY_CURSOR_LEFT, KEY_CURSOR_RIGHT, KEY_CURSOR_UP, KEY_CURSOR_DOWN,
        PS2_KC_F1, PS2_KC_F2, PS2_KC_F3, PS2_KC_F4, PS2_KC_F5,
        PS2_KC_F6, PS2_KC_F7, PS2_KC_F8, PS2_KC_F9, PS2_KC_F10,
        PS2_KC_F11, PS2_KC_F12
    };
    const size_t key_count = sizeof(monitored_keys) / sizeof(monitored_keys[0]);

    for (size_t i = 0; i < key_count; i++) {
        gb_key_cur[i] = keymap[monitored_keys[i]];
    }

    // Keep the proven state-array input path. Acknowledge only the event sent
    // this poll so simultaneous key changes are delivered on later polls.
    for (size_t i = 0; i < key_count; i++) {
        if (gb_key_cur[i] != gb_key_before[i]) {
            unsigned char xt_code = TraduceTecla(monitored_keys[i]);
            gb_key_before[i] = gb_key_cur[i];
            if (xt_code) {
                CheckTeclaSDL(xt_code, gb_key_cur[i]);
                return;
            }
        }
    }
}

// ===============================================
// Speaker timer callback (I2S-based)
// ===============================================
// Speaker variables defined in cpu.cpp
extern volatile unsigned int gb_pulsos_onda;
extern volatile unsigned int gb_cont_my_callbackfunc;
extern volatile unsigned char speaker_pin_estado;

void my_callback_speaker_func(void) {
    // Called from timer ISR or polled in loop
    // I2S output handled via cardputer_speaker
    if (gb_silence) return;

    if (gb_volumen01 > 0 && gb_frecuencia01 > 0 && gb_pulsos_onda > 0) {
        gb_cont_my_callbackfunc++;
        if (gb_cont_my_callbackfunc >= gb_pulsos_onda) {
            gb_cont_my_callbackfunc = 0;
            speaker_pin_estado = ~speaker_pin_estado;
            int16_t sample = speaker_pin_estado ? (gb_volumen01 * 256) : -(gb_volumen01 * 256);
            cardputer_speaker_write_sample(sample);
        }
    }
}

// ===============================================
// Main setup
// ===============================================
void setup() {
#ifdef use_lib_log_serial
    Serial.begin(115200);
    delay(500);
    Serial.printf("\n=== CardPuter86 ===\n");
    Serial.printf("Free heap: %lu\n", (unsigned long)ESP.getFreeHeap());
    Serial.printf("PSRAM: %lu\n", (unsigned long)ESP.getPsramSize());
#endif

    // Initialize TFT display (draws boot log on screen)
    cardputer_display_init();
    tft_log("Display ready");

    tft_log("Allocating 128 KB emulator RAM...");
    // Reserve the four contiguous emulator RAM banks before filesystems and
    // drivers fragment the remaining heap.
    if (!CreateRAM()) {
        tft_log("ERROR: emulator RAM allocation failed");
#ifdef use_lib_log_serial
        Serial.printf("RAM allocation failed. Free heap: %lu\n",
                      (unsigned long)ESP.getFreeHeap());
#endif
        while (true) delay(1000);
    }
    tft_log("Emulator RAM ready");
    ClearRAM();
    SetRAMTruco();
    bootstrapPoll();
    memset(gb_key_cur, 1, sizeof(gb_key_cur));
    memset(gb_key_before, 1, sizeof(gb_key_before));
    FuerzoParityRAM();

    // Mount internal Flash/SD storage and select the boot IMG.
    cardputer_storage_init_and_select();
    bootdrive = gb_disk_image.mounted ? gb_disk_image.drive : 255;
    cardputer_storage_show_boot_status();

    // Initialize keyboard
    cardputer_kbd_init();

    // Initialize I2S speaker
    cardputer_speaker_init();

    // BIOS data area: number of installed hard disks.
    if (gb_disk_image.mounted && gb_disk_image.drive == 0x80) {
        gb_ram_bank[0][0x475] = 1;
        hdcount = 1;
    }

    // Initialize emulator
    running = 1;
    reset86();
#ifdef use_lib_log_serial
    Serial.printf("CPU reset OK!\n");
#endif
    inithardware();

    lasttick = starttick = 0;
    gb_ini_vga = lasttick;
    gb_keyboard_time_before = gb_keyboard_time_cur = gb_cpu_timer_before = gb_cpu_timer_cur = millis();

#ifdef use_lib_log_serial
    Serial.printf("Setup complete. Free heap: %lu\n", (unsigned long)ESP.getFreeHeap());
#endif
}

// ===============================================
// Main loop
// ===============================================
unsigned char gb_cpunoexe = 0;
unsigned int gb_cpunoexe_timer_ini;
unsigned int tiempo_ini_cpu, tiempo_fin_cpu;
unsigned int total_tiempo_ms_cpu;
unsigned int tiene_que_tardar = 0;

void loop() {
    jj_ini_cpu = micros();
    exec86(10000);
    jj_end_cpu = micros();
    gb_cur_cpu_ticks = (jj_end_cpu - jj_ini_cpu);
    total_tiempo_ms_cpu = gb_cur_cpu_ticks / 1000;
    if (gb_cur_cpu_ticks > gb_max_cpu_ticks)
        gb_max_cpu_ticks = gb_cur_cpu_ticks;
    if (gb_cur_cpu_ticks < gb_min_cpu_ticks)
        gb_min_cpu_ticks = gb_cur_cpu_ticks;

    // Scan keyboard on schedule
    gb_keyboard_time_cur = millis();
    if ((gb_keyboard_time_cur - gb_keyboard_time_before) > gb_keyboard_poll_milis) {
        gb_keyboard_time_before = gb_keyboard_time_cur;

        // Scan Cardputer keyboard matrix
        cardputer_update();
        cardputer_display_update_mode_button();
        cardputer_kbd_fill_keymap();

        handleinput();
        ProcesarAcciones();
        do_tinyOSD();
    }

    if (scrmodechange) {
        doscrmodechange();
    }

    // Video rendering on schedule
    gb_cur_vga = millis();
    if ((gb_cur_vga - gb_ini_vga) >= gb_vga_poll_milis) {
        draw();
        // Speaker timer callback
        my_callback_speaker_func();
        gb_ini_vga = gb_cur_vga;
    }

    // CPU timing control
    if (gb_cpunoexe == 0) {
        gb_cpunoexe = 1;
        gb_cpunoexe_timer_ini = millis();
        tiene_que_tardar = gb_delay_tick_cpu_milis;
    } else {
        if ((millis() - gb_cpunoexe_timer_ini) >= tiene_que_tardar) {
            gb_cpunoexe = 0;
        }
    }

    // Periodic debug output
    gb_cpu_timer_cur = millis();
    if ((gb_cpu_timer_cur - gb_cpu_timer_before) > 1000) {
        gb_cpu_timer_before = gb_cpu_timer_cur;
        if (tiempo_vga == 1) {
#ifdef use_lib_log_serial
            Serial.printf("c:%u m:%u mx:%u vc:%u m:%u mx:%u\n",
                gb_cur_cpu_ticks, gb_min_cpu_ticks, gb_max_cpu_ticks,
                gb_cur_vga_ticks, gb_min_vga_ticks, gb_max_vga_ticks);
#endif
            gb_min_vga_ticks = 1000000;
            gb_max_vga_ticks = 0;
            gb_cur_vga_ticks = 0;
            tiempo_vga = 0;
        } else {
#ifdef use_lib_log_serial
            Serial.printf("c:%u m:%u mx:%u\n",
                gb_cur_cpu_ticks, gb_min_cpu_ticks, gb_max_cpu_ticks);
#endif
        }
        gb_min_cpu_ticks = 1000000;
        gb_max_cpu_ticks = 0;
        gb_cur_cpu_ticks = 0;
    }

    // Yield to prevent watchdog timeout
    yield();
}
