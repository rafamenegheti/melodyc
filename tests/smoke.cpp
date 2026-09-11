/*
  ==============================================================================

    smoke.cpp -- o processador de verdade, sem host.

    melody_tests prova o gerador sem JUCE nenhum, em milissegundos. Este alvo
    prova o que so existe depois que o JUCE entra: a nota chegando ao buffer
    MIDI na amostra certa, o estado sobrevivendo ao round-trip COM a semente, o
    .mid que sai do arrastar, e a janela que abre.

    O MODO --shot existe porque conferir interface por descricao nao funciona.

  ==============================================================================
*/

#include <JuceHeader.h>
#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "plugin/Editor.h"
#include "plugin/Processor.h"

namespace
{

int failures = 0;

void check (bool ok, const juce::String& what)
{
    if (! ok)
    {
        std::printf ("      FALHOU  %s\n", what.toRawUTF8());
        ++failures;
    }
}

//==============================================================================
/** Um host que roda. Sem isto o processador nunca sai do lugar e todo caso de
    transporte mediria silencio e passaria. */
class FakeHost : public juce::AudioPlayHead
{
public:
    double beat = 0.0;
    double bpm = 140.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (playing);
        p.setPpqPosition (beat);
        p.setBpm (bpm);
        p.setTimeInSamples (0);
        return p;
    }
};

/** O saldo de cada nota: quantas vezes ligou menos quantas vezes desligou.

    E a unica forma de medir "nota presa" -- olhar o buffer de um bloco so nao
    diz nada, porque a nota legitimamente atravessa dezenas de blocos. */
class Hanging
{
public:
    void feed (const juce::MidiBuffer& b)
    {
        for (const auto ev : b)
        {
            const auto m = ev.getMessage();
            const int ch = m.getChannel();

            if (m.isNoteOn())
                ++open[key (ch, m.getNoteNumber())];
            else if (m.isNoteOff())
                open[key (ch, m.getNoteNumber())] = 0;
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                for (auto& e : open)
                    if (e.first / 128 == ch)
                        e.second = 0;
        }
    }

    int count() const
    {
        int n = 0;

        for (const auto& e : open)
            if (e.second > 0)
                ++n;

        return n;
    }

    juce::String describe() const
    {
        juce::String out;

        for (const auto& e : open)
            if (e.second > 0)
                out << "canal " << juce::String (e.first / 128)
                    << " nota " << juce::String (e.first % 128) << "  ";

        return out;
    }

private:
    static int key (int ch, int note) { return ch * 128 + note; }

    std::map<int, int> open;
};

int countNoteOns (const juce::MidiBuffer& b)
{
    int n = 0;

    for (const auto m : b)
        if (m.getMessage().isNoteOn())
            ++n;

    return n;
}

//==============================================================================
void testGeraNaConstrucao()
{
    MelodyProcessor p;

    check (p.theBank().valid(), "o banco embutido carrega");
    check (p.theBank().size() > 1000, "o banco tem os trechos todos");
    check (! p.livePhrase().empty(), "ja existe frase antes de qualquer clique");

    std::printf ("      %d trechos no binario, frase inicial com %d notas\n",
                 p.theBank().size(), p.livePhrase().melodyCount);
}

/** O BOTAO DE TOCAR NAO PODE MENTIR.

    Dois defeitos relatados, e os dois sao a mesma causa: existem duas fontes de
    tempo -- o transporte do host e a audicao interna -- e o botao so olhava
    uma. Com o host rodando ele continuava escrito TOCAR com som saindo, e
    apertar espaco para parar nao parava, porque a audicao interna assumia. */
void testBotaoTocarNaoMente()
{
    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto* me = dynamic_cast<MelodyEditor*> (ed.get());

    if (me == nullptr)
    {
        check (false, "a janela abre");
        return;
    }

    FakeHost host;
    host.playing = false;
    p.setPlayHead (&host);

    auto correr = [&p] (int blocos)
    {
        for (int i = 0; i < blocos; ++i)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);
        }
    };

    auto energia = [&p, &host] (int blocos)
    {
        double soma = 0.0;

        for (int i = 0; i < blocos; ++i)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);

            if (host.playing)
                host.beat += 512.0 * host.bpm / (60.0 * 48000.0);

            for (int t = 0; t < 512; ++t)
                soma += std::abs (buf.getSample (0, t));
        }

        return soma;
    };

    // Sem host: o botao e um interruptor comum.
    correr (2);
    me->timerForTest();
    check (me->playLabelForTest() == "TOCAR", "parado, o botao diz TOCAR");

    me->clickForTest ("tocar");
    check (p.auditioning(), "clicar liga a audicao");
    check (me->playLabelForTest() == "PARAR", "e o rotulo vira PARAR");

    me->clickForTest ("tocar");
    check (! p.auditioning(), "clicar de novo desliga");
    me->timerForTest();
    check (me->playLabelForTest() == "TOCAR", "e o rotulo volta a TOCAR");

    // O CASO RELATADO: apertar TOCAR e depois rodar o projeto.
    me->clickForTest ("tocar");
    check (p.auditioning(), "audicao ligada antes do host rodar");

    host.playing = true;
    correr (4);
    me->timerForTest();

    check (p.hostDriving(), "o plugin sabe que quem manda agora e o host");
    check (! p.auditioning(), "o host desliga a audicao interna");
    check (me->playLabelForTest() == "HOST", "e o botao diz HOST, em vez de mentir");

    // Apertar o botao com o host rodando nao pode ligar nada por baixo.
    me->clickForTest ("tocar");
    check (! p.auditioning(), "clicar com o host rodando nao liga a audicao");

    // E AGORA O ESPACO: parar o transporte tem de dar silencio.
    const double tocando = energia (20);

    host.playing = false;
    const double depois = energia (60);

    std::printf ("      energia com host %.1f | depois do espaco %.6f\n", tocando, depois);

    check (tocando > 1.0, "com o host rodando sai som");
    check (depois < 1.0e-3, "parar o transporte para o som de verdade");

    me->timerForTest();
    check (me->playLabelForTest() == "TOCAR", "e o botao volta a oferecer TOCAR");
}

/** Instancia nova abre com frase SORTEADA.

    Abrir sempre com a mesma melodia faz o plugin parecer que so tem uma. O caso
    pede oito instancias e aceita uma coincidencia ou outra -- com 2.872 trechos
    menores e sorteio enviesado para a frente do banco, dois iguais em oito
    acontece de vez em quando e nao e defeito. Seis distintas seriam
    impossiveis se a semente fosse fixa. */
void testAbreSorteado()
{
    std::set<int> vistos;

    for (int i = 0; i < 8; ++i)
    {
        MelodyProcessor p;
        vistos.insert (p.livePhrase().melodyIndex);
    }

    std::printf ("      8 instancias novas -> %d frases distintas\n", (int) vistos.size());

    check ((int) vistos.size() >= 6, "instancia nova nao abre sempre na mesma melodia");

    // E reabrir um projeto continua devolvendo a MESMA -- o sorteio nao pode
    // atropelar o que ja foi gravado.
    MelodyProcessor origem;
    origem.regenerate (555u);

    juce::MemoryBlock blob;
    origem.getStateInformation (blob);

    for (int i = 0; i < 3; ++i)
    {
        MelodyProcessor destino;
        destino.setStateInformation (blob.getData(), (int) blob.getSize());

        check (destino.livePhrase().melodyIndex == origem.livePhrase().melodyIndex,
               "reabrir o projeto devolve a frase gravada, e nao uma sorteada");
    }
}

/** A nota chega ao buffer MIDI, e chega uma vez por volta do laco. */
void testTransporteEmiteNota()
{
    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    FakeHost host;
    p.setPlayHead (&host);

    const auto& frase = p.livePhrase();
    const int esperadas = frase.melodyCount + frase.chordCount + frase.bassCount;

    // Uma volta EXATA. A 120 BPM, 16 batidas sao 8 segundos, 384.000 amostras,
    // 750 blocos de 512 sem sobra. Com um andamento que nao divide, o ultimo
    // bloco entra na volta seguinte e as notas do tempo zero contam duas vezes.
    host.bpm = 120.0;
    const double beatsPerBlock = 512.0 * host.bpm / (60.0 * 48000.0);
    const int blocos = 750;    // 16 batidas exatas, o laco de quatro compassos

    int total = 0;

    for (int i = 0; i < blocos; ++i)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;

        p.processBlock (buf, midi);
        total += countNoteOns (midi);

        host.beat = (double) ((i + 1) * 512) * host.bpm / (60.0 * 48000.0);
    }

    juce::ignoreUnused (beatsPerBlock);

    std::printf ("      %d note-on em uma volta, frase tem %d notas\n", total, esperadas);

    check (total > 0, "o transporte do host faz o plugin emitir nota");

    check (total == esperadas, "cada nota da frase soa exatamente uma vez por volta");
}

