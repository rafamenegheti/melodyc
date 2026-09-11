"""Sintetizador de conferencia. Nao e o som do produto -- e para ouvir a NOTA.

Existe porque conferir melodia por descricao nao funciona: 'rep 0.83, 20 notas'
nao diz se a frase e bonita. Uma tabela de onda por timbre e um envelope
exponencial bastam para julgar altura, ritmo e registro.
"""

import array
import math
import struct

SR = 32000
TABLE = 2048


def _wave(harmonics):
    t = array.array("f", [0.0] * TABLE)
    for h, amp in harmonics:
        for i in range(TABLE):
            t[i] += amp * math.sin(2.0 * math.pi * h * i / TABLE)
    peak = max(abs(x) for x in t) or 1.0
    return array.array("f", [x / peak for x in t])


# keys: fundamental forte com brilho curto. pad: quase so fundamental e quinta.
# baixo: seno com um pouco de segundo harmonico, que e o que faz o 808 aparecer
# em caixa de som pequena.
VOICES = {
    "mel":   (_wave([(1, 1.0), (2, 0.45), (3, 0.22), (4, 0.12), (6, 0.05)]), 2.8, 0.004),
    "harm":  (_wave([(1, 1.0), (2, 0.30), (3, 0.10), (5, 0.04)]), 1.1, 0.030),
    "baixo": (_wave([(1, 1.0), (2, 0.18), (3, 0.05)]), 1.6, 0.006),
}

GAIN = {"mel": 0.34, "harm": 0.20, "baixo": 0.42}


def render(tracks, bpm, bars=8, seg_bars=4):
    """tracks: [(nome, [(pos16, pitch, dur16, vel)])]. Repete o trecho ate `bars`."""
    spb = 60.0 / bpm
    step = spb / 4.0
    total = int((bars * 4 * spb + 1.5) * SR)
    buf = array.array("f", [0.0] * total)

    loop_len = seg_bars * 16
    reps = max(1, bars // seg_bars)

    for name, notes in tracks:
        table, decay, attack = VOICES.get(name, VOICES["mel"])
        gain = GAIN.get(name, 0.3)

        for rep in range(reps):
            off = rep * loop_len

            for pos, pitch, dur, vel in notes:
                start = int((pos + off) * step * SR)
                freq = 440.0 * (2.0 ** ((pitch - 69) / 12.0))

                held = dur * step
                length = int(min(held + 0.35, held * 1.25 + 0.30) * SR)
                if start + length > total:
                    length = total - start
                if length <= 0:
                    continue

                inc = freq * TABLE / SR
                phase = 0.0
                amp = gain * (vel / 127.0)

                atk = max(1, int(attack * SR))
                # decaimento por multiplicacao: uma multiplicacao por amostra
                d = math.exp(-decay / SR)
                env = 0.0
                cur = 1.0

                for i in range(length):
                    if i < atk:
                        env = i / atk
                    else:
                        env = 1.0
                        cur *= d

                    idx = int(phase)
                    buf[start + i] += amp * env * cur * table[idx & (TABLE - 1)]
                    phase += inc

    # limitador brando, para o pico nao estourar quando as camadas somam
    peak = max(abs(x) for x in buf) or 1.0
    k = 0.85 / peak if peak > 0.85 else 1.0

    out = array.array("h", [0] * total)
    for i in range(total):
        x = buf[i] * k
        x = math.tanh(x * 1.2) * 0.9
        out[i] = int(max(-32767, min(32767, x * 32767)))

    return out


def write_wav(path, samples, sr=SR):
    data = samples.tobytes()
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, sr, sr * 2, 2, 16))
        f.write(b"data" + struct.pack("<I", len(data)) + data)


def concat(chunks, gap=0.4, sr=SR):
    out = array.array("h")
    silence = array.array("h", [0] * int(gap * sr))
    for c in chunks:
        out.extend(c)
        out.extend(silence)
    return out
