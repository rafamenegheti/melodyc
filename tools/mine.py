"""Minerador do corpus: 4 compassos inteiros, com as camadas que vieram juntas.

A diferenca em relacao a tentativa anterior nao esta na quantidade -- esta no
que se guarda.

1. GUARDA SEMITONS RELATIVOS A TONICA, nao graus de escala. Transpor vira uma
   soma, e a frase soa EXATAMENTE como soava no arquivo original. Mapear para
   grau reescreve os intervalos: uma terca menor vira maior conforme a escala
   destino, e e assim que material real vira material generico. Por isso o banco
   e separado em maior e menor -- cada um so recebe transposicao.

2. GUARDA AS CAMADAS QUE VIERAM DO MESMO ARQUIVO. Melodia sorteada de um lado e
   harmonia de outro nao tem por que combinar. Um trecho inteiro combina porque
   alguem escreveu ele assim.

3. NAO FILTRA POR REPETICAO INTERNA -- PONTUA. O filtro anterior deixou 24
   frases no banco, e 24 frases repetem. A pontuacao deixa o gerador preferir as
   boas sem jogar o resto fora.
"""

import json
import math
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import smf

DEFAULT_ROOT = os.path.expanduser("~/Downloads/Axel's Midi Collection")

# Compatibilidade: outros modulos e testes importam ROOT.
ROOT = DEFAULT_ROOT
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "bank.json")

STEPS_PER_BAR = 16

# DOIS COMPRIMENTOS, e os dois minerados de verdade.
#
# Colar dois trechos de quatro para fazer oito seria inventar a forma da frase
# por cima de material emprestado -- que e exatamente o erro da terceira
# tentativa, descrito no cabecalho de core/Generator.h. O levantamento mostrou
# 1.486 arquivos de oito compassos contra 453 de quatro: o material de oito
# existe inteiro no corpus, e nao precisa ser montado.
LENGTHS = (4, 8)

DRUM_WORDS = ("hihat", "hi-hat", "hat", "kick", "snare", "clap", "perc", "drum",
              "tom", "crash", "ride", "rim", "shaker", "openhat")

KS_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88]
KS_MINOR = [6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17]

MAJOR_SET = {0, 2, 4, 5, 7, 9, 11}
MINOR_SET = {0, 2, 3, 5, 7, 8, 10}


def corr(a, b):
    n = len(a)
    ma = sum(a) / n
    mb = sum(b) / n
    num = sum((a[i] - ma) * (b[i] - mb) for i in range(n))
    da = math.sqrt(sum((x - ma) ** 2 for x in a))
    db = math.sqrt(sum((x - mb) ** 2 for x in b))
    return num / (da * db) if da > 0 and db > 0 else 0.0


def detect_key(notes):
    """Krumhansl-Schmuckler, pesado por duracao. Devolve (tonica, modo, conf)."""
    hist = [0.0] * 12
    for pos, pitch, dur, vel in notes:
        hist[pitch % 12] += dur

    if sum(hist) <= 0:
        return 0, "minor", 0.0

    best = (-2.0, 0, "minor")
    for root in range(12):
        rot = hist[root:] + hist[:root]
        cmaj = corr(rot, KS_MAJOR)
        cmin = corr(rot, KS_MINOR)
        if cmaj > best[0]:
            best = (cmaj, root, "major")
        if cmin > best[0]:
            best = (cmin, root, "minor")

    return best[1], best[2], round(best[0], 3)


def load(path):
    m = smf.read(path)
    if not m.notes:
        return None
    if m.time_sigs and m.time_sigs[0][1] not in (4,):
        return None

    step = m.ppq / 4.0
    out = []
    for n in m.notes:
        if n.chan == 9 or n.vel <= 0 or not (24 <= n.pitch <= 108):
            continue
        pos = int(round(n.start / step))
        dur = max(1, int(round(n.dur / step)))
        out.append((pos, n.pitch, min(dur, 64), n.vel))

    if not out:
        return None

    base = min(p for p, _, _, _ in out)
    base -= base % STEPS_PER_BAR          # ancora no compasso, nao na 1a nota
    out = [(p - base, pi, d, v) for p, pi, d, v in out if p - base >= 0]
    out.sort()
    return out, m