/** Oito compassos dao a volta em 32 batidas, e nao em 16.

    O laco era uma constante no tocador. Se ele voltar a ser, este caso e o
    unico lugar em que a frase de oito compassos daria a volta na metade -- e o
    sintoma seria a segunda metade nunca tocar, que ninguem associaria a uma
    constante. */
void testOitoCompassos()
{
    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    if (auto* prm = p.apvts.getParameter (pid::length))
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) Length::eight));

    const auto& frase = p.livePhrase();

    check (frase.bars == melody::longBars, "pedir oito devolve uma frase de oito");
    check (std::abs (frase.loopBeats() - 32.0) < 1.0e-9, "o laco tem 32 batidas");

    FakeHost host;
    host.bpm = 120.0;
    p.setPlayHead (&host);

    const int esperadas = frase.melodyCount + frase.chordCount + frase.bassCount;

    int total = 0;

    for (int i = 0; i < 1500; ++i)          // 32 batidas exatas a 120 BPM
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        total += countNoteOns (midi);

        // A BATIDA VEM DA CONTAGEM DE AMOSTRAS, e nao de somar o passo.
        // Somando, o erro de arredondamento acumula em 1.500 blocos e o ultimo
        // entra na volta seguinte -- as notas do tempo zero contam duas vezes,
        // e o caso acusa defeito onde nao ha.
        host.beat = (double) ((i + 1) * 512) * host.bpm / (60.0 * 48000.0);
    }

    std::printf ("      %d note-on numa volta de oito compassos, frase tem %d notas\n",
                 total, esperadas);

    check (total == esperadas, "cada nota da frase de oito soa uma vez por volta");

    // E o .mid do arrastar tem oito compassos, nao quatro.
    MidiDragButton drag (p);
    const auto file = drag.writeMidiFile();

    juce::FileInputStream in (file);
    juce::MidiFile lido;

    if (lido.readFrom (in) && lido.getNumTracks() == 1)
    {
        double fim = 0.0;

        for (int e = 0; e < lido.getTrack (0)->getNumEvents(); ++e)
            fim = juce::jmax (fim, lido.getTrack (0)->getEventPointer (e)->message.getTimeStamp());

        const double compassos = fim / (960.0 * 4.0);

        std::printf ("      o .mid do arrastar tem %.1f compassos\n", compassos);
        check (compassos > 4.0, "o .mid exportado tem oito compassos, e nao quatro");
    }

    file.deleteFile();
}

/** Parado, o plugin nao emite nada -- e a diferenca entre um instrumento e um
    plugin que enche o projeto de nota sozinho. */
void testParadoNaoEmite()
{
    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    FakeHost host;
    host.playing = false;
    p.setPlayHead (&host);

    int total = 0;

    for (int i = 0; i < 40; ++i)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        total += countNoteOns (midi);
    }

    check (total == 0, "com o host parado o plugin nao emite nota");

    // E o botao TOCAR anda sozinho, sem host nenhum.
    p.startAudition();

    int comAudicao = 0;
    double energia = 0.0;

    for (int i = 0; i < 200; ++i)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        comAudicao += countNoteOns (midi);

        for (int s = 0; s < 512; ++s)
            energia += std::abs (buf.getSample (0, s));
    }

    std::printf ("      audicao interna: %d note-on, energia %.1f\n", comAudicao, energia);

    check (comAudicao > 0, "o botao TOCAR anda sem host");
    check (energia > 1.0, "o som interno realmente sai no buffer");
}

/** O som interno desligado tem de dar silencio EXATO -- nao "quase". */
void testSomInternoDesliga()
{
    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    if (auto* prm = p.apvts.getParameter (pid::internal))
        prm->setValueNotifyingHost (0.0f);

    p.startAudition();

    double pico = 0.0;
    int notas = 0;

    for (int i = 0; i < 200; ++i)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        notas += countNoteOns (midi);

        for (int s = 0; s < 512; ++s)
            pico = juce::jmax (pico, (double) std::abs (buf.getSample (0, s)));
    }

    check (pico == 0.0, "som interno desligado da silencio exato");
    check (notas > 0, "e o MIDI continua saindo com o som interno desligado");
}

/** Mesma semente, mesma frase. Sem isto, reabrir um projeto devolve outra
    musica. */
void testDeterminismo()
{
    MelodyProcessor a, b;

    a.regenerate (123456u);
    b.regenerate (123456u);

    const auto& x = a.livePhrase();
    const auto& y = b.livePhrase();

    check (x.melodyCount == y.melodyCount, "a mesma semente da a mesma contagem de notas");
    check (x.melodyIndex == y.melodyIndex, "a mesma semente escolhe o mesmo trecho");

    bool iguais = x.melodyCount == y.melodyCount;

    for (int i = 0; i < x.melodyCount && i < y.melodyCount; ++i)
        if (x.melody[i].pitch != y.melody[i].pitch || x.melody[i].pos != y.melody[i].pos)
            iguais = false;

    check (iguais, "a mesma semente da a mesma frase, nota a nota");

    // Vinte sementes vizinhas. Uma coincidencia entre duas e estatistica; o
    // que o caso pega e o defeito de verdade, que era metade das sementes
    // colapsar na outra metade antes da mistura.
    std::set<int> distintas;

    for (std::uint32_t s = 500000u; s < 500020u; ++s)
    {
        a.regenerate (s);
        distintas.insert (a.livePhrase().melodyIndex);
    }

    std::printf ("      20 sementes vizinhas -> %d trechos distintos\n",
                 (int) distintas.size());

    check ((int) distintas.size() >= 17, "sementes vizinhas dao frases diferentes");
}

/** O estado salvo leva a SEMENTE. E o unico caso deste arquivo que protege o
    projeto que o usuario ja gravou. */
void testEstadoLevaASemente()
{
    MelodyProcessor a;
    a.regenerate (987654321u);

    if (auto* prm = a.apvts.getParameter (pid::key))
        prm->setValueNotifyingHost (prm->convertTo0to1 (3.0f));

    juce::MemoryBlock blob;
    a.getStateInformation (blob);
    check (blob.getSize() > 0, "o estado salvo nao e vazio");

    MelodyProcessor b;
    b.setStateInformation (blob.getData(), (int) blob.getSize());

    check (b.currentSeed() == a.currentSeed(), "a semente sobrevive ao round-trip");

    const auto& x = a.livePhrase();
    const auto& y = b.livePhrase();

    check (x.melodyIndex == y.melodyIndex, "e o trecho reaberto e o mesmo");
    check (x.root == y.root, "o tom reaberto e o mesmo");

    bool iguais = x.melodyCount == y.melodyCount;

    for (int i = 0; i < x.melodyCount && i < y.melodyCount; ++i)
        if (x.melody[i].pitch != y.melody[i].pitch)
            iguais = false;

    check (iguais, "a frase reaberta e a mesma, nota a nota");
}

/** O .mid do arrastar e UM clipe, e nao tres.

    Escrever as camadas em trilhas separadas fazia a DAW criar tres faixas ao
    receber o arraste -- o oposto de arrastar para o instrumento que ja esta
    ali. Numa trilha so aparece o problema que este caso existe para pegar: 37%
    das frases tem alguma altura repetida entre camadas, e duas notas iguais
    sobrepostas no mesmo canal sao um note-on sem par. */
