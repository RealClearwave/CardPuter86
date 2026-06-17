#include "cardputer_storage.h"
#include "hardware.h"
#include "guest_memory.h"
#include "cardputer_cpu.h"
#include "cardputer_settings.h"
#include "cardputer_modem.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <USB.h>
#include <USBMSC.h>
#include <dirent.h>
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>

extern "C" {
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"
}

static const char *FLASH_MOUNT_POINT = "/flash";
static const char *FLASH_PARTITION_LABEL = "disk";
static const uint8_t MAX_BOOT_IMAGES = 20;

struct BootImageEntry {
    CardputerStorageType storage;
    uint32_t size;
    char path[CARDPUTER_IMAGE_PATH_LEN];
};

static SPIClass sd_spi;
static bool sd_available = false;
static bool flash_available = false;
static bool flash_raw_fallback = false;
static bool sd_detected = false;
static bool flash_detected = false;
static wl_handle_t flash_wl_handle = WL_INVALID_HANDLE;
static FILE *flash_image_file = nullptr;
static File sd_image_file;
static BootImageEntry boot_images[MAX_BOOT_IMAGES];
static uint8_t boot_image_count = 0;

// Construct before TinyUSB starts so MSC is part of the USB descriptor. In
// normal boot it remains present with no media; USB mode makes it ready.
static USBMSC usb_msc;
static CardputerStorageType usb_storage = CARDPUTER_STORAGE_NONE;
static uint8_t sd_usb_sector[512];
static wl_handle_t usb_flash_wl = WL_INVALID_HANDLE;
static uint8_t *usb_flash_cache = nullptr;
static size_t usb_flash_cache_address = SIZE_MAX;
static size_t usb_flash_cache_size = 0;
static bool usb_flash_cache_dirty = false;
static uint16_t usb_block_size = 512;
static volatile bool sd_mount_finished = false;
static volatile bool sd_mount_result = false;
static TaskHandle_t sd_mount_task_handle = nullptr;

CardputerDiskImage gb_disk_image = {};

static void show_probe_status(const char *status, const char *hint = nullptr) {
    auto &display = M5Cardputer.Display;
    display.fillScreen(TFT_BLACK);
    display.fillRect(0, 0, display.width(), 16, TFT_BLUE);
    display.setTextSize(1);
    display.setTextColor(TFT_WHITE, TFT_BLUE);
    display.setCursor(4, 4);
    display.print("CardPuter86 storage POST");
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setCursor(6, 30);
    display.print(status);
    if (hint) {
        display.setTextColor(TFT_YELLOW, TFT_BLACK);
        display.setCursor(6, 48);
        display.print(hint);
    }
}

static bool is_image_name(const char *name) {
    const size_t len = strlen(name);
    if (len <= 4) return false;
    const char *extension = name + len - 4;
    return strcasecmp(extension, ".img") == 0 ||
           strcasecmp(extension, ".dsk") == 0;
}

