# CardPuter86

CardPuter86 is a compact 8086 PC emulator for the M5Stack Cardputer. It adapts the Fake86 emulator and ESP32TinyFake86 codebase to the Cardputer's ESP32-S3, integrated keyboard, 240x135 display, speaker and microSD interface.

| POST | BIOS |
| --- | --- |
| ![CardPuter86 POST](preview/cardputer86-post.png) | ![CardPuter86 BIOS](preview/cardputer86-bios.png) |
| DOS | BASIC |
| ![CardPuter86 DOS](preview/cardputer86-dos.png) | ![CardPuter86 BASIC](preview/cardputer86-basic.png) |

## Building with PlatformIO

Open `ESP32/CardPuter86` as the PlatformIO project or build from a terminal:

```sh
cd ESP32/CardPuter86
pio run
```

For a first install, upload the firmware and initialize the internal IMG partition:

```sh
./flash.sh --with-images
```

`--with-images` erases the device and reinstalls the default image partition. Later `./flash.sh` runs update only the firmware and preserve images imported by the user.

## M5Burner Release

Run `./flash.sh --package` to create a complete 8 MB merged image for M5Burner v3 User Custom import and a ZIP bundle containing offset-named images, `m5burner.json`, the flash layout, and SHA-256 checksums. The version is read from `VERSION` or overridden with `--version X.Y.Z`. Packaging never flashes the device and includes the default internal IMG.

## Controls

The regular Cardputer keys map to the corresponding PC keys. The Aa key maps to Shift, while Ctrl and Alt act as PC modifiers. Fn is the CardPuter86 function layer: Fn+1 through Fn+0 send F1-F10, Fn+- sends F11, and Fn+= sends F12. In the default DSx86-style text mode, Fn+;, Fn+., Fn+,, and Fn+/ scroll the 40x16 viewport up, down, left, and right. Manual scrolling selects FIXED mode; Fn+' returns a FIXED viewport to its top-left starting position, while Fn+Space restores AUTO mode. AUTO follows the last content line while keeping up to two detected bottom status rows pinned. Opt switches between this readable 6x8-cell text mode and scaled mode. Scaled mode uses a 3x5 font for text screens and scales graphics screens to the full LCD. G0 is reserved.

The default text view uses the BSD-licensed [Adafruit Classic 5x7 glyphs](https://github.com/adafruit/Adafruit-GFX-Library/blob/master/glcdfont.c) in 6x8 cells. The scaled text view uses [Tom Thumb](https://opengameart.org/content/tom-thumb-tiny-ascii-font-3x5) by Robey Pointer, released under CC0.

## Disk Images

Writable `.img` files live in an independent FAT partition in internal Flash or in the microSD root. If multiple images exist, a startup menu selects the boot image; `cardputer86.img` is the timed default. Legacy `.dsk` files are also accepted.

After the optional SD check, hold `Ctrl` to enter USB storage mode. If SD was enabled with `Alt` and detected, choose internal Flash or SD; otherwise internal Flash is exported automatically. Copy IMG files, safely eject the drive, and reboot.

Press `Ctrl` after the SD check to open POST Settings. USB disk mode applies only to the current boot. The 512 KB memory option is stored in NVS across power cycles; when disabled, the emulated PC uses 128 KB. In 512 KB mode, active 4 KB pages use a 128 KB SRAM cache and cold dirty pages use a dedicated wear-levelled Flash partition.

## Embedded software

ROM and COM data compiled into the firmware is stored under `ESP32/CardPuter86/CardPuter86/dataFlash`. The default independent image is `ESP32/CardPuter86/data/cardputer86.img`.

## Upstream

CardPuter86 preserves the Fake86 emulator core and credits Mike Chambers for Fake86 and Ackerman for ESP32TinyFake86.
