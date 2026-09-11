/*
  ==============================================================================

    Generator.h -- escolher um trecho real e transpo-lo. Nada alem disso.

    Tres tentativas anteriores inventaram a frase e as tres falharam, cada uma
    de um jeito que ensinou a seguinte:

      REGRAS ESCRITAS A MAO (passo pequeno, contorno suave, celula ritmica
      escolhida a dedo) deram "uma sequencia de notas uma atras da outra que nem
      sequer combinam".

      ESTATISTICA DE 2.108 MIDIS -- metade dos intervalos e salto, o ritmo mora
      na colcheia -- deu numeros certos e som errado: distribuicao agregada nao
      captura SEQUENCIA. Sorteando nota a nota, cada nota fica plausivel e a
      frase inteira nao e nada.

      CELULAS REAIS DE UM COMPASSO montadas num arranjo A-B-A'-C melhorou, e a
      forma da frase continuava sendo invencao minha por cima de material
      emprestado.

    Aqui a frase inteira vem do arquivo -- com a propria repeticao, a propria
    variacao e a propria resolucao, que era exatamente o que eu vinha imitando
    mal. Gerar virou escolher e somar.

    ---------------------------------------------------------------------------
    O QUE E DE FATO GERADO SAO TRES COISAS.

    A OITAVA, por custo. Centrar so pela melodia empurrava o sub para a nota 18
    quando o trecho abria cinco oitavas. As oitavas candidatas sao pontuadas e
    ganha a menos pior: nota fora da faixa tocavel pesa dez, melodia fora do
    registro de melodia pesa tres, e o resto e distancia do centro.

    A RECOMBINACAO, com prova. Melodia de um trecho sobre a harmonia de outro,
    aceita so acima de um limiar de encaixe medido nota a nota e PONDERADO PELA
    DURACAO -- semicolcheia fora do acorde e ornamento, minima fora do acorde e
    erro, e contar notas trata as duas igual.

    O SORTEIO, enviesado para a frente do banco. O blob ja vem ordenado por
    qualidade, entao a curva no indice e a unica coisa que o gerador precisa
    saber sobre "melhor" -- o criterio mora no empacotador, onde da para medir.

  ==============================================================================
*/

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#include "core/Bank.h"
#include "core/Config.h"
#include "core/Rng.h"

namespace melody
{

//==============================================================================
constexpr unsigned majorMask = 0b101010110101u;   // 0 2 4 5 7 9 11
constexpr unsigned minorMask = 0b010110101101u;   // 0 2 3 5 7 8 10

inline bool inScale (int pitchClass, bool minor) noexcept
{
    const unsigned m = minor ? minorMask : majorMask;
    return ((m >> (((pitchClass % 12) + 12) % 12)) & 1u) != 0u;
}

//==============================================================================
/** Uma nota pronta: altura MIDI absoluta, posicao e duracao em semicolcheias. */
struct Voice
{
    int pos = 0;
    int pitch = 60;
    int dur = 1;
    int vel = 100;
};

/** O resultado. Tamanho fixo: nada aqui aloca, entao gerar pode acontecer no
    clique sem tirar o audio do lugar. */
struct Phrase
{
    Voice melody[maxEvents];
    Voice bass[maxEvents];
    Voice chords[maxChordVoices];

    int melodyCount = 0;
    int bassCount = 0;
    int chordCount = 0;

    int  root = 0;              // 0-11
    bool minor = true;
    int  bpm = 140;

    /** 4 ou 8, e vem do trecho. O laco tem o comprimento da frase, e nao um
        numero fixo em algum lugar do tocador. */
    int  bars = shortBars;

    int steps() const noexcept { return stepsFor (bars); }
    double loopBeats() const noexcept { return (double) steps() * 0.25; }

    int   melodyIndex = -1;     // qual trecho deu a melodia
    int   harmonyIndex = -1;    // e qual deu a harmonia (igual, se nao recombinou)
    float fit = 1.0f;

    bool empty() const noexcept { return melodyCount == 0; }

    int lowest() const noexcept
    {
        int lo = 127;
        for (int i = 0; i < melodyCount; ++i) lo = melody[i].pitch < lo ? melody[i].pitch : lo;
        for (int i = 0; i < bassCount; ++i)   lo = bass[i].pitch   < lo ? bass[i].pitch   : lo;
        for (int i = 0; i < chordCount; ++i)  lo = chords[i].pitch < lo ? chords[i].pitch : lo;
        return lo == 127 ? 60 : lo;
    }

