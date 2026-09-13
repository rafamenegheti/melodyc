/*
  ==============================================================================

    Params.h -- os identificadores e o layout.

    O id de parametro e o unico nome que atravessa tudo: a automacao do usuario,
    o estado salvo do projeto, o MIDI learn, a interface. Um id escrito a mao em
    dois lugares e uma automacao que morre em silencio na proxima versao. Por
    isso cada um existe UMA vez, aqui.

    A SEMENTE NAO E PARAMETRO. Ela e estado salvo, e nao um valor automatizavel:
    automacao interpola, e uma semente interpolada entre 3.100 e 3.200 nao e uma
    melodia intermediaria, e uma melodia sem relacao nenhuma com as duas. O que
    o usuario automatiza e tom, escala, oitava e volume das camadas -- coisas em
    que "no meio do caminho" quer dizer alguma coisa.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "core/Config.h"
#include "core/Player.h"

//==============================================================================
namespace pid
{
    inline constexpr const char* key       = "key";
    inline constexpr const char* scale     = "scale";
    inline constexpr const char* source    = "source";

    inline constexpr const char* melodyOn  = "melodyOn";
    inline constexpr const char* chordsOn  = "chordsOn";
    inline constexpr const char* bassOn    = "bassOn";

    inline constexpr const char* melodyVol = "melodyVol";
    inline constexpr const char* chordsVol = "chordsVol";
    inline constexpr const char* bassVol   = "bassVol";

    inline constexpr const char* internal  = "internal";
    inline constexpr const char* routing   = "routing";
    inline constexpr const char* length    = "length";
}

//==============================================================================
/** De onde vem a harmonia que toca embaixo da melodia.

    UNICO: as tres camadas saem do MESMO arquivo, como o produtor escreveu.
    Combinam por construcao, porque alguem escreveu elas juntas -- e o limite e
    que os trechos sao os que sao, e acabou. E o PADRAO: e a aposta segura, e a
    primeira frase que a pessoa ouve decide se o plugin presta.

    MIXADO: a melodia de um arquivo com a harmonia e o baixo de outro, aceito so
    acima do limiar de encaixe medido em core/Generator.h. Abre muito mais
    combinacao, ao custo de a harmonia nao ter sido escrita para aquela melodia.

    Os rotulos sao ASCII sem acento, como o resto da janela ("SAIDA", "TOM E
    ESCALA"): literal UTF-8 cru sai como mojibake, e abrir excecao para um
    rotulo so deixaria a janela desalinhada com ela mesma. */
namespace Source
{
    enum Id { whole = 0, recombined, count };

    inline const char* name (int i) noexcept
    {
        static const char* n[count] = { "UNICO", "MIXADO" };
        return n[i < 0 ? 0 : (i >= count ? count - 1 : i)];
    }
}

/** JUNTO ou SEPARADO: vale para o MIDI que sai ao vivo E para o arquivo que
    sai do arrastar.

    Deixou de ser um controle solto e virou o menu do proprio botao que exporta.
    A escolha so importa na hora de mandar a frase para algum lugar, e e la que
    ela agora mora.

    TUDO EM 1 e o padrao, e o padrao e o caso comum: um instrumento so, na mesma
    faixa, depois do plugin. Instrumento monotimbral costuma escutar o canal 1 e
    ignorar o resto -- com as camadas separadas, sairia so a melodia e o usuario
    concluiria que o baixo e o acorde nao estao sendo gerados.

    SEPARADO existe para quem quer cada camada num instrumento diferente, que e
    o motivo de as camadas terem canal proprio. No FL, o canal ainda escolhe para
    onde o MIDI out vai. */
namespace Routing
{
    enum Id { single = 0, split, count };

    inline const char* name (int i) noexcept
    {
        static const char* n[count] = { "JUNTO", "SEPARADO" };
        return n[i < 0 ? 0 : (i >= count ? count - 1 : i)];
    }
}

/** O comprimento da frase.

    Os dois sao minerados inteiros do corpus -- 3.274 trechos de quatro
    compassos e 1.795 de oito. Nao ha oito montado a partir de dois de quatro:
    colar seria inventar a forma da frase por cima de material emprestado, que e
    o erro da terceira tentativa. */
