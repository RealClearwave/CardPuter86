# CardPuter86

CardPuter86 is a compact 8086 PC emulator for the M5Stack Cardputer. It adapts the Fake86 emulator and ESP32TinyFake86 codebase to the Cardputer's ESP32-S3, integrated keyboard, 240x135 display, speaker and microSD interface.

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

## Controls

The regular Cardputer keys map to the corresponding PC keys. The Aa key maps to Shift, while Ctrl and Alt act as PC modifiers. Fn is the CardPuter86 function layer: Fn+1 through Fn+0 send F1-F10, Fn+- sends F11, and Fn+= sends F12. Opt switches between the default original-font text mode and scaled mode. Text mode keeps the original 4x8 framebuffer size with wrapping and bottom-follow; scaled mode uses a 3x5 font for text screens and scales graphics screens to the full LCD. G0 is reserved.

The scaled text view uses [Tom Thumb](https://opengameart.org/content/tom-thumb-tiny-ascii-font-3x5) by Robey Pointer, released under CC0.

## Disk Images

Writable `.img` files live in an independent FAT partition in internal Flash or in the microSD root. If multiple images exist, a startup menu selects the boot image; `cardputer86.img` is the timed default. Legacy `.dsk` files are also accepted.

After the optional SD check, hold `Ctrl` to enter USB storage mode. If SD was enabled with `Alt` and detected, choose internal Flash or SD; otherwise internal Flash is exported automatically. Copy IMG files, safely eject the drive, and reboot.

Press `Ctrl` after the SD check to open POST Settings. USB disk mode applies only to the current boot. The 512 KB memory option is stored in NVS across power cycles; when disabled, the emulated PC uses 128 KB. In 512 KB mode, active 4 KB pages use a 128 KB SRAM cache and cold dirty pages use a dedicated wear-levelled Flash partition.

## Embedded software

ROM and COM data compiled into the firmware is stored under `ESP32/CardPuter86/CardPuter86/dataFlash`. The default independent image is `ESP32/CardPuter86/data/cardputer86.img`.

## Upstream

CardPuter86 preserves the Fake86 emulator core and credits Mike Chambers for Fake86 and Ackerman for ESP32TinyFake86.
