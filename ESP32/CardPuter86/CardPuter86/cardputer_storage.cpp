#include "cardputer_storage.h"
#include "hardware.h"
#include "guest_memory.h"
#include "cardputer_cpu.h"
#include "cardputer_input.h"
#include "cardputer_rtc.h"
#include "cardputer_settings.h"
#include "cardputer_modem.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <USB.h>
#include <USBCDC.h>
#include <USBMSC.h>
#include <WiFi.h>
#include <dirent.h>
#include <stdio.h>
#include <strings.h>
#include <sys/stat.h>

extern "C" {
#include "esp_partition.h"
#include "esp_vfs_fat.h"
#include "nvs.h"
#include "wear_levelling.h"
}

static const char *FLASH_MOUNT_POINT = "/flash";
static const char *FLASH_PARTITION_LABEL = "disk";
static const uint8_t MAX_BOOT_IMAGES = 20;
static const uint8_t MENU_CANCEL = 0xFF;
static const uint8_t DRIVE_IDS[CARDPUTER_DRIVE_COUNT] = {0x00, 0x01, 0x80, 0x81};

struct BootImageEntry {
    CardputerStorageType storage;
    uint32_t size;
    char path[CARDPUTER_IMAGE_PATH_LEN];
};

static SPIClass sd_spi;
static bool sd_available = false;
static bool flash_available = false;
static bool sd_detected = false;
static bool flash_detected = false;
static wl_handle_t flash_wl_handle = WL_INVALID_HANDLE;
static FILE *flash_image_files[CARDPUTER_DRIVE_COUNT] = {};
static File sd_image_files[CARDPUTER_DRIVE_COUNT];
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
CardputerDiskImage gb_disk_drives[CARDPUTER_DRIVE_COUNT] = {};
static USBCDC usb_cdc;

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

static int8_t drive_slot(uint8_t drive) {
    for (uint8_t i = 0; i < CARDPUTER_DRIVE_COUNT; i++) {
        if (DRIVE_IDS[i] == drive) return i;
    }
    return -1;
}

static const char *drive_label(uint8_t drive) {
    switch (drive) {
        case 0x00: return "A:";
        case 0x01: return "B:";
        case 0x80: return "C:";
        case 0x81: return "D:";
        default: return "?:";
    }
}

static void set_disk_geometry(CardputerDiskImage &image, uint8_t drive,
                              uint32_t size) {
    image.size = size;
    image.drive = drive;
    image.heads = 2;
    image.sectors = 18;
    image.cylinders = 80;

    switch (size) {
        case 163840: image.heads = 1; image.sectors = 8; image.cylinders = 40; break;
        case 184320: image.heads = 1; image.sectors = 9; image.cylinders = 40; break;
        case 327680: image.heads = 2; image.sectors = 8; image.cylinders = 40; break;
        case 368640: image.heads = 2; image.sectors = 9; image.cylinders = 40; break;
        case 737280: image.heads = 2; image.sectors = 9; image.cylinders = 80; break;
        case 1228800: image.heads = 2; image.sectors = 15; image.cylinders = 80; break;
        case 1474560: image.heads = 2; image.sectors = 18; image.cylinders = 80; break;
        case 2949120: image.heads = 2; image.sectors = 36; image.cylinders = 80; break;
        default:
            image.heads = 16;
            image.sectors = 63;
            image.cylinders = size / (512UL * 16UL * 63UL);
            if (image.cylinders == 0) image.cylinders = 1;
            if (image.cylinders > 1024) image.cylinders = 1024;
            break;
    }
}

static uint8_t default_drive_for_image(uint32_t size) {
    return size > 2949120 ? 0x80 : 0x00;
}

static bool nvs_read_last_image(CardputerStorageType *storage, char *path,
                                size_t path_size) {
    if (!storage || !path || path_size == 0) return false;
    nvs_handle_t handle;
    if (nvs_open("cardputer86", NVS_READONLY, &handle) != ESP_OK) return false;
    uint8_t saved_storage = 0;
    size_t len = path_size;
    const bool ok = nvs_get_u8(handle, "last_store", &saved_storage) == ESP_OK &&
                    nvs_get_str(handle, "last_img", path, &len) == ESP_OK &&
                    (saved_storage == CARDPUTER_STORAGE_FLASH ||
                     saved_storage == CARDPUTER_STORAGE_SD);
    nvs_close(handle);
    if (ok) *storage = (CardputerStorageType)saved_storage;
    return ok;
}

