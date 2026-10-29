"""CI only: writes four short made-up stems (24-bit / 48 kHz WAV) so the UI screenshots show tracks,
waveforms and meters. Pure standard library; nothing of Steven's audio is used anywhere in CI."""
import math
import random
import struct
import sys
import wave
from pathlib import Path

RATE = 48000
SECONDS = 16
BPM = 96.0


def write(path, left, right):
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(3)
        w.setframerate(RATE)
        frames = bytearray()
        for l, r in zip(left, right):
            for v in (l, r):
                i = max(-8388607, min(8388607, int(round(v * 8388607))))
                frames += struct.pack("<i", i)[0:3]
        w.writeframes(bytes(frames))


def main(folder):
    out = Path(folder)
    out.mkdir(parents=True, exist_ok=True)
    n = RATE * SECONDS
    beat = 60.0 / BPM
    rng = random.Random(7)

    # drums: kick on 1 and 3, hats on eighths
    l = [0.0] * n
    for k in range(int(SECONDS / beat) + 1):
        t0 = int(k * beat * RATE)
        if k % 2 == 0:
            for i in range(min(9000, n - t0)):
                t = i / RATE
                l[t0 + i] += 0.8 * math.sin(2 * math.pi * (50 + 90 * math.exp(-t * 30)) * t) * math.exp(-t * 9)
        for h in (0, 0.5):
            s = int((k + h) * beat * RATE)
            for i in range(min(2500, max(0, n - s))):
                l[s + i] += 0.18 * (rng.random() * 2 - 1) * math.exp(-i / 400.0)
    write(out / "01 Drums.wav", l, l)

    # bass: root notes, one per bar
    roots = [43, 43, 46, 41]
    b = [0.0] * n
    for i in range(n):
        t = i / RATE
        bar = int(t / (4 * beat)) % len(roots)
        f = 440.0 * 2 ** ((roots[bar] - 69) / 12)
        ph = (t % beat) / beat
        b[i] = 0.5 * math.sin(2 * math.pi * f * t) * (0.6 + 0.4 * math.exp(-ph * 4))
    write(out / "02 Bass.wav", b, b)

    # keys: three-note chords, a little wider on the right
    chords = [(55, 59, 62), (55, 59, 62), (58, 62, 65), (53, 57, 60)]
    kl, kr = [0.0] * n, [0.0] * n
    for i in range(n):
        t = i / RATE
        c = chords[int(t / (4 * beat)) % len(chords)]
        v = sum(math.sin(2 * math.pi * 440.0 * 2 ** ((m - 69) / 12) * t) for m in c) / 6.0
        kl[i] = v
        kr[i] = v * 0.8 + 0.05 * math.sin(2 * math.pi * 3.0 * t) * v
    write(out / "03 Keys.wav", kl, kr)

    # vox: a soft saw melody with vibrato
    melody = [67, 69, 71, 69, 67, 64, 62, 64]
    v = [0.0] * n
    phase = 0.0
    for i in range(n):
        t = i / RATE
        m = melody[int(t / (2 * beat)) % len(melody)]
        f = 440.0 * 2 ** ((m - 69) / 12) * (1 + 0.004 * math.sin(2 * math.pi * 5.5 * t))
        phase = (phase + f / RATE) % 1.0
        v[i] = 0.25 * (2 * phase - 1) * (0.5 + 0.5 * math.sin(math.pi * ((t / (2 * beat)) % 1.0)))
    write(out / "04 Vox.wav", v, v)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "demo-stems")
