#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Writes two songs (assets/test_room/music/):

night_street.bmsong, the rural street's: horror ambience at 60 beats a minute, 16 bars (64 s)
that loop. Two low drones a semitone apart (A and B flat, later A and E flat, a tritone) beat
against each other; wind swells and falls (noise, slow in and out); a broken music box rings
high and sparse around the tritone, its notes trailing in a long echo; halfway through a
heartbeat starts, quickening in the last bars; now and then something groans (a saw sliding
down in semitones) or scrapes (a thin pulse, two notes a semitone apart). At most 6 voices.

dusk_pulse.bmsong, for a livelier stage (not in the test level's pack): dark wave at 104 beats
a minute in A minor, 16 bars that loop after a four-bar intro.

usage: make_street_music.py <assets/test_room>

  bars 1-4   intro: a slow saw pad and the bass pulsing in eighths, a hat ticking
  bars 5-12  the drums (a four-on-the-floor kick, a noise snare on 2 and 4, hats between) and a
             thin pulse arpeggio in sixteenths, smeared by the echo; Am F C G twice
  bars 13-16 the turn: Am F Dm E, the arpeggio drops out and a sine lead sings over the pad

At most 6 voices sound at once (the pad two, bass, arpeggio or lead, kick, snare or hat), so
two of the eight are left for the game's sounds. Everything here is written by this script.
"""
import os
import sys
import xml.etree.ElementTree as ET

BEAT = 24
BAR = 4 * BEAT
BARS = 16
LOOP_BAR = 4                      # the intro plays once

# A minor: chords as (root key, the two pad notes above it).
A2, F2, C3, G2, D3, E2 = 45, 41, 48, 43, 50, 40
CHORDS = {
    "Am": (A2, (60, 64)),          # c e   over a
    "F": (F2, (60, 65)),           # c f   over f
    "C": (C3, (60, 64)),           # c e   over c
    "G": (G2, (59, 62)),           # b d   over g
    "Dm": (D3, (62, 65)),          # d f   over d
    "E": (E2, (59, 68)),           # b g#  over e: the dominant, pulling home
}
ARP = {                            # chord tones for the arpeggio, an octave up
    "Am": (69, 72, 76, 81), "F": (69, 72, 77, 81), "C": (67, 72, 76, 79),
    "G": (67, 71, 74, 79), "Dm": (69, 74, 77, 81), "E": (68, 71, 76, 80),
}
PROGRESSION = ["Am", "F", "C", "G"] * 3 + ["Am", "F", "Dm", "E"]

# The lead over the turn (bars 13-16): (tick within the four bars, key, length in ticks).
LEAD = [
    (0, 76, 36), (36, 74, 12), (48, 72, 24), (72, 71, 24),
    (96, 72, 48), (144, 69, 48),
    (192, 74, 36), (228, 72, 12), (240, 69, 24), (264, 65, 24),
    (288, 68, 48), (336, 71, 24), (360, 76, 24),
]


def dusk_pulse():
    s = ET.Element("song", {"version": "1", "bpm": "104", "length": str(BARS * BAR), "loop": str(LOOP_BAR * BAR),
                            "effect": "0", "echo": "216", "echo-feedback": "120"})
    instruments = [
        ("pad", {"wave": "saw", "attack": "40", "decay": "30", "sustain": "150", "release": "20", "volume": "62", "pan": "0"}),
        ("bass", {"wave": "saw", "attack": "0", "decay": "8", "sustain": "120", "release": "3", "volume": "170", "pan": "0"}),
        ("arp", {"wave": "pulse12", "attack": "0", "decay": "4", "sustain": "40", "release": "1", "volume": "64", "pan": "30"}),
        ("lead", {"wave": "sine", "attack": "6", "decay": "20", "sustain": "170", "release": "6", "volume": "120", "pan": "-20"}),
        ("kick", {"wave": "sample", "sample": "kick.wav", "root": "60", "loop": "0", "attack": "0", "decay": "0", "sustain": "255",
                  "release": "2", "volume": "200", "pan": "0"}),
        ("snare", {"wave": "noise", "attack": "0", "decay": "9", "sustain": "0", "release": "2", "volume": "90", "pan": "-8"}),
        ("hat", {"wave": "noise", "attack": "0", "decay": "2", "sustain": "0", "release": "1", "volume": "40", "pan": "16"}),
    ]
    for name, attrs in instruments:
        ET.SubElement(s, "instrument", dict({"name": name}, **attrs))

    tracks = {name: [] for name, _ in instruments}
    tracks["pad_hi"] = []
    for bar, chord in enumerate(PROGRESSION):
        t0 = bar * BAR
        root, (lo, hi) = CHORDS[chord]
        # The pad: two notes held through the bar, one panned each way by the instrument (the
        # second voice is the same instrument: both sit in the middle, the echo spreads them).
        # Released early enough to have faded when the next bar's begin (no overlap: two voices).
        tracks["pad"].append((t0, lo, BAR - 16, 70))
        tracks["pad_hi"].append((t0, hi, BAR - 16, 64))
        # The bass in eighths: root, root, octave on the off-beats of 2 and 4.
        for e in range(8):
            key = root + (12 if e in (3, 7) else 0)
            tracks["bass"].append((t0 + e * 12, key, 9, 100 if e % 2 == 0 else 84))
        drums = LOOP_BAR <= bar < 12
        if bar < LOOP_BAR or bar >= 12:
            for b in range(4):                       # the hat alone, on the off-beats
                tracks["hat"].append((t0 + b * BEAT + 12, 96, 4, 70))
        if drums:
            for b in range(4):
                tracks["kick"].append((t0 + b * BEAT, 60, 7, 110))
                tracks["hat"].append((t0 + b * BEAT + 12, 96, 4, 80 if b % 2 else 64))
            for b in (1, 3):
                tracks["snare"].append((t0 + b * BEAT, 60, 6, 100))
            arp = ARP[chord]
            order = (0, 1, 2, 3, 2, 1, 2, 3)
            for i in range(16):                      # sixteenths, up and back
                tracks["arp"].append((t0 + i * 6, arp[order[i % 8]], 3, 90 if i % 4 == 0 else 66))
        if bar >= 12:
            for b in (0, 2):                         # the kick half time under the lead
                tracks["kick"].append((t0 + b * BEAT, 60, 7, 96))
    for t, key, length in LEAD:
        tracks["lead"].append((12 * BAR + t, key, length - 6, 100))   # a breath before the next

    for name, instrument in (("pad", "pad"), ("pad_hi", "pad"), ("bass", "bass"), ("arp", "arp"), ("lead", "lead"),
                             ("kick", "kick"), ("snare", "snare"), ("hat", "hat")):
        tr = ET.SubElement(s, "track", {"name": name, "instrument": instrument})
        for t, key, length, vel in sorted(tracks[name]):
            ET.SubElement(tr, "note", {"t": str(t), "key": str(key), "len": str(length), "vel": str(vel)})
    return s


def night_street():
    """The horror ambience (see the top)."""
    bar = 4 * BEAT                                   # 4 s at 60 beats a minute
    bars = 16
    s = ET.Element("song", {"version": "1", "bpm": "60", "length": str(bars * bar), "loop": "0", "effect": "0",
                            "echo": "225", "echo-feedback": "170"})
    instruments = [
        ("drone", {"wave": "triangle", "attack": "120", "decay": "0", "sustain": "230", "release": "90", "volume": "120", "pan": "-12"}),
        ("drone2", {"wave": "saw", "attack": "150", "decay": "0", "sustain": "200", "release": "90", "volume": "38", "pan": "14"}),
        ("wind", {"wave": "noise", "attack": "180", "decay": "0", "sustain": "150", "release": "150", "volume": "46", "pan": "-20"}),
        ("bell", {"wave": "sine", "attack": "0", "decay": "40", "sustain": "0", "release": "20", "volume": "92", "pan": "26"}),
        ("heart", {"wave": "sample", "sample": "kick.wav", "root": "60", "loop": "0", "attack": "0", "decay": "0", "sustain": "255",
                   "release": "2", "volume": "150", "pan": "0"}),
        ("groan", {"wave": "saw", "attack": "12", "decay": "30", "sustain": "110", "release": "20", "volume": "64", "pan": "-6"}),
        ("scrape", {"wave": "pulse12", "attack": "2", "decay": "10", "sustain": "60", "release": "6", "volume": "40", "pan": "40"}),
    ]
    for name, attrs in instruments:
        ET.SubElement(s, "instrument", dict({"name": name}, **attrs))
    notes = {name: [] for name, _ in instruments}
    # The drones, a section of 4 bars each: the low one stays on A, the other moves: a semitone
    # above, then the tritone, then a semitone below, then a semitone above again.
    for k, (lo, hi) in enumerate(((33, 34), (33, 39), (32, 33), (33, 34))):
        t = k * 4 * bar
        notes["drone"].append((t, lo, 4 * bar - 40, 100))
        notes["drone2"].append((t, hi, 4 * bar - 40, 90))
    # The wind, a swell every 4 bars, each at its own pitch (lower is darker).
    for k, key in enumerate((44, 50, 40, 47)):
        notes["wind"].append((k * 4 * bar + 24, key, 250, 90))
    # The music box: sparse, high, around A and its tritone E flat; never two at once.
    box = [(1, 48, 81), (2, 0, 84), (2, 72, 87), (3, 36, 86),
           (5, 24, 81), (5, 60, 80), (6, 48, 87), (7, 12, 84), (7, 72, 81),
           (9, 0, 93), (9, 48, 87), (10, 24, 86), (11, 60, 80),
           (13, 12, 81), (13, 48, 84), (14, 0, 87), (14, 36, 88), (15, 24, 81)]
    for b, t, key in box:
        notes["bell"].append((b * bar + t, key, 20, 96 if key < 90 else 70))
    # The heartbeat from bar 8 (lub dub), once a bar, then every two beats in the last four.
    for b in range(8, bars):
        for beat in ((0,) if b < 12 else (0, 2)):
            t = b * bar + beat * BEAT
            notes["heart"].append((t, 48, 5, 70 + (b - 8) * 3))
            notes["heart"].append((t + 8, 46, 5, 54 + (b - 8) * 3))
    # Groans: a saw sliding down in semitones.
    for b in (5, 13):
        for i, key in enumerate((41, 40, 39, 38)):
            notes["groan"].append((b * bar + 60 + i * 9, key, 8, 80))
    # Scrapes: two thin notes a semitone apart, quick.
    for b in (3, 10):
        for i, key in enumerate((90, 91, 90, 91)):
            notes["scrape"].append((b * bar + 84 + i * 2, key, 2, 70))
    for name, _ in instruments:
        tr = ET.SubElement(s, "track", {"name": name, "instrument": name})
        for t, key, length, vel in sorted(notes[name]):
            ET.SubElement(tr, "note", {"t": str(t), "key": str(key), "len": str(length), "vel": str(vel)})
    return s


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "test_room")
    for name, s in (("night_street", night_street()), ("dusk_pulse", dusk_pulse())):
        ET.indent(s, " ")
        path = os.path.join(out, "music", name + ".bmsong")
        ET.ElementTree(s).write(path, encoding="UTF-8", xml_declaration=True)
        print("wrote", path)


if __name__ == "__main__":
    main()
