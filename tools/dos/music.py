#!/usr/bin/env python3
"""Build MUSIC.COM with an embedded single-voice Phantom Ensemble demo."""

from __future__ import annotations

import argparse
import math
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MIDI = Path.home() / "Downloads" / "Phantom Ensemble.mid"
DEFAULT_COM = ROOT / "build" / "dos" / "MUSIC.COM"


@dataclass
class Note:
    start: int
    end: int
    note: int
    velocity: int


def read_var(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    while True:
        b = data[pos]
        pos += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, pos


def parse_midi(path: Path) -> tuple[int, list[Note]]:
    data = path.read_bytes()
    pos = 0
    if data[pos:pos + 4] != b"MThd":
        raise ValueError("not a MIDI file")
    header_len = int.from_bytes(data[pos + 4:pos + 8], "big")
    tracks = int.from_bytes(data[pos + 10:pos + 12], "big")
    division = int.from_bytes(data[pos + 12:pos + 14], "big")
    if division & 0x8000:
        raise ValueError("SMPTE MIDI timing is not supported")
    pos += 8 + header_len
    notes: list[Note] = []
    for _ in range(tracks):
        if data[pos:pos + 4] != b"MTrk":
            raise ValueError("missing track chunk")
        length = int.from_bytes(data[pos + 4:pos + 8], "big")
        end = pos + 8 + length
        pos += 8
        tick = 0
        running = None
        active: dict[tuple[int, int], tuple[int, int]] = {}
        while pos < end:
            delta, pos = read_var(data, pos)
            tick += delta
            status = data[pos]
            if status & 0x80:
                pos += 1
                running = status
            elif running is not None:
                status = running
            else:
                raise ValueError("running status without previous event")
            if status == 0xFF:
                pos += 1
                meta_len, pos = read_var(data, pos)
                pos += meta_len
                continue
            if status in (0xF0, 0xF7):
                syx_len, pos = read_var(data, pos)
                pos += syx_len
                continue
            event = status & 0xF0
            channel = status & 0x0F
            if event in (0xC0, 0xD0):
                pos += 1
                continue
            p1 = data[pos]
            p2 = data[pos + 1]
            pos += 2
            if event == 0x90 and p2:
                active[(channel, p1)] = (tick, p2)
            elif event in (0x80, 0x90):
                started = active.pop((channel, p1), None)
                if started and tick > started[0]:
                    notes.append(Note(started[0], tick, p1, started[1]))
        pos = end
    return division, sorted(notes, key=lambda n: (n.start, -n.velocity, -n.note))


def midi_note_to_divisor(note: int) -> int:
    freq = 440.0 * (2.0 ** ((note - 69) / 12.0))
    return max(1, min(0xFFFE, int(round(1193180.0 / freq))))


def build_records(notes: list[Note], ppq: int, max_notes: int) -> list[tuple[int, int]]:
    selected: list[Note] = []
    cursor = 0
    for note in notes:
        if note.note < 36 or note.note > 96:
            continue
        if note.end <= cursor or note.start < cursor:
            continue
        selected.append(note)
        cursor = note.end
        if len(selected) >= max_notes:
            break

    records: list[tuple[int, int]] = []
    unit_ticks = max(1, ppq // 8)
    cursor = 0
    for note in selected:
        if note.start > cursor:
            rest_units = max(1, min(48, round((note.start - cursor) / unit_ticks)))
            records.append((0, rest_units))
        note_units = max(1, min(48, round((note.end - note.start) / unit_ticks)))
        records.append((midi_note_to_divisor(note.note), note_units))
        cursor = note.end
    return records


def emit_com(records: list[tuple[int, int]]) -> bytes:
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

    def print_msg(name: str) -> None:
        b(0xBA); patches.append((len(code), name, "off16")); w(0)
        b(0xB4, 0x09, 0xCD, 0x21)

    b(0x0E, 0x1F)  # push cs; pop ds
    print_msg("title")
    b(0xBE); patches.append((len(code), "notes", "off16")); w(0)
    label("next_note")
    b(0xAD)                         # lodsw, AX=divisor
    b(0x3D); w(0xFFFF); jz("done")
    b(0x89, 0xC3)                   # mov bx,ax
    b(0xAC)                         # lodsb, AL=duration units
    b(0x30, 0xE4)                   # xor ah,ah
    b(0x89, 0xC1)                   # mov cx,ax
    b(0x09, 0xDB); jz("rest")
    call("speaker_on")
    call("delay_units")
    call("speaker_off")
    b(0xB9); w(1); call("delay_units")
    jmp("next_note")
    label("rest")
    call("delay_units")
    jmp("next_note")
    label("done")
    call("speaker_off")
    print_msg("done_msg")
    b(0xB8); w(0x4C00); b(0xCD, 0x21)

    label("speaker_on")
    b(0x50, 0x52)                   # push ax; push dx
    b(0xB0, 0xB6, 0xE6, 0x43)       # PIT ch2 square wave
    b(0x89, 0xD8)                   # mov ax,bx
    b(0xE6, 0x42)                   # low byte
    b(0x88, 0xE0, 0xE6, 0x42)       # high byte
    b(0xE4, 0x61, 0x0C, 0x03, 0xE6, 0x61)
    b(0x5A, 0x58, 0xC3)

    label("speaker_off")
    b(0xE4, 0x61, 0x24, 0xFC, 0xE6, 0x61, 0xC3)

    label("delay_units")
    b(0x50, 0x53, 0x51, 0x52)
    b(0x09, 0xC9); jz("delay_done")
    label("delay_outer")
    b(0xBB); w(2)
    label("delay_mid")
    b(0xBA); w(0xFFFF)
    label("delay_inner")
    b(0x4A); jnz("delay_inner")
    b(0x4B); jnz("delay_mid")
    b(0xE2); patches.append((len(code), "delay_outer", "rel8")); b(0)
    label("delay_done")
    b(0x5A, 0x59, 0x5B, 0x58, 0xC3)

    label("title")
    code.extend(b"\r\nMUSIC.COM - Phantom Ensemble speaker demo\r\nPress Ctrl+C to stop.\r\n$")
    label("done_msg")
    code.extend(b"\r\nDone.\r\n$")
    label("notes")
    for divisor, duration in records:
        w(divisor)
        b(duration & 0xFF)
    w(0xFFFF)
    b(0)

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
    parser.add_argument("--midi", type=Path, default=DEFAULT_MIDI)
    parser.add_argument("--write-com", type=Path, default=DEFAULT_COM)
    parser.add_argument("--max-notes", type=int, default=220)
    args = parser.parse_args()
    ppq, notes = parse_midi(args.midi)
    records = build_records(notes, ppq, args.max_notes)
    payload = emit_com(records)
    args.write_com.parent.mkdir(parents=True, exist_ok=True)
    args.write_com.write_bytes(payload)
    print(f"Wrote MUSIC.COM ({len(payload)} bytes, {len(records)} events) to {args.write_com}")


if __name__ == "__main__":
    main()
