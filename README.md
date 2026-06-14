# CardPuter86

CardPuter86 is an 8086 PC emulator for the M5Stack Cardputer (ESP32-S3), based on the Fake86 emulator and the ESP32TinyFake86 port.

## Features

- M5Stack Cardputer keyboard support
- ST7789 LCD output with CGA text and graphics modes
- PC speaker output through the Cardputer I2S speaker
- Built-in BIOS, BASIC ROM and COM program support
- Independent writable IMG storage on internal Flash and microSD
- PlatformIO build and flash workflow

## Build

Requirements:

- M5Stack Cardputer
- Python 3
- PlatformIO Core

```sh
cd ESP32/CardPuter86
pio run
```

The generated firmware is written to:

```text
ESP32/CardPuter86/.pio/build/cardputer86/firmware.bin
```

## Flash

Connect the Cardputer over USB and run:

```sh
./flash.sh
```

For the first installation, initialize the internal IMG partition:

```sh
./flash.sh --with-images
```

`--with-images` erases the device and reinstalls the default image partition. Normal `./flash.sh` updates only the firmware and preserves imported images.

## Keyboard

- `Fn` + `1` through `0`: F1 through F10
- `Fn` + `-`: F11
- `Fn` + `=`: F12
- `Aa`: Shift
- `Ctrl`, `Alt`: corresponding PC modifier keys
- `Opt`: switch between the default 1:1 text display and full-screen graphics scaling. Text overflow wraps to the next line, and tall output follows the newest content at the bottom.
- `G0`: reserved

Fn combinations replace their base keys, so `Fn+1` sends only F1 rather than both `1` and F1.

## Disk Images

Disk images are regular writable `.img` files stored independently from the firmware. The built-in Flash image partition initially contains `cardputer86.img`. Images in the microSD root are also detected; legacy `.dsk` files remain compatible.

When more than one image is available, startup shows a boot menu. Use `W`/`S` (or the printed arrow keys) and `Enter`; keys `1` through `9` select directly. Without input, `cardputer86.img` starts after four seconds. Floppy-sized images boot as `A:`, while images larger than 2.88 MB boot as hard drive `C:`.

To import images from a computer, wait for the optional SD check and hold `Ctrl` when it finishes. If SD was enabled with `Alt` and detected, select either `Internal Flash` or `SD Card`; otherwise the internal Flash is exported automatically as a USB drive. Copy `.img` files to its root, safely eject it, and reboot.

CardPuter86 presents 512 KB of conventional RAM to the emulated PC. A 128 KB SRAM page cache keeps active 4 KB pages in memory, while cold dirty pages are written through ESP-IDF wear levelling to a dedicated Flash partition. The swap partition is separate from the IMG filesystem.

`ESP32/CardPuter86/data/cardputer86.img` is used only by `--with-images`. This option resets the internal image partition, so routine firmware updates intentionally do not run `uploadfs`.

## Documentation

- [English](README.en.md)
- [Español](README.es.md)

## Credits

CardPuter86 uses the Fake86 emulator by Mike Chambers and is derived from the ESP32TinyFake86 project by Ackerman. Original project material remains under its respective license and attribution.
