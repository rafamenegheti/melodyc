"""Junta bancos e ordena por qualidade.

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
"""

import json
import os
import sys


def quality(s):
    return (2.0 * s["rep"] + 0.4 * s["conf"]
            + (0.5 if s["chords"] else 0.0) + (0.3 if s["bass"] else 0.0))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return

    out = sys.argv[1]
    total = []

    for path in sys.argv[2:]:
        with open(path) as f:
            b = json.load(f)

        b.sort(key=lambda s: -quality(s))     # qualidade DENTRO da entrada

        r = sorted(s["rep"] for s in b)
        print(f"{os.path.basename(path):28s} {len(b):7d} trechos  "
              f"repeticao mediana {r[len(r)//2]:.2f}  "
              f"-> posicoes {len(total)}..{len(total)+len(b)-1}")
        total += b

    with open(out, "w") as f:
        json.dump(total, f)

    r = sorted(s["rep"] for s in total)
    print(f"\n{len(total)} trechos no total, repeticao mediana {r[len(r)//2]:.2f}")

    # Quanto de cada entrada o gerador vai sortear de fato. O vies e u^2, entao
    # o peso de um trecho na posicao i e a largura da fatia dele em sqrt.
    import math

    n = len(total)
    faixa = 0

    print("\nquanto cada entrada leva do sorteio:")
    for path in sys.argv[2:]:
        with open(path) as f:
            k = len(json.load(f))

        peso = math.sqrt((faixa + k) / n) - math.sqrt(faixa / n)
        print(f"  {os.path.basename(path):28s} {100*peso:5.1f}%")
        faixa += k

    print(f"\n{out}")


if __name__ == "__main__":
    main()
