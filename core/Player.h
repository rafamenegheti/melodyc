/*
  ==============================================================================

    Player.h -- de frase para eventos, sem estado escondido.

    O tocador nao guarda "que nota esta soando". Ele recebe a janela de batidas
    do bloco e pergunta a cada nota se ela COMECA ou TERMINA ali dentro. Sem
    estado, seguir o transporte do host vira aritmetica: se o usuario pular para
    o compasso 47, a janela seguinte simplesmente cai em outro lugar do laco, e
    nao ha nada para ressincronizar.

    A CONSEQUENCIA ACEITA E QUE TROCAR DE FRASE PENDURA NOTA. Quem troca manda
    silenciar antes -- e o processador faz isso -- porque a alternativa seria
    guardar a lista de notas soando, e essa lista e exatamente o estado que faz
    salto de transporte virar nota presa.

  ==============================================================================
*/

#pragma once

#include <cmath>

#include "core/Config.h"
#include "core/Generator.h"

namespace melody
{

/** As tres camadas saem em canais MIDI proprios, para o usuario mandar cada uma
    para um instrumento diferente. */
enum class Layer { melody = 0, chords, bass, count };

constexpr int layerChannel (Layer l) noexcept { return (int) l + 1; }

/** O laco tem o comprimento DA FRASE. Era uma constante de 16 batidas, e virar
    constante de novo seria a forma mais silenciosa de quebrar os oito
    compassos: a frase teria 32 batidas e o tocador daria a volta na metade. */
inline double loopBeatsOf (const Phrase& p) noexcept { return p.loopBeats(); }

//==============================================================================
/** Alguma OUTRA nota da frase ainda segura esta altura nesta batida?

    Existe porque as camadas podem ser somadas num canal MIDI so -- que e o caso
    comum, um instrumento na mesma faixa. Somadas, melodia e acorde que calham na
    mesma altura viram uma nota so para o instrumento, e o note-off da primeira
    corta a segunda no meio. O sintoma nao e nota presa, e o contrario: acorde
    que morre antes da hora, sem nada no piano roll explicando.

    A conta e uma funcao PURA DA FRASE -- que ja e conhecida -- entao o tocador
    continua sem estado. `mask` diz quais camadas dividem o canal.

    Uma nota que TERMINA exatamente aqui nao conta (o `<` cuida disso), entao ela
    nao segura a si mesma. Uma que COMECA aqui conta, e deve mesmo: e retomada da
    mesma altura, e deixar o note-off passar mataria o note-on do mesmo instante. */
inline bool heldAt (const Phrase& p, int pitch, double beatInLoop,
                    unsigned layerMask) noexcept
{
    constexpr double eps = 1.0e-6;
    const double loopBeats = p.loopBeats();

    auto scan = [&] (Layer layer, const Voice* v, int count)
    {
        if (((layerMask >> (int) layer) & 1u) == 0u)
            return false;

        for (int i = 0; i < count; ++i)
        {
            if (v[i].pitch != pitch)
                continue;

            const double start = v[i].pos * 0.25;
            double end = start + v[i].dur * 0.25;

            if (end > loopBeats)
                end = loopBeats;

            if (beatInLoop >= start - eps && beatInLoop < end - eps)
                return true;
        }

        return false;
    };

    return scan (Layer::melody, p.melody, p.melodyCount)
        || scan (Layer::chords, p.chords, p.chordCount)
        || scan (Layer::bass,   p.bass,   p.bassCount);
}

//==============================================================================
/** Percorre [from, to) em batidas e chama `on` / `off`.

    `on (layer, pitch, velocity, beatOffset)` e `off (layer, pitch, beatOffset)`,
    com o deslocamento medido a partir de `from` -- quem chama converte para
    amostras, porque so quem chama sabe a taxa. */
template <typename OnFn, typename OffFn>
inline void advance (const Phrase& p, double from, double to, OnFn&& on, OffFn&& off)
{
    if (p.empty() || to <= from)
        return;

    const double loopBeats = p.loopBeats();

    // A janela pode cruzar o fim do laco -- e mais de uma vez, se o bloco for
    // longo ou o andamento absurdo. Cada volta e tratada como uma janela sua.
    double a = from;

    while (a < to)
    {
        const double loopStart = std::floor (a / loopBeats) * loopBeats;
        const double b = (to < loopStart + loopBeats) ? to : loopStart + loopBeats;

        const double la = a - loopStart;
        const double lb = b - loopStart;

        auto emit = [&] (Layer layer, const Voice* v, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const double start = v[i].pos * 0.25;
                double end = start + v[i].dur * 0.25;

                if (end > loopBeats)
                    end = loopBeats;

                if (start >= la && start < lb)
                    on (layer, v[i].pitch, v[i].vel, loopStart + start - from);

                if (end > la && end <= lb)
                    off (layer, v[i].pitch, loopStart + end - from);
            }
        };

        emit (Layer::melody, p.melody, p.melodyCount);
        emit (Layer::chords, p.chords, p.chordCount);
        emit (Layer::bass,   p.bass,   p.bassCount);

        a = b;
    }
}

} // namespace melody
