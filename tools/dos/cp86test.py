#!/usr/bin/env python3
"""Build and install the CardPuter86 all-in-one DOS hardware test program."""

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
        b(0xE8); patches.append((len(code), target, "rel16")); w(0)

    def jmp(target: str) -> None:
        b(0xE9); patches.append((len(code), target, "rel16")); w(0)

    def jz(target: str) -> None:
        b(0x74); patches.append((len(code), target, "rel8")); b(0)

    def jnz(target: str) -> None:
        b(0x75); patches.append((len(code), target, "rel8")); b(0)

    def jnc(target: str) -> None:
        b(0x73); patches.append((len(code), target, "rel8")); b(0)

    def loop(target: str) -> None:
        b(0xE2); patches.append((len(code), target, "rel8")); b(0)

    def print_msg(name: str) -> None:
        b(0xBA); patches.append((len(code), name, "off16")); w(0)
        b(0xB4, 0x09, 0xCD, 0x21)

    print_msg("title")
    print_msg("rtc_label")
    b(0xB4, 0x2A, 0xCD, 0x21)  # DOS get date
    b(0x89, 0xC8); call("print_word_dec")  # CX year
    b(0xB0, ord('-')); call("putch")
    b(0x88, 0xF0); call("print_byte_2d")   # DH month
    b(0xB0, ord('-')); call("putch")
    b(0x88, 0xD0); call("print_byte_2d")   # DL day
    b(0xB0, ord(' ')); call("putch")
    b(0xB4, 0x2C, 0xCD, 0x21)  # DOS get time
    b(0x88, 0xE8); call("print_byte_2d")   # CH hour
    b(0xB0, ord(':')); call("putch")
    b(0x88, 0xC8); call("print_byte_2d")   # CL minute
    b(0xB0, ord(':')); call("putch")
    b(0x88, 0xF0); call("print_byte_2d")   # DH second
    print_msg("crlf")

    print_msg("bios_label")
    b(0xB4, 0x00, 0xCD, 0x1A)  # BIOS get ticks
    print_msg("ticks_msg")
    b(0x89, 0xC8); call("print_hex16")
    b(0x89, 0xD0); call("print_hex16")
    print_msg("crlf")

    print_msg("disk_label")
    b(0xB2, 0x00); call("probe_drive")
    b(0xB2, 0x01); call("probe_drive")
    b(0xB2, 0x80); call("probe_drive")
    b(0xB2, 0x81); call("probe_drive")

    print_msg("kbd_label")
    b(0xB4, 0x08, 0xCD, 0x21)  # getchar no echo
    print_msg("key_msg")
    call("print_hex8")
    print_msg("crlf")

    print_msg("sound_label")
    b(0xBE); patches.append((len(code), "notes", "off16")); w(0)
    label("next_note")
    b(0xAD, 0x09, 0xC0); jz("done_sound")
    call("speaker_on")
    b(0xB9); w(10); call("delay_ticks")
    call("speaker_off")
    b(0xB9); w(2); call("delay_ticks")
    jmp("next_note")
    label("done_sound")
    call("speaker_off")

    print_msg("usb_label")
    print_msg("done_msg")
    b(0xB8); w(0x4C00); b(0xCD, 0x21)

    label("probe_drive")
    b(0x52)                    # push dx
    print_msg("drive_prefix")
    b(0x5A, 0x52)              # pop dx; push dx
    b(0x88, 0xD0); call("print_hex8")
    print_msg("colon_space")
    b(0x5A, 0x52)              # pop dx; push dx
    b(0xB4, 0x08, 0xCD, 0x13)  # get drive params
    jnc("drive_ok")
    print_msg("missing_msg")
    b(0x5A, 0xC3)
    label("drive_ok")
    print_msg("present_msg")
    b(0x5A, 0xC3)

    label("putch")
    b(0x52, 0x88, 0xC2, 0xB4, 0x02, 0xCD, 0x21, 0x5A, 0xC3)

    label("print_nibble")
    b(0x24, 0x0F, 0x04, 0x30, 0x3C, 0x3A)
    b(0x72, 0x02)  # jb emit
    b(0x04, 0x07)  # A-F
    call("putch")
    b(0xC3)

    label("print_hex8")
    b(0x50, 0xD0, 0xE8, 0xD0, 0xE8, 0xD0, 0xE8, 0xD0, 0xE8)
    call("print_nibble")
    b(0x58)
    call("print_nibble")
    b(0xC3)

    label("print_hex16")
    b(0x50, 0x86, 0xC4)
    call("print_hex8")
    b(0x58)
    call("print_hex8")
    b(0xC3)

    label("print_byte_2d")
    b(0xB4, 0x00, 0xB3, 10, 0xF6, 0xF3, 0x50)
    b(0x04, 0x30); call("putch")
    b(0x58, 0x88, 0xE0, 0x04, 0x30); call("putch")
    b(0xC3)

    label("print_word_dec")
    b(0x53, 0x51, 0x52, 0xBB); w(1000); b(0x31, 0xD2, 0xF7, 0xF3, 0x04, 0x30); call("putch")
    b(0x89, 0xD0, 0xBB); w(100); b(0x31, 0xD2, 0xF7, 0xF3, 0x04, 0x30); call("putch")
    b(0x89, 0xD0, 0xBB); w(10); b(0x31, 0xD2, 0xF7, 0xF3, 0x04, 0x30); call("putch")
    b(0x88, 0xD0, 0x04, 0x30); call("putch")
    b(0x5A, 0x59, 0x5B, 0xC3)

    label("speaker_on")
    b(0x89, 0xC3, 0xBA); w(0x0012); b(0xB8); w(0x34DC)
    b(0xF7, 0xF3, 0x89, 0xC3, 0xB0, 0xB6, 0xE6, 0x43)
    b(0x88, 0xD8, 0xE6, 0x42, 0x88, 0xF8, 0xE6, 0x42)
    b(0xE4, 0x61, 0x0C, 0x03, 0xE6, 0x61, 0xC3)

    label("speaker_off")
    b(0xE4, 0x61, 0x24, 0xFC, 0xE6, 0x61, 0xC3)

    label("delay_ticks")
    b(0x50, 0x52)
    label("delay_outer")
    b(0xBA); w(0xFFFF)
    label("delay_inner")
    b(0x4A); jnz("delay_inner")
    loop("delay_outer")
    b(0x5A, 0x58, 0xC3)

    label("title")
    code.extend(b"\r\nCardPuter86 all-in-one test\r\n$" )
    label("rtc_label")
    code.extend(b"RTC/DOS time: $")
    label("bios_label")
    code.extend(b"BIOS clock: $")
    label("ticks_msg")
    code.extend(b"ticks=0x$")
    label("disk_label")
    code.extend(b"Disk INT13 probes:\r\n$")
    label("drive_prefix")
    code.extend(b"  DL=$")
    label("colon_space")
    code.extend(b": $")
    label("present_msg")
    code.extend(b"present\r\n$")
    label("missing_msg")
    code.extend(b"missing\r\n$")
    label("kbd_label")
    code.extend(b"Keyboard test: press one key...$")
    label("key_msg")
    code.extend(b"\r\nASCII/scan low byte: 0x$")
    label("sound_label")
    code.extend(b"Speaker test: playing tones...\r\n$")
    label("usb_label")
    code.extend(b"USB modes are selected from Settings.\r\n$")
    label("done_msg")
    code.extend(b"Done.\r\n$")
    label("crlf")
    code.extend(b"\r\n$")
    label("notes")
    for freq in (262, 330, 392, 523, 392, 330, 262, 0):
        w(freq)

    for offset, target, kind in patches:
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


