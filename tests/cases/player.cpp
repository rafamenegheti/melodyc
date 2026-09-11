/*
  ==============================================================================

    player.cpp -- o tocador, e a pergunta que ele faz a frase.

    `heldAt` existe porque as tres camadas podem sair num canal MIDI so, que e o
    caso comum: um instrumento na mesma faixa, depois do plugin. Somadas, duas
    camadas que calham na mesma altura viram uma nota so para o instrumento, e o
    note-off da primeira corta a segunda. O sintoma nao e nota presa -- e o
    contrario, acorde que morre antes da hora sem nada no piano roll explicando.

  ==============================================================================
*/

#include <cstdio>

#include "core/Player.h"
#include "tests/Test.h"

namespace
{

/** Duas camadas na MESMA altura, sobrepostas: melodia dos tempos 0 a 2, acorde
    de 1 a 3. E a colisao que o canal unico cria. */
melody::Phrase colisao()
{
    melody::Phrase p;

    p.melody[0] = { 0, 72, 8, 100 };        // 8 semicolcheias = 2 tempos
    p.melodyCount = 1;

    p.chords[0] = { 4, 72, 8, 84 };         // comeca no tempo 1, acaba no 3
    p.chordCount = 1;

    p.bass[0] = { 0, 48, 16, 96 };
    p.bassCount = 1;

    return p;
}

constexpr unsigned todas = 0b111u;
constexpr unsigned soMelodia = 1u << (int) melody::Layer::melody;

} // namespace

TEST_CASE (player_altura_segurada_por_outra_camada)
{
    const auto p = colisao();

    // No tempo 2 a melodia acaba e o acorde ainda segura o mesmo do5.
    CHECK (melody::heldAt (p, 72, 2.0, todas));

    // Olhando so a melodia, ninguem segura -- que e o modo SEPARADO, em que
    // cada camada tem canal proprio e a colisao nao existe.
    CHECK (! melody::heldAt (p, 72, 2.0, soMelodia));

    // No tempo 3 o acorde tambem acabou.
    CHECK (! melody::heldAt (p, 72, 3.0, todas));

    // UMA NOTA NAO SEGURA A SI MESMA: no instante em que ela acaba, ela ja nao
    // conta. Sem isso, nenhum note-off sairia nunca e toda nota ficaria presa.
    CHECK (! melody::heldAt (p, 48, 4.0, todas));
    CHECK (melody::heldAt (p, 48, 3.9, todas));

    // Altura que nao existe na frase.
    CHECK (! melody::heldAt (p, 61, 1.0, todas));
}

//==============================================================================
/** Uma volta inteira liga e desliga cada nota exatamente uma vez. */
TEST_CASE (player_uma_volta_e_equilibrada)
{
    const auto p = colisao();

    int ons = 0, offs = 0;

    melody::advance (p, 0.0, p.loopBeats(),
                     [&ons] (melody::Layer, int, int, double) { ++ons; },
                     [&offs] (melody::Layer, int, double) { ++offs; });

    std::printf ("      %d ligar, %d desligar numa volta\n", ons, offs);

    CHECK (ons == 3);
    CHECK (offs == 3);
}

/** A janela pode cruzar o fim do laco, e a volta seguinte tem de comecar
    sozinha -- e o caso de um bloco grande num andamento alto. */
TEST_CASE (player_atravessa_a_volta)
{
    const auto p = colisao();

    int ons = 0;

    melody::advance (p, p.loopBeats() - 0.5, p.loopBeats() + 2.0,
                     [&ons] (melody::Layer, int, int, double at)
                     {
                         CHECK (at >= 0.0);
                         ++ons;
                     },
                     [] (melody::Layer, int, double) {});

    std::printf ("      %d ligar atravessando o fim do laco\n", ons);

    // Tempo 0 (melodia e baixo) e tempo 1 (acorde) da volta nova.
    CHECK (ons == 3);
}

/** Janela vazia ou invertida nao emite nada, em vez de percorrer o laco ao
    contrario. */
TEST_CASE (player_janela_vazia_nao_emite)
{
    const auto p = colisao();

    int eventos = 0;

    auto conta = [&eventos] (melody::Layer, int, int, double) { ++eventos; };
    auto contaOff = [&eventos] (melody::Layer, int, double) { ++eventos; };

    melody::advance (p, 4.0, 4.0, conta, contaOff);
    melody::advance (p, 8.0, 2.0, conta, contaOff);

    CHECK (eventos == 0);

    // E uma frase vazia tambem nao.
    melody::Phrase vazia;
    melody::advance (vazia, 0.0, 16.0, conta, contaOff);

    CHECK (eventos == 0);
}
