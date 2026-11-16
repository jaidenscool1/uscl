#!/usr/bin/env python3
"""Create original airy menu cues. No recordings or console sound assets are used.

Short filtered transients, soft sine partials and sparse echoes evoke the quiet
feel of an early console crossbar, with independently chosen notes and timing.
"""
import math
from pathlib import Path
import random
import struct
import wave

RATE = 48000
OUT = Path(__file__).resolve().parents[1] / "sounds" / "xmb"
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


def render(name, duration, notes, decay):
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
                tone = math.sin(phase)
                if name == "startup":
                    # Slowly detuned partials create an ensemble-like swell without samples.
                    tone = .65 * math.sin(phase + .035 * math.sin(u * 31 + n))
                    tone += .19 * math.sin(phase * 2.002 + n) + .10 * math.sin(phase * 3.007)
                    tone += .06 * math.sin(phase * 4.011 + .02 * math.sin(u * 17))
                value += tone * math.exp(-u * decay) / (n + 1)
        value += (rng.random() * 2 - 1) * .11 * math.exp(-t * 350)
        samples.append(value * attack * release)
    dry = samples[:]
    channels = [samples[:], samples[:]]
    taps = ((.037, .12), (.071, .055), (.131, .025))
    if name == "startup":
        taps = ((.113, .22), (.181, .16), (.293, .11), (.431, .07), (.691, .04))
    for channel, output_samples in enumerate(channels):
        for delay, gain in taps:
            offset = int((delay + channel * .007) * RATE)
            for i in range(offset, count):
                output_samples[i] += dry[i - offset] * gain
    fade_frames = int(.04 * RATE)
    for output_samples in channels:
        for i in range(max(0, count - fade_frames), count):
            output_samples[i] *= (count - i - 1) / fade_frames
    peak = max(abs(x) for channel in channels for x in channel) or 1
    level = .20 if name == "startup" else .15 if name in ("cursor", "category") else .23
    with wave.open(str(OUT / (name + ".wav")), "wb") as output:
        output.setparams((2, 2, RATE, count, "NONE", "not compressed"))
        output.writeframes(b"".join(struct.pack("<hh", round(left / peak * level * 32767), round(right / peak * level * 32767)) for left, right in zip(*channels)))


if __name__ == "__main__":
    OUT.mkdir(parents=True, exist_ok=True)
    for name, args in CUES.items():
        render(name, *args)