static void nvs_save_last_image(CardputerStorageType storage, const char *path) {
    nvs_handle_t handle;
    if (!path || nvs_open("cardputer86", NVS_READWRITE, &handle) != ESP_OK) return;
    nvs_set_u8(handle, "last_store", storage);
    nvs_set_str(handle, "last_img", path);
    nvs_commit(handle);
    nvs_close(handle);
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
    esp_vfs_fat_mount_config_t config = {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 4096
    };
    esp_err_t result = esp_vfs_fat_spiflash_mount(
        FLASH_MOUNT_POINT, FLASH_PARTITION_LABEL, &config, &flash_wl_handle);
    if (result != ESP_OK) {
#ifdef use_lib_log_serial
        Serial.printf("Internal IMG partition mount failed: %s\n",
                      esp_err_to_name(result));
#endif
        flash_wl_handle = WL_INVALID_HANDLE;
        return false;
    }
    flash_available = true;
    return true;
}

static void unmount_flash(void) {
    if (flash_available && flash_wl_handle != WL_INVALID_HANDLE) {
        esp_vfs_fat_spiflash_unmount(FLASH_MOUNT_POINT, flash_wl_handle);
    }
    flash_wl_handle = WL_INVALID_HANDLE;
    flash_available = false;
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
    if (strcmp(title, "CardPuter86 Settings") == 0) {
        display.print("Settings");
        CardputerRtcTime now = cardputer_rtc_now();
        char status_text[32];
        const int32_t battery = M5Cardputer.Power.getBatteryLevel();
        if (battery >= 0) {
            snprintf(status_text, sizeof(status_text), "%02u:%02u:%02u  BAT %ld%%",
                     now.hour, now.minute, now.second, (long)battery);
        } else {
            snprintf(status_text, sizeof(status_text), "%02u:%02u:%02u  BAT --%%",
                     now.hour, now.minute, now.second);
        }
        const int status_x = display.width() - (int)strlen(status_text) * 6 - 4;
        display.setCursor(status_x > 88 ? status_x : 88, 4);
        display.print(status_text);
    } else {
        display.print(title);
    }

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

    uint32_t deadline = millis() + 4000;
    bool redraw = true;

    while (true) {
        M5Cardputer.update();
        const bool up = cardputer_input_consume_char('w') ||
                        cardputer_input_consume_char('W') ||
                        cardputer_input_consume(CARDPUTER_VK_UP);
        const bool down = cardputer_input_consume_char('s') ||
                          cardputer_input_consume_char('S') ||
                          cardputer_input_consume(CARDPUTER_VK_DOWN);
        if (cardputer_input_consume(CARDPUTER_VK_ESC) ||
            cardputer_input_consume_char('`')) {
            return MENU_CANCEL;
        }
        bool enter = cardputer_input_consume(CARDPUTER_VK_ENTER);

        if (up) {
            selected = selected == 0 ? count - 1 : selected - 1;
            timeout = false;
            redraw = true;
        }
        if (down) {
            selected = (selected + 1) % count;
            timeout = false;
            redraw = true;
        }
        for (uint8_t i = 0; i < count && i < 9; i++) {
            if (cardputer_input_consume_char('1' + i)) {
                selected = i;
                enter = true;
                break;
            }
        }
        if (enter) return selected;
        if (timeout && (int32_t)(deadline - millis()) <= 0) return selected;

        if (redraw) {
            draw_menu(title, items, count, selected,
                      timeout ? "W/S/arrows Enter Esc (auto 4s)" :
                                "W/S/arrows Enter Esc");
            redraw = false;
        }
        delay(15);
    }
}

static char consume_printable_char(void) {
    for (char key = 32; key < 127; key++) {
        if (cardputer_input_consume_char(key)) return key;
    }
    return 0;
}

