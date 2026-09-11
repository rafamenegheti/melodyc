/*
  ==============================================================================

    Rack.h -- o slot onde mora o instrumento do usuario.

    E o que faz o produto caber numa faixa so. Sem isto, usar o melody com o
    proprio som exige rotear MIDI entre duas faixas -- trabalho de engenheiro, e
    a primeira coisa que faz alguem desistir de um plugin.

    ---------------------------------------------------------------------------
    O THREAD DE AUDIO NUNCA ESPERA.

    Carregar um plugin leva de centenas de milissegundos a segundos, e acontece
    no thread da mensagem. Se o bloco de audio esperasse essa troca com um lock
    comum, o resultado seria um estouro de prazo audivel toda vez que alguem
    trocasse de instrumento. Aqui o audio usa `tryEnter`: se a troca esta em
    curso, o bloco sai em silencio. Um bloco mudo na hora da troca e barato; um
    estouro no meio de uma gravacao, nao.

    ---------------------------------------------------------------------------
    O BUFFER DO CONVIDADO E DELE, E NAO O NOSSO.

    O plugin carregado pode querer mais canais do que os dois que a DAW nos deu
    -- multi-out e sidechain sao comuns. Entregar um buffer de dois canais a um
    plugin que declara oito e leitura fora do lugar, que num plugin quer dizer a
    DAW caindo. Entao ele processa num buffer proprio, dimensionado no preparo
    pelo que ELE declara, e so os dois primeiros canais voltam.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include <atomic>

class Rack
{
public:
    Rack();
    ~Rack();

    //==========================================================================
    void prepare (double sampleRate, int blockSize);
    void release();

    /** Thread do audio. `false` quer dizer "nao havia som meu para dar" -- e
        quem chama decide se toca o sintetizador interno ou fica quieto.

        O playhead entra por aqui, e nao por um metodo proprio: aplicar no
        convidado exige o ponteiro dele, e pegar o ponteiro exige o lock. Dentro
        de `process` o lock ja esta na mao. */
    bool process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                  juce::AudioPlayHead* ph);

    /** Um bloco OFFLINE, para o arrastar WAV.

        Pega o lock inteiro, e nao o `tryEnter` do audio: aqui esperar e o
        certo, porque o arquivo tem de sair completo. Enquanto dura, o thread de
        audio falha o `tryEnter` e sai mudo -- alguns milissegundos de silencio
        no clique de arrastar, que e o preco de nao processar o convidado por
        dois threads ao mesmo tempo. */
    void renderBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi);

    /** Manda o convidado calar. O render offline deixa nota presa dentro dele,
        porque o envelope dele nao sabe que aquilo era um arquivo. */
    void silenceGuest();

    /** Pergunta barata, para o thread de audio. `loaded()` pega o lock; esta
        nao pega, e e a que pode ser feita a cada bloco. */
    bool hasGuest() const noexcept { return present.load (std::memory_order_relaxed); }

    /** Sobe a cada troca de instrumento. A janela compara com o que viu por
        ultimo: se o convidado trocou por baixo dela -- carregar um preset da DAW
        faz isso -- o editor embutido esta apontando para um plugin ja destruido,
        e desenhar nele derruba o host. */
    int generation() const noexcept { return churn.load (std::memory_order_acquire); }

    //==========================================================================
    // Thread da mensagem.

    /** O IDENTIFICADOR E UMA STRING, E NAO UM CAMINHO.

        Para VST3 os dois coincidem -- e o caminho do bundle. Para Audio Unit,
        nao: AU e identificado por uma string do registro do sistema, e passar o
        caminho do .component devolve "nenhum plugin dentro dele". Foi o que
        aconteceu na primeira versao, e o sintoma era o slot listar os AU e
        recusar todos eles. */
    bool load (const juce::String& identifier, juce::String& error);
    void unload();

    bool loaded() const;
    juce::String name() const;
    juce::String source() const;

    /** A janela do convidado. Devolve nulo se ele nao tiver uma. */
    juce::AudioProcessorEditor* createGuestEditor() const;

    //==========================================================================
    struct Found
    {
        juce::String format;
        juce::String name;
        juce::String identifier;

        /** Se e instrumento, decidido SEM CARREGAR o plugin.

            Carregar 62 plugins para montar uma lista custaria segundos e
            arriscaria a DAW num bundle quebrado. Para Audio Unit o proprio
            identificador diz o tipo (`aumu` e instrumento). Para VST3, o bundle
            traz um `moduleinfo.json` que declara as subcategorias -- basta
            procurar "Instrument" nele. */
        bool instrument = false;
    };

    /** Os plugins instalados. A varredura e a do proprio JUCE, que so ANDA nas
        pastas e le o registro de AU -- nao instancia nada, entao um bundle
        quebrado nao derruba a DAW e a lista sai na hora. */
    juce::Array<Found> installed() const;

    //==========================================================================
    void getState (juce::MemoryBlock& dest) const;
    void setState (const void* data, int size);

private:
    void configure (juce::AudioPluginInstance& inst) const;

    juce::AudioPluginFormatManager formats;

    /** Protege a TROCA do ponteiro, e nao o processamento. */
    juce::CriticalSection lock;

    std::unique_ptr<juce::AudioPluginInstance> guest;
    juce::String identifier;

    juce::AudioBuffer<float> scratch;
    std::atomic<bool> present { false };
    std::atomic<int> churn { 0 };

    double sr = 44100.0;
    int block = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Rack)
};
