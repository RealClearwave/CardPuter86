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


@dataclass
class TempoEvent:
    tick: int
    us_per_quarter: int


def read_var(data: bytes, pos: int) -> tuple[int, int]:
    value = 0
    while True:
        b = data[pos]
        pos += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, pos


def normalize_tempos(tempos: list[TempoEvent]) -> list[TempoEvent]:
    if not tempos:
        return [TempoEvent(0, 500000)]
    result: list[TempoEvent] = []
    for tempo in sorted(tempos, key=lambda item: item.tick):
        if result and result[-1].tick == tempo.tick:
            result[-1] = tempo
        else:
            result.append(tempo)
    if result[0].tick > 0:
        result.insert(0, TempoEvent(0, 500000))
    return result


def parse_midi(path: Path) -> tuple[int, list[TempoEvent], list[Note]]:
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
    tempos: list[TempoEvent] = []
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
                meta_type = data[pos]
                pos += 1
                meta_len, pos = read_var(data, pos)
                payload = data[pos:pos + meta_len]
                pos += meta_len
                if meta_type == 0x51 and meta_len == 3:
                    tempos.append(TempoEvent(tick, int.from_bytes(payload, "big")))
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
    return division, normalize_tempos(tempos), sorted(notes, key=lambda n: (n.start, -n.velocity, -n.note))


def midi_note_to_divisor(note: int) -> int:
    freq = 440.0 * (2.0 ** ((note - 69) / 12.0))
    return max(1, min(0xFFFE, int(round(1193180.0 / freq))))


def midi_span_seconds(start_tick: int, end_tick: int, ppq: int,
                      tempos: list[TempoEvent]) -> float:
    if end_tick <= start_tick:
        return 0.0
    tempo_index = 0
    while tempo_index + 1 < len(tempos) and tempos[tempo_index + 1].tick <= start_tick:
        tempo_index += 1

    seconds = 0.0
    cursor = start_tick
    while cursor < end_tick:
        tempo = tempos[tempo_index]
        next_tempo_tick = tempos[tempo_index + 1].tick if tempo_index + 1 < len(tempos) else end_tick
        segment_end = min(end_tick, next_tempo_tick)
        if segment_end > cursor:
            seconds += (segment_end - cursor) * tempo.us_per_quarter / (ppq * 1000000.0)
        cursor = segment_end
        if tempo_index + 1 < len(tempos) and cursor >= tempos[tempo_index + 1].tick:
            tempo_index += 1
    return seconds


def seconds_to_bios_ticks(seconds: float) -> int:
    # DOS BIOS timer ticks at about 18.2065 Hz. Rounding to this clock keeps the
    # music independent from the emulator's effective CPU speed.
    return max(1, min(255, int(round(seconds * 18.2065))))


def midi_ticks_to_bios_ticks(start_tick: int, end_tick: int, ppq: int,
                             tempos: list[TempoEvent]) -> int:
    return seconds_to_bios_ticks(midi_span_seconds(start_tick, end_tick, ppq, tempos))


def build_records(notes: list[Note], ppq: int, tempos: list[TempoEvent],
                  max_notes: int) -> list[tuple[int, int]]:
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
    cursor = selected[0].start if selected else 0
    for note in selected:
        if note.start > cursor:
            rest_units = midi_ticks_to_bios_ticks(
                cursor, note.start, ppq, tempos)
            records.append((0, rest_units))
        note_units = midi_ticks_to_bios_ticks(
            note.start, note.end, ppq, tempos)
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
    b(0x50, 0x53, 0x51, 0x52, 0x56) # save ax,bx,cx,dx,si
    b(0x09, 0xC9); jz("delay_done")
    b(0x89, 0xCE)                   # mov si,cx (requested BIOS ticks)
    b(0xB4, 0x00, 0xCD, 0x1A)       # BIOS get timer ticks, CX:DX
    b(0x89, 0xD3)                   # mov bx,dx (start low word)
    label("delay_wait")
    b(0xB4, 0x00, 0xCD, 0x1A)
    b(0x89, 0xD0)                   # mov ax,dx
    b(0x29, 0xD8)                   # sub ax,bx
    b(0x39, 0xF0)                   # cmp ax,si
    b(0x72); patches.append((len(code), "delay_wait", "rel8")); b(0)
    label("delay_done")
    b(0x5E, 0x5A, 0x59, 0x5B, 0x58, 0xC3)

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
    ppq, tempos, notes = parse_midi(args.midi)
    records = build_records(notes, ppq, tempos, args.max_notes)
    payload = emit_com(records)
    args.write_com.parent.mkdir(parents=True, exist_ok=True)
    args.write_com.write_bytes(payload)
    print(f"Wrote MUSIC.COM ({len(payload)} bytes, {len(records)} events) to {args.write_com}")


if __name__ == "__main__":
    main()
