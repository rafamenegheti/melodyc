"""Varre uma pasta e pontua cada arquivo pela semelhanca com o corpus conhecido.

  python3 tools/sift.py perfil                     -> mede o corpus de trap
  python3 tools/sift.py peneira <pasta> [amostra]  -> pontua a pasta
  python3 tools/sift.py topo <pasta> <n>           -> guarda os n melhores
"""

import heapq
import json
import os
import random
import sys
import time
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mine
import traits

HERE = os.path.dirname(os.path.abspath(__file__))
PROFILE = os.path.join(HERE, "..", "data", "profile.json")
CHECKPOINT = os.path.join(HERE, "..", "data", "sift-progress.json")


def midis(root, limit=None, seed=1):
    out = []
    for dirpath, _, names in os.walk(root):
        for n in names:
            if n.lower().endswith((".mid", ".midi")):
                out.append(os.path.join(dirpath, n))
    out.sort()

    if limit and len(out) > limit:
        random.Random(seed).shuffle(out)
        out = out[:limit]

    return out


# ARQUIVO GRANDE NEM E ABERTO.
#
# O porteiro ja exige no maximo 16 compassos, entao um MIDI de meio mega nao
# tinha chance nenhuma de passar -- e era ele que derrubava o operario. Tres dos
# oito processos morreram na varredura anterior, e um operario que morre leva o
# resultado dele junto. Conferir o tamanho antes de abrir custa uma chamada de
# sistema e evita o parser inteiro.
MAX_BYTES = 512 * 1024


def _feat(path):
    try:
        if os.path.getsize(path) > MAX_BYTES:
            return path, None

        return path, traits.features(path)
    except Exception:
        return path, None


def scan(paths, workers=8):
    with Pool(workers) as pool:
        return [(p, v) for p, v in pool.imap_unordered(_feat, paths, chunksize=200)
                if v is not None]


def top(root, keep):
    """Os `keep` melhores de uma pasta grande, com ponto de retomada.

    HEAP LIMITADO, e nao lista ordenada no fim. Com 6,74 milhoes de arquivos,
    guardar nota e caminho de todos passa de meio giga de memoria antes de
    ordenar -- e o unico motivo de guardar seria jogar fora 98% em seguida.

    E GRAVA O PROGRESSO A CADA 200 MIL. Uma varredura de quase duas horas ja
    morreu uma vez junto com a sessao que a lancou, sem deixar nada. Com o
    ponto de retomada, a segunda tentativa comeca de onde a primeira parou -- a
    ordem dos caminhos e fixa, entao "quantos ja foram" basta para saber onde
    continuar."""
    with open(PROFILE) as f:
        profile = json.load(f)

    cache = os.path.join(HERE, "..", "data", "sift-paths.txt")

    if os.path.exists(cache):
        with open(cache) as f:
            paths = [ln.rstrip("\n") for ln in f]
        print(f"lista em cache: {len(paths)} arquivos", flush=True)
    else:
        print("andando pela arvore...", flush=True)
        paths = midis(root)

        with open(cache, "w") as f:
            f.write("\n".join(paths))

        print(f"{len(paths)} arquivos", flush=True)

    total = len(paths)

    melhores = []
    feitos = passaram = 0

    if os.path.exists(CHECKPOINT):
        with open(CHECKPOINT) as f:
            ck = json.load(f)

        if ck.get("root") == root and ck.get("total") == total:
            melhores = [(n, p) for n, p in ck["heap"]]
            heapq.heapify(melhores)
            feitos = ck["feitos"]
            passaram = ck["passaram"]
            print(f"retomando de {feitos} ({100.0*feitos/total:.1f}%), "
                  f"{len(melhores)} ja guardados", flush=True)

    def grava(final=False):
        alvo = os.path.join(HERE, "..", "data",
                            "sift-top.json" if final else "sift-progress.json")
        ordenado = sorted(melhores, reverse=True)

        with open(alvo + ".tmp", "w") as f:
            if final:
                json.dump([[round(n, 4), p] for n, p in ordenado], f)
            else:
                json.dump({"root": root, "total": total, "feitos": feitos,
                           "passaram": passaram,
                           "heap": [[round(n, 5), p] for n, p in ordenado]}, f)

        os.replace(alvo + ".tmp", alvo)      # troca atomica: nunca meio arquivo
        return alvo

    # EM LOTES, E COM `imap_unordered`.
    #
    # A versao anterior usava `imap` ordenado para poder contar o progresso, e
    # foi isso que a travou: ele espera o resultado numero N em ordem, e quando
    # o operario que tinha aquele item morre, o resultado nunca chega. Os outros
    # sete seguem queimando CPU e o parente espera para sempre -- 46 minutos sem
    # gravar nada.
    #
    # `imap_unordered` perde a tarefa do operario morto em vez de travar. A
    # contagem para retomar deixa de valer nota a nota, entao a retomada passa a
    # ser por LOTE: a ordem dos caminhos e fixa, e um lote so conta como feito
    # depois de terminar inteiro.
    LOTE = 100000
    t0 = time.time()
    inicio = feitos

    with Pool(8, maxtasksperchild=20000) as pool:
        for comeco in range(feitos, total, LOTE):
            fatia = paths[comeco:comeco + LOTE]

            for path, v in pool.imap_unordered(_feat, fatia, chunksize=400):
                if v is None or not traits.passes_gates(v):
                    continue

                passaram += 1
                nota = traits.score(v, profile)

                if len(melhores) < keep:
                    heapq.heappush(melhores, (nota, path))
                elif nota > melhores[0][0]:
                    heapq.heapreplace(melhores, (nota, path))

            feitos = comeco + len(fatia)
            grava()

            dt = time.time() - t0
            falta = (total - feitos) * dt / max(feitos - inicio, 1) / 60.0
            corte = melhores[0][0] if len(melhores) >= keep else 0.0

            print(f"  {feitos}/{total} ({100.0*feitos/total:.1f}%)  "
                  f"{passaram} candidatos  corte {corte:.3f}  "
                  f"faltam ~{falta:.0f} min", flush=True)

    out = grava(final=True)

    if os.path.exists(CHECKPOINT):
        os.remove(CHECKPOINT)

    print(f"\n{feitos} lidos, {passaram} passaram o porteiro, "
          f"{len(melhores)} guardados")

    if melhores:
        ordenado = sorted(melhores, reverse=True)
        print(f"nota do corte: {ordenado[-1][0]:.3f}   melhor: {ordenado[0][0]:.3f}   "
              f"mediana dos escolhidos: {ordenado[len(ordenado)//2][0]:.3f}")

    print(f"em {out}")
    print(f"levou {(time.time()-t0)/60:.0f} min")


