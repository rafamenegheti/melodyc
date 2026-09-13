/*
  ==============================================================================

    Editor.cpp

  ==============================================================================
*/

#include "plugin/Editor.h"

#include <algorithm>
#include <vector>

using namespace ui;

namespace
{

constexpr int leftColumn = 320;
constexpr int margin = 20;
constexpr int gap = 24;
constexpr int headerH = 40;
constexpr int guestBarH = 34;

// O TAMANHO DE FABRICA, E OS LIMITES.
//
// 1000x600 era tamanho FIXO. O roll e o miolo do plugin e ele estava preso numa
// caixa de 460 pixels de altura: a nota, que hoje tem 17, iria a 31 numa janela
// de 850. Esticar nao custa layout nenhum -- o `resized()` ja tira as barras de
// cima e de baixo com altura fixa e da o resto ao roll --, faltava so destravar.
//
// O MINIMO EM LARGURA E O PROPRIO TAMANHO DE FABRICA, e isso nao e preguica:
// a barra de cima soma 954 pixels de controle mais 40 de margem. Em 900 o
// SOM INTERNO encostava no MENOR -- visto na captura, nao deduzido. A janela so
// cresce, que e o que interessava. O maximo existe para ela nao virar tela
// cheia por acidente num arrasto.
constexpr int windowW = 1000;
constexpr int windowH = 600;
constexpr int minW = windowW;
constexpr int minH = 500;
constexpr int maxW = 2400;
constexpr int maxH = 1600;
constexpr int topBar = 54;
constexpr int bottomBar = 62;

/** Lê o parâmetro como índice de escolha, sem depender do tipo concreto. */
int choiceOf (juce::AudioProcessorValueTreeState& s, const char* id)
{
    return (int) *s.getRawParameterValue (id);
}

void setChoice (juce::AudioProcessorValueTreeState& s, const char* id, int value)
{
    if (auto* p = s.getParameter (id))
    {
        const auto norm = p->convertTo0to1 ((float) value);

        p->beginChangeGesture();
        p->setValueNotifyingHost (norm);
        p->endChangeGesture();
    }
}

void setFloat (juce::AudioProcessorValueTreeState& s, const char* id, float value)
{
    if (auto* p = s.getParameter (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (value));
        p->endChangeGesture();
    }
}

} // namespace

