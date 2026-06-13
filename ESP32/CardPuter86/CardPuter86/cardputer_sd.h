#ifndef _CARDputer_SD_H
#define _CARDputer_SD_H

#include "gbConfig.h"
#include <SD.h>

#ifdef use_lib_sdcard

#define MAX_SD_DISK_FILES 32
#define MAX_SD_FILENAME_LEN 64

extern char gb_sd_disk_files[MAX_SD_DISK_FILES][MAX_SD_FILENAME_LEN];
extern int gb_sd_disk_count;

bool cardputer_sd_init(void);
int cardputer_sd_scan_disks(void);
bool cardputer_sd_read_sector(const char *filename, unsigned long lba, unsigned char *buffer);
bool cardputer_sd_is_available(void);

#endif // use_lib_sdcard

#endif
