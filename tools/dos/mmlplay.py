#!/usr/bin/env python3
"""Build MMLPLAY.COM, a tiny DOS MML player for the PC speaker."""

from __future__ import annotations

import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_COM = ROOT / "build" / "dos" / "MMLPLAY.COM"


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

    def jb(target: str) -> None:
        b(0x72); patches.append((len(code), target, "rel8")); b(0)

    def ja(target: str) -> None:
        b(0x77); patches.append((len(code), target, "rel8")); b(0)

    def loop(target: str) -> None:
        b(0xE2); patches.append((len(code), target, "rel8")); b(0)

    def print_msg(name: str) -> None:
        b(0xBA); patches.append((len(code), name, "off16")); w(0)
        b(0xB4, 0x09, 0xCD, 0x21)

    print_msg("title")
    call("parse_arg")
    b(0x84, 0xC0); jnz("have_arg")
    print_msg("usage")
    b(0xB8); w(0x4C01); b(0xCD, 0x21)
    label("have_arg")
    b(0xBA); patches.append((len(code), "filename", "off16")); w(0)
    b(0xB8); w(0x3D00); b(0xCD, 0x21)
    jb("open_failed")
    b(0xA3); patches.append((len(code), "handle", "off16")); w(0)
    print_msg("playing")
    call("play_file")
    call("speaker_off")
    b(0x8B, 0x1E); patches.append((len(code), "handle", "off16")); w(0)
    b(0xB4, 0x3E, 0xCD, 0x21)
    print_msg("done")
    b(0xB8); w(0x4C00); b(0xCD, 0x21)
    label("open_failed")
    print_msg("open_msg")
    b(0xB8); w(0x4C02); b(0xCD, 0x21)

    label("parse_arg")
    b(0xBE); w(0x0081)        # si = PSP command tail text
    b(0xBF); patches.append((len(code), "filename", "off16")); w(0)
    b(0xB1); b(0x7F)          # cl max scan
    label("skip_spaces")
    b(0xAC, 0x3C, 0x0D); jz("no_arg")
    b(0x3C, 0x20); jnz("copy_arg")
    loop("skip_spaces")
    label("no_arg")
    b(0x30, 0xC0, 0xC3)       # al=0
    label("copy_arg")
    b(0xAA)                   # first char already in al
    label("copy_loop")
    b(0xAC, 0x3C, 0x0D); jz("arg_done")
    b(0x3C, 0x20); jz("arg_done")
    b(0xAA)
    loop("copy_loop")
    label("arg_done")
    b(0x30, 0xC0, 0xAA)       # nul terminate
    b(0xB0, 0x01, 0xC3)

    label("play_file")
    b(0xC6, 0x06); patches.append((len(code), "octave", "off16")); w(0); b(4)
    b(0xC6, 0x06); patches.append((len(code), "deflen", "off16")); w(0); b(8)
    label("next_char")
    call("read_char")
    b(0x84, 0xC0); jz("play_done")
    b(0x3C, ord('a')); jb("upper_ready")
    b(0x3C, ord('z')); ja("upper_ready")
    b(0x2C, 0x20)
    label("upper_ready")
    b(0x3C, ord(' ')); jz("next_char")
    b(0x3C, 0x0D); jz("next_char")
    b(0x3C, 0x0A); jz("next_char")
    b(0x3C, ord('T')); jz("skip_number_cmd")
    b(0x3C, ord('L')); jz("set_length")
    b(0x3C, ord('O')); jz("set_octave")
    b(0x3C, ord('>')); jz("octave_up")
    b(0x3C, ord('<')); jz("octave_down")
    b(0x3C, ord('R')); jz("rest_note")
    b(0x3C, ord('A')); jb("next_char")
    b(0x3C, ord('G')); ja("next_char")
    call("play_note_char")
    jmp("next_char")
    label("skip_number_cmd")
    call("read_number")
    jmp("next_char")
    label("set_length")
    call("read_number")
    b(0x84, 0xC0); jz("next_char")
    b(0xA2); patches.append((len(code), "deflen", "off16")); w(0)
    jmp("next_char")
    label("set_octave")
    call("read_number")
    b(0x84, 0xC0); jz("next_char")
    b(0xA2); patches.append((len(code), "octave", "off16")); w(0)
    jmp("next_char")
    label("octave_up")
    b(0xFE, 0x06); patches.append((len(code), "octave", "off16")); w(0)
    jmp("next_char")
    label("octave_down")
    b(0xFE, 0x0E); patches.append((len(code), "octave", "off16")); w(0)
    jmp("next_char")
    label("rest_note")
    call("read_length_or_default")
    call("delay_len")
    jmp("next_char")
    label("play_done")
    b(0xC3)

    label("play_note_char")
    b(0x2C, ord('A'))         # al = note char index A=0..G=6
    b(0x30, 0xE4)             # ah=0
    b(0xBB); patches.append((len(code), "note_map", "off16")); w(0)
    b(0x01, 0xC3)             # bx += ax
    b(0x8A, 0x07)             # al = semitone
    b(0xA2); patches.append((len(code), "semitone", "off16")); w(0)
    call("peek_char")
    b(0x3C, ord('#')); jz("sharp_note")
    b(0x3C, ord('+')); jz("sharp_note")
    b(0x3C, ord('-')); jz("flat_note")
    jmp("acc_done")
    label("sharp_note")
    call("read_char")
    b(0xFE, 0x06); patches.append((len(code), "semitone", "off16")); w(0)
    jmp("acc_done")
    label("flat_note")
    call("read_char")
    b(0xFE, 0x0E); patches.append((len(code), "semitone", "off16")); w(0)
    label("acc_done")
    call("read_length_or_default")
    b(0x50)                   # save length
    call("note_divisor")
    call("speaker_on_divisor")
    b(0x58)
    call("delay_len")
    call("speaker_off")
    b(0xC3)

    label("note_divisor")
    b(0xA0); patches.append((len(code), "octave", "off16")); w(0)
    b(0x2C, 0x02)             # table starts at octave 2
    b(0xB4, 0x0C)
    b(0xF6, 0xE4)             # ax *= 12
    b(0x8A, 0x1E); patches.append((len(code), "semitone", "off16")); w(0)
    b(0x30, 0xFF)
    b(0x01, 0xD8)
    b(0xD1, 0xE0)             # word offset
    b(0xBB); patches.append((len(code), "div_table", "off16")); w(0)
    b(0x01, 0xC3)
    b(0x8B, 0x07, 0xC3)

    label("read_length_or_default")
    call("read_number")
    b(0x84, 0xC0); jnz("have_len")
    b(0xA0); patches.append((len(code), "deflen", "off16")); w(0)
    label("have_len")
    b(0xC3)

    label("read_number")
    b(0x30, 0xD2)             # dl = value
    label("num_loop")
    call("peek_char")
    b(0x3C, ord('0')); jb("num_done")
    b(0x3C, ord('9')); ja("num_done")
    call("read_char")
    b(0x2C, ord('0'))         # al digit
    b(0x88, 0xC3)             # bl=digit
    b(0x88, 0xD0)             # al=dl
    b(0xB7, 10, 0xF6, 0xE7)   # ax = al * bh
    b(0x00, 0xD8)             # al += bl
    b(0x88, 0xC2)             # dl = al
    jmp("num_loop")
    label("num_done")
    b(0x88, 0xD0, 0xC3)

    label("peek_char")
    b(0x80, 0x3E); patches.append((len(code), "has_peek", "off16")); w(0); b(0)
    jz("peek_read")
    b(0xA0); patches.append((len(code), "peeked", "off16")); w(0)
    b(0xC3)
    label("peek_read")
    call("read_char")
    b(0xA2); patches.append((len(code), "peeked", "off16")); w(0)
    b(0xC6, 0x06); patches.append((len(code), "has_peek", "off16")); w(0); b(1)
    b(0xC3)

    label("read_char")
    b(0x80, 0x3E); patches.append((len(code), "has_peek", "off16")); w(0); b(0)
    jz("read_file")
    b(0xC6, 0x06); patches.append((len(code), "has_peek", "off16")); w(0); b(0)
    b(0xA0); patches.append((len(code), "peeked", "off16")); w(0)
    b(0xC3)
    label("read_file")
    b(0x8B, 0x1E); patches.append((len(code), "handle", "off16")); w(0)
    b(0xBA); patches.append((len(code), "charbuf", "off16")); w(0)
    b(0xB9); w(1)
    b(0xB4, 0x3F, 0xCD, 0x21)
    b(0x83, 0xF8, 0x01); jz("read_ok")
    b(0x30, 0xC0, 0xC3)
    label("read_ok")
    b(0xA0); patches.append((len(code), "charbuf", "off16")); w(0)
    b(0xC3)

    label("delay_len")
    # Use length buckets; exact tempo is deliberately approximate.
    b(0x3C, 0x01); jz("delay_whole")
    b(0x3C, 0x02); jz("delay_half")
    b(0x3C, 0x04); jz("delay_quarter")
    b(0x3C, 0x08); jz("delay_eighth")
    b(0xB9); w(2); jmp("delay_units")
    label("delay_whole")
    b(0xB9); w(32); jmp("delay_units")
    label("delay_half")
    b(0xB9); w(16); jmp("delay_units")
    label("delay_quarter")
    b(0xB9); w(8); jmp("delay_units")
    label("delay_eighth")
    b(0xB9); w(4)
    label("delay_units")
    label("delay_outer")
    b(0xBA); w(0xFFFF)
    label("delay_inner")
    b(0x4A); jnz("delay_inner")
    loop("delay_outer")
    b(0xC3)

    label("speaker_on_divisor")
    b(0x89, 0xC3, 0xB0, 0xB6, 0xE6, 0x43)
    b(0x88, 0xD8, 0xE6, 0x42, 0x88, 0xF8, 0xE6, 0x42)
    b(0xE4, 0x61, 0x0C, 0x03, 0xE6, 0x61, 0xC3)

    label("speaker_off")
    b(0xE4, 0x61, 0x24, 0xFC, 0xE6, 0x61, 0xC3)

    label("title"); code.extend(b"MMLPLAY 0.1\r\n$")
    label("usage"); code.extend(b"Usage: MMLPLAY FILE.MML\r\n$")
    label("playing"); code.extend(b"Playing...\r\n$")
    label("done"); code.extend(b"\r\nDone.\r\n$")
    label("open_msg"); code.extend(b"Cannot open file.\r\n$")
    label("note_map"); code.extend(bytes([9, 11, 0, 2, 4, 5, 7]))  # A B C D E F G
    label("div_table")
    # PIT divisors for C..B, octaves 2..7.
    freqs = [
        65, 69, 73, 78, 82, 87, 92, 98, 104, 110, 117, 123,
        131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247,
        262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494,
        523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988,
        1047, 1109, 1175, 1245, 1319, 1397, 1480, 1568, 1661, 1760, 1865, 1976,
        2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951,
    ]
    for f in freqs:
        w(1193180 // f)
    label("handle"); w(0)
    label("octave"); b(4)
    label("deflen"); b(8)
    label("semitone"); b(0)
    label("has_peek"); b(0)
    label("peeked"); b(0)
    label("charbuf"); b(0)
    label("filename"); code.extend(bytes(64))

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
    print(f"Wrote MMLPLAY.COM ({len(payload)} bytes) to {args.write_com}")


if __name__ == "__main__":
    main()