//==============================================================================
juce::File MidiDragButton::writeMidiFile() const
{
    const auto& p = proc.livePhrase();

    // UMA TRILHA SO, e por isso as tres camadas viram uma lista.
    //
    // A versao anterior escrevia melodia, acordes e baixo em trilhas separadas,
    // e arrastar isso para a DAW criava TRES faixas -- que e o oposto de
    // arrastar para o instrumento que ja esta ali.
    std::vector<melody::Voice> notas;
    notas.reserve ((std::size_t) (p.melodyCount + p.chordCount + p.bassCount));

    notas.insert (notas.end(), p.melody, p.melody + p.melodyCount);
    notas.insert (notas.end(), p.chords, p.chords + p.chordCount);
    notas.insert (notas.end(), p.bass,   p.bass   + p.bassCount);

    std::sort (notas.begin(), notas.end(), [] (const melody::Voice& a, const melody::Voice& b)
    {
        return a.pitch != b.pitch ? a.pitch < b.pitch : a.pos < b.pos;
    });

    // DUAS CAMADAS NA MESMA ALTURA VIRAM UMA NOTA.
    //
    // Medido no banco: 37% das frases tem pelo menos um par de alturas iguais
    // sobrepostas entre camadas. Numa trilha so isso e um note-on sem par --
    // o primeiro note-off encerra os dois, e a nota mais longa e cortada no
    // meio sem nada no piano roll explicando. Fundir e a unica saida que nao
    // depende de como cada DAW resolve a ambiguidade.
    std::vector<melody::Voice> fundidas;
    fundidas.reserve (notas.size());

    for (const auto& n : notas)
    {
        if (! fundidas.empty())
        {
            auto& atual = fundidas.back();

            // Encostar nao e sobrepor: nota que comeca exatamente onde a outra
            // acaba e repique, e repique tem de continuar sendo duas notas.
            if (atual.pitch == n.pitch && n.pos < atual.pos + atual.dur)
            {
                const int fim = juce::jmax (atual.pos + atual.dur, n.pos + n.dur);
                atual.dur = fim - atual.pos;
                atual.vel = juce::jmax (atual.vel, n.vel);
                continue;
            }
        }

        fundidas.push_back (n);
    }

    //--------------------------------------------------------------------------
    juce::MidiMessageSequence trilha;

    // O andamento e a formula vao na MESMA trilha: em arquivo de tipo 0 nao ha
    // outra. Sem eles o arquivo abre a 120 e a frase soa no andamento errado --
    // a primeira coisa que o usuario notaria e a ultima de que suspeitaria.
    trilha.addEvent (juce::MidiMessage::tempoMetaEvent (
                         (int) (60000000.0 / juce::jmax (40.0, proc.tempo()))), 0.0);
    trilha.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
    trilha.addEvent (juce::MidiMessage::textMetaEvent (3, "melody"), 0.0);

    for (const auto& n : fundidas)
    {
        // O tique e por semicolcheia: 960 por seminima e a resolucao que toda
        // DAW abre sem reamostrar.
        const double on = n.pos * 240.0;

        // UM TIQUE ANTES DO FIM. Se um note-off cai exatamente no tique de um
        // note-on da mesma altura, quem le decide a ordem -- e metade dos
        // leitores decide errado, comendo a nota repicada. Um tique em 960 nao
        // se ouve; a nota comida, sim.
        const double off = juce::jmax (on + 1.0, on + n.dur * 240.0 - 1.0);

        trilha.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.vel), on);
        trilha.addEvent (juce::MidiMessage::noteOff (1, n.pitch), off);
    }

    trilha.updateMatchedPairs();

    juce::MidiFile file;
    file.setTicksPerQuarterNote (960);

    //--------------------------------------------------------------------------
    // SEPARADO: uma trilha por camada, e a fusao deixa de ser necessaria.
    //
    // Fundir existe porque duas camadas na mesma altura, no mesmo canal, viram
    // um note-on sem par. Em trilhas separadas elas nao se encontram, entao o
    // arquivo sai com as notas como elas sao -- que e o motivo de alguem
    // escolher separado.
    const bool separado =
        (int) *proc.apvts.getRawParameterValue (pid::routing) == Routing::split;

    if (separado)
    {
        juce::MidiMessageSequence cabecalho;
        cabecalho.addEvent (juce::MidiMessage::tempoMetaEvent (
                                (int) (60000000.0 / juce::jmax (40.0, proc.tempo()))), 0.0);
        cabecalho.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0.0);
        file.addTrack (cabecalho);

        static const char* nomes[] = { "melodia", "acordes", "baixo" };
        const melody::Voice* camadas[] = { p.melody, p.chords, p.bass };
        const int contagens[] = { p.melodyCount, p.chordCount, p.bassCount };

        for (int c = 0; c < 3; ++c)
        {
            if (contagens[c] == 0)
                continue;

            juce::MidiMessageSequence t;
            t.addEvent (juce::MidiMessage::textMetaEvent (3, nomes[c]), 0.0);

            for (int i = 0; i < contagens[c]; ++i)
            {
                const auto& n = camadas[c][i];
                const double on = n.pos * 240.0;
                const double off = juce::jmax (on + 1.0, on + n.dur * 240.0 - 1.0);

                t.addEvent (juce::MidiMessage::noteOn (c + 1, n.pitch,
                                                       (juce::uint8) n.vel), on);
                t.addEvent (juce::MidiMessage::noteOff (c + 1, n.pitch), off);
            }

            t.updateMatchedPairs();
            file.addTrack (t);
        }
    }
    else
    {
        file.addTrack (trilha);
    }

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("melody");
    dir.createDirectory();

    const auto name = juce::String ("melody_") + keyName (p.root)
                        + (p.minor ? "m" : "") + "_" + juce::String (p.bpm) + "bpm"
                        + (separado ? "_separado" : "") + ".mid";

    auto out = dir.getChildFile (name);
    out.deleteFile();

    if (auto stream = out.createOutputStream())
    {
        // Tipo 0 quando e uma trilha so: e o formato que toda DAW abre como UM
        // clipe -- tipo 1 com uma trilha tambem funcionaria, mas ha host que
        // insiste em criar uma faixa por trilha declarada. Separado precisa do
        // tipo 1, que e o que tem mais de uma trilha.
        file.writeTo (*stream, separado ? 1 : 0);
        stream->flush();
    }

    return out;
}

//==============================================================================
/** ENTREGA O ARQUIVO AO SISTEMA, UMA VEZ POR GESTO, E ANOTA O QUE ACONTECEU.

    `mouseDrag` chega a cada pixel que o mouse anda. Sem trava, cada chegada
    APAGAVA e reescrevia o arquivo e pedia outro arrasto -- enquanto a DAW podia
    estar lendo exatamente aquele caminho. No WAV era pior: cada pixel
    renderizava a frase inteira de novo. Foi relatado como "nao consigo arrastar
    no FL Studio".

    O LIMIAR DE 6 PIXELS separa clique de arrasto. Sem ele, um clique com a mao
    tremendo virava arrasto -- e o arquivo era escrito para nada.

    O REGISTRO EXISTE PORQUE O ARRASTO NAO SE TESTA SEM A DAW. Nao ha como
    simular uma sessao de arrasto do macOS num caso de teste, e "nao funciona" nao
    diz se o arquivo nao foi escrito, se o sistema recusou comecar o arrasto, ou
    se a DAW recusou soltar. Cada tentativa vira uma linha em
    ~/Library/Logs/melodyc/arrastar.log com essas tres respostas. */
