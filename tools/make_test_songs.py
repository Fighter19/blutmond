#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Writes the test level's music and sound effects (assets/test_room/music).

usage: make_test_songs.py <assets/test_room/music>

A four-bar tune in A minor (lead, bass, arpeggio, drums), a jump and a hurt effect, and the
kick drum's sample. Everything is made by this script, so the test assets stay free of
third-party material.
"""
import math
import os
import struct
import sys
import wave

BEAT = 24


def kick(path):
    """A sine that falls from 160 Hz to 45 Hz and fades: 8-bit mono at 9079 Hz."""
    rate, n = 9079, 1600
    data = bytearray()
    phase = 0.0
    for i in range(n):
        t = i / n
        freq = 45 + 115 * (1 - t) ** 3
        phase += 2 * math.pi * freq / rate
        data.append(int(128 + 120 * math.sin(phase) * (1 - t) ** 1.5))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(1)
        w.setframerate(rate)
        w.writeframes(bytes(data))


def song(path, attrs, instruments, tracks):
    out = ['<?xml version="1.0" encoding="UTF-8"?>', "<song version=\"1\" %s>" % attrs]
    for inst in instruments:
        out.append(" <instrument %s/>" % inst)
    for name, instrument, notes in tracks:
        out.append(' <track name="%s" instrument="%s">' % (name, instrument))
        for t, key, length, vel in sorted(notes):
            out.append('  <note t="%d" key="%d" len="%d" vel="%d"/>' % (t, key, length, vel))
        out.append(" </track>")
    out.append("</song>")
    with open(path, "w") as f:
        f.write("\n".join(out) + "\n")


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    kick(os.path.join(out, "kick.wav"))

    # Chords per bar: Am, F, C, G (root keys for the bass, triads for the arpeggio).
    chords = [(45, [57, 60, 64]), (41, [53, 57, 60]), (48, [55, 60, 64]), (43, [55, 59, 62])]
    bass, arp, lead, drums, hats = [], [], [], [], []
    for bar, (root, triad) in enumerate(chords):
        t0 = bar * 4 * BEAT
        for beat in range(4):
            bass.append((t0 + beat * BEAT, root if beat != 2 else root + 12, 20, 110))
            drums.append((t0 + beat * BEAT, 60, 12, 120 if beat % 2 == 0 else 90))
            for half in range(2):
                hats.append((t0 + beat * BEAT + half * 12, 84, 3, 70 if half == 0 else 45))
            for step in range(4):
                arp.append((t0 + beat * BEAT + step * 6, triad[(beat + step) % 3] + 12, 5, 70))
    melody = [(0, 76, 36), (36, 72, 12), (48, 69, 24), (72, 72, 24),
              (96, 77, 36), (132, 76, 12), (144, 72, 48),
              (192, 76, 24), (216, 79, 24), (240, 84, 36), (276, 79, 12),
              (288, 74, 24), (312, 71, 24), (336, 74, 48)]
    for t, key, length in melody:
        lead.append((t, key, length - 2, 105))
    song(os.path.join(out, "castle.bmsong"), 'bpm="112" length="384" loop="0" effect="0" echo="140" echo-feedback="70"', [
        'name="lead" wave="pulse25" attack="1" decay="12" sustain="150" release="10" volume="150" pan="-12"',
        'name="bass" wave="triangle" attack="0" decay="6" sustain="200" release="4" volume="210" pan="0"',
        'name="arp" wave="pulse12" attack="0" decay="8" sustain="60" release="3" volume="80" pan="24"',
        'name="kick" wave="sample" sample="kick.wav" root="60" loop="0" attack="0" decay="0" sustain="255" release="2" volume="230" pan="0"',
        'name="hat" wave="noise" attack="0" decay="3" sustain="0" release="1" volume="70" pan="18"',
    ], [("lead", "lead", lead), ("bass", "bass", bass), ("arp", "arp", arp), ("kick", "kick", drums), ("hat", "hat", hats)])

    song(os.path.join(out, "jump.bmsong"), 'bpm="150" length="24" loop="-1" effect="1" echo="0" echo-feedback="0"', [
        'name="blip" wave="pulse50" attack="0" decay="5" sustain="0" release="2" volume="120" pan="0"',
    ], [("blip", "blip", [(0, 72, 3, 100), (3, 79, 3, 100), (6, 84, 5, 100)])])

    song(os.path.join(out, "hurt.bmsong"), 'bpm="150" length="36" loop="-1" effect="1" echo="0" echo-feedback="0"', [
        'name="buzz" wave="saw" attack="0" decay="10" sustain="0" release="3" volume="170" pan="0"',
        'name="hiss" wave="noise" attack="0" decay="8" sustain="0" release="2" volume="140" pan="0"',
    ], [("buzz", "buzz", [(0, 52, 6, 120), (6, 47, 6, 120), (12, 43, 10, 120)]), ("hiss", "hiss", [(0, 60, 10, 110)])])


if __name__ == "__main__":
    main()
