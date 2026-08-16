#!/usr/bin/env python3
"""Synthesise the sweep chime from scratch.

Writes mod/Sound/FX/ScavengerSense/sweep.wav: a short, quiet swell of
filtered noise under a soft two-note chime (A4 + E5). Everything here is
computed - no samples, no recordings - so the file carries no licence but
ours, and regenerating it is deterministic: same script, same bytes.
"""
import math
import os
import struct
import wave

RATE = 44100
SECONDS = 1.1
# Near full scale. Loudness belongs to the file: the engine's volume control
# only attenuates, so a quiet master cannot be turned up in game. The chime
# stays gentle by its shape, not by a low peak.
PEAK = 0.85

samples = [0.0] * int(RATE * SECONDS)


# --- airy swell: low-passed noise with a rise-and-fall envelope -----------
# Deterministic LCG rather than random.random(), so the wav never changes
# between runs (numerical recipes constants).
state = 20260816
lp = 0.0
for i in range(len(samples)):
    state = (state * 1664525 + 1013904223) & 0xFFFFFFFF
    noise = (state / 0x7FFFFFFF) - 1.0
    # one-pole low-pass around ~900 Hz keeps it a breath, not a hiss
    lp += 0.12 * (noise - lp)

    t = i / RATE
    if t < 0.40:
        env = (t / 0.40) ** 2
    else:
        env = math.exp(-(t - 0.40) / 0.28)
    samples[i] += 0.55 * lp * env

# --- chime: A4 and E5, soft attack, long ring -----------------------------
for freq, gain, start in ((440.0, 0.50, 0.12), (659.25, 0.35, 0.18)):
    for i in range(int(start * RATE), len(samples)):
        t = i / RATE - start
        attack = min(t / 0.03, 1.0)
        ring = math.exp(-t / 0.33)
        samples[i] += gain * attack * ring * math.sin(2.0 * math.pi * freq * t)

# --- final fade and normalise --------------------------------------------
fade = int(0.15 * RATE)
for i in range(fade):
    samples[-1 - i] *= i / fade

scale = PEAK / max(abs(s) for s in samples)
frames = b"".join(
    struct.pack("<h", int(max(-1.0, min(1.0, s * scale)) * 32767)) for s in samples
)

out = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "mod", "Sound", "FX", "ScavengerSense", "sweep.wav",
)
os.makedirs(os.path.dirname(out), exist_ok=True)
with wave.open(out, "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(frames)

print(f"wrote {os.path.normpath(out)}: {len(samples)} samples, "
      f"{SECONDS:.2f}s, peak {PEAK}")
