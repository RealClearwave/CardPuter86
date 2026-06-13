#include "cardputer_sd.h"
#include "hardware.h"
#include "gbConfig.h"
#include <Arduino.h>
#include <M5Cardputer.h>
#include "USB.h"
#include "USBMSC.h"

#ifdef use_lib_sdcard

static bool sd_available = false;
static SPIClass sd_spi;
static USBMSC *sd_usb_msc = nullptr;
static uint8_t sd_usb_sector[512];
static char sd_default_disk[MAX_SD_FILENAME_LEN];

int gb_sd_disk_count = 0;
CardputerSdDisk gb_sd_disk = {};

static void store_disk_path(char *destination, const char *name) {
    if (name[0] != '/') {
        destination[0] = '/';
        strncpy(destination + 1, name, MAX_SD_FILENAME_LEN - 2);
    } else {
        strncpy(destination, name, MAX_SD_FILENAME_LEN - 1);
    }
    destination[MAX_SD_FILENAME_LEN - 1] = '\0';
}

static bool sd_card_responds(void) {
    sd_spi.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);

    sd_spi.beginTransaction(SPISettings(400000, MSBFIRST, SPI_MODE0));
    for (uint8_t i = 0; i < 10; i++) sd_spi.transfer(0xFF);

    const uint32_t deadline = millis() + 150;
    bool detected = false;
    do {
        digitalWrite(SD_CS, LOW);
        sd_spi.transfer(0x40); // CMD0: GO_IDLE_STATE
        sd_spi.transfer(0x00);
        sd_spi.transfer(0x00);
        sd_spi.transfer(0x00);
        sd_spi.transfer(0x00);
        sd_spi.transfer(0x95);
        for (uint8_t i = 0; i < 10; i++) {
            uint8_t response = sd_spi.transfer(0xFF);
            if (response == 0x01) {
                detected = true;
                break;
            }
        }
        digitalWrite(SD_CS, HIGH);
        sd_spi.transfer(0xFF);
        if (!detected) delay(2);
    } while (!detected && (int32_t)(deadline - millis()) > 0);

    digitalWrite(SD_CS, HIGH);
    sd_spi.endTransaction();
    return detected;
}

static int32_t usb_msc_read(uint32_t lba, uint32_t offset,
                            void *buffer, uint32_t size) {
    uint8_t *dst = static_cast<uint8_t *>(buffer);
    uint32_t copied = 0;
    while (copied < size) {
        uint32_t sector = lba + (offset + copied) / 512;
        uint32_t sector_offset = (offset + copied) % 512;
        uint32_t chunk = min(size - copied, 512U - sector_offset);
        if (!SD.readRAW(sd_usb_sector, sector)) return copied ? copied : -1;
        memcpy(dst + copied, sd_usb_sector + sector_offset, chunk);
        copied += chunk;
    }
    return copied;
}

static int32_t usb_msc_write(uint32_t lba, uint32_t offset,
                             uint8_t *buffer, uint32_t size) {
    uint32_t copied = 0;
    while (copied < size) {
        uint32_t sector = lba + (offset + copied) / 512;
        uint32_t sector_offset = (offset + copied) % 512;
        uint32_t chunk = min(size - copied, 512U - sector_offset);
        if ((sector_offset != 0 || chunk != 512) &&
            !SD.readRAW(sd_usb_sector, sector)) return copied ? copied : -1;
        memcpy(sd_usb_sector + sector_offset, buffer + copied, chunk);
        if (!SD.writeRAW(sd_usb_sector, sector)) return copied ? copied : -1;
        copied += chunk;
    }
    return copied;
}

static bool usb_msc_start_stop(uint8_t power_condition, bool start,
                               bool load_eject) {
    (void)power_condition;
    if (sd_usb_msc) sd_usb_msc->mediaPresent(!load_eject || start);
    return true;
}

