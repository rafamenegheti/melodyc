/*
  ==============================================================================

    Widgets.h -- os controles desenhados a mao.

    Sao poucos e pequenos de proposito. Um LookAndFeel completo obrigaria a
    responder por todo componente do JUCE que a janela nunca usa; estes cinco
    respondem so pelo que aparece.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <functional>

#include "plugin/ui/Theme.h"

namespace ui
{

//==============================================================================
/** Botao chapado. `primary` e o GERAR: claro sobre escuro, o unico da janela --
    ter dois botoes de destaque e nao ter nenhum. */
class FlatButton : public juce::Button
{
public:
    FlatButton (const juce::String& text, bool isPrimary = false)
        : juce::Button (text), primary (isPrimary)
    {
        setButtonText (text);
    }

    void setAccent (juce::Colour c) { accent = c; repaint(); }

    /** Reserva espaco a direita para quem desenha alguma coisa la.

        O texto e centrado, e centrado na largura INTEIRA ele encosta no que
        estiver na borda -- foi o que aconteceu com a seta do arrastar. Centrar
        no que sobra e o unico jeito de o texto continuar parecendo centrado. */
    void setTextRightInset (int px) { rightInset = px; repaint(); }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        const float r = juce::jmin (radiusControl, b.getHeight() * 0.32f);

        // O primario e o unico cheio de cor da janela. Dois botoes de destaque
        // e o mesmo que nenhum: o olho nao sabe onde comecar.
        juce::Colour f = primary ? col::accent : col::fill;

        if (down)      f = primary ? f.darker (0.18f) : col::fillMax;
        else if (over) f = primary ? f.brighter (0.14f) : col::fillHi;

        g.setColour (f);
        g.fillRoundedRectangle (b, r);

        // Sem sombra: sobre quase preto ela vira mancha. O que separa o botao
        // do fundo e o fio de um pixel.
        g.setColour (primary ? juce::Colours::white.withAlpha (0.14f) : col::stroke);
        g.drawRoundedRectangle (b.reduced (0.5f), r, 1.0f);

        g.setColour (primary ? juce::Colours::white
                             : (accent == juce::Colour() ? col::text : accent));
        g.setFont (uiFont (b.getHeight() * 0.34f, true));
        g.drawText (getButtonText(), b.withTrimmedRight ((float) rightInset),
                    juce::Justification::centred, false);
    }

private:
    bool primary = false;
    int rightInset = 0;
    juce::Colour accent;
};

//==============================================================================
/** Duas ou mais opcoes lado a lado, com a escolhida acesa.

    Existe no lugar de um ComboBox onde as opcoes cabem: escolher entre MAIOR e
    MENOR nao vale um menu que abre, cobre o piano roll e fecha. */
class Segmented : public juce::Component
{
public:
    std::function<void (int)> onChange;

    void setOptions (juce::StringArray opts) { options = std::move (opts); repaint(); }

    void setSelected (int i, juce::NotificationType n = juce::dontSendNotification)
    {
        const int clamped = juce::jlimit (0, juce::jmax (0, options.size() - 1), i);

        if (clamped == selected)
            return;

        selected = clamped;
        repaint();

        if (n != juce::dontSendNotification && onChange)
            onChange (selected);
    }

    int getSelected() const noexcept { return selected; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float r = juce::jmin (radiusControl, b.getHeight() * 0.30f);

        paintSurface (g, b, r);

        if (options.isEmpty())
            return;

        const float w = b.getWidth() / (float) options.size();

        for (int i = 0; i < options.size(); ++i)
        {
            auto cell = b.withX (b.getX() + w * (float) i).withWidth (w);

            if (i == selected)
            {
                g.setColour (col::fillMax);
                g.fillRoundedRectangle (cell.reduced (2.0f), r * 0.8f);
                g.setColour (col::strokeHi);
                g.drawRoundedRectangle (cell.reduced (2.5f), r * 0.8f, 1.0f);
            }

            g.setColour (i == selected ? col::text : col::text2);
            g.setFont (uiFont (b.getHeight() * 0.34f, true));
            g.drawText (options[i], cell, juce::Justification::centred, false);
        }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (options.isEmpty())
            return;

        const int i = (int) ((float) e.position.x / (float) getWidth() * (float) options.size());
        setSelected (i, juce::sendNotification);
    }

private:
    juce::StringArray options;
    int selected = 0;
};