static const char *base_name(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool sd_card_responds(void) {
    // Cardputer has no card-detect switch. Keep MISO high when the socket is
    // empty; otherwise a floating line can look like CMD0's 0x01 response.
    pinMode(SD_MISO, INPUT_PULLUP);
    sd_spi.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    sd_spi.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (uint8_t i = 0; i < 10; i++) sd_spi.transfer(0xFF);

    digitalWrite(SD_CS, LOW);
    sd_spi.transfer(0x40); // CMD0
    sd_spi.transfer(0x00);
    sd_spi.transfer(0x00);
    sd_spi.transfer(0x00);
    sd_spi.transfer(0x00);
    sd_spi.transfer(0x95);
    uint8_t cmd0_response = 0xFF;
    for (uint8_t i = 0; i < 10; i++) {
        cmd0_response = sd_spi.transfer(0xFF);
        if ((cmd0_response & 0x80) == 0) break;
    }
    digitalWrite(SD_CS, HIGH);
    sd_spi.transfer(0xFF);

    bool detected = false;
    if (cmd0_response == 0x01) {
        digitalWrite(SD_CS, LOW);
        sd_spi.transfer(0x48); // CMD8: SEND_IF_COND
        sd_spi.transfer(0x00);
        sd_spi.transfer(0x00);
        sd_spi.transfer(0x01);
        sd_spi.transfer(0xAA);
        sd_spi.transfer(0x87);
        uint8_t cmd8_response = 0xFF;
        for (uint8_t i = 0; i < 10; i++) {
            cmd8_response = sd_spi.transfer(0xFF);
            if ((cmd8_response & 0x80) == 0) break;
        }
        uint8_t r7[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        if (cmd8_response == 0x01) {
            for (uint8_t i = 0; i < 4; i++) r7[i] = sd_spi.transfer(0xFF);
            detected = r7[2] == 0x01 && r7[3] == 0xAA;
        }
        digitalWrite(SD_CS, HIGH);
        sd_spi.transfer(0xFF);
    }

    digitalWrite(SD_CS, HIGH);
    sd_spi.endTransaction();
    return detected;
}

static bool mount_sd(void) {
    if (!sd_card_responds()) {
        sd_spi.end();
        return false;
    }
    if (!SD.begin(SD_CS, sd_spi, 25000000) || SD.cardType() == CARD_NONE) {
        SD.end();
        sd_spi.end();
        return false;
    }
    sd_available = true;
    return true;
}

static void sd_mount_task(void *parameter) {
    (void)parameter;
    sd_mount_result = mount_sd();
    sd_mount_finished = true;
    // Keep the handle valid until the caller observes the result and deletes
    // this task. This also removes the completion/cancellation race.
    while (true) vTaskDelay(portMAX_DELAY);
}

static bool mount_sd_timed(void) {
    sd_mount_finished = false;
    sd_mount_result = false;
    sd_mount_task_handle = nullptr;

    BaseType_t created = xTaskCreatePinnedToCore(
        sd_mount_task, "sd_mount", 4096, nullptr, 1,
        &sd_mount_task_handle, 0);
    if (created != pdPASS) return false;

    const uint32_t started_at = millis();
    while (!sd_mount_finished) {
        // A faulty card or bus must not prevent the internal image from booting.
        if (millis() - started_at >= 15000) {
            if (sd_mount_task_handle) {
                vTaskDelete(sd_mount_task_handle);
                sd_mount_task_handle = nullptr;
            }
            show_probe_status("SD check timed out", "Continuing with internal IMG");
            delay(250);
            return false;
        }
        delay(10);
    }

    const bool result = sd_mount_result;
    if (sd_mount_task_handle) vTaskDelete(sd_mount_task_handle);
    sd_mount_task_handle = nullptr;
    return result;
}

static bool alt_requests_sd(void) {
    show_probe_status("SD disabled by default", "Hold Alt to scan SD (1.5s)");
    const uint32_t deadline = millis() + 1500;
    uint32_t alt_pressed_at = 0;
    while ((int32_t)(deadline - millis()) > 0) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.keysState().alt) {
            if (alt_pressed_at == 0) alt_pressed_at = millis();
            if (millis() - alt_pressed_at >= 200) return true;
        } else {
            alt_pressed_at = 0;
        }
        delay(10);
    }
    return false;
}

static void unmount_sd(void) {
    if (sd_available) SD.end();
    sd_spi.end();
    sd_available = false;
}

static bool mount_flash(void) {
    flash_raw_fallback = false;
    esp_vfs_fat_mount_config_t config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 4096
    };
    esp_err_t result = esp_vfs_fat_spiflash_mount(
        FLASH_MOUNT_POINT, FLASH_PARTITION_LABEL, &config, &flash_wl_handle);
    if (result == ESP_OK) {
        flash_available = true;
        return true;
    }

#ifdef use_lib_log_serial
    Serial.printf("Internal IMG partition WL mount failed: %s\n",
                  esp_err_to_name(result));
#endif
    flash_wl_handle = WL_INVALID_HANDLE;

    result = esp_vfs_fat_rawflash_mount(
        FLASH_MOUNT_POINT, FLASH_PARTITION_LABEL, &config);
    if (result != ESP_OK) {
#ifdef use_lib_log_serial
        Serial.printf("Internal IMG raw FAT fallback failed: %s\n",
                      esp_err_to_name(result));
#endif
        return false;
    }
    flash_available = true;
    flash_raw_fallback = true;
#ifdef use_lib_log_serial
    Serial.printf("Internal IMG mounted read-only via raw FAT fallback\n");
#endif
    return true;
}

static void unmount_flash(void) {
    if (flash_available) {
        if (flash_raw_fallback) {
            esp_vfs_fat_rawflash_unmount(FLASH_MOUNT_POINT, FLASH_PARTITION_LABEL);
        } else if (flash_wl_handle != WL_INVALID_HANDLE) {
            esp_vfs_fat_spiflash_unmount(FLASH_MOUNT_POINT, flash_wl_handle);
        }
    }
    flash_wl_handle = WL_INVALID_HANDLE;
    flash_available = false;
    flash_raw_fallback = false;
}

static void add_boot_image(CardputerStorageType storage, const char *path,
                           uint32_t size) {
    if (boot_image_count >= MAX_BOOT_IMAGES || size < 512 || size % 512 != 0) return;
    BootImageEntry &entry = boot_images[boot_image_count++];
    entry.storage = storage;
    entry.size = size;
    strncpy(entry.path, path, sizeof(entry.path) - 1);
    entry.path[sizeof(entry.path) - 1] = '\0';
}

static void scan_flash_images(void) {
    if (!flash_available) return;
    DIR *directory = opendir(FLASH_MOUNT_POINT);
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory)) != nullptr) {
        if (!is_image_name(entry->d_name)) continue;
        char path[CARDPUTER_IMAGE_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", FLASH_MOUNT_POINT, entry->d_name);
        struct stat info;
        if (stat(path, &info) == 0 && S_ISREG(info.st_mode)) {
            add_boot_image(CARDPUTER_STORAGE_FLASH, path, info.st_size);
        }
    }
    closedir(directory);
}