namespace Length
{
    enum Id { four = 0, eight, count };

    inline const char* name (int i) noexcept
    {
        static const char* n[count] = { "4 COMPASSOS", "8 COMPASSOS" };
        return n[i < 0 ? 0 : (i >= count ? count - 1 : i)];
    }
}

namespace ScaleChoice
{
    enum Id { major = 0, minor, count };

    inline const char* name (int i) noexcept
    {
        static const char* n[count] = { "MAIOR", "MENOR" };
        return n[i < 0 ? 0 : (i >= count ? count - 1 : i)];
    }
}

/** ASCII puro: literal UTF-8 cru sai como mojibake na janela. */
inline const char* keyName (int i) noexcept
{
    static const char* n[12] = { "C", "C#", "D", "D#", "E", "F",
                                 "F#", "G", "G#", "A", "A#", "B" };
    return n[i < 0 ? 0 : (i >= 12 ? 11 : i)];
}

//==============================================================================
inline juce::AudioProcessorValueTreeState::ParameterLayout makeLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    StringArray keys;
    for (int i = 0; i < 12; ++i)
        keys.add (keyName (i));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::key, 1 }, "Tom", keys, 9));          // la, o tom do genero

    StringArray scales;
    for (int i = 0; i < ScaleChoice::count; ++i)
        scales.add (ScaleChoice::name (i));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::scale, 1 }, "Escala", scales, ScaleChoice::minor));

    StringArray lengths;
    for (int i = 0; i < Length::count; ++i)
        lengths.add (Length::name (i));

    // OITO E O PADRAO.
    //
    // Quatro compassos e o trecho mais curto que se sustenta sozinho, e por isso
    // era o padrao. Oito e uma IDEIA: tem primeira metade, volta e desfecho, que
    // e o que a pessoa vai levar para a faixa -- e o material de oito existe
    // inteiro no corpus, minerado, e nao e quatro colado duas vezes.
    //
    // O custo esta medido e aceito: o pool de oito e menor que o de quatro (938
    // menores e 590 maiores, contra 3.428 e 2.210). Sao menos resultados
    // distintos antes de repetir, e quem quiser o campo maior troca em um clique
    // -- o controle esta na primeira barra, ao lado.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::length, 1 }, "Compassos", lengths, Length::eight));

    StringArray sources;
    for (int i = 0; i < Source::count; ++i)
        sources.add (Source::name (i));

    // UNICO E O PADRAO.
    //
    // Abrir no MIXADO mostra primeiro o modo que tem mais variacao -- e tambem
    // o que erra mais, porque a harmonia vem de outro arquivo e so o limiar de
    // encaixe garante que combina. A primeira frase que a pessoa ouve e a que
    // decide se o plugin presta, e nela vale mais a aposta segura: tres camadas
    // que alguem escreveu juntas. Quem quiser abrir a variacao troca em um
    // clique, e o controle esta na primeira barra, visivel.
    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::source, 1 }, "Fonte", sources, Source::whole));

    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::melodyOn, 1 }, "Melodia", true));
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::chordsOn, 1 }, "Acordes", true));
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::bassOn, 1 }, "Baixo", true));

    const NormalisableRange<float> vol { 0.0f, 1.0f, 0.001f };

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::melodyVol, 1 }, "Volume melodia", vol, 0.85f));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::chordsVol, 1 }, "Volume acordes", vol, 0.55f));
    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { pid::bassVol, 1 }, "Volume baixo", vol, 0.80f));

    StringArray routes;
    for (int i = 0; i < Routing::count; ++i)
        routes.add (Routing::name (i));

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { pid::routing, 1 }, "Canal MIDI", routes, Routing::single));

    // O som interno e de conferencia. Quem ja roteou o MIDI para o proprio
    // sampler desliga aqui e para de ouvir as duas coisas somadas.
    layout.add (std::make_unique<AudioParameterBool> (
        ParameterID { pid::internal, 1 }, "Som interno", true));

    return layout;
}
