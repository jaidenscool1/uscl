#!/usr/bin/env python3
"""Create eight original console-inspired menu palettes. No recordings or console sound assets are used.

Transient, harmonic, pitch-envelope and stereo-space profiles distinguish each
theme, with independently chosen notes, timing and a balanced RMS mix.
"""
import math
from pathlib import Path
import random
import struct
import wave

RATE = 48000
OUT = Path(__file__).resolve().parents[1] / "sounds"
# (duration, notes, decay): frequencies intentionally define our own sound palette.
CUES = {
    "cursor": (.24, [2863, 4291, 5737], 62),
    "category": (.38, [1531, 2303, 3821], 31),
    "select": (.58, [1177, 1769, 2351], 18),
    "back": (.26, [941, 627], 28),
    "cancel": (.26, [941, 627], 28),
    "error": (.30, [181, 227], 14),
    "notify": (.65, [813, 1219, 1627], 8),
    "startup": (5.2, [185, 278, 416, 623, 937], .85),
}


# Pitch, duration, decay, transient, harmonic, sweep and stereo-space profiles.
# Independently composed palettes, rather than transcriptions of console cues.
PROFILES = {
    "xmb": (1, 1, 1, .11, 0, 0, 1),
    "ps4": (.74, .72, .85, .035, .12, .009, 1.25),
    "ps5": (.92, .85, .72, .018, .22, .004, 1.6),
    "x360classic": (.34, .82, .65, .19, .42, -.065, .65),
    "x360": (.46, .64, .90, .10, .31, .035, .5),
    "xboxone": (.30, .68, .80, .04, .18, .018, .85),
    "switch": (1.17, .38, 1.65, .25, .08, -.024, .15),
    "switch2": (1.33, .46, 1.35, .16, .30, .021, .3),
}


def render(theme, name, duration, notes, decay):
    pitch, length, damping, noise, harmonic, sweep, space = PROFILES[theme]
    duration *= length
    if name == "startup" and theme != "xmb":
        duration = {"ps4": 2.4, "ps5": 2.8, "x360classic": 1.9,
                    "x360": 1.5, "xboxone": 1.7, "switch": .48, "switch2": .64}[theme]
    notes = [f * pitch for f in notes]
    decay *= damping
    rng = random.Random(732)
    count = int(duration * RATE)
    samples = []
    for i in range(count):
        t = i / RATE
        attack = 1 - math.exp(-t * (2.8 if name == "startup" else 1800))
        release = min(1, (duration - t) / .025)
        value = 0.0
        for n, freq in enumerate(notes):
            onset = n * (.13 if name == "startup" else .018 if name == "notify" else .002)
            u = t - onset
            if u >= 0:
                # Slight pitch settling and inharmonic upper partials soften the click.
                phase = 2 * math.pi * freq * (u + .0007 * (1 - math.exp(-u * 70)))
                phase += 2 * math.pi * freq * sweep * (1 - math.exp(-u * 35)) / 35
                tone = math.sin(phase) + harmonic * math.sin(phase * 2.013)
                if name == "startup":
                    # Slowly detuned partials create an ensemble-like swell without samples.
                    tone = .65 * math.sin(phase + .035 * math.sin(u * 31 + n))
                    tone += .19 * math.sin(phase * 2.002 + n) + .10 * math.sin(phase * 3.007)
                    tone += .06 * math.sin(phase * 4.011 + .02 * math.sin(u * 17))
                value += tone * math.exp(-u * decay) / (n + 1)
        value += (rng.random() * 2 - 1) * noise * math.exp(-t * 350)
        samples.append(value * attack * release)
    dry = samples[:]
    channels = [samples[:], samples[:]]
    taps = ((.037, .12), (.071, .055), (.131, .025))
    if name == "startup":
        taps = ((.113, .22), (.181, .16), (.293, .11), (.431, .07), (.691, .04))
    for channel, output_samples in enumerate(channels):
        for delay, gain in taps:
            offset = int((delay + channel * .007) * space * RATE)
            for i in range(offset, count):
                output_samples[i] += dry[i - offset] * gain * space
    fade_frames = int(.04 * RATE)
    for output_samples in channels:
        for i in range(max(0, count - fade_frames), count):
            output_samples[i] *= (count - i - 1) / fade_frames
    peak = max(abs(x) for channel in channels for x in channel) or 1
    # Match active-window energy, rather than normalizing every cue to the same peak.
    # RMS targets are an original mix, not measured console output levels.
    target_db = {"cursor": -32, "category": -30, "select": -26, "back": -29,
                 "cancel": -29, "error": -25, "notify": -26, "startup": -29}[name]
    window = min(count, int((.8 if name == "startup" else .12) * RATE))
    rms = math.sqrt(sum(x*x for channel in channels for x in channel[:window]) / (2 * window)) or 1
    gain = min(10 ** (target_db / 20) / rms, .28 / peak)
    level = peak * gain
    with wave.open(str(OUT / theme / (name + ".wav")), "wb") as output:
        output.setparams((2, 2, RATE, count, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<hh", round(left / peak * level * 32767), round(right / peak * level * 32767)) for left, right in zip(*channels)))


if __name__ == "__main__":
    for theme in PROFILES:
        (OUT / theme).mkdir(parents=True, exist_ok=True)
        for name, args in CUES.items():
            render(theme, name, *args)
