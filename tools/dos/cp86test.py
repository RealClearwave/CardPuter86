#!/usr/bin/env python3
"""Build and install the CardPuter86 all-in-one DOS hardware test program."""

from __future__ import annotations

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_IMG = ROOT / "ESP32" / "CardPuter86" / "data" / "cardputer86.img"
DEFAULT_COM = ROOT / "tools" / "dos" / "image_root" / "CP86TEST.COM"


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

    def ask_test(prompt: str, skip: str) -> None:
        print_msg(prompt)
        call("ask_run")
        b(0x84, 0xC0)  # test al,al
        jz(skip)

    print_msg("title")

    ask_test("rtc_prompt", "skip_rtc")
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
    label("skip_rtc")

    ask_test("bios_prompt", "skip_bios")
    print_msg("bios_label")
    b(0xB4, 0x00, 0xCD, 0x1A)  # BIOS get ticks
    print_msg("ticks_msg")
    b(0x89, 0xC8); call("print_hex16")
    b(0x89, 0xD0); call("print_hex16")
    print_msg("crlf")
    label("skip_bios")

    ask_test("disk_prompt", "skip_disk")
    print_msg("disk_label")
    b(0xB2, 0x00); call("probe_drive")
    b(0xB2, 0x01); call("probe_drive")
    b(0xB2, 0x80); call("probe_drive")
    b(0xB2, 0x81); call("probe_drive")
    label("skip_disk")

    ask_test("kbd_prompt", "skip_kbd")
    print_msg("kbd_label")
    b(0xB4, 0x08, 0xCD, 0x21)  # getchar no echo
    print_msg("key_msg")
    call("print_hex8")
    print_msg("crlf")
    label("skip_kbd")

    ask_test("sound_prompt", "skip_sound")
    print_msg("sound_label")
    b(0xBE); patches.append((len(code), "notes", "off16")); w(0)
    label("next_note")
    b(0xAD, 0x09, 0xC0); jz("done_sound")
    call("speaker_on")
    b(0xB9); w(4); call("delay_ticks")
    call("speaker_off")
    b(0xB9); w(1); call("delay_ticks")
    jmp("next_note")
    label("done_sound")
    call("speaker_off")
    label("skip_sound")

    ask_test("modem_prompt", "skip_modem")
    print_msg("modem_label")
    call("modem_init_com1")
    call("modem_test")
    label("skip_modem")

    ask_test("network_prompt", "skip_network")
    print_msg("network_label")
    call("modem_init_com1")
    b(0xBE); patches.append((len(code), "wifi_status_cmd", "off16")); w(0)
    call("modem_send_string")
    call("modem_print_response")
    label("skip_network")

    ask_test("usb_prompt", "skip_usb")
    print_msg("usb_label")
    label("skip_usb")
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

    label("ask_run")
    print_msg("run_prompt")
    b(0xB4, 0x08, 0xCD, 0x21)  # getchar no echo
    b(0x3C, ord('n')); jz("ask_skip")
    b(0x3C, ord('N')); jz("ask_skip")
    print_msg("crlf")
    b(0xB0, 0x01, 0xC3)        # mov al,1; ret
    label("ask_skip")
    print_msg("skipped_msg")
    b(0x30, 0xC0, 0xC3)        # xor al,al; ret

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

    label("modem_init_com1")
    b(0xBA); w(0x03FB)        # LCR
    b(0xB0, 0x80)             # DLAB on
    b(0xEE)
    b(0xBA); w(0x03F8)        # divisor low: 115200 / 9600 = 12
    b(0xB0, 0x0C)
    b(0xEE)
    b(0xBA); w(0x03F9)        # divisor high
    b(0x30, 0xC0)
    b(0xEE)
    b(0xBA); w(0x03FB)        # 8 data bits, no parity, 1 stop, DLAB off
    b(0xB0, 0x03)
    b(0xEE)
    b(0xBA); w(0x03F9)        # IER off
    b(0x30, 0xC0)
    b(0xEE)
    b(0xBA); w(0x03FC)        # DTR + RTS + OUT2
    b(0xB0, 0x0B)
    b(0xEE)
    b(0xBA); w(0x03FA)        # FCR: enable FIFO, clear RX/TX on 16550 clones
    b(0xB0, 0x07)
    b(0xEE)
    b(0xC3)

    label("modem_test")
    b(0xB0, ord('A')); call("modem_send")
    b(0xB0, ord('T')); call("modem_send")
    b(0xB0, 0x0D); call("modem_send")
    b(0xB9); w(0xFFFF)        # cx timeout
    b(0x31, 0xDB)             # xor bx,bx; bl tracks seen 'O'
    label("modem_wait")
    b(0xBA); w(0x03FD)        # dx=LSR
    b(0xEC, 0xA8, 0x01)       # in al,dx; test al,1
    jz("modem_next")
    b(0xBA); w(0x03F8)        # dx=RBR
    b(0xEC)                   # in al,dx
    b(0x3C, ord('O')); jz("modem_seen_o")
    b(0x80, 0xFB, 0x01); jnz("modem_next")  # cmp bl,1
    b(0x3C, ord('K')); jz("modem_ok")
    label("modem_next")
    loop("modem_wait")
    print_msg("missing_msg")
    b(0xC3)
    label("modem_seen_o")
    b(0xB3, 0x01)
    jmp("modem_next")
    label("modem_ok")
    print_msg("present_msg")
    b(0xC3)

    label("modem_send_string")
    b(0xAC)                    # lodsb
    b(0x08, 0xC0)              # or al,al
    jz("modem_send_string_done")
    call("modem_send")
    jmp("modem_send_string")
    label("modem_send_string_done")
    b(0xC3)

    label("modem_print_response")
    b(0xB9); w(0xFFFF)
    label("modem_response_wait")
    b(0xBA); w(0x03FD)
    b(0xEC, 0xA8, 0x01)       # in al,dx; test al,1
    jz("modem_response_next")
    b(0xBA); w(0x03F8)
    b(0xEC)
    call("putch")
    b(0xB9); w(0x4000)        # extend timeout after activity
    label("modem_response_next")
    loop("modem_response_wait")
    print_msg("crlf")
    b(0xC3)

    label("modem_send")
    b(0x50)                   # push ax
    b(0xB9); w(0xFFFF)
    label("modem_send_wait")
    b(0xBA); w(0x03FD)
    b(0xEC, 0xA8, 0x20)       # in al,dx; test al,20h
    jnz("modem_send_ready")
    loop("modem_send_wait")
    b(0x58, 0xC3)             # pop ax; ret
    label("modem_send_ready")
    b(0x58)                   # pop ax
    b(0xBA); w(0x03F8)
    b(0xEE, 0xC3)             # out dx,al; ret

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
    label("run_prompt")
    code.extend(b"Run? [Y/n] $")
    label("skipped_msg")
    code.extend(b"\r\nskipped\r\n$")
    label("rtc_prompt")
    code.extend(b"\r\nRTC/DOS time test\r\n$")
    label("rtc_label")
    code.extend(b"RTC/DOS time: $")
    label("bios_prompt")
    code.extend(b"\r\nBIOS clock test\r\n$")
    label("bios_label")
    code.extend(b"BIOS clock: $")
    label("ticks_msg")
    code.extend(b"ticks=0x$")
    label("disk_prompt")
    code.extend(b"\r\nDisk INT13 probe test\r\n$")
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
    label("kbd_prompt")
    code.extend(b"\r\nKeyboard input test\r\n$")
    label("kbd_label")
    code.extend(b"Keyboard test: press one key...$")
    label("key_msg")
    code.extend(b"\r\nASCII/scan low byte: 0x$")
    label("sound_prompt")
    code.extend(b"\r\nPC speaker test\r\n$")
    label("sound_label")
    code.extend(b"Speaker test: playing tones...\r\n$")
    label("modem_prompt")
    code.extend(b"\r\nCOM1 Hayes modem AT test\r\n$")
    label("modem_label")
    code.extend(b"Hayes modem COM1 AT probe: $")
    label("network_prompt")
    code.extend(b"\r\nNetwork status test\r\n$")
    label("network_label")
    code.extend(b"Hayes modem AT+WIFI? response:\r\n$")
    label("usb_prompt")
    code.extend(b"\r\nUSB mode note\r\n$")
    label("usb_label")
    code.extend(b"USB modes are selected from Settings.\r\n$")
    label("done_msg")
    code.extend(b"Done.\r\n$")
    label("crlf")
    code.extend(b"\r\n$")
    label("notes")
    for freq in (262, 330, 392, 523, 392, 330, 262, 0):
        w(freq)
    label("wifi_status_cmd")
    code.extend(b"AT+WIFI?\r\0")

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
    parser.add_argument("--write-com", type=Path, default=DEFAULT_COM)
    parser.add_argument("--update-image", action="store_true",
                        help="legacy path: patch CP86TEST.COM into an existing image")
    args = parser.parse_args()

    payload = emit_com()
    if args.write_com:
        args.write_com.parent.mkdir(parents=True, exist_ok=True)
        args.write_com.write_bytes(payload)
    if args.update_image:
        update_file(args.image, "SND" "TEST COM", None)
        update_file(args.image, "CP86TESTCOM", payload)
        print(f"Installed CP86TEST.COM ({len(payload)} bytes) into {args.image}; removed legacy speaker test")
    else:
        print(f"Wrote CP86TEST.COM ({len(payload)} bytes) to {args.write_com}")


if __name__ == "__main__":
    main()
