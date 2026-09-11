"""Pontua UMA FATIA da lista de caminhos, num processo so.

  python3 tools/fatia.py <lista> <inicio> <fim> <quantos> <saida.json>

SEM `multiprocessing`, E DE PROPOSITO.

Duas varreduras morreram nele. A primeira usava `imap` ordenado e travou para
sempre quando um operario morreu com o item que o parente esperava; a segunda
entupiu os canos entre parente e filhos e parou com todos os processos a 0% de
CPU. Sao dois modos de falha diferentes da mesma biblioteca, e nenhum dos dois
aparece em teste pequeno.

O paralelismo aqui e outro: N processos independentes, cada um com a sua fatia e
o seu arquivo de saida. Nao ha fila, nao ha cano, nao ha parente. Se um morre,
morre so a fatia dele, e da para rodar aquela de novo sem tocar nas outras.
"""

import heapq
import json
import os
import signal
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import traits

HERE = os.path.dirname(os.path.abspath(__file__))
PROFILE = os.path.join(HERE, "..", "data", "profile.json")

# O porteiro ja exige no maximo 16 compassos, entao meio mega nao tinha chance.
# Conferir o tamanho antes de abrir custa uma chamada de sistema e evita o
# parser inteiro -- eram esses arquivos que derrubavam os operarios.
MAX_BYTES = 512 * 1024

# PRAZO POR ARQUIVO.
#
# Duas fatias ficaram 55 minutos a 89% de CPU sem passar de 50 mil arquivos,
# enquanto outras chegavam a metade da faixa delas. Medindo as mesmas faixas por
# fora dava 135 arquivos/s, sem nenhum lento -- ou seja, nao e a faixa, e um
# arquivo especifico onde o parser nao sai.
#
# Achar QUAL exige forense em processo vivo; nao achar custa a varredura inteira.
# Num lote sobre 6,74 milhoes de arquivos de origem desconhecida, o prazo tinha
# de existir desde o comeco: um arquivo que passa de cinco segundos nao ia virar
# frase de quatro compassos de todo jeito.
TEMPO_MAX = 5.0


class Estourou(Exception):
    pass


def _alarme(sig, frame):
    raise Estourou()


signal.signal(signal.SIGALRM, _alarme)


def main():
    if len(sys.argv) < 6:
        print(__doc__)
        return

    lista, inicio, fim, keep, saida = (sys.argv[1], int(sys.argv[2]),
                                       int(sys.argv[3]), int(sys.argv[4]), sys.argv[5])

    with open(PROFILE) as f:
        profile = json.load(f)

    with open(lista) as f:
        paths = [ln.rstrip("\n") for ln in f][inicio:fim]

    print(f"fatia {inicio}..{fim}  ({len(paths)} arquivos)", flush=True)

    melhores = []
    estourados = []
    passaram = lidos = 0
    t0 = time.time()

    def grava():
        ordenado = sorted(melhores, reverse=True)

        with open(saida + ".tmp", "w") as f:
            json.dump({"inicio": inicio, "fim": fim, "lidos": lidos,
                       "passaram": passaram,
                       "top": [[round(n, 5), p] for n, p in ordenado]}, f)

        os.replace(saida + ".tmp", saida)     # troca atomica: nunca meio arquivo

    for path in paths:
        lidos += 1

        try:
            if os.path.getsize(path) > MAX_BYTES:
                continue

            signal.setitimer(signal.ITIMER_REAL, TEMPO_MAX)
            v = traits.features(path)
            signal.setitimer(signal.ITIMER_REAL, 0)
        except Estourou:
            signal.setitimer(signal.ITIMER_REAL, 0)
            estourados.append(path)
            print(f"  ESTOUROU {TEMPO_MAX}s: {path}", flush=True)
            continue
        except Exception:
            signal.setitimer(signal.ITIMER_REAL, 0)
            continue

        if v is None or not traits.passes_gates(v):
            continue

        passaram += 1
        nota = traits.score(v, profile)

        if len(melhores) < keep:
            heapq.heappush(melhores, (nota, path))
        elif nota > melhores[0][0]:
            heapq.heapreplace(melhores, (nota, path))

        if lidos % 50000 == 0:
            grava()
            dt = time.time() - t0
            falta = (len(paths) - lidos) * dt / lidos / 60.0
            corte = melhores[0][0] if len(melhores) >= keep else 0.0
            print(f"  {lidos}/{len(paths)}  {passaram} candidatos  "
                  f"corte {corte:.3f}  faltam ~{falta:.0f} min", flush=True)

    grava()
    print(f"pronto: {lidos} lidos, {passaram} candidatos, {len(melhores)} guardados, "
          f"{len(estourados)} estourados, em {(time.time()-t0)/60:.0f} min", flush=True)


if __name__ == "__main__":
    main()
