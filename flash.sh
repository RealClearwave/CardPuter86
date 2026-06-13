#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/ESP32/CardPuter86"
PIO_ENV="cardputer86"
BUILD_ONLY=false

if [[ "${1:-}" == "--build-only" ]]; then
    BUILD_ONLY=true
elif [[ $# -gt 0 ]]; then
    echo "Usage: $0 [--build-only]"
    exit 2
fi

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
read -r -p "[WAIT] Press ENTER to flash $PORT, or Ctrl-C to cancel ..." _
echo ""

# 4. Flash
echo "[FLASH] Uploading..."
"$PIO_BIN" run --project-dir "$PROJECT_DIR" --environment "$PIO_ENV" \
    --target upload --upload-port "$PORT"
echo ""
echo "[DONE]"
