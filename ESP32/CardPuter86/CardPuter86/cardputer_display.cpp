#include "cardputer_display.h"
#include "gbConfig.h"
#include "hardware.h"
#include "gbGlobals.h"
#include <M5Cardputer.h>

// ===============================================
// RGB565 palette for TFT — 16 CGA colors
// ===============================================
unsigned short int gb_palette_rgb565[16];
unsigned short int gb_palette_text_rgb565[16];

// Flat 320x200 framebuffer (one byte per pixel = CGA color index 0-15)
unsigned char *gb_frame_buffer = NULL;

// TFT sprite for double-buffered output (240x135)
LGFX_Sprite *gb_tft_sprite = NULL;

enum CardputerDisplayMode {
    DISPLAY_MODE_TEXT,
    DISPLAY_MODE_GRAPHICS
};

static CardputerDisplayMode display_mode = DISPLAY_MODE_TEXT;
static bool opt_was_pressed = false;

// ===============================================
// Convert 6-bit VGA color (2bpc: RRGGBB) to RGB565
// ===============================================
static unsigned short int vga6_to_rgb565(unsigned char vga_color) {
    // 6-bit VGA: bits[5:4]=R, bits[3:2]=G, bits[1:0]=B
    unsigned char r2 = (vga_color >> 4) & 0x03; // 2-bit red
    unsigned char g2 = (vga_color >> 2) & 0x03; // 2-bit green
    unsigned char b2 = vga_color & 0x03;          // 2-bit blue

    // Expand 2-bit to 5-6-5 bit
    // R: 2 bits → 5 bits: repeat pattern
    unsigned short int r5 = (r2 << 3) | (r2 << 1) | (r2 >> 1);
    // G: 2 bits → 6 bits
    unsigned short int g6 = (g2 << 4) | (g2 << 2) | g2;
    // B: 2 bits → 5 bits
    unsigned short int b5 = (b2 << 3) | (b2 << 1) | (b2 >> 1);

    return (r5 << 11) | (g6 << 5) | b5;
}

// ===============================================
// CGA 16-color palettes (6-bit VGA encoding)
// ===============================================

// CGA 1 palette
static const unsigned char gb_color_cga[16] = {
    0x00,0x28,0x22,0x2A,0x21,0x19,0x10,0x1E,
    0x05,0x01,0x16,0x15,0x15,0x2E,0x25,0x2A
};

// CGA 2 palette
static const unsigned char gb_color_cga2[16] = {
    0x00,0x08,0x02,0x0A,0x21,0x19,0x10,0x1E,
    0x05,0x01,0x16,0x15,0x15,0x2E,0x25,0x2A
};

// PCjr palette
static const unsigned char gb_color_pcjr[16] = {
    0x00,0x15,0x2A,0x3F,0x21,0x19,0x10,0x1E,
    0x05,0x01,0x16,0x15,0x15,0x2E,0x25,0x2A
};

// Grayscale palette
static const unsigned char gb_color_cgagray[16] = {
    0x00,0x2A,0x15,0x3F,0x21,0x19,0x10,0x1E,
    0x05,0x01,0x16,0x15,0x15,0x2E,0x25,0x2A
};

// Default palette (grayscale-like, used by the original project)
static const unsigned char gb_color_vga_default[16] = {
    0x00,0x15,0x2A,0x3F,0x21,0x19,0x10,0x1E,
    0x05,0x01,0x16,0x15,0x15,0x2E,0x25,0x2A
};

// Text mode CGA palette (brighter colors)
static const unsigned char gb_color_text_src[16] = {
    0, 32, 8, 40, 2, 34, 6, 42,
    21, 53, 29, 61, 23, 55, 31, 63
};

// ===============================================
// Build RGB565 palette from VGA 6-bit color array
// ===============================================
static void build_palette(const unsigned char *vga_colors, unsigned short int *rgb565_out) {
    for (int i = 0; i < 16; i++) {
        rgb565_out[i] = vga6_to_rgb565(vga_colors[i]);
    }
}

