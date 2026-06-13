//  Fake86: A portable, open-source 8086 PC emulator.
//  Copyright (C)2010-2012 Mike Chambers
//
//  Ported to M5Stack Cardputer — TFT display rendering
//  render.cpp: functions for framebuffer rendering and TFT output.

#include "gbConfig.h"
#include <stdint.h>
#include <stdio.h>
#include <Arduino.h>
#include "gbGlobals.h"
#include "cpu.h"
#include "render.h"
#include "fake86.h"
#include "gb_sdl_font8x8.h"
#include "gb_sdl_font4x8.h"
#include "cardputer_display.h"

#define uint32_t int

// Forward declarations
void InitPaletaCGA(void);
void InitPaletaCGA2(void);
void InitPaletaCGAgray(void);
void InitPaletaPCJR(void);

uint64_t totalframes = 0;
char windowtitle[128];
int gb_cont_rgb=0;

extern uint8_t cgabg, blankattr, vidgfxmode, vidcolor;
extern uint16_t cursx, cursy, cols, rows, vgapage, cursorposition, cursorvisible;
extern uint8_t clocksafe, port6, portout16;
extern uint32_t videobase, textbase;
extern uint32_t usefullscreen;

// ===============================================
// Fast putpixel into flat framebuffer
// ===============================================
inline void jj_fast_putpixel(int x, int y, unsigned char c) {
    if (x < 0 || x >= FAKE86_FB_W || y < 0 || y >= FAKE86_FB_H) return;
    gb_frame_buffer[y * FAKE86_FB_W + x] = c;
}

// ===============================================
// Text mode putpixel using text palette
// ===============================================
inline void jj_fast_putpixel_text(int x, int y, unsigned char c) {
    if (x < 0 || x >= FAKE86_FB_W || y < 0 || y >= FAKE86_FB_H) return;
    gb_frame_buffer[y * FAKE86_FB_W + x] = c;
}

unsigned char initscreen() {
#ifdef use_lib_log_serial
    Serial.printf("initscreen: framebuffer %dx%d\n", FAKE86_FB_W, FAKE86_FB_H);
#endif
    return (1);
}

uint32_t nw, nh;

void doscrmodechange() {
    if (scrmodechange) {
        // On Cardputer, we don't resize the display — just acknowledge
        // the video mode change. The framebuffer stays at 320x200.
        scrmodechange = 0;
    }
}

extern uint16_t vtotal;

// ===============================================
// Text character rendering (8x8 font)
// Uses text palette for CGA text colors
// ===============================================
void SDLprintChar(char car, int x, int y, unsigned char color, unsigned char backcolor) {
    int auxId = car << 3; // *8
    unsigned char aux;
    unsigned char auxBit, auxColor;
    for (unsigned char j = 0; j < 8; j++) {
        aux = gb_sdl_font_8x8[auxId + j];
        for (int i = 0; i < 8; i++) {
            auxColor = ((aux >> i) & 0x01);
            gb_frame_buffer[(y + j) * FAKE86_FB_W + (x + (7 - i))] =
                (auxColor == 1) ? color : backcolor;
        }
    }
}

// ===============================================
// Text character rendering (4x8 font)
// ===============================================
void SDLprintChar4x8(char car, int x, int y, unsigned char color, unsigned char backcolor) {
    int auxId = car << 3; // *8
    unsigned char aux;
    unsigned char auxBit, auxColor;
    for (unsigned char j = 0; j < 8; j++) {
        aux = gb_sdl_font_4x8[auxId + j];
        for (int i = 4; i < 8; i++) {
            auxColor = ((aux >> i) & 0x01);
            gb_frame_buffer[(y + j) * FAKE86_FB_W + (x + (7 - i))] =
                ((auxColor == 1) ? color : backcolor);
        }
    }
}

