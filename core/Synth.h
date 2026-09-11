/*
  ==============================================================================

    Synth.h -- o som de conferencia.

    NAO E O PRODUTO. O produto e a nota: o plugin manda MIDI para fora e a frase
    e arrastada para a DAW. Este sintetizador existe para o botao TOCAR
    responder na hora, sem o usuario ter que rotear nada -- ouvir a frase e o
    unico jeito de decidir se ela serve, e obrigar a montar uma cadeia antes de
    ouvir mataria o ciclo de "gera, escuta, gera de novo".

    Por isso e deliberadamente simples: tres tabelas de onda, uma por camada,
    com envelope de ataque e queda. Uma tabela por camada, e nao um oscilador
    somando harmonicos por amostra, porque o custo por voz precisa caber em
    dezesseis vozes num bloco pequeno sem pensar duas vezes.

  ==============================================================================
*/

#pragma once

#include <cmath>
#include <cstring>

namespace melody
{

class Synth
{
public:
    static constexpr int numVoices = 24;
    static constexpr int tableSize = 2048;
    static constexpr int numLayers = 3;

    //==========================================================================
    void prepare (double sampleRate) noexcept
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        buildTables();
        allNotesOff();
    }

    void setLayerGain (int layer, float gain) noexcept
    {
        if (layer >= 0 && layer < numLayers)
            layerGain[layer] = gain;
    }

    //==========================================================================
    void noteOn (int layer, int pitch, float velocity) noexcept
    {
        if (layer < 0 || layer >= numLayers)
            return;

        Voice& v = voices[allocate (layer, pitch)];

        v.layer = layer;
        v.pitch = pitch;
        v.phase = 0.0f;
        v.inc = (float) (freqOf (pitch) * tableSize / sr);
        v.level = velocity;
        v.env = 0.0f;
        v.releasing = false;
        v.active = true;

        // O ataque e por camada: acorde entrando com o mesmo estalo da melodia
        // faz a frase inteira soar como um bloco so.
        v.attackStep = 1.0f / (float) (attackSamples (layer) + 1);
        v.decay = decayPerSample (layer);
    }

    void noteOff (int layer, int pitch) noexcept
    {
        for (Voice& v : voices)
            if (v.active && ! v.releasing && v.layer == layer && v.pitch == pitch)
                v.releasing = true;
    }

    void allNotesOff() noexcept
    {
        for (Voice& v : voices)
            v = {};
    }

    //==========================================================================
    /** Soma no buffer -- nao limpa. Quem chama decide se o bloco comeca em
        silencio ou ja tem outra coisa dentro. */
    void render (float* left, float* right, int numSamples) noexcept
    {
        for (Voice& v : voices)
        {
            if (! v.active)
                continue;

            const float* table = wave[v.layer];
            const float gain = layerGain[v.layer] * v.level * 0.25f;
            const float rel = releasePerSample;

            for (int i = 0; i < numSamples; ++i)
            {
                if (v.env < 1.0f && ! v.releasing)
                    v.env += v.attackStep;
                else
                    v.env *= v.releasing ? rel : v.decay;

                if (v.env < 0.0002f)
                {
                    v.active = false;
                    break;
                }

                const int idx = (int) v.phase;
                const float frac = v.phase - (float) idx;
                const int i0 = idx & (tableSize - 1);
                const int i1 = (idx + 1) & (tableSize - 1);

                const float s = (table[i0] + (table[i1] - table[i0]) * frac)
                                  * v.env * gain;

                left[i] += s;
                right[i] += s;

                v.phase += v.inc;

                if (v.phase >= (float) tableSize)
                    v.phase -= (float) tableSize;
            }
        }
    }

    bool anySounding() const noexcept
    {
        for (const Voice& v : voices)
            if (v.active)
                return true;

        return false;
    }

private:
    //==========================================================================
    struct Voice
    {
        bool  active = false;
        bool  releasing = false;
        int   layer = 0;
        int   pitch = 60;
        float phase = 0.0f;
        float inc = 1.0f;
        float level = 1.0f;
        float env = 0.0f;
        float attackStep = 1.0f;
        float decay = 1.0f;
    };

    static double freqOf (int pitch) noexcept
    {
        return 440.0 * std::pow (2.0, (pitch - 69) / 12.0);
    }

    int attackSamples (int layer) const noexcept
    {
        const double s = layer == 1 ? 0.030 : (layer == 2 ? 0.006 : 0.004);
        return (int) (s * sr);
    }

    float decayPerSample (int layer) const noexcept
    {
        // Batidas por segundo de queda. Acorde segura, melodia decai, baixo
        // decai mais devagar que a melodia -- e o que faz o 808 sustentar.
        const double rate = layer == 1 ? 0.9 : (layer == 2 ? 1.6 : 2.6);
        return (float) std::exp (-rate / sr);
    }

    /** Rouba a voz mais fraca. Nunca falha: com o banco chegando a acordes de
        oito vozes, ficar sem voz e questao de tempo, e uma nota perdida e menos
        ruim que uma nota que nao para. */
    int allocate (int layer, int pitch) noexcept
    {
        for (int i = 0; i < numVoices; ++i)
            if (! voices[i].active)
                return i;

        // Mesma nota da mesma camada primeiro: e retomada, nao roubo.
        for (int i = 0; i < numVoices; ++i)
            if (voices[i].layer == layer && voices[i].pitch == pitch)
                return i;

        int worst = 0;

        for (int i = 1; i < numVoices; ++i)
            if (voices[i].env < voices[worst].env)
                worst = i;

        return worst;
    }

    void buildTables() noexcept
    {
        // melodia: fundamental com brilho curto -- le como teclado.
        // acorde: quase so fundamental e quinta, para nao brigar com a melodia.
        // baixo: seno com um pouco de segundo harmonico, que e o que faz o
        //        grave aparecer em caixa pequena.
        static const float partials[numLayers][6] =
        {
            { 1.00f, 0.45f, 0.22f, 0.12f, 0.00f, 0.05f },
            { 1.00f, 0.30f, 0.10f, 0.00f, 0.04f, 0.00f },
            { 1.00f, 0.18f, 0.05f, 0.00f, 0.00f, 0.00f }
        };

        for (int l = 0; l < numLayers; ++l)
        {
            float peak = 0.0f;

            for (int i = 0; i < tableSize; ++i)
            {
                float s = 0.0f;

                for (int h = 0; h < 6; ++h)
                    if (partials[l][h] > 0.0f)
                        s += partials[l][h]
                               * std::sin (6.283185307179586f * (float) (h + 1)
                                             * (float) i / (float) tableSize);

                wave[l][i] = s;
                peak = std::fabs (s) > peak ? std::fabs (s) : peak;
            }

            if (peak > 0.0f)
                for (int i = 0; i < tableSize; ++i)
                    wave[l][i] /= peak;
        }
    }

    double sr = 44100.0;
    float wave[numLayers][tableSize] {};
    float layerGain[numLayers] { 0.9f, 0.55f, 0.85f };
    float releasePerSample = 0.9997f;
    Voice voices[numVoices];
};

} // namespace melody