static void scan_sd_images(void) {
    if (!sd_available) return;
    File root = SD.open("/");
    if (!root) return;
    while (true) {
        File file = root.openNextFile();
        if (!file) break;
        const char *name = file.name();
        if (!file.isDirectory() && is_image_name(name)) {
            char path[CARDPUTER_IMAGE_PATH_LEN];
            snprintf(path, sizeof(path), "%s%s", name[0] == '/' ? "" : "/", name);
            add_boot_image(CARDPUTER_STORAGE_SD, path, file.size());
        }
        file.close();
    }
    root.close();
}

static void draw_menu(const char *title, const char *const *items, uint8_t count,
                      uint8_t selected, const char *footer) {
    auto &display = M5Cardputer.Display;
    display.fillScreen(TFT_BLACK);
    display.fillRect(0, 0, display.width(), 16, TFT_BLUE);
    display.setTextColor(TFT_WHITE, TFT_BLUE);
    display.setTextSize(1);
    display.setCursor(4, 4);
    display.print(title);

    const uint8_t visible = 12;
    uint8_t first = selected >= visible ? selected - visible + 1 : 0;
    for (uint8_t row = 0; row < visible && first + row < count; row++) {
        uint8_t index = first + row;
        int y = 20 + row * 9;
        if (index == selected) {
            display.fillRect(0, y - 1, display.width(), 9, TFT_DARKGREEN);
            display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
        } else {
            display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
        }
        display.setCursor(4, y);
        display.printf("%c %s", index == selected ? '>' : ' ', items[index]);
    }
    display.fillRect(0, display.height() - 10, display.width(), 10, TFT_NAVY);
    display.setTextColor(TFT_WHITE, TFT_NAVY);
    display.setCursor(3, display.height() - 9);
    display.print(footer);
}

static uint8_t choose_menu(const char *title, const char *const *items,
                           uint8_t count, uint8_t selected, bool timeout) {
    // Do not carry Ctrl/Enter/navigation keys from the previous screen into
    // this menu. A held Enter used to select item 0 (USB disk mode) when a
    // Settings submenu returned to its parent.
    do {
        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isPressed()) break;
        delay(10);
    } while (true);

    bool previous_up = false;
    bool previous_down = false;
    bool previous_enter = false;
    uint32_t deadline = millis() + 4000;
    bool redraw = true;

    while (true) {
        M5Cardputer.update();
        auto state = M5Cardputer.Keyboard.keysState();
        const bool up = M5Cardputer.Keyboard.isKeyPressed('w') ||
                        M5Cardputer.Keyboard.isKeyPressed(';');
        const bool down = M5Cardputer.Keyboard.isKeyPressed('s') ||
                          M5Cardputer.Keyboard.isKeyPressed('.');
        bool enter = state.enter;

        if (up && !previous_up) {
            selected = selected == 0 ? count - 1 : selected - 1;
            timeout = false;
            redraw = true;
        }
        if (down && !previous_down) {
            selected = (selected + 1) % count;
            timeout = false;
            redraw = true;
        }
        for (uint8_t i = 0; i < count && i < 9; i++) {
            if (M5Cardputer.Keyboard.isKeyPressed('1' + i)) {
                selected = i;
                enter = true;
                break;
            }
        }
        if (enter && !previous_enter) return selected;
        if (timeout && (int32_t)(deadline - millis()) <= 0) return selected;

        if (redraw) {
            draw_menu(title, items, count, selected,
                      timeout ? "W/S or arrows, Enter (auto 4s)" :
                                "W/S or arrows, Enter");
            redraw = false;
        }
        previous_up = up;
        previous_down = down;
        previous_enter = enter;
        delay(15);
    }
}