void testArquivoMidi()
{
    int comFusao = 0;

    for (unsigned semente : { 42u, 7u, 1234u, 99999u, 5u, 777u, 31337u, 8u })
    {
        MelodyProcessor p;
        p.regenerate (semente);

        MidiDragButton drag (p);
        const auto file = drag.writeMidiFile();

        check (file.existsAsFile(), "o arquivo .mid e escrito");

        if (! file.existsAsFile())
            return;

        juce::FileInputStream in (file);
        juce::MidiFile lido;

        check (lido.readFrom (in), "o .mid escrito e legivel");
        check (lido.getNumTracks() == 1, "o .mid tem UMA trilha, para virar um clipe so");

        if (lido.getNumTracks() != 1)
            return;

        const auto& frase = p.livePhrase();
        const int geradas = frase.melodyCount + frase.chordCount + frase.bassCount;

        const auto* trilha = lido.getTrack (0);

        int notas = 0;
        bool temAndamento = false;
        double andamento = 0.0;
        std::vector<std::pair<double, double>> soando[128];

        for (int e = 0; e < trilha->getNumEvents(); ++e)
        {
            const auto& m = trilha->getEventPointer (e)->message;

            if (m.isTempoMetaEvent())
            {
                temAndamento = true;
                andamento = 60.0 / m.getTempoSecondsPerQuarterNote();
            }

            if (! m.isNoteOn())
                continue;

            ++notas;

            const double on = m.getTimeStamp();
            const double off = trilha->getTimeOfMatchingKeyUp (e);

            check (off > on, "toda nota do arquivo tem um note-off depois dela");
            soando[m.getNoteNumber()].push_back ({ on, off });
        }

        check (temAndamento, "o .mid leva o andamento -- senao abre a 120");

        // E o andamento e o DA FRASE, e nao o chute inicial: quem gera e
        // arrasta sem nunca dar play exportava tudo a 140.
        check (std::abs (andamento - frase.bpm) < 1.0,
               "o andamento do arquivo e o do trecho, nao o padrao");
        check (notas > 0, "o .mid tem nota");
        check (notas <= geradas, "fundir nunca inventa nota");

        if (notas < geradas)
            ++comFusao;

        // A propriedade que importa: nenhuma altura soa duas vezes ao mesmo
        // tempo. E o que garante que nenhum leitor precisa adivinhar.
        int sobreposicoes = 0;

        for (auto& lista : soando)
        {
            std::sort (lista.begin(), lista.end());

            for (std::size_t i = 1; i < lista.size(); ++i)
                if (lista[i].first < lista[i - 1].second)
                    ++sobreposicoes;
        }

        check (sobreposicoes == 0, "nenhuma altura se sobrepoe a si mesma no arquivo");

        if (semente == 42u)
            std::printf ("      %s: 1 trilha, %d notas de %d geradas, %.0f BPM\n",
                         file.getFileName().toRawUTF8(), notas, geradas, andamento);

        file.deleteFile();
    }

    std::printf ("      %d das 8 frases precisaram fundir alguma nota\n", comFusao);
    check (comFusao > 0, "a fusao acontece de verdade -- senao este caso nao prova nada");

    //--------------------------------------------------------------------------
    // SEPARADO: uma trilha por camada, e nenhuma nota se perde.
    //
    // Em trilhas separadas as camadas nao se encontram, entao a fusao deixa de
    // ser necessaria -- e o arquivo tem de sair com TODAS as notas geradas, que
    // e justamente o motivo de alguem escolher separado.
    MelodyProcessor p;
    p.regenerate (42u);

    if (auto* prm = p.apvts.getParameter (pid::routing))
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) Routing::split));

    MidiDragButton drag (p);
    const auto file = drag.writeMidiFile();

    check (file.existsAsFile(), "o .mid separado e escrito");

    if (! file.existsAsFile())
        return;

    check (file.getFileName().contains ("separado"),
           "o nome do arquivo diz que ele esta separado");

    juce::FileInputStream in (file);
    juce::MidiFile lido;

    check (lido.readFrom (in), "o .mid separado e legivel");

    const auto& frase = p.livePhrase();
    const int geradas = frase.melodyCount + frase.chordCount + frase.bassCount;

    int notas = 0;
    std::set<int> canais;

    for (int t = 0; t < lido.getNumTracks(); ++t)
        for (int e = 0; e < lido.getTrack (t)->getNumEvents(); ++e)
        {
            const auto& m = lido.getTrack (t)->getEventPointer (e)->message;

            if (m.isNoteOn())
            {
                ++notas;
                canais.insert (m.getChannel());
            }
        }

    std::printf ("      separado: %d trilhas, %d canais, %d notas de %d geradas\n",
                 lido.getNumTracks(), (int) canais.size(), notas, geradas);

    check (lido.getNumTracks() > 2, "separado tem cabecalho mais uma trilha por camada");
    check (canais.size() > 1, "e cada camada no seu canal");
    check (notas == geradas, "separado nao funde: sai o numero exato de notas geradas");

    file.deleteFile();
}

/** O .wav do arrastar tem audio de verdade, e o comprimento da frase. */
void testArquivoWav()
{
    for (const int compasso : { Length::four, Length::eight })
    {
        MelodyProcessor p;
        p.setPlayConfigDetails (0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        if (auto* prm = p.apvts.getParameter (pid::length))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) compasso));

        p.regenerate (2024u);

        WavDragButton drag (p);
        const auto file = drag.writeWavFile();

        check (file.existsAsFile(), "o arquivo .wav e escrito");

        if (! file.existsAsFile())
            return;

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatReader> lido (
            wav.createReaderFor (new juce::FileInputStream (file), true));

        check (lido != nullptr, "o .wav escrito e legivel");

        if (lido == nullptr)
            return;

        const auto& frase = p.livePhrase();
        const double esperado = frase.loopBeats() * 60.0 / frase.bpm;
        const double duracao = lido->lengthInSamples / lido->sampleRate;

        juce::AudioBuffer<float> audio ((int) lido->numChannels,
                                        (int) lido->lengthInSamples);
        lido->read (&audio, 0, (int) lido->lengthInSamples, 0, true, true);

        const float pico = audio.getMagnitude (0, 0, audio.getNumSamples());

        std::printf ("      %s: %.1f s (frase %.1f s + cauda), %d canais, pico %.2f\n",
                     file.getFileName().toRawUTF8(), duracao, esperado,
                     (int) lido->numChannels, pico);

        check (lido->numChannels == 2, "o .wav e estereo");
        check (duracao > esperado, "o .wav cobre a frase inteira mais a cauda");
        check (duracao < esperado + 3.0, "e nao sai muito maior que isso");
        check (pico > 0.01f, "o .wav tem audio, e nao silencio");
        check (pico <= 1.0f, "e nao estoura");

        file.deleteFile();
    }
}

/** PARAR O TRANSPORTE NAO PODE DEIXAR NOTA PRESA.

    Foi o defeito relatado: o acorde ficava soando para sempre depois do stop.
    A causa e a escolha de `Player` nao guardar estado -- ele so emite note-off
    quando a janela de batidas CRUZA o fim da nota, e parando no meio de um
    acorde essa travessia nunca acontece. O tocador continua sem estado; quem
    para e que passou a pedir silencio. */
void testPararNaoPrendeNota()
{
    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    FakeHost host;
    p.setPlayHead (&host);

    Hanging hanging;

    auto correr = [&] (int blocos)
    {
        for (int i = 0; i < blocos; ++i)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);
            hanging.feed (midi);

            if (host.playing)
                host.beat += 512.0 * host.bpm / (60.0 * 48000.0);
        }
    };

    // ANDA ATE HAVER NOTA NO AR, e nao um numero fixo de blocos.
    //
    // Eram 30 blocos -- meia batida -- e o caso exigia nota soando ali. Com o
    // banco antigo sempre havia uma no primeiro tempo; com 51.004 trechos ha
    // frases que comecam depois, e o caso falhava em uma execucao a cada
    // quatro. O que ele quer provar e "parar silencia o que estava soando", e
    // para isso basta chegar a um instante em que algo esteja soando.
    for (int i = 0; i < 700 && hanging.count() == 0; ++i)
        correr (1);

    check (hanging.count() > 0, "com o transporte rodando ha nota soando");

    const int antes = hanging.count();

    host.playing = false;
    correr (10);

    std::printf ("      soando ao parar: %d   presas depois do stop: %d  %s\n",
                 antes, hanging.count(), hanging.describe().toRawUTF8());

    check (hanging.count() == 0, "parar o transporte silencia tudo");

    // E o som interno tambem para -- nao adianta so o MIDI.
    double pico = 0.0;

    for (int i = 0; i < 40; ++i)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);

        for (int s = 0; s < 512; ++s)
            pico = juce::jmax (pico, (double) std::abs (buf.getSample (0, s)));
    }

    check (pico < 1.0e-4, "o som interno tambem para");

    // E o mesmo pelo botao TOCAR, que foi o caminho relatado: sem host nenhum,
    // aperta para tocar, aperta para parar.
    Hanging pelaAudicao;

    p.startAudition();

    for (int i = 0; i < 700 && pelaAudicao.count() == 0; ++i)
    {
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        p.processBlock (b, m);
        pelaAudicao.feed (m);
    }

    check (pelaAudicao.count() > 0, "o botao TOCAR poe nota no ar");

    p.stopAudition();

    double picoDepois = 0.0;

    for (int i = 0; i < 40; ++i)
    {
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        p.processBlock (b, m);
        pelaAudicao.feed (m);

        for (int t = 0; t < 512; ++t)
            picoDepois = juce::jmax (picoDepois, (double) std::abs (b.getSample (0, t)));
    }

    std::printf ("      presas depois do PARAR: %d  %s\n",
                 pelaAudicao.count(), pelaAudicao.describe().toRawUTF8());

    check (pelaAudicao.count() == 0, "o botao PARAR silencia tudo");
    check (picoDepois < 1.0e-4, "e o som interno para junto");
}