static bool input_text(const char *title, const char *prompt, char *out,
                       size_t out_size, bool mask) {
    if (!out || out_size == 0) return false;
    out[0] = '\0';
    uint8_t pos = 0;
    do {
        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isPressed()) break;
        delay(10);
    } while (true);

    while (true) {
        auto &display = M5Cardputer.Display;
        display.fillScreen(TFT_BLACK);
        display.fillRect(0, 0, display.width(), 16, TFT_BLUE);
        display.setTextColor(TFT_WHITE, TFT_BLUE);
        display.setCursor(4, 4);
        display.print(title);
        display.setTextColor(TFT_WHITE, TFT_BLACK);
        display.setCursor(6, 28);
        display.print(prompt);
        display.setCursor(6, 48);
        if (mask) {
            for (uint8_t i = 0; i < pos; i++) display.print('*');
        } else {
            display.print(out);
        }
        display.setTextColor(TFT_YELLOW, TFT_BLACK);
        display.setCursor(6, 78);
        display.print("Enter=save Backspace=del");
        display.setCursor(6, 92);
        display.print("Esc=cancel");

        M5Cardputer.update();
        if (cardputer_input_consume(CARDPUTER_VK_ESC) ||
            cardputer_input_consume_char('`')) {
            return false;
        }
        if (cardputer_input_consume(CARDPUTER_VK_BACKSPACE) && pos > 0) {
            out[--pos] = '\0';
        }
        if (cardputer_input_consume(CARDPUTER_VK_ENTER)) return true;
        const char c = consume_printable_char();
        if (c && pos + 1 < out_size) {
            out[pos++] = c;
            out[pos] = '\0';
        }
        delay(20);
    }
}

static bool show_wifi_modem_menu(void) {
    show_probe_status("Scanning WiFi...", "Please wait");
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false);
    delay(100);
    const int count = WiFi.scanNetworks(false, true);
    if (count <= 0) {
        show_probe_status("No WiFi networks found", "Try again near an AP");
        delay(1500);
        return false;
    }

    static char labels[12][40];
    const char *items[12];
    const uint8_t shown = count > 12 ? 12 : count;
    for (uint8_t i = 0; i < shown; i++) {
        const bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;
        snprintf(labels[i], sizeof(labels[i]), "%-24.24s %4d %s",
                 WiFi.SSID(i).c_str(), WiFi.RSSI(i), open ? "open" : "lock");
        items[i] = labels[i];
    }

    const uint8_t choice = choose_menu("WiFi modem AP", items, shown, 0, false);
    if (choice == MENU_CANCEL) {
        WiFi.scanDelete();
        return false;
    }

    char ssid[33];
    char pass[65] = {};
    strlcpy(ssid, WiFi.SSID(choice).c_str(), sizeof(ssid));
    const bool open = WiFi.encryptionType(choice) == WIFI_AUTH_OPEN;
    WiFi.scanDelete();

    if (!open) {
        if (!input_text("WiFi password", ssid, pass, sizeof(pass), true)) {
            return false;
        }
    }

    if (!cardputer_modem_save_wifi(ssid, pass)) {
        show_probe_status("WiFi save failed", "NVS write error");
        delay(1500);
        return false;
    }
    show_probe_status("WiFi saved", ssid);
    delay(1500);
    return true;
}

static bool close_drive(uint8_t drive) {
    const int8_t slot = drive_slot(drive);
    if (slot < 0) return false;
    if (gb_disk_drives[slot].storage == CARDPUTER_STORAGE_FLASH &&
        flash_image_files[slot]) {
        fclose(flash_image_files[slot]);
        flash_image_files[slot] = nullptr;
    }
    if (gb_disk_drives[slot].storage == CARDPUTER_STORAGE_SD &&
        sd_image_files[slot]) {
        sd_image_files[slot].close();
    }
    gb_disk_drives[slot] = {};
    return true;
}