static bool text_input(const char *title, const char *hint, char *buffer,
                       size_t buffer_size, bool masked) {
    if (buffer_size == 0) return false;
    do {
        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isPressed()) break;
        delay(10);
    } while (true);

    size_t len = strnlen(buffer, buffer_size);
    bool redraw = true;
    while (true) {
        if (redraw) {
            auto &display = M5Cardputer.Display;
            display.fillScreen(TFT_BLACK);
            display.fillRect(0, 0, display.width(), 16, TFT_BLUE);
            display.setTextSize(1);
            display.setTextColor(TFT_WHITE, TFT_BLUE);
            display.setCursor(4, 4);
            display.print(title);
            display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
            display.setCursor(6, 24);
            display.print(hint);
            display.drawRect(5, 48, display.width() - 10, 18, TFT_DARKGREY);
            display.setCursor(9, 53);
            display.setTextColor(TFT_WHITE, TFT_BLACK);
            if (masked) {
                for (size_t i = 0; i < len && i < 36; i++) display.print('*');
            } else {
                const size_t start = len > 36 ? len - 36 : 0;
                display.print(buffer + start);
            }
            display.fillRect(0, display.height() - 10, display.width(), 10, TFT_NAVY);
            display.setTextColor(TFT_WHITE, TFT_NAVY);
            display.setCursor(3, display.height() - 9);
            display.print("Enter=save  Esc/Ctrl=cancel  Del=back");
            redraw = false;
        }

        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
            auto state = M5Cardputer.Keyboard.keysState();
            if (state.ctrl || M5Cardputer.Keyboard.isKeyPressed('`')) return false;
            if (state.enter) {
                buffer[len] = 0;
                return true;
            }
            if (state.del) {
                if (len > 0) buffer[--len] = 0;
                redraw = true;
            } else {
                for (char c : state.word) {
                    if (c >= 32 && c <= 126 && len + 1 < buffer_size) {
                        buffer[len++] = c;
                        buffer[len] = 0;
                    }
                }
                redraw = true;
            }
        }
        delay(15);
    }
}

static bool configure_wifi_from_scan(void) {
    show_probe_status("Scanning Wi-Fi...", "Please wait");
    CardputerWifiNetwork networks[CARDPUTER_MODEM_MAX_SCAN];
    const int count = cardputer_modem_scan_networks(
        networks, CARDPUTER_MODEM_MAX_SCAN);
    if (count <= 0) {
        show_probe_status("No Wi-Fi networks found",
                          count < 0 ? "Scan failed" : "Try again");
        delay(1500);
        return false;
    }

    char labels[CARDPUTER_MODEM_MAX_SCAN][48];
    const char *network_items[CARDPUTER_MODEM_MAX_SCAN];
    for (int i = 0; i < count; i++) {
        snprintf(labels[i], sizeof(labels[i]), "%c %3ld dBm %.30s",
                 networks[i].encrypted ? '*' : ' ',
                 (long)networks[i].rssi, networks[i].ssid);
        network_items[i] = labels[i];
    }
    const uint8_t selected = choose_menu(
        "Select Wi-Fi SSID", network_items, count, 0, false);

    char pass[65] = {};
    if (networks[selected].encrypted) {
        char hint[64];
        snprintf(hint, sizeof(hint), "Password for %.28s",
                 networks[selected].ssid);
        if (!text_input("Wi-Fi password", hint, pass, sizeof(pass), true)) {
            return false;
        }
    } else {
        show_probe_status("Open Wi-Fi selected", networks[selected].ssid);
        delay(700);
    }

    if (!cardputer_modem_set_wifi(networks[selected].ssid, pass)) {
        show_probe_status("Wi-Fi save failed", "NVS write error");
        delay(1500);
        return false;
    }
    return true;
}

static void show_wifi_settings_menu(void) {
    while (true) {
        char ssid_item[48];
        char status_item[32];
        snprintf(ssid_item, sizeof(ssid_item), "SSID: %.34s",
                 cardputer_modem_ssid()[0] ? cardputer_modem_ssid() : "(not set)");
        snprintf(status_item, sizeof(status_item), "Wi-Fi: %s",
                 cardputer_modem_wifi_connected() ? "connected" : "disconnected");
        const char *items[] = {
            "Configure Wi-Fi", ssid_item, status_item, "Connect saved Wi-Fi",
            "Clear Wi-Fi settings", "Back"
        };
        const uint8_t choice = choose_menu("Wi-Fi settings", items, 6, 0, false);
        if (choice == 0) {
            configure_wifi_from_scan();
            continue;
        }
        if (choice == 3) {
            show_probe_status("Connecting Wi-Fi...", cardputer_modem_ssid());
            const bool ok = cardputer_modem_connect_saved(15000);
            show_probe_status(ok ? "Wi-Fi connected" : "Wi-Fi connect failed",
                              ok ? "Wi-Fi ready" : "Check SSID/password");
            delay(1500);
            continue;
        }
        if (choice == 4) {
            if (!cardputer_modem_clear_wifi()) {
                show_probe_status("Clear Wi-Fi failed", "NVS write error");
                delay(1500);
            }
            continue;
        }
        if (choice == 5) return;
    }
}

static void show_hayes_modem_menu(void) {
    while (true) {
        char status_item[40];
        char wifi_item[48];
        snprintf(status_item, sizeof(status_item), "COM1: 0x3F8 Hayes modem");
        snprintf(wifi_item, sizeof(wifi_item), "Wi-Fi: %s",
                 cardputer_modem_wifi_connected() ? "connected" : "disconnected");
        const char *items[] = {
            status_item, wifi_item, "DOS: MODEM.COM tests AT/OK",
            "Dial: ATDT host:port", "Back"
        };
        const uint8_t choice = choose_menu("COM1 Hayes modem", items, 5, 4, false);
        if (choice == 4) return;
    }
}