/** Um salto de transporte -- laco voltando, ou o usuario clicando na regua --
    tambem atravessa o fim das notas sem passar por ele.

    O caso NAO pode simplesmente contar nota presa depois do salto: saltar para
    a batida 64 cai no tempo zero do laco, e o acorde do primeiro tempo entra ali
    de novo, legitimamente. Contando saldo, essas notas novas seriam indistintas
    das velhas -- e o teste acusaria defeito onde ha comportamento certo. O que
    se mede e o contrato: o bloco do salto tem de CARREGAR o silencio. */
void testSaltoNaoPrendeNota()
{
    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    FakeHost host;
    p.setPlayHead (&host);

    std::set<int> abertos;      // canais com nota soando

    for (int i = 0; i < 30; ++i)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);

        for (const auto ev : midi)
        {
            const auto m = ev.getMessage();

            if (m.isNoteOn())
                abertos.insert (m.getChannel());
        }

        host.beat += 512.0 * host.bpm / (60.0 * 48000.0);
    }

    check (! abertos.empty(), "ha nota soando antes do salto");

    // O laco voltou para outro lugar.
    host.beat = 64.0;

    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    p.processBlock (buf, midi);

    std::set<int> calados;

    for (const auto ev : midi)
    {
        const auto m = ev.getMessage();

        if (m.isAllNotesOff() || m.isAllSoundOff())
            calados.insert (m.getChannel());
    }

    juce::String faltando;

    for (const int ch : abertos)
        if (calados.find (ch) == calados.end())
            faltando << "canal " << juce::String (ch) << " ";

    std::printf ("      canais soando %d, calados no bloco do salto %d  %s\n",
                 (int) abertos.size(), (int) calados.size(), faltando.toRawUTF8());

    check (faltando.isEmpty(), "o bloco do salto silencia todo canal que soava");
}

/** Desligar uma camada no meio de uma nota nao pode prender essa nota.

    O note-off estava atras da mesma condicao do note-on: desligando ACORDES com
    o acorde soando, o desligar era pulado junto. */
void testDesligarCamadaNaoPrendeNota()
{
    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    FakeHost host;
    p.setPlayHead (&host);

    Hanging hanging;

    auto correr = [&] (int blocos)
    {
        for (int i = 0; i < blocos; ++i)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);
            hanging.feed (midi);
            host.beat += 512.0 * host.bpm / (60.0 * 48000.0);
        }
    };

    correr (30);

    if (auto* prm = p.apvts.getParameter (pid::chordsOn))
        prm->setValueNotifyingHost (0.0f);

    if (auto* prm = p.apvts.getParameter (pid::melodyOn))
        prm->setValueNotifyingHost (0.0f);

    if (auto* prm = p.apvts.getParameter (pid::bassOn))
        prm->setValueNotifyingHost (0.0f);

    correr (400);

    std::printf ("      presas depois de desligar as camadas: %d  %s\n",
                 hanging.count(), hanging.describe().toRawUTF8());

    check (hanging.count() == 0, "desligar a camada nao prende a nota que ja soava");
}

/** Sem barramento de audio, o plugin ainda tem de mandar nota -- e nao pode
    tocar no buffer.

    E a versao de efeito MIDI (`melody FX`), que senta antes do instrumento na
    mesma faixa. O smoke compila como instrumento, entao o caso constroi a
    situacao na mao: buffer de ZERO canais. `getWritePointer (0)` ali e leitura
    fora do lugar, e num plugin isso e o host caindo, nao uma mensagem de erro. */
void testSemAudioAindaMandaNota()
{
    MelodyProcessor p;
    p.setPlayConfigDetails (0, 0, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    FakeHost host;
    p.setPlayHead (&host);

    Hanging hanging;
    int notas = 0;

    for (int i = 0; i < 200; ++i)
    {
        juce::AudioBuffer<float> vazio (0, 512);
        juce::MidiBuffer midi;

        p.processBlock (vazio, midi);
        hanging.feed (midi);
        notas += countNoteOns (midi);

        host.beat += 512.0 * host.bpm / (60.0 * 48000.0);
    }

    std::printf ("      %d note-on com buffer de zero canais\n", notas);

    check (notas > 0, "sem audio o plugin continua mandando nota");

    host.playing = false;

    for (int i = 0; i < 4; ++i)
    {
        juce::AudioBuffer<float> vazio (0, 512);
        juce::MidiBuffer midi;
        p.processBlock (vazio, midi);
        hanging.feed (midi);
    }

    check (hanging.count() == 0, "e parar continua silenciando");
}

/** TUDO EM 1 poe as tres camadas no canal 1; SEPARADO devolve um canal por
    camada. E a diferenca entre a frase inteira tocar no instrumento que o
    usuario colocou e so a melodia tocar. */
void testCanalDeSaida()
{
    auto canais = [] (int modo)
    {
        MelodyProcessor p;
        p.setPlayConfigDetails (0, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);

        if (auto* prm = p.apvts.getParameter (pid::routing))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) modo));

        FakeHost host;
        host.bpm = 120.0;
        p.setPlayHead (&host);

        std::set<int> vistos;

        for (int i = 0; i < 750; ++i)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);

            for (const auto ev : midi)
                if (ev.getMessage().isNoteOn())
                    vistos.insert (ev.getMessage().getChannel());

            host.beat += 512.0 * host.bpm / (60.0 * 48000.0);
        }

        return vistos;
    };

    const auto um = canais (Routing::single);
    const auto tres = canais (Routing::split);

    std::printf ("      TUDO EM 1 usa %d canal(is), SEPARADO usa %d\n",
                 (int) um.size(), (int) tres.size());

    check (um.size() == 1 && *um.begin() == 1, "TUDO EM 1 manda tudo no canal 1");
    check (tres.size() > 1, "SEPARADO usa um canal por camada");
}

/** Sem instrumento carregado, quem soa e o sintetizador interno; e um caminho
    de arquivo que nao existe falha sem levar o som junto. */
