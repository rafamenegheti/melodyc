"""Levantamento do corpus antes de minerar nada."""

import os
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smf

ROOT = os.path.expanduser("~/Downloads/Axel's Midi Collection")

DRUM_WORDS = ("hihat", "hi-hat", "hat", "kick", "snare", "clap", "perc", "drum",
              "tom", "crash", "ride", "rim", "shaker", "openhat", "808 midi")


def is_drum_path(p):
    low = p.lower()
    return any(w in low for w in DRUM_WORDS)


def main():
    files = []
    for dirpath, _, names in os.walk(ROOT):
        for n in names:
            if n.lower().endswith((".mid", ".midi")):
                files.append(os.path.join(dirpath, n))

    files.sort()
    print(f"arquivos: {len(files)}")

    ok = 0
    fail = Counter()
    drum = 0
    stats = Counter()
    bars_hist = Counter()
    poly_hist = Counter()
    range_hist = Counter()
    tracks_hist = Counter()
    tsig = Counter()
    bpm_hist = Counter()

    for p in files:
        if is_drum_path(os.path.relpath(p, ROOT)):
            drum += 1
            continue
        try:
            m = smf.read(p)
        except Exception as e:
            fail[type(e).__name__ + ": " + str(e)[:40]] += 1
            continue

        if not m.notes:
            fail["sem notas"] += 1
            continue

        ok += 1

        span = max(n.start + n.dur for n in m.notes)
        bars = span / (m.ppq * 4.0)
        bars_hist[min(int(round(bars)), 33)] += 1

        pitches = [n.pitch for n in m.notes]
        rng = max(pitches) - min(pitches)
        range_hist[min(rng // 6 * 6, 48)] += 1

        # polifonia media: quantas notas soam junto no inicio de cada nota
        starts = Counter(n.start for n in m.notes)
        poly = sum(starts.values()) / max(len(starts), 1)
        poly_hist[min(int(round(poly)), 6)] += 1

        tracks_hist[min(m.n_tracks, 8)] += 1

        if m.time_sigs:
            tsig[(m.time_sigs[0][1], m.time_sigs[0][2])] += 1
        else:
            tsig["ausente"] += 1

        b = m.bpm
        if b:
            bpm_hist[int(round(b / 10) * 10)] += 1

        stats["notas"] += len(m.notes)

    print(f"pulados por serem bateria: {drum}")
    print(f"lidos: {ok}   notas totais: {stats['notas']}")
    print(f"falhas: {sum(fail.values())}")
    for k, v in fail.most_common(8):
        print(f"   {v:5d}  {k}")

    def show(title, c, fmt=str):
        print(f"\n-- {title} --")
        for k, v in sorted(c.items(), key=lambda kv: (isinstance(kv[0], str), kv[0]))[:20]:
            print(f"   {fmt(k):>10}  {v:5d}  {'#' * min(v // 20, 60)}")

    show("compassos (4/4)", bars_hist)
    show("polifonia media", poly_hist)
    show("ambito em semitons", range_hist)
    show("numero de tracks", tracks_hist)
    show("formula de compasso", tsig)
    show("bpm", bpm_hist)


main()
