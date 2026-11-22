#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Writes the test level's music and sound effects (assets/test_room/music).

usage: make_test_songs.py <assets/test_room/music>

A four-bar tune in A minor (lead, bass, arpeggio, drums), a jump and a hurt effect, and the
kick drum's sample. The moon bat's song (vesper.bmsong) is dynamic music in D phrygian: four
layers that come in as the fight grows (instrument attribute `layer`, the script's
music_intensity): 0 a drone under the dialogue, 1 bass and a heartbeat of kicks, 2 the lead
and hats, 3 a frantic diminished arpeggio. The speakers' voices are short blips
(voice_hero, voice_vesper). Everything is made by this script, so the test assets stay free of
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
    vesper(out)
    voices(out)

    # A teleport (the wave, effects.h): a shimmer rising and falling over a second.
    shimmer = [(i * 3, 72 + (i * 5) % 24 if i < 10 else 96 - (i * 5) % 24, 4, 70) for i in range(20)]
    song(os.path.join(out, "teleport.bmsong"), 'bpm="120" length="64" loop="-1" effect="1" echo="0" echo-feedback="0"', [
        'name="glass" wave="sine" attack="0" decay="6" sustain="0" release="2" volume="110" pan="0"',
        'name="hum" wave="triangle" attack="10" decay="20" sustain="0" release="6" volume="90" pan="0"',
    ], [("glass", "glass", shimmer), ("hum", "hum", [(0, 48, 56, 90)])])


def vesper(out):
    """The moon bat's song: 8 bars at 92 bpm, D phrygian, in four layers."""
    roots = [38, 38, 39, 38, 34, 36, 39, 33]   # D D Eb D Bb C Eb A (bass octave)
    drone, choir, bass, kicks, lead, hats, arp = [], [], [], [], [], [], []
    for bar, root in enumerate(roots):
        t0 = bar * 4 * BEAT
        drone.append((t0, root + 12, 4 * BEAT - 8, 90))
        if bar % 2 == 0:   # every second bar, held for two: fewer voices at the bar lines
            choir.append((t0, root + 31, 8 * BEAT - 8, 60))
        # Eighths with the flat second leaning in, the threat of the mode.
        for i, step in enumerate([0, 0, 12, 0, 0, 1, 0, 12]):
            bass.append((t0 + i * 12, root + step, 9, 105 if i % 2 == 0 else 85))
        # A heartbeat: two beats close together, twice a bar.
        for beat in (0, 2):
            kicks.append((t0 + beat * BEAT, 60, 6, 120))
            kicks.append((t0 + beat * BEAT + 8, 60, 6, 85))
        for i in range(8):
            if i % 2 == 1:
                hats.append((t0 + i * 12, 84, 3, 55))
        # A diminished seventh above the root, sixteenths, rising and falling.
        chord = [root + 24 + k for k in (0, 3, 6, 9)]
        for i in range(16):
            arp.append((t0 + i * 6, (chord + chord[2:0:-1])[i % 6], 4, 62 if i % 4 == 0 else 48))
    melody = [(0, 69, 36), (36, 70, 12), (48, 69, 48),
              (96, 67, 24), (120, 65, 24), (144, 63, 48),
              (192, 67, 36), (228, 65, 12), (240, 63, 24), (264, 62, 24),
              (288, 62, 84),
              (384, 65, 24), (408, 70, 24), (432, 74, 48),
              (480, 72, 24), (504, 70, 24), (528, 69, 48),
              (576, 70, 24), (600, 69, 24), (624, 67, 24), (648, 63, 24),
              (672, 62, 90)]
    for t, key, length in melody:
        lead.append((t, key, length - 2, 110))
    song(os.path.join(out, "vesper.bmsong"), 'bpm="92" length="768" loop="0" effect="0" echo="180" echo-feedback="90"', [
        'name="drone" wave="triangle" attack="40" decay="0" sustain="255" release="10" volume="150" pan="0"',
        'name="choir" wave="sine" attack="60" decay="0" sustain="255" release="10" volume="70" pan="-20"',
        'name="bass" wave="saw" attack="0" decay="8" sustain="120" release="2" volume="150" pan="0" layer="1"',
        'name="kick" wave="sample" sample="kick.wav" root="60" loop="0" attack="0" decay="0" sustain="255" release="2" volume="230" pan="0" layer="1"',
        'name="lead" wave="pulse25" attack="2" decay="20" sustain="170" release="6" volume="140" pan="-10" layer="2"',
        'name="hat" wave="noise" attack="0" decay="3" sustain="0" release="1" volume="60" pan="16" layer="2"',
        'name="arp" wave="pulse12" attack="0" decay="5" sustain="40" release="2" volume="75" pan="26" layer="3"',
    ], [("drone", "drone", drone), ("choir", "choir", choir), ("bass", "bass", bass), ("kick", "kick", kicks),
        ("lead", "lead", lead), ("hat", "hat", hats), ("arp", "arp", arp)])


def voices(out):
    """A speaker's voice: a blip heard with every second letter of a line (pitched by the letter)."""
    song(os.path.join(out, "voice_hero.bmsong"), 'bpm="400" length="6" loop="-1" effect="1" echo="0" echo-feedback="0"', [
        'name="blip" wave="pulse25" attack="0" decay="3" sustain="0" release="1" volume="90" pan="-8"',
    ], [("blip", "blip", [(0, 74, 5, 80)])])
    song(os.path.join(out, "voice_vesper.bmsong"), 'bpm="400" length="10" loop="-1" effect="1" echo="0" echo-feedback="0"', [
        'name="rasp" wave="saw" attack="0" decay="5" sustain="0" release="2" volume="110" pan="8"',
        'name="hiss" wave="pulse12" attack="0" decay="4" sustain="0" release="1" volume="45" pan="8"',
    ], [("rasp", "rasp", [(0, 47, 8, 100)]), ("hiss", "hiss", [(0, 54, 6, 70)])])


if __name__ == "__main__":
    main()