static void iniciaArrasto (juce::Component& origem, const juce::File& arquivo,
                           const char* tipo)
{
    const bool escrito = arquivo.existsAsFile() && arquivo.getSize() > 0;

    bool comecou = false;

    if (escrito)
        comecou = juce::DragAndDropContainer::performExternalDragDropOfFiles (
            { arquivo.getFullPathName() }, false, &origem);

   #if JUCE_MAC
    const auto log = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                       .getChildFile ("Library/Logs/melodyc/arrastar.log");
   #else
    const auto log = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("melodyc/arrastar.log");
   #endif
    log.getParentDirectory().createDirectory();

    juce::String linha;
    linha << juce::Time::getCurrentTime().toString (true, true, true, true)
          << "  " << tipo
          << "  host=" << juce::PluginHostType().getHostDescription()
          << "  escrito=" << (escrito ? "sim" : "NAO")
          << "  bytes=" << juce::String (arquivo.getSize())
          << "  sistema_aceitou_arrasto=" << (comecou ? "sim" : "NAO")
          << "  " << arquivo.getFullPathName() << "\n";

    log.appendText (linha);
}

/** A metade direita e um menu, e nao arrasto./** A metade direita e um menu, e nao arrasto.

    Um botao so, com duas zonas: o corpo arrasta o arquivo, a seta escolhe como
    ele sai. Junto ou separado e uma decisao que so importa na hora de mandar a
    frase para algum lugar -- deixar isso num controle solto do outro lado da
    janela era pedir para o usuario decidir antes de saber que ia precisar. */
void MidiDragButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    ui::FlatButton::paintButton (g, over, down);

    auto b = getLocalBounds().toFloat();
    auto zona = b.removeFromRight ((float) arrowZone);

    g.setColour (ui::col::stroke);
    g.fillRect (zona.getX(), zona.getY() + 9.0f, 1.0f, zona.getHeight() - 18.0f);

    // Chevron para baixo, tres pontos e um traco.
    const auto c = zona.getCentre();
    juce::Path seta;
    seta.startNewSubPath (c.x - 3.5f, c.y - 1.5f);
    seta.lineTo (c.x, c.y + 2.0f);
    seta.lineTo (c.x + 3.5f, c.y - 1.5f);

    g.setColour (ui::col::text2);
    g.strokePath (seta, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
}

void MidiDragButton::mouseDown (const juce::MouseEvent& e)
{
    arrastando = false;
    noMenu = e.position.x < (float) (getWidth() - arrowZone);

    if (noMenu)
        return;

    auto& s = proc.apvts;
    const int atual = (int) *s.getRawParameterValue (pid::routing);

    juce::PopupMenu menu;

    if (menuLook != nullptr)
        menu.setLookAndFeel (menuLook);

    menu.addSectionHeader ("ARRASTAR COMO");
    menu.addItem (1, Routing::name (Routing::single), true, atual == Routing::single);
    menu.addItem (2, Routing::name (Routing::split), true, atual == Routing::split);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
        [&s] (int escolha)
        {
            if (escolha == 0)
                return;

            if (auto* p = s.getParameter (pid::routing))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (
                    p->convertTo0to1 ((float) (escolha == 1 ? Routing::single
                                                            : Routing::split)));
                p->endChangeGesture();
            }
        });
}

void MidiDragButton::mouseDrag (const juce::MouseEvent& e)
{
    if (! noMenu)
        return;                       // o gesto comecou na seta: nao e arrasto

    if (arrastando || e.getDistanceFromDragStart() < 6)
        return;

    arrastando = true;
    iniciaArrasto (*this, writeMidiFile(), "MIDI");
}

void MidiDragButton::mouseUp (const juce::MouseEvent& e)
{
    arrastando = false;
    ui::FlatButton::mouseUp (e);
}

