#include "cardputer_display.h"
#include "gbConfig.h"
#include "hardware.h"
#include "gbGlobals.h"
#include "TomThumb3x5.h"
#include <M5Cardputer.h>
#include "cardputer_input.h"
#include <lgfx/Fonts/glcdfont.h>

// ===============================================
// RGB565 palette for TFT — 16 CGA colors
// ===============================================
unsigned short int gb_palette_rgb565[16];
unsigned short int gb_palette_text_rgb565[16];

// Flat 320x200 framebuffer (one byte per pixel = CGA color index 0-15)
unsigned char *gb_frame_buffer = NULL;

// TFT sprite for double-buffered output (240x135, two 4-bit pixels per byte)
LGFX_Sprite *gb_tft_sprite = NULL;

enum CardputerDisplayMode {
    DISPLAY_MODE_TEXT,
    DISPLAY_MODE_SCALED
};

static CardputerDisplayMode display_mode = DISPLAY_MODE_TEXT;
static bool opt_space_was_pressed = false;
static bool last_emulated_graphics_mode = false;
static int text_view_col = 0;
static int text_view_row = 0;
static bool nav_up_was_pressed = false;
static bool nav_down_was_pressed = false;
static bool nav_left_was_pressed = false;
static bool nav_right_was_pressed = false;
static bool nav_auto_was_pressed = false;
static bool nav_home_was_pressed = false;
static bool text_auto_scroll = true;

extern uint16_t cursy, cols, rows;

static const int TEXT_VIEW_COLS = 40;
static const int TEXT_VIEW_ROWS = 16;

static bool text_row_has_content(int row, int source_cols) {
    for (int col = 0; col < source_cols; col++) {
        const unsigned char character = gb_video_cga[(row * 80 + col) * 2];
        if (character != 0 && character != ' ') return true;
    }
    return false;
}

static bool text_row_looks_like_status_bar(int row, int source_cols) {
    int text_cells = 0;
    int colored_background_cells = 0;
    for (int col = 0; col < source_cols; col++) {
        const int offset = (row * 80 + col) * 2;
        const unsigned char character = gb_video_cga[offset];
        const unsigned char attribute = gb_video_cga[offset + 1];
        if (character != 0 && character != ' ') text_cells++;
        if ((attribute & 0x70) != 0) colored_background_cells++;
    }

    const int colored_threshold = source_cols / 3;
    const int dense_text_threshold = (source_cols * 3) / 4;
    return colored_background_cells >= colored_threshold ||
           text_cells >= dense_text_threshold;
}