bool cardputer_sd_enter_usb_mode_if_requested(void) {
    const uint32_t detection_deadline = millis() + 300;
    bool opt_pressed = false;
    do {
        M5Cardputer.update();
        opt_pressed = M5Cardputer.Keyboard.keysState().opt;
        if (opt_pressed) break;
        delay(10);
    } while ((int32_t)(detection_deadline - millis()) > 0);

    if (!opt_pressed) return false;

    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.setTextSize(1);
    M5Cardputer.Display.setCursor(8, 42);
    M5Cardputer.Display.print("Hold Opt for USB disk mode");

    const uint32_t hold_started = millis();
    while (millis() - hold_started < 3000) {
        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.keysState().opt) return false;
        int width = (millis() - hold_started) * 224 / 3000;
        M5Cardputer.Display.fillRect(8, 66, width, 6, TFT_GREEN);
        delay(10);
    }

    if (!sd_card_responds() ||
        !SD.begin(SD_CS, sd_spi, 25000000) || SD.cardType() == CARD_NONE) {
        M5Cardputer.Display.setCursor(8, 84);
        M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5Cardputer.Display.print("SD card not found");
        delay(1500);
        return false;
    }

    uint32_t sector_count = SD.cardSize() / 512;
    sd_usb_msc = new USBMSC();
    if (!sd_usb_msc) {
        M5Cardputer.Display.setCursor(8, 84);
        M5Cardputer.Display.setTextColor(TFT_RED, TFT_BLACK);
        M5Cardputer.Display.print("USB storage init failed");
        delay(1500);
        SD.end();
        return false;
    }
    sd_usb_msc->vendorID("M5Stack");
    sd_usb_msc->productID("CardPuter86 SD");
    sd_usb_msc->productRevision("1.0");
    sd_usb_msc->onStartStop(usb_msc_start_stop);
    sd_usb_msc->onRead(usb_msc_read);
    sd_usb_msc->onWrite(usb_msc_write);
    sd_usb_msc->mediaPresent(true);
    if (!sd_usb_msc->begin(sector_count, 512)) {
        delete sd_usb_msc;
        sd_usb_msc = nullptr;
        SD.end();
        return false;
    }
    USB.productName("CardPuter86 SD Card");
    USB.begin();

    M5Cardputer.Display.fillScreen(TFT_BLACK);
    M5Cardputer.Display.setCursor(8, 48);
    M5Cardputer.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5Cardputer.Display.print("SD USB disk active");
    M5Cardputer.Display.setCursor(8, 66);
    M5Cardputer.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5Cardputer.Display.print("Eject on computer, then reboot");

    while (true) delay(1000);
}

static void set_disk_geometry(uint32_t size) {
    gb_sd_disk.size = size;
    gb_sd_disk.heads = 2;
    gb_sd_disk.sectors = 18;
    gb_sd_disk.cylinders = 80;
    gb_sd_disk.drive = 1; // B:

    switch (size) {
        case 163840: gb_sd_disk.heads = 1; gb_sd_disk.sectors = 8; gb_sd_disk.cylinders = 40; break;
        case 184320: gb_sd_disk.heads = 1; gb_sd_disk.sectors = 9; gb_sd_disk.cylinders = 40; break;
        case 327680: gb_sd_disk.heads = 2; gb_sd_disk.sectors = 8; gb_sd_disk.cylinders = 40; break;
        case 368640: gb_sd_disk.heads = 2; gb_sd_disk.sectors = 9; gb_sd_disk.cylinders = 40; break;
        case 737280: gb_sd_disk.heads = 2; gb_sd_disk.sectors = 9; gb_sd_disk.cylinders = 80; break;
        case 1228800: gb_sd_disk.heads = 2; gb_sd_disk.sectors = 15; gb_sd_disk.cylinders = 80; break;
        case 1474560: gb_sd_disk.heads = 2; gb_sd_disk.sectors = 18; gb_sd_disk.cylinders = 80; break;
        case 2949120: gb_sd_disk.heads = 2; gb_sd_disk.sectors = 36; gb_sd_disk.cylinders = 80; break;
        default:
            if (size > 2949120) {
                gb_sd_disk.drive = 0x80; // C:
                gb_sd_disk.heads = 16;
                gb_sd_disk.sectors = 63;
                gb_sd_disk.cylinders = size / (512UL * 16UL * 63UL);
                if (gb_sd_disk.cylinders == 0) gb_sd_disk.cylinders = 1;
                if (gb_sd_disk.cylinders > 1024) gb_sd_disk.cylinders = 1024;
            }
            break;
    }
}