// ===============================================
// Initialize TFT display and framebuffer
// ===============================================
void cardputer_display_init(void) {
    display_mode = DISPLAY_MODE_TEXT;
    opt_was_pressed = false;

    // 1. Init M5Cardputer
    auto cfg = M5.config();
    cfg.serial_baudrate = 115200;
    cfg.clear_display = true;
    cfg.output_power = true;
    cfg.internal_spk = false;
    cfg.internal_mic = false;
    M5Cardputer.begin(cfg, true);

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(TFT_BLACK);

    int dw = M5Cardputer.Display.width();
    int dh = M5Cardputer.Display.height();

    // Draw title bar
    M5Cardputer.Display.fillRect(0, 0, dw, 14, TFT_BLUE);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLUE);
    M5Cardputer.Display.setTextSize(1.0);
    M5Cardputer.Display.setCursor(2, 1);
    M5Cardputer.Display.print("CardPuter86");

    tft_log("CardPuter86 PC Emulator");
    tft_log_num("LCD:", dw);
    tft_log_num("x", dh);

    // 2. Init framebuffer
    gb_frame_buffer = (unsigned char *)malloc(FAKE86_FB_W * FAKE86_FB_H);
    if (gb_frame_buffer) {
        memset(gb_frame_buffer, 0, FAKE86_FB_W * FAKE86_FB_H);
        tft_log("Framebuffer 320x200 OK");
    } else {
        tft_log("ERROR: FB malloc failed");
    }

    // 3. Init sprite
    gb_tft_sprite = new LGFX_Sprite(&M5Cardputer.Display);
    if (gb_tft_sprite) {
        gb_tft_sprite->setPsram(false);
        gb_tft_sprite->setColorDepth(16);
        void *buf = gb_tft_sprite->createSprite(dw, dh);
        tft_log_num("Sprite buf:", (unsigned long)buf);
    }

    // 4. Init palette
    init_tft_palette();
    tft_log("Palette RGB565 OK");

    // 5. Backlight
    M5Cardputer.Display.setBrightness(128);
    tft_log("Backlight ON");
}

// ===============================================
// Initialize default TFT palette
// ===============================================
void init_tft_palette(void) {
    build_palette(gb_color_vga_default, gb_palette_rgb565);
    build_palette(gb_color_text_src, gb_palette_text_rgb565);
}

void InitPaletaCGA(void) {
    build_palette(gb_color_cga, gb_palette_rgb565);
}

void InitPaletaCGA2(void) {
    build_palette(gb_color_cga2, gb_palette_rgb565);
}

void InitPaletaCGAgray(void) {
    build_palette(gb_color_cgagray, gb_palette_rgb565);
}

void InitPaletaPCJR(void) {
    build_palette(gb_color_pcjr, gb_palette_rgb565);
}

// ===============================================
// Clear framebuffer
// ===============================================
void cardputer_display_clear(unsigned char color_index) {
    if (gb_frame_buffer) {
        memset(gb_frame_buffer, color_index, FAKE86_FB_W * FAKE86_FB_H);
    }
}

// ===============================================
struct TextDisplayRow {
    int source_x;
    int source_y;
};