static bool open_image_on_drive(const BootImageEntry &entry, uint8_t drive) {
    const int8_t slot = drive_slot(drive);
    if (slot < 0) return false;
    close_drive(drive);

    if (entry.storage == CARDPUTER_STORAGE_FLASH && !flash_available &&
        !mount_flash()) {
        return false;
    }
    if (entry.storage == CARDPUTER_STORAGE_SD && !sd_available &&
        !mount_sd()) {
        return false;
    }

    CardputerDiskImage image = {};
    image.storage = entry.storage;
    strncpy(image.path, entry.path, sizeof(image.path) - 1);
    set_disk_geometry(image, drive, entry.size);

    if (entry.storage == CARDPUTER_STORAGE_FLASH) {
        flash_image_files[slot] = fopen(image.path, "r+b");
        image.mounted = flash_image_files[slot] != nullptr;
    } else {
        sd_image_files[slot] = SD.open(image.path, "r+");
        image.mounted = (bool)sd_image_files[slot];
    }
    if (!image.mounted) {
        close_drive(drive);
        return false;
    }
    gb_disk_drives[slot] = image;
    if (drive == 0x00 || drive == 0x80 || !gb_disk_image.mounted) {
        gb_disk_image = image;
    }
    return true;
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
    CardputerStorageType last_storage = CARDPUTER_STORAGE_NONE;
    char last_path[CARDPUTER_IMAGE_PATH_LEN] = {};
    const bool has_last = nvs_read_last_image(&last_storage, last_path,
                                              sizeof(last_path));
    for (uint8_t i = 0; i < boot_image_count; i++) {
        snprintf(labels[i], sizeof(labels[i]), "[%c] %.26s",
                 boot_images[i].storage == CARDPUTER_STORAGE_FLASH ? 'F' : 'S',
                 base_name(boot_images[i].path));
        items[i] = labels[i];
        if (has_last && boot_images[i].storage == last_storage &&
            strcmp(boot_images[i].path, last_path) == 0) {
            selected = i;
        }
        if (boot_images[i].storage == CARDPUTER_STORAGE_FLASH &&
            strcasecmp(base_name(boot_images[i].path), "cardputer86.img") == 0 &&
            !has_last) {
            selected = i;
        }
    }
    if (boot_image_count > 1) {
        selected = choose_menu("Select boot image", items, boot_image_count,
                               selected, true);
        if (selected == MENU_CANCEL) selected = 0;
    }

    const BootImageEntry &entry = boot_images[selected];
    const uint8_t drive = default_drive_for_image(entry.size);
    gb_disk_image = {};
    if (open_image_on_drive(entry, drive)) {
        nvs_save_last_image(entry.storage, entry.path);
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
        display.print("Use storage import workflow");
        display.setCursor(6, 97);
        display.print("then reboot with an IMG");
        delay(2500);
    }
}

bool cardputer_storage_drive_geometry(uint8_t drive, uint16_t *cylinders,
                                      uint8_t *heads, uint8_t *sectors) {
    const int8_t slot = drive_slot(drive);
    if (slot < 0 || !gb_disk_drives[slot].mounted) return false;
    if (cylinders) *cylinders = gb_disk_drives[slot].cylinders;
    if (heads) *heads = gb_disk_drives[slot].heads;
    if (sectors) *sectors = gb_disk_drives[slot].sectors;
    return true;
}

uint8_t cardputer_storage_hard_drive_count(void) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < CARDPUTER_DRIVE_COUNT; i++) {
        if (gb_disk_drives[i].mounted && gb_disk_drives[i].drive >= 0x80) count++;
    }
    return count;
}

bool cardputer_storage_read_sector(uint8_t drive, unsigned long lba,
                                   unsigned char *buffer) {
    const int8_t slot = drive_slot(drive);
    if (slot < 0) return false;
    CardputerDiskImage &image = gb_disk_drives[slot];
    if (!image.mounted || (lba + 1) * 512UL > image.size) return false;
    if (image.storage == CARDPUTER_STORAGE_FLASH) {
        return flash_image_files[slot] &&
               fseek(flash_image_files[slot], lba * 512UL, SEEK_SET) == 0 &&
               fread(buffer, 1, 512, flash_image_files[slot]) == 512;
    }
    if (image.storage == CARDPUTER_STORAGE_SD) {
        return sd_image_files[slot] && sd_image_files[slot].seek(lba * 512UL) &&
               sd_image_files[slot].read(buffer, 512) == 512;
    }
    return false;
}