bool cardputer_sd_init(void) {
    sd_available = false;
    gb_sd_disk = {};
    gb_sd_disk_count = 0;
    sd_default_disk[0] = '\0';

    // Cardputer has no card-detect switch. Probe CMD0 with a strict timeout so
    // boot continues immediately when no SD card is inserted.
    if (!sd_card_responds()) {
#ifdef use_lib_log_serial
        Serial.println("SD card: not detected");
#endif
        return false;
    }

    if (!SD.begin(SD_CS, sd_spi, 25000000) || SD.cardType() == CARD_NONE) {
#ifdef use_lib_log_serial
        Serial.println("SD card: mount failed");
#endif
        SD.end();
        return false;
    }

    sd_available = true;
#ifdef use_lib_log_serial
    unsigned char cardType = SD.cardType();
    unsigned long long cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD card: detected, type=%d, size=%llu MB\n", cardType, cardSize);
#endif

    // Scan for disk images
    cardputer_sd_scan_disks();
    cardputer_sd_mount_default_disk();

    return true;
}

int cardputer_sd_scan_disks(void) {
    if (!sd_available) return 0;

    gb_sd_disk_count = 0;
    sd_default_disk[0] = '\0';
    File root = SD.open("/");
    if (!root) return 0;

    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;

        const char *name = entry.name();
        int len = strlen(name);

        // Check for .img or .dsk extension
        if (len > 4) {
            const char *ext = name + len - 4;
            if (strcasecmp(ext, ".img") == 0 || strcasecmp(ext, ".dsk") == 0) {
                if (gb_sd_disk_count == 0 ||
                    strcasecmp(name[0] == '/' ? name + 1 : name,
                               "cardputer86.img") == 0 ||
                    strcasecmp(name[0] == '/' ? name + 1 : name,
                               "cardputer86.dsk") == 0) {
                    store_disk_path(sd_default_disk, name);
                }
#ifdef use_lib_log_serial
                Serial.printf("SD disk[%d]: %s\n", gb_sd_disk_count, name);
#endif
                gb_sd_disk_count++;
            }
        }
        entry.close();
    }
    root.close();

#ifdef use_lib_log_serial
    Serial.printf("SD card: %d disk images found\n", gb_sd_disk_count);
#endif

    return gb_sd_disk_count;
}

bool cardputer_sd_read_sector(const char *filename, unsigned long lba, unsigned char *buffer) {
    if (!sd_available) return false;

    File f = SD.open(filename, FILE_READ);
    if (!f) return false;

    unsigned long offset = lba * 512;
    if (!f.seek(offset)) {
        f.close();
        return false;
    }

    size_t read = f.read(buffer, 512);
    f.close();

    return (read == 512);
}

bool cardputer_sd_write_sector(const char *filename, unsigned long lba, const unsigned char *buffer) {
    if (!sd_available) return false;

    File f = SD.open(filename, "r+");
    if (!f) return false;

    unsigned long offset = lba * 512;
    bool ok = f.seek(offset) && f.write(buffer, 512) == 512;
    f.flush();
    f.close();
    return ok;
}

bool cardputer_sd_mount_default_disk(void) {
    gb_sd_disk = {};
    if (!sd_available || gb_sd_disk_count == 0 || !sd_default_disk[0]) return false;

    File f = SD.open(sd_default_disk, FILE_READ);
    if (!f) return false;
    uint32_t size = f.size();
    f.close();
    if (size < 512 || (size % 512) != 0) return false;

    strncpy(gb_sd_disk.filename, sd_default_disk, MAX_SD_FILENAME_LEN - 1);
    gb_sd_disk.filename[MAX_SD_FILENAME_LEN - 1] = '\0';
    set_disk_geometry(size);
    gb_sd_disk.mounted = true;

#ifdef use_lib_log_serial
    Serial.printf("SD image mounted as %c: %s (%lu bytes, %u/%u/%u)\n",
                  gb_sd_disk.drive == 0x80 ? 'C' : 'B', gb_sd_disk.filename,
                  (unsigned long)gb_sd_disk.size, gb_sd_disk.cylinders,
                  gb_sd_disk.heads, gb_sd_disk.sectors);
#endif
    return true;
}

bool cardputer_sd_is_available(void) {
    return sd_available;
}

#endif // use_lib_sdcard
