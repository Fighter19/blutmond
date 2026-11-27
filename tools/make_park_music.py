#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Writes the park's songs and sounds (Mitsuki's park scene, assets/mitsuki/park):

park_dread.bmsong: the park at night, tense, and more so the nearer the bench (dynamic music,
sound-format.md: the scene's script, park.ms, sets the intensity from the distance). A at 66
beats a minute, 8 bars (29 s) that loop, in four layers that come in one over the other:
  0, always: two low drones a semitone apart (A and B flat), wind swelling and falling;
  1 and up: a slow heartbeat (a low sine thump, lub dub, once a bar) and a music box ringing
    sparse around A and its tritone, far off in the echo;
  2 and up: the heart every two beats, a low saw pulsing on A in eighths;
  3 and up: the heart on every beat, a high tremolo between B flat and A.
At most 7 voices.

park_reunion.bmsong: when Hina is found: warm but with an ache, F major leaning on D minor at
76 beats a minute, 16 bars (50 s) that loop: a music-box melody (a triangle ringing out) over
a left hand breaking the chords in eighths and a soft sine bass, a sine pad entering in the
second half. F C Dm Bb, F Am Bb C, Dm Bb F C, Gm Bb C F. Sines and triangles only, at most 5
voices.

hush.bmsong: the nightmare's hush: a breath of noise swelling and falling, with a low sine
sinking under it (a sound effect).

usage: make_park_music.py <assets/mitsuki/park>
"""
import os
import sys
import xml.etree.ElementTree as ET

BEAT = 24


def write(path, s):
    ET.indent(s, " ")
    ET.ElementTree(s).write(path, encoding="UTF-8", xml_declaration=True)


def tracks(s, instruments, notes):
    for name, _ in instruments:
        tr = ET.SubElement(s, "track", {"name": name, "instrument": name})
        for t, key, length, vel in sorted(notes[name]):
            ET.SubElement(tr, "note", {"t": str(t), "key": str(key), "len": str(length), "vel": str(vel)})


def park_dread():
    bar = 4 * BEAT
    bars = 8
    s = ET.Element("song", {"version": "1", "bpm": "66", "length": str(bars * bar), "loop": "0", "effect": "0",
                            "echo": "220", "echo-feedback": "160", "intensity": "0", "fade": str(2 * 60 * 2)})
    for lid, name, lo, hi in ((0, "always", 0, 15), (1, "near", 1, 15), (2, "nearer", 2, 15), (3, "there", 3, 15)):
        ET.SubElement(s, "layer", {"id": str(lid), "name": name, "from": str(lo), "until": str(hi)})
    instruments = [
        ("drone", {"wave": "triangle", "attack": "120", "decay": "0", "sustain": "230", "release": "90", "volume": "120", "pan": "-12", "layer": "0"}),
        ("drone2", {"wave": "saw", "attack": "150", "decay": "0", "sustain": "200", "release": "90", "volume": "34", "pan": "14", "layer": "0"}),
        ("wind", {"wave": "noise", "attack": "180", "decay": "0", "sustain": "150", "release": "150", "volume": "40", "pan": "-20", "layer": "0"}),
        ("heart", {"wave": "sine", "attack": "0", "decay": "6", "sustain": "0", "release": "2", "volume": "200", "pan": "0", "layer": "1"}),
        ("box", {"wave": "sine", "attack": "0", "decay": "40", "sustain": "0", "release": "20", "volume": "84", "pan": "26", "layer": "1"}),
        ("heart2", {"wave": "sine", "attack": "0", "decay": "6", "sustain": "0", "release": "2", "volume": "190", "pan": "0", "layer": "2"}),
        ("pulse", {"wave": "saw", "attack": "0", "decay": "8", "sustain": "60", "release": "3", "volume": "54", "pan": "-6", "layer": "2"}),
        ("heart3", {"wave": "sine", "attack": "0", "decay": "6", "sustain": "0", "release": "2", "volume": "180", "pan": "0", "layer": "3"}),
        ("trem", {"wave": "pulse25", "attack": "0", "decay": "3", "sustain": "80", "release": "1", "volume": "34", "pan": "20", "layer": "3"}),
    ]
    for name, attrs in instruments:
        ET.SubElement(s, "instrument", dict({"name": name}, **attrs))
    notes = {name: [] for name, _ in instruments}
    for k in range(2):
        t = k * 4 * bar
        notes["drone"].append((t, 33, 4 * bar - 110, 100))
        notes["drone2"].append((t, 34 if k == 0 else 39, 4 * bar - 110, 90))
        notes["wind"].append((t + 24, 46 if k == 0 else 41, 200, 90))
    for b in range(bars):
        t0 = b * bar
        # Lub dub, once a bar (1), every two beats (2), on every beat (3).
        notes["heart"].append((t0, 31, 4, 100))
        notes["heart"].append((t0 + 7, 29, 4, 80))
        notes["heart2"].append((t0 + 2 * BEAT, 31, 4, 100))
        notes["heart2"].append((t0 + 2 * BEAT + 7, 29, 4, 80))
        for beat in (1, 3):
            notes["heart3"].append((t0 + beat * BEAT, 31, 4, 96))
            notes["heart3"].append((t0 + beat * BEAT + 7, 29, 4, 76))
        for e in range(8):
            notes["pulse"].append((t0 + e * 12, 45, 8, 90 if e % 2 == 0 else 70))
        for i in range(16):
            notes["trem"].append((t0 + i * 6, 82 if i % 2 == 0 else 81, 5, 70))
    for b, t, key in ((0, 48, 81), (1, 24, 87), (2, 60, 84), (3, 12, 93), (4, 36, 81), (5, 72, 87), (6, 24, 86), (7, 48, 80)):
        notes["box"].append((b * bar + t, key, 20, 90))
    tracks(s, instruments, notes)
    return s


def park_reunion():
    bar = 4 * BEAT
    s = ET.Element("song", {"version": "1", "bpm": "76", "length": str(16 * bar), "loop": "0", "effect": "0",
                            "echo": "190", "echo-feedback": "90"})
    instruments = [
        ("melody", {"wave": "triangle", "attack": "0", "decay": "44", "sustain": "80", "release": "18", "volume": "130", "pan": "-8"}),
        ("hands", {"wave": "triangle", "attack": "0", "decay": "20", "sustain": "50", "release": "10", "volume": "66", "pan": "10"}),
        ("bass", {"wave": "sine", "attack": "2", "decay": "60", "sustain": "120", "release": "20", "volume": "150", "pan": "0"}),
        ("pad", {"wave": "sine", "attack": "40", "decay": "0", "sustain": "200", "release": "30", "volume": "44", "pan": "-14"}),
    ]
    for name, attrs in instruments:
        ET.SubElement(s, "instrument", dict({"name": name}, **attrs))
    chords = {
        "F": (41, (53, 57, 60)), "C": (48, (48, 52, 55)), "Dm": (50, (50, 53, 57)), "Bb": (46, (46, 50, 53)),
        "Am": (45, (45, 48, 52)), "Gm": (43, (43, 46, 50)),
    }
    progression = ["F", "C", "Dm", "Bb", "F", "Am", "Bb", "C", "Dm", "Bb", "F", "C", "Gm", "Bb", "C", "F"]
    melody = [
        [(0, 72, 36), (36, 74, 12), (48, 76, 24), (72, 77, 24)],
        [(0, 76, 48), (48, 72, 48)],
        [(0, 74, 36), (36, 72, 12), (48, 69, 24), (72, 72, 24)],
        [(0, 70, 72), (72, 69, 24)],
        [(0, 72, 36), (36, 74, 12), (48, 76, 24), (72, 79, 24)],
        [(0, 76, 48), (48, 72, 24), (72, 74, 24)],
        [(0, 77, 36), (36, 76, 12), (48, 74, 24), (72, 70, 24)],
        [(0, 72, 72), (72, 67, 24)],
        [(0, 69, 36), (36, 72, 12), (48, 74, 24), (72, 77, 24)],
        [(0, 74, 48), (48, 70, 48)],
        [(0, 72, 36), (36, 69, 12), (48, 65, 24), (72, 69, 24)],
        [(0, 67, 72), (72, 72, 24)],
        [(0, 70, 36), (36, 69, 12), (48, 67, 24), (72, 70, 24)],
        [(0, 74, 48), (48, 72, 24), (72, 70, 24)],
        [(0, 72, 48), (48, 76, 48)],
        [(0, 77, 72), (72, 72, 24)],
    ]
    notes = {name: [] for name, _ in instruments}
    for b, chord in enumerate(progression):
        t0 = b * bar
        root, (r, third, fifth) = chords[chord]
        notes["bass"].append((t0, root - 12 if root >= 48 else root, bar - 12, 100))
        for i, key in enumerate((r, fifth, r + 12, third + 12, r + 12, fifth, third, fifth)):
            notes["hands"].append((t0 + i * 12, key, 9, 82 if i == 0 else 64))
        for t, key, length in melody[b]:
            notes["melody"].append((t0 + t, key, length - 5, 100 if t == 0 else 86))
        if b >= 8:
            notes["pad"].append((t0, third + 12, bar - 16, 80))
    tracks(s, instruments, notes)
    return s


def hush():
    s = ET.Element("song", {"version": "1", "bpm": "120", "length": str(2 * 4 * BEAT), "loop": "-1", "effect": "1",
                            "echo": "160", "echo-feedback": "60"})
    instruments = [
        ("breath", {"wave": "noise", "attack": "40", "decay": "60", "sustain": "0", "release": "40", "volume": "110", "pan": "0"}),
        ("sink", {"wave": "sine", "attack": "10", "decay": "50", "sustain": "60", "release": "30", "volume": "120", "pan": "0"}),
    ]
    for name, attrs in instruments:
        ET.SubElement(s, "instrument", dict({"name": name}, **attrs))
    notes = {"breath": [(0, 76, 60, 100)], "sink": [(6, 40, 20, 90), (26, 38, 20, 80), (46, 36, 30, 70)]}
    tracks(s, instruments, notes)
    return s


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "mitsuki", "park")
    os.makedirs(out, exist_ok=True)
    for name, song in (("park_dread", park_dread()), ("park_reunion", park_reunion()), ("hush", hush())):
        write(os.path.join(out, name + ".bmsong"), song)
        print("wrote", name)


if __name__ == "__main__":
    main()
