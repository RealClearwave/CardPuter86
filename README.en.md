# CardPuter86

CardPuter86 is a compact 8086 PC emulator for the M5Stack Cardputer. It adapts the Fake86 emulator and ESP32TinyFake86 codebase to the Cardputer's ESP32-S3, integrated keyboard, 240x135 display, speaker and microSD interface.

## Building with PlatformIO

Open `ESP32/CardPuter86` as the PlatformIO project or build from a terminal:

```sh
cd ESP32/CardPuter86
pio run
```

Upload with PlatformIO:

```sh
pio run --target upload
```

The repository-level `flash.sh` script can also build, detect the USB serial port and upload the firmware.

## Controls

The regular Cardputer keys map to the corresponding PC keys. The Aa key maps to Shift, while Ctrl and Alt act as PC modifiers. Fn is the CardPuter86 function layer: Fn+1 through Fn+0 send F1-F10, Fn+- sends F11, and Fn+= sends F12. Opt switches between the default 1:1 text display and full-screen graphics scaling. Text overflow wraps without shrinking, and tall output follows the bottom of the content. G0 is reserved.

## Embedded software

ROM, disk and COM data compiled into the firmware is stored under `ESP32/CardPuter86/CardPuter86/dataFlash`. The `tools/ima2h` utility originates from ESP32TinyFake86 and can convert supported images and binaries into C headers.

## Upstream

CardPuter86 preserves the Fake86 emulator core and credits Mike Chambers for Fake86 and Ackerman for ESP32TinyFake86.