bool cardputer_storage_write_sector(uint8_t drive, unsigned long lba,
                                    const unsigned char *buffer) {
    const int8_t slot = drive_slot(drive);
    if (slot < 0) return false;
    CardputerDiskImage &image = gb_disk_drives[slot];
    if (!image.mounted || (lba + 1) * 512UL > image.size) return false;
    if (image.storage == CARDPUTER_STORAGE_FLASH) {
        if (!flash_image_files[slot]) return false;
        bool ok = fseek(flash_image_files[slot], lba * 512UL, SEEK_SET) == 0 &&
                  fwrite(buffer, 1, 512, flash_image_files[slot]) == 512;
        fflush(flash_image_files[slot]);
        return ok;
    }
    if (image.storage == CARDPUTER_STORAGE_SD) {
        if (!sd_image_files[slot]) return false;
        bool ok = sd_image_files[slot].seek(lba * 512UL) &&
                  sd_image_files[slot].write(buffer, 512) == 512;
        sd_image_files[slot].flush();
        return ok;
    }
    return false;
}

bool cardputer_storage_read_sector(unsigned long lba, unsigned char *buffer) {
    return gb_disk_image.mounted &&
           cardputer_storage_read_sector(gb_disk_image.drive, lba, buffer);
}

bool cardputer_storage_write_sector(unsigned long lba, const unsigned char *buffer) {
    return gb_disk_image.mounted &&
           cardputer_storage_write_sector(gb_disk_image.drive, lba, buffer);
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
        if (choice == MENU_CANCEL) {
            unmount_sd();
            return false;
        }
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

static bool start_usb_cdc_mode(void) {
    auto &display = M5Cardputer.Display;
    display.fillScreen(TFT_BLACK);
    display.setCursor(8, 40);
    display.setTextColor(TFT_CYAN, TFT_BLACK);
    display.print("USB CDC serial mode");
    display.setCursor(8, 58);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.print("Mutually exclusive with disk");

    USB.productName("CardPuter86 Serial");
    usb_cdc.begin();
    USB.begin();
    usb_cdc.println("CardPuter86 CDC serial ready.");
    usb_cdc.println("Reboot to leave this mode.");
    display.setCursor(8, 82);
    display.setTextColor(TFT_GREEN, TFT_BLACK);
    display.print("Serial active; reboot to exit");
    while (true) {
        if (usb_cdc.available()) {
            int c = usb_cdc.read();
            if (c >= 0) usb_cdc.write((uint8_t)c);
        }
        delay(10);
    }
}

bool cardputer_storage_show_mount_menu(void) {
    if (boot_image_count == 0) {
        show_probe_status("No IMG files found", "Use USB disk mode to import");
        delay(1500);
        return false;
    }

    const char *drive_items[] = {"A: floppy", "B: floppy", "C: hard disk", "D: hard disk"};
    uint8_t drive_choice = choose_menu("Mount target", drive_items, 4, 1, false);
    if (drive_choice == MENU_CANCEL) return false;
    const uint8_t drive = DRIVE_IDS[drive_choice];

    const char *items[MAX_BOOT_IMAGES];
    static char labels[MAX_BOOT_IMAGES][36];
    uint8_t selected = 0;
    for (uint8_t i = 0; i < boot_image_count; i++) {
        snprintf(labels[i], sizeof(labels[i]), "[%c] %.30s",
                 boot_images[i].storage == CARDPUTER_STORAGE_FLASH ? 'F' : 'S',
                 base_name(boot_images[i].path));
        items[i] = labels[i];
    }
    selected = choose_menu("Mount IMG", items, boot_image_count, selected, false);
    if (selected == MENU_CANCEL) return false;

    if ((drive < 0x80 && boot_images[selected].size > 2949120) ||
        (drive >= 0x80 && boot_images[selected].size <= 2949120)) {
        show_probe_status("Image/drive type mismatch",
                          drive < 0x80 ? "Choose floppy-sized IMG" : "Choose hard-disk IMG");
        delay(1800);
        return false;
    }

    if (!open_image_on_drive(boot_images[selected], drive)) {
        show_probe_status("Mount failed", base_name(boot_images[selected].path));
        delay(1500);
        return false;
    }
    show_probe_status("Mounted", base_name(boot_images[selected].path));
    delay(1000);
    return true;
}

bool cardputer_storage_show_settings_menu(bool allow_usb_disk) {
    while (true) {
        char ram_item[32];
        char cpu_item[32];
        char sound_item[32];
        char sleep_item[32];
        char sleep_led_item[32];
        char rtc_item[40];
        char mount_item[32];
        char usb_item[40];
        char wifi_item[40];
        snprintf(ram_item, sizeof(ram_item), "512 KB memory: %s",
                 guest_memory_512k_enabled() ? "Enabled" : "Disabled");
        snprintf(cpu_item, sizeof(cpu_item), "CPU speed: %s",
                 cardputer_cpu_profile_label(cardputer_cpu_profile()));
        snprintf(sound_item, sizeof(sound_item), "POST sound: %s",
                 cardputer_settings_post_sound_enabled() ? "Enabled" : "Disabled");
        const uint32_t sleep_seconds = cardputer_settings_sleep_timeout_seconds();
        if (sleep_seconds == 0) {
            snprintf(sleep_item, sizeof(sleep_item), "Sleep timeout: Never");
        } else if (sleep_seconds < 60) {
            snprintf(sleep_item, sizeof(sleep_item), "Sleep timeout: %lu sec",
                     (unsigned long)sleep_seconds);
        } else {
            snprintf(sleep_item, sizeof(sleep_item), "Sleep timeout: %lu min",
                     (unsigned long)(sleep_seconds / 60));
        }
        snprintf(sleep_led_item, sizeof(sleep_led_item), "Sleep LED: %s",
                 cardputer_settings_sleep_led_enabled() ? "Enabled" : "Disabled");
        char rtc_text[24];
        cardputer_rtc_format(cardputer_rtc_now(), rtc_text, sizeof(rtc_text));
        snprintf(rtc_item, sizeof(rtc_item), "RTC: %.20s", rtc_text);
        snprintf(mount_item, sizeof(mount_item), "Mount disk/IMG");
        const char *usb_labels[] = {"Charge only", "Serial CDC", "USB disk"};
        snprintf(usb_item, sizeof(usb_item), "USB interface: %s",
                 usb_labels[cardputer_settings_usb_mode()]);
        snprintf(wifi_item, sizeof(wifi_item), "WiFi modem: %s",
                 cardputer_modem_wifi_connected() ? "Connected" :
                 (cardputer_modem_wifi_configured() ? "Configured" : "Not set"));
        const char *items[] = {
            usb_item, mount_item, wifi_item, ram_item, cpu_item, sound_item,
            sleep_item, sleep_led_item, rtc_item, "Continue"
        };
        const uint8_t choice = choose_menu(
            "CardPuter86 Settings", items, 10, 0, false);
        if (choice == MENU_CANCEL) return false;
        if (choice == 0) {
            const char *usb_modes[] = {"Charge only", "Serial CDC", "USB disk"};
            uint8_t mode = choose_menu("USB interface", usb_modes, 3,
                                       cardputer_settings_usb_mode(), false);
            if (mode == MENU_CANCEL) continue;
            cardputer_settings_set_usb_mode(mode);
            if (mode == 0) {
                show_probe_status("USB: charge only", "No USB function started");
                delay(1000);
                continue;
            }
            if (mode == 1) return start_usb_cdc_mode();
            if (allow_usb_disk) return start_selected_usb_mode();
            show_probe_status("USB disk unavailable", "Use boot storage mode");
            delay(1500);
            continue;
        }
        if (choice == 1) {
            cardputer_storage_show_mount_menu();
            continue;
        }
        if (choice == 2) {
            show_wifi_modem_menu();
            continue;
        }
        if (choice == 3) {
            const bool enable = !guest_memory_512k_enabled();
            if (!guest_memory_set_512k_enabled(enable)) {
                show_probe_status("512 KB mode failed", "Swap partition unavailable");
                delay(1500);
            }
            continue;
        }
        if (choice == 4) {
            const char *cpu_profiles[] = {
                "4.77 MHz (IBM PC)", "8 MHz", "10 MHz", "12 MHz",
                "Unlimited (fastest)", "16 MHz", "24 MHz", "33 MHz"
            };
            const uint8_t profile = choose_menu(
                "CPU speed", cpu_profiles, cardputer_cpu_profile_count(),
                cardputer_cpu_profile(), false);
            if (profile == MENU_CANCEL) continue;
            if (!cardputer_cpu_set_profile(profile)) {
                show_probe_status("CPU setting failed", "NVS write error");
                delay(1500);
            }
            continue;
        }
        if (choice == 5) {
            const bool enable = !cardputer_settings_post_sound_enabled();
            if (!cardputer_settings_set_post_sound_enabled(enable)) {
                show_probe_status("POST sound failed", "NVS write error");
                delay(1500);
            }
            continue;
        }
        if (choice == 6) {
            const char *sleep_profiles[] = {
                "30 seconds", "2 minutes", "5 minutes", "10 minutes", "Never"
            };
            const uint32_t sleep_values[] = {30, 120, 300, 600, 0};
            uint8_t selected = 1;
            for (uint8_t i = 0; i < 5; i++) {
                if (sleep_values[i] == cardputer_settings_sleep_timeout_seconds()) {
                    selected = i;
                    break;
                }
            }
            const uint8_t profile = choose_menu(
                "Sleep timeout", sleep_profiles, 5, selected, false);
            if (profile == MENU_CANCEL) continue;
            if (!cardputer_settings_set_sleep_timeout_seconds(sleep_values[profile])) {
                show_probe_status("Sleep setting failed", "NVS write error");
                delay(1500);
            }
            continue;
        }
        if (choice == 7) {
            const bool enable = !cardputer_settings_sleep_led_enabled();
            if (!cardputer_settings_set_sleep_led_enabled(enable)) {
                show_probe_status("Sleep LED failed", "NVS write error");
                delay(1500);
            }
            continue;
        }
        if (choice == 8) {
            char input[15] = {};
            uint8_t pos = 0;
            while (true) {
                auto &display = M5Cardputer.Display;
                display.fillScreen(TFT_BLACK);
                display.fillRect(0, 0, display.width(), 16, TFT_BLUE);
                display.setTextColor(TFT_WHITE, TFT_BLUE);
                display.setCursor(4, 4);
                display.print("Set RTC");
                display.setTextColor(TFT_WHITE, TFT_BLACK);
                display.setCursor(6, 30);
                display.print("YYYYMMDDhhmmss");
                display.setCursor(6, 48);
                display.print(input);
                display.setTextColor(TFT_YELLOW, TFT_BLACK);
                display.setCursor(6, 70);
                display.print("Enter=save Backspace=del");
                display.setCursor(6, 84);
                display.print("Esc=cancel");

                M5Cardputer.update();
                const char digit = cardputer_input_consume_digit();
                if (digit != 0 && pos < 14) {
                    input[pos++] = digit;
                    input[pos] = '\0';
                }
                if (cardputer_input_consume(CARDPUTER_VK_BACKSPACE) && pos > 0) {
                    input[--pos] = '\0';
                }
                if (cardputer_input_consume(CARDPUTER_VK_ESC) ||
                    cardputer_input_consume_char('`')) break;
                if (cardputer_input_consume(CARDPUTER_VK_ENTER)) {
                    CardputerRtcTime rtc_time;
                    if (cardputer_rtc_parse_yyyymmddhhmmss(input, &rtc_time) &&
                        cardputer_rtc_set(rtc_time)) {
                        show_probe_status("RTC updated", input);
                    } else {
                        show_probe_status("RTC update failed", "Use YYYYMMDDhhmmss");
                    }
                    delay(1500);
                    break;
                }
                delay(20);
            }
            continue;
        }
        return false;
    }
}
