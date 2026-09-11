"""As medidas que descrevem um arquivo MIDI, e o quanto ele parece trap.

NAO HA ROTULO NEGATIVO, e por isso nao ha classificador de duas classes. O que
existe e um conjunto POSITIVO -- os 2.062 arquivos dos kits, que sao trap por
construcao -- e a pergunta util e "quanto este arquivo desconhecido se parece
com aquilo". Isso e classificacao de uma classe so, e a forma mais honesta de
fazer sem biblioteca e a percentil: para cada medida, olha onde o candidato cai
na distribuicao do corpus conhecido.

Um arquivo na mediana do corpus tira 1.0 naquela medida; um na cauda, perto de
zero. A nota final e a media geometrica, e nao a aritmetica, porque UMA medida
absurda deve derrubar o arquivo inteiro -- um MIDI de 300 compassos nao vira
trap por acertar o resto.
"""

import math
import os
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mine
import smf

STEPS_PER_BAR = 16

# A ordem importa so para o relatorio ficar legivel.
NAMES = ["compassos", "trilhas", "polifonia", "bpm", "ambito", "notas_por_compasso",
         "na_grade_16", "modo_menor", "confianca_tom", "no_tom", "silencio",
         "passo_curto", "duracao_mediana", "vao_do_baixo", "notas_longas"]


def features(path):
    """Devolve a lista de medidas, ou None se o arquivo nao serve para nada."""
    try:
        m = smf.read(path)
    except Exception:
        return None

    notes = [n for n in m.notes if n.chan != 9 and n.vel > 0 and 24 <= n.pitch <= 108]

    if len(notes) < 8:
        return None

    step = m.ppq / 4.0
    if step <= 0:
        return None

    q = [(int(round(n.start / step)), n.pitch, max(1, int(round(n.dur / step))), n.vel)
         for n in notes]
    q.sort()

    base = min(p for p, _, _, _ in q)
    q = [(p - base, pi, d, v) for p, pi, d, v in q]

    span = max(p + d for p, _, d, _ in q)
    bars = max(1.0, span / STEPS_PER_BAR)

    # O PORTEIRO VEM ANTES DA CONTA CARA.
    #
    # `rest_ratio` aloca uma lista do tamanho do trecho e percorre nota por
    # nota; com 2.369 compassos isso e dezenas de milhoes de operacoes, e com
    # uma nota perdida num tique absurdo sao gigabytes. Era o que matava os
    # processos e o que fazia uma fatia andar a tres arquivos por segundo.
    #
    # Como o porteiro ja recusa acima de 16 compassos, medir o comprimento
    # primeiro nao muda resultado nenhum -- so deixa de gastar a analise inteira
    # em arquivo que ia ser recusado de todo jeito. E a maioria dos 6,74 milhoes
    # e musica inteira, entao a maioria sai por aqui.
    lo, hi = GATES["compassos"]

    if bars < lo or bars > hi:
        return None

    if len({n.track for n in notes}) > GATES["trilhas"][1]:
        return None

    pitches = [pi for _, pi, _, _ in q]
    ambito = max(pitches) - min(pitches)

    onsets = defaultdict(list)
    for n in q:
        onsets[n[0]].append(n)

    poly = len(q) / max(len(onsets), 1)
    notas_por_compasso = len(q) / bars

    # Quanto do material cai na grade de semicolcheia. MIDI de kit e escrito na
    # grade; transcricao de musica tocada, nao.
    exatos = sum(1 for n in notes if abs(n.start / step - round(n.start / step)) < 0.08)
    na_grade = exatos / len(notes)

    tonic, mode, conf = mine.detect_key(q)

    top = mine.skyline(q)
    silencio = mine.rest_ratio(top, span) if span > 0 else 0.0

    saltos = [abs(top[i][1] - top[i - 1][1]) for i in range(1, len(top))]
    passo_curto = sum(1 for s in saltos if s <= 2) / max(len(saltos), 1)

    duracoes = sorted(d for _, _, d, _ in q)
    dur_mediana = duracoes[len(duracoes) // 2]

    # O vao entre a voz mais grave e a melodia. Em trap o sub mora uma ou duas
    # oitavas abaixo; em transcricao de banda, as vozes se encostam.
    mel_min = min(n[1] for n in top)
    vao = mel_min - min(pitches)

    # Fracao de notas que duram meio compasso ou mais.
    #
    # A primeira versao contava TROCA DE ACORDE por ataque, e em arquivo
    # monofonico isso vira "notas por compasso" outra vez -- um arpejo do
    # Cymatics deu 15,75 trocas, que nao mede harmonia nenhuma. Nota longa mede
    # a mesma coisa por outro lado e funciona em qualquer textura: harmonia
    # estatica e sub sustentado aparecem como nota comprida.
    longas = sum(1 for _, _, d, _ in q if d >= 8) / len(q)

    trilhas = len({n.track for n in notes})

    return [bars, trilhas, poly, m.bpm or 140.0, ambito, notas_por_compasso,
            na_grade, 1.0 if mode == "minor" else 0.0, conf,
            mine.in_scale(top, tonic, mode), silencio, passo_curto,
            float(dur_mediana), float(vao), longas]


# MEDIDA SEM ESPALHAMENTO NAO PONTUA, FILTRA.
#
# `trilhas` vale 1 em 95% do corpus. Como nota, ela dava o piso para todo
# arquivo do acervo com duas trilhas ou mais -- ou seja, para quase todos --,
# uma penalidade constante que nao separa ninguem e ainda comprime a faixa das
# outras. E `compassos` tinha espalhamento pequeno demais para se defender: nos
# 200 melhores entraram arquivos de 72 e 123 compassos, que nao sao loop
# nenhum, carregados pelas outras treze medidas.
#
# As duas viraram porteiro, com folga generosa sobre o corpus.
GATES = {
    "trilhas": (1, 2),
    "compassos": (2, 16),
}


def passes_gates(vector):
    for name, (lo, hi) in GATES.items():
        v = vector[NAMES.index(name)]

        if v < lo or v > hi:
            return False

    return True


def build_profile(vectors):
    """A distribuicao empirica de cada medida no corpus conhecido."""
    return [sorted(v[i] for v in vectors) for i in range(len(NAMES))]


def _centrality(value, sorted_values):
    """1.0 na mediana, perto de 0 nas caudas."""
    n = len(sorted_values)

    if n == 0:
        return 0.0

    lo, hi = 0, n
    while lo < hi:
        mid = (lo + hi) // 2
        if sorted_values[mid] < value:
            lo = mid + 1
        else:
            hi = mid

    f = lo / n
    return 2.0 * min(f, 1.0 - f)


def score(vector, profile, floor=0.02):
    """Media GEOMETRICA das centralidades.

    Geometrica porque uma medida absurda tem de derrubar o arquivo inteiro: um
    MIDI de 300 compassos nao vira trap por acertar as outras quatorze. Na
    aritmetica ele passaria com 0,9."""
    total = 0.0
    n = 0

    for i, v in enumerate(vector):
        if NAMES[i] in GATES:
            continue                      # essa e porteiro, nao nota

        total += math.log(max(floor, _centrality(v, profile[i])))
        n += 1

    return math.exp(total / max(n, 1))