//==============================================================================
/** Menos, valor, mais. Para tom e oitava, onde arrastar seria pior que clicar. */
class Stepper : public juce::Component
{
public:
    std::function<void (int)> onChange;
    std::function<juce::String (int)> format;

    void setRange (int lo, int hi) { low = lo; high = hi; setValue (value); }

    void setValue (int v, juce::NotificationType n = juce::dontSendNotification)
    {
        const int clamped = juce::jlimit (low, high, v);

        if (clamped == value)
            return;

        value = clamped;
        repaint();

        if (n != juce::dontSendNotification && onChange)
            onChange (value);
    }

    int getValue() const noexcept { return value; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float r = juce::jmin (radiusControl, b.getHeight() * 0.30f);

        paintSurface (g, b, r);

        const float side = b.getHeight();

        g.setColour (value > low ? col::text2 : col::text3.withAlpha (0.4f));
        g.setFont (uiFont (b.getHeight() * 0.42f, true));
        g.drawText ("-", b.withWidth (side), juce::Justification::centred, false);

        g.setColour (value < high ? col::text2 : col::text3.withAlpha (0.4f));
        g.drawText ("+", b.withX (b.getRight() - side).withWidth (side),
                    juce::Justification::centred, false);

        g.setColour (col::text);
        g.setFont (uiFont (b.getHeight() * 0.36f, true));
        g.drawText (format ? format (value) : juce::String (value),
                    b.reduced (side, 0.0f), juce::Justification::centred, false);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const float side = (float) getHeight();

        if (e.position.x < side)
            setValue (value - 1, juce::sendNotification);
        else if (e.position.x > (float) getWidth() - side)
            setValue (value + 1, juce::sendNotification);
    }

private:
    int low = 0, high = 11, value = 0;
};

//==============================================================================
/** A linha de uma camada: nome, liga-desliga e volume, na cor da camada.

    Nome e cor no mesmo lugar em que a nota aparece no piano roll -- e o que
    dispensa legenda. */
class LayerRow : public juce::Component
{
public:
    std::function<void (bool)> onToggle;
    std::function<void (float)> onVolume;

    LayerRow (juce::String labelText, juce::Colour c)
        : label (std::move (labelText)), accent (c) {}

    void setState (bool isOn, float vol)
    {
        on = isOn;
        volume = juce::jlimit (0.0f, 1.0f, vol);
        repaint();
    }

    bool isOn() const noexcept { return on; }
    float getVolume() const noexcept { return volume; }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        auto dot = b.removeFromLeft (b.getHeight()).reduced (b.getHeight() * 0.3f);
        g.setColour (on ? accent : col::text3.withAlpha (0.45f));
        g.fillEllipse (dot);

        auto text = b.removeFromLeft (72.0f);
        g.setColour (on ? col::text : col::text3);
        g.setFont (uiFont (10.5f, true));
        g.drawText (label, text, juce::Justification::centredLeft, false);

        // O trilho e o preenchimento: o volume e o unico valor continuo da
        // janela, e uma barra le mais rapido que um numero.
        auto track = b.reduced (4.0f, b.getHeight() * 0.40f);

        g.setColour (col::fill);
        g.fillRoundedRectangle (track, track.getHeight() * 0.5f);

        auto filled = track.withWidth (track.getWidth() * volume);
        g.setColour (on ? accent : col::text3.withAlpha (0.35f));
        g.fillRoundedRectangle (filled, track.getHeight() * 0.5f);

