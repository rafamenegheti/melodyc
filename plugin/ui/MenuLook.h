/*
  ==============================================================================

    MenuLook.h -- o menu, na linguagem da janela.

    O JUCE desenha menu com o visual padrao dele, que e cinza claro de outro
    aplicativo. Num plugin escuro isso e o unico lugar onde a interface troca de
    identidade, e troca justamente quando o usuario esta escolhendo alguma coisa.

    So os metodos de menu sao sobrescritos. Um LookAndFeel completo obrigaria a
    responder por todo componente do JUCE que a janela nunca usa.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "plugin/ui/Theme.h"

namespace ui
{

class MenuLook : public juce::LookAndFeel_V4
{
public:
    MenuLook()
    {
        setColour (juce::PopupMenu::backgroundColourId, col::raised);
        setColour (juce::PopupMenu::textColourId, col::text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent);
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::PopupMenu::headerTextColourId, col::text3);
    }

    void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
    {
        auto b = juce::Rectangle<float> ((float) w, (float) h).reduced (0.5f);

        g.setColour (col::raised);
        g.fillRoundedRectangle (b, radiusPanel);

        g.setColour (col::strokeHi);
        g.drawRoundedRectangle (b, radiusPanel, 1.0f);
    }

    void getIdealPopupMenuItemSize (const juce::String& text, bool separator,
                                    int standardHeight, int& w, int& h) override
    {
        if (separator)
        {
            w = 60;
            h = 11;
            return;
        }

        const auto f = uiFont (12.0f);
        w = juce::GlyphArrangement::getStringWidthInt (f, text) + 62;
        h = juce::jmax (26, standardHeight);
    }

    void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area,
                            bool separator, bool active, bool highlighted,
                            bool ticked, bool hasSubMenu, const juce::String& text,
                            const juce::String& shortcut,
                            const juce::Drawable* icon,
                            const juce::Colour* textColour) override
    {
        juce::ignoreUnused (icon, textColour, shortcut);

        if (separator)
        {
            g.setColour (col::stroke);
            g.fillRect (area.getX() + 12, area.getCentreY(), area.getWidth() - 24, 1);
            return;
        }

        auto r = area.reduced (6, 1).toFloat();

        if (highlighted && active)
        {
            g.setColour (col::accent);
            g.fillRoundedRectangle (r, radiusControl - 2.0f);
        }

        const auto cor = ! active ? col::text3
                                  : (highlighted ? juce::Colours::white : col::text);

        // O visto e desenhado, e nao um caractere: o tique de fonte muda de
        // desenho conforme a familia que o sistema resolver.
        if (ticked)
        {
            juce::Path v;
            const float cx = r.getX() + 14.0f, cy = r.getCentreY();
            v.startNewSubPath (cx - 4.0f, cy);
            v.lineTo (cx - 1.0f, cy + 3.5f);
            v.lineTo (cx + 5.0f, cy - 4.0f);

            g.setColour (cor);
            g.strokePath (v, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
        }

        g.setColour (cor);
        g.setFont (uiFont (12.0f));
        g.drawText (text, r.withTrimmedLeft (28.0f).withTrimmedRight (hasSubMenu ? 22.0f : 8.0f),
                    juce::Justification::centredLeft, true);

        if (hasSubMenu)
        {
            juce::Path seta;
            const float cx = r.getRight() - 13.0f, cy = r.getCentreY();
            seta.startNewSubPath (cx - 2.0f, cy - 4.0f);
            seta.lineTo (cx + 2.5f, cy);
            seta.lineTo (cx - 2.0f, cy + 4.0f);

            g.setColour (cor.withMultipliedAlpha (0.8f));
            g.strokePath (seta, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded));
        }
    }

    void drawPopupMenuSectionHeader (juce::Graphics& g, const juce::Rectangle<int>& area,
                                     const juce::String& text) override
    {
        paintCaption (g, area.reduced (14, 0).toFloat(), text, col::text3, 9.0f);
    }
};

} // namespace ui
