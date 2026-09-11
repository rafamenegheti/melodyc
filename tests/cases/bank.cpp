/*
  ==============================================================================

    bank.cpp -- o blob e lido certo, e um blob quebrado nao derruba nada.

    O caso que importa nao e o feliz: e o truncado. Um blob cortado passa pela
    magica e devolve deslocamento apontando para fora do buffer -- e a leitura
    acontece no thread de audio, onde nao ha pilha para olhar depois.

  ==============================================================================
*/

#include <cstdio>
#include <vector>

#include "core/Bank.h"
#include "tests/Test.h"

namespace
{

std::vector<unsigned char> readBank()
{
    std::vector<unsigned char> out;

    if (std::FILE* f = std::fopen (MELODY_BANK_PATH, "rb"))
    {
        std::fseek (f, 0, SEEK_END);
        const long n = std::ftell (f);
        std::fseek (f, 0, SEEK_SET);

        out.resize ((std::size_t) (n > 0 ? n : 0));

        if (! out.empty())
            if (std::fread (out.data(), 1, out.size(), f) != out.size())
                out.clear();

        std::fclose (f);
    }

    return out;
}

} // namespace

TEST_CASE (bank_carrega)
{
    const auto blob = readBank();
    CHECK (! blob.empty());

    melody::Bank b;
    CHECK (b.load (blob.data(), blob.size()));
    CHECK (b.valid());
    CHECK (b.size() > 1000);

    std::printf ("      %d trechos, %.0f KB\n", b.size(), blob.size() / 1024.0);
}

TEST_CASE (bank_recusa_lixo)
{
    melody::Bank b;

    CHECK (! b.load (nullptr, 0));

    const unsigned char naoEBanco[16] = { 'X', 'X', 'X', 'X' };
    CHECK (! b.load (naoEBanco, sizeof (naoEBanco)));
    CHECK (! b.valid());

    // Truncado: a magica esta certa e o arquivo acabou no meio.
    const auto blob = readBank();

    if (blob.size() > 64)
    {
        CHECK (! b.load (blob.data(), 64));
        CHECK (! b.valid());

        CHECK (! b.load (blob.data(), blob.size() / 2));
        CHECK (! b.valid());
    }

    // E depois de recusar, `at` continua devolvendo trecho invalido em vez de
    // desreferenciar nulo.
    CHECK (! b.at (0).valid());
    CHECK (! b.at (-1).valid());
    CHECK (! b.at (999999).valid());
}

TEST_CASE (bank_trechos_sao_coerentes)
{
    const auto blob = readBank();
    melody::Bank b;

    if (! b.load (blob.data(), blob.size()))
        return;

    int comAcorde = 0, comBaixo = 0, menores = 0;
    int piorPos = 0, piorRel = 0;

    for (int i = 0; i < b.size(); ++i)
    {
        const auto s = b.at (i);
        CHECK (s.valid());

        if (s.chordCount() > 0) ++comAcorde;
        if (s.bassCount() > 0)  ++comBaixo;
        if (s.minor())          ++menores;

        CHECK (s.melodyCount() > 0);
        CHECK (s.bpm() >= 60);
        CHECK (s.bars() == melody::shortBars || s.bars() == melody::longBars);

        for (int n = 0; n < s.melodyCount(); ++n)
        {
            const auto step = s.melody (n);

            // Toda nota mora dentro dos quatro compassos e dura alguma coisa.
            CHECK (step.pos >= 0 && step.pos < s.steps());
            CHECK (step.dur >= 1 && step.dur <= melody::maxSteps);
            CHECK (step.vel > 0 && step.vel <= 127);

            if (step.pos > piorPos) piorPos = step.pos;
            if (step.rel > piorRel) piorRel = step.rel;
        }

        for (int n = 0; n < s.bassCount(); ++n)
        {
            const auto step = s.bass (n);
            CHECK (step.pos >= 0 && step.pos < s.steps());
            CHECK (step.dur >= 1);
        }

        const int limite = s.steps();

        s.forEachChord ([limite] (int pos, int dur, const unsigned char* rels, int voices)
        {
            CHECK (pos >= 0 && pos < limite);
            CHECK (dur >= 1);
            CHECK (voices >= 1 && voices <= melody::maxVoices);
            CHECK (rels != nullptr);
        });
    }

    std::printf ("      %d menores, %d com acorde, %d com baixo   pos max %d, rel max %d\n",
                 menores, comAcorde, comBaixo, piorPos, piorRel);

    CHECK (menores > b.size() / 3);
    CHECK (comAcorde > b.size() / 2);
}
