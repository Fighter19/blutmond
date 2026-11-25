#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Writes the test level's spare songs and the rural street's (assets/test_room/music/):

night_street.bmsong, the rural street's: dread with a pulse, after how players describe "The
Voice Someone Calls" (Persona 3): short and creepy, light piano chimes over a lot of
ambience and long silences, a constant slow beat of the bass drum under it all. E phrygian at
84 beats a minute, 24 bars (69 s) that loop: a soft kick on every beat that never changes, a
sine bass on the half notes leaning onto the flat second (F against E), a low triangle pad
swelling slowly, and a small piano call of three notes that comes back, varied, with whole bars
of silence between, ringing out in a long echo. Sines and triangles (no noise, no saw), at
most 6 voices.

test_chamber.bmsong, for a laboratory (not in the test level's pack): cold electronics after the
character of the Portal (2007) soundtrack, B minor at 120 beats a minute, 24 bars (48 s) that
loop in three parts. The chamber: a slow pad, sine bleeps arpeggiating sparsely in the echo,
small clicks at odd moments, no beat. The escape: a steady kick and a bass in eighths, a sine
lead lamenting over them. The fire pit: the bass tightens to sixteenths rocking between B and C,
the pad holds C against it, the bleeps climb in semitones. The clicks are thin pulse blips, not
noise. At most 6 voices.

quiet_sorrow.bmsong, for an emotional scene (not in the test level's pack): a melancholic
piece in D minor at 70 beats a minute,
16 bars (55 s) that loop. A piano-like melody (a triangle that decays to a soft hold) sings
over a left hand breaking each chord in eighths (root, fifth, octave, tenth), a sine bass on
the root, and in the second half a soft sine pad on the chord's third, all in a gentle echo:
Dm Bb Gm A twice, then F C Dm Bb, Gm Dm Bb A. Sines and triangles only (no saw, no noise),
at most 6 voices.

hollow_dread.bmsong, for an empty stage of high suspense (not in the test level's pack):
horror ambience at 60 beats a minute, 16 bars (64 s) that loop. Two low drones a semitone apart (A and B flat, later A and E flat, a tritone) beat
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


def hollow_dread():
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


def quiet_sorrow():
    """The melancholic piece (see the top)."""
    bar = 4 * BEAT                                   # 3.4 s at 70 beats a minute
    s = ET.Element("song", {"version": "1", "bpm": "70", "length": str(16 * bar), "loop": "0", "effect": "0",
                            "echo": "200", "echo-feedback": "100"})
    instruments = [
        ("melody", {"wave": "triangle", "attack": "0", "decay": "50", "sustain": "90", "release": "18", "volume": "130", "pan": "-8"}),
        ("hands", {"wave": "triangle", "attack": "0", "decay": "20", "sustain": "50", "release": "10", "volume": "72", "pan": "10"}),
        ("bass", {"wave": "sine", "attack": "2", "decay": "60", "sustain": "120", "release": "20", "volume": "150", "pan": "0"}),
        ("pad", {"wave": "sine", "attack": "40", "decay": "0", "sustain": "200", "release": "30", "volume": "46", "pan": "-14"}),
    ]
    for name, attrs in instruments:
        ET.SubElement(s, "instrument", dict({"name": name}, **attrs))
    # Chords: (bass root, three chord tones for the left hand: root, third, fifth).
    chords = {
        "Dm": (50, (50, 53, 57)), "Bb": (46, (46, 50, 53)), "Gm": (43, (43, 46, 50)), "A": (45, (45, 49, 52)),
        "F": (41, (41, 45, 48)), "C": (48, (48, 52, 55)),
    }
    progression = ["Dm", "Bb", "Gm", "A"] * 2 + ["F", "C", "Dm", "Bb", "Gm", "Dm", "Bb", "A"]
    # The melody, bar by bar: (tick in the bar, key, length).
    melody = [
        [(0, 69, 36), (36, 74, 12), (48, 72, 24), (72, 70, 24)],
        [(0, 69, 48), (48, 65, 24), (72, 67, 24)],
        [(0, 70, 36), (36, 69, 12), (48, 67, 24), (72, 65, 24)],
        [(0, 64, 48), (48, 61, 48)],
        [(0, 69, 36), (36, 74, 12), (48, 77, 36), (84, 76, 12)],
        [(0, 74, 48), (48, 70, 24), (72, 72, 24)],
        [(0, 74, 24), (24, 72, 24), (48, 70, 24), (72, 67, 24)],
        [(0, 69, 72), (72, 73, 24)],
        [(0, 72, 36), (36, 69, 12), (48, 72, 24), (72, 77, 24)],
        [(0, 76, 48), (48, 72, 48)],
        [(0, 74, 36), (36, 72, 12), (48, 74, 24), (72, 77, 24)],
        [(0, 74, 72), (72, 70, 24)],
        [(0, 70, 36), (36, 72, 12), (48, 74, 24), (72, 70, 24)],
        [(0, 69, 48), (48, 65, 24), (72, 69, 24)],
        [(0, 65, 48), (48, 62, 48)],
        [(0, 64, 72), (72, 61, 24)],
    ]
    notes = {name: [] for name, _ in instruments}
    for b, chord in enumerate(progression):
        t0 = b * bar
        root, (r, third, fifth) = chords[chord]
        notes["bass"].append((t0, root - 12 if root >= 48 else root, bar - 12, 100))
        # The left hand: root, fifth, octave, tenth, octave, fifth, third, fifth (in eighths).
        for i, key in enumerate((r, fifth, r + 12, third + 12, r + 12, fifth, third, fifth)):
            notes["hands"].append((t0 + i * 12, key, 9, 84 if i == 0 else 66))
        for t, key, length in melody[b]:
            notes["melody"].append((t0 + t, key, length - 5, 100 if t == 0 else 88))
        if b >= 8:
            notes["pad"].append((t0, third + 24, bar - 16, 80))
    for name, _ in instruments:
        tr = ET.SubElement(s, "track", {"name": name, "instrument": name})
        for t, key, length, vel in sorted(notes[name]):
            ET.SubElement(tr, "note", {"t": str(t), "key": str(key), "len": str(length), "vel": str(vel)})
    return s


def night_street():
    """Dread with a pulse (see the top)."""
    bar = 4 * BEAT                                   # 2.9 s at 84 beats a minute
    bars = 24
    s = ET.Element("song", {"version": "1", "bpm": "84", "length": str(bars * bar), "loop": "0", "effect": "0",
                            "echo": "225", "echo-feedback": "150"})
    instruments = [
        ("kick", {"wave": "sample", "sample": "kick.wav", "root": "60", "loop": "0", "attack": "0", "decay": "0", "sustain": "255",
                  "release": "2", "volume": "130", "pan": "0"}),
        ("bass", {"wave": "sine", "attack": "3", "decay": "40", "sustain": "110", "release": "16", "volume": "150", "pan": "0"}),
        ("pad", {"wave": "triangle", "attack": "90", "decay": "0", "sustain": "180", "release": "40", "volume": "54", "pan": "-14"}),
        ("chime", {"wave": "triangle", "attack": "0", "decay": "24", "sustain": "0", "release": "10", "volume": "110", "pan": "18"}),
    ]
    for name, attrs in instruments:
        ET.SubElement(s, "instrument", dict({"name": name}, **attrs))
    notes = {name: [] for name, _ in instruments}
    E1, F1 = 40, 41                                  # E2, F2: the sine's low register
    # Per bar: the bass root (E, or F leaning on it) and the pad's note (low, held).
    plan = [(E1, 55), (E1, 55), (F1, 57), (E1, 55),  (E1, 59), (E1, 59), (F1, 60), (E1, 59),
            (E1, 55), (E1, 55), (F1, 57), (F1, 56),  (E1, 52), (E1, 52), (F1, 53), (E1, 52),
            (E1, 55), (E1, 55), (F1, 57), (E1, 55),  (E1, 59), (F1, 60), (F1, 57), (E1, 55)]
    # The piano's call: three notes, and where it comes (bar, tick, which variant); bars
    # without it are silence for the chime.
    calls = {0: (83, 79, 81), 1: (83, 79, 77), 2: (88, 84, 83), 3: (76, 77, 79)}
    when = [(1, 24, 0), (3, 48, 1), (6, 12, 0), (7, 60, 3), (9, 24, 2), (11, 48, 1), (14, 36, 0), (15, 72, 3),
            (17, 24, 0), (19, 48, 2), (21, 12, 1), (22, 60, 3)]
    for b in range(bars):
        t0 = b * bar
        root, pad = plan[b]
        for beat in range(4):                        # the pulse, never changing
            notes["kick"].append((t0 + beat * BEAT, 48, 6, 76 if beat == 0 else 64))
        for half in (0, 2):
            notes["bass"].append((t0 + half * BEAT, root, 2 * BEAT - 10, 100 if half == 0 else 84))
        if b % 2 == 0:                               # the pad, two bars a note
            notes["pad"].append((t0, pad, 2 * bar - 24, 80))
    for b, t, v in when:
        for i, key in enumerate(calls[v]):
            notes["chime"].append((b * bar + t + i * 12, key, 8, 96 - i * 10))
    for name, _ in instruments:
        tr = ET.SubElement(s, "track", {"name": name, "instrument": name})
        for t, key, length, vel in sorted(notes[name]):
            ET.SubElement(tr, "note", {"t": str(t), "key": str(key), "len": str(length), "vel": str(vel)})
    return s


def test_chamber():
    """Cold electronics in three parts (see the top)."""
    bar = 4 * BEAT                                   # 2 s at 120 beats a minute
    s = ET.Element("song", {"version": "1", "bpm": "120", "length": str(24 * bar), "loop": "0", "effect": "0",
                            "echo": "180", "echo-feedback": "120"})
    instruments = [
        ("pad", {"wave": "triangle", "attack": "60", "decay": "0", "sustain": "160", "release": "30", "volume": "60", "pan": "-16"}),
        ("bleep", {"wave": "sine", "attack": "0", "decay": "7", "sustain": "0", "release": "3", "volume": "82", "pan": "22"}),
        ("click", {"wave": "pulse12", "attack": "0", "decay": "1", "sustain": "0", "release": "1", "volume": "34", "pan": "-30"}),
        ("kick", {"wave": "sample", "sample": "kick.wav", "root": "60", "loop": "0", "attack": "0", "decay": "0", "sustain": "255",
                  "release": "2", "volume": "150", "pan": "0"}),
        ("bass", {"wave": "triangle", "attack": "0", "decay": "16", "sustain": "130", "release": "4", "volume": "150", "pan": "0"}),
        ("lead", {"wave": "sine", "attack": "4", "decay": "30", "sustain": "150", "release": "10", "volume": "110", "pan": "-6"}),
    ]
    for name, attrs in instruments:
        ET.SubElement(s, "instrument", dict({"name": name}, **attrs))
    notes = {name: [] for name, _ in instruments}
    # B minor: (bass root, pad note, the bleeps' four notes) per chord, two bars each.
    chords = {
        "Bm": (47, 66, (71, 74, 78, 83)), "G": (43, 67, (71, 74, 79, 83)), "Em": (40, 67, (71, 76, 79, 83)),
        "F#": (42, 70, (70, 73, 78, 82)), "C": (48, 72, (72, 76, 79, 84)),
    }
    plan = ["Bm", "G", "Em", "F#"] * 3                # 12 chords of two bars
    rnd = __import__("random").Random(31)
    for c, chord in enumerate(plan):
        t0 = c * 2 * bar
        part = c // 4                                 # 0 chamber, 1 escape, 2 fire pit
        root, pad, arp = chords[chord]
        if part == 2:                                 # the fire pit: C held against the B below
            pad = 72 if chord in ("Bm", "F#") else pad
        notes["pad"].append((t0, pad, 2 * bar - 28, 110 if part == 2 else 80))   # faded before the next one
        for b in range(2):
            tb = t0 + b * bar
            if part == 0:
                notes["bass"].append((tb, root, bar - 8, 90))
                for i in range(16):                   # sparse bleeps: some sixteenths only
                    if (i * 7 + c * 3 + b) % 5 in (0, 3):
                        notes["bleep"].append((tb + i * 6, arp[i % 4], 3, 90 if i % 4 == 0 else 70))
            elif part == 1:
                for e in range(8):
                    notes["bass"].append((tb + e * 12, root + (12 if e % 4 == 3 else 0), 8, 100 if e % 2 == 0 else 80))
                for k in range(4):
                    notes["kick"].append((tb + k * BEAT, 60, 7, 104))
                for i in (2, 6, 11, 14):
                    notes["bleep"].append((tb + i * 6, arp[(i + c) % 4], 3, 70))
            else:
                for x in range(16):                   # sixteenths rocking between the root and a semitone up
                    notes["bass"].append((tb + x * 6, root + (1 if x % 4 == 3 else 0), 2, 124 if x % 4 == 0 else 108))
                for k in (0, 1, 2, 3):
                    notes["kick"].append((tb + k * BEAT, 60, 7, 124 if k % 2 == 0 else 110))
                for i in range(8):                    # climbing in semitones, two bars a run
                    notes["bleep"].append((tb + i * 12, 78 + (b * 8 + i) // 2, 3, 96 + i * 3))
            for _ in range(3 if part == 0 else 2):    # clicks at odd moments
                t = tb + rnd.randrange(0, 16) * 6 + rnd.choice((0, 2, 3))
                notes["click"].append((t, 100 + rnd.randrange(8), 1, 80))
    # The lament over the escape (bars 8-15): (bar, tick, key, length).
    lament = [(8, 0, 78, 36), (8, 36, 76, 12), (8, 48, 74, 48), (9, 0, 73, 72), (9, 72, 71, 24),
              (10, 0, 74, 36), (10, 36, 76, 12), (10, 48, 79, 48), (11, 0, 78, 96),
              (12, 0, 76, 36), (12, 36, 74, 12), (12, 48, 71, 48), (13, 0, 73, 48), (13, 48, 74, 48),
              (14, 0, 70, 72), (14, 72, 73, 24), (15, 0, 71, 90)]
    for b, t, key, length in lament:
        notes["lead"].append((b * bar + t, key, length - 10, 96))   # its release ends before the next
    for name, _ in instruments:
        tr = ET.SubElement(s, "track", {"name": name, "instrument": name})
        for t, key, length, vel in sorted(notes[name]):
            ET.SubElement(tr, "note", {"t": str(t), "key": str(key), "len": str(length), "vel": str(vel)})
    return s


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "test_room")
    for name, s in (("night_street", night_street()), ("test_chamber", test_chamber()), ("quiet_sorrow", quiet_sorrow()),
                    ("hollow_dread", hollow_dread()), ("dusk_pulse", dusk_pulse())):
        ET.indent(s, " ")
        path = os.path.join(out, "music", name + ".bmsong")
        ET.ElementTree(s).write(path, encoding="UTF-8", xml_declaration=True)
        print("wrote", path)


if __name__ == "__main__":
    main()
