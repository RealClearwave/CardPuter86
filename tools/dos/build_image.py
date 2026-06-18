#!/usr/bin/env python3
"""Build the default CardPuter86 FAT12 disk image from a source directory."""

from __future__ import annotations

import argparse
import os
import shutil
from dataclasses import dataclass
from pathlib import Path

from cp86test import emit_com

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SOURCE = ROOT / "tools" / "dos" / "image_root"
DEFAULT_OUTPUT = ROOT / "ESP32" / "CardPuter86" / "data" / "cardputer86.img"
DEFAULT_BOOTSECTOR = ROOT / "tools" / "dos" / "bootsector_1440.bin"
BYTES_PER_SECTOR = 512
SECTORS_PER_CLUSTER = 1
RESERVED_SECTORS = 1
FAT_COUNT = 2
ROOT_ENTRIES = 224
TOTAL_SECTORS = 2880
MEDIA_DESCRIPTOR = 0xF0
SECTORS_PER_FAT = 9
SECTORS_PER_TRACK = 18
HEADS = 2
ROOT_SECTORS = (ROOT_ENTRIES * 32 + BYTES_PER_SECTOR - 1) // BYTES_PER_SECTOR
ROOT_OFFSET = (RESERVED_SECTORS + FAT_COUNT * SECTORS_PER_FAT) * BYTES_PER_SECTOR
DATA_OFFSET = ROOT_OFFSET + ROOT_SECTORS * BYTES_PER_SECTOR
CLUSTER_SIZE = BYTES_PER_SECTOR * SECTORS_PER_CLUSTER


@dataclass(frozen=True)
class DosFile:
    source: Path
    path_parts: tuple[str, ...]
    size: int


def dos_83(name: str) -> bytes:
    if name in (".", "..") or not name:
        raise ValueError(f"invalid DOS name: {name!r}")
    stem, dot, ext = name.partition(".")
    if "." in ext or not stem or len(stem) > 8 or len(ext) > 3:
        raise ValueError(f"{name!r} is not an 8.3 DOS filename")
    valid = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&"
    upper_stem = stem.upper()
    upper_ext = ext.upper()
    if any(ch not in valid for ch in upper_stem + upper_ext):
        raise ValueError(f"{name!r} contains unsupported DOS characters")
    return upper_stem.encode("ascii").ljust(8) + upper_ext.encode("ascii").ljust(3)


def set_fat12_entry(fat: bytearray, cluster: int, value: int) -> None:
    offset = cluster + cluster // 2
    value &= 0xFFF
    if cluster & 1:
        fat[offset] = (fat[offset] & 0x0F) | ((value & 0x0F) << 4)
        fat[offset + 1] = (value >> 4) & 0xFF
    else:
        fat[offset] = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)


def collect_files(source: Path) -> list[DosFile]:
    files: list[DosFile] = []
    for root, dirs, names in os.walk(source):
        dirs[:] = sorted(d for d in dirs if not d.startswith("."))
        for filename in sorted(n for n in names if not n.startswith(".")):
            path = Path(root) / filename
            relative = path.relative_to(source)
            parts = tuple(relative.parts)
            for part in parts:
                dos_83(part)
            files.append(DosFile(path, parts, path.stat().st_size))
    return files


def write_dir_entry(directory: bytearray, index: int, name: bytes, attr: int,
                    first_cluster: int, size: int) -> None:
    offset = index * 32
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = attr
    entry[26:28] = first_cluster.to_bytes(2, "little")
    entry[28:32] = size.to_bytes(4, "little")
    directory[offset:offset + 32] = entry


def build_image(source: Path, output: Path, volume_label: str,
                bootsector: Path | None) -> None:
    files = collect_files(source)
    image = bytearray(TOTAL_SECTORS * BYTES_PER_SECTOR)

    if bootsector is not None:
        boot = bytearray(bootsector.read_bytes())
        if len(boot) != BYTES_PER_SECTOR:
            raise ValueError(f"boot sector must be 512 bytes: {bootsector}")
    else:
        boot = bytearray(BYTES_PER_SECTOR)
        boot[0:3] = b"\xEB\x3C\x90"
    boot[3:11] = b"CP86FAT "
    boot[11:13] = BYTES_PER_SECTOR.to_bytes(2, "little")
    boot[13] = SECTORS_PER_CLUSTER
    boot[14:16] = RESERVED_SECTORS.to_bytes(2, "little")
    boot[16] = FAT_COUNT
    boot[17:19] = ROOT_ENTRIES.to_bytes(2, "little")
    boot[19:21] = TOTAL_SECTORS.to_bytes(2, "little")
    boot[21] = MEDIA_DESCRIPTOR
    boot[22:24] = SECTORS_PER_FAT.to_bytes(2, "little")
    boot[24:26] = SECTORS_PER_TRACK.to_bytes(2, "little")
    boot[26:28] = HEADS.to_bytes(2, "little")
    boot[36] = 0x29
    boot[39:43] = b"\x86\x86\x04\x06"
    boot[43:54] = volume_label.upper().encode("ascii")[:11].ljust(11)
    boot[54:62] = b"FAT12   "
    boot[510:512] = b"\x55\xAA"
    image[0:BYTES_PER_SECTOR] = boot

    fat = bytearray(SECTORS_PER_FAT * BYTES_PER_SECTOR)
    fat[0:3] = bytes((MEDIA_DESCRIPTOR, 0xFF, 0xFF))
    root_dir = bytearray(ROOT_SECTORS * BYTES_PER_SECTOR)
    write_dir_entry(root_dir, 0, volume_label.upper().encode("ascii")[:11].ljust(11), 0x08, 0, 0)
    root_index = 1
    next_cluster = 2

    for dos_file in files:
        if len(dos_file.path_parts) != 1:
            raise ValueError("subdirectories are not implemented yet")
        needed_clusters = max(1, (dos_file.size + CLUSTER_SIZE - 1) // CLUSTER_SIZE)
        first_cluster = next_cluster
        payload = dos_file.source.read_bytes()
        for i in range(needed_clusters):
            cluster = next_cluster
            next_cluster += 1
            set_fat12_entry(fat, cluster, next_cluster if i + 1 < needed_clusters else 0xFFF)
            start = DATA_OFFSET + (cluster - 2) * CLUSTER_SIZE
            chunk = payload[i * CLUSTER_SIZE:(i + 1) * CLUSTER_SIZE]
            image[start:start + len(chunk)] = chunk
        if root_index >= ROOT_ENTRIES:
            raise RuntimeError("root directory is full")
        write_dir_entry(root_dir, root_index, dos_83(dos_file.path_parts[0]), 0x20,
                        first_cluster, dos_file.size)
        root_index += 1

    for fat_index in range(FAT_COUNT):
        start = (RESERVED_SECTORS + fat_index * SECTORS_PER_FAT) * BYTES_PER_SECTOR
        image[start:start + len(fat)] = fat
    image[ROOT_OFFSET:ROOT_OFFSET + len(root_dir)] = root_dir
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(image)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--volume-label", default="CARDPUTER86")
    parser.add_argument("--bootsector", type=Path, default=DEFAULT_BOOTSECTOR)
    parser.add_argument("--prepare-source", action="store_true",
                        help="generate CP86TEST.COM in the source directory before packing")
    args = parser.parse_args()

    if args.prepare_source:
        args.source.mkdir(parents=True, exist_ok=True)
        (args.source / "CP86TEST.COM").write_bytes(emit_com())
    build_image(args.source, args.output, args.volume_label, args.bootsector)
    print(f"Built {args.output} from {args.source}")


if __name__ == "__main__":
    main()
