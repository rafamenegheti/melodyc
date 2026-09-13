/*
  ==============================================================================

    Editor.h -- a janela.

    Um botao GERAR, um TOCAR, e o piano roll ao lado. O ciclo que o produto
    precisa ter e "gera, olha, escuta, gera de novo" -- tudo o que fica entre
    dois GERAR e atrito, e por isso tom, escala e oitava sao um clique cada e
    nao um menu que abre.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>

#include "plugin/Processor.h"
#include "plugin/ui/MenuLook.h"
#include "plugin/ui/PianoRoll.h"

#include "plugin/ui/Widgets.h"

//==============================================================================
/** O botao de arrastar. Escreve a frase num .mid temporario e entrega ao
    sistema -- e como a nota sai daqui para a DAW sem passar por menu de
    exportacao. */
class MidiDragButton : public ui::FlatButton
{
public:
    explicit MidiDragButton (MelodyProcessor& p)
        : ui::FlatButton ("ARRASTAR MIDI"), proc (p) {}

    void setMenuLook (juce::LookAndFeel* l) { menuLook = l; }

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void paintButton (juce::Graphics&, bool over, bool down) override;

    /** Publico porque o smoke precisa provar que o arquivo sai certo sem
        arrastar nada. */
    juce::File writeMidiFile() const;

    /** A largura da zona da seta, na direita do botao. */
    static constexpr int arrowZone = 28;

private:
    MelodyProcessor& proc;
    juce::LookAndFeel* menuLook = nullptr;
    bool noMenu = false;

    /** Ja entregou o arquivo ao sistema neste gesto. Ver `iniciaArrasto`. */
    bool arrastando = false;
};

//==============================================================================
/** O mesmo arrastar, com audio.

    Renderiza pelo instrumento carregado quando ha um: o arquivo tem de soar
    como o que se ouve, senao arrastar WAV entrega um som que nao e o do
    projeto. */
class WavDragButton : public ui::FlatButton
{
public:
    explicit WavDragButton (MelodyProcessor& p)
        : ui::FlatButton ("ARRASTAR WAV"), proc (p) {}

    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;

    juce::File writeWavFile() const;

private:
    MelodyProcessor& proc;
    bool arrastando = false;
};

//==============================================================================
class MelodyEditor : public juce::AudioProcessorEditor,
                     private juce::Timer,
                     private juce::ComponentListener
{
public:
    explicit MelodyEditor (MelodyProcessor&);
    ~MelodyEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Para a captura do smoke: gera uma frase conhecida. */
    /** A frase da captura. A semente e parametro porque conferir um layout em
        UMA frase nao prova nada: foi assim que eu conclui, de uma captura so,
        que o baixo comia o campo vertical -- e a medicao depois mostrou que
        nao. */
    void demoPhrase (std::uint32_t semente = 20250908u);

    /** Idem: abre o instrumento embutido, para a foto mostrar o encaixe. */
    void openGuestForShot();

    /** Congela a animacao e o cursor, para a captura. */
    void poseForShot (float progresso, double head, bool headOn, int grupo = -1)
    {
        roll.poseForShot (progresso, head, headOn);

        // O pulso do botao comecou no MESMO instante da chegada da frase -- os
        // dois sao a resposta ao mesmo clique. A captura tem de congelar os dois
        // no mesmo ponto do tempo, senao mostra um estado que nunca existe.
        generate.poseForShot ((juce::uint32) (juce::jlimit (0.0f, 1.0f, progresso) * 420.0f));

        juce::ignoreUnused (grupo);
    }

    /** Aperta os botoes, para o smoke provar que eles estao ligados em alguma
        coisa. Um `onClick` que ninguem atribuiu compila, desenha certo e nao
        faz nada -- foi o que aconteceu quando um recorte de layout levou junto
        a ligacao do GERAR e do TOCAR. */
    void clickForTest (const juce::String& qual);

    /** O que esta escrito no botao de tocar, para o smoke comparar com o
        estado. Rotulo que discorda do estado foi defeito relatado. */
    juce::String playLabelForTest() const { return play.getButtonText(); }

    /** Um tique do timer, para o smoke ver o rotulo sem laco de mensagens. */
    void timerForTest() { timerCallback(); }

    /** O editor do convidado, para o smoke medir o encaixe. Foto nao serve:
        editor de plugin hospedado e view nativa, e captura por software desenha
        preto no lugar dela. */
    const juce::Component* guestEditorForTest() const
    {
        return guestView.getViewedComponent();
    }

private:
    void timerCallback() override;
    void syncFromParams();

    MelodyProcessor& proc;

    ui::Segmented source, scale, routing, length;
    ui::Stepper key;
    ui::FlatButton generate { "GERAR", true };
    ui::FlatButton play { "TOCAR" };
    ui::FlatButton soundSlot { "SOM INTERNO" };
    ui::FlatButton openGuest { "ABRIR" };
    MidiDragButton dragMidi;
    WavDragButton dragWav;

    /** O INSTRUMENTO OCUPA A JANELA INTEIRA, com uma barra de VOLTAR em cima.

        Foram tres formas ate esta. Janela solta: no Live as janelas de plugin
        ficam sempre por cima, entao as duas brigavam pelo topo e nao havia como
        trazer o instrumento de volta. Embutido ao lado dos controles: a janela
        precisava caber os dois, e um instrumento de 1.180 pixels empurrava o
        melody para 1.430 de largura com barra de rolagem. Aqui a janela vira o
        tamanho do instrumento e mais nada -- os controles do melody somem
        enquanto ele esta aberto, porque as duas coisas nao sao olhadas ao mesmo
        tempo.

        Dentro de um Viewport porque editor de instrumento tem o tamanho que
        quiser: o que nao couber na tela rola, em vez de ficar cortado. */
    juce::Viewport guestView;
    bool showingGuest = false;
    bool resizingForGuest = false;
    int seenGeneration = 0;

    std::unique_ptr<juce::FileChooser> chooser;

    void componentMovedOrResized (juce::Component&, bool, bool) override;

    void setChromeVisible (bool visible);
    void showSoundMenu();
    void loadGuest (const juce::String& identifier);
    void toggleGuestEditor();
    void openGuestEditor();
    void closeGuestEditor();
    void fitToGuest();
    void refreshSoundSlot();
    void refreshPlayButton();

    ui::LayerPill melodyRow { "MELODIA", ui::col::melody };
    ui::LayerPill chordsRow { "ACORDES", ui::col::chords };
    ui::LayerPill bassRow   { "BAIXO",   ui::col::bass };

    ui::PianoRoll roll;
    ui::MenuLook menuLook;


    /** Os rotulos de secao sao posicionados em `resized`, junto do que eles
        rotulam. Escritos a mao no `paint`, eles ficaram para tras assim que o
        layout mudou uma vez -- CAMADAS foi parar embaixo do seletor de escala,
        e nada no build reclamou. */
    struct Caption { juce::Rectangle<int> area; const char* text; };
    juce::Array<Caption> captions;

    juce::String info;

    void updateInfo();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyEditor)
};
