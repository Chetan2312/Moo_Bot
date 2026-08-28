#!/usr/bin/env python3
"""Render the synthesized creature sounds to WAV, on the laptop.

A mirror of firmware/components/sfx/sfx.c. Same maths, same constants, so
you can tune a moo by ear in a couple of seconds instead of a four-minute
reflash. Change a number here, listen, and once it sounds right copy the
value into sfx.c.

    python3 tools/sfx_preview.py            # writes ./sfx_preview/*.wav
    python3 tools/sfx_preview.py --play     # ...and plays them if aplay exists

Output is 16 kHz mono s16le, exactly what the device generates. It will
sound cleaner than the hardware: the board renders this through an 8-bit
PWM-DAC into a small mylar speaker, so expect less low end and some hiss.
"""

import argparse
import math
import os
import struct
import subprocess
import sys
import wave

RATE = 16000

# ---- moo shape: keep these in step with sfx.c -------------------------------
MOO_F_START = 190.0
MOO_F_PEAK = 255.0
MOO_F_END = 150.0
MOO_VIB_HZ = 5.5
MOO_VIB_DEPTH = 0.030
MOO_FORMANT = 720.0
MOO_FORMANT_BW = 420.0
MOO_HARMONICS = 9

DURATIONS_MS = {
    "moo": 900,
    "moo_short": 450,
    "munch": 560,
    "yawn": 800,
    "giggle": 560,
}

_seed = 0x13579BDF


def rnd():
    global _seed
    _seed = (_seed * 1664525 + 1013904223) & 0xFFFFFFFF
    return _seed


def noise():
    return (rnd() >> 8) / 8388608.0 - 1.0


def envelope(t, attack, release):
    if attack > 0.0 and t < attack:
        return t / attack
    if release > 0.0 and t > 1.0 - release:
        return (1.0 - t) / release
    return 1.0


def clamp16(v):
    return max(-32768, min(32767, int(v)))


def gen_moo(n, f_peak, dur_s):
    out = []
    phase = 0.0
    for i in range(n):
        t = i / n
        if t < 0.15:
            f0 = MOO_F_START + (f_peak - MOO_F_START) * (t / 0.15)
        elif t < 0.55:
            f0 = f_peak
        else:
            f0 = f_peak + (MOO_F_END - f_peak) * ((t - 0.55) / 0.45)
        f0 *= 1.0 + MOO_VIB_DEPTH * math.sin(2 * math.pi * MOO_VIB_HZ * t * dur_s)

        phase += 2 * math.pi * f0 / RATE
        if phase > 2 * math.pi:
            phase -= 2 * math.pi

        s = norm = 0.0
        for h in range(1, MOO_HARMONICS + 1):
            d = (f0 * h - MOO_FORMANT) / MOO_FORMANT_BW
            a = (1.0 / h) * (1.0 + 2.5 * math.exp(-d * d))
            s += a * math.sin(phase * h)
            norm += a
        s /= norm if norm > 0 else 1.0

        out.append(clamp16(s * envelope(t, 0.10, 0.28) * 0.92 * 32767.0))
    return out


def gen_munch(n):
    out = []
    span = n // 4
    lp = phase = 0.0
    for i in range(n):
        ct = (i % span) / span
        env = envelope(ct / 0.55, 0.15, 0.55) if ct < 0.55 else 0.0
        lp += 0.22 * (noise() - lp)
        phase += 2 * math.pi * 95.0 / RATE
        thump = math.sin(phase) * 0.45
        body = envelope(i / n, 0.02, 0.10)
        out.append(clamp16((lp * 1.5 + thump) * env * body * 0.75 * 32767.0))
    return out


def gen_yawn(n):
    out = []
    phase = lp = 0.0
    for i in range(n):
        t = i / n
        f0 = 170.0 + 120.0 * (t / 0.55) if t < 0.55 else 290.0 - 140.0 * ((t - 0.55) / 0.45)
        phase += 2 * math.pi * f0 / RATE
        if phase > 2 * math.pi:
            phase -= 2 * math.pi
        tone = sum(math.sin(phase * h) / h for h in range(1, 5)) / 2.08
        lp += 0.10 * (noise() - lp)
        out.append(clamp16((tone * 0.6 + lp * 2.2) * envelope(t, 0.30, 0.35) * 0.8 * 32767.0))
    return out


def gen_giggle(n):
    out = []
    span = n // 4
    phase = 0.0
    for i in range(n):
        b, k = i // span, i % span
        bt = k / span
        f0 = 520.0 + 110.0 * b
        phase += 2 * math.pi * f0 / RATE
        if phase > 2 * math.pi:
            phase -= 2 * math.pi
        env = envelope(bt / 0.45, 0.20, 0.50) if bt < 0.45 else 0.0
        s = math.sin(phase) + 0.35 * math.sin(phase * 2.0)
        out.append(clamp16(s * env * 0.62 * 32767.0))
    return out


def normalize(buf, target=0.94):
    """Match sfx.c: scale to a fixed peak so all five are equally loud and
    the 8-bit DAC gets the full range."""
    peak = max(abs(x) for x in buf)
    if peak <= 0:
        return buf
    g = target * 32767.0 / peak
    return [clamp16(x * g) for x in buf]


def build(name):
    n = RATE * DURATIONS_MS[name] // 1000
    dur_s = DURATIONS_MS[name] / 1000.0
    if name == "moo":
        raw = gen_moo(n, MOO_F_PEAK, dur_s)
    elif name == "moo_short":
        raw = gen_moo(n, MOO_F_PEAK + 45.0, dur_s)
    elif name == "munch":
        raw = gen_munch(n)
    elif name == "yawn":
        raw = gen_yawn(n)
    elif name == "giggle":
        raw = gen_giggle(n)
    else:
        raise ValueError(name)
    return normalize(raw)


def write_wav(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(struct.pack("<%dh" % len(samples), *samples))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="sfx_preview")
    ap.add_argument("--play", action="store_true", help="play each via aplay")
    ap.add_argument("names", nargs="*", default=sorted(DURATIONS_MS))
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    for name in args.names:
        s = build(name)
        path = os.path.join(args.out, name + ".wav")
        write_wav(path, s)
        peak = max(abs(x) for x in s)
        print("%-10s %5d ms  peak %5d  %s" % (name, DURATIONS_MS[name], peak, path))
        if args.play:
            try:
                subprocess.run(["aplay", "-q", path], check=False)
            except FileNotFoundError:
                print("  (aplay not found — open the wav yourself)", file=sys.stderr)
                break


if __name__ == "__main__":
    main()
