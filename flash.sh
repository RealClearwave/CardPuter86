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

# 3. Confirm
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
    "$PIO_BIN" run --project-dir "$PROJECT_DIR" --environment "$PIO_ENV" \
        --target uploadfs --upload-port "$PORT"
else
    echo "[KEEP] Internal IMG partition was not changed."
fi
echo ""
echo "[DONE]"
