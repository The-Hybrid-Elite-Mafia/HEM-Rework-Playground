#!/usr/bin/env python3
"""
Builds the "HEM Sound Pack" for Band Forge from free, redistributable recordings:

  VS Chamber Orchestra 2: Community Edition (CC0)  github.com/sgossner/VSCO-2-CE
      solo violin (fiddle), violin / viola / cello sections (strings), solo contrabass pizz
      (upright bass), upright piano.  (VSCO's organ is a pipe organ; Band Forge's organ is the
      tonewheel kind gospel and blues use, so it stays modeled.)
  Virtuosity Drums (CC0)                            github.com/sfzinstruments/virtuosity_drums
      jazz kit: kick, snare, cross-stick, hi-hat closed/pedal/open, ride, bell, crash, toms
      (GM 40 is Band Forge's brush stroke, which stays modeled, like clap, tambourine and shaker)

Output (one folder, read by Source/Band/BandSampler):
  pack.json              instruments -> zones (file, root note, key range, velocity range, loop)
  <instrument>/*.flac    48 kHz 24-bit, silence trimmed, sustained notes get a seamless loop
  LICENSES.txt, CREDITS.txt

Every pitched sample's root note is MEASURED (YIN pitch detection), not trusted from the file
name: the name only gives the note letter, the octave comes from the detector, and any sample
that disagrees with its name by more than half a semitone is reported and left out.

Usage: build_sound_pack.py <VSCO-2-CE folder> <virtuosity_drums folder> <output folder>
"""
import json, math, os, re, sys
import numpy as np
import soundfile as sf
from scipy.signal import resample_poly

OUT_RATE = 48000
NOTE = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3, 'E': 4, 'F': 5, 'F#': 6, 'Gb': 6,
        'G': 7, 'G#': 8, 'Ab': 8, 'A': 9, 'A#': 10, 'Bb': 10, 'B': 11}
report = []


