#!/usr/bin/env python3
"""Convert a MIDI file to a compact single-voice MML file for MMLPLAY.COM."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path


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


def parse_midi(path: Path) -> tuple[int, int, list[Note]]:
    data = path.read_bytes()
    pos = 0
    if data[pos:pos + 4] != b"MThd":
        raise ValueError("not a MIDI file")
    header_len = int.from_bytes(data[pos + 4:pos + 8], "big")
    fmt = int.from_bytes(data[pos + 8:pos + 10], "big")
    tracks = int.from_bytes(data[pos + 10:pos + 12], "big")
    division = int.from_bytes(data[pos + 12:pos + 14], "big")
    if division & 0x8000:
        raise ValueError("SMPTE time division is not supported")
    pos += 8 + header_len
    notes: list[Note] = []
    tempo = 500000
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
                raise ValueError("running status without previous status")
            if status == 0xFF:
                meta_type = data[pos]
                pos += 1
                meta_len, pos = read_var(data, pos)
                payload = data[pos:pos + meta_len]
                pos += meta_len
                if meta_type == 0x51 and meta_len == 3:
                    tempo = int.from_bytes(payload, "big")
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
    return division, round(60000000 / tempo), sorted(notes, key=lambda n: (n.start, -n.velocity, -n.note))


NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def quantize_length(ticks: int, ppq: int) -> int:
    candidates = [1, 2, 4, 8, 16, 32]
    return min(candidates, key=lambda l: abs(ticks - (ppq * 4 // l)))


def midi_to_mml(notes: list[Note], ppq: int, tempo: int, max_notes: int) -> str:
    # Keep one melody note at a time: highest velocity, then highest pitch when
    # multiple notes start together. This is deliberate for a monophonic speaker.
    selected: list[Note] = []
    cursor = 0
    for note in notes:
        if note.end <= cursor:
            continue
        if note.start < cursor:
            continue
        selected.append(note)
        cursor = note.end
        if len(selected) >= max_notes:
            break

    tokens = [f"T{tempo}", "O4", "L8"]
    current_octave = 4
    cursor = 0
    for note in selected:
        if note.start > cursor:
            rest = note.start - cursor
            tokens.append("R" + str(quantize_length(rest, ppq)))
        octave = note.note // 12 - 1
        while current_octave < octave:
            tokens.append(">")
            current_octave += 1
        while current_octave > octave:
            tokens.append("<")
            current_octave -= 1
        tokens.append(NAMES[note.note % 12] + str(quantize_length(note.end - note.start, ppq)))
        cursor = note.end

    lines: list[str] = []
    line = ""
    for token in tokens:
        if len(line) + len(token) + 1 > 72:
            lines.append(line.rstrip())
            line = ""
        line += token + " "
    if line:
        lines.append(line.rstrip())
    return "\r\n".join(lines) + "\r\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--max-notes", type=int, default=220)
    args = parser.parse_args()
    ppq, tempo, notes = parse_midi(args.input)
    args.output.write_text(midi_to_mml(notes, ppq, tempo, args.max_notes), encoding="ascii", newline="")
    print(f"Wrote {args.output} from {len(notes)} MIDI notes")


if __name__ == "__main__":
    main()