// Blit the emulator framebuffer to the Cardputer display.
// Text mode keeps pixels at 1:1, wraps overflow at character-row boundaries,
// and follows the bottom of content when the wrapped result is taller than the
// LCD. Graphics mode fits the complete 320x200 frame.
// ===============================================
void tft_blit_scaled(bool emulated_graphics_mode) {
    if (!gb_frame_buffer) return;
    if (!gb_tft_sprite) return;

    unsigned short int *sprite_buf = (unsigned short int *)gb_tft_sprite->getBuffer();
    if (!sprite_buf) return;

    int dw = gb_tft_sprite->width();
    int dh = gb_tft_sprite->height();

    const unsigned short int *palette = emulated_graphics_mode
        ? gb_palette_rgb565
        : gb_palette_text_rgb565;

    if (display_mode == DISPLAY_MODE_GRAPHICS) {
        for (int y = 0; y < dh; y++) {
            unsigned short int *dst = sprite_buf + y * dw;
            const int src_y = y * (FAKE86_FB_H - 1) / (dh - 1);
            const unsigned char *src = gb_frame_buffer + src_y * FAKE86_FB_W;
            for (int x = 0; x < dw; x++) {
                const int src_x = x * (FAKE86_FB_W - 1) / (dw - 1);
                dst[x] = palette[src[src_x] & 0x0F];
            }
        }
    } else {
        static const int text_row_height = 8;
        static const int max_wrapped_rows =
            (FAKE86_FB_H / text_row_height) * 2;
        TextDisplayRow wrapped_rows[max_wrapped_rows];
        int wrapped_count = 0;
        int last_content_y = -1;

        for (int y = 0; y < FAKE86_FB_H; y++) {
            const unsigned char *src = gb_frame_buffer + y * FAKE86_FB_W;
            for (int x = 0; x < FAKE86_FB_W; x++) {
                if ((src[x] & 0x0F) != 0) {
                    last_content_y = y;
                    break;
                }
            }
        }

        const int source_text_rows = last_content_y < 0
            ? 0
            : (last_content_y / text_row_height) + 1;

        for (int row = 0; row < source_text_rows; row++) {
            const int source_y = row * text_row_height;
            wrapped_rows[wrapped_count++] = {0, source_y};

            bool has_overflow = false;
            for (int y = 0; y < text_row_height && !has_overflow; y++) {
                const unsigned char *src =
                    gb_frame_buffer + (source_y + y) * FAKE86_FB_W;
                for (int x = dw; x < FAKE86_FB_W; x++) {
                    if ((src[x] & 0x0F) != 0) {
                        has_overflow = true;
                        break;
                    }
                }
            }
            if (has_overflow) wrapped_rows[wrapped_count++] = {dw, source_y};
        }

        const int wrapped_height = wrapped_count * text_row_height;
        const int first_virtual_y = wrapped_height > dh ? wrapped_height - dh : 0;

        for (int y = 0; y < dh; y++) {
            unsigned short int *dst = sprite_buf + y * dw;
            const int virtual_y = first_virtual_y + y;
            if (virtual_y >= wrapped_height) {
                memset(dst, 0, dw * sizeof(*dst));
                continue;
            }

            const TextDisplayRow &row = wrapped_rows[virtual_y / text_row_height];
            const int source_y = row.source_y + (virtual_y % text_row_height);
            const unsigned char *src =
                gb_frame_buffer + source_y * FAKE86_FB_W + row.source_x;
            const int copy_width = FAKE86_FB_W - row.source_x < dw
                ? FAKE86_FB_W - row.source_x
                : dw;
            for (int x = 0; x < copy_width; x++) {
                dst[x] = palette[src[x] & 0x0F];
            }
            for (int x = copy_width; x < dw; x++) {
                dst[x] = palette[0];
            }
        }
    }

    gb_tft_sprite->pushSprite(0, 0);
}

// ===============================================
// Update M5Cardputer state (call from main loop)
// ===============================================
void cardputer_update(void) {
    M5Cardputer.update();
}

void cardputer_display_update_mode_button(void) {
    const bool opt_pressed = M5Cardputer.Keyboard.keysState().opt;
    if (opt_pressed && !opt_was_pressed) {
        display_mode = display_mode == DISPLAY_MODE_TEXT
            ? DISPLAY_MODE_GRAPHICS
            : DISPLAY_MODE_TEXT;
    }
    opt_was_pressed = opt_pressed;
}

// ===============================================
// On-screen log — draw text directly to TFT
// ===============================================
static int tft_log_line = 0;
static const int TFT_LOG_MAX = 16;

void tft_log(const char *msg) {
    if (tft_log_line >= TFT_LOG_MAX) return;
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1.0);
    M5Cardputer.Display.setCursor(3, tft_log_line * 14 + 14);
    M5Cardputer.Display.print(msg);
    tft_log_line++;
}

void tft_log_num(const char *msg, unsigned long num) {
    if (tft_log_line >= TFT_LOG_MAX) return;
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1.0);
    M5Cardputer.Display.setCursor(3, tft_log_line * 14 + 14);
    M5Cardputer.Display.print(msg);
    M5Cardputer.Display.print(" ");
    M5Cardputer.Display.print(num);
    tft_log_line++;
}
