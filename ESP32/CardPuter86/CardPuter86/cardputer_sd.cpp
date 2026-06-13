#include "cardputer_sd.h"
#include "hardware.h"
#include "gbConfig.h"
#include <Arduino.h>

#ifdef use_lib_sdcard

static bool sd_available = false;
static SPIClass sd_spi;

char gb_sd_disk_files[MAX_SD_DISK_FILES][MAX_SD_FILENAME_LEN];
int gb_sd_disk_count = 0;

bool cardputer_sd_init(void) {
    // Configure SD SPI bus
    sd_spi.begin(SD_CLK, SD_MISO, SD_MOSI, SD_CS);

    // Try to mount SD card
    if (!SD.begin(SD_CS, sd_spi, 4000000)) {
#ifdef use_lib_log_serial
        Serial.println("SD card: not detected");
#endif
        sd_available = false;
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

    return true;
}

int cardputer_sd_scan_disks(void) {
    if (!sd_available) return 0;

    gb_sd_disk_count = 0;
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
                if (gb_sd_disk_count < MAX_SD_DISK_FILES) {
                    strncpy(gb_sd_disk_files[gb_sd_disk_count], name, MAX_SD_FILENAME_LEN - 1);
                    gb_sd_disk_files[gb_sd_disk_count][MAX_SD_FILENAME_LEN - 1] = '\0';
#ifdef use_lib_log_serial
                    Serial.printf("SD disk[%d]: %s\n", gb_sd_disk_count, name);
#endif
                    gb_sd_disk_count++;
                }
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

bool cardputer_sd_is_available(void) {
    return sd_available;
}

#endif // use_lib_sdcard
