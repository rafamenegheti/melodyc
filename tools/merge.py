"""Junta bancos, remove duplicata e ordena por qualidade.

  python3 tools/merge.py saida.json entrada1.json entrada2.json ...

A PRIMEIRA ENTRADA VEM NA FRENTE, e dentro de cada entrada a ordem e por
qualidade.

Ordenar tudo junto por qualidade parecia obvio e deu no contrario do que se
queria. O material novo passa por um corte de repeticao que o conhecido nao
passa -- ele entra no nivel do que ja esta la, e por isso pontua MELHOR (0,74
contra 0,57). Misturado, ele foi para a frente e empurrou o corpus de trap para
a posicao mediana 39.982 de 51.004: com o sorteio enviesado, sobravam 6,6% dos
sorteios para o material conhecido. Isso nao e acrescentar material, e trocar o
plugin.

Repeticao interna e um bom indicio, nao a verdade. O corpus de trap foi validado
por ouvido; o material novo, so por medida. Quem foi validado por ouvido fica na
frente.

--------------------------------------------------------------------------------
A DUPLICATA E EXATA, E NAO PARECIDA.

O banco guarda semitons RELATIVOS A TONICA -- e o que faz a frase soar igual em
qualquer tom. Pack de progressao rende a mesma progressao nos doze tons
(`Db - I V I IV.mid`, `Eb - I V I IV.mid`, ...), e depois da normalizacao os doze
arquivos viram bytes identicos.

Sem esta passada, 12.353 trechos eram 7.166 frases: um terco eram copias, e
algumas apareciam VINTE E SEIS vezes. Como o sorteio escolhe por posicao, uma
frase repetida 26 vezes tem 26 bilhetes -- ela sai muito mais que as outras.
Apertar GERAR devolvia a mesma coisa mais vezes do que deveria, que e o oposto
do que acrescentar material deveria fazer.

Compara melodia, acordes, baixo e comprimento. Nao compara `src`, `bpm` nem
`conf`: duas copias da mesma frase com andamento diferente continuam sendo a
mesma frase para quem escuta.

FICA A PRIMEIRA OCORRENCIA, e e por isso que a deduplicacao acontece DEPOIS da
ordem por procedencia: uma frase que existe nos dois corpora e mantida na
posicao do corpus validado por ouvido, e nao na do material novo.
"""

import json
import math
import os
import sys


def quality(s):
    return (2.0 * s["rep"] + 0.4 * s["conf"]
            + (0.5 if s["chords"] else 0.0) + (0.3 if s["bass"] else 0.0))


def identidade(s):
    """O que faz duas frases serem a mesma para quem escuta."""
    return json.dumps([s["melody"], s["chords"], s["bass"], s["bars"]],
                      separators=(",", ":"))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return

    out = sys.argv[1]
    entradas = sys.argv[2:]

    total = []
    vistos = set()
    mantidos = []          # quantos de cada entrada sobreviveram

    for path in entradas:
        with open(path) as f:
            b = json.load(f)

        b.sort(key=lambda s: -quality(s))     # qualidade DENTRO da entrada

        inicio = len(total)
        repetidas = 0

        for s in b:
            k = identidade(s)
            if k in vistos:
                repetidas += 1
                continue
            vistos.add(k)
            total.append(s)

        novos = len(total) - inicio
        mantidos.append(novos)

        r = sorted(s["rep"] for s in b)
        print(f"{os.path.basename(path):28s} {len(b):7d} trechos  "
              f"repeticao mediana {r[len(r)//2]:.2f}")
        print(f"{'':28s} {novos:7d} unicos   {repetidas} duplicata(s)  "
              f"-> posicoes {inicio}..{len(total)-1}")

    with open(out, "w") as f:
        json.dump(total, f)

    r = sorted(s["rep"] for s in total)
    print(f"\n{len(total)} trechos no total, repeticao mediana {r[len(r)//2]:.2f}")

    # Quanto de cada entrada o gerador vai sortear de fato. O vies e u^2, entao
    # o peso de um trecho na posicao i e a largura da fatia dele em sqrt.
    n = len(total)
    faixa = 0

    print("\nquanto cada entrada leva do sorteio:")
    for path, k in zip(entradas, mantidos):
        peso = math.sqrt((faixa + k) / n) - math.sqrt(faixa / n)
        print(f"  {os.path.basename(path):28s} {100*peso:5.1f}%")
        faixa += k

    print(f"\n{out}")


if __name__ == "__main__":
    main()