static void set_disk_geometry(uint32_t size) {
    gb_disk_image.size = size;
    gb_disk_image.drive = 0;
    gb_disk_image.heads = 2;
    gb_disk_image.sectors = 18;
    gb_disk_image.cylinders = 80;

    switch (size) {
        case 163840: gb_disk_image.heads = 1; gb_disk_image.sectors = 8; gb_disk_image.cylinders = 40; break;
        case 184320: gb_disk_image.heads = 1; gb_disk_image.sectors = 9; gb_disk_image.cylinders = 40; break;
        case 327680: gb_disk_image.heads = 2; gb_disk_image.sectors = 8; gb_disk_image.cylinders = 40; break;
        case 368640: gb_disk_image.heads = 2; gb_disk_image.sectors = 9; gb_disk_image.cylinders = 40; break;
        case 737280: gb_disk_image.heads = 2; gb_disk_image.sectors = 9; gb_disk_image.cylinders = 80; break;
        case 1228800: gb_disk_image.heads = 2; gb_disk_image.sectors = 15; gb_disk_image.cylinders = 80; break;
        case 1474560: gb_disk_image.heads = 2; gb_disk_image.sectors = 18; gb_disk_image.cylinders = 80; break;
        case 2949120: gb_disk_image.heads = 2; gb_disk_image.sectors = 36; gb_disk_image.cylinders = 80; break;
        default:
            if (size > 2949120) {
                gb_disk_image.drive = 0x80;
                gb_disk_image.heads = 16;
                gb_disk_image.sectors = 63;
                gb_disk_image.cylinders = size / (512UL * 16UL * 63UL);
                if (gb_disk_image.cylinders == 0) gb_disk_image.cylinders = 1;
                if (gb_disk_image.cylinders > 1024) gb_disk_image.cylinders = 1024;
            }
            break;
    }
}

static bool select_boot_image(void) {
    if (boot_image_count == 0) {
        unmount_sd();
        unmount_flash();
        return false;
    }

    uint8_t selected = 0;
    const char *items[MAX_BOOT_IMAGES];
    static char labels[MAX_BOOT_IMAGES][32];
    for (uint8_t i = 0; i < boot_image_count; i++) {
        snprintf(labels[i], sizeof(labels[i]), "[%c] %.26s",
                 boot_images[i].storage == CARDPUTER_STORAGE_FLASH ? 'F' : 'S',
                 base_name(boot_images[i].path));
        items[i] = labels[i];
        if (boot_images[i].storage == CARDPUTER_STORAGE_FLASH &&
            strcasecmp(base_name(boot_images[i].path), "cardputer86.img") == 0) {
            selected = i;
        }
    }
    if (boot_image_count > 1) {
        selected = choose_menu("Select boot image", items, boot_image_count,
                               selected, true);
    }

    const BootImageEntry &entry = boot_images[selected];
    gb_disk_image = {};
    gb_disk_image.storage = entry.storage;
    strncpy(gb_disk_image.path, entry.path, sizeof(gb_disk_image.path) - 1);
    set_disk_geometry(entry.size);

    if (entry.storage == CARDPUTER_STORAGE_FLASH) {
        if (!mount_flash()) {
            gb_disk_image = {};
            return false;
        }
        flash_image_file = fopen(gb_disk_image.path,
                                 flash_raw_fallback ? "rb" : "r+b");
        gb_disk_image.mounted = flash_image_file != nullptr;
        if (!gb_disk_image.mounted) unmount_flash();
    } else {
        if (!mount_sd()) {
            gb_disk_image = {};
            return false;
        }
        sd_image_file = SD.open(gb_disk_image.path, "r+");
        gb_disk_image.mounted = (bool)sd_image_file;
        if (!gb_disk_image.mounted) unmount_sd();
    }
#ifdef use_lib_log_serial
    if (gb_disk_image.mounted) {
        Serial.printf("Boot IMG: source=%s path=%s size=%lu drive=%02X\n",
                      gb_disk_image.storage == CARDPUTER_STORAGE_FLASH ?
                      "flash" : "sd", gb_disk_image.path,
                      (unsigned long)gb_disk_image.size, gb_disk_image.drive);
    } else {
        Serial.printf("Boot IMG open failed: %s\n", gb_disk_image.path);
    }
#endif
    return gb_disk_image.mounted;
}

bool cardputer_storage_init_and_select(void) {
    gb_disk_image = {};
    boot_image_count = 0;
    flash_detected = false;
    sd_detected = false;

    // Scan one filesystem at a time. Mounting FFat and SD together after the
    // emulator RAM reservation can exhaust or fragment the remaining heap.
    show_probe_status("Checking internal IMG partition...");
    if (mount_flash()) {
        flash_detected = true;
        scan_flash_images();
        unmount_flash();
    }
    if (alt_requests_sd()) {
        show_probe_status("Alt held; checking SD...", "Timeout: 15 seconds");
        if (mount_sd_timed()) {
            sd_detected = true;
            scan_sd_images();
            unmount_sd();
        }
    } else {
        show_probe_status("SD skipped", "Using internal IMG only");
        delay(250);
    }

    // Settings is offered after storage probing and before a boot image
    // filesystem is reopened. Raw MSC access must not overlap a mounted image.
    cardputer_storage_enter_usb_mode_if_requested();
    show_probe_status("Selecting boot image...");

#ifdef use_lib_log_serial
    Serial.printf("Storage: images=%u\n", boot_image_count);
#endif
    return select_boot_image();
}

