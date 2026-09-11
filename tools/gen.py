"""O gerador. Escolhe um trecho real do banco e o transpoe -- nada alem disso.

A transposicao e uma SOMA EM SEMITONS, e por isso a frase soa exatamente como
soava no arquivo de onde saiu. As tres camadas andam juntas com o mesmo
deslocamento, entao o espacamento entre baixo, acorde e melodia tambem e o do
original.
"""

import json
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import midiout

BANK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "bank.json")

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

# Onde cada camada deve morar. A mediana da melodia e puxada para ca, e o mesmo
# deslocamento vai para acorde e baixo -- mexer em cada uma por conta propria
# desmontaria o espacamento que veio do arquivo.
MELODY_CENTER = 74          # o meio da melodia mora aqui

# Faixa tocavel. 33-96 era estreito demais e forcava a escolha errada: trecho
# com lead agudo e sub embaixo abre cinco oitavas de verdade, e nao cabia em
# oitava nenhuma -- o custo entao preferia subir a melodia inteira para o teto.
LOW, HIGH = 28, 100         # mi1 a mi7
MEL_LOW, MEL_HIGH = 55, 92  # onde uma melodia ainda soa como melodia


def load_bank(path=BANK):
    with open(path) as f:
        return json.load(f)


def score(seg):
    """Quanto o trecho merece ser sorteado. Repeticao interna e o que o ouvido
    agarra; ter as tres camadas vale porque ja vieram combinando."""
    s = 1.0
    s += 2.0 * seg["rep"]
    s += 0.5 if seg["chords"] else 0.0
    s += 0.3 if seg["bass"] else 0.0
    s += 0.4 * min(seg["conf"], 1.0)
    n = len(seg["melody"])
    if n < 6 or n > 40:
        s *= 0.5
    return s


def pick(bank, mode, rng):
    pool = [s for s in bank if s["mode"] == mode]
    weights = [score(s) for s in pool]
    return rng.choices(pool, weights=weights, k=1)[0]


def choose_base(seg, root, octave_shift=0):
    """A oitava em que o bloco inteiro vai morar.

    Centrar so pela melodia nao basta: dois while empurrando um contra o outro
    ficavam presos quando o trecho tinha melodia aguda E sub, e o sub saia na
    nota 28. Aqui as oitavas candidatas sao pontuadas e ganha a menos pior --
    nota fora da faixa tocavel pesa muito mais que melodia fora do centro."""
    mel = [rel for _, rel, _, _ in seg["melody"]]
    allr = (mel + [rel for _, _, rr in seg["chords"] for rel in rr]
                + [rel for _, rel, _ in seg["bass"]])
    if not allr:
        return root + 12 * 5

    # `rel` ja e altura absoluta menos a tonica: carrega a oitava do arquivo.
    # Somar 60 de novo empurrava tudo uma oitava acima da voz humana.
    mid = (min(mel) + max(mel)) / 2.0
    k0 = int(round((MELODY_CENTER - root - mid) / 12.0))

    best, best_cost = None, None

    for k in range(k0 - 3, k0 + 4):
        base = root + 12 * (k + octave_shift)
        out = sum(1 for r in allr if base + r < LOW or base + r > HIGH)
        stray = sum(1 for r in mel if base + r < MEL_LOW or base + r > MEL_HIGH)
        cost = 10.0 * out + 3.0 * stray + abs(base + mid - MELODY_CENTER)
        if best_cost is None or cost < best_cost:
            best, best_cost = base, cost

    return best


def render(seg, root=9, octave_shift=0):
    """Transpoe o trecho para o tom pedido e devolve as tres camadas."""
    base = choose_base(seg, root, octave_shift)

    melody = [(p, base + rel, d, v) for p, rel, d, v in seg["melody"]]
    chords = [(p, base + rel, d, 84) for p, d, rels in seg["chords"] for rel in rels]
    bass = [(p, base + rel, d, 96) for p, rel, d in seg["bass"]]

    return melody, chords, bass


def main():
    rng = random.Random(int(sys.argv[1]) if len(sys.argv) > 1 else 7)
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "/tmp/melody-demo"
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 8

    bank = load_bank()
    os.makedirs(out_dir, exist_ok=True)

    print(f"banco: {len(bank)} trechos\n")

    for i in range(n):
        mode = "minor" if rng.random() < 0.7 else "major"
        root = rng.randrange(12)
        seg = pick(bank, mode, rng)
        melody, chords, bass = render(seg, root)

        tracks = [("melodia", melody)]
        if chords:
            tracks.append(("acordes", chords))
        if bass:
            tracks.append(("baixo", bass))

        bpm = seg["bpm"] or 140
        name = f"{i+1:02d}_{NOTE_NAMES[root]}{'m' if mode=='minor' else ''}_{bpm}bpm.mid"
        midiout.write(os.path.join(out_dir, name), tracks, bpm=bpm)

        print(f"{name:28s} rep {seg['rep']:.2f}  pausa {seg['rest']:.2f}  "
              f"{len(melody):2d} notas  {len(seg['chords'])} acordes  "
              f"{len(bass):2d} baixo   <- {seg['src'][:52]}")

    print(f"\n{n} arquivos em {out_dir}")


if __name__ == "__main__":
    main()
