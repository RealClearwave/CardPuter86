#ifndef _CARDputer_SD_H
#define _CARDputer_SD_H

#include "gbConfig.h"
#include <SD.h>

#ifdef use_lib_sdcard

#define MAX_SD_DISK_FILES 32
#define MAX_SD_FILENAME_LEN 64

extern char gb_sd_disk_files[MAX_SD_DISK_FILES][MAX_SD_FILENAME_LEN];
extern int gb_sd_disk_count;

struct CardputerSdDisk {
    bool mounted;
    uint8_t drive;
    uint32_t size;
    uint16_t cylinders;
    uint8_t sectors;
    uint8_t heads;
    char filename[MAX_SD_FILENAME_LEN];
};

extern CardputerSdDisk gb_sd_disk;

bool cardputer_sd_init(void);
bool cardputer_sd_enter_usb_mode_if_requested(void);
int cardputer_sd_scan_disks(void);
bool cardputer_sd_read_sector(const char *filename, unsigned long lba, unsigned char *buffer);
bool cardputer_sd_write_sector(const char *filename, unsigned long lba, const unsigned char *buffer);
bool cardputer_sd_mount_default_disk(void);
bool cardputer_sd_is_available(void);

#endif // use_lib_sdcard

#endif