//==============================================================================
juce::File WavDragButton::writeWavFile() const
{
    constexpr double sr = 44100.0;

    juce::AudioBuffer<float> audio;

    const auto& p = proc.livePhrase();

    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                       .getChildFile ("melody");
    dir.createDirectory();

    const auto name = juce::String ("melody_") + keyName (p.root)
                        + (p.minor ? "m" : "") + "_" + juce::String (p.bpm) + "bpm.wav";

    auto out = dir.getChildFile (name);
    out.deleteFile();

    if (! proc.renderPhrase (audio, sr))
        return {};

    std::unique_ptr<juce::FileOutputStream> stream (out.createOutputStream());

    if (stream == nullptr)
        return {};

    juce::WavAudioFormat wav;

    std::unique_ptr<juce::AudioFormatWriter> writer (
        wav.createWriterFor (stream.release(), sr, 2, 24, {}, 0));

    if (writer == nullptr)
        return {};

    writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
    writer.reset();

    return out;
}

void WavDragButton::mouseDown (const juce::MouseEvent& e)
{
    arrastando = false;
    ui::FlatButton::mouseDown (e);
}

void WavDragButton::mouseDrag (const juce::MouseEvent& e)
{
    if (arrastando || e.getDistanceFromDragStart() < 6)
        return;

    arrastando = true;
    iniciaArrasto (*this, writeWavFile(), "WAV");
}

void WavDragButton::mouseUp (const juce::MouseEvent& e)
{
    arrastando = false;
    ui::FlatButton::mouseUp (e);
}

//==============================================================================
MelodyEditor::MelodyEditor (MelodyProcessor& p)
    : juce::AudioProcessorEditor (&p), proc (p), dragMidi (p), dragWav (p)
{
    auto& s = proc.apvts;

    source.setOptions ({ Source::name (0), Source::name (1) });
    source.onChange = [this, &s] (int i)
    {
        setChoice (s, pid::source, i);
        proc.refresh();
        roll.setPhrase (proc.livePhrase());
    };

    length.setOptions ({ Length::name (0), Length::name (1) });
    length.onChange = [this, &s] (int i)
    {
        setChoice (s, pid::length, i);
        proc.refresh();
        roll.setPhrase (proc.livePhrase());
    };

    scale.setOptions ({ ScaleChoice::name (0), ScaleChoice::name (1) });
    scale.onChange = [this, &s] (int i)
    {
        setChoice (s, pid::scale, i);
        proc.refresh();
        roll.setPhrase (proc.livePhrase());
    };

    key.setRange (0, 11);
    key.format = [] (int v) { return juce::String (keyName (v)); };
    key.onChange = [this, &s] (int v)
    {
        setChoice (s, pid::key, v);
        proc.refresh();
        roll.setPhrase (proc.livePhrase());
    };

    // A SEMENTE VEM DO RELOGIO, e nao de um contador. Contador em ordem faz o
    // usuario andar pelo banco na ordem do banco -- e o banco esta ordenado por
    // qualidade, entao ele desceria a ladeira sem saber.
    generate.onClick = [this]
    {
        proc.regenerate ((std::uint32_t) juce::Time::getHighResolutionTicks());
        roll.setPhrase (proc.livePhrase());
        updateInfo();
        generate.pulse();
    };

    play.onClick = [this]
    {
        // O clique so mexe no ESTADO. Quem escreve o rotulo e o timer, num
        // lugar so -- escrever nos dois deixava os dois discordarem, que era
        // metade do defeito.
        if (proc.hostDriving())
            return;

        if (proc.auditioning())
            proc.stopAudition();
        else
            proc.startAudition();

        refreshPlayButton();
    };

    dragMidi.setMenuLook (&menuLook);
    dragMidi.setTextRightInset (MidiDragButton::arrowZone);

    soundSlot.onClick = [this] { showSoundMenu(); };
    openGuest.onClick = [this] { toggleGuestEditor(); };

    struct Wiring { ui::LayerPill* row; const char* onId; const char* volId; };

    const Wiring wiring[] =
    {
        { &melodyRow, pid::melodyOn, pid::melodyVol },
        { &chordsRow, pid::chordsOn, pid::chordsVol },
        { &bassRow,   pid::bassOn,   pid::bassVol }
    };

    for (const auto& w : wiring)
    {
        const char* onId = w.onId;
        const char* volId = w.volId;

        w.row->onToggle = [&s, onId] (bool on) { setFloat (s, onId, on ? 1.0f : 0.0f); };
        w.row->onVolume = [&s, volId] (float v) { setFloat (s, volId, v); };
    }

    const std::initializer_list<juce::Component*> kids
    {
        &source, &scale, &key, &generate, &play, &length,
        &dragMidi, &dragWav, &melodyRow, &chordsRow, &bassRow, &roll
    };

    for (auto* c : kids)
        addAndMakeVisible (c);

    // Som e canal moram atras da engrenagem: sao os dois unicos que sobraram
    // com uso raro o bastante para justificar um clique.


    // No efeito MIDI nao ha barramento de audio, entao SOM INTERNO e TOCAR nao
    // tem por onde soar. Some com os dois em vez de deixar dois controles
    // mortos: o preview ali e o instrumento que o usuario pos depois.
    // Som e canal ficam ESCONDIDOS ate a engrenagem abrir. `addChildComponent`
    // e nao `addAndMakeVisible`: a diferenca entre os dois era o motivo de eles
    // aparecerem flutuando sobre o roll com o painel fechado.
    // O seletor de som fica NA BARRA DE CIMA, junto do resto que define o
    // resultado. Ele estava atras da engrenagem, e escolher o som e coisa que
    // se faz olhando -- nao merecia um clique a mais.
    addAndMakeVisible (soundSlot);
    addAndMakeVisible (openGuest);
    addChildComponent (guestView);

    if (MelodyProcessor::midiEffect)
        play.setVisible (false);

    seenGeneration = proc.rack.generation();

    syncFromParams();
    roll.setPhrase (proc.livePhrase());
    updateInfo();

    // O TAMANHO SALVO E LIDO ANTES DE LIGAR O REDIMENSIONAMENTO.
    //
    // `setResizeLimits` chama `setBoundsConstrained` na hora, e o editor recem
    // construido tem 0x0: ele sobe para o MINIMO, dispara `resized()`, e o
    // `resized()` grava 900x480 em `proc.editorW`. Lendo depois, a janela abria
    // sempre no minimo -- e o tamanho que o projeto guardou era destruido pela
    // propria abertura, antes de alguem ver.
    const int salvoW = proc.editorW;
    const int salvoH = proc.editorH;

    setResizable (true, true);
    setResizeLimits (minW, minH, maxW, maxH);

    setSize (salvoW > 0 ? juce::jlimit (minW, maxW, salvoW) : windowW,
             salvoH > 0 ? juce::jlimit (minH, maxH, salvoH) : windowH);

    startTimerHz (30);
}

