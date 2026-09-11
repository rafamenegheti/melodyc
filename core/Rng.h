/*
  ==============================================================================

    Rng.h — sorteio barato e reprodutivel.

    xorshift32. Nao serve para criptografia e serve muito bem para decidir onde
    um grao nasce: e uma multiplicacao e tres deslocamentos, sem divisao e sem
    tabela, e cabe num registrador.

    Reprodutivel importa mais do que parece: com semente fixa, uma falha no
    teste do motor acontece de novo na proxima execucao. Um motor granular com
    sorteio nao reprodutivel produz bugs que somem quando voce vai olhar.

  ==============================================================================
*/

#pragma once

#include <cstdint>

namespace melody
{

struct Rng
{
    explicit Rng (std::uint32_t seed = 0x9E3779B9u) noexcept
    {
        // A SEMENTE E EMBARALHADA ANTES DE VALER.
        //
        // xorshift precisa de aquecimento: partindo de sementes proximas, as
        // primeiras saidas ficam proximas tambem. Isso apareceu no gerador de
        // melodia -- vinte sementes sequenciais produziam treze melodias
        // distintas onde a estatistica previa dezessete, porque o PRIMEIRO
        // sorteio (qual celula usar) era correlacionado entre elas.
        //
        // A mistura e a do splitmix32: multiplica, embaralha os bits altos com
        // os baixos, e repete. Duas sementes vizinhas saem completamente
        // diferentes.
        std::uint32_t z = seed + 0x9E3779B9u;

        z = (z ^ (z >> 16)) * 0x21F0AAADu;
        z = (z ^ (z >> 15)) * 0x735A2D97u;
        z = z ^ (z >> 15);

        state = z | 1u;

        // E mais alguns passos, para o estado deixar de lembrar da entrada.
        next(); next(); next();
    }

    std::uint32_t next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** [0, 1) */
    float unipolar() noexcept
    {
        return (float) (next() >> 8) * (1.0f / 16777216.0f);
    }

    /** (-1, 1) */
    float bipolar() noexcept { return unipolar() * 2.0f - 1.0f; }

    bool coin() noexcept { return (next() & 0x10000u) != 0u; }

    /** [0, n) */
    int below (int n) noexcept
    {
        return n <= 1 ? 0 : (int) (unipolar() * (float) n) % n;
    }

    /** Distribuicao de cauda pesada: quase sempre perto de zero, de vez em
        quando bem longe. E o que separa "disperso" de "aleatorio uniforme" --
        o enxame precisa de saltos ocasionais, nao de ruido constante. */
    float heavyTail() noexcept
    {
        const float u = unipolar();
        return u * u * u;
    }

    std::uint32_t state;
};

} // namespace melody