def update_file(img_path: Path, dos_name: str, payload: bytes | None) -> None:
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
        if first in (0x00, 0xE5) and free_slot is None:
            free_slot = idx
        if entry_name == name:
            existing_slot = idx
            existing_cluster = int.from_bytes(image[entry_offset + 26:entry_offset + 28], "little")
            break

    if existing_cluster:
        free_fat12_chain(fat, existing_cluster)
    if payload is None:
        if existing_slot is not None:
            image[root_offset + existing_slot * 32] = 0xE5
    else:
        slot = existing_slot if existing_slot is not None else free_slot
        if slot is None:
            raise RuntimeError("no free root directory slot")
        needed = (len(payload) + cluster_size - 1) // cluster_size
        free_clusters = [c for c in range(2, cluster_count + 2) if get_fat12_entry(fat, c) == 0]
        if len(free_clusters) < needed:
            raise RuntimeError("not enough free clusters in image")
        chain = free_clusters[:needed]
        for idx, cluster in enumerate(chain):
            set_fat12_entry(fat, cluster, chain[idx + 1] if idx + 1 < len(chain) else 0xFFF)
            start = data_offset + (cluster - 2) * cluster_size
            block = payload[idx * cluster_size:(idx + 1) * cluster_size]
            image[start:start + cluster_size] = bytes(cluster_size)
            image[start:start + len(block)] = block
        entry_offset = root_offset + slot * 32
        entry = bytearray(32)
        entry[0:11] = name
        entry[11] = 0x20
        entry[26:28] = chain[0].to_bytes(2, "little")
        entry[28:32] = len(payload).to_bytes(4, "little")
        image[entry_offset:entry_offset + 32] = entry

    for fat_index in range(fat_count):
        start = fat_offset + fat_index * fat_size
        image[start:start + fat_size] = fat
    img_path.write_bytes(image)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, default=DEFAULT_IMG)
    parser.add_argument("--write-com", type=Path)
    args = parser.parse_args()

    payload = emit_com()
    if args.write_com:
        args.write_com.write_bytes(payload)
    update_file(args.image, "SND" "TEST COM", None)
    update_file(args.image, "CP86TESTCOM", payload)
    print(f"Installed CP86TEST.COM ({len(payload)} bytes) into {args.image}; removed legacy speaker test")


if __name__ == "__main__":
    main()
