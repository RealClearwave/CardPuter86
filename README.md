# CardPuter86

CardPuter86 is an 8086 PC emulator for the M5Stack Cardputer (ESP32-S3), based on the Fake86 emulator and the ESP32TinyFake86 port.

| POST | BIOS |
| --- | --- |
| ![CardPuter86 POST](preview/cardputer86-post.png) | ![CardPuter86 BIOS](preview/cardputer86-bios.png) |
| DOS | BASIC |
| ![CardPuter86 DOS](preview/cardputer86-dos.png) | ![CardPuter86 BASIC](preview/cardputer86-basic.png) |

## Features

- M5Stack Cardputer keyboard support
- ST7789 LCD output with a DSx86-style 40x16 text viewport and full-screen scaled modes
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

## M5Burner Release

Build a complete release package without flashing hardware:

```sh
./flash.sh --package
```

The version comes from [`VERSION`](VERSION) and can be overridden with `--version X.Y.Z`. Output is written under `release/M5Burner/` and includes a complete 8 MB merged image, offset-named component images, `m5burner.json`, the flash layout, SHA-256 checksums, and a ZIP bundle.

The package includes the default internal `cardputer86.img`, so installing the merged image replaces the complete device Flash.

## Keyboard

- `Fn` + `1` through `0`: F1 through F10
- `Fn` + `-`: F11
- `Fn` + `=`: F12
- `Aa`: Shift
- `Ctrl`, `Alt`: corresponding PC modifier keys
- `Fn` + `;`, `.`, `,`, `/`: scroll the 40x16 text viewport up, down, left, and right
- `Fn` + `'`: return a FIXED viewport to its top-left starting position
- `Fn` + `Space`: return the text viewport to automatic follow mode
- `Opt`: switch between the default DSx86-style text mode and scaled mode. Text mode uses a readable 6x8 cell and supports viewport scrolling; scaled mode uses a 3x5 font for text screens and scales graphics screens to the full LCD.
- `G0`: reserved

The default text view uses the BSD-licensed [Adafruit Classic 5x7 glyphs](https://github.com/adafruit/Adafruit-GFX-Library/blob/master/glcdfont.c) in 6x8 cells. The scaled text view uses [Tom Thumb](https://opengameart.org/content/tom-thumb-tiny-ascii-font-3x5) by Robey Pointer, released under CC0.

Text mode starts in AUTO mode and follows the last content line while keeping up to two detected bottom status rows pinned. Manual scrolling switches to FIXED mode; `Fn+Space` restores AUTO.

Fn combinations replace their base keys, so `Fn+1` sends only F1 rather than both `1` and F1.

## Disk Images

Disk images are regular writable `.img` files stored independently from the firmware. The built-in Flash image partition initially contains `cardputer86.img`. Images in the microSD root are also detected; legacy `.dsk` files remain compatible.

When more than one image is available, startup shows a boot menu. Use `W`/`S` (or the printed arrow keys) and `Enter`; keys `1` through `9` select directly. Without input, `cardputer86.img` starts after four seconds. Floppy-sized images boot as `A:`, while images larger than 2.88 MB boot as hard drive `C:`.

To import images from a computer, wait for the optional SD check and hold `Ctrl` when it finishes. If SD was enabled with `Alt` and detected, select either `Internal Flash` or `SD Card`; otherwise the internal Flash is exported automatically as a USB drive. Copy `.img` files to its root, safely eject it, and reboot.

Press `Ctrl` after the SD check to open POST Settings. USB disk mode applies only to the current boot. The 512 KB memory option is stored in NVS and remains selected after power-off; when disabled, the emulated PC uses 128 KB. In 512 KB mode, a 128 KB SRAM page cache keeps active 4 KB pages in memory while cold dirty pages are written through ESP-IDF wear levelling to a dedicated Flash partition.

`ESP32/CardPuter86/data/cardputer86.img` is used only by `--with-images`. This option resets the internal image partition, so routine firmware updates intentionally do not run `uploadfs`.

## Documentation

- [简体中文](README.zh-CN.md)
- [English](README.en.md)
- [Español](README.es.md)

## Credits

CardPuter86 uses the Fake86 emulator by Mike Chambers and is derived from the ESP32TinyFake86 project by Ackerman. Original project material remains under its respective license and attribution.