//==============================================================================
void MelodyEditor::syncFromParams()
{
    auto& s = proc.apvts;

    source.setSelected (choiceOf (s, pid::source));
    length.setSelected (choiceOf (s, pid::length));
    scale.setSelected (choiceOf (s, pid::scale));
    key.setValue (choiceOf (s, pid::key));

    melodyRow.setState (*s.getRawParameterValue (pid::melodyOn) > 0.5f,
                        *s.getRawParameterValue (pid::melodyVol));
    chordsRow.setState (*s.getRawParameterValue (pid::chordsOn) > 0.5f,
                        *s.getRawParameterValue (pid::chordsVol));
    bassRow.setState (*s.getRawParameterValue (pid::bassOn) > 0.5f,
                      *s.getRawParameterValue (pid::bassVol));

    refreshSoundSlot();
}

void MelodyEditor::openGuestForShot()
{
    openGuestEditor();
}

void MelodyEditor::clickForTest (const juce::String& qual)
{
    // Chama o callback DIRETO. `triggerClick` e assincrono, e o smoke nao roda
    // laco de mensagens -- o clique nunca chegaria, e o caso mediria a ausencia
    // do laco em vez da ausencia da ligacao.
    if (qual == "gerar" && generate.onClick != nullptr)
        generate.onClick();
    else if (qual == "tocar" && play.onClick != nullptr)
        play.onClick();
}

/** O rotulo do TOCAR sai do ESTADO, e de um lugar so.

    Tres textos porque ha tres estados, e nao dois. Com o host rodando, apertar
    o botao nao pode parar nada -- entao ele diz HOST em vez de mentir que
    tocaria ou pararia. */
void MelodyEditor::refreshPlayButton()
{
    const bool host = proc.hostDriving();

    const juce::String texto = host ? "HOST"
                                    : (proc.auditioning() ? "PARAR" : "TOCAR");

    if (play.getButtonText() != texto)
        play.setButtonText (texto);

    play.setAccent (host ? col::text3 : col::text);
}

void MelodyEditor::refreshSoundSlot()
{
    const auto nome = proc.rack.name();

    if (nome.isNotEmpty())
    {
        soundSlot.setButtonText (nome.toUpperCase());
        soundSlot.setAccent (col::accent);
    }
    else
    {
        const bool interno = *proc.apvts.getRawParameterValue (pid::internal) > 0.5f;

        soundSlot.setButtonText (interno ? "SOM INTERNO" : "SILENCIO");
        soundSlot.setAccent (interno ? col::text : col::text3);
    }

    openGuest.setEnabled (proc.rack.loaded());
    openGuest.setAccent (proc.rack.loaded() ? col::text : col::text3);
}

