/*
  ==============================================================================

    PianoRoll.h -- a frase, desenhada.

    E o unico jeito de o usuario decidir sem tocar: da para VER que a segunda
    metade repete a primeira, que existe pausa, que o baixo nao esta em cima da
    melodia. Um botao GERAR sem isto vira caca-niquel -- gera, escuta os quatro
    compassos inteiros, gera de novo.

    A ALTURA E AUTOESCALADA PELA FRASE, e nao fixa em 128 notas. Frase de trap
    ocupa duas oitavas e meia; desenhar as 128 deixaria tudo achatado numa faixa
    de dez pixels no meio de um retangulo vazio.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "core/Generator.h"
#include "core/Player.h"
#include "plugin/ui/Theme.h"

namespace ui
{

class PianoRoll : public juce::Component
{
public:
    void setPhrase (const melody::Phrase& p)
    {
        phrase = p;
        arrived = juce::Time::getMillisecondCounter();
        repaint();
    }

    /** Um tique do relogio da janela. Repinta enquanto houver o que animar --
        e SO enquanto: fora disso a janela nao gasta um quadro. */
    void tick()
    {
        if (animating())
            repaint();
    }

    /** Congela a chegada num ponto, para a captura poder mostrar a animacao.
        Sem isto a foto sai sempre no quadro zero -- com o piano roll vazio, que
        parece defeito e nao animacao. */
    void poseForShot (float progresso, double headBeats, bool headOn)
    {
        arrived = juce::Time::getMillisecondCounter()
                    - (juce::uint32) (juce::jlimit (0.0f, 1.0f, progresso) * arrivalMs);
        head = headBeats;
        headVisible = headOn;
        repaint();
    }

    bool animating() const noexcept
    {
        return juce::Time::getMillisecondCounter() - arrived < arrivalMs;
    }

    /** O estado da frase, escrito no canto de cima do proprio roll. */
    void setStatus (juce::String s)
    {
        if (s == status)
            return;

        status = std::move (s);
        repaint();
    }

    void setPlayhead (double beats, bool visible)
    {
        const double b = juce::jlimit (0.0, phrase.loopBeats(), beats);

        if (visible == headVisible && std::abs (b - head) < 0.01)
            return;

        head = b;
        headVisible = visible;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        // O roll e o miolo da janela, entao ele e o plano MAIS FUNDO -- um
        // pouco mais escuro que o fundo geral. Assim a nota colorida flutua
        // sobre ele em vez de disputar com uma superficie clara.
        g.setColour (col::roll);
        g.fillRect (b);

        // O TECLADO E A REGUA SAO O QUE FAZ ISTO SER UM PIANO ROLL.
        //
        // Sem eles a janela mostra retangulos coloridos numa grade: nao da para
        // dizer se aquilo e um do ou um fa, nem em que compasso se esta. Sao as
        // duas perguntas que qualquer um faz olhando, e as duas se respondem
        // com pixels que nao competem com nada.
        constexpr float keyW = 42.0f;
        constexpr float rulerH = 17.0f;

        auto todo = b.reduced (0.0f);
        auto regua = todo.removeFromTop (rulerH);
        auto teclado = todo.removeFromLeft (keyW);
        auto area = todo.reduced (0.0f, 4.0f);
        regua = regua.withTrimmedLeft (keyW);

        if (phrase.empty())
        {
            g.setColour (col::text3);
            g.setFont (uiFont (11.0f, true));
            g.drawText ("SEM BANCO", area, juce::Justification::centred, false);
            return;
        }

        //----------------------------------------------------------------------
        // A JANELA VERTICAL, COM OS BURACOS ENCOLHIDOS.
        //
        // Medido sobre mil frases (`melody_smoke --span`): a janela ocupava 41
        // semitons em media e VINTE E UMA dessas linhas nao tinham nota
        // nenhuma. Metade do roll desenhava grade. A nota sobrava com 11
        // pixels, e por isso a dinamica no brilho, o halo e o aquecimento --
        // que ja estavam todos implementados -- quase nao apareciam: faltava
        // superficie onde aparecer.
        //
        // O que some e a distancia LITERAL entre clusters distantes, que era
        // desenhada com fidelidade e nao dizia nada: entre o baixo e os acordes
        // podem passar duas oitavas de vazio. O que fica intacto e o intervalo
        // DENTRO de cada cluster, que e o que se le num acorde ou numa linha.
        //
        // NAO ERA O BAIXO. A primeira hipotese foi dar pista propria ao baixo,
        // tirada de UMA captura. A medicao matou: sem o baixo a janela cai de
        // 41 para 36 semitons, e a pista custaria mais do que economiza -- a
        // nota ENCOLHERIA. O custo do campo e buraco, e nao camada.
        const int lo = phrase.lowest() - 3;
        const int hi = phrase.highest() + 3;

        bool usada[128] = {};
        auto marca = [&usada] (const melody::Voice* v, int n)
        {
            for (int i = 0; i < n; ++i)
                if (juce::isPositiveAndBelow (v[i].pitch, 128))
                    usada[v[i].pitch] = true;
        };
        marca (phrase.melody, phrase.melodyCount);
        marca (phrase.chords, phrase.chordCount);
        marca (phrase.bass,   phrase.bassCount);

        // Corrida de 3+ vazias encolhe. Buraco curto NAO: buraco curto e o
        // desenho do proprio acorde, e achatar ele descaracterizaria a voz.
        bool colapsada[128] = {};
        for (int pitch = lo; pitch <= hi; )
        {
            if (pitch >= 0 && pitch < 128 && usada[pitch]) { ++pitch; continue; }

            int fim = pitch;
            while (fim <= hi && ! (fim >= 0 && fim < 128 && usada[fim]))
                ++fim;

            if (fim - pitch >= 3)
                for (int k = pitch; k < fim; ++k)
                    if (k >= 0 && k < 128)
                        colapsada[k] = true;

            pitch = fim;
        }

        // A QUEBRA E UMA COSTURA, E NAO UMA LINHA.
        //
        // Na primeira versao o buraco encolhido ocupava uma linha inteira, e a
        // lateral virava um codigo de barras: metade das linhas era entalhe, e
        // cada entalhe disputava atencao com uma nota. Valendo menos da metade
        // de uma linha ele le como emenda entre dois trechos de teclado -- que
        // e o que ele e -- e ainda devolve altura para a nota.
        constexpr float fatiaCostura = 0.45f;

        int linhasReais = 0, costuras = 0;
        for (int pitch = lo; pitch <= hi; ++pitch)
        {
            const bool col = pitch >= 0 && pitch < 128 && colapsada[pitch];

            if (! col) { ++linhasReais; continue; }

            const bool colAnterior = pitch > lo && pitch - 1 >= 0 && pitch - 1 < 128
                                       && colapsada[pitch - 1];
            if (! colAnterior)
                ++costuras;
        }

        // O MINIMO E EM UNIDADES DE ALTURA, E NAO EM SEMITONS.
        //
        // Era "abre ate 24 semitons" para a nota nao ficar gorda demais. Depois
        // do encolhimento, semitom deixou de ser a unidade de altura -- abrir o
        // ambito so criaria vazio que o proprio encolhimento comeria de volta.
        // O PISO DE LINHAS ACOMPANHA A ALTURA DA JANELA.
        //
        // Era 20 fixo, herdado de quando a janela tinha tamanho unico. Numa
        // janela esticada isso dava nota de 40 pixels: deixa de ser piano roll e
        // vira grafico de barras, e o teclado da lateral fica com tecla de
        // dedao. Espaco a mais tem de virar CONTEXTO, e nao nota mais gorda --
        // e o que um piano roll de verdade faz ao abrir.
        constexpr float alturaIdeal = 26.0f;
        const float minUnidades = juce::jmax (20.0f, area.getHeight() / alturaIdeal);
        const float unidadesCruas = (float) linhasReais + fatiaCostura * (float) costuras;
        const float rowH = area.getHeight() / juce::jmax (minUnidades, unidadesCruas);

        // A FOLGA VAI PARA AS COSTURAS, E NAO PARA A MARGEM.
        //
        // Com o piso valendo, a frase magra deixava faixa escura em cima e
        // embaixo: trocar um vazio (o buraco entre camadas) por outro (a
        // moldura) nao e ganho nenhum. Sobrando altura, quem cresce e a
        // costura, que volta a lembrar o tamanho do buraco que representa.
        float costuraH = rowH * fatiaCostura;

        if (costuras > 0 && unidadesCruas < minUnidades)
        {
            const float sobra = (minUnidades - unidadesCruas) * rowH;
            costuraH = juce::jmin (rowH * 1.5f, costuraH + sobra / (float) costuras);
        }

        const float alturaUsada = rowH * (float) linhasReais
                                    + costuraH * (float) costuras;
        const float topo = area.getY() + (area.getHeight() - alturaUsada) * 0.5f;

        const int steps = phrase.steps();
        const float stepW = area.getWidth() / (float) steps;

        // O topo de cada linha, por altura. Virou tabela porque a posicao
        // deixou de ser `(pitch - lo) * rowH`: depois de uma costura a conta
        // nao fecha mais com multiplicacao.
        float linhaY[128];
        for (auto& v : linhaY) v = -1.0f;

        {
            float y = topo;
            int pitch = hi;
            while (pitch >= lo)
            {
                if (pitch >= 0 && pitch < 128 && colapsada[pitch])
                {
                    int base = pitch;
                    while (base >= lo && base >= 0 && base < 128 && colapsada[base])
                        --base;

                    for (int k = base + 1; k <= pitch; ++k)
                        if (k >= 0 && k < 128)
                            linhaY[k] = y;

                    y += costuraH;
                    pitch = base;
                }
                else
                {
                    if (pitch >= 0 && pitch < 128)
                        linhaY[pitch] = y;
                    y += rowH;
                    --pitch;
                }
            }
        }

        auto yOf = [&linhaY, topo] (int pitch)
        {
            return juce::isPositiveAndBelow (pitch, 128) && linhaY[pitch] >= 0.0f
                     ? linhaY[pitch] : topo;
        };

        auto ehColapsada = [&colapsada] (int pitch)
        {
            return juce::isPositiveAndBelow (pitch, 128) && colapsada[pitch];
        };

        /** A faixa que as linhas realmente ocupam -- teclado e grade param
            junto com o conteudo em vez de atravessar o vazio. */
        const auto banda = area.withTop (topo).withBottom (topo + alturaUsada);

        //----------------------------------------------------------------------
        // A CHEGADA DA FRASE.
        //
        // Apertar GERAR trocava a frase num quadro so, e por isso nao contava
        // nada: acabou de acontecer uma escolha no banco inteiro e a janela
        // dava a mesma resposta de um redesenho qualquer. As notas passam a
        // chegar da esquerda para a direita, cada uma nascendo estreita e
        // abrindo ate a largura de verdade.
        //
        // O escalonamento e por POSICAO no compasso, e nao por indice na lista:
        // por indice, as camadas chegariam em blocos (melodia inteira, depois
        // acordes) em vez de o compasso se montar da esquerda para a direita.
        const juce::uint32 desde = juce::Time::getMillisecondCounter() - arrived;
        const float chegada = juce::jlimit (0.0f, 1.0f, (float) desde / (float) arrivalMs);

        auto progressoDe = [chegada, steps] (int pos)
        {
            constexpr float janela = 0.45f;
            const float atraso = (1.0f - janela) * (float) pos / (float) juce::jmax (1, steps);
            const float t = juce::jlimit (0.0f, 1.0f, (chegada - atraso) / janela);

            // easeOutCubic: rapido no comeco e assentando no fim, que e o que
            // faz parecer que a nota pousa em vez de aparecer.
            const float u = 1.0f - t;
            return 1.0f - u * u * u;
        };

        //----------------------------------------------------------------------
        // As linhas de tecla preta ficam mais escuras.
        //
        // E o que permite achar uma altura sem contar linha: o olho reconhece o
        // padrao de duas e tres pretas do teclado, e sabe onde esta.
        auto ehPreta = [] (int p)
        {
            static const bool preta[12] = { false, true, false, true, false, false,
                                            true, false, true, false, true, false };
            return preta[((p % 12) + 12) % 12];
        };

        for (int p = lo; p <= hi; ++p)
        {
            if (ehColapsada (p))
            {
                // Desenhada UMA vez por corrida: pintar por altura repintaria a
                // mesma costura dez vezes.
                if (p > lo && ehColapsada (p - 1))
                    continue;

                g.setColour (col::roll.withMultipliedBrightness (0.5f));
                g.fillRect (area.getX(), yOf (p), area.getWidth(), costuraH);
                continue;
            }

            g.setColour (ehPreta (p) ? col::rowBlack : col::rowWhite);
            g.fillRect (area.getX(), yOf (p), area.getWidth(), rowH);
        }

        // A grade: compasso forte, tempo medio, semicolcheia nenhuma. Linha por
        // semicolcheia neste tamanho vira textura, nao informacao.
        for (int st = 0; st <= steps; st += 4)
        {
            const bool compasso = (st % melody::stepsPerBar) == 0;

            g.setColour (compasso ? col::gridBar : col::gridBeat);
            g.fillRect (area.getX() + (float) st * stepW, banda.getY(),
                        1.0f, banda.getHeight());
        }

        //----------------------------------------------------------------------
        // O TECLADO.
        // O fundo do teclado e branco: as pretas sao desenhadas POR CIMA, mais
        // curtas. Pretas do mesmo comprimento que as brancas nao formam o
        // desenho de teclado que o olho reconhece -- viram listras.
        g.setColour (col::keyWhite);
        g.fillRect (teclado.withTrimmedRight (1.0f)
                           .withTop (banda.getY()).withBottom (banda.getBottom()));

        for (int p = lo; p <= hi; ++p)
        {
            const auto linhaCheia = juce::Rectangle<float> (
                teclado.getX(), yOf (p), teclado.getWidth() - 1.0f,
                juce::jmax (1.0f, ehColapsada (p) ? costuraH : rowH));

            // A QUEBRA APARECE NO TECLADO.
            //
            // O teclado e INTERROMPIDO, e nao apenas apagado: o fundo do roll
            // invade a lateral e duas diagonais atravessam a emenda. E o sinal
            // de eixo quebrado de um grafico -- sem ele o do3 e o do4
            // encostariam sem aviso, e a lateral passaria a mentir sobre a
            // distancia entre as duas alturas.
            if (ehColapsada (p))
            {
                if (p > lo && ehColapsada (p - 1))
                    continue;

                g.setColour (col::roll);
                g.fillRect (linhaCheia);

                g.setColour (col::keyWhite.withAlpha (0.34f));
                const float meio = linhaCheia.getCentreY();
                const float alt = costuraH * 0.42f;

                for (int t = 0; t < 2; ++t)
                {
                    const float x0 = linhaCheia.getX() + 9.0f + (float) t * 12.0f;
                    g.drawLine (x0, meio + alt, x0 + 6.0f, meio - alt, 1.0f);
                }
                continue;
            }

            const bool preta = ehPreta (p);
            const auto linha = linhaCheia.withWidth (
                linhaCheia.getWidth() * (preta ? 0.62f : 1.0f));

            if (preta)
            {
                g.setColour (col::keyBlack);
                g.fillRect (linha.reduced (0.0f, 0.5f));
            }
            else
            {
                // O fio entre duas brancas vizinhas, como num teclado de verdade.
                g.setColour (col::roll.withAlpha (0.45f));
                g.fillRect (linha.getX(), linha.getBottom() - 0.5f,
                            linha.getWidth(), 1.0f);
            }

            // O do de cada oitava leva o nome. Sem isso o teclado diz "aqui tem
            // um padrao" e nao "aqui e o do3".
            if (p % 12 == 0 && rowH >= 7.0f)
            {
                g.setColour (col::roll.withAlpha (0.75f));
                g.setFont (uiFont (juce::jmin (9.0f, rowH * 0.8f), true));
                g.drawText ("C" + juce::String (p / 12 - 1),
                            linha.reduced (4.0f, 0.0f),
                            juce::Justification::centredRight, false);
            }
        }

        g.setColour (col::gridBar);
        g.fillRect (teclado.getRight() - 1.0f, banda.getY(), 1.0f, banda.getHeight());

        //----------------------------------------------------------------------
        // A REGUA, numerada por compasso.
        g.setColour (col::rail);
        g.fillRect (regua);

        g.setColour (col::gridBar);
        g.fillRect (regua.getX(), regua.getBottom() - 1.0f, regua.getWidth(), 1.0f);

        for (int compasso = 0; compasso < phrase.bars; ++compasso)
        {
            const float x = area.getX() + (float) (compasso * melody::stepsPerBar) * stepW;

            g.setColour (col::gridBar);
            g.fillRect (x, regua.getY() + 4.0f, 1.0f, regua.getHeight() - 5.0f);

            g.setColour (col::text2);
            g.setFont (uiFont (9.5f, true));
            g.drawText (juce::String (compasso + 1),
                        juce::Rectangle<float> (x + 5.0f, regua.getY(), 24.0f,
                                                regua.getHeight()),
                        juce::Justification::centredLeft, false);
        }

        // Quanto uma nota esta "quente": 1 enquanto soa, e esfriando depois.
        //
        // E funcao PURA da posicao do cursor, sem estado por nota. Guardar
        // quando cada nota acendeu daria o mesmo desenho e um estado a mais
        // para dessincronizar quando o transporte salta.
        auto calorDe = [this] (int pos, int dur) -> float
        {
            if (! headVisible)
                return 0.0f;

            const double inicio = pos * 0.25;
            const double fim = inicio + dur * 0.25;

            if (head >= inicio && head < fim)
                return 1.0f;

            constexpr double esfria = 0.9;      // batidas ate apagar de todo

            if (head >= fim && head < fim + esfria)
                return (float) (1.0 - (head - fim) / esfria);

            return 0.0f;
        };

        auto drawLayer = [&] (const melody::Voice* v, int count, juce::Colour c,
                              float alpha, float inset)
        {
            for (int i = 0; i < count; ++i)
            {
                const float chega = progressoDe (v[i].pos);

                if (chega <= 0.0f)
                    continue;

                const float calor = calorDe (v[i].pos, v[i].dur);

                const float x = area.getX() + (float) v[i].pos * stepW;
                const float wCheia = juce::jmax (3.0f, (float) v[i].dur * stepW - 1.5f);
                const float w = juce::jmax (2.0f, wCheia * chega);
                const float y = yOf (v[i].pitch) + inset;
                const float h = juce::jmax (2.5f, rowH - 1.0f - inset * 2.0f);

                auto r = juce::Rectangle<float> (x, y, juce::jmin (w, area.getRight() - x), h);

                // Quente cresce um pouco. Sobre fundo claro nao da para usar
                // brilho aditivo -- ele simplesmente some. O que le como
                // "acendeu" aqui e saturacao, tamanho e uma sombra curta.
                if (calor > 0.0f)
                    r = r.expanded (calor * 1.6f, calor * 1.2f);

                const float raio = juce::jmin (2.5f, r.getHeight() * 0.5f);

                // O halo e dois retangulos translucidos, e nao um DropShadow.
                //
                // `DropShadow::drawForPath` renderiza uma imagem borrada a cada
                // chamada; com meia duzia de notas quentes a 30 quadros por
                // segundo sao centenas de borroes por segundo no thread de
                // interface. Num plugin isso disputa com a DAW inteira. Dois
                // aros translucidos custam quase nada e leem igual sobre claro.
                if (calor > 0.0f)
                    for (int anel = 3; anel >= 1; --anel)
                    {
                        const float g2 = (float) anel * 2.2f * calor;

                        g.setColour (c.withAlpha (calor * 0.14f / (float) anel));
                        g.fillRoundedRectangle (r.expanded (g2, g2 * 0.7f),
                                                raio + g2 * 0.5f);
                    }

                // Sobre escuro a nota fria e a cor a 70%, e a quente clareia
                // ate o branco. Aqui brilho FUNCIONA -- foi so sobre claro que
                // ele sumia.
                // A VELOCITY APARECE. O banco guarda a velocity real dos MIDIs
                // de origem e ela nao aparecia em lugar nenhum -- duas notas com
                // dinamica bem diferente desenhavam identicas.
                const float vel = juce::jlimit (0.35f, 1.0f, (float) v[i].vel / 110.0f);

                const juce::Colour cor = calor > 0.0f
                    ? c.interpolatedWith (juce::Colours::white, calor * 0.45f)
                    : c.withMultipliedBrightness (0.62f + 0.38f * vel);

                g.setColour (cor.withAlpha (alpha * (0.62f + 0.38f * chega)));
                g.fillRoundedRectangle (r, raio);

                // Sobre escuro o aro e CLARO, e so na nota quente. No frio ele
                // seria mais um contorno competindo com a cor da camada, que e
                // a unica coisa que a janela usa cor para dizer.
                if (calor > 0.0f)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.30f * calor));
                    g.drawRoundedRectangle (r.reduced (0.5f), raio, 1.0f);
                }
            }
        };

        // Ordem de pintura = ordem de leitura: harmonia ao fundo, melodia por
        // cima, para a linha que interessa nunca ficar coberta.
        drawLayer (phrase.chords, phrase.chordCount, col::chords, 0.92f, 0.5f);
        drawLayer (phrase.bass,   phrase.bassCount,  col::bass,   0.92f, 0.5f);
        drawLayer (phrase.melody, phrase.melodyCount, col::melody, 1.00f, 0.0f);

        //----------------------------------------------------------------------
        if (status.isNotEmpty())
        {
            g.setColour (col::text3);
            g.setFont (uiFont (10.0f, false));
            g.drawText (status, regua.withTrimmedRight (8.0f),
                        juce::Justification::centredRight, false);
        }

        //----------------------------------------------------------------------
        // A VARREDURA DA CHEGADA.
        //
        // GERAR e o botao que a pessoa aperta vinte vezes seguidas ate uma frase
        // pegar, e a resposta era so o fade de 420 ms das notas -- correto e
        // discreto demais para a acao central do plugin. A luz corre JUNTO com a
        // frente de chegada, e nao no tempo bruto: as notas comecam a nascer em
        // `0,55 * pos / steps` (ver `progressoDe`), entao a frente esta em
        // `chegada / 0,55`. Fora de sincronia ela viraria um segundo efeito
        // acontecendo por cima, em vez do mesmo.
        //
        // Gradiente sobre retangulo, como o halo e o rastro: aqui `shadowBlur`
        // ou DropShadow rasterizariam a tela inteira a cada quadro.
        if (chegada < 1.0f)
        {
            const float frente = juce::jlimit (0.0f, 1.0f, chegada / 0.55f);
            const float x = area.getX() + frente * area.getWidth();
            const float cauda = juce::jmin (110.0f, x - area.getX());

            // Some no fim: a varredura anuncia a frase, e depois sai da frente.
            const float forca = 1.0f - chegada * chegada;

            if (cauda > 1.0f)
            {
                g.setGradientFill (juce::ColourGradient (
                    juce::Colours::white.withAlpha (0.0f), x - cauda, 0.0f,
                    juce::Colours::white.withAlpha (0.10f * forca), x, 0.0f, false));
                g.fillRect (x - cauda, banda.getY(), cauda, banda.getHeight());
            }

            if (frente < 1.0f)
            {
                g.setColour (juce::Colours::white.withAlpha (0.30f * forca));
                g.fillRect (x - 1.0f, banda.getY(), 2.0f, banda.getHeight());
            }
        }

        if (headVisible)
        {
            const float x = area.getX()
                              + (float) (head / phrase.loopBeats()) * area.getWidth();

            // Um rastro atras do cursor, e nao so a linha. A linha sozinha nao
            // diz de que lado ela veio; o rastro da a direcao sem custar leitura.
            const float rastro = juce::jmin (28.0f, x - area.getX());

            // Sobre escuro o rastro precisa ser bem mais fraco que sobre claro:
            // a 7% ele virava uma coluna leitosa e parecia mancha, nao rastro.
            if (rastro > 1.0f)
            {
                g.setGradientFill (juce::ColourGradient (
                    juce::Colours::white.withAlpha (0.0f), x - rastro, 0.0f,
                    juce::Colours::white.withAlpha (0.035f), x, 0.0f, false));
                g.fillRect (x - rastro, area.getY(), rastro, area.getHeight());
            }

            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.fillRect (x, regua.getY(), 1.5f, area.getBottom() - regua.getY());
        }
    }

private:
    melody::Phrase phrase;
    double head = 0.0;
    bool headVisible = false;

    /** Quando a frase atual chegou, e quanto dura a chegada. 420 ms: curto o
        bastante para nao atrapalhar quem esta gerando em sequencia, longo o
        bastante para se ver. */
    juce::String status;
    juce::uint32 arrived = 0;
    static constexpr juce::uint32 arrivalMs = 420;
};

} // namespace ui