    int highest() const noexcept
    {
        int hi = 0;
        for (int i = 0; i < melodyCount; ++i) hi = melody[i].pitch > hi ? melody[i].pitch : hi;
        for (int i = 0; i < bassCount; ++i)   hi = bass[i].pitch   > hi ? bass[i].pitch   : hi;
        for (int i = 0; i < chordCount; ++i)  hi = chords[i].pitch > hi ? chords[i].pitch : hi;
        return hi == 0 ? 72 : hi;
    }
};

//==============================================================================
class Generator
{
public:
    /** Separa os indices por modo, uma vez. A ordem de qualidade do blob e
        preservada dentro de cada lista, entao o vies no sorteio continua
        valendo depois da separacao. */
    void setBank (const Bank& b)
    {
        bank = &b;

        for (auto& p : pools)
            p.clear();

        if (! b.valid())
            return;

        for (int i = 0; i < b.size(); ++i)
        {
            const auto s = b.at (i);
            pools[slot (s.minor(), s.bars())].push_back (i);
        }
    }

    bool ready() const noexcept
    {
        if (bank == nullptr || ! bank->valid())
            return false;

        for (const auto& p : pools)
            if (! p.empty())
                return true;

        return false;
    }

    int count (bool wantMinor, int wantBars) const noexcept
    {
        return (int) pools[slot (wantMinor, wantBars)].size();
    }

    //==========================================================================
    /** `octave` desloca o bloco inteiro em oitavas, depois da escolha. */
    void generate (Phrase& out, int root, bool wantMinor, std::uint32_t seed,
                   bool recombine, int octave = 0, int wantBars = shortBars) const
    {
        out.melodyCount = 0;
        out.bassCount = 0;
        out.chordCount = 0;
        out.root = ((root % 12) + 12) % 12;
        out.minor = wantMinor;
        out.bars = wantBars >= longBars ? longBars : shortBars;
        out.fit = 1.0f;
        out.melodyIndex = -1;
        out.harmonyIndex = -1;

        if (! ready())
            return;

        // A HARMONIA VEM DO MESMO COMPRIMENTO. Recombinar uma melodia de oito
        // compassos com uma harmonia de quatro deixaria a segunda metade sem
        // acorde nenhum -- e o gerador nao repete a harmonia para tapar, porque
        // repetir seria inventar a forma.
        const auto& pool = pools[slot (wantMinor, out.bars)];

        if (pool.empty())
            return;

        Rng rng { seed };

        const int melIdx = pool[(std::size_t) biased (rng, (int) pool.size())];
        Segment mel = bank->at (melIdx);

        int harmIdx = melIdx;
        Segment harm = mel;
        float fit = 1.0f;

        if (recombine)
        {
            // Ate vinte tentativas. Se nenhuma passar, fica a harmonia que veio
            // junto com a melodia -- que sempre encaixa, porque veio junto.
            for (int t = 0; t < 20; ++t)
            {
                const int cand = pool[(std::size_t) biased (rng, (int) pool.size())];

                if (cand == melIdx)
                    continue;

                Segment h = bank->at (cand);

                if (h.chordCount() == 0)
                    continue;

                const float f = fitness (mel, h, wantMinor);

                if (f >= fitThreshold)
                {
                    harmIdx = cand;
                    harm = h;
                    fit = f;
                    break;
                }
            }
        }

        out.melodyIndex = melIdx;
        out.harmonyIndex = harmIdx;
        out.fit = fit;
        out.bpm = mel.bpm();
        out.bars = mel.bars();

        const int base = chooseBase (mel, harm, out.root, octave);

        for (int i = 0, n = mel.melodyCount(); i < n && out.melodyCount < maxEvents; ++i)
        {
            const Step s = mel.melody (i);
            out.melody[out.melodyCount++] = { s.pos, base + s.rel, s.dur, s.vel };
        }

        for (int i = 0, n = harm.bassCount(); i < n && out.bassCount < maxEvents; ++i)
        {
            const Step s = harm.bass (i);
            out.bass[out.bassCount++] = { s.pos, base + s.rel, s.dur, 96 };
        }

        harm.forEachChord ([&out, base] (int pos, int dur, const std::uint8_t* rels, int voices)
        {
            for (int v = 0; v < voices; ++v)
                if (out.chordCount < maxChordVoices)
                    out.chords[out.chordCount++] = { pos, base + rels[v], dur, 84 };
        });
    }