static int detect_pinned_status_rows(int source_cols, int source_rows) {
    int pinned_rows = 0;
    for (int row = source_rows - 1;
         row >= 0 && pinned_rows < 2;
         row--) {
        if (!text_row_looks_like_status_bar(row, source_cols)) break;
        pinned_rows++;
    }
    return pinned_rows;
}

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
    opt_space_was_pressed = false;
    last_emulated_graphics_mode = false;
    text_view_col = 0;
    text_view_row = 0;
    text_auto_scroll = true;

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

    // 2. Init framebuffer
    gb_frame_buffer = (unsigned char *)malloc(FAKE86_FB_W * FAKE86_FB_H);
    if (gb_frame_buffer) {
        memset(gb_frame_buffer, 0, FAKE86_FB_W * FAKE86_FB_H);
    } else {
        tft_log("ERROR: FB malloc failed");
    }

    // 3. Init sprite
    gb_tft_sprite = new LGFX_Sprite(&M5Cardputer.Display);
    if (gb_tft_sprite) {
        gb_tft_sprite->setPsram(false);
        gb_tft_sprite->setColorDepth(4);
        void *buf = gb_tft_sprite->createSprite(dw, dh);
        if (!buf) tft_log("ERROR: display buffer failed");
    }

    // 4. Init palette
    init_tft_palette();

    // 5. Backlight
    M5Cardputer.Display.setBrightness(128);
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
// Blit the emulator framebuffer to the Cardputer display.
// Text mode follows the DSx86 layout: a 40x16 viewport using the classic
// 5x7 glyph in a 6x8 cell. Scaled mode uses Tom Thumb 3x5 for text screens
// and fits graphics screens to the complete LCD.
// ===============================================
void tft_blit_scaled(bool emulated_graphics_mode) {
    if (!gb_frame_buffer) return;
    if (!gb_tft_sprite) return;

    unsigned char *sprite_buf = (unsigned char *)gb_tft_sprite->getBuffer();
    if (!sprite_buf) return;

    int dw = gb_tft_sprite->width();
    int dh = gb_tft_sprite->height();
    const int sprite_stride = (dw + 1) / 2;
    last_emulated_graphics_mode = emulated_graphics_mode;

    const unsigned short int *palette = emulated_graphics_mode
        ? gb_palette_rgb565
        : gb_palette_text_rgb565;
    for (int i = 0; i < 16; i++) {
        gb_tft_sprite->setPaletteColor(i, lgfx::rgb565_t(palette[i]));
    }

    if (emulated_graphics_mode) {
        for (int y = 0; y < dh; y++) {
            unsigned char *dst = sprite_buf + y * sprite_stride;
            const int src_y = y * (FAKE86_FB_H - 1) / (dh - 1);
            const unsigned char *src = gb_frame_buffer + src_y * FAKE86_FB_W;
            for (int x = 0; x < dw; x += 2) {
                const int src_x0 = x * (FAKE86_FB_W - 1) / (dw - 1);
                const unsigned char color0 = src[src_x0] & 0x0F;
                unsigned char color1 = 0;
                if (x + 1 < dw) {
                    const int src_x1 = (x + 1) * (FAKE86_FB_W - 1) / (dw - 1);
                    color1 = src[src_x1] & 0x0F;
                }
                dst[x / 2] = (color0 << 4) | color1;
            }
        }
    } else if (display_mode == DISPLAY_MODE_SCALED) {
        memset(sprite_buf, 0, sprite_stride * dh);
        const int visible_cols = cols > 80 ? 80 : cols;
        const int visible_rows = rows > 25 ? 25 : rows;
        const int x_offset = (dw - visible_cols * 3) / 2;
        const int y_offset = (dh - visible_rows * 5) / 2;
        for (int row = 0; row < visible_rows; row++) {
            for (int col = 0; col < visible_cols; col++) {
                const int offset = (row * 80 + col) * 2;
                unsigned char character = gb_video_cga[offset];
                const unsigned char attribute = gb_video_cga[offset + 1];
                unsigned char foreground = attribute & 0x0F;
                unsigned char background = (attribute >> 4) & 0x07;
                if (gb_invert_color) {
                    const unsigned char swap = foreground;
                    foreground = background;
                    background = swap;
                }
                if (character < 32 || character > 127) character = '?';
                const uint8_t *glyph =
                    tom_thumb_3x5 + (character - 32) * 3;
                for (int gy = 0; gy < 5; gy++) {
                    const int y = y_offset + row * 5 + gy;
                    for (int gx = 0; gx < 3; gx++) {
                        const int x = x_offset + col * 3 + gx;
                        const unsigned char color =
                            (glyph[gx] & (0x80 >> gy)) ? foreground : background;
                        unsigned char &packed = sprite_buf[y * sprite_stride + x / 2];
                        if (x & 1) packed = (packed & 0xF0) | color;
                        else packed = (packed & 0x0F) | (color << 4);
                    }
                }
            }
        }
    } else {
        static const int cell_width = 6;
        static const int cell_height = 8;
        const int source_cols = cols > 80 ? 80 : cols;
        const int source_rows = rows > 25 ? 25 : rows;
        const int pinned_rows = detect_pinned_status_rows(source_cols, source_rows);
        const int body_source_rows = source_rows - pinned_rows;
        const int body_view_rows = TEXT_VIEW_ROWS - pinned_rows;
        const int max_view_col = source_cols > TEXT_VIEW_COLS
            ? source_cols - TEXT_VIEW_COLS
            : 0;
        const int max_view_row = body_source_rows > body_view_rows
            ? body_source_rows - body_view_rows
            : 0;
        if (text_view_col > max_view_col) text_view_col = max_view_col;
        if (text_auto_scroll) {
            int last_content_row = -1;
            for (int row = 0; row < body_source_rows; row++) {
                if (text_row_has_content(row, source_cols)) last_content_row = row;
            }
            if (cursy < body_source_rows && cursy > last_content_row) {
                last_content_row = cursy;
            }
            text_view_row = last_content_row >= body_view_rows
                ? last_content_row - body_view_rows + 1
                : 0;
        } else if (text_view_row > max_view_row) {
            text_view_row = max_view_row;
        }

        memset(sprite_buf, 0, sprite_stride * dh);
        const int y_offset = (dh - TEXT_VIEW_ROWS * cell_height) / 2;
        for (int row = 0; row < TEXT_VIEW_ROWS; row++) {
            const int source_row = row < body_view_rows
                ? text_view_row + row
                : body_source_rows + (row - body_view_rows);
            if (source_row >= source_rows) break;
            for (int col = 0; col < TEXT_VIEW_COLS; col++) {
                const int source_col = text_view_col + col;
                if (source_col >= source_cols) break;
                const int offset = (source_row * 80 + source_col) * 2;
                const unsigned char character = gb_video_cga[offset];
                const unsigned char attribute = gb_video_cga[offset + 1];
                unsigned char foreground = attribute & 0x0F;
                unsigned char background = (attribute >> 4) & 0x07;
                if (gb_invert_color) {
                    const unsigned char swap = foreground;
                    foreground = background;
                    background = swap;
                }

                for (int gy = 0; gy < cell_height; gy++) {
                    const int y = y_offset + row * cell_height + gy;
                    for (int gx = 0; gx < cell_width; gx++) {
                        const int x = col * cell_width + gx;
                        const bool glyph_pixel = gx < 5 &&
                            (font[character * 5 + gx] & (1 << gy));
                        const unsigned char color = glyph_pixel
                            ? foreground
                            : background;
                        unsigned char &packed = sprite_buf[y * sprite_stride + x / 2];
                        if (x & 1) packed = (packed & 0xF0) | color;
                        else packed = (packed & 0x0F) | (color << 4);
                    }
                }
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
    const Keyboard_Class::KeysState &state = M5Cardputer.Keyboard.keysState();
    const bool opt_space_pressed = state.opt && state.space;
    if (opt_space_pressed && !opt_space_was_pressed) {
        display_mode = display_mode == DISPLAY_MODE_TEXT
            ? DISPLAY_MODE_SCALED
            : DISPLAY_MODE_TEXT;
    }
    opt_space_was_pressed = opt_space_pressed;

    const bool navigation_active = cardputer_display_navigation_active();
    const bool up_pressed = navigation_active && state.opt &&
        M5Cardputer.Keyboard.isKeyPressed(';');
    const bool down_pressed = navigation_active && state.opt &&
        M5Cardputer.Keyboard.isKeyPressed('.');
    const bool left_pressed = navigation_active && state.opt &&
        M5Cardputer.Keyboard.isKeyPressed(',');
    const bool right_pressed = navigation_active && state.opt &&
        M5Cardputer.Keyboard.isKeyPressed('/');
    const bool home_pressed = navigation_active && state.opt &&
        M5Cardputer.Keyboard.isKeyPressed('\'');

    const int source_cols = cols > 80 ? 80 : cols;
    const int source_rows = rows > 25 ? 25 : rows;
    const int pinned_rows = detect_pinned_status_rows(source_cols, source_rows);
    const int body_source_rows = source_rows - pinned_rows;
    const int body_view_rows = TEXT_VIEW_ROWS - pinned_rows;
    const int max_view_col = source_cols > TEXT_VIEW_COLS
        ? source_cols - TEXT_VIEW_COLS
        : 0;
    const int max_view_row = body_source_rows > body_view_rows
        ? body_source_rows - body_view_rows
        : 0;

    if (home_pressed && !nav_home_was_pressed && !text_auto_scroll) {
        text_view_col = 0;
        text_view_row = 0;
    }
    if (up_pressed && !nav_up_was_pressed) {
        text_auto_scroll = false;
        if (text_view_row > 0) text_view_row--;
    }
    if (down_pressed && !nav_down_was_pressed) {
        text_auto_scroll = false;
        if (text_view_row < max_view_row) text_view_row++;
    }
    if (left_pressed && !nav_left_was_pressed) {
        text_auto_scroll = false;
        if (text_view_col > 0) text_view_col--;
    }
    if (right_pressed && !nav_right_was_pressed) {
        text_auto_scroll = false;
        if (text_view_col < max_view_col) text_view_col++;
    }

    nav_up_was_pressed = up_pressed;
    nav_down_was_pressed = down_pressed;
    nav_left_was_pressed = left_pressed;
    nav_right_was_pressed = right_pressed;
    nav_auto_was_pressed = false;
    nav_home_was_pressed = home_pressed;
}

bool cardputer_display_navigation_active(void) {
    return display_mode == DISPLAY_MODE_TEXT && !last_emulated_graphics_mode;
}

// ===============================================
// On-screen log — draw text directly to TFT
// ===============================================
static const int TFT_LOG_MAX = 8;
static const int TFT_LOG_CHARS = 39;
static char tft_log_lines[TFT_LOG_MAX][TFT_LOG_CHARS + 1];
static int tft_log_count = 0;

static void redraw_tft_log(void) {
    M5Cardputer.Display.fillRect(0, 14, M5Cardputer.Display.width(),
                                 M5Cardputer.Display.height() - 14, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1.0);
    for (int i = 0; i < tft_log_count; i++) {
        M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
        M5Cardputer.Display.setCursor(3, 16 + i * 14);
        M5Cardputer.Display.print(tft_log_lines[i]);
    }
}

void tft_log(const char *msg) {
    if (tft_log_count == TFT_LOG_MAX) {
        memmove(tft_log_lines, tft_log_lines + 1,
                sizeof(tft_log_lines[0]) * (TFT_LOG_MAX - 1));
        tft_log_count--;
    }
    strncpy(tft_log_lines[tft_log_count], msg, TFT_LOG_CHARS);
    tft_log_lines[tft_log_count][TFT_LOG_CHARS] = '\0';
    tft_log_count++;
    redraw_tft_log();
}

void tft_log_num(const char *msg, unsigned long num) {
    char line[TFT_LOG_CHARS + 1];
    snprintf(line, sizeof(line), "%s %lu", msg, num);
    tft_log(line);
}