def load(path):
    x, sr = sf.read(path, always_2d=True, dtype='float64')
    if x.shape[1] == 1:
        x = np.repeat(x, 2, axis=1)
    x = x[:, :2]
    if sr != OUT_RATE:
        g = math.gcd(sr, OUT_RATE)
        x = resample_poly(x, OUT_RATE // g, sr // g, axis=0)
    return x


def trim(x, max_seconds, thresh_db=-55.0):
    """Cut leading silence (keep 3 ms before the onset) and trailing silence; cap the length."""
    mono = np.abs(x).max(axis=1)
    peak = mono.max() + 1e-12
    on = np.argmax(mono > peak * 10 ** (thresh_db / 20))
    start = max(0, on - int(0.003 * OUT_RATE))
    above = np.nonzero(mono > peak * 10 ** ((thresh_db - 5) / 20))[0]
    end = min(len(x), above[-1] + int(0.05 * OUT_RATE) if len(above) else len(x))
    end = min(end, start + int(max_seconds * OUT_RATE))
    y = x[start:end].copy()
    fade = min(len(y) // 4, int(0.03 * OUT_RATE))
    if fade > 0:
        y[-fade:] *= np.linspace(1, 0, fade)[:, None]
    return y


def yin_f0(x, lo=30.0, hi=2000.0):
    """YIN fundamental (Hz) of the steady part of a note; None if unvoiced."""
    m = x.mean(axis=1)
    n = len(m)
    seg = m[int(min(0.25 * OUT_RATE, n * 0.2)): int(min(1.25 * OUT_RATE, n * 0.8))]
    if len(seg) < 4096:
        seg = m[: min(n, 16384)]
    W = min(len(seg) // 2, 8192)
    if W < 1024:
        return None
    tmax = min(int(OUT_RATE / lo), W - 1)
    tmin = max(2, int(OUT_RATE / hi))
    d = np.zeros(tmax + 1)
    x0 = seg[:W]
    for tau in range(1, tmax + 1):
        diff = x0 - seg[tau: tau + W]
        d[tau] = np.dot(diff, diff)
    cmnd = np.ones_like(d)
    run = np.cumsum(d[1:])
    cmnd[1:] = d[1:] * np.arange(1, tmax + 1) / np.maximum(run, 1e-20)
    cand = np.nonzero(cmnd[tmin:] < 0.15)[0]
    if len(cand) == 0:
        tau = tmin + int(np.argmin(cmnd[tmin:]))
        if cmnd[tau] > 0.4:
            return None
    else:
        tau = tmin + cand[0]
        while tau + 1 <= tmax and cmnd[tau + 1] < cmnd[tau]:
            tau += 1
    if 1 <= tau < tmax:   # parabolic refinement
        a, b, c = cmnd[tau - 1], cmnd[tau], cmnd[tau + 1]
        den = a - 2 * b + c
        tau = tau + 0.5 * (a - c) / den if den != 0 else tau
    return OUT_RATE / tau


def detected_note(x):
    """Detected pitch as a fractional MIDI note, or None if the note has no clear pitch."""
    f0 = yin_f0(x)
    return None if f0 is None else 69 + 12 * math.log2(f0 / 440.0)


def add_loop(x, start_frac=0.35, min_len_s=0.6):
    """Bake a seamless crossfade loop near the end of a sustained sample. Returns (x, loopStart, loopEnd)."""
    n = len(x)
    if n < int(1.5 * OUT_RATE):
        return x, -1, -1
    ls = int(n * start_frac)
    le = int(n * 0.88)
    if le - ls < int(min_len_s * OUT_RATE):
        return x, -1, -1
    xf = int(min(0.12 * OUT_RATE, (le - ls) // 3))
    y = x[:le].copy()
    # blend the audio just before the loop start into the loop's tail so the jump is invisible
    t = np.linspace(0, 1, xf)[:, None]
    fade_in, fade_out = np.sqrt(t), np.sqrt(1 - t)
    y[le - xf: le] = y[le - xf: le] * fade_out + x[ls - xf: ls] * fade_in
    return y, ls, le


def write(path, x):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    peak = np.abs(x).max()
    if peak > 0.999:
        x = x * (0.999 / peak)
    sf.write(path, x.astype(np.float32), OUT_RATE, subtype='PCM_24', format='FLAC')


def pitched(name, files, out, max_s, loop, velmap, gain=1.0, strict=True, retune=True):
    """files: list of (path, midi_note, layer_index, n_layers, rr). Velocity ranges from layers.
    strict: a sample whose detected pitch disagrees with its note by more than half a semitone is left
    out; otherwise (note given by the library's own key chart) the disagreement is only reported.
    retune: store the measured tuning error so the sampler plays the sample in tune (not for piano,
    whose stretched tuning is part of its sound)."""
    zones = []
    offsets = []
    data = []
    loaded = []
    for path, root, layer, nlayers, rr in files:
        x = trim(load(path), max_s)
        loaded.append((path, x, root, detected_note(x), layer, nlayers, rr))
    # Libraries disagree on octave numbering (C3 or C4 = middle C), and even folders within one library do.
    # The octave is taken from what the recordings actually measure: the most common octave difference.
    octs = [round((det - root) / 12) for _, _, root, det, *_ in loaded if det is not None]
    shift = 12 * max(set(octs), key=octs.count) if octs else 0
    if shift:
        report.append(f"{name}: file names are {shift // 12:+d} octave(s) off the measured pitch - corrected")
    for path, x, root, det, layer, nlayers, rr in loaded:
        root += shift
        tune = 0.0
        if det is None:
            report.append(f"{name}: {os.path.basename(path)} has no clear pitch (kept, note {root} from its name)")
        else:
            off = (det - root) - 12 * round((det - root) / 12)   # ignore octave slips onto a harmonic
            if abs(off) > 0.5:
                report.append(f"{name}: {os.path.basename(path)} measures {off:+.2f} semitones from note {root}"
                              + (" - left out" if strict else " (kept, note from the library chart)"))
                if strict:
                    continue
            else:
                offsets.append(off)
                if retune:
                    tune = round(-off * 100, 1)
        data.append((path, x, root, layer, nlayers, rr, tune))
    roots = sorted({d[2] for d in data})
    for i, (path, x, root, layer, nlayers, rr, tune) in enumerate(data):
        ls, le = -1, -1
        if loop:
            x, ls, le = add_loop(x)
        rel = f"{name}/{root:03d}_v{layer}_rr{rr}.flac"
        write(os.path.join(out, rel), x * gain)
        idx = roots.index(root)
        lo = 0 if idx == 0 else (roots[idx - 1] + root) // 2 + 1
        hi = 127 if idx == len(roots) - 1 else (root + roots[idx + 1]) // 2
        vlo, vhi = velmap(layer, nlayers)
        zones.append({"file": rel, "root": root, "lo": lo, "hi": hi, "velLo": vlo, "velHi": vhi,
                      "rr": rr, "tune": tune, "loopStart": ls, "loopEnd": le})
    report.append(f"{name}: {len(zones)} samples, notes {roots[0]}..{roots[-1]}, "
                  f"measured tuning within {max(abs(o) for o in offsets) * 100:.0f} cents of the named notes"
                  + (" (corrected per sample)" if retune else " (kept as recorded)"))
    return zones


def midi(letter, octave):
    return NOTE[letter] + 12 * (int(octave) + 1)


def layers_even(layer, n):
    return round((layer - 1) / n, 3), round(layer / n, 3)


def parse(fname, pattern):
    m = re.search(pattern, fname)
    return m


def build_vsco(src, out):
    inst = {}
    # fiddle: solo violin, sustained vibrato (long notes) + spiccato (short notes)
    d = os.path.join(src, "Strings/Solo Violin/Arco Vib")
    files = []
    for f in sorted(os.listdir(d)):
        m = parse(f, r"_([A-G][#b]?)(-?\d)_([pf])\.wav$")
        if m:
            files.append((os.path.join(d, f), midi(m.group(1), m.group(2)), 1 if m.group(3) == 'p' else 2, 2, 1))
    long_z = pitched("fiddle", files, out, 6.0, True, layers_even)
    d = os.path.join(src, "Strings/Solo Violin/spic")
    files = []
    for f in sorted(os.listdir(d)):
        m = parse(f, r"_([A-G][#b]?)(-?\d)_v(\d)_rr(\d)\.wav$")
        if m:
            files.append((os.path.join(d, f), midi(m.group(1), m.group(2)), int(m.group(3)), 0, int(m.group(4))))
    n = max(f[2] for f in files)
    files = [(p, pc, l, n, rr) for p, pc, l, _, rr in files]
    short_z = pitched("fiddle-short", files, out, 1.2, False, layers_even)
    inst["fiddle"] = {"type": "pitched", "zones": long_z, "short": short_z, "release": 0.18}

    # strings: cello / viola / violin sections, each covering its own range
    sec = []
    for folder, pat, name in [("Strings/Cello Section/susvib", r"susvib_([A-G][#b]?)(-?\d)_v(\d)", "cello"),
                              ("Strings/Viola Section/susvib", r"susvib_([A-G][#b]?)(-?\d)_v(\d)", "viola"),
                              ("Strings/Violin Section/susVib", r"susVib_([A-G][#b]?)(-?\d)_v(\d)", "violins")]:
        d = os.path.join(src, folder)
        files = []
        for f in sorted(os.listdir(d)):
            m = parse(f, pat)
            if m:
                files.append((os.path.join(d, f), midi(m.group(1), m.group(2)), int(m.group(3)), 0, 1))
        layers = sorted({f[2] for f in files})
        files = [(p, pc, layers.index(l) + 1, len(layers), 1) for p, pc, l, _, _ in files]
        sec.append((name, pitched("strings-" + name, files, out, 6.0, True, layers_even)))
    # hand each section its own key range: cello below the viola's lowest note, violins above viola's top
    lo_viola = min(z["root"] for z in sec[1][1])
    lo_violin = min(z["root"] for z in sec[2][1])
    zones = []
    for name, zs in sec:
        lo_cut = {"cello": 0, "viola": lo_viola, "violins": lo_violin + 3}[name]
        hi_cut = {"cello": lo_viola - 1, "viola": lo_violin + 2, "violins": 127}[name]
        for z in zs:
            z = dict(z)
            z["lo"], z["hi"] = max(z["lo"], lo_cut), min(z["hi"], hi_cut)
            if z["lo"] <= z["hi"]:
                zones.append(z)
        # stretch the outermost zones so the whole keyboard is covered
    zones.sort(key=lambda z: z["root"])
    inst["strings"] = {"type": "pitched", "zones": zones, "release": 0.35}

    # upright bass: solo contrabass pizzicato
    d = os.path.join(src, "Strings/Solo Contrabass/Pizz")
    files = []
    for f in sorted(os.listdir(d)):
        m = parse(f, r"_([A-G][#b]?)(-?\d)_v(\d)_rr(\d)\.wav$")
        if m:
            files.append((os.path.join(d, f), midi(m.group(1), m.group(2)), int(m.group(3)), 0, int(m.group(4))))
    layers = sorted({f[2] for f in files})
    files = [(p, pc, layers.index(l) + 1, len(layers), rr) for p, pc, l, _, rr in files]
    inst["uprightBass"] = {"type": "pitched", "zones": pitched("uprightBass", files, out, 3.0, False, layers_even), "release": 0.08}

    # piano: upright, 3 dynamics; MappingChart gives the key number for each file index
    d = os.path.join(src, "Keys/Upright Piano")
    chart = {}
    for line in open(os.path.join(d, "MappingChart.txt"), encoding="utf-8", errors="replace"):
        m = re.match(r"(\d{3})=(\d+)", line.strip())
        if m:
            chart[m.group(1)] = int(m.group(2))
    files = []
    for f in sorted(os.listdir(d)):
        m = parse(f, r"_dyn(\d)_rr1_(\d{3})\.wav$")
        if m and m.group(2) in chart:
            key = chart[m.group(2)]
            files.append((os.path.join(d, f), key, int(m.group(1)), 3, 1))
    inst["piano"] = {"type": "pitched", "zones": pitched("piano", files, out, 7.0, False, layers_even, strict=False, retune=False), "release": 0.25}

    return inst


def build_drums(src, out):
    mid = os.path.join(src, "Samples/mid")
    kick_close = os.path.join(src, "Samples/kickmic")
    snare_close = os.path.join(src, "Samples/snaremic")
    pieces = [  # GM note, folder, articulation, close mic folder, close mic blend
        (36, "kick", "snon", kick_close, 0.8),
        (38, "snare", "center", snare_close, 0.6),
        (37, "snare", "crossstick", snare_close, 0.5),
        (42, "hh", "closed", None, 0), (44, "hh", "pedal", None, 0), (46, "hh", "open", None, 0),
        (51, "ride", "ride", None, 0), (53, "ride", "bell", None, 0),
        (49, "crash", "crash", None, 0), (57, "flatride", "crash", None, 0),
        (45, "ltom", "center", None, 0), (41, "ltom", "center", None, 0),
        (50, "htom", "center", None, 0), (48, "htom", "center", None, 0), (47, "htom", "center", None, 0),
    ]
    zones = []
    for note, folder, art, close, blend in pieces:
        d = os.path.join(mid, folder)
        hits = []
        for f in sorted(os.listdir(d)):
            m = re.match(rf"mid_{folder}_{art}_vl(\d+)(?:_rr(\d+))?\.flac$", f)
            if not m:
                continue
            x = load(os.path.join(d, f))
            if close:
                cf = os.path.join(close, folder, f.replace("mid_", os.path.basename(close) + "_"))
                if os.path.exists(cf):
                    c = load(cf)
                    n = min(len(x), len(c))
                    x = x[:n] + blend * c[:n]
            x = trim(x, 3.5 if folder in ("crash", "ride", "flatride") or art == "open" else 1.6)
            loud = float(np.sqrt(np.mean(x[: int(0.05 * OUT_RATE)] ** 2)))
            hits.append((loud, x, f))
        if not hits:
            report.append(f"drums: no samples for {folder}/{art}")
            continue
        hits.sort(key=lambda h: h[0])
        # split the loudness-sorted hits into layers of up to 4 round-robins each
        per = 4 if len(hits) >= 8 else max(1, len(hits) // 2)
        layers = [hits[i:i + per] for i in range(0, len(hits), per)]
        for li, layer in enumerate(layers):
            vlo, vhi = round(li / len(layers), 3), round((li + 1) / len(layers), 3)
            for rr, (loud, x, f) in enumerate(layer, 1):
                rel = f"drums/{note:03d}_v{li + 1}_rr{rr}.flac"
                if not os.path.exists(os.path.join(out, rel)):
                    write(os.path.join(out, rel), x)
                zones.append({"file": rel, "note": note, "velLo": vlo, "velHi": vhi, "rr": rr})
        report.append(f"drums: note {note} ({folder} {art}) {len(hits)} hits in {len(layers)} layers")
    return {"type": "drums", "zones": zones}


def main():
    vsco, vd, out = sys.argv[1:4]
    os.makedirs(out, exist_ok=True)
    inst = build_vsco(vsco, out)
    inst["drums"] = build_drums(vd, out)
    pack = {"name": "HEM Sound Pack", "version": 1, "sampleRate": OUT_RATE, "instruments": inst}
    with open(os.path.join(out, "pack.json"), "w", encoding="utf-8") as f:
        json.dump(pack, f, indent=1)
    with open(os.path.join(out, "CREDITS.txt"), "w", encoding="utf-8") as c:
        c.write("HEM Sound Pack - recordings used by Band Forge in HEM Rework Playground\n\n"
                "VS Chamber Orchestra 2: Community Edition - recorded by Sam Gossner & Simon Dalzell,\n"
                "sample cutting by Elan Hickler/Soundemote. Versilian Studios LLC. CC0 1.0.\n"
                "Upright piano sampled by Simon Dalzell of Ivy Audio.\n"
                "https://github.com/sgossner/VSCO-2-CE\n\n"
                "Virtuosity Drums - performed by Austin McMahon at Virtuosity Musical Instruments, Boston.\n"
                "CC0 1.0. https://github.com/sfzinstruments/virtuosity_drums\n\n"
                "Samples were trimmed, resampled to 48 kHz and given crossfade loops for Band Forge.\n")
    with open(os.path.join(out, "LICENSES.txt"), "w", encoding="utf-8") as l:
        for name, p in (("VS Chamber Orchestra 2: Community Edition", os.path.join(vsco, "LICENSE")),
                        ("Virtuosity Drums", os.path.join(vd, "LICENSE"))):
            l.write(f"===== {name} =====\n" + open(p, encoding="utf-8", errors="replace").read() + "\n\n")
    with open(os.path.join(out, "BUILD-REPORT.txt"), "w", encoding="utf-8") as r:
        r.write("\n".join(report) + "\n")
    print("\n".join(report))


if __name__ == "__main__":
    main()