void cardputer_storage_show_boot_status(void) {
    auto &display = M5Cardputer.Display;
    display.fillScreen(TFT_BLACK);
    display.fillRect(0, 0, display.width(), 16, TFT_BLUE);
    display.setTextSize(1);
    display.setTextColor(TFT_WHITE, TFT_BLUE);
    display.setCursor(4, 4);
    display.print("CardPuter86 POST");

    display.setTextColor(flash_detected ? TFT_GREEN : TFT_RED, TFT_BLACK);
    display.setCursor(6, 24);
    display.printf("Internal IMG partition: %s",
                   flash_detected ? "ready" : "unavailable");

    display.setTextColor(sd_detected ? TFT_GREEN : TFT_DARKGREY, TFT_BLACK);
    display.setCursor(6, 37);
    display.printf("SD card: %s", sd_detected ? "detected" : "not inserted");

    display.setTextColor(guest_memory_512k_enabled() ? TFT_GREEN : TFT_YELLOW,
                         TFT_BLACK);
    display.setCursor(6, 50);
    display.printf("Guest RAM: %s",
                   guest_memory_512k_enabled() ? "512 KB" : "128 KB");

    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.setCursor(6, 63);
    display.printf("CPU speed: %s",
                   cardputer_cpu_profile_label(cardputer_cpu_profile()));

    if (gb_disk_image.mounted) {
        display.setTextColor(TFT_CYAN, TFT_BLACK);
        display.setCursor(6, 76);
        display.printf("Boot source: %s",
                       gb_disk_image.storage == CARDPUTER_STORAGE_FLASH ?
                       "Internal Flash" : "SD card");
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.setCursor(6, 89);
        display.printf("Image: %.34s", base_name(gb_disk_image.path));
        display.setCursor(6, 102);
        display.printf("Size: %lu KB", (unsigned long)(gb_disk_image.size / 1024));
        display.setCursor(6, 115);
        display.printf("Emulated drive: %s",
                       gb_disk_image.drive == 0x80 ? "C: hard disk" : "A: floppy");
        display.setTextColor(TFT_GREEN, TFT_BLACK);
        display.setCursor(150, 125);
        display.print("Ready");
        delay(1200);
    } else {
        display.setTextColor(TFT_RED, TFT_BLACK);
        display.setCursor(6, 66);
        display.print("No bootable IMG found");
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.setCursor(6, 84);
        display.print("Press Ctrl for Settings");
        display.setCursor(6, 97);
        display.print("and choose USB disk mode");
        delay(2500);
    }
}

bool cardputer_storage_read_sector(unsigned long lba, unsigned char *buffer) {
    if (!gb_disk_image.mounted || (lba + 1) * 512UL > gb_disk_image.size) return false;
    if (gb_disk_image.storage == CARDPUTER_STORAGE_FLASH) {
        return flash_image_file &&
               fseek(flash_image_file, lba * 512UL, SEEK_SET) == 0 &&
               fread(buffer, 1, 512, flash_image_file) == 512;
    }
    if (gb_disk_image.storage == CARDPUTER_STORAGE_SD) {
        return sd_image_file && sd_image_file.seek(lba * 512UL) &&
               sd_image_file.read(buffer, 512) == 512;
    }
    return false;
}

bool cardputer_storage_write_sector(unsigned long lba, const unsigned char *buffer) {
    if (!gb_disk_image.mounted || (lba + 1) * 512UL > gb_disk_image.size) return false;
    if (gb_disk_image.storage == CARDPUTER_STORAGE_FLASH) {
        if (flash_raw_fallback) return false;
        if (!flash_image_file) return false;
        bool ok = fseek(flash_image_file, lba * 512UL, SEEK_SET) == 0 &&
                  fwrite(buffer, 1, 512, flash_image_file) == 512;
        fflush(flash_image_file);
        return ok;
    }
    if (gb_disk_image.storage == CARDPUTER_STORAGE_SD) {
        if (!sd_image_file) return false;
        bool ok = sd_image_file.seek(lba * 512UL) &&
                  sd_image_file.write(buffer, 512) == 512;
        sd_image_file.flush();
        return ok;
    }
    return false;
}