        trackArea = track;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.position.x < (float) getHeight())
        {
            on = ! on;
            repaint();

            if (onToggle)
                onToggle (on);

            return;
        }

        dragTo (e);
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.getMouseDownX() >= getHeight())
            dragTo (e);
    }

private:
    void dragTo (const juce::MouseEvent& e)
    {
        if (trackArea.getWidth() <= 0.0f)
            return;

        const float v = juce::jlimit (0.0f, 1.0f,
                                      (e.position.x - trackArea.getX()) / trackArea.getWidth());

        if (std::abs (v - volume) < 1.0e-4f)
            return;

        volume = v;
        repaint();

        if (onVolume)
            onVolume (volume);
    }

    juce::String label;
    juce::Colour accent;
    bool on = true;
    float volume = 0.8f;
    juce::Rectangle<float> trackArea;
};

//==============================================================================
/** A camada em forma compacta: ponto, nome, e o volume como barra fina embaixo.

    A versao anterior era uma linha larga com trilho separado, e tres delas
    comiam a coluna inteira. Aqui as tres cabem na barra de baixo, ao lado do
    GERAR, que e onde a mao ja esta enquanto se ouve.

    Clique liga e desliga; arrastar na horizontal ajusta o volume. Sao os dois
    gestos que a linha larga tinha, no espaco de um terco. */
class LayerPill : public juce::Component
{
public:
    std::function<void (bool)> onToggle;
    std::function<void (float)> onVolume;

    LayerPill (juce::String labelText, juce::Colour c)
        : label (std::move (labelText)), accent (c) {}

    void setState (bool isOn, float vol)
    {
        on = isOn;
        volume = juce::jlimit (0.0f, 1.0f, vol);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();

        g.setColour (on ? col::fill : col::fill.withMultipliedAlpha (0.5f));
        g.fillRoundedRectangle (b, radiusControl);

        auto dentro = b.reduced (10.0f, 0.0f);
        auto barra = dentro.removeFromBottom (9.0f).withTrimmedBottom (5.0f);

        auto ponto = juce::Rectangle<float> (7.0f, 7.0f)
                       .withCentre ({ dentro.getX() + 3.5f, dentro.getCentreY() + 1.0f });

        g.setColour (on ? accent : col::text3.withAlpha (0.4f));
        g.fillEllipse (ponto);

        g.setColour (on ? col::text : col::text3);
        g.setFont (uiFont (9.5f, true));
        g.drawText (label, dentro.withTrimmedLeft (14.0f).withTrimmedBottom (2.0f),
                    juce::Justification::centredLeft, false);

        g.setColour (col::fillHi);
        g.fillRoundedRectangle (barra, 2.0f);

        g.setColour (on ? accent : col::text3.withAlpha (0.3f));
        g.fillRoundedRectangle (barra.withWidth (barra.getWidth() * volume), 2.0f);

        track = barra;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        arrastou = false;
        partiu = e.position.x;
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! arrastou && std::abs (e.position.x - partiu) < 3.0f)
            return;

        arrastou = true;

        if (track.getWidth() <= 0.0f)
            return;

        const float v = juce::jlimit (0.0f, 1.0f,
                                      (e.position.x - track.getX()) / track.getWidth());

        if (std::abs (v - volume) < 1.0e-4f)
            return;

        volume = v;
        repaint();

        if (onVolume)
            onVolume (volume);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        // So alterna se NAO arrastou: senao ajustar o volume desligaria a
        // camada no fim do gesto, que e o jeito mais rapido de fazer um
        // controle parecer possuido.
        if (arrastou)
            return;

        on = ! on;
        repaint();

        if (onToggle)
            onToggle (on);
    }

private:
    juce::String label;
    juce::Colour accent;
    bool on = true;
    float volume = 0.8f;
    float partiu = 0.0f;
    bool arrastou = false;
    juce::Rectangle<float> track;
};

} // namespace ui