// ===============================================
// 40x25 text mode dump (8x8 font)
// ===============================================
void SDLdump80x25_font8x8() {
    unsigned char aColor, aBgColor, aChar;
    unsigned int auxOffset = 0;
    if ((gb_portramTiny[fast_tiny_port_0x3D8] == 9) && (gb_portramTiny[fast_tiny_port_0x3D4] == 9)) {
        // 160x100 mode (PAKUPAKU)
        for (int y = 0; y < 100; y++) {
            for (unsigned char x = 0; x < 40; x++) {
                aChar = gb_video_cga[auxOffset];
                auxOffset++;
                aColor = gb_video_cga[auxOffset] & 0x0F;
                aBgColor = ((gb_video_cga[auxOffset] >> 4) & 0x0F);
                // 160x100: each char is 8x2 pixels
                unsigned char nc, nb;
                switch (aChar) {
                    case 221: nc = aColor; nb = aBgColor; break;
                    case 222: nc = aBgColor; nb = aColor; break;
                    default:  nc = 0; nb = 0; break;
                }
                for (unsigned char j = 0; j < 2; j++) {
                    for (int i = 0; i < 4; i++) {
                        gb_frame_buffer[(y * 2 + j) * FAKE86_FB_W + (x * 8 + i)] = nc;
                    }
                    for (int i = 4; i < 8; i++) {
                        gb_frame_buffer[(y * 2 + j) * FAKE86_FB_W + (x * 8 + i)] = nb;
                    }
                }
                auxOffset++;
            }
            auxOffset += 80; // skip second half of 40x25 line
        }
        return;
    }

    for (unsigned char y = 0; y < 25; y++) {
        for (unsigned char x = 0; x < 40; x++) {
            aChar = gb_video_cga[auxOffset];
            auxOffset++;
            aColor = gb_video_cga[auxOffset] & 0x0F;
            aBgColor = ((gb_video_cga[auxOffset] >> 4) & 0x07);
#ifdef use_lib_capture_usb
            if (x < 79) {
                SDLprintChar(aChar, ((x + 1) << 3), (y << 3), aColor, aBgColor);
            }
#else
            SDLprintChar(aChar, (x << 3), (y << 3), aColor, aBgColor);
#endif
            auxOffset++;
        }
        auxOffset += 80;
    }
}

// ===============================================
// 80x25 text mode dump (4x8 font)
// ===============================================
void SDLdump80x25_font4x8() {
    unsigned char aColor, aBgColor, aChar, swapColor;
    unsigned int auxOffset = 0;

    if ((gb_portramTiny[fast_tiny_port_0x3D8] == 9) && (gb_portramTiny[fast_tiny_port_0x3D4] == 9)) {
        // 160x100 mode
        for (int y = 0; y < 100; y++) {
            for (unsigned char x = 0; x < 80; x++) {
                aChar = gb_video_cga[auxOffset];
                auxOffset++;
                aColor = gb_video_cga[auxOffset] & 0x0F;
                aBgColor = ((gb_video_cga[auxOffset] >> 4) & 0x0F);
                unsigned char nc, nb;
                switch (aChar) {
                    case 221: nc = aColor; nb = aBgColor; break;
                    case 222: nc = aBgColor; nb = aColor; break;
                    default:  nc = 0; nb = 0; break;
                }
                for (unsigned char j = 0; j < 2; j++) {
                    for (int i = 0; i < 2; i++) {
                        gb_frame_buffer[(y * 2 + j) * FAKE86_FB_W + (x * 4 + i)] = nc;
                    }
                    for (int i = 2; i < 4; i++) {
                        gb_frame_buffer[(y * 2 + j) * FAKE86_FB_W + (x * 4 + i)] = nb;
                    }
                }
                auxOffset++;
            }
        }
        return;
    }

    for (int y = 0; y < 25; y++) {
        for (int x = 0; x < 80; x++) {
            aChar = gb_video_cga[auxOffset];
            auxOffset++;
            aColor = gb_video_cga[auxOffset] & 0x0F;
            aBgColor = ((gb_video_cga[auxOffset] >> 4) & 0x07);

            if (gb_invert_color == 1) {
                swapColor = aColor;
                aColor = aBgColor;
                aBgColor = swapColor;
            }
#ifdef use_lib_capture_usb
            if (x < 79) {
                SDLprintChar4x8(aChar, ((x + 1) << 2), (y << 3), aColor, aBgColor);
            }
#else
            SDLprintChar4x8(aChar, (x << 2), (y << 3), aColor, aBgColor);
#endif
            auxOffset++;
        }
    }
}