static bool flush_flash_cache(void) {
    if (!usb_flash_cache_dirty || usb_flash_cache_address == SIZE_MAX) return true;
    if (wl_erase_range(usb_flash_wl, usb_flash_cache_address,
                       usb_flash_cache_size) != ESP_OK ||
        wl_write(usb_flash_wl, usb_flash_cache_address, usb_flash_cache,
                 usb_flash_cache_size) != ESP_OK) {
        return false;
    }
    usb_flash_cache_dirty = false;
    return true;
}

static bool load_flash_cache(size_t address) {
    const size_t block = address - address % usb_flash_cache_size;
    if (block == usb_flash_cache_address) return true;
    if (!flush_flash_cache()) return false;
    if (wl_read(usb_flash_wl, block, usb_flash_cache,
                usb_flash_cache_size) != ESP_OK) return false;
    usb_flash_cache_address = block;
    return true;
}

static int32_t usb_read(uint32_t lba, uint32_t offset, void *buffer,
                        uint32_t size) {
    uint8_t *destination = static_cast<uint8_t *>(buffer);
    uint32_t copied = 0;
    while (copied < size) {
        const size_t address = (size_t)lba * usb_block_size + offset + copied;
        uint32_t chunk;
        if (usb_storage == CARDPUTER_STORAGE_SD) {
            const uint32_t sector = address / 512;
            const uint32_t sector_offset = address % 512;
            chunk = min(size - copied, 512U - sector_offset);
            if (!SD.readRAW(sd_usb_sector, sector)) return copied ? copied : -1;
            memcpy(destination + copied, sd_usb_sector + sector_offset, chunk);
        } else {
            if (!load_flash_cache(address)) return copied ? copied : -1;
            const size_t cache_offset = address - usb_flash_cache_address;
            chunk = min((size_t)(size - copied), usb_flash_cache_size - cache_offset);
            memcpy(destination + copied, usb_flash_cache + cache_offset, chunk);
        }
        copied += chunk;
    }
    return copied;
}

static int32_t usb_write(uint32_t lba, uint32_t offset, uint8_t *buffer,
                         uint32_t size) {
    uint32_t copied = 0;
    while (copied < size) {
        const size_t address = (size_t)lba * usb_block_size + offset + copied;
        uint32_t chunk;
        if (usb_storage == CARDPUTER_STORAGE_SD) {
            const uint32_t sector = address / 512;
            const uint32_t sector_offset = address % 512;
            chunk = min(size - copied, 512U - sector_offset);
            if ((sector_offset || chunk != 512) &&
                !SD.readRAW(sd_usb_sector, sector)) return copied ? copied : -1;
            memcpy(sd_usb_sector + sector_offset, buffer + copied, chunk);
            if (!SD.writeRAW(sd_usb_sector, sector)) return copied ? copied : -1;
        } else {
            if (!load_flash_cache(address)) return copied ? copied : -1;
            const size_t cache_offset = address - usb_flash_cache_address;
            chunk = min((size_t)(size - copied), usb_flash_cache_size - cache_offset);
            memcpy(usb_flash_cache + cache_offset, buffer + copied, chunk);
            usb_flash_cache_dirty = true;
        }
        copied += chunk;
    }
    return copied;
}

static bool usb_start_stop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    flush_flash_cache();
    usb_msc.mediaPresent(!load_eject || start);
    return true;
}

static bool start_flash_usb(void) {
    const esp_partition_t *partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
        FLASH_PARTITION_LABEL);
    if (!partition || wl_mount(partition, &usb_flash_wl) != ESP_OK) return false;
    usb_flash_cache_size = wl_sector_size(usb_flash_wl);
    usb_flash_cache = static_cast<uint8_t *>(malloc(usb_flash_cache_size));
    if (!usb_flash_cache) {
        wl_unmount(usb_flash_wl);
        usb_flash_wl = WL_INVALID_HANDLE;
        return false;
    }
    usb_storage = CARDPUTER_STORAGE_FLASH;
    return true;
}

static bool start_usb_msc(CardputerStorageType storage) {
    uint32_t sector_count = 0;
    if (storage == CARDPUTER_STORAGE_SD) {
        usb_storage = CARDPUTER_STORAGE_SD;
        usb_block_size = 512;
        sector_count = SD.cardSize() / 512;
    } else {
        if (!start_flash_usb()) return false;
        usb_block_size = usb_flash_cache_size;
        sector_count = wl_size(usb_flash_wl) / usb_block_size;
    }
#ifdef use_lib_log_serial
    Serial.printf("USB MSC: source=%s blocks=%lu block_size=%u\n",
                  storage == CARDPUTER_STORAGE_SD ? "sd" : "flash",
                  (unsigned long)sector_count, usb_block_size);
#endif

    usb_msc.vendorID("M5Stack");
    usb_msc.productID(storage == CARDPUTER_STORAGE_SD ?
                      "CardPuter86 SD" : "CardPuter86 Flash");
    usb_msc.productRevision("1.0");
    usb_msc.onStartStop(usb_start_stop);
    usb_msc.onRead(usb_read);
    usb_msc.onWrite(usb_write);
    usb_msc.mediaPresent(true);
    if (!usb_msc.begin(sector_count, usb_block_size)) {
        usb_msc.end();
        if (usb_flash_wl != WL_INVALID_HANDLE) {
            free(usb_flash_cache);
            usb_flash_cache = nullptr;
            wl_unmount(usb_flash_wl);
            usb_flash_wl = WL_INVALID_HANDLE;
        }
        return false;
    }
    USB.productName(storage == CARDPUTER_STORAGE_SD ?
                    "CardPuter86 SD Card" : "CardPuter86 Internal Disk");
    USB.begin();
    return true;
}