void testSlotDeInstrumento()
{
    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 512);
    p.prepareToPlay (48000.0, 512);

    check (! p.rack.loaded(), "comeca sem instrumento carregado");
    check (! p.rack.hasGuest(), "e a pergunta barata concorda com a cara");

    auto energia = [&p]
    {
        double soma = 0.0;
        p.startAudition();

        for (int i = 0; i < 120; ++i)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);

            for (int s = 0; s < 512; ++s)
                soma += std::abs (buf.getSample (0, s));
        }

        p.stopAudition();
        return soma;
    };

    const double interno = energia();
    check (interno > 1.0, "sem convidado, o sintetizador interno soa");

    // Um instrumento que nao existe: tem de recusar e deixar tudo como estava.
    juce::String erro;
    const bool ok = p.rack.load ("/tmp/nao-existe-nunca.vst3", erro);

    check (! ok, "carregar arquivo inexistente falha");
    check (erro.isNotEmpty(), "e diz por que");
    check (! p.rack.loaded(), "e nao deixa meio carregado");

    std::printf ("      %s\n", erro.toRawUTF8());

    check (energia() > 1.0, "e o som interno continua depois da falha");

    // Os plugins que a maquina tem, listados sem carregar nenhum.
    const auto achados = p.rack.installed();

    int vst3 = 0, au = 0;
    juce::String exemploAu;

    for (const auto& f : achados)
    {
        if (f.format.containsIgnoreCase ("VST3")) ++vst3;
        else { ++au; if (exemploAu.isEmpty()) exemploAu = f.identifier; }
    }

    int instrumentos = 0;

    for (const auto& f : achados)
        if (f.instrument)
            ++instrumentos;

    std::printf ("      %d plugins no slot: %d VST3, %d AU\n",
                 achados.size(), vst3, au);
    std::printf ("      identificador de AU e assim: %s\n", exemploAu.toRawUTF8());
    std::printf ("      %d sao instrumentos (%d%% da lista)\n",
                 instrumentos, 100 * instrumentos / juce::jmax (1, achados.size()));

    check (! achados.isEmpty(), "a lista de instrumentos nao esta vazia");
    check (au > 0, "os Audio Units aparecem -- eles nao sao achados por caminho");

    // O FILTRO TEM DE FILTRAR, e nao aprovar tudo. Um "e instrumento" que diz
    // sim para todo mundo passaria neste caso sem fazer nada.
    check (instrumentos > 0, "algum plugin e reconhecido como instrumento");
    check (instrumentos < achados.size(), "e algum e recusado -- o filtro filtra");

    // E o proprio melody, que e instrumento, tem de estar entre eles.
    bool achouMelody = false;

    for (const auto& f : achados)
        if (f.instrument && f.name.equalsIgnoreCase ("melody"))
            achouMelody = true;

    check (achouMelody, "o melody aparece como instrumento");
}

void testEditor()
{
    MelodyProcessor p;
    p.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    check (ed != nullptr, "a janela abre");

    if (ed == nullptr)
        return;

    check (ed->getWidth() > 0 && ed->getHeight() > 0, "a janela tem tamanho");

    auto* me = dynamic_cast<MelodyEditor*> (ed.get());

    if (me == nullptr)
        return;

    // OS BOTOES ESTAO LIGADOS EM ALGUMA COISA?
    //
    // `onClick` que ninguem atribuiu compila, desenha certo e nao faz nada.
    // Aconteceu de verdade: um recorte de layout levou junto a ligacao do GERAR
    // e do TOCAR, e a janela continuou bonita na captura.
    const int antes = p.livePhrase().melodyIndex;

    for (int i = 0; i < 10 && p.livePhrase().melodyIndex == antes; ++i)
        me->clickForTest ("gerar");

    check (p.livePhrase().melodyIndex != antes, "GERAR troca a frase");

    check (! p.auditioning(), "TOCAR comeca desligado");
    me->clickForTest ("tocar");
    check (p.auditioning(), "TOCAR liga a audicao");
    me->clickForTest ("tocar");
    check (! p.auditioning(), "e apertar de novo desliga");
}

//==============================================================================
/** --span [n] : mede quanto do campo vertical cada camada custa.

    Existe porque "o baixo come um terco da altura" era estimativa tirada de UMA
    captura, e o desenho do piano roll ia mudar por causa dela. O ambito da
    frase decide a altura da nota -- `rowH = altura / linhas` -- entao o numero
    que importa nao e opiniao: e quantos semitons a frase ocupa, quantas linhas
    nao desenham nada, e o que sobra depois de encolher os buracos. */
int measureSpan (int howMany)
{
    juce::ScopedJuceInitialiser_GUI gui;

    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, 44100.0, 512);
    p.prepareToPlay (44100.0, 512);

    juce::Random rng (20260910);

    struct Acc { long long soma = 0; int pior = 0; int melhor = 999; };
    Acc tudo, semBaixo, soBaixo, soMelodia, soAcordes, buracos, comprimido;
    int comBaixo = 0;

    auto conta = [] (Acc& a, int v)
    {
        a.soma += v;
        a.pior = juce::jmax (a.pior, v);
        a.melhor = juce::jmin (a.melhor, v);
    };

    for (int i = 0; i < howMany; ++i)
    {
        if (auto* prm = p.apvts.getParameter (pid::key))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) rng.nextInt (12)));
        if (auto* prm = p.apvts.getParameter (pid::length))
            prm->setValueNotifyingHost (prm->convertTo0to1 (
                (float) (rng.nextFloat() < 0.5f ? Length::four : Length::eight)));

        p.regenerate ((std::uint32_t) rng.nextInt());
        const auto& f = p.livePhrase();

        if (f.empty())
            continue;

        // O mesmo calculo do PianoRoll antes do encolhimento: ambito mais tres
        // semitons de folga de cada lado, e no minimo 24.
        auto janela = [] (int lo, int hi)
        {
            return juce::jmax (25, hi + 3 - (lo - 3) + 1);
        };

        int loT = 127, hiT = 0, loM = 127, hiM = 0, loB = 127, hiB = 0;
        int loMel = 127, hiMel = 0, loAc = 127, hiAc = 0;

        for (int k = 0; k < f.melodyCount; ++k)
        {
            const int q = f.melody[k].pitch;
            loT = juce::jmin (loT, q); hiT = juce::jmax (hiT, q);
            loM = juce::jmin (loM, q); hiM = juce::jmax (hiM, q);
            loMel = juce::jmin (loMel, q); hiMel = juce::jmax (hiMel, q);
        }
        for (int k = 0; k < f.chordCount; ++k)
        {
            const int q = f.chords[k].pitch;
            loT = juce::jmin (loT, q); hiT = juce::jmax (hiT, q);
            loM = juce::jmin (loM, q); hiM = juce::jmax (hiM, q);
            loAc = juce::jmin (loAc, q); hiAc = juce::jmax (hiAc, q);
        }
        for (int k = 0; k < f.bassCount; ++k)
        {
            const int q = f.bass[k].pitch;
            loT = juce::jmin (loT, q); hiT = juce::jmax (hiT, q);
            loB = juce::jmin (loB, q); hiB = juce::jmax (hiB, q);
        }

        conta (tudo, janela (loT, hiT));
        conta (semBaixo, janela (loM, hiM));

        if (f.melodyCount > 0) conta (soMelodia, hiMel - loMel + 1);
        if (f.chordCount > 0)  conta (soAcordes, hiAc - loAc + 1);

        if (f.bassCount > 0)
        {
            ++comBaixo;
            conta (soBaixo, hiB - loB + 1);
        }

        bool usada[128] = {};
        for (int k = 0; k < f.melodyCount; ++k) usada[f.melody[k].pitch] = true;
        for (int k = 0; k < f.chordCount; ++k)  usada[f.chords[k].pitch] = true;
        for (int k = 0; k < f.bassCount; ++k)   usada[f.bass[k].pitch]   = true;

        int vazias = 0;
        for (int pitch = loT; pitch <= hiT; ++pitch)
            if (! usada[pitch]) ++vazias;
        conta (buracos, vazias);

        // O MODELO QUE O PianoRoll USA DE VERDADE: corrida de 3+ vazias vira
        // uma COSTURA de 0,45 linha. Acompanha `fatiaCostura` la; se um mudar
        // sem o outro, o numero aqui passa a mentir.
        float linhasReais = 0.0f, emendas = 0.0f, corrida = 0.0f;
        for (int pitch = loT - 3; pitch <= hiT + 3; ++pitch)
        {
            if (pitch >= 0 && pitch < 128 && usada[pitch])
            {
                if (corrida >= 3.0f) emendas += 1.0f;
                else                 linhasReais += corrida;
                corrida = 0.0f;
                linhasReais += 1.0f;
            }
            else corrida += 1.0f;
        }
        if (corrida >= 3.0f) emendas += 1.0f; else linhasReais += corrida;

        conta (comprimido, juce::roundToInt (
            juce::jmax (20.0f, linhasReais + 0.45f * emendas)));
    }

    // O MIXADO AINDA ACHA HARMONIA?
    //
    // A recombinacao tenta ate vinte candidatos e, se nenhum passa do limiar,
    // fica com a harmonia que veio junto com a melodia -- o MIXADO vira UNICO
    // em silencio. Com banco menor isso passa a acontecer mais, e a unica forma
    // de saber se o modo continua sendo um modo e medir.
    int recombinou = 0, tentativas = 0;
    double somaFit = 0.0;

    if (auto* prm = p.apvts.getParameter (pid::source))
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) Source::recombined));

    for (int i = 0; i < howMany; ++i)
    {
        p.regenerate ((std::uint32_t) rng.nextInt());
        const auto& f = p.livePhrase();

        if (f.empty())
            continue;

        ++tentativas;

        if (f.harmonyIndex != f.melodyIndex)
        {
            ++recombinou;
            somaFit += f.fit;
        }
    }

    const double n = (double) juce::jmax (1, howMany);
    const double alturaRoll = 460.0;   // o roll numa janela de 1000x600

    std::printf ("\n  %d frases\n\n", howMany);

    std::printf ("  MIXADO: recombinou em %d de %d (%.1f%%), encaixe medio %.0f%%\n",
                 recombinou, tentativas,
                 tentativas > 0 ? 100.0 * recombinou / tentativas : 0.0,
                 recombinou > 0 ? 100.0 * somaFit / recombinou : 0.0);
    std::printf ("          o resto cai na harmonia que veio com a melodia\n\n");

    std::printf ("  janela vertical (semitons, com folga e minimo de 24)\n");
    std::printf ("    com baixo      media %5.1f   pior %3d\n", tudo.soma / n, tudo.pior);
    std::printf ("    sem baixo      media %5.1f   pior %3d\n", semBaixo.soma / n, semBaixo.pior);

    std::printf ("\n  ambito PROPRIO de cada camada (semitons)\n");
    std::printf ("    melodia        media %5.1f   pior %3d\n", soMelodia.soma / n, soMelodia.pior);
    std::printf ("    acordes        media %5.1f   pior %3d\n", soAcordes.soma / n, soAcordes.pior);
    std::printf ("    baixo          media %5.1f   pior %3d\n",
                 comBaixo > 0 ? soBaixo.soma / (double) comBaixo : 0.0, soBaixo.pior);
    std::printf ("    linhas VAZIAS  media %5.1f   pior %3d   <- altura sem desenhar nada\n",
                 buracos.soma / n, buracos.pior);

    std::printf ("\n  O QUE O PLUGIN FAZ HOJE (costura de 0,45 linha, limiar 3)\n");
    std::printf ("    janela         media %5.1f   pior %3d   (era %.1f)\n",
                 comprimido.soma / n, comprimido.pior, tudo.soma / n);
    std::printf ("    altura da nota  %4.1f px   (era %.1f)  -> %+.0f%%\n\n",
                 alturaRoll / (comprimido.soma / n), alturaRoll / (tudo.soma / n),
                 100.0 * ((tudo.soma / n) / (comprimido.soma / n) - 1.0));

    return 0;
}