//==============================================================================
void MelodyEditor::showSoundMenu()
{
    auto& s = proc.apvts;
    const bool interno = *s.getRawParameterValue (pid::internal) > 0.5f;
    const bool temHospede = proc.rack.loaded();

    juce::PopupMenu menu;
    menu.setLookAndFeel (&menuLook);

    menu.addSectionHeader ("SEM INSTRUMENTO");
    menu.addItem (1, "Som interno", true, ! temHospede && interno);
    menu.addItem (2, "Silencio (so MIDI)", true, ! temHospede && ! interno);

    // A lista sai de ARQUIVO, nao de varredura: varrer de verdade carrega cada
    // plugin da maquina para perguntar o nome, o que custa minutos e derruba a
    // DAW no primeiro bundle quebrado.
    const auto achados = proc.rack.installed();
    const auto atual = proc.rack.source();

    juce::PopupMenu vst3, au;
    int id = 100;

    // SO INSTRUMENTOS. Um efeito de audio no slot carrega, nao soa, e nao ha
    // nada na janela que explique por que -- ele nao recebe nota.
    int mostrados = 0;

    for (const auto& f : achados)
    {
        if (! f.instrument)
            continue;

        ++mostrados;
        auto& destino = f.format.containsIgnoreCase ("VST3") ? vst3 : au;
        destino.addItem (id, f.name, true, f.identifier == atual);
        ++id;
    }

    if (mostrados > 0)
    {
        menu.addSectionHeader ("INSTRUMENTOS");

        if (vst3.getNumItems() > 0) menu.addSubMenu ("VST3", vst3);
        if (au.getNumItems() > 0)   menu.addSubMenu ("Audio Unit", au);
    }

    menu.addSeparator();
    menu.addItem (3, "Escolher arquivo...");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (soundSlot),
        [this, achados] (int escolha)
        {
            if (escolha == 0)
                return;

            if (escolha == 1 || escolha == 2)
            {
                closeGuestEditor();
                proc.rack.unload();
                seenGeneration = proc.rack.generation();
                setFloat (proc.apvts, pid::internal, escolha == 1 ? 1.0f : 0.0f);
                refreshSoundSlot();
                return;
            }

            if (escolha == 3)
            {
                // A pasta onde os plugins moram em cada sistema. No Windows o
                // caminho de Mac viraria C:\\Users\\x\\Library, que nao existe, e o
                // seletor abriria numa pasta qualquer.
               #if JUCE_WINDOWS
                const auto pastaDePlugins =
                    juce::File::getSpecialLocation (juce::File::globalApplicationsDirectory)
                        .getChildFile ("Common Files/VST3");
               #else
                const auto pastaDePlugins =
                    juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                        .getChildFile ("Library/Audio/Plug-Ins");
               #endif

                chooser = std::make_unique<juce::FileChooser> (
                    "Escolher instrumento", pastaDePlugins, "*.vst3");

                chooser->launchAsync (juce::FileBrowserComponent::openMode
                                        | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::canSelectDirectories,
                    [this] (const juce::FileChooser& fc)
                    {
                        const auto f = fc.getResult();

                        if (f != juce::File())
                            loadGuest (f.getFullPathName());
                    });

                return;
            }

            // O indice conta so os mostrados, entao a busca refaz o mesmo
            // filtro -- guardar uma lista paralela seria uma segunda fonte de
            // verdade para a mesma coisa.
            int i = 100;

            for (const auto& f : achados)
            {
                if (! f.instrument)
                    continue;

                if (i++ == escolha)
                {
                    loadGuest (f.identifier);
                    return;
                }
            }
        });
}

void MelodyEditor::loadGuest (const juce::String& id)
{
    juce::String erro;

    closeGuestEditor();

    if (! proc.rack.load (id, erro))
    {
        juce::NativeMessageBox::showAsync (
            juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::WarningIcon)
                .withTitle ("melody")
                .withMessage ("Nao consegui carregar o instrumento.\n\n" + erro)
                .withButton ("ok"),
            nullptr);

        refreshSoundSlot();
        return;
    }

    seenGeneration = proc.rack.generation();

    refreshSoundSlot();
    openGuestEditor();
}

void MelodyEditor::toggleGuestEditor()
{
    if (showingGuest)
        closeGuestEditor();
    else
        openGuestEditor();
}

void MelodyEditor::setChromeVisible (bool visible)
{
    juce::Component* const chrome[] = { &roll, &generate, &play, &dragMidi, &dragWav,
                                        &source, &length, &key, &scale, &soundSlot,
                                        &melodyRow, &chordsRow, &bassRow };

    for (auto* c : chrome)
        c->setVisible (visible);
}