def skyline(notes):
    """A voz mais aguda em cada ataque. 71% dos arquivos tem baixo e melodia na
    mesma trilha, e tratar tudo como uma linha so foi o defeito 'notas que nem
    combinam'."""
    by_pos = defaultdict(list)
    for n in notes:
        by_pos[n[0]].append(n)

    top = [max(v, key=lambda n: n[1]) for _, v in sorted(by_pos.items())]

    # corta o que caiu mais de uma oitava abaixo do corpo da linha: e baixo que
    # tocou sozinho num ataque, nao melodia
    if len(top) >= 4:
        pitches = sorted(n[1] for n in top)
        med = pitches[len(pitches) // 2]
        top = [n for n in top if n[1] >= med - 12]

    return top


def rest_ratio(line, span):
    filled = [False] * span
    for pos, _, dur, _ in line:
        for i in range(pos, min(pos + dur, span)):
            filled[i] = True
    return 1.0 - (sum(filled) / span)


def repetition(line, span):
    """Quanto a segunda metade se parece com a primeira. E o que o ouvido agarra."""
    half = span // 2
    a = [(p, pi) for p, pi, _, _ in line if p < half]
    b = [(p - half, pi) for p, pi, _, _ in line if p >= half]
    if not a or not b:
        return 0.0

    sa = {p for p, _ in a}
    sb = {p for p, _ in b}
    rhythm = len(sa & sb) / max(len(sa | sb), 1)

    da = {(p, pi - a[0][1]) for p, pi in a}
    db = {(p, pi - b[0][1]) for p, pi in b}
    contour = len(da & db) / max(len(da | db), 1)

    return round(0.5 * rhythm + 0.5 * contour, 3)


def repeated_half(seg, span):
    """A segunda metade e copia exata da primeira, em TODAS as camadas?

    Trecho assim nao e de oito compassos: e um de quatro escrito duas vezes, e o
    banco de quatro ja tem esse mesmo material do mesmo arquivo. Deixar no banco
    de oito faria a opcao de oito entregar o que a de quatro ja entregava --
    medido: 59% dos trechos de oito eram isso.

    A comparacao e exata de proposito. Segunda metade PARECIDA com a primeira e
    o que o ouvido agarra, e continua valendo ponto na `repetition`; so a copia
    literal nao acrescenta nada."""
    half = span // 2

    def split(items, pos_of):
        a = [x for x in items if pos_of(x) < half]
        b = [x for x in items if pos_of(x) >= half]
        return a, b

    ma, mb = split(seg["melody"], lambda n: n[0])
    if not mb:
        return True                     # segunda metade vazia: nao ha oito nenhum

    # SEM A VELOCITY. Comparar com ela deixava passar 126 em 800: quatro
    # compassos escritos duas vezes com a dinamica um pouco diferente ainda sao
    # quatro compassos escritos duas vezes.
    if [[n[0] - half, n[1], n[2]] for n in mb] != [[n[0], n[1], n[2]] for n in ma]:
        return False

    ca, cb = split(seg["chords"], lambda c: c[0])
    if [[c[0] - half, c[1], c[2]] for c in cb] != ca:
        return False

    ba, bb = split(seg["bass"], lambda n: n[0])
    return [[n[0] - half, n[1], n[2]] for n in bb] == ba


def thin_second_half(seg, span):
    """Menos de tres notas depois da metade: sao quatro compassos com cauda."""
    half = span // 2
    return sum(1 for n in seg["melody"] if n[0] >= half) < 3


def in_scale(line, tonic, mode):
    s = MAJOR_SET if mode == "major" else MINOR_SET
    if not line:
        return 0.0
    hit = sum(1 for _, pi, _, _ in line if (pi - tonic) % 12 in s)
    return round(hit / len(line), 3)


def segment(notes, tonic, mode, bar_off, bars):
    """Recorta `bars` compassos e separa as camadas."""
    span = bars * STEPS_PER_BAR
    lo = bar_off * STEPS_PER_BAR
    hi = lo + span

    inside = [(p - lo, pi, min(d, hi - p), v) for p, pi, d, v in notes if lo <= p < hi]
    minimo = 5 if bars <= 4 else 9

    if len(inside) < minimo:
        return None

    top = skyline(inside)
    if len(top) < minimo:
        return None

    top_set = set(id(n) for n in top)
    rest = [n for n in inside if id(n) not in top_set]

    mel_min = min(n[1] for n in top)

    # BAIXO E O QUE ESTA EMBAIXO, nao "o que sobrou sozinho". A versao anterior
    # chamava de baixo qualquer nota fora da linha de cima, e caia uma voz
    # interna -- as vezes ACIMA da melodia -- no lugar do 808.
    by_pos = defaultdict(list)
    for n in rest:
        by_pos[n[0]].append(n)

    lowest = [min(g, key=lambda n: n[1]) for _, g in sorted(by_pos.items())]
    cand = [n for n in lowest if n[1] <= mel_min - 7]

    bass_ids = set()
    if lowest and len(cand) >= 0.6 * len(lowest):
        bass_ids = {id(n) for n in cand}

    bass = [[n[0], n[1] - tonic, n[2]] for n in cand] if bass_ids else []

    # o resto e harmonia: acordes e vozes internas, na forma em que foram tocados
    chords = []
    for pos, group in sorted(by_pos.items()):
        voices = sorted((n for n in group if id(n) not in bass_ids), key=lambda n: n[1])
        if voices:
            chords.append([pos, max(n[2] for n in voices), [n[1] - tonic for n in voices]])

    melody = [[p, pi - tonic, d, v] for p, pi, d, v in top]

    return {
        "melody": melody,
        "chords": chords,
        "bass": bass,
        "rep": repetition(top, span),
        "rest": round(rest_ratio(top, span), 3),
        "scale": in_scale(top, tonic, mode),
        "lo": min(pi for _, pi, _, _ in top) - tonic,
        "hi": max(pi for _, pi, _, _ in top) - tonic,
    }


def collect(roots):
    """Acha os MIDIs melodicos de cada pasta, guardando de qual raiz cada um veio
    -- e a raiz que faz o `src` gravado ser legivel em vez de caminho absoluto."""
    found = []

    for root in roots:
        root = os.path.expanduser(root)

        if not os.path.isdir(root):
            print(f"aviso: {root} nao e uma pasta, pulando")
            continue

        for dirpath, _, names in os.walk(root):
            for n in names:
                if not n.lower().endswith((".mid", ".midi")):
                    continue

                p = os.path.join(dirpath, n)

                if not any(w in os.path.relpath(p, root).lower() for w in DRUM_WORDS):
                    found.append((p, root))

    found.sort()
    return found


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    dry = "--dry" in sys.argv

    # MATERIAL NOVO ENTRA NO NIVEL DO QUE JA ESTA LA.
    #
    # O corpus de trap tem repeticao interna mediana 0,57; o material peneirado
    # do acervo generico, 0,34. Misturar sem corte trocaria o carater do banco:
    # com o sorteio enviesado, 234 mil trechos novos deixariam so 13% dos
    # sorteios caindo no material conhecido. O corte nao se aplica ao corpus
    # conhecido -- esse ja foi validado por ouvido, e cortar ele por uma medida
    # jogaria fora material bom.
    min_rep = 0.0
    out_path = OUT

    for i, a in enumerate(sys.argv):
        if a == "--min-rep" and i + 1 < len(sys.argv):
            min_rep = float(sys.argv[i + 1])
            args = [x for x in args if x != sys.argv[i + 1]]
        if a == "--out" and i + 1 < len(sys.argv):
            out_path = sys.argv[i + 1]
            args = [x for x in args if x != sys.argv[i + 1]]

    if min_rep > 0.0:
        print(f"corte de repeticao interna: {min_rep}")

    # `--list` evita copiar 100 mil arquivos so para poder minerar: a peneira
    # ja produziu a lista, e copiar seria 1,3 GB de duplicata para nada.
    lista = None

    for i, a in enumerate(sys.argv):
        if a == "--list" and i + 1 < len(sys.argv):
            lista = sys.argv[i + 1]
            args = [x for x in args if x != lista]

    if lista:
        with open(lista) as f:
            paths = [ln.strip() for ln in f if ln.strip()]

        raiz = os.path.commonpath(paths) if paths else "/"
        pairs = [(p, raiz) for p in sorted(paths)]
        roots = [f"{lista} ({len(paths)} caminhos, raiz {raiz})"]
    else:
        roots = args or [DEFAULT_ROOT]
        pairs = collect(roots)
    files = [p for p, _ in pairs]
    root_of = dict(pairs)

    for r in roots:
        print(f"pasta: {r}")

    segs = []
    drop = Counter()

    for path in files:
        try:
            loaded = load(path)
        except Exception:
            drop["ilegivel"] += 1
            continue
        if not loaded:
            drop["vazio"] += 1
            continue

        notes, m = loaded
        tonic, mode, conf = detect_key(notes)

        if conf < 0.6:
            drop["tom incerto"] += 1
            continue

        span = max(p + d for p, _, d, _ in notes)
        n_bars = max(1, math.ceil(span / STEPS_PER_BAR))

        made = 0
        for bars in LENGTHS:
            for bar_off in range(0, max(1, n_bars - (bars - 1)), bars):
                s = segment(notes, tonic, mode, bar_off, bars)
                if not s:
                    continue
                if s["scale"] < 0.7:
                    drop["fora do tom"] += 1
                    continue
                if len({n[1] for n in s["melody"]}) < 3:
                    drop["uma nota so"] += 1
                    continue
                if s["hi"] - s["lo"] > 26:
                    drop["ambito grande"] += 1
                    continue

                # O CORTE E A FOLGA DE UMA OITAVA, e nao um numero escolhido a
                # dedo. A faixa tocavel tem 72 semitons (28 a 100) e o gerador
                # so pode deslocar o bloco em OITAVAS INTEIRAS -- qualquer outro
                # passo mudaria o tom. Com folga menor que 12, existe tom em que
                # nenhuma oitava encaixa, e o gerador e obrigado a estourar a
                # faixa: era a origem das 14 frases fora de faixa em 2.400.
                allr = ([n[1] for n in s["melody"]]
                        + [v for c in s["chords"] for v in c[2]]
                        + [n[1] for n in s["bass"]])
                if max(allr) - min(allr) > 60:
                    drop["bloco largo demais"] += 1
                    continue

                if bars > 4:
                    span = bars * STEPS_PER_BAR

                    if thin_second_half(s, span):
                        drop["oito com metade vazia"] += 1
                        continue

                    if repeated_half(s, span):
                        drop["oito que e quatro repetido"] += 1
                        continue

                if s["rep"] < min_rep:
                    drop["repeticao baixa"] += 1
                    continue

                s["src"] = os.path.relpath(path, root_of[path])
                s["bar"] = bar_off
                s["bars"] = bars
                s["mode"] = mode
                s["conf"] = conf
                s["bpm"] = round(m.bpm) if m.bpm else None
                segs.append(s)
                made += 1

        if made == 0:
            drop["nenhum trecho"] += 1

    if not dry:
        os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
        with open(out_path, "w") as f:
            json.dump(segs, f)

    print(f"arquivos varridos: {len(files)}")
    print(f"trechos de 4 compassos: {len(segs)}")
    for k, v in drop.most_common():
        print(f"   descartado {v:5d}  {k}")

    modes = Counter(s["mode"] for s in segs)
    print("\nmodo:", dict(modes))
    print("compassos:", dict(Counter(s["bars"] for s in segs)))
    print("com acordes:", sum(1 for s in segs if s["chords"]))
    print("com baixo:  ", sum(1 for s in segs if s["bass"]))
    print("so melodia: ", sum(1 for s in segs if not s["chords"] and not s["bass"]))

    rep = sorted(s["rep"] for s in segs)
    if rep:
        print(f"\nrepeticao interna  mediana {rep[len(rep)//2]:.2f}   "
              f"acima de 0.5: {sum(1 for r in rep if r > 0.5)}")
    rst = sorted(s["rest"] for s in segs)
    print(f"silencio no compasso  mediana {rst[len(rst)//2]:.2f}")
    if dry:
        print("\n--dry: nada foi escrito. Tire o --dry para gravar o banco.")
    else:
        print(f"\nbanco: {out_path}  ({os.path.getsize(out_path)/1e6:.1f} MB)")


if __name__ == "__main__":
    main()