static bool start_selected_usb_mode(void) {
    auto &display = M5Cardputer.Display;
    CardputerStorageType selected = CARDPUTER_STORAGE_FLASH;
    if (sd_detected && mount_sd_timed()) {
        const char *items[] = {"Internal Flash", "SD Card"};
        uint8_t choice = choose_menu("USB storage source", items, 2, 0, false);
        selected = choice == 0 ? CARDPUTER_STORAGE_FLASH : CARDPUTER_STORAGE_SD;
        if (selected == CARDPUTER_STORAGE_FLASH) unmount_sd();
    }

    display.fillScreen(TFT_BLACK);
    display.setCursor(8, 48);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.print("Starting USB storage...");
    if (!start_usb_msc(selected)) {
        display.setCursor(8, 68);
        display.setTextColor(TFT_RED, TFT_BLACK);
        display.print("USB storage init failed");
        delay(2000);
        return false;
    }

    display.fillScreen(TFT_BLACK);
    display.setCursor(8, 44);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.print(selected == CARDPUTER_STORAGE_SD ?
                  "SD USB disk active" : "Flash USB disk active");
    display.setCursor(8, 64);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.print("Copy IMG files, then eject");
    display.setCursor(8, 78);
    display.print("Reboot after safe eject");
    while (true) delay(1000);
}

bool cardputer_storage_enter_usb_mode_if_requested(void) {
    show_probe_status("Storage check complete", "Press Ctrl for Settings (1.5s)");
    const uint32_t detection_deadline = millis() + 1500;
    bool ctrl_pressed = false;
    do {
        M5Cardputer.update();
        ctrl_pressed = M5Cardputer.Keyboard.keysState().ctrl;
        if (ctrl_pressed) break;
        delay(10);
    } while ((int32_t)(detection_deadline - millis()) > 0);
    if (!ctrl_pressed) return false;

    while (true) {
        char ram_item[32];
        char cpu_item[32];
        char sound_item[32];
        char wifi_item[48];
        snprintf(ram_item, sizeof(ram_item), "512 KB memory: %s",
                 guest_memory_512k_enabled() ? "Enabled" : "Disabled");
        snprintf(cpu_item, sizeof(cpu_item), "CPU speed: %s",
                 cardputer_cpu_profile_label(cardputer_cpu_profile()));
        snprintf(sound_item, sizeof(sound_item), "POST sound: %s",
                 cardputer_settings_post_sound_enabled() ? "Enabled" : "Disabled");
        snprintf(wifi_item, sizeof(wifi_item), "Wi-Fi: %.34s",
                 cardputer_modem_ssid()[0] ? cardputer_modem_ssid() : "(not set)");
        const char *items[] = {
            "USB disk mode", ram_item, cpu_item, sound_item, wifi_item,
            "COM1 Hayes modem", "Continue boot"
        };
        const uint8_t choice = choose_menu(
            "CardPuter86 Settings", items, 7, 0, false);
        if (choice == 0) return start_selected_usb_mode();
        if (choice == 1) {
            const bool enable = !guest_memory_512k_enabled();
            if (!guest_memory_set_512k_enabled(enable)) {
                show_probe_status("512 KB mode failed", "Swap partition unavailable");
                delay(1500);
            }
            continue;
        }
        if (choice == 2) {
            const char *cpu_profiles[] = {
                "4.77 MHz (IBM PC)", "8 MHz", "10 MHz", "12 MHz",
                "Unlimited (fastest)", "16 MHz", "24 MHz", "33 MHz"
            };
            const uint8_t profile = choose_menu(
                "CPU speed", cpu_profiles, cardputer_cpu_profile_count(),
                cardputer_cpu_profile(), false);
            if (!cardputer_cpu_set_profile(profile)) {
                show_probe_status("CPU setting failed", "NVS write error");
                delay(1500);
            }
            continue;
        }
        if (choice == 3) {
            const bool enable = !cardputer_settings_post_sound_enabled();
            if (!cardputer_settings_set_post_sound_enabled(enable)) {
                show_probe_status("POST sound failed", "NVS write error");
                delay(1500);
            }
            continue;
        }
        if (choice == 4) {
            show_wifi_settings_menu();
            continue;
        }
        if (choice == 5) {
            show_hayes_modem_menu();
            continue;
        }
        return false;
    }
}
