#!/usr/bin/env python3
"""Build and install the CardPuter86 DOS COM1 modem test program."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
DEFAULT_IMG = ROOT / "ESP32" / "CardPuter86" / "data" / "cardputer86.img"
sys.path.insert(0, str(Path(__file__).resolve().parent))
from sndtest import install_file  # noqa: E402


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

    def patch_label(name: str, kind: str) -> None:
        patches.append((len(code), name, kind))
        if kind == "rel8":
            b(0)
        else:
            w(0)

    def call(name: str) -> None:
        b(0xE8)
        patch_label(name, "rel16")

    def jmp(name: str) -> None:
        b(0xE9)
        patch_label(name, "rel16")

    def jz(name: str) -> None:
        b(0x74)
        patch_label(name, "rel8")

    def jnz(name: str) -> None:
        b(0x75)
        patch_label(name, "rel8")

    def loop(name: str) -> None:
        b(0xE2)
        patch_label(name, "rel8")

    def print_msg(name: str) -> None:
        b(0xBA)
        patch_label(name, "off16")
        b(0xB4, 0x09, 0xCD, 0x21)

    print_msg("msg_intro")
    call("init_com1")
    call("test_scratch")
    print_msg("msg_send")
    b(0xBE)
    patch_label("cmd_at", "off16")
    call("send_string")
    print_msg("msg_response")
    call("read_response")
    print_msg("msg_done")
    b(0xB8)
    w(0x4C00)
    b(0xCD, 0x21)

    label("init_com1")
    b(0xBA)
    w(0x03FB)
    b(0xB0, 0x80, 0xEE)  # DLAB
    b(0xBA)
    w(0x03F8)
    b(0xB0, 0x0C, 0xEE)  # divisor 12 = 9600 baud
    b(0xBA)
    w(0x03F9)
    b(0x30, 0xC0, 0xEE)
    b(0xBA)
    w(0x03FB)
    b(0xB0, 0x03, 0xEE)  # 8N1
    b(0xBA)
    w(0x03FC)
    b(0xB0, 0x0B, 0xEE)  # DTR, RTS, OUT2
    b(0xC3)

    label("test_scratch")
    b(0xBA)
    w(0x03FF)
    b(0xB0, 0x5A, 0xEE)
    b(0xEC)
    b(0x3C, 0x5A)
    jnz("scratch_fail")
    print_msg("msg_scratch_ok")
    b(0xC3)
    label("scratch_fail")
    print_msg("msg_scratch_fail")
    b(0xC3)

    label("send_string")
    b(0xAC)              # lodsb
    b(0x08, 0xC0)        # or al,al
    jz("send_done")
    call("send_char")
    jmp("send_string")
    label("send_done")
    b(0xC3)

    label("send_char")
    b(0x50)              # push ax
    label("wait_thre")
    b(0xBA)
    w(0x03FD)
    b(0xEC)
    b(0xA8, 0x20)
    jz("wait_thre")
    b(0xBA)
    w(0x03F8)
    b(0x58)              # pop ax
    b(0xEE)
    b(0xC3)

    label("read_response")
    b(0xB9)
    w(0xFFFF)
    label("read_loop")
    b(0xBA)
    w(0x03FD)
    b(0xEC)
    b(0xA8, 0x01)
    jz("read_wait")
    b(0xBA)
    w(0x03F8)
    b(0xEC)
    b(0x88, 0xC2)        # mov dl,al
    b(0xB4, 0x02, 0xCD, 0x21)
    b(0xB9)
    w(0xFFFF)
    label("read_wait")
    loop("read_loop")
    b(0xC3)

    label("msg_intro")
    code.extend(b"\r\nCardPuter86 COM1 modem test\r\n$");
    label("msg_scratch_ok")
    code.extend(b"COM1 scratch register: OK\r\n$");
    label("msg_scratch_fail")
    code.extend(b"COM1 scratch register: FAIL\r\n$");
    label("msg_send")
    code.extend(b"Sending AT to COM1...\r\n$");
    label("msg_response")
    code.extend(b"Response follows; expected AT and OK:\r\n$");
    label("msg_done")
    code.extend(b"\r\nDone.\r\n$");
    label("cmd_at")
    code.extend(b"AT\r\0")

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


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--image", type=Path, default=DEFAULT_IMG)
    parser.add_argument("--write-com", type=Path)
    args = parser.parse_args()

    payload = emit_com()
    if args.write_com:
        args.write_com.write_bytes(payload)
    install_file(args.image, "MODEM   COM", payload)
    print(f"Installed MODEM.COM ({len(payload)} bytes) into {args.image}")


if __name__ == "__main__":
    main()
