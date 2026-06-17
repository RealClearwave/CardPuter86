#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$SCRIPT_DIR/ESP32/CardPuter86"
PIO_ENV="cardputer86"
BUILD_ONLY=false
WITH_IMAGES=false
PACKAGE=false
VERSION_FILE="$SCRIPT_DIR/VERSION"
RELEASE_VERSION=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-only) BUILD_ONLY=true ;;
        --with-images) WITH_IMAGES=true ;;
        --package) PACKAGE=true ;;
        --version)
            if [[ $# -lt 2 ]]; then
                echo "[ERROR] --version requires a value."
                exit 2
            fi
            RELEASE_VERSION="$2"
            shift
            ;;
        --help|-h)
            cat <<EOF
Usage: $0 [--build-only] [--with-images] [--package] [--version X.Y.Z]

  --build-only       Build firmware and FATFS without flashing.
  --with-images      Erase and flash firmware plus the default internal IMG.
  --package          Build an M5Burner release package without flashing.
  --version X.Y.Z    Override the version from VERSION for --package.
EOF
            exit 0
            ;;
        *)
            echo "Usage: $0 [--build-only] [--with-images] [--package] [--version X.Y.Z]"
            exit 2
            ;;
    esac
    shift
done

if [[ "$PACKAGE" == true && "$WITH_IMAGES" == true ]]; then
    echo "[ERROR] --package and --with-images cannot be used together."
    exit 2
fi

if [[ "$PACKAGE" == true && -z "$RELEASE_VERSION" ]]; then
    if [[ ! -f "$VERSION_FILE" ]]; then
        echo "[ERROR] Version file not found: $VERSION_FILE"
        exit 1
    fi
    RELEASE_VERSION="$(tr -d '[:space:]' < "$VERSION_FILE")"
fi

if [[ "$PACKAGE" == true && ! "$RELEASE_VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+([.-][0-9A-Za-z.-]+)?$ ]]; then
    echo "[ERROR] Invalid release version: $RELEASE_VERSION"
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
if [[ "$BUILD_ONLY" == true || "$WITH_IMAGES" == true || "$PACKAGE" == true ]]; then
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

ESPTOOL="$(find "$HOME/.platformio/packages/tool-esptoolpy" -maxdepth 1 -type f -name 'esptool.py' | head -n 1 || true)"
FATFS_IMAGE="$PROJECT_DIR/.pio/build/$PIO_ENV/fatfs.bin"
FATFS_OFFSET="0x2A1000"

if [[ "$PACKAGE" == true ]]; then
    BUILD_DIR="$PROJECT_DIR/.pio/build/$PIO_ENV"
    BOOTLOADER="$BUILD_DIR/bootloader.bin"
    PARTITIONS="$BUILD_DIR/partitions.bin"
    RELEASE_ROOT="$SCRIPT_DIR/release/M5Burner"
    RELEASE_NAME="CardPuter86-$RELEASE_VERSION"
    PACKAGE_DIR="$RELEASE_ROOT/$RELEASE_NAME"
    MERGED_IMAGE="$PACKAGE_DIR/CardPuter86_${RELEASE_VERSION}_full_8MB.bin"
    ZIP_FILE="$RELEASE_ROOT/$RELEASE_NAME.zip"

    for required in "$ESPTOOL" "$BOOTLOADER" "$PARTITIONS" "$FIRMWARE" "$FATFS_IMAGE"; do
        if [[ -z "$required" || ! -f "$required" ]]; then
            echo "[ERROR] Required release artifact is missing: $required"
            exit 1
        fi
    done

    echo "[PACKAGE] Preparing M5Burner release $RELEASE_VERSION..."
    rm -rf "$PACKAGE_DIR"
    mkdir -p "$PACKAGE_DIR"
    cp "$BOOTLOADER" "$PACKAGE_DIR/bootloader_0x0000.bin"
    cp "$PARTITIONS" "$PACKAGE_DIR/partitions_0x8000.bin"
    cp "$FIRMWARE" "$PACKAGE_DIR/CardPuter86_0x10000.bin"
    cp "$FATFS_IMAGE" "$PACKAGE_DIR/fatfs_0x2A1000.bin"
    cp "$SCRIPT_DIR/preview/cardputer86-cover.svg" "$PACKAGE_DIR/cover.svg"
    cp "$SCRIPT_DIR/preview/cardputer86-cover.png" "$PACKAGE_DIR/cover.png"

    python3 "$ESPTOOL" --chip esp32s3 merge_bin \
        --flash_mode keep --flash_freq keep --flash_size 8MB \
        -o "$MERGED_IMAGE" \
        0x0 "$BOOTLOADER" \
        0x8000 "$PARTITIONS" \
        0x10000 "$FIRMWARE" \
        "$FATFS_OFFSET" "$FATFS_IMAGE"

    cat > "$PACKAGE_DIR/m5burner.json" <<EOF
{
  "name": "CardPuter86",
  "description": "8086 PC emulator for M5Stack Cardputer",
  "keywords": "Cardputer,8086,DOS,emulator",
  "author": "RealClearwave",
  "repository": "https://github.com/RealClearwave/CardPuter86",
  "cover": "cover.png",
  "firmware_category": [
    {
      "Cardputer-8MB": {
        "path": ".",
        "device": ["M5Stack Cardputer"],
        "default_baud": 921600
      }
    }
  ],
  "version": "$RELEASE_VERSION",
  "framework": "PlatformIO / Arduino"
}
EOF

    cat > "$PACKAGE_DIR/FLASH_LAYOUT.txt" <<EOF
CardPuter86 $RELEASE_VERSION - ESP32-S3 8 MB QIO

0x000000  bootloader_0x0000.bin
0x008000  partitions_0x8000.bin
0x010000  CardPuter86_0x10000.bin
0x2A1000  fatfs_0x2A1000.bin

For M5Burner v3 User Custom, import CardPuter86_${RELEASE_VERSION}_full_8MB.bin
and flash it at address 0x000000 with the entire flash erased.
EOF

    (
        cd "$PACKAGE_DIR"
        shasum -a 256 ./*.bin > SHA256SUMS
    )
    rm -f "$ZIP_FILE"
    (
        cd "$RELEASE_ROOT"
        zip -qr "$(basename "$ZIP_FILE")" "$RELEASE_NAME"
    )

    echo "[PACKAGE] Merged image: $MERGED_IMAGE"
    echo "[PACKAGE] Repository bundle: $ZIP_FILE"
    echo "[DONE] Release package completed. Nothing was flashed."
    exit 0
fi

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
