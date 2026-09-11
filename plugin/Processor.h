/*
  ==============================================================================

    Processor.h -- o plugin.

    A FRASE E PUBLICADA, NAO EDITADA. Sao dois slots e um indice atomico: quem
    gera escreve no slot que o audio nao esta lendo e so entao troca o indice.
    Editar a frase viva no lugar seria ler nota pela metade no meio de um bloco
    -- e o sintoma seria uma nota errada de vez em quando, que e o defeito mais
    caro de achar que existe.

    A TROCA PENDURA NOTA, E POR ISSO A TROCA PEDE SILENCIO. `Player` nao guarda
    quais notas estao soando (o motivo esta no cabecalho dele), entao quem troca
    de frase levanta uma bandeira e o proximo bloco manda all-notes-off antes de
    qualquer coisa.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>

#include "core/Bank.h"
#include "core/Generator.h"
#include "core/Player.h"
#include "core/Synth.h"
#include "plugin/Params.h"
#include "plugin/Rack.h"

// O smoke nao e um alvo de plugin, entao o JUCE nao gera as macros de formato
// para ele. Sem este padrao, o mesmo Processor.cpp compila no plugin e nao
// compila no binario que existe justamente para testa-lo.
#ifndef JucePlugin_IsMidiEffect
 #define JucePlugin_IsMidiEffect 0
#endif

//==============================================================================
class MelodyProcessor : public juce::AudioProcessor,
                        private juce::AudioProcessorValueTreeState::Listener
{
public:
    MelodyProcessor();
    ~MelodyProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "melody"; }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return midiEffect; }

    /** Duas versoes saem do mesmo codigo: o instrumento, com o som de
        conferencia, e o efeito MIDI, que so manda nota. A janela esconde o
        botao SOM INTERNO na segunda -- um botao que nao pode fazer nada e pior
        que um botao ausente. */
    static constexpr bool midiEffect = JucePlugin_IsMidiEffect != 0;
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==========================================================================
    /** Gera uma frase nova. Thread da mensagem, nunca do audio. */
    void regenerate (std::uint32_t seed);

    /** De novo com a semente que ja esta valendo -- e o que o tom e a oitava
        chamam quando mudam, para a frase seguir o controle sem trocar de
        melodia debaixo do usuario.

        PODE SER CHAMADO DO THREAD DE AUDIO, e e de proposito: quem muda o tom
        pode ser a automacao do host, e um plugin que so obedece ao tom quando a
        janela esta aberta e um defeito que o usuario descobre no bounce. O
        gerador nao aloca (ha um caso na suite que mede isso) e publicar e
        memcpy de cinco kilobytes, entao o custo cabe num bloco. */
    void refresh();

    std::uint32_t currentSeed() const noexcept { return seed; }

    /** A frase que esta soando. Thread da mensagem: a janela desenha daqui. */
    const melody::Phrase& livePhrase() const noexcept
    {
        return phrases[(std::size_t) live.load (std::memory_order_acquire)];
    }

    const melody::Bank& theBank() const noexcept { return bank; }

    //==========================================================================
    /** Renderiza a frase inteira num buffer, para o arrastar WAV.

        Passa pelo instrumento carregado quando ha um -- o arquivo tem de soar
        como o que se ouve, senao arrastar WAV entrega um som que nao e o do
        projeto. Sem convidado, usa um sintetizador PROPRIO, e nao o do audio:
        compartilhar o de tocar deixaria o render pegar vozes no meio e a
        audicao pegar as do arquivo.

        Thread da mensagem. Devolve false se nao ha frase. */
    bool renderPhrase (juce::AudioBuffer<float>& out, double sampleRate);

    /** O botao TOCAR: um transporte proprio, para conferir sem rodar a DAW. */
    void startAudition();
    void stopAudition();
    bool auditioning() const noexcept { return internalOn.load (std::memory_order_relaxed); }

    /** O transporte do host esta mandando?

        Existem DUAS fontes de tempo, e o botao so olhava uma. Com o host
        rodando, o plugin toca e o botao continuava escrito TOCAR -- e apertar
        ele nao parava nada, porque nao havia audicao interna para parar. */
    bool hostDriving() const noexcept { return fromHost.load (std::memory_order_relaxed); }

    /** Onde a cabeca esta, em batidas dentro do laco. A janela desenha o
        cursor com isto. */
    double playPosition() const noexcept
    {
        return position.load (std::memory_order_relaxed);
    }

    bool isMoving() const noexcept { return moving.load (std::memory_order_relaxed); }

    /** O andamento em vigor: o do host quando ha host, o do trecho quando nao
        ha. O trecho traz o BPM do arquivo de onde saiu, e usar esse numero e o
        que faz a frase soar como soava.

        O valor so era escrito quando o transporte rodava, entao quem gerava e
        arrastava sem nunca dar play exportava um .mid com 140 BPM fixo -- o
        chute inicial -- para uma frase de 160. */
    double tempo() const noexcept { return bpm.load (std::memory_order_relaxed); }

    /** O instrumento do usuario, morando dentro do plugin. E o que faz tudo
        caber numa faixa so. */
    Rack rack;

    juce::AudioProcessorValueTreeState apvts;

private:
    //==========================================================================
    void publish (const melody::Phrase& p);
    void parameterChanged (const juce::String& id, float value) override;
    void emitBlock (juce::MidiBuffer& midi, double from, double to,
                    int numSamples, double beatsPerSample);

    melody::Bank bank;
    melody::Generator gen;
    melody::Synth synth;

    /** A copia que vai para o convidado. O convidado pode LIMPAR o buffer de
        MIDI que recebe -- varios instrumentos limpam -- e ai a saida MIDI do
        melody para o resto do projeto sumiria junto. Membro, e nao local:
        `clear` guarda a capacidade, entao depois dos primeiros blocos nao ha
        alocacao nenhuma no caminho do audio. */
    juce::MidiBuffer guestMidi;

    bool guestWasLoaded = false;

    /** Como as camadas estavam no bloco anterior. Desligar uma tem de calar
        AGORA, e nao quando a nota acabar por conta propria. */
    bool layerWas[3] { true, true, true };

    melody::Phrase phrases[2];
    std::atomic<int> live { 0 };
    std::atomic<bool> panic { false };

    std::atomic<bool> internalOn { false };
    std::atomic<bool> moving { false };
    std::atomic<bool> fromHost { false };
    std::atomic<double> position { 0.0 };
    std::atomic<double> bpm { 140.0 };
    std::atomic<bool> sawHostTempo { false };

    double internalBeat = 0.0;
    double sampleRate = 44100.0;

    /** O fim da janela do bloco anterior, e se o bloco anterior andava.

        Sao os dois unicos pedacos de estado de transporte que existem, e ambos
        servem para a mesma coisa: reconhecer a DESCONTINUIDADE. `Player` nao
        guarda quais notas soam de proposito (o motivo esta no cabecalho dele),
        entao parar, saltar ou dar play sao os momentos em que alguem precisa
        pedir silencio -- senao a nota que estava no meio nunca recebe o
        note-off e fica presa para sempre. */
    double lastEnd = -1.0;
    bool wasRunning = false;

    std::uint32_t seed = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MelodyProcessor)
};
