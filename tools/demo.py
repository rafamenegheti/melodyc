"""Gera um lote para ouvir: metade trecho inteiro, metade recombinado."""

import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import combine
import gen
import midiout
import preview

NOTE = gen.NOTE_NAMES


def make(bank, rng, recombine):
    mode = "minor" if rng.random() < 0.72 else "major"
    a = gen.pick(bank, mode, rng)

    if not recombine:
        return a, "inteiro"

    pool = [s for s in bank if s["mode"] == mode and s["chords"]]
    for _ in range(40):
        b = rng.choice(pool)
        if b is a:
            continue
        c = combine.combine(a, b)
        if c:
            return c, f"recomb {c['fit']:.2f}"
    return a, "inteiro"


def main():
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 11
    out = sys.argv[2] if len(sys.argv) > 2 else "/tmp/melody-demo"
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 12

    rng = random.Random(seed)
    bank = gen.load_bank()
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(out):
        os.remove(os.path.join(out, f))

    chunks = []
    print(f"banco: {len(bank)} trechos\n")
    print(f"{'arquivo':<26} {'origem':<13} {'compassos':>4}  notas  fonte")

    for i in range(n):
        seg, kind = make(bank, rng, recombine=(i % 2 == 1))
        root = rng.randrange(12)
        mel, ch, ba = gen.render(seg, root)

        tracks = [("mel", mel)]
        if ch:
            tracks.append(("harm", ch))
        if ba:
            tracks.append(("baixo", ba))

        bpm = seg["bpm"] or 140
        bpm = min(max(bpm, 120), 170)
        name = f"{i+1:02d}_{NOTE[root]}{'m' if seg['mode']=='minor' else ''}_{bpm}"

        midiout.write(os.path.join(out, name + ".mid"),
                      [(t[0], t[1]) for t in tracks], bpm=bpm)

        audio = preview.render(tracks, bpm, bars=8)
        preview.write_wav(os.path.join(out, name + ".wav"), audio)
        chunks.append(audio)

        src = seg["src"]
        src = src if len(src) <= 60 else src[:57] + "..."
        print(f"{name:<26} {kind:<13} {len(mel):5d}  {len(ch):3d}h {len(ba):3d}b  {src}")

    preview.write_wav(os.path.join(out, "00_tudo.wav"), preview.concat(chunks))
    print(f"\n{n} exemplos + 00_tudo.wav em {out}")


if __name__ == "__main__":
    main()
