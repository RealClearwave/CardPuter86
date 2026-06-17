#ifndef CARDPUTER_STORAGE_H
#define CARDPUTER_STORAGE_H

#include <Arduino.h>

#define CARDPUTER_IMAGE_PATH_LEN 96

enum CardputerStorageType : uint8_t {
    CARDPUTER_STORAGE_NONE = 0,
    CARDPUTER_STORAGE_FLASH,
    CARDPUTER_STORAGE_SD
};

struct CardputerDiskImage {
    bool mounted;
    CardputerStorageType storage;
    uint8_t drive;
    uint32_t size;
    uint16_t cylinders;
    uint8_t sectors;
    uint8_t heads;
    char path[CARDPUTER_IMAGE_PATH_LEN];
};

extern CardputerDiskImage gb_disk_image;

bool cardputer_storage_init_and_select(void);
bool cardputer_storage_show_settings_menu(bool allow_usb_disk);
void cardputer_storage_show_boot_status(void);
bool cardputer_storage_read_sector(unsigned long lba, unsigned char *buffer);
bool cardputer_storage_write_sector(unsigned long lba, const unsigned char *buffer);

#endif