void MelodyEditor::openGuestEditor()
{
    if (showingGuest)
        return;

    auto* ed = proc.rack.createGuestEditor();

    if (ed == nullptr)
        return;

    ed->addComponentListener (this);

    // O Viewport passa a ser dono: quando ele solta o editor, o destrutor do
    // AudioProcessorEditor avisa o processador do convidado sozinho.
    guestView.setViewedComponent (ed, true);
    guestView.setVisible (true);
    showingGuest = true;

    setChromeVisible (false);
    openGuest.setButtonText ("VOLTAR");

    fitToGuest();
}

void MelodyEditor::closeGuestEditor()
{
    if (! showingGuest)
        return;

    if (auto* ed = guestView.getViewedComponent())
        ed->removeComponentListener (this);

    guestView.setViewedComponent (nullptr, true);
    guestView.setVisible (false);
    showingGuest = false;

    setChromeVisible (true);
    openGuest.setButtonText ("ABRIR");

    setSize (windowW, windowH);
}

/** A janela vira o tamanho do instrumento, ate onde a TELA deixa.

    Nada de somar a coluna de controles: eles estao escondidos enquanto o
    instrumento esta aberto. Um instrumento de 1.180 pixels dava 1.430 de janela
    na versao anterior, com rolagem horizontal por causa de controles que
    ninguem estava olhando. O que passar do tamanho da tela rola. */
void MelodyEditor::fitToGuest()
{
    auto* ed = guestView.getViewedComponent();

    if (ed == nullptr)
        return;

    auto tela = juce::Rectangle<int> (1600, 1000);

    if (auto* display = juce::Desktop::getInstance().getDisplays()
                          .getPrimaryDisplay())
        tela = display->userArea;

    const int larguraMax = juce::jmax (400, tela.getWidth() - 2 * margin - 40);
    const int alturaMax  = juce::jmax (300, tela.getHeight() - guestBarH - 2 * margin - 80);

    int w = juce::jlimit (360, larguraMax, ed->getWidth());
    int h = juce::jlimit (150, alturaMax, ed->getHeight());

    // Barra de rolagem rouba espaco do outro eixo, e o Viewport entao acha que
    // tambem falta espaco la -- aparece uma segunda barra que nao precisava
    // existir. Abrir lugar para ela de uma vez corta o ciclo.
    const int grossura = guestView.getScrollBarThickness();

    if (h < ed->getHeight() && w + grossura <= larguraMax)
        w += grossura;

    if (w < ed->getWidth() && h + grossura <= alturaMax)
        h += grossura;

    resizingForGuest = true;
    setSize (w + 2 * margin, h + guestBarH + 2 * margin);
    resizingForGuest = false;
}

/** Convidado redimensionavel que muda de tamanho sozinho -- a janela acompanha. */
void MelodyEditor::componentMovedOrResized (juce::Component& c, bool, bool wasResized)
{
    if (! wasResized || ! showingGuest || resizingForGuest)
        return;

    if (&c == guestView.getViewedComponent())
        fitToGuest();
}

//==============================================================================
//==============================================================================
void MelodyEditor::demoPhrase (std::uint32_t semente)
{
    proc.regenerate (semente);
    roll.setPhrase (proc.livePhrase());
    updateInfo();
    repaint();
}

void MelodyEditor::updateInfo()
{
    const auto& p = proc.livePhrase();

    // O INDICE E O TAMANHO DO BANCO SAIRAM.
    //
    // "875 / 4075" nao ajuda ninguem a decidir se a frase serve -- e numero de
    // bastidor, e conta quantas frases existem ali dentro, que e coisa que o
    // usuario nao precisa saber. A mesma regra vale para a pagina; ver a secao
    // "O que a pagina nao diz" no README de melodyc_lp.
    //
    // Se um dia precisar disto para depurar, a saida e o log, e nao a regua.
    juce::String next;
    next << juce::String (p.bpm) << " BPM";

    if (p.harmonyIndex != p.melodyIndex)
        next << "     " << juce::String (juce::roundToInt (p.fit * 100.0f)) << "%";

    if (next != info)
    {
        info = next;
        roll.setStatus (info);
    }
}

//==============================================================================
void MelodyEditor::timerCallback()
{
    // O convidado pode ter trocado por baixo da janela: carregar um preset na
    // DAW chama setStateInformation, que descarrega e recarrega o instrumento
    // sem passar por aqui. O editor embutido ficaria apontando para um plugin
    // ja destruido.
    if (const int agora = proc.rack.generation(); agora != seenGeneration)
    {
        seenGeneration = agora;
        closeGuestEditor();
        refreshSoundSlot();
    }

    roll.setPlayhead (proc.playPosition(), proc.isMoving());
    roll.tick();

    // O pulso do botao repinta pelo mesmo relogio do roll, e SO enquanto dura:
    // fora disso a barra de baixo nao gasta um quadro.
    if (generate.pulsing())
        generate.repaint();

    updateInfo();

    refreshPlayButton();
}