//==============================================================================
/** --render arquivo.wav [n] : escreve n frases tocadas pelo som interno.

    Existe pelo mesmo motivo do --shot. Conferir melodia por descricao nao
    funciona -- "trecho 52, encaixe 95%" nao diz se a frase e bonita -- e abrir
    uma DAW para ouvir quatro compassos e um ciclo de meio minuto por tentativa.
    Este modo passa pelo caminho de verdade: gerador, tocador e sintetizador, os
    mesmos que o plugin usa. */
int renderAudio (const juce::String& path, int howMany, std::uint32_t seedBase,
                 bool longPhrases)
{
    juce::ScopedJuceInitialiser_GUI gui;

    constexpr double sr = 44100.0;
    constexpr int block = 512;

    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, sr, block);
    p.prepareToPlay (sr, block);

    juce::Random rng ((int) seedBase);

    // O --render segue o parametro de comprimento, para dar para ouvir os oito
    // compassos sem abrir a DAW.
    if (auto* prm = p.apvts.getParameter (pid::length))
        if (longPhrases)
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) Length::eight));

    juce::AudioBuffer<float> out (2, 0);
    int written = 0;

    auto append = [&out, &written] (const juce::AudioBuffer<float>& src, int n)
    {
        if (written + n > out.getNumSamples())
            out.setSize (2, juce::jmax (written + n, out.getNumSamples() * 2 + n), true, true, true);

        for (int c = 0; c < 2; ++c)
            out.copyFrom (c, written, src, c, 0, n);

        written += n;
    };

    for (int i = 0; i < howMany; ++i)
    {
        const int key = rng.nextInt (12);
        const bool minor = rng.nextFloat() < 0.72f;

        if (auto* prm = p.apvts.getParameter (pid::key))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) key));

        if (auto* prm = p.apvts.getParameter (pid::scale))
            prm->setValueNotifyingHost (prm->convertTo0to1 (minor ? 1.0f : 0.0f));

        p.regenerate (seedBase + (std::uint32_t) i * 2654435761u);

        const auto& frase = p.livePhrase();
        const double bpm = frase.bpm > 0 ? frase.bpm : 140.0;

        // Duas voltas, mais uma cauda para a ultima nota nao ser cortada.
        const double seconds = (frase.bars >= melody::longBars ? 1.0 : 2.0)
                                 * frase.loopBeats() * 60.0 / bpm;
        const int blocks = (int) (seconds * sr / block);

        std::printf ("  %2d/%d  %s%s  %.0f BPM  trecho %d  encaixe %d%%\n",
                     i + 1, howMany, keyName (frase.root), minor ? "m" : "",
                     bpm, frase.melodyIndex + 1,
                     juce::roundToInt (frase.fit * 100.0f));

        p.startAudition();

        for (int b = 0; b < blocks; ++b)
        {
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);
            append (buf, block);
        }

        p.stopAudition();

        for (int b = 0; b < 26; ++b)      // cauda e uma pausa curta entre as frases
        {
            juce::AudioBuffer<float> buf (2, block);
            juce::MidiBuffer midi;
            p.processBlock (buf, midi);
            append (buf, block);
        }
    }

    out.setSize (2, written, true, true, true);

    juce::File file (path);
    file.deleteFile();

    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());

    if (stream == nullptr)
    {
        std::printf ("nao consegui escrever em %s\n", path.toRawUTF8());
        return 1;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.release(), sr, 2, 16, {}, 0));

    if (writer == nullptr)
    {
        std::printf ("nao consegui criar o escritor de wav\n");
        return 1;
    }

    writer->writeFromAudioSampleBuffer (out, 0, written);
    writer.reset();

    std::printf ("\n%.1f s em %s\n", written / sr, path.toRawUTF8());
    return 0;
}

//==============================================================================
/** --load : carrega os plugins JA INSTALADOS num host de verdade.

    Existe por causa de um "This VST3 plug-in could not be opened" no Ableton.
    Erro de host nao diz de quem e a culpa, e as duas respostas possiveis tem
    consertos opostos: se o bundle nao instancia aqui, o defeito e meu; se
    instancia, o host esta recusando um plugin valido e o conserto e de roteamento,
    nao de codigo. Adivinhar isso custaria uma tarde de mudanca no lugar errado. */
