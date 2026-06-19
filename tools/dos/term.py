#!/usr/bin/env python3
"""Build TERM.COM, a tiny COM1 terminal for CardPuter86 Hayes modem tests."""

from __future__ import annotations

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_COM = ROOT / "build" / "dos" / "TERM.COM"


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

    def loop(target: str) -> None:
        b(0xE2); patches.append((len(code), target, "rel8")); b(0)

    def print_msg(name: str) -> None:
        b(0xBA); patches.append((len(code), name, "off16")); w(0)
        b(0xB4, 0x09, 0xCD, 0x21)

    print_msg("title")
    call("com_init")
    label("main_loop")
    call("poll_rx")
    b(0xB4, 0x01, 0xCD, 0x16)  # keyboard status
    jz("main_loop")
    b(0xB4, 0x00, 0xCD, 0x16)  # read key
    b(0x3C, 0x1B); jz("quit")
    call("send_al")
    jmp("main_loop")
    label("quit")
    print_msg("bye")
    b(0xB8); w(0x4C00); b(0xCD, 0x21)

    label("com_init")
    b(0xBA); w(0x03FB); b(0xB0, 0x80, 0xEE)
    b(0xBA); w(0x03F8); b(0xB0, 0x0C, 0xEE)
    b(0xBA); w(0x03F9); b(0x30, 0xC0, 0xEE)
    b(0xBA); w(0x03FB); b(0xB0, 0x03, 0xEE)
    b(0xBA); w(0x03F9); b(0x30, 0xC0, 0xEE)
    b(0xBA); w(0x03FC); b(0xB0, 0x0B, 0xEE)
    b(0xBA); w(0x03FA); b(0xB0, 0x07, 0xEE)
    b(0xC3)

    label("poll_rx")
    b(0xBA); w(0x03FD)
    b(0xEC, 0xA8, 0x01)
    jz("poll_done")
    b(0xBA); w(0x03F8)
    b(0xEC)
    call("putch")
    jmp("poll_rx")
    label("poll_done")
    b(0xC3)

    label("send_al")
    b(0x50)
    b(0xB9); w(0xFFFF)
    label("send_wait")
    b(0xBA); w(0x03FD)
    b(0xEC, 0xA8, 0x20)
    jnz("send_ready")
    loop("send_wait")
    b(0x58, 0xC3)
    label("send_ready")
    b(0x58)
    b(0xBA); w(0x03F8)
    b(0xEE, 0xC3)

    label("putch")
    b(0x52, 0x88, 0xC2, 0xB4, 0x02, 0xCD, 0x21, 0x5A, 0xC3)

    label("title")
    code.extend(b"TERM.COM - COM1 Hayes terminal. Remote echo only. ESC quits.\r\n$")
    label("bye")
    code.extend(b"\r\nBye.\r\n$")

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
    parser.add_argument("--write-com", type=Path, default=DEFAULT_COM)
    args = parser.parse_args()
    payload = emit_com()
    args.write_com.parent.mkdir(parents=True, exist_ok=True)
    args.write_com.write_bytes(payload)
    print(f"Wrote TERM.COM ({len(payload)} bytes) to {args.write_com}")


if __name__ == "__main__":
    main()
