#!/usr/bin/env python3
"""Build TE.COM, a tiny nano-like full-screen editor for 128K DOS."""

from __future__ import annotations

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_COM = ROOT / "build" / "dos" / "TE.COM"


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

    def jc(target: str) -> None:
        b(0x72); patches.append((len(code), target, "rel8")); b(0)

    def ja(target: str) -> None:
        b(0x77); patches.append((len(code), target, "rel8")); b(0)

    def jb(target: str) -> None:
        b(0x72); patches.append((len(code), target, "rel8")); b(0)

    def print_msg(name: str) -> None:
        b(0xBA); patches.append((len(code), name, "off16")); w(0)
        b(0xB4, 0x09, 0xCD, 0x21)

    def mov_al_mem(name: str) -> None:
        b(0xA0); patches.append((len(code), name, "off16")); w(0)

    def mov_mem_al(name: str) -> None:
        b(0xA2); patches.append((len(code), name, "off16")); w(0)

    def mov_byte(name: str, value: int) -> None:
        b(0xC6, 0x06); patches.append((len(code), name, "off16")); w(0); b(value)

    def cmp_byte(name: str, value: int) -> None:
        b(0x80, 0x3E); patches.append((len(code), name, "off16")); w(0); b(value)

    b(0x0E, 0x1F)  # push cs; pop ds
    call("parse_name")
    b(0x84, 0xC0, 0x75, 0x03); jmp("usage")
    call("clear_buffer")
    call("load_file")
    call("draw_screen")

    label("main_loop")
    call("show_cursor")
    b(0xB4, 0x00, 0xCD, 0x16)  # BIOS read key
    b(0x3C, 0x00, 0x75, 0x03); jmp("extended_key")
    b(0x3C, 0x18, 0x75, 0x03); jmp("exit_editor")  # Ctrl+X
    b(0x3C, 0x0F, 0x75, 0x03); jmp("save_and_redraw")  # Ctrl+O
    b(0x3C, 0x08, 0x75, 0x03); jmp("backspace")
    b(0x3C, 0x0D, 0x75, 0x03); jmp("enter_key")
    b(0x3C, 0x20); jb("main_loop")
    b(0x3C, 0x7E); ja("main_loop")
    call("put_printable")
    jmp("redraw")

    label("extended_key")
    b(0x80, 0xFC, 0x48, 0x75, 0x03); jmp("up_key")
    b(0x80, 0xFC, 0x50, 0x75, 0x03); jmp("down_key")
    b(0x80, 0xFC, 0x4B, 0x75, 0x03); jmp("left_key")
    b(0x80, 0xFC, 0x4D, 0x75, 0x03); jmp("right_key")
    jmp("main_loop")

    label("redraw")
    call("draw_screen")
    jmp("main_loop")

    label("save_and_redraw")
    call("save_file")
    call("draw_screen")
    print_msg("saved_msg")
    jmp("main_loop")

    label("exit_editor")
    call("clear_screen")
    b(0xB8); w(0x4C00); b(0xCD, 0x21)

    label("usage")
    print_msg("usage_msg")
    b(0xB8); w(0x4C01); b(0xCD, 0x21)

    label("left_key")
    cmp_byte("cur_x", 0); b(0x75, 0x03); jmp("main_loop")
    b(0xFE, 0x0E); patches.append((len(code), "cur_x", "off16")); w(0)
    jmp("main_loop")

    label("right_key")
    cmp_byte("cur_x", 39); b(0x75, 0x03); jmp("main_loop")
    b(0xFE, 0x06); patches.append((len(code), "cur_x", "off16")); w(0)
    jmp("main_loop")

    label("up_key")
    cmp_byte("cur_y", 0); b(0x75, 0x03); jmp("main_loop")
    b(0xFE, 0x0E); patches.append((len(code), "cur_y", "off16")); w(0)
    jmp("main_loop")

    label("down_key")
    cmp_byte("cur_y", 13); b(0x75, 0x03); jmp("main_loop")
    b(0xFE, 0x06); patches.append((len(code), "cur_y", "off16")); w(0)
    jmp("main_loop")

    label("enter_key")
    cmp_byte("cur_y", 13); b(0x75, 0x03); jmp("main_loop")
    mov_byte("cur_x", 0)
    b(0xFE, 0x06); patches.append((len(code), "cur_y", "off16")); w(0)
    jmp("main_loop")

    label("backspace")
    cmp_byte("cur_x", 0); b(0x75, 0x03); jmp("main_loop")
    b(0xFE, 0x0E); patches.append((len(code), "cur_x", "off16")); w(0)
    call("cursor_ptr")
    b(0xC6, 0x04, 0x20)  # mov byte [si], ' '
    jmp("redraw")

    label("put_printable")
    b(0x50)  # push ax
    call("cursor_ptr")
    b(0x58, 0x88, 0x04)  # pop ax; mov [si],al
    cmp_byte("cur_x", 39); jz("put_wrap")
    b(0xFE, 0x06); patches.append((len(code), "cur_x", "off16")); w(0)
    b(0xC3)
    label("put_wrap")
    cmp_byte("cur_y", 13); jz("put_done")
    mov_byte("cur_x", 0)
    b(0xFE, 0x06); patches.append((len(code), "cur_y", "off16")); w(0)
    label("put_done")
    b(0xC3)

    label("cursor_ptr")
    b(0x31, 0xC0)  # xor ax,ax
    mov_al_mem("cur_y")
    b(0xB3, 40, 0xF6, 0xE3)  # mov bl,40; mul bl
    b(0x31, 0xDB)
    b(0x8A, 0x1E); patches.append((len(code), "cur_x", "off16")); w(0)
    b(0x01, 0xD8)  # add ax,bx
    b(0xBE); patches.append((len(code), "buffer", "off16")); w(0)
    b(0x01, 0xC6)  # add si,ax
    b(0xC3)

    label("show_cursor")
    b(0xB4, 0x02, 0xB7, 0x00)
    b(0x8A, 0x16); patches.append((len(code), "cur_x", "off16")); w(0)
    b(0x8A, 0x36); patches.append((len(code), "cur_y", "off16")); w(0)
    b(0x80, 0xC6, 0x01)  # add dh,1
    b(0xCD, 0x10, 0xC3)

    label("clear_screen")
    b(0xB8); w(0x0003); b(0xCD, 0x10, 0xC3)

    label("put_char_at")
    b(0x50, 0x53, 0x51, 0x52)  # push ax,bx,cx,dx
    b(0xB4, 0x02, 0xB7, 0x00, 0xCD, 0x10)
    b(0xB4, 0x09, 0xB7, 0x00, 0xB9); w(1); b(0xCD, 0x10)
    b(0x5A, 0x59, 0x5B, 0x58, 0xC3)

    label("draw_screen")
    call("clear_screen")
    print_msg("title_msg")
    mov_byte("draw_y", 0)
    b(0xBE); patches.append((len(code), "buffer", "off16")); w(0)
    label("draw_row")
    cmp_byte("draw_y", 14); jz("draw_help")
    mov_byte("draw_x", 0)
    label("draw_col")
    cmp_byte("draw_x", 40); jz("draw_next_row")
    b(0xAC)  # lodsb
    b(0xB3, 0x07)
    b(0x8A, 0x16); patches.append((len(code), "draw_x", "off16")); w(0)
    b(0x8A, 0x36); patches.append((len(code), "draw_y", "off16")); w(0)
    b(0x80, 0xC6, 0x01)
    call("put_char_at")
    b(0xFE, 0x06); patches.append((len(code), "draw_x", "off16")); w(0)
    jmp("draw_col")
    label("draw_next_row")
    b(0xFE, 0x06); patches.append((len(code), "draw_y", "off16")); w(0)
    jmp("draw_row")
    label("draw_help")
    b(0xB4, 0x02, 0xB7, 0x00, 0xBA); w(0x0F00); b(0xCD, 0x10)
    print_msg("help_msg")
    b(0xC3)

    label("clear_buffer")
    b(0xBE); patches.append((len(code), "buffer", "off16")); w(0)
    b(0xB9); w(560)
    b(0xB0, 0x20)
    label("clear_loop")
    b(0x88, 0x04, 0x46, 0xE2); patches.append((len(code), "clear_loop", "rel8")); b(0)
    mov_byte("cur_x", 0)
    mov_byte("cur_y", 0)
    b(0xC3)

    label("parse_name")
    b(0xBE); w(0x0081)
    label("skip_ws")
    b(0xAC)
    b(0x3C, 0x0D); jz("parse_fail")
    b(0x3C, ord(' ')); jz("skip_ws")
    b(0x3C, 0x09); jz("skip_ws")
    b(0x4E)
    b(0xBF); patches.append((len(code), "filename", "off16")); w(0)
    b(0xB9); w(63)
    label("copy_name")
    b(0xAC)
    b(0x3C, 0x0D); jz("name_done")
    b(0x3C, ord(' ')); jz("name_done")
    b(0x3C, 0x09); jz("name_done")
    b(0xAA, 0xE2); patches.append((len(code), "copy_name", "rel8")); b(0)
    label("name_done")
    b(0x30, 0xC0, 0xAA, 0xB0, 0x01, 0xC3)
    label("parse_fail")
    b(0x30, 0xC0, 0xC3)

    label("load_file")
    b(0xBA); patches.append((len(code), "filename", "off16")); w(0)
    b(0xB8); w(0x3D00); b(0xCD, 0x21)
    jc("load_done")
    b(0xA3); patches.append((len(code), "handle", "off16")); w(0)
    mov_byte("load_x", 0)
    mov_byte("load_y", 0)
    label("load_loop")
    b(0x8B, 0x1E); patches.append((len(code), "handle", "off16")); w(0)
    b(0xBA); patches.append((len(code), "one_byte", "off16")); w(0)
    b(0xB9); w(1)
    b(0xB4, 0x3F, 0xCD, 0x21)
    jc("load_close")
    b(0x09, 0xC0); jz("load_close")
    mov_al_mem("one_byte")
    b(0x3C, 0x0D); jz("load_loop")
    b(0x3C, 0x0A); jz("load_newline")
    cmp_byte("load_x", 40); jz("load_loop")
    b(0x31, 0xC0)
    mov_al_mem("load_y")
    b(0xB3, 40, 0xF6, 0xE3, 0x31, 0xDB)
    b(0x8A, 0x1E); patches.append((len(code), "load_x", "off16")); w(0)
    b(0x01, 0xD8)
    b(0xBE); patches.append((len(code), "buffer", "off16")); w(0)
    b(0x01, 0xC6)
    mov_al_mem("one_byte")
    b(0x88, 0x04)
    b(0xFE, 0x06); patches.append((len(code), "load_x", "off16")); w(0)
    jmp("load_loop")
    label("load_newline")
    cmp_byte("load_y", 13); jz("load_close")
    mov_byte("load_x", 0)
    b(0xFE, 0x06); patches.append((len(code), "load_y", "off16")); w(0)
    jmp("load_loop")
    label("load_close")
    b(0x8B, 0x1E); patches.append((len(code), "handle", "off16")); w(0)
    b(0xB4, 0x3E, 0xCD, 0x21)
    label("load_done")
    b(0xC3)

    label("save_file")
    b(0xBA); patches.append((len(code), "filename", "off16")); w(0)
    b(0x31, 0xC9, 0xB4, 0x3C, 0xCD, 0x21)
    jc("save_done")
    b(0xA3); patches.append((len(code), "handle", "off16")); w(0)
    mov_byte("save_y", 0)
    b(0xBE); patches.append((len(code), "buffer", "off16")); w(0)
    label("save_loop")
    cmp_byte("save_y", 14); jz("save_close")
    b(0x8B, 0x1E); patches.append((len(code), "handle", "off16")); w(0)
    b(0x89, 0xF2, 0xB9); w(40)
    b(0xB4, 0x40, 0xCD, 0x21)
    b(0x83, 0xC6, 40)
    b(0x8B, 0x1E); patches.append((len(code), "handle", "off16")); w(0)
    b(0xBA); patches.append((len(code), "newline", "off16")); w(0)
    b(0xB9); w(2)
    b(0xB4, 0x40, 0xCD, 0x21)
    b(0xFE, 0x06); patches.append((len(code), "save_y", "off16")); w(0)
    jmp("save_loop")
    label("save_close")
    b(0x8B, 0x1E); patches.append((len(code), "handle", "off16")); w(0)
    b(0xB4, 0x3E, 0xCD, 0x21)
    label("save_done")
    b(0xC3)

    label("title_msg")
    code.extend(b"TE  tiny editor  40x14 buffer\r\n$")
    label("help_msg")
    code.extend(b"^O Save  ^X Exit  Arrows Move$")
    label("saved_msg")
    code.extend(b"\r\nSaved.$")
    label("usage_msg")
    code.extend(b"Usage: TE FILE.TXT\r\n^O saves, ^X exits. Max 40x14 chars.\r\n$")
    label("newline")
    code.extend(b"\r\n")
    label("handle")
    w(0)
    for name in ("cur_x", "cur_y", "draw_x", "draw_y", "load_x", "load_y", "save_y", "one_byte"):
        label(name)
        b(0)
    label("filename")
    code.extend(bytes(64))
    label("buffer")
    code.extend(b" " * 560)

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
    print(f"Wrote TE.COM ({len(payload)} bytes) to {args.write_com}")


if __name__ == "__main__":
    main()