int loadInstalled()
{
    juce::ScopedJuceInitialiser_GUI gui;

    // `addDefaultFormats` esta deletado no juce_audio_processors_headless do
    // JUCE 9: os formatos entram um a um, e sao exatamente os dois que
    // interessam aqui.
    juce::AudioPluginFormatManager formats;
    formats.addFormat (new juce::VST3PluginFormat());

   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats.addFormat (new juce::AudioUnitPluginFormat());
   #endif

    const juce::File pastas[] =
    {
        juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library/Audio/Plug-Ins/VST3"),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Library/Audio/Plug-Ins/Components")
    };

    int achados = 0;

    for (const auto& pasta : pastas)
        for (const auto& nome : { "melody", "melody FX" })
        {
            for (int f = 0; f < formats.getNumFormats(); ++f)
            {
                auto* format = formats.getFormat (f);

                const auto arquivo = pasta.getChildFile (
                    juce::String (nome) + (pasta.getFileName() == "VST3" ? ".vst3"
                                                                        : ".component"));

                if (! arquivo.exists() || ! format->fileMightContainThisPluginType (
                                              arquivo.getFullPathName()))
                    continue;

                juce::OwnedArray<juce::PluginDescription> achadas;
                format->findAllTypesForFile (achadas, arquivo.getFullPathName());

                for (const auto* desc : achadas)
                {
                    ++achados;

                    juce::String erro;
                    std::unique_ptr<juce::AudioPluginInstance> inst (
                        formats.createPluginInstance (*desc, 48000.0, 512, erro));

                    std::printf ("  %-10s %-10s %s\n",
                                 format->getName().toRawUTF8(),
                                 desc->name.toRawUTF8(),
                                 inst != nullptr ? "instancia" : "FALHOU");

                    if (inst == nullptr)
                    {
                        std::printf ("      %s\n", erro.toRawUTF8());
                        ++failures;
                        continue;
                    }

                    inst->prepareToPlay (48000.0, 512);

                    std::printf ("      categoria \"%s\"   entradas %d, saidas %d   "
                                 "midi in %s, midi out %s, e efeito midi %s\n",
                                 desc->category.toRawUTF8(),
                                 inst->getTotalNumInputChannels(),
                                 inst->getTotalNumOutputChannels(),
                                 inst->acceptsMidi() ? "sim" : "nao",
                                 inst->producesMidi() ? "sim" : "nao",
                                 inst->isMidiEffect() ? "sim" : "nao");

                    // E ele produz nota? Um plugin que carrega e nao gera seria
                    // um defeito diferente, e este e o momento de ver.
                    int notas = 0;

                    for (int b = 0; b < 60; ++b)
                    {
                        juce::AudioBuffer<float> buf (
                            juce::jmax (1, inst->getTotalNumOutputChannels()), 512);
                        buf.clear();
                        juce::MidiBuffer midi;
                        inst->processBlock (buf, midi);
                        notas += countNoteOns (midi);
                    }

                    std::printf ("      %d note-on em 60 blocos parado\n", notas);
                }
            }
        }

    if (achados == 0)
    {
        std::printf ("  nenhum plugin instalado encontrado\n");
        return 1;
    }

    std::printf ("\n%s\n", failures == 0 ? "os bundles instanciam." : "VERMELHO.");
    return failures == 0 ? 0 : 1;
}

//==============================================================================
/** --rack <caminho> : carrega um instrumento de verdade no slot e prova que a
    nota gerada aqui vira som la dentro.

    Nao da para deixar isto na suite: qual plugin existe muda de maquina, e um
    teste que passa ou falha conforme o que esta instalado nao prova nada. Mas a
    conta de "a frase chega ao instrumento" so fecha com um instrumento real. */
int rackCheck (const juce::String& caminho)
{
    juce::ScopedJuceInitialiser_GUI gui;

    constexpr double sr = 48000.0;
    constexpr int block = 512;

    MelodyProcessor p;
    p.setPlayConfigDetails (0, 2, sr, block);
    p.prepareToPlay (sr, block);

    juce::String erro;

    if (! p.rack.load (caminho, erro))
    {
        std::printf ("nao carregou: %s\n", erro.toRawUTF8());
        return 1;
    }

    std::printf ("carregado: %s\n", p.rack.name().toRawUTF8());

    FakeHost host;
    p.setPlayHead (&host);

    double energia = 0.0;
    double pico = 0.0;
    int notas = 0;

    for (int i = 0; i < 400; ++i)
    {
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;

        p.processBlock (buf, midi);
        notas += countNoteOns (midi);

        for (int s = 0; s < block; ++s)
        {
            const double x = std::abs (buf.getSample (0, s));
            energia += x;
            pico = juce::jmax (pico, x);
        }

        host.beat += block * host.bpm / (60.0 * sr);
    }

    std::printf ("%d notas geradas, energia %.1f, pico %.3f\n", notas, energia, pico);

    if (pico <= 1.0e-5)
    {
        std::printf ("\nO INSTRUMENTO NAO SOOU. A nota saiu daqui e nao virou audio la.\n");
        return 1;
    }

    // E o editor dele encaixa na janela? Nao da para conferir por foto: editor
    // de plugin hospedado e uma NSView nativa, e captura por software desenha
    // preto no lugar dela. O que da para medir e a geometria -- que a janela
    // cresceu, que o editor virou filho e que ele tem tamanho.
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());

    if (auto* me = dynamic_cast<MelodyEditor*> (ed.get()))
    {
        const int antesW = me->getWidth(), antesH = me->getHeight();

        me->openGuestForShot();

        const auto* dentro = me->guestEditorForTest();

        std::printf ("\njanela %d x %d  ->  %d x %d\n",
                     antesW, antesH, me->getWidth(), me->getHeight());

        if (dentro == nullptr)
        {
            std::printf ("O EDITOR DO CONVIDADO NAO ENTROU NA JANELA.\n");
            return 1;
        }

        std::printf ("editor do convidado: %d x %d, dentro do melody: %s\n",
                     dentro->getWidth(), dentro->getHeight(),
                     me->isParentOf (dentro) ? "sim" : "NAO");

        // A JANELA E SO DO INSTRUMENTO. Se os controles do melody continuassem
        // visiveis, a janela teria de caber os dois -- foi a versao anterior, e
        // dava rolagem horizontal por causa de controle que ninguem estava
        // olhando.
        const int sobra = me->getWidth() - dentro->getWidth();

        std::printf ("sobra de largura em volta do instrumento: %d px\n", sobra);

        if (sobra > 120)
        {
            std::printf ("A JANELA ESTA MAIOR QUE O INSTRUMENTO: sobrou coisa visivel.\n");
            return 1;
        }

        if (dentro->getWidth() <= 0 || dentro->getHeight() <= 0)
        {
            std::printf ("O EDITOR DO CONVIDADO ESTA COM TAMANHO ZERO.\n");
            return 1;
        }

        if (! me->isParentOf (dentro))
        {
            std::printf ("O EDITOR DO CONVIDADO NAO E FILHO DA JANELA.\n");
            return 1;
        }
    }

    std::printf ("\no instrumento carregado soou, e o editor dele encaixou.\n");
    return 0;
}

//==============================================================================
/** --slots [filtro] : lista o que o slot de instrumento enxerga. */
int listSlots (const juce::String& filtro)
{
    juce::ScopedJuceInitialiser_GUI gui;

    MelodyProcessor p;

    for (const auto& f : p.rack.installed())
        if (filtro.isEmpty() || f.name.containsIgnoreCase (filtro)
              || f.identifier.containsIgnoreCase (filtro))
            std::printf ("  %-5s %-28s %s\n", f.format.toRawUTF8(),
                         f.name.toRawUTF8(), f.identifier.toRawUTF8());

    return 0;
}

//==============================================================================
/** --midi <arquivo> [semente] : escreve o .mid do arrastar e guarda.

    O caso da suite confere o arquivo com o leitor do proprio JUCE, que e o
    mesmo que escreveu -- se ele tivesse um vies, os dois teriam o mesmo. Este
    modo deixa o arquivo no disco para ser aberto por outro leitor. */
int writeMidi (const juce::String& path, std::uint32_t semente)
{
    juce::ScopedJuceInitialiser_GUI gui;

    MelodyProcessor p;
    p.regenerate (semente);

    MidiDragButton drag (p);
    const auto tmp = drag.writeMidiFile();

    if (! tmp.existsAsFile())
    {
        std::printf ("nao escreveu\n");
        return 1;
    }

    juce::File destino (path);
    destino.deleteFile();
    tmp.copyFileTo (destino);
    tmp.deleteFile();

    const auto& frase = p.livePhrase();

    std::printf ("%s  %d notas geradas (%d melodia, %d harmonia, %d baixo), %d BPM\n",
                 destino.getFullPathName().toRawUTF8(),
                 frase.melodyCount + frase.chordCount + frase.bassCount,
                 frase.melodyCount, frase.chordCount, frase.bassCount, frase.bpm);
    return 0;
}

//==============================================================================
/** --menu arquivo.png : desenha o menu pelo LookAndFeel, sem abrir menu nenhum.

    `PopupMenu` cria uma janela propria, e `createComponentSnapshot` da janela do
    plugin nao a alcanca. Chamar os metodos de desenho direto confere o unico
    pedaco que e meu -- fundo, item, visto, seta, cabecalho -- que e onde um
    engano apareceria. */
