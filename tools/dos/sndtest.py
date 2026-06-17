#!/usr/bin/env python3
"""Build and install the CardPuter86 DOS PC speaker test program."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_IMG = ROOT / "ESP32" / "CardPuter86" / "data" / "cardputer86.img"


def emit_com() -> bytes:
    code = bytearray()
    labels: dict[str, int] = {}
    patches: list[tuple[int, str, str]] = []

    def label(name: str) -> None:
        labels[name] = len(code)

    def b(*values: int) -> None:
        code.extend(values)

    def w(value: int) -> None:
        code.extend((value & 0xFF, (value >> 8) & 0xFF))

    def call(target: str) -> None:
        b(0xE8)
        patches.append((len(code), target, "rel16"))
        w(0)

    def jmp(target: str) -> None:
        b(0xE9)
        patches.append((len(code), target, "rel16"))
        w(0)

    def jz(target: str) -> None:
        b(0x74)
        patches.append((len(code), target, "rel8"))
        b(0)

    def jb(target: str) -> None:
        b(0x72)
        patches.append((len(code), target, "rel8"))
        b(0)

    # COM entry point.
    b(0xBA)
    patches.append((len(code), "msg", "off16"))
    w(0)
    b(0xB4, 0x09, 0xCD, 0x21)  # mov ah,09h; int 21h
    b(0xBE)
    patches.append((len(code), "notes", "off16"))
    w(0)

    label("next_note")
    b(0xAD)              # lodsw
    b(0x09, 0xC0)        # or ax,ax
    jz("done")
    call("speaker_on")
    b(0xB9)
    w(4)                 # about 220 ms on the BIOS tick
    call("delay_ticks")
    call("speaker_off")
    b(0xB9)
    w(1)
    call("delay_ticks")
    jmp("next_note")

    label("done")
    call("speaker_off")
    b(0xBA)
    patches.append((len(code), "done_msg", "off16"))
    w(0)
    b(0xB4, 0x09, 0xCD, 0x21)
    b(0xB8)
    w(0x4C00)
    b(0xCD, 0x21)

    label("speaker_on")
    b(0x89, 0xC3)        # mov bx,ax
    b(0xBA)
    w(0x0012)
    b(0xB8)
    w(0x34DC)            # 1193180 decimal as DX:AX
    b(0xF7, 0xF3)        # div bx
    b(0x89, 0xC3)        # mov bx,ax
    b(0xB0, 0xB6)        # channel 2, lobyte/hibyte, mode 3
    b(0xE6, 0x43)
    b(0x88, 0xD8)        # mov al,bl
    b(0xE6, 0x42)
    b(0x88, 0xF8)        # mov al,bh
    b(0xE6, 0x42)
    b(0xE4, 0x61)
    b(0x0C, 0x03)
    b(0xE6, 0x61)
    b(0xC3)

    label("speaker_off")
    b(0xE4, 0x61)
    b(0x24, 0xFC)
    b(0xE6, 0x61)
    b(0xC3)

    label("delay_ticks")
    b(0x50, 0x53, 0x52, 0x1E)  # push ax,bx,dx,ds
    b(0x89, 0xCB)              # mov bx,cx
    b(0xB8)
    w(0x0040)
    b(0x8E, 0xD8)              # mov ds,ax
    b(0x8B, 0x16)
    w(0x006C)                  # mov dx,[006ch]
    b(0x01, 0xD3)              # add bx,dx
    label("delay_wait")
    b(0x8B, 0x16)
    w(0x006C)
    b(0x39, 0xDA)              # cmp dx,bx
    jb("delay_wait")
    b(0x1F, 0x5A, 0x5B, 0x58, 0xC3)

    label("msg")
    code.extend(
        b"\r\nCardPuter86 PC Speaker test\r\n"
        b"Playing PIT channel 2 tones...\r\n$"
    )
    label("done_msg")
    code.extend(b"Done.\r\n$")

    label("notes")
    for freq in (262, 330, 392, 523, 392, 330, 262, 0):
        w(freq)

    for offset, target, kind in patches:
        if target not in labels:
            raise ValueError(f"unknown label: {target}")
        target_offset = labels[target]
        if kind == "off16":
            value = 0x0100 + target_offset
        elif kind == "rel16":
            value = target_offset - (offset + 2)
        elif kind == "rel8":
            value = target_offset - (offset + 1)
            if not -128 <= value <= 127:
                raise ValueError(f"short jump out of range: {target}")
        else:
            raise ValueError(kind)
        code[offset] = value & 0xFF
        if kind != "rel8":
            code[offset + 1] = (value >> 8) & 0xFF

    return bytes(code)


def get_fat12_entry(fat: bytearray, cluster: int) -> int:
    offset = cluster + cluster // 2
    if cluster & 1:
        return ((fat[offset] >> 4) | (fat[offset + 1] << 4)) & 0xFFF
    return (fat[offset] | ((fat[offset + 1] & 0x0F) << 8)) & 0xFFF


def set_fat12_entry(fat: bytearray, cluster: int, value: int) -> None:
    offset = cluster + cluster // 2
    value &= 0xFFF
    if cluster & 1:
        fat[offset] = (fat[offset] & 0x0F) | ((value & 0x0F) << 4)
        fat[offset + 1] = (value >> 4) & 0xFF
    else:
        fat[offset] = value & 0xFF
        fat[offset + 1] = (fat[offset + 1] & 0xF0) | ((value >> 8) & 0x0F)


def free_fat12_chain(fat: bytearray, start_cluster: int) -> None:
    cluster = start_cluster
    seen: set[int] = set()
    while 2 <= cluster < 0xFF8 and cluster not in seen:
        seen.add(cluster)
        next_cluster = get_fat12_entry(fat, cluster)
        set_fat12_entry(fat, cluster, 0)
        cluster = next_cluster


def install_file(img_path: Path, dos_name: str, payload: bytes) -> None:
    image = bytearray(img_path.read_bytes())
    bytes_per_sector = int.from_bytes(image[11:13], "little")
    sectors_per_cluster = image[13]
    reserved_sectors = int.from_bytes(image[14:16], "little")
    fat_count = image[16]
    root_entries = int.from_bytes(image[17:19], "little")
    total_sectors = int.from_bytes(image[19:21], "little")
    sectors_per_fat = int.from_bytes(image[22:24], "little")

    root_offset = (reserved_sectors + fat_count * sectors_per_fat) * bytes_per_sector
    root_size = root_entries * 32
    data_offset = root_offset + root_size
    cluster_size = bytes_per_sector * sectors_per_cluster
    data_sectors = total_sectors - (data_offset // bytes_per_sector)
    cluster_count = data_sectors // sectors_per_cluster

    fat_offset = reserved_sectors * bytes_per_sector
    fat_size = sectors_per_fat * bytes_per_sector
    fat = bytearray(image[fat_offset:fat_offset + fat_size])

    name = dos_name.upper().encode("ascii")
    if len(name) != 11:
        raise ValueError("DOS name must be exactly 11 bytes")

    existing_slot = None
    existing_cluster = 0
    free_slot = None
    for idx in range(root_entries):
        entry_offset = root_offset + idx * 32
        first = image[entry_offset]
        entry_name = bytes(image[entry_offset:entry_offset + 11])
        if first == 0x00 and free_slot is None:
            free_slot = idx
        if first == 0xE5 and free_slot is None:
            free_slot = idx
        if entry_name == name:
            existing_slot = idx
            existing_cluster = int.from_bytes(
                image[entry_offset + 26:entry_offset + 28], "little"
            )
            break

    slot = existing_slot if existing_slot is not None else free_slot
    if slot is None:
        raise RuntimeError("no free root directory slot")
    if existing_cluster:
        free_fat12_chain(fat, existing_cluster)

    needed = (len(payload) + cluster_size - 1) // cluster_size
    free_clusters = [
        cluster for cluster in range(2, cluster_count + 2)
        if get_fat12_entry(fat, cluster) == 0
    ]
    if len(free_clusters) < needed:
        raise RuntimeError("not enough free clusters in image")
    chain = free_clusters[:needed]

    for idx, cluster in enumerate(chain):
        set_fat12_entry(fat, cluster, chain[idx + 1] if idx + 1 < len(chain) else 0xFFF)
        start = data_offset + (cluster - 2) * cluster_size
        block = payload[idx * cluster_size:(idx + 1) * cluster_size]
        image[start:start + cluster_size] = bytes(cluster_size)
        image[start:start + len(block)] = block

    for fat_index in range(fat_count):
        start = fat_offset + fat_index * fat_size
        image[start:start + fat_size] = fat

    entry_offset = root_offset + slot * 32
    entry = bytearray(32)
    entry[0:11] = name
    entry[11] = 0x20
    entry[26:28] = chain[0].to_bytes(2, "little")
    entry[28:32] = len(payload).to_bytes(4, "little")
    image[entry_offset:entry_offset + 32] = entry

    img_path.write_bytes(image)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, default=DEFAULT_IMG)
    parser.add_argument("--write-com", type=Path)
    args = parser.parse_args()

    payload = emit_com()
    if args.write_com:
        args.write_com.write_bytes(payload)
    install_file(args.image, "SNDTEST COM", payload)
    print(f"Installed SNDTEST.COM ({len(payload)} bytes) into {args.image}")


if __name__ == "__main__":
    main()
