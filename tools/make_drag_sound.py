#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause OR MIT
"""Makes the push block's drag sound (assets/test_room/music/drag.wav).

usage: make_drag_sound.py <assets/test_room/music>

Stone dragged over stone: a dark, rough noise, played as a looping sample (drag.bmsong) for as
long as a block slides (src/world/puzzle.c). Made in the frequency domain, so it loops without
a seam: random phases over a spectrum that falls off towards the treble (gone above about
2 kHz) and has no rumble below 70 Hz, held at one level (noise this short swells and dips by chance, and the loop would repeat that
as a flutter).
2048 samples at the engine's 18157 Hz, 16-bit mono (the cooker
makes 8 bits of it): 2 KB in the pack, which the multiboot image has room for. drag.bmsong
plays it 5 semitones low, so the loop lasts 0.15 seconds and the noise is darker still. Its
peaks stay well below full scale: the instrument's volume decides how loud it is.
"""
import os
import sys
import wave

import numpy as np

RATE = 18157
N = 2048


def drag(seed=7):
    rng = np.random.default_rng(seed)
    freqs = np.fft.rfftfreq(N, 1.0 / RATE)
    mag = np.zeros_like(freqs)
    band = (freqs >= 70) & (freqs <= 2200)
    mag[band] = (freqs[band] / 70.0) ** -0.7
    mag *= np.clip((2200 - freqs) / 800.0, 0, 1)      # a soft treble edge
    spectrum = mag * np.exp(2j * np.pi * rng.random(len(freqs)))
    x = np.fft.irfft(spectrum, N)
    # Even loudness across the loop: noise this short swells and dips by chance, and the loop
    # would repeat that 7 times a second. Divided by its own loudness (smoothed over 120 samples,
    # around the loop), it holds one level.
    k = 120
    power = np.convolve(np.concatenate([x[-k:], x, x[:k]]) ** 2, np.ones(k) / k, mode="same")[k:k + N]
    x /= np.sqrt(power)
    x /= np.max(np.abs(x))
    return (x * 0.6 * 32767).astype(np.int16)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "assets/test_room/music"
    with wave.open(os.path.join(out, "drag.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(drag().tobytes())


if __name__ == "__main__":
    main()