int drawMenuSample (const juce::String& path)
{
    juce::ScopedJuceInitialiser_GUI gui;
    ui::MenuLook look;

    struct Item { const char* texto; bool visto; bool sub; bool cabecalho; bool sep; };

    const Item itens[] =
    {
        { "SEM INSTRUMENTO", false, false, true,  false },
        { "Som interno",     true,  false, false, false },
        { "Silencio (so MIDI)", false, false, false, false },
        { "",                false, false, false, true  },
        { "INSTRUMENTOS",    false, false, true,  false },
        { "VST3",            false, true,  false, false },
        { "Audio Unit",      false, true,  false, false },
        { "",                false, false, false, true  },
        { "Escolher arquivo...", false, false, false, false },
    };

    constexpr int w = 260;
    int h = 12;

    for (const auto& i : itens)
    {
        int iw = 0, ih = 0;
        look.getIdealPopupMenuItemSize (i.texto, i.sep, 26, iw, ih);
        h += i.cabecalho ? 22 : ih;
    }

    h += 12;

    juce::Image img (juce::Image::ARGB, w, h, true);
    juce::Graphics g (img);

    look.drawPopupMenuBackground (g, w, h);

    int y = 12;

    for (const auto& i : itens)
    {
        int iw = 0, ih = 0;
        look.getIdealPopupMenuItemSize (i.texto, i.sep, 26, iw, ih);

        const int alt = i.cabecalho ? 22 : ih;
        const juce::Rectangle<int> area (4, y, w - 8, alt);

        if (i.cabecalho)
            look.drawPopupMenuSectionHeader (g, area, i.texto);
        else
            look.drawPopupMenuItem (g, area, i.sep, true, false, i.visto, i.sub,
                                    i.texto, {}, nullptr, nullptr);

        y += alt;
    }

    // O item destacado, desenhado de novo por cima para aparecer na foto.
    {
        int iw = 0, ih = 0;
        look.getIdealPopupMenuItemSize ("Audio Unit", false, 26, iw, ih);
        look.drawPopupMenuItem (g, { 4, 12 + 22 + 26 + 26 + 11 + 22 + 26, w - 8, ih },
                                false, true, true, false, true, "Audio Unit",
                                {}, nullptr, nullptr);
    }

    juce::File out (path);
    out.deleteFile();

    if (auto stream = out.createOutputStream())
    {
        juce::PNGImageFormat png;
        png.writeImageToStream (img, *stream);
    }

    std::printf ("menu em %s (%d x %d)\n", path.toRawUTF8(), w, h);
    return 0;
}

//==============================================================================
int takeShot (const juce::String& path, const juce::String& guest)
{
    juce::ScopedJuceInitialiser_GUI gui;

    MelodyProcessor p;
    p.prepareToPlay (48000.0, 512);

    // "8" sozinho, ou como quarto campo do anim=. Eram modos exclusivos, e por
    // isso nao dava para ver oito compassos COM a animacao pronta -- a captura
    // saia sempre no quadro zero, com um compasso desenhado.
    const auto tokensAnim = guest.startsWith ("anim=")
        ? juce::StringArray::fromTokens (guest.substring (5), ",", "")
        : juce::StringArray();

    if (guest == "8" || (tokensAnim.size() > 3 && tokensAnim[3].trim() == "8"))
        if (auto* prm = p.apvts.getParameter (pid::length))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) Length::eight));

    // Com um instrumento carregado, a captura mostra o editor DELE embutido --
    // que e a unica forma de conferir que o encaixe coube e nao ficou cortado.
    if (guest.isNotEmpty() && guest != "8" && ! guest.startsWith ("anim="))
    {
        juce::String erro;

        if (! p.rack.load (guest, erro))
            std::printf ("aviso: nao carreguei o convidado (%s)\n", erro.toRawUTF8());
    }

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());

    if (ed == nullptr)
    {
        std::printf ("nao consegui criar a janela\n");
        return 1;
    }

    if (auto* me = dynamic_cast<MelodyEditor*> (ed.get()))
    {
        // A semente vem ANTES do demoPhrase, porque e ela que decide o que sera
        // gerado. "anim=<0..1>[,<batida>[,<semente>[,8]]]".
        me->demoPhrase (tokensAnim.size() > 2
                          ? (std::uint32_t) tokensAnim[2].getLargeIntValue()
                          : 20250908u);

        if (guest.startsWith ("anim="))
        {
            const float t = tokensAnim[0].getFloatValue();
            const double h = tokensAnim.size() > 1 ? tokensAnim[1].getDoubleValue() : -1.0;

            me->poseForShot (t, h < 0.0 ? 0.0 : h, h >= 0.0, -1);
        }

        if (guest.isNotEmpty() && guest != "8" && ! guest.startsWith ("anim="))
        {
            me->openGuestForShot();

            // O CONVIDADO PRECISA DO LACO DE MENSAGENS. Editor de plugin
            // costuma se popular em callAsync ou no primeiro tique de timer, e
            // num binario de console sem laco nada disso roda -- a foto sai
            // preta e parece que o embutir falhou.
            juce::MessageManager::getInstance()->runDispatchLoopUntil (600);
        }
    }

    const auto image = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);

    juce::File out (path);
    out.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());

    if (stream == nullptr)
    {
        std::printf ("nao consegui escrever em %s\n", path.toRawUTF8());
        return 1;
    }

    juce::PNGImageFormat png;
    png.writeImageToStream (image, *stream);

    std::printf ("captura em %s (%d x %d)\n", path.toRawUTF8(),
                 image.getWidth(), image.getHeight());
    return 0;
}

} // namespace

//==============================================================================
int main (int argc, char* argv[])
{
    if (argc >= 2 && juce::String (argv[1]) == "--slots")
        return listSlots (argc >= 3 ? juce::String (argv[2]) : juce::String());

    if (argc >= 3 && juce::String (argv[1]) == "--midi")
        return writeMidi (argv[2],
                          argc >= 4 ? (std::uint32_t) juce::String (argv[3]).getLargeIntValue()
                                    : 42u);

    if (argc >= 3 && juce::String (argv[1]) == "--rack")
        return rackCheck (argv[2]);

    if (argc >= 2 && juce::String (argv[1]) == "--load")
        return loadInstalled();

    if (argc >= 3 && juce::String (argv[1]) == "--menu")
        return drawMenuSample (argv[2]);

    if (argc >= 3 && juce::String (argv[1]) == "--shot")
        return takeShot (argv[2], argc >= 4 ? juce::String (argv[3]) : juce::String());

    if (argc >= 2 && juce::String (argv[1]) == "--span")
        return measureSpan (argc >= 3 ? juce::String (argv[2]).getIntValue() : 1000);

    if (argc >= 3 && juce::String (argv[1]) == "--render")
        return renderAudio (argv[2],
                            argc >= 4 ? juce::String (argv[3]).getIntValue() : 8,
                            argc >= 5 ? (std::uint32_t) juce::String (argv[4]).getLargeIntValue()
                                      : 20250908u,
                            argc >= 6 && juce::String (argv[5]) == "8");

    juce::ScopedJuceInitialiser_GUI gui;

    std::printf ("melody_smoke\n\n");

    std::printf ("  o banco embutido\n");          testGeraNaConstrucao();
    std::printf ("  transporte emite nota\n");     testTransporteEmiteNota();
    std::printf ("  abre sorteado\n");             testAbreSorteado();
    std::printf ("  o botao de tocar nao mente\n"); testBotaoTocarNaoMente();
    std::printf ("  oito compassos\n");            testOitoCompassos();
    std::printf ("  parado nao emite\n");          testParadoNaoEmite();
    std::printf ("  som interno desliga\n");       testSomInternoDesliga();
    std::printf ("  determinismo\n");              testDeterminismo();
    std::printf ("  estado leva a semente\n");     testEstadoLevaASemente();
    std::printf ("  o .mid do arrastar\n");        testArquivoMidi();
    std::printf ("  o .wav do arrastar\n");       testArquivoWav();
    std::printf ("  parar nao prende nota\n");     testPararNaoPrendeNota();
    std::printf ("  saltar nao prende nota\n");    testSaltoNaoPrendeNota();
    std::printf ("  desligar camada nao prende\n"); testDesligarCamadaNaoPrendeNota();
    std::printf ("  sem audio ainda manda nota\n"); testSemAudioAindaMandaNota();
    std::printf ("  canal de saida\n");             testCanalDeSaida();
    std::printf ("  slot de instrumento\n");        testSlotDeInstrumento();
    std::printf ("  janela\n");                    testEditor();

    std::printf ("\n%s\n", failures == 0 ? "verde." : "VERMELHO.");
    return failures == 0 ? 0 : 1;
}
