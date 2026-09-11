"""Recombinacao: melodia de um trecho sobre a harmonia de outro.

Guardar trechos inteiros garante que soa bem, mas o banco inteiro cabe num
navegador de loops -- 3.291 resultados e acabou. A variacao de verdade vem de
trocar as camadas, e trocar as camadas so funciona com uma prova de que a
melodia CABE naquela harmonia. Sem essa prova, e o defeito relatado na primeira
tentativa: "notas que nem sequer combinam".

As duas camadas ja estao em semitons relativos a tonica e no mesmo modo, entao
comparar e olhar classe de altura -- nada de detectar acorde de novo.
"""

CHORD_TONE = 0
SCALE_TONE = 1
OUTSIDE = 2

MAJOR_SET = {0, 2, 4, 5, 7, 9, 11}
MINOR_SET = {0, 2, 3, 5, 7, 8, 10}


def _sounding(chords, pos, dur):
    """As classes de altura da harmonia enquanto a nota soa."""
    out = set()
    for cpos, cdur, rels in chords:
        if cpos < pos + dur and cpos + cdur > pos:
            out.update(r % 12 for r in rels)
    return out


def fit(melody, chords, mode):
    """Quanto a melodia cabe na harmonia, ponderado pela duracao da nota.

    Peso por duracao, e nao por nota: uma semicolcheia de passagem fora do
    acorde e ornamento, uma minima fora do acorde e erro. Contar notas trata as
    duas igual e foi o que fez a media mentir na medicao anterior."""
    scale = MAJOR_SET if mode == "major" else MINOR_SET

    total = 0.0
    good = 0.0

    for pos, rel, dur, _ in melody:
        pc = rel % 12
        under = _sounding(chords, pos, dur)

        if not under:
            w = 1.0 if pc in scale else 0.0
        elif pc in under:
            w = 1.0
        elif pc in scale:
            w = 0.75
        else:
            w = 0.0

        total += dur
        good += w * dur

    return good / total if total > 0 else 0.0


def combine(mel_seg, harm_seg, threshold=0.82):
    """Devolve o trecho recombinado, ou None se a melodia nao couber."""
    if mel_seg["mode"] != harm_seg["mode"]:
        return None

    score = fit(mel_seg["melody"], harm_seg["chords"], mel_seg["mode"])
    if score < threshold:
        return None

    return {
        "melody": mel_seg["melody"],
        "chords": harm_seg["chords"],
        "bass": harm_seg["bass"],
        "mode": mel_seg["mode"],
        "conf": min(mel_seg["conf"], harm_seg["conf"]),
        "rep": mel_seg["rep"],
        "rest": mel_seg["rest"],
        "bpm": mel_seg["bpm"] or harm_seg["bpm"],
        "src": f"mel {mel_seg['src']}  +  harm {harm_seg['src']}",
        "fit": round(score, 3),
    }
