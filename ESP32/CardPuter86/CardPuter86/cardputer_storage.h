#ifndef CARDPUTER_STORAGE_H
#define CARDPUTER_STORAGE_H

#include <Arduino.h>

#define CARDPUTER_IMAGE_PATH_LEN 96
#define CARDPUTER_DRIVE_COUNT 4

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
extern CardputerDiskImage gb_disk_drives[CARDPUTER_DRIVE_COUNT];

bool cardputer_storage_init_and_select(void);
bool cardputer_storage_show_settings_menu(bool allow_usb_disk);
void cardputer_storage_show_boot_status(void);
bool cardputer_storage_read_sector(unsigned long lba, unsigned char *buffer);
bool cardputer_storage_write_sector(unsigned long lba, const unsigned char *buffer);
bool cardputer_storage_read_sector(uint8_t drive, unsigned long lba, unsigned char *buffer);
bool cardputer_storage_write_sector(uint8_t drive, unsigned long lba, const unsigned char *buffer);
bool cardputer_storage_drive_geometry(uint8_t drive, uint16_t *cylinders,
                                      uint8_t *heads, uint8_t *sectors);
uint8_t cardputer_storage_hard_drive_count(void);
bool cardputer_storage_show_mount_menu(void);

#endif
