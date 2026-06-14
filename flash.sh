#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/ESP32/CardPuter86"
PIO_ENV="cardputer86"
BUILD_ONLY=false
WITH_IMAGES=false

for arg in "$@"; do
    case "$arg" in
        --build-only) BUILD_ONLY=true ;;
        --with-images) WITH_IMAGES=true ;;
        *)
            echo "Usage: $0 [--build-only] [--with-images]"
            exit 2
            ;;
    esac
done

if command -v pio >/dev/null 2>&1; then
    PIO_BIN="$(command -v pio)"
elif command -v platformio >/dev/null 2>&1; then
    PIO_BIN="$(command -v platformio)"
else
    echo "[ERROR] PlatformIO Core was not found. Install it with: pipx install platformio"
    exit 1
fi

if [[ ! -f "$PROJECT_DIR/platformio.ini" ]]; then
    echo "[ERROR] PlatformIO project not found: $PROJECT_DIR"
    exit 1
fi
echo "======================================="
echo " CardPuter86 Flash Tool"
echo "======================================="
echo ""

# 1. Build
echo "[BUILD] Compiling..."
"$PIO_BIN" run --project-dir "$PROJECT_DIR" --environment "$PIO_ENV"
if [[ "$BUILD_ONLY" == true || "$WITH_IMAGES" == true ]]; then
    echo "[BUILD] Creating internal disk filesystem..."
    "$PIO_BIN" run --project-dir "$PROJECT_DIR" --environment "$PIO_ENV" --target buildfs
fi
echo ""

FIRMWARE="$PROJECT_DIR/.pio/build/$PIO_ENV/firmware.bin"
if [[ ! -f "$FIRMWARE" ]]; then
    echo "[ERROR] Firmware was not generated: $FIRMWARE"
    exit 1
fi
echo "[BUILD] Firmware: $FIRMWARE"
echo ""

if [[ "$BUILD_ONLY" == true ]]; then
    echo "[DONE] Build completed. Nothing was flashed."
    exit 0
fi

# 2. Find port
PORT="$(find /dev -maxdepth 1 -type c \( -name 'cu.usbmodem*' -o -name 'cu.usbserial*' \) 2>/dev/null | sort | head -n 1 || true)"
if [[ -z "$PORT" ]]; then
    echo "[ERROR] Cardputer not found."
    exit 1
fi
echo "[PORT] $PORT"
echo ""

ESPTOOL="$(find "$HOME/.platformio/packages/tool-esptoolpy" -maxdepth 1 -type f -name 'esptool.py' | head -n 1 || true)"
FATFS_IMAGE="$PROJECT_DIR/.pio/build/$PIO_ENV/fatfs.bin"
FATFS_OFFSET="0x2A1000"

# 3. Confirm
PARTITIONS="$PROJECT_DIR/.pio/build/$PIO_ENV/partitions.bin"
if [[ "$WITH_IMAGES" == false && -f "$PARTITIONS" ]]; then
    CURRENT_PARTITIONS="$(mktemp /tmp/cardputer86-partitions.XXXXXX)"
    if [[ -n "$ESPTOOL" ]] && python3 "$ESPTOOL" --chip esp32s3 --port "$PORT" \
        read_flash 0x8000 0xC00 "$CURRENT_PARTITIONS" >/dev/null 2>&1; then
        if ! cmp -s "$CURRENT_PARTITIONS" "$PARTITIONS"; then
            WITH_IMAGES=true
            echo "[MIGRATE] Old partition layout detected; the default IMG partition will be installed."
            echo "[BUILD] Creating internal disk filesystem for migration..."
            "$PIO_BIN" run --project-dir "$PROJECT_DIR" --environment "$PIO_ENV" --target buildfs
        fi
    fi
    rm -f "$CURRENT_PARTITIONS"
    sleep 1
fi
if [[ "$WITH_IMAGES" == true ]]; then
    echo "[WARNING] --with-images erases the device before reinstalling firmware and the default IMG partition."
fi
read -r -p "[WAIT] Press ENTER to flash $PORT, or Ctrl-C to cancel ..." _
echo ""

# 4. Flash
if [[ "$WITH_IMAGES" == true ]]; then
    echo "[FLASH] Erasing old Flash and wear-leveling metadata..."
    "$PIO_BIN" run --project-dir "$PROJECT_DIR" --environment "$PIO_ENV" \
        --target erase --upload-port "$PORT"
fi
echo "[FLASH] Uploading..."
"$PIO_BIN" run --project-dir "$PROJECT_DIR" --environment "$PIO_ENV" \
    --target upload --upload-port "$PORT"
if [[ "$WITH_IMAGES" == true ]]; then
    echo "[FLASH] Uploading internal disk filesystem..."
    if [[ -z "$ESPTOOL" || ! -f "$FATFS_IMAGE" ]]; then
        echo "[ERROR] esptool or fatfs.bin is missing."
        exit 1
    fi
    echo "[WAIT] Waiting for Cardputer USB to re-enumerate..."
    sleep 3
    PORT="$(find /dev -maxdepth 1 -type c \( -name 'cu.usbmodem*' -o -name 'cu.usbserial*' \) 2>/dev/null | sort | head -n 1 || true)"
    if [[ -z "$PORT" ]]; then
        echo "[ERROR] Cardputer did not reappear after firmware upload."
        exit 1
    fi
    echo "[FLASH] Raw FAT image: $FATFS_IMAGE"
    echo "[FLASH] Raw FAT offset: $FATFS_OFFSET (no transport compression)"
    python3 "$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 460800 \
        --before default_reset --after hard_reset write_flash --no-compress \
        "$FATFS_OFFSET" "$FATFS_IMAGE"
else
    echo "[KEEP] Internal IMG partition was not changed."
fi
echo ""
echo "[DONE]"
