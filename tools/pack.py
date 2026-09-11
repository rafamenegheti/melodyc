"""Empacota data/bank.json em data/bank.bin -- o formato que o C++ le.

O blob tem tabela de deslocamentos na frente, entao pegar o trecho 2.417 e uma
soma de ponteiro. Nada e parseado no carregamento e nada e alocado no sorteio,
que e o que permite o gerador rodar sem susto e o plugin abrir instantaneo.

  'MFB2'                          4 bytes
  u32 quantidade
  u32 deslocamento[quantidade]    do inicio do arquivo
  por trecho:
    u8 modo (0 maior, 1 menor)
    u8 repeticao, u8 silencio, u8 confianca   0-255
    u8 bpm (clampeado 60-255)
    u8 compassos (4 ou 8)
    u8 nMel, u8 nAcordes, u8 nBaixo
    nMel     x { u8 pos, u8 rel, u8 dur, u8 vel }
    nBaixo   x { u8 pos, u8 rel, u8 dur }
    nAcordes x { u8 pos, u8 dur, u8 vozes, u8 rel x vozes }

O acorde e a UNICA camada de tamanho variavel, e por isso vem por ultimo: assim
melodia e baixo sao alcancados por aritmetica de ponteiro, e so quem itera
acorde precisa caminhar.

`rel` e semitom acima da tonica, e nunca e negativo: a tonica e classe de altura
0-11 e a nota mais grave do corpus e 24.

A MAGICA VIROU MFB2 porque o cabecalho ganhou um byte. Blob antigo tem de ser
RECUSADO, e nao lido torto: com o layout deslocado, `nMel` cairia no byte de
compassos e o gerador leria contagem de nota onde ha 4 ou 8 -- silencio ou lixo,
sem nada apontando para a causa.
"""

import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "data", "bank.json")
DST = os.path.join(HERE, "..", "data", "bank.bin")

MAX_EVENTS = 128        # medido: melodia de 8 compassos chega a 128 notas
MAX_VOICES = 8


def q(x):
    return max(0, min(255, int(round(x * 255))))


def pack_segment(s):
    mel = sorted(s["melody"])[:MAX_EVENTS]
    ch = sorted(s["chords"])[:MAX_EVENTS]
    ba = sorted(s["bass"])[:MAX_EVENTS]

    body = bytearray()
    body.append(1 if s["mode"] == "minor" else 0)
    body.append(q(s["rep"]))
    body.append(q(s["rest"]))
    body.append(q(s["conf"]))
    body.append(max(60, min(255, int(s["bpm"] or 140))))
    body.append(int(s["bars"]))
    body.append(len(mel))
    body.append(len(ch))
    body.append(len(ba))

    for pos, rel, dur, vel in mel:
        body += bytes([pos & 127, rel & 127, min(dur, 128), min(vel, 127)])

    for pos, rel, dur in ba:
        body += bytes([pos & 127, rel & 127, min(dur, 128)])

    for pos, dur, rels in ch:
        v = sorted(rels)[:MAX_VOICES]
        body += bytes([pos & 127, min(dur, 128), len(v)]) + bytes(r & 127 for r in v)

    return bytes(body)


def main():
    with open(SRC) as f:
        bank = json.load(f)

    # A ORDEM DE ENTRADA E RESPEITADA.
    #
    # Este arquivo ordenava por qualidade, e isso passou a ser errado quando o
    # banco deixou de ter uma origem so: o material novo passa por um corte de
    # repeticao que o conhecido nao passa, entao pontua melhor e ia para a
    # frente, empurrando o corpus validado por ouvido para o fim. Quem ordena
    # agora e o tools/merge.py, que sabe qual entrada foi validada como.
    #
    # A ordem continua sendo a ordem do sorteio: indice baixo sai mais.

    bodies = [pack_segment(s) for s in bank]

    head = 4 + 4 + 4 * len(bodies)
    offsets = []
    at = head
    for b in bodies:
        offsets.append(at)
        at += len(b)

    with open(DST, "wb") as f:
        f.write(b"MFB2")
        f.write(struct.pack("<I", len(bodies)))
        f.write(struct.pack(f"<{len(offsets)}I", *offsets))
        for b in bodies:
            f.write(b)

    size = os.path.getsize(DST)
    print(f"{len(bodies)} trechos  ->  {DST}")
    print(f"{size/1024:.0f} KB   ({size/len(bodies):.0f} bytes por trecho)")

    modes, comp = {}, {}
    for s in bank:
        modes[s["mode"]] = modes.get(s["mode"], 0) + 1
        comp[s["bars"]] = comp.get(s["bars"], 0) + 1
    print("modo:", modes, "  compassos:", comp)


if __name__ == "__main__":
    main()