    //==========================================================================
    /** Quanto a melodia cabe na harmonia, ponderado pela duracao.

        Publico porque e a medida que o teste confere e a janela mostra. */
    static float fitness (const Segment& mel, const Segment& harm, bool wantMinor)
    {
        double total = 0.0;
        double good = 0.0;

        for (int i = 0, n = mel.melodyCount(); i < n; ++i)
        {
            const Step s = mel.melody (i);
            const int pc = ((s.rel % 12) + 12) % 12;

            unsigned sounding = 0;

            harm.forEachChord ([&sounding, &s] (int pos, int dur,
                                                const std::uint8_t* rels, int voices)
            {
                if (pos < s.pos + s.dur && pos + dur > s.pos)
                    for (int v = 0; v < voices; ++v)
                        sounding |= 1u << (rels[v] % 12);
            });

            double w;

            if (sounding == 0u)
                w = inScale (pc, wantMinor) ? 1.0 : 0.0;
            else if (((sounding >> pc) & 1u) != 0u)
                w = 1.0;
            else if (inScale (pc, wantMinor))
                w = 0.75;
            else
                w = 0.0;

            total += s.dur;
            good += w * s.dur;
        }

        return total > 0.0 ? (float) (good / total) : 0.0f;
    }

    static constexpr float fitThreshold = 0.82f;

private:
    //==========================================================================
    /** Vies para a frente do banco, que e o lado bom -- o blob vem ordenado por
        qualidade. Elevar a sorte ao quadrado nao exclui nada: o trecho 3.290
        continua saindo, so sai menos. */
    static int biased (Rng& rng, int n) noexcept
    {
        if (n <= 1)
            return 0;

        const float u = rng.unipolar();
        const int i = (int) (u * u * (float) n);
        return i < 0 ? 0 : (i >= n ? n - 1 : i);
    }

    /** A oitava em que o bloco inteiro vai morar, por custo. */
    static int chooseBase (const Segment& mel, const Segment& harm,
                           int root, int octave) noexcept
    {
        int melLo = 0, melHi = 0;
        mel.melodyExtent (melLo, melHi);

        const double mid = 0.5 * (melLo + melHi);
        const int k0 = (int) std::lround (((double) melodyCentre - root - mid) / 12.0);

        int best = root + 12 * (k0 + octave);
        double bestCost = 1.0e18;

        for (int k = k0 - 3; k <= k0 + 3; ++k)
        {
            const int base = root + 12 * (k + octave);

            int outside = 0;
            int stray = 0;

            for (int i = 0, n = mel.melodyCount(); i < n; ++i)
            {
                const int p = base + mel.melody (i).rel;
                if (p < lowestNote || p > highestNote) ++outside;
                if (p < melodyLow  || p > melodyHigh)  ++stray;
            }

            for (int i = 0, n = harm.bassCount(); i < n; ++i)
            {
                const int p = base + harm.bass (i).rel;
                if (p < lowestNote || p > highestNote) ++outside;
            }

            harm.forEachChord ([&outside, base] (int, int, const std::uint8_t* rels, int voices)
            {
                for (int v = 0; v < voices; ++v)
                {
                    const int p = base + rels[v];
                    if (p < lowestNote || p > highestNote) ++outside;
                }
            });

            // Fora da faixa tocavel domina tudo. Com peso 10 o custo trocava
            // uma nota estourada por vinte e quatro semitons de centragem, e
            // 14 frases em 2.400 saiam com sub inaudivel ou lead gritando.
            // O minerador garante que existe oitava limpa (folga de 12
            // semitons), entao o peso alto nunca fica sem opcao.
            const double cost = 1000.0 * outside + 3.0 * stray
                                + std::fabs (base + mid - (double) melodyCentre);

            if (cost < bestCost)
            {
                bestCost = cost;
                best = base;
            }
        }

        return best;
    }

    /** Quatro listas: maior/menor x quatro/oito compassos. A ordem de qualidade
        do blob e preservada dentro de cada uma, entao o vies no sorteio
        continua valendo depois da separacao. */
    static int slot (bool isMinor, int bars) noexcept
    {
        return (isMinor ? 1 : 0) + (bars >= longBars ? 2 : 0);
    }

    const Bank* bank = nullptr;
    std::vector<int> pools[4];
};

} // namespace melody
