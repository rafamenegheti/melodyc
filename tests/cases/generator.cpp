/*
  ==============================================================================

    generator.cpp -- as propriedades que o gerador existe para ter.

    Nenhum destes casos julga se a melodia e bonita; nenhum teste julga. O que
    da para provar por maquina e o que ja falhou antes: altura fora da faixa
    tocavel, transposicao que reescreve o intervalo, semente que devolve sempre
    a mesma frase, e recombinacao que aceita o que nao encaixa.

  ==============================================================================
*/

#include <cmath>
#include <cstdio>
#include <set>
#include <vector>

#include "core/Generator.h"
#include "tests/Alloc.h"
#include "tests/Test.h"

namespace
{

std::vector<unsigned char>& blob()
{
    static std::vector<unsigned char> data = []
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
    }();

    return data;
}

struct Fixture
{
    melody::Bank bank;
    melody::Generator gen;

    Fixture()
    {
        bank.load (blob().data(), blob().size());
        gen.setBank (bank);
    }
};

} // namespace

//==============================================================================
TEST_CASE (gerador_fica_na_faixa_tocavel)
{
    Fixture f;
    CHECK (f.gen.ready());

    melody::Phrase p;
    int fora = 0, total = 0, melodiaExtrema = 0;

    for (unsigned s = 0; s < 400; ++s)
        for (int root = 0; root < 12; root += 4)
            for (const bool minor : { true, false })
            {
                f.gen.generate (p, root, minor, s * 2654435761u + (unsigned) root, false);

                if (p.empty())
                    continue;

                ++total;

                if (p.lowest() < melody::lowestNote || p.highest() > melody::highestNote)
                    ++fora;

                for (int i = 0; i < p.melodyCount; ++i)
                    if (p.melody[i].pitch < 45 || p.melody[i].pitch > 100)
                        ++melodiaExtrema;
            }

    std::printf ("      %d frases: %d fora da faixa, %d notas de melodia em registro extremo\n",
                 total, fora, melodiaExtrema);

    CHECK (total > 2000);
    CHECK (fora == 0);
    CHECK (melodiaExtrema == 0);
}

//==============================================================================
/** A tonica pedida tem de ser a tonica que sai.

    E a propriedade que o banco em semitons compra. Se alguem trocar o
    armazenamento por grau de escala para "economizar", este caso continua
    passando -- mas o proximo, nao. */
TEST_CASE (gerador_transpoe_para_o_tom_pedido)
{
    Fixture f;
    melody::Phrase a, b;

    for (unsigned s = 0; s < 60; ++s)
    {
        f.gen.generate (a, 0, true, s, false);      // do menor
        f.gen.generate (b, 7, true, s, false);      // sol menor

        CHECK (a.melodyCount == b.melodyCount);

        // Mesma semente, mesma frase: so a altura anda, e anda igual em todas
        // as notas e em todas as camadas.
        for (int i = 0; i < a.melodyCount && i < b.melodyCount; ++i)
        {
            CHECK (a.melody[i].pos == b.melody[i].pos);
            CHECK (a.melody[i].dur == b.melody[i].dur);
        }
    }
}

/** O INTERVALO E PRESERVADO NA TRANSPOSICAO.

    Guardar grau de escala reescreve o intervalo: uma terca menor vira maior
    conforme a escala destino. E o defeito que fez material real soar generico,
    e o unico jeito de pegar e comparar a sequencia de intervalos. */
TEST_CASE (gerador_preserva_os_intervalos)
{
    Fixture f;
    melody::Phrase a, b;
    int comparadas = 0;

    for (unsigned s = 0; s < 200; ++s)
        for (const bool minor : { true, false })
        {
            f.gen.generate (a, 2, minor, s, false);
            f.gen.generate (b, 9, minor, s, false);

            if (a.empty() || a.melodyCount != b.melodyCount)
                continue;

            ++comparadas;

            for (int i = 1; i < a.melodyCount; ++i)
                CHECK (a.melody[i].pitch - a.melody[i - 1].pitch
                         == b.melody[i].pitch - b.melody[i - 1].pitch);
        }

    std::printf ("      %d frases comparadas em dois tons\n", comparadas);
    CHECK (comparadas > 300);
}

