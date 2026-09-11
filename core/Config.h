/*
  ==============================================================================

    Config.h -- as constantes da grade.

    Quatro compassos de dezesseis semicolcheias, e nao "um numero de passos
    configuravel". O banco inteiro foi minerado nessa grade: cada `pos` guardado
    e uma semicolcheia de 0 a 63, e mudar a grade aqui invalidaria o blob sem
    quebrar a compilacao -- que e a pior forma de quebrar.

  ==============================================================================
*/

#pragma once

namespace melody
{

constexpr int stepsPerBar = 16;

/** DOIS COMPRIMENTOS, e o trecho diz qual e o dele.

    Nao ha "montar oito a partir de dois de quatro": colar dois trechos seria
    inventar a forma da frase por cima de material emprestado, que e o erro da
    terceira tentativa descrito no cabecalho de Generator.h. Os dois
    comprimentos sao minerados inteiros -- o corpus tem 1.486 arquivos de oito
    compassos contra 453 de quatro, entao o material existe pronto. */
constexpr int shortBars = 4;
constexpr int longBars  = 8;
constexpr int maxBars   = longBars;
constexpr int maxSteps  = stepsPerBar * maxBars;   // 128

constexpr int stepsFor (int bars) noexcept { return stepsPerBar * bars; }

/** Faixa tocavel, em nota MIDI. Mi1 a mi7.

    Nao e gosto: 33-96 era o limite anterior e forcava a escolha errada de
    oitava. Trecho com lead agudo e sub embaixo abre cinco oitavas de verdade e
    nao cabia em oitava nenhuma -- o custo entao preferia subir a melodia
    inteira para o teto, e a melodia saia gritando em vez do sub sumindo. */
constexpr int lowestNote  = 28;
constexpr int highestNote = 100;

/** Onde a melodia ainda soa como melodia, e nao como assobio ou como baixo. */
constexpr int melodyLow    = 55;
constexpr int melodyHigh   = 92;
constexpr int melodyCentre = 74;

/** Medido no banco: melodia de oito compassos chega a 128 notas, o baixo a 64,
    e as vozes de acorde somadas a 200. Os limites sao esses numeros com folga,
    e nao chutes redondos. */
constexpr int maxEvents = 128;
constexpr int maxVoices = 8;
constexpr int maxChordVoices = 256;

} // namespace melody
