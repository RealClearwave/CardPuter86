#ifndef _CARDputer_DISPLAY_H
#define _CARDputer_DISPLAY_H

#include "gbConfig.h"
#include <M5Cardputer.h>

// RGB565 palette: 16 entries for CGA emulation colors
extern unsigned short int gb_palette_rgb565[16];
extern unsigned short int gb_palette_text_rgb565[16];

// Flat framebuffer 320x200, one byte per pixel (CGA color index 0-15)
extern unsigned char *gb_frame_buffer;

// TFT sprite pointer - allocated in display init
extern LGFX_Sprite *gb_tft_sprite;

void cardputer_display_init(void);
void tft_blit_scaled(bool emulated_graphics_mode);
void init_tft_palette(void);
void cardputer_display_clear(unsigned char color_index);
void cardputer_update(void);
void cardputer_display_update_mode_button(void);
void tft_log(const char *msg);
void tft_log_num(const char *msg, unsigned long num);

#endif
