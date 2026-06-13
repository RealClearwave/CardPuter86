#!/bin/bash
set -e
PROJECT_DIR="$(cd "$(dirname "$0")/ESP32/Tinyfake86ttgovga32" && pwd)"
echo "======================================="
echo " Cardputer Flash Tool"
echo "======================================="
echo ""

# 1. Build
echo "[BUILD] Compiling..."
python3 -m platformio run --project-dir "$PROJECT_DIR" 2>&1 | tail -5
echo ""

# 2. Find port
PORT=$(ls /dev/cu.* 2>/dev/null | grep -iE "usbmodem|usbserial" | head -1)
if [ -z "$PORT" ]; then
    echo "[ERROR] Cardputer not found."
    exit 1
fi
echo "[PORT] $PORT"
echo ""

# 3. Confirm
read -p "[WAIT] Press ENTER to flash $PORT ..." _
echo ""

# 4. Flash
echo "[FLASH] Uploading..."
python3 -m platformio run --project-dir "$PROJECT_DIR" --target upload --upload-port "$PORT" 2>&1 | tail -8
echo ""
echo "[DONE]"
