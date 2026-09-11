/*
  ==============================================================================

    Bank.h -- o banco de trechos, lido direto do blob.

    3.291 trechos de quatro compassos, minerados de 2.062 MIDIs de kits reais.
    Cada um traz melodia, baixo e harmonia COMO ESTAVAM NO ARQUIVO -- e vieram
    juntos, entao combinam por construcao e nao por regra.

    ---------------------------------------------------------------------------
    AS ALTURAS SAO SEMITONS ACIMA DA TONICA, E NAO GRAUS DE ESCALA.

    Foi a decisao que mudou o som. Grau de escala reescreve o intervalo na hora
    de transpor: uma terca menor vira maior conforme a escala destino, e frase
    real vira frase generica. Guardando semitom, transpor e uma soma -- a frase
    soa EXATAMENTE como soava no arquivo de onde saiu. O preco e ter banco de
    maior e banco de menor separados, e o preco vale.

    ---------------------------------------------------------------------------
    NADA E PARSEADO NO CARREGAMENTO.

    `load` confere o cabecalho e guarda um ponteiro. A tabela de deslocamentos
    faz de `at(i)` uma soma, e um `Segment` e um ponteiro dentro do blob -- nao
    ha copia, nao ha alocacao, e o blob pode vir do BinaryData sem passar por
    memoria de heap nenhuma.

  ==============================================================================
*/

#pragma once

#include <cstdint>
#include <cstddef>

#include "core/Config.h"

namespace melody
{

//==============================================================================
/** Uma nota do banco: posicao e duracao em semicolcheias, altura em semitons
    acima da tonica. */
struct Step
{
    int pos = 0;
    int rel = 0;
    int dur = 1;
    int vel = 100;
};

//==============================================================================
/** Uma janela sobre um trecho dentro do blob. Copiar e copiar um ponteiro. */
class Segment
{
public:
    Segment() = default;
    explicit Segment (const std::uint8_t* p) noexcept : d (p) {}

    bool valid() const noexcept { return d != nullptr; }

    bool  minor()      const noexcept { return d[0] != 0; }
    float repetition() const noexcept { return (float) d[1] * (1.0f / 255.0f); }
    float rest()       const noexcept { return (float) d[2] * (1.0f / 255.0f); }
    float confidence() const noexcept { return (float) d[3] * (1.0f / 255.0f); }
    int   bpm()        const noexcept { return (int) d[4]; }

    /** 4 ou 8. O trecho carrega o proprio comprimento; nada no gerador supoe
        um numero fixo de compassos. */
    int   bars()       const noexcept { return (int) d[5]; }
    int   steps()      const noexcept { return stepsFor (bars()); }

    int melodyCount() const noexcept { return (int) d[6]; }
    int chordCount()  const noexcept { return (int) d[7]; }
    int bassCount()   const noexcept { return (int) d[8]; }

    Step melody (int i) const noexcept
    {
        const std::uint8_t* p = d + header + 4 * i;
        return { p[0], p[1], p[2], p[3] };
    }

    Step bass (int i) const noexcept
    {
        const std::uint8_t* p = d + header + 4 * melodyCount() + 3 * i;
        return { p[0], p[1], p[2], 96 };
    }

    /** O acorde e a unica camada de tamanho variavel, entao e a unica que se
        percorre. `fn (pos, dur, rels, count)`. */
    template <typename Fn>
    void forEachChord (Fn&& fn) const
    {
        const std::uint8_t* p = d + header + 4 * melodyCount() + 3 * bassCount();

        for (int i = 0, n = chordCount(); i < n; ++i)
        {
            const int pos = p[0];
            const int dur = p[1];
            const int voices = p[2];

            fn (pos, dur, p + 3, voices);
            p += 3 + voices;
        }
    }

    /** A menor e a maior altura do bloco inteiro -- e o que decide a oitava. */
    void extent (int& lo, int& hi) const noexcept
    {
        lo = 127;
        hi = 0;

        for (int i = 0, n = melodyCount(); i < n; ++i)
        {
            const int r = melody (i).rel;
            if (r < lo) lo = r;
            if (r > hi) hi = r;
        }

        for (int i = 0, n = bassCount(); i < n; ++i)
        {
            const int r = bass (i).rel;
            if (r < lo) lo = r;
            if (r > hi) hi = r;
        }

        forEachChord ([&lo, &hi] (int, int, const std::uint8_t* rels, int count)
        {
            for (int v = 0; v < count; ++v)
            {
                if (rels[v] < lo) lo = rels[v];
                if (rels[v] > hi) hi = rels[v];
            }
        });

        if (lo > hi) { lo = 0; hi = 0; }
    }

    void melodyExtent (int& lo, int& hi) const noexcept
    {
        lo = 127;
        hi = 0;

        for (int i = 0, n = melodyCount(); i < n; ++i)
        {
            const int r = melody (i).rel;
            if (r < lo) lo = r;
            if (r > hi) hi = r;
        }

        if (lo > hi) { lo = melodyCentre; hi = melodyCentre; }
    }

private:
    static constexpr int header = 9;

    const std::uint8_t* d = nullptr;
};

//==============================================================================
class Bank
{
public:
    /** Confere o cabecalho e guarda o ponteiro. Nao copia e nao aloca; o blob
        precisa continuar vivo enquanto o banco existir. */
    bool load (const void* data, std::size_t size) noexcept
    {
        blob = nullptr;
        count = 0;

        const auto* p = static_cast<const std::uint8_t*> (data);

        if (p == nullptr || size < 8)
            return false;

        // MFB2: o cabecalho do trecho ganhou o byte de compassos. Um blob MFB1
        // tem de ser RECUSADO, e nao lido torto -- com o layout deslocado,
        // `nMel` cairia no byte de compassos e o gerador leria contagem de nota
        // onde ha 4 ou 8, sem nada apontando para a causa.
        if (p[0] != 'M' || p[1] != 'F' || p[2] != 'B' || p[3] != '2')
            return false;

        const std::uint32_t n = read32 (p + 4);

        // A tabela precisa CABER: um blob truncado passaria pela magica e
        // devolveria ponteiro para fora do buffer no primeiro sorteio.
        if ((std::size_t) n * 4 + 8 > size)
            return false;

        blob = p;
        bytes = size;
        count = (int) n;

        // E cada deslocamento tem de apontar para dentro. Conferir aqui custa
        // uma varredura de 3.291 inteiros uma vez; nao conferir custa um
        // estouro no thread de audio, que nao da pilha nenhuma.
        for (int i = 0; i < count; ++i)
            if ((std::size_t) offset (i) + 8 > size)
            {
                blob = nullptr;
                count = 0;
                return false;
            }

        return true;
    }

    bool valid() const noexcept { return blob != nullptr && count > 0; }
    int  size()  const noexcept { return count; }

    Segment at (int i) const noexcept
    {
        if (! valid() || i < 0 || i >= count)
            return {};

        return Segment (blob + offset (i));
    }

private:
    static std::uint32_t read32 (const std::uint8_t* p) noexcept
    {
        return (std::uint32_t) p[0] | ((std::uint32_t) p[1] << 8)
             | ((std::uint32_t) p[2] << 16) | ((std::uint32_t) p[3] << 24);
    }

    std::uint32_t offset (int i) const noexcept { return read32 (blob + 8 + 4 * i); }

    const std::uint8_t* blob = nullptr;
    std::size_t bytes = 0;
    int count = 0;
};

} // namespace melody