//==============================================================================
/** Sementes vizinhas nao podem dar a mesma frase.

    A tentativa anterior tinha 24 frases embutidas: vinte sementes davam treze
    resultados distintos. E a reclamacao original -- "as melodias repetem" -- e
    o motivo de o banco existir. */
TEST_CASE (gerador_nao_repete)
{
    Fixture f;
    melody::Phrase p;

    std::set<int> distintas;

    for (unsigned s = 0; s < 200; ++s)
    {
        f.gen.generate (p, 9, true, s, false);
        distintas.insert (p.melodyIndex);
    }

    std::printf ("      200 sementes vizinhas -> %d melodias distintas\n",
                 (int) distintas.size());

    CHECK ((int) distintas.size() > 120);
}

//==============================================================================
/** A recombinacao so aceita o que passa no limiar, e o limiar mede de verdade. */
TEST_CASE (gerador_recombina_com_prova)
{
    Fixture f;
    melody::Phrase p;

    int recombinou = 0, abaixoDoLimiar = 0, total = 0;

    for (unsigned s = 0; s < 400; ++s)
    {
        f.gen.generate (p, 9, true, s, true);

        if (p.empty())
            continue;

        ++total;

        if (p.harmonyIndex != p.melodyIndex)
        {
            ++recombinou;

            if (p.fit < melody::Generator::fitThreshold)
                ++abaixoDoLimiar;
        }
    }

    std::printf ("      %d de %d recombinaram, %d abaixo do limiar\n",
                 recombinou, total, abaixoDoLimiar);

    CHECK (recombinou > total / 2);
    CHECK (abaixoDoLimiar == 0);

    // E uma melodia cromatica contra uma harmonia diatonica tem de reprovar --
    // senao o limiar nao esta medindo nada.
    int reprovados = 0;

    for (int i = 0; i < f.bank.size() && i < 400; ++i)
        for (int j = 0; j < f.bank.size() && j < 400; j += 37)
        {
            const auto m = f.bank.at (i);
            const auto h = f.bank.at (j);

            if (m.minor() != h.minor() || h.chordCount() == 0)
                continue;

            if (melody::Generator::fitness (m, h, m.minor())
                  < melody::Generator::fitThreshold)
                ++reprovados;
        }

    std::printf ("      pares reprovados na varredura: %d\n", reprovados);
    CHECK (reprovados > 0);
}

//==============================================================================
/** Oito compassos sao OITO COMPASSOS DE VERDADE, e nao quatro repetidos.

    Colar dois trechos de quatro daria a mesma duracao e nenhuma forma: a
    segunda metade seria identica a primeira, e o ouvido pega isso na primeira
    volta. O material de oito e minerado inteiro, entao a segunda metade tem de
    ser diferente da primeira na maioria das frases. */
