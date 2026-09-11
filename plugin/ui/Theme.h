/*
  ==============================================================================

    Theme.h -- escuro neutro, na linguagem do sistema.

    A versao anterior era a paleta aquatica do Plasma, para o melody parecer
    irmao do Lume e do mosaic. O custo aceito ao sair dela e esse: ele deixa de
    parecer da familia. O ganho e que a janela deixa de competir com o proprio
    conteudo.

    ---------------------------------------------------------------------------
    O FUNDO E NEUTRO, E NAO AZUL-MARINHO.

    Preto azulado parece escolha de cor e puxa o olho; #0B0B0D nao parece nada,
    que e o ponto. Num plugin cujo miolo e um piano roll colorido, a unica cor
    da janela tem de ser a das notas -- assim a cor volta a significar alguma
    coisa em vez de ser decoracao.

    ---------------------------------------------------------------------------
    SUPERFICIE E BRANCO COM ALFA, E NAO UMA COR CHAPADA.

    Um cinza fixo so funciona sobre um fundo; branco a 6% funciona sobre
    qualquer um, e empilha: duas camadas de 6% dao a elevacao seguinte sem
    ninguem precisar escolher o segundo cinza. E o que faz painel sobre painel
    continuar legivel sem sombra nenhuma.

    ---------------------------------------------------------------------------
    TRES NIVEIS DE TEXTO, E SO TRES.

    92% para o que se le, 56% para rotulo, 34% para o que esta desligado. Sem
    quarto nivel: quando tudo pode ser um pouco mais claro ou um pouco mais
    escuro, nada tem hierarquia.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "core/Player.h"

namespace ui
{

namespace col
{
    // O CHASSI E CINZA DESSATURADO, E O ROLL E MAIS ESCURO QUE ELE.
    //
    // E a hierarquia do Logic, e ela tem funcao: o roll afundado separa
    // conteudo de controle sem precisar de moldura, e as cores das notas
    // mantem o contraste que perderiam sobre cinza medio.
    inline const juce::Colour base    { 0xff232327 };
    inline const juce::Colour rail    { 0xff1e1e21 };
    inline const juce::Colour raised  { 0xff2b2b30 };
    inline const juce::Colour roll    { 0xff17171a };

    // Dentro do roll: linha de tecla branca, linha de tecla preta, e a grade.
    inline const juce::Colour rowWhite { 0xff1d1d20 };
    inline const juce::Colour rowBlack { 0xff141417 };
    inline const juce::Colour gridBeat { 0xff26262b };
    inline const juce::Colour gridBar  { 0xff3a3a41 };

    // O teclado da lateral.
    inline const juce::Colour keyWhite { 0xffbcbcc2 };
    inline const juce::Colour keyBlack { 0xff2f2f34 };

    // Os materiais: branco com alfa, empilhaveis. Um pouco mais fortes que na
    // versao quase-preta -- sobre cinza medio, 6% quase nao aparece.
    inline const juce::Colour fill    { 0x14ffffff };   //  8%
    inline const juce::Colour fillHi  { 0x24ffffff };   // 14%
    inline const juce::Colour fillMax { 0x33ffffff };   // 20%
    inline const juce::Colour stroke  { 0x1fffffff };   // 12%
    inline const juce::Colour strokeHi{ 0x38ffffff };   // 22%

    // Os tres niveis de texto
    inline const juce::Colour text    { 0xebffffff };   // 92%
    inline const juce::Colour text2   { 0x8fffffff };   // 56%
    inline const juce::Colour text3   { 0x57ffffff };   // 34%

    /** UMA cor de destaque. Duas ja e uma decisao que o usuario tem de tomar a
        cada olhada, e nesta janela a cor precisa ficar reservada para as notas. */
    inline const juce::Colour accent  { 0xff2f8bff };

    // As camadas, na paleta da marca. Eram as cores de sistema do macOS; agora
    // sao as do melodyc, para o plugin e o material dele nao dizerem coisas
    // diferentes sobre a mesma camada.
    inline const juce::Colour melody  { 0xff3fbfe0 };   // ciano
    inline const juce::Colour chords  { 0xffe8971e };   // ambar
    inline const juce::Colour bass    { 0xffb45fe8 };   // violeta

    inline juce::Colour layer (int i) noexcept
    {
        return i == 0 ? melody : (i == 1 ? chords : bass);
    }

    static_assert ((int) melody::Layer::count == 3, "a paleta tem uma cor por camada");
}

//==============================================================================
/** A fonte do sistema primeiro.

    `SFNS.ttf` e a fonte da interface do macOS, e o JUCE chega nela pelo nome de
    familia ou pelo padrao sans-serif -- que no macOS ja e ela. E o unico sinal
    tipografico que importa aqui: qualquer outra escolha faz a janela parecer de
    outro lugar. */
inline const juce::String& typefaceName()
{
    static const juce::String name = []
    {
        const juce::StringArray wanted { "SF Pro Text", "SF Pro Display", "SF Pro",
                                         "Helvetica Neue" };
        const auto available = juce::Font::findAllTypefaceNames();

        for (const auto& w : wanted)
            if (available.contains (w))
                return w;

        return juce::Font::getDefaultSansSerifFontName();
    }();

    return name;
}

inline juce::Font uiFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (typefaceName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
constexpr float radiusPanel   = 12.0f;
constexpr float radiusControl = 8.0f;

/** Superficie: preenchimento translucido e um fio de um pixel.

    Sem sombra. Sombra sobre quase preto vira uma mancha suja; o que separa um
    plano do outro aqui e o fio, que custa um retangulo. */
inline void paintSurface (juce::Graphics& g, juce::Rectangle<float> b,
                          float radius = radiusControl,
                          juce::Colour f = col::fill,
                          juce::Colour s = col::stroke)
{
    g.setColour (f);
    g.fillRoundedRectangle (b, radius);

    g.setColour (s);
    g.drawRoundedRectangle (b.reduced (0.5f), radius, 1.0f);
}

/** Versal espacado, no nivel de rotulo. */
inline void paintCaption (juce::Graphics& g, juce::Rectangle<float> b,
                          const juce::String& text, juce::Colour c = col::text3,
                          float size = 9.5f)
{
    g.setColour (c);
    g.setFont (uiFont (size, true));

    juce::String spaced;
    for (int i = 0; i < text.length(); ++i)
    {
        spaced += text[i];
        if (i < text.length() - 1)
            spaced += " ";
    }

    g.drawText (spaced, b, juce::Justification::centredLeft, false);
}

/** Fio de um pixel, na horizontal. O separador da janela inteira. */
inline void paintRule (juce::Graphics& g, juce::Rectangle<int> b)
{
    g.setColour (col::stroke);
    g.fillRect (b.getX(), b.getY(), b.getWidth(), 1);
}

} // namespace ui