def build():
    root = mine.DEFAULT_ROOT
    paths = [p for p in midis(root)
             if not any(w in os.path.relpath(p, root).lower() for w in mine.DRUM_WORDS)]

    print(f"corpus conhecido: {len(paths)} arquivos")

    got = scan(paths)
    print(f"legiveis: {len(got)}")

    vectors = [v for _, v in got]
    profile = traits.build_profile(vectors)

    with open(PROFILE, "w") as f:
        json.dump(profile, f)

    print(f"\n{'medida':<22} {'p05':>9} {'mediana':>9} {'p95':>9}")
    for i, name in enumerate(traits.NAMES):
        col = profile[i]
        print(f"{name:<22} {col[len(col)//20]:9.2f} {col[len(col)//2]:9.2f} "
              f"{col[int(len(col)*0.95)]:9.2f}")

    # Auto-nota: quanto o proprio corpus tira. E o teto pratico da peneira.
    notas = sorted(traits.score(v, profile) for v in vectors)
    print(f"\nnota do proprio corpus: p10 {notas[len(notas)//10]:.3f}  "
          f"mediana {notas[len(notas)//2]:.3f}  p90 {notas[int(len(notas)*0.9)]:.3f}")


def sift(root, limit=None):
    with open(PROFILE) as f:
        profile = json.load(f)

    paths = midis(root, limit)
    print(f"{len(paths)} arquivos em {root}")

    got = scan(paths)
    print(f"legiveis: {len(got)} ({100.0*len(got)/max(len(paths),1):.0f}%)")

    passaram = [(p, v) for p, v in got if traits.passes_gates(v)]

    print(f"passam o porteiro (ate 2 trilhas, ate 16 compassos): {len(passaram)} "
          f"({100.0*len(passaram)/max(len(got),1):.1f}%)")

    scored = sorted(((traits.score(v, profile), p) for p, v in passaram), reverse=True)

    if not scored:
        print("nenhum candidato")
        return

    notas = [s for s, _ in scored]

    print(f"\nnota: p50 {notas[len(notas)//2]:.3f}  p90 {notas[int(len(notas)*0.1)]:.3f}  "
          f"p99 {notas[int(len(notas)*0.01)]:.3f}  max {notas[0]:.3f}")

    print("\ncorte   passam   % do lido")
    for corte in (0.30, 0.40, 0.50, 0.55, 0.60, 0.65, 0.70):
        n = sum(1 for s in notas if s >= corte)
        print(f"{corte:.2f}  {n:7d}   {100.0*n/len(got):6.2f}%")

    out = os.path.join(HERE, "..", "data", "sift.json")
    with open(out, "w") as f:
        json.dump([[round(s, 4), p] for s, p in scored[:200000]], f)

    print(f"\nranking em {out}")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "perfil":
        build()
    elif len(sys.argv) > 2 and sys.argv[1] == "peneira":
        sift(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else None)
    elif len(sys.argv) > 3 and sys.argv[1] == "topo":
        top(sys.argv[2], int(sys.argv[3]))
    else:
        print(__doc__)