// ===============================================
// Scanline buffer for optimized CGA rendering
// ===============================================
static unsigned int gb_local_scanline[80];

// ===============================================
// CGA mode 5 (320x200, 4 colors) — optimized
// ===============================================
void jj_sdl_dump_cga5() {
    unsigned short int cont = 0;
    unsigned int a0, a1, a2, a3;
    unsigned char aux;
    unsigned int yDest;
    unsigned int x;
    unsigned char y;
    unsigned int *ptr32;
    unsigned int a32;

    // Even scanlines (from bank 0)
    for (y = 0; y < 100; y++) {
        yDest = (y << 1);
        ptr32 = (unsigned int *)(gb_frame_buffer + yDest * FAKE86_FB_W);
        for (x = 0; x < 80; x++) {
            aux = gb_video_cga[cont];
            a3 = (aux & 0x03);
            a2 = ((aux >> 2) & 0x03);
            a1 = ((aux >> 4) & 0x03);
            a0 = ((aux >> 6) & 0x03);
            // Pack 4 pixels as bytes into a 32-bit word
            a32 = a2 | (a3 << 8) | (a0 << 16) | (a1 << 24);
            gb_local_scanline[x] = a32;
            cont++;
        }
        memcpy(ptr32, gb_local_scanline, 320);
    }

    // Odd scanlines (from bank 1 at offset 0x2000)
    cont = 0x2000;
    for (y = 0; y < 100; y++) {
        yDest = (y << 1) + 1;
        ptr32 = (unsigned int *)(gb_frame_buffer + yDest * FAKE86_FB_W);
        for (x = 0; x < 80; x++) {
            aux = gb_video_cga[cont];
            a3 = (aux & 0x03);
            a2 = ((aux >> 2) & 0x03);
            a1 = ((aux >> 4) & 0x03);
            a0 = ((aux >> 6) & 0x03);
            a32 = a2 | (a3 << 8) | (a0 << 16) | (a1 << 24);
            gb_local_scanline[x] = a32;
            cont++;
        }
        memcpy(ptr32, gb_local_scanline, 320);
    }
}

// ===============================================
// CGA mode 6 (640x200, 1-bit) — optimized
// ===============================================
void jj_sdl_dump_cga6() {
    unsigned short int cont = 0;
    unsigned char a0, a2, a4, a6;
    unsigned char aux;
    unsigned int yDest;
    unsigned int x;
    unsigned char y;
    unsigned int *ptr32;
    unsigned int a32;

    for (y = 0; y < 100; y++) {
        yDest = (y << 1);
        ptr32 = (unsigned int *)(gb_frame_buffer + yDest * FAKE86_FB_W);
        for (x = 0; x < 80; x++) {
            aux = gb_video_cga[cont];
            a6 = ((aux >> 1) & 0x01);
            a4 = ((aux >> 3) & 0x01);
            a2 = ((aux >> 5) & 0x01);
            a0 = ((aux >> 7) & 0x01);

            a0 = (a0 == 0 ? 0 : 3);
            a2 = (a2 == 0 ? 0 : 3);
            a4 = (a4 == 0 ? 0 : 3);
            a6 = (a6 == 0 ? 0 : 3);

            a32 = a4 | (a6 << 8) | (a0 << 16) | (a2 << 24);
            gb_local_scanline[x] = a32;
            cont++;
        }
        memcpy(ptr32, gb_local_scanline, 320);
    }

    cont = 0x2000;
    for (y = 0; y < 100; y++) {
        yDest = (y << 1) + 1;
        ptr32 = (unsigned int *)(gb_frame_buffer + yDest * FAKE86_FB_W);
        for (x = 0; x < 80; x++) {
            aux = gb_video_cga[cont];
            a6 = ((aux >> 1) & 0x01);
            a4 = ((aux >> 3) & 0x01);
            a2 = ((aux >> 5) & 0x01);
            a0 = ((aux >> 7) & 0x01);

            a0 = (a0 == 0 ? 0 : 3);
            a2 = (a2 == 0 ? 0 : 3);
            a4 = (a4 == 0 ? 0 : 3);
            a6 = (a6 == 0 ? 0 : 3);

            a32 = a4 | (a6 << 8) | (a0 << 16) | (a2 << 24);
            gb_local_scanline[x] = a32;
            cont++;
        }
        memcpy(ptr32, gb_local_scanline, 320);
    }
}