TEST_CASE (gerador_oito_compassos)
{
    Fixture f;

    std::printf ("      banco: %d curtos menores, %d longos menores, "
                 "%d curtos maiores, %d longos maiores\n",
                 f.gen.count (true, melody::shortBars), f.gen.count (true, melody::longBars),
                 f.gen.count (false, melody::shortBars), f.gen.count (false, melody::longBars));

    CHECK (f.gen.count (true, melody::longBars) > 200);
    CHECK (f.gen.count (false, melody::longBars) > 100);

    melody::Phrase p;

    int total = 0, comSegundaMetade = 0, metadesIguais = 0, foraDaFaixa = 0;

    for (unsigned s = 0; s < 400; ++s)
        for (const bool minor : { true, false })
        {
            f.gen.generate (p, 9, minor, s * 7919u, false, 0, melody::longBars);

            if (p.empty())
                continue;

            ++total;

            CHECK (p.bars == melody::longBars);
            CHECK (p.steps() == 128);
            CHECK (std::abs (p.loopBeats() - 32.0) < 1.0e-9);

            if (p.lowest() < melody::lowestNote || p.highest() > melody::highestNote)
                ++foraDaFaixa;

            // Alguma nota mora depois do quarto compasso?
            bool tarde = false;

            for (int i = 0; i < p.melodyCount; ++i)
                if (p.melody[i].pos >= 64)
                    tarde = true;

            if (tarde)
                ++comSegundaMetade;

            // As duas metades sao a mesma coisa? A pergunta vale para o
            // BLOCO INTEIRO, e nao so para a melodia: melodia que repete com
            // harmonia diferente por baixo e desenvolvimento, nao repeticao --
            // e foi assim que o minerador decidiu o que descartar.
            auto metadesIdenticas = [] (const melody::Voice* v, int count)
            {
                int primeira = 0;

                for (int i = 0; i < count; ++i)
                    if (v[i].pos < 64)
                        ++primeira;

                if (primeira == 0 || primeira * 2 != count)
                    return false;

                for (int i = 0; i < primeira; ++i)
                    if (v[i].pitch != v[primeira + i].pitch
                        || v[i].pos + 64 != v[primeira + i].pos
                        || v[i].dur != v[primeira + i].dur)
                        return false;

                return true;
            };

            const bool igual = metadesIdenticas (p.melody, p.melodyCount)
                                 && metadesIdenticas (p.chords, p.chordCount)
                                 && metadesIdenticas (p.bass, p.bassCount);

            if (igual)
                ++metadesIguais;
        }

    std::printf ("      %d frases de oito: %d usam a segunda metade, "
                 "%d sao quatro repetidos, %d fora da faixa\n",
                 total, comSegundaMetade, metadesIguais, foraDaFaixa);

    CHECK (total > 500);
    CHECK (foraDaFaixa == 0);
    CHECK (comSegundaMetade == total);
    CHECK (metadesIguais == 0);
}

/** Pedir quatro nao pode devolver oito, nem o contrario. */
TEST_CASE (gerador_respeita_o_comprimento)
{
    Fixture f;
    melody::Phrase p;

    for (unsigned s = 0; s < 200; ++s)
    {
        f.gen.generate (p, 0, true, s, true, 0, melody::shortBars);
        CHECK (p.bars == melody::shortBars);

        for (int i = 0; i < p.melodyCount; ++i)
            CHECK (p.melody[i].pos < 64);

        for (int i = 0; i < p.chordCount; ++i)
            CHECK (p.chords[i].pos < 64);

        f.gen.generate (p, 0, true, s, true, 0, melody::longBars);
        CHECK (p.bars == melody::longBars);

        for (int i = 0; i < p.melodyCount; ++i)
            CHECK (p.melody[i].pos < 128);
    }
}

//==============================================================================
/** Gerar nao aloca.

    O botao GENERATE e clicavel com o transporte rodando, e a frase publicada
    e lida pelo thread de audio. Uma alocacao aqui e um risco de estouro de
    prazo que so aparece na maquina do usuario, com o projeto cheio. */
TEST_CASE (gerador_nao_aloca)
{
    Fixture f;
    melody::Phrase p;

    f.gen.generate (p, 0, true, 1, true);   // aquece

    test::NoAllocations guard;

    for (unsigned s = 0; s < 200; ++s)
        f.gen.generate (p, (int) s % 12, (s & 1u) != 0u, s * 7919u, (s & 2u) != 0u, 0,
                        (s & 4u) != 0u ? melody::longBars : melody::shortBars);

    std::printf ("      %d alocacoes em 200 geracoes\n", guard.count());
    CHECK (guard.count() == 0);
}