//==============================================================================
void MelodyEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::base);

    if (showingGuest)
    {
        g.setColour (col::text);
        g.setFont (uiFont (13.0f, true));
        g.drawText (proc.rack.name(),
                    juce::Rectangle<int> (margin + 130, margin,
                                          getWidth() - margin * 2 - 130, guestBarH),
                    juce::Justification::centredLeft, false);

        paintRule (g, { margin, margin + guestBarH - 2, getWidth() - 2 * margin, 1 });
        return;
    }

    // TRES FAIXAS: o que define a frase, a frase, e o que se faz com ela.
    //
    // A marca fica na de cima com os controles, e nao numa faixa propria: uma
    // linha inteira so para dizer o nome do plugin e espaco tirado do unico
    // lugar que importa.
    g.setColour (col::text);
    g.setFont (uiFont (15.0f, true));
    g.drawText ("melodyc", 20, 0, 100, topBar, juce::Justification::centredLeft, false);

    paintRule (g, { 0, topBar - 1, getWidth(), 1 });
    paintRule (g, { 0, getHeight() - bottomBar, getWidth(), 1 });

    // O estado mora DENTRO do roll, no canto de cima -- e o roll que o desenha.
    // Ele descreve o que esta ali, e a barra de cima virou inteira de controle:
    // forcar uma faixa de texto nela seria tirar espaco de quem tem funcao.
}

//==============================================================================
void MelodyEditor::resized()
{
    captions.clearQuick();

    // O tamanho vai para o processador a cada arrasto, e de la para o projeto
    // salvo. Escrever aqui e o unico jeito de pegar TODO redimensionamento --
    // inclusive o que a DAW faz sozinha ao restaurar uma janela.
    if (! showingGuest && getWidth() > 0 && getHeight() > 0)
    {
        proc.editorW = getWidth();
        proc.editorH = getHeight();
    }

    if (showingGuest)
    {
        auto b = getLocalBounds().reduced (margin);
        auto barra = b.removeFromTop (guestBarH);

        openGuest.setBounds (barra.removeFromLeft (110).withSizeKeepingCentre (110, 28));
        openGuest.setVisible (true);
        guestView.setBounds (b);
        return;
    }

    auto b = getLocalBounds();

    //--------------------------------------------------------------------------
    // EM CIMA: o que define a frase. Tudo visivel, nada atras de clique.
    //
    // Sao quatro controles, e ler os quatro de relance vale mais que a linha
    // limpa: sao eles que respondem "o que vai sair quando eu apertar GERAR".
    // A MARGEM E DOS DOIS LADOS.
    //
    // Era `reduced (0, 11)` -- so vertical -- e o lado direito encostava na
    // borda. Passava despercebido porque a esquerda tem a marca ocupando 110
    // pixels, e isso parece margem sem ser: e conteudo.
    auto topo = b.removeFromTop (topBar).reduced (20, 11);
    topo.removeFromLeft (90);                      // o que sobra da marca

    source.setBounds (topo.removeFromLeft (160));
    topo.removeFromLeft (8);
    length.setBounds (topo.removeFromLeft (180));

    topo.removeFromLeft (16);
    key.setBounds (topo.removeFromLeft (104));
    topo.removeFromLeft (8);
    scale.setBounds (topo.removeFromLeft (140));

    // O seletor de som ocupa a direita, onde estava a engrenagem.
    openGuest.setBounds (topo.removeFromRight (64));
    topo.removeFromRight (8);
    soundSlot.setBounds (topo.removeFromRight (juce::jmin (176, topo.getWidth())));

    //--------------------------------------------------------------------------
    // EMBAIXO: o que se faz. GERAR sempre no mesmo lugar.
    auto acao = b.removeFromBottom (bottomBar).reduced (20, 13);

    generate.setBounds (acao.removeFromLeft (150));
    acao.removeFromLeft (8);
    play.setBounds (acao.removeFromLeft (96));

    dragWav.setBounds (acao.removeFromRight (120));
    acao.removeFromRight (8);
    dragMidi.setBounds (acao.removeFromRight (120));

    // As camadas ficam junto do GERAR, que e onde a mao esta enquanto se ouve.
    acao.removeFromLeft (18);
    melodyRow.setBounds (acao.removeFromLeft (112));
    acao.removeFromLeft (6);
    chordsRow.setBounds (acao.removeFromLeft (112));
    acao.removeFromLeft (6);
    bassRow.setBounds (acao.removeFromLeft (112));

    //--------------------------------------------------------------------------
    // NO MEIO: so o roll, de borda a borda.
    roll.setBounds (b);
    guestView.setBounds (b);


}