// ===============================================
// Main draw function — dispatches to video-mode renderers,
// then blits framebuffer to TFT
// ===============================================
void draw() {
    int x, y;
    uint32_t planemode, vgapage, color, chary, charx, vidptr, divx, divy, curchar, curpixel, usepal, intensity, blockw, curheight, x1, y1;

    switch (vidmode) {
        case 0:
        case 1:
        case 2:
        case 3:
        case 7:
        case 0x82:
            if (gb_font_8x8 == 1)
                SDLdump80x25_font8x8();
            else
                SDLdump80x25_font4x8();
            break;

        case 4:
        case 5:
            jj_sdl_dump_cga5();
            break;

        case 6:
            jj_sdl_dump_cga6();
            break;

        case 127:
            nw = 720;
            nh = 348;
            for (y = 0; y < 348; y++) {
                for (x = 0; x < 720; x++) {
                    charx = x;
                    chary = y >> 1;
                    vidptr = videobase + ((y & 3) << 13) + (y >> 2) * 90 + (x >> 3);
                    curpixel = (read86(vidptr) >> (7 - (charx & 7))) & 1;
                    if (curpixel) color = 15; // white
                    else color = 0;
                    jj_fast_putpixel((x >> 2), (y >> 1), color);
                }
            }
            break;

        case 0x8:
            nw = 640;
            nh = 400;
            for (y = 0; y < 400; y++)
                for (x = 0; x < 640; x++) {
                    vidptr = 0xB8000 + (y >> 2) * 80 + (x >> 3) + ((y >> 1) & 1) * 8192;
                    if (((x >> 1) & 1) == 0)
                        color = palettecga[read86(vidptr) >> 4];
                    else
                        color = palettecga[read86(vidptr) & 15];
                    jj_fast_putpixel((x >> 1), (y >> 1), color);
                }
            break;

        case 0x9:
            nw = 640;
            nh = 400;
            for (y = 0; y < 400; y++)
                for (x = 0; x < 640; x++) {
                    vidptr = 0xB8000 + (y >> 3) * 160 + (x >> 2) + ((y >> 1) & 3) * 8192;
                    if (((x >> 1) & 1) == 0)
                        color = palettecga[read86(vidptr) >> 4];
                    else
                        color = palettecga[read86(vidptr) & 15];
                    jj_fast_putpixel((x >> 1), (y >> 1), color);
                }
            break;

        case 0xD:
        case 0xE:
            nw = 640;
            nh = 400;
            for (y = 0; y < 400; y++)
                for (x = 0; x < 640; x++) {
                    divx = x >> 1;
                    divy = y >> 1;
                    vidptr = divy * 40 + (divx >> 3);
                    x1 = 7 - (divx & 7);
                    color = 0; // Simplified VGA — no VGA plane support
                    jj_fast_putpixel((x >> 1), (y >> 1), color);
                }
            break;

        case 0x10:
            nw = 640;
            nh = 350;
            for (y = 0; y < 350; y++)
                for (x = 0; x < 640; x++) {
                    vidptr = y * 80 + (x >> 3);
                    x1 = 7 - (x & 7);
                    color = 0;
                    jj_fast_putpixel((x >> 1), (y >> 1), color);
                }
            break;

        default:
            break;
    }

    // Blit framebuffer to TFT
    const bool graphics_mode = !(
        vidmode == 0 || vidmode == 1 || vidmode == 2 || vidmode == 3 ||
        vidmode == 7 || vidmode == 0x82);
    tft_blit_scaled(graphics_mode);
}

// ===============================================
// Palette initialization functions
// (actual implementations in cardputer_display.cpp)
// These call through to build RGB565 palettes from CGA source data.
// The original code used build-time palette tables;
// we do the same but build RGB565 instead of VGA 6-bit + sync bits.
// =======================================
// Palette init stubs — implemented in cardputer_display.cpp
