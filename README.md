# CardPuter86

CardPuter86 is an 8086 PC emulator for the M5Stack Cardputer (ESP32-S3), based on the Fake86 emulator and the ESP32TinyFake86 port.

## Features

- M5Stack Cardputer keyboard support
- ST7789 LCD output with CGA text and graphics modes
- PC speaker output through the Cardputer I2S speaker
- Built-in BIOS, BASIC ROM, disk image and COM program support
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

The script builds the project, detects the serial device and asks for confirmation before uploading.

## Keyboard

- `Fn` + `1` through `0`: F1 through F10
- `Fn` + `-`: F11
- `Fn` + `=`: F12
- `Aa`: Shift
- `Ctrl`, `Alt`: corresponding PC modifier keys
- `Opt`: switch between the default 1:1 text display and full-screen graphics scaling. Text overflow wraps to the next line, and tall output follows the newest content at the bottom.
- `G0`: reserved

Fn combinations replace their base keys, so `Fn+1` sends only F1 rather than both `1` and F1.

## SD Card

Place a raw DOS disk image in the SD card root. `cardputer86.img` or `cardputer86.dsk` is preferred; otherwise the first `.img`/`.dsk` file is used. Floppy-sized images are mounted as `B:`, while images larger than 2.88 MB are mounted as hard drive `C:`. The image is writable.

To access the whole SD card from a connected computer, hold `Opt` continuously for at least three seconds while powering on or resetting the Cardputer. CardPuter86 then starts in USB mass-storage mode instead of launching the emulator. Eject the disk on the computer before rebooting the Cardputer.

## Documentation

- [English](README.en.md)
- [Español](README.es.md)

## Credits

CardPuter86 uses the Fake86 emulator by Mike Chambers and is derived from the ESP32TinyFake86 project by Ackerman. Original project material remains under its respective license and attribution.
