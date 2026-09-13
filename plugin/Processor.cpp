/*
  ==============================================================================

    Processor.cpp

  ==============================================================================
*/

#include "plugin/Processor.h"

#include "BankData.h"
#include "plugin/Editor.h"

#include <algorithm>
#include <vector>

//==============================================================================
MelodyProcessor::MelodyProcessor()
    : AudioProcessor (
       #if JucePlugin_IsMidiEffect
        // Sem barramento de audio nenhum. E o que faz o host aceitar o plugin
        // ANTES do instrumento na mesma faixa -- que e o ponto desta versao: a
        // frase sai daqui e toca com o som que o usuario ja escolheu.
        BusesProperties()
       #else
        BusesProperties().withOutput ("Saida", juce::AudioChannelSet::stereo(), true)
       #endif
      ),
      apvts (*this, nullptr, "melody", makeLayout())
{
    // O banco vive no binario. Se ele nao carregar, o plugin abre e nao gera --
    // e a janela diz isso, em vez de mostrar um piano roll vazio sem motivo.
    bank.load (BankData::bank_bin, (std::size_t) BankData::bank_binSize);
    gen.setBank (bank);

    // O que muda a frase avisa. Sem isto, automatizar o tom no host nao faria
    // nada ate alguem abrir a janela e mexer -- e o usuario so descobriria no
    // bounce, com a musica ja escrita por cima.
    for (const char* id : { pid::key, pid::scale, pid::source, pid::length })
        apvts.addParameterListener (id, this);

    // A PRIMEIRA FRASE E SORTEADA, e nao a semente 1.
    //
    // Abrir sempre com a mesma melodia faz o plugin parecer que so tem uma:
    // quem coloca ele numa faixa nova ouve o que ja ouviu da ultima vez, e a
    // impressao e de que ele nao gera nada ate alguem apertar GERAR.
    //
    // Isto vale so para instancia NOVA. Reabrir um projeto passa por
    // setStateInformation, que devolve a semente gravada -- a musica que ja
    // estava escrita continua sendo a mesma.
    regenerate ((std::uint32_t) juce::Random::getSystemRandom().nextInt()
                  ^ (std::uint32_t) juce::Time::getHighResolutionTicks());
}

//==============================================================================
void MelodyProcessor::prepareToPlay (double rate, int samplesPerBlock)
{
    sampleRate = rate > 0.0 ? rate : 44100.0;
    synth.prepare (sampleRate);
    rack.prepare (sampleRate, samplesPerBlock);
    guestMidi.ensureSize (2048);

    internalBeat = 0.0;
    lastEnd = -1.0;
    lastHostEnd = -1.0;
    phraseOffset = 0.0;
    wasRunning = false;
    position.store (0.0, std::memory_order_relaxed);
}

void MelodyProcessor::releaseResources()
{
    rack.release();
}

bool MelodyProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
   #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
   #else
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
   #endif
}

//==============================================================================
void MelodyProcessor::publish (const melody::Phrase& p)
{
    const int spare = 1 - live.load (std::memory_order_relaxed);

    phrases[(std::size_t) spare] = p;

    // Enquanto o host nao disser o andamento dele, vale o do trecho. Sem isto o
    // arrastar sai com o chute inicial em vez do BPM do arquivo de origem.
    if (! sawHostTempo.load (std::memory_order_acquire))
        bpm.store (p.bpm > 0 ? (double) p.bpm : 140.0, std::memory_order_relaxed);

    live.store (spare, std::memory_order_release);
    panic.store (true, std::memory_order_release);
}

void MelodyProcessor::regenerate (std::uint32_t newSeed)
{
    // A SEMENTE VAI CRUA. Um `| 1u` aqui parecia inofensivo e colapsava metade
    // do espaco: 123456 e 123457 viravam a mesma semente, e sementes vizinhas
    // davam a mesma frase -- exatamente a reclamacao que o banco existe para
    // resolver. O xorshift ja garante estado nao-nulo por conta propria, DEPOIS
    // de misturar a entrada, que e onde essa garantia nao custa nada.
    seed = newSeed;
    refresh();
}

void MelodyProcessor::parameterChanged (const juce::String&, float)
{
    refresh();
}

void MelodyProcessor::refresh()
{
    // `getRawParameterValue` de um AudioParameterChoice devolve o INDICE.
    // `getParameterAsValue` devolveria o valor normalizado, e 3 de 12 escolhas
    // viraria 0.27 -- que truncado para int e zero, e o tom escolhido sumia
    // sem erro nenhum.
    const int key = (int) *apvts.getRawParameterValue (pid::key);
    const int scaleIdx = (int) *apvts.getRawParameterValue (pid::scale);
    const int src = (int) *apvts.getRawParameterValue (pid::source);
    const int len = (int) *apvts.getRawParameterValue (pid::length);

    melody::Phrase fresh;
    gen.generate (fresh, key, scaleIdx == ScaleChoice::minor, seed,
                  // A oitava e escolhida por custo em chooseBase, e nao pelo
                  // usuario: o controle manual so servia para desfazer uma
                  // colocacao que o gerador ja acerta.
                  src == Source::recombined, 0,
                  len == Length::eight ? melody::longBars : melody::shortBars);

    publish (fresh);
}

//==============================================================================
bool MelodyProcessor::renderPhrase (juce::AudioBuffer<float>& out, double sr)
{
    const auto& p = livePhrase();

    if (p.empty() || sr <= 0.0)
        return false;

    const double useBpm = juce::jlimit (40.0, 300.0, tempo());
    const double samplesPerBeat = 60.0 / useBpm * sr;

    // Cauda de dois segundos: sem ela o arquivo corta a ultima nota no ataque
    // seguinte, que e o defeito mais obvio que um render pode ter.
    const int corpo = (int) (p.loopBeats() * samplesPerBeat);
    const int total = corpo + (int) (2.0 * sr);

    out.setSize (2, total, false, true, true);
    out.clear();

    struct Ev { int sample; bool on; int layer; int pitch; int vel; };
    std::vector<Ev> eventos;

    melody::advance (p, 0.0, p.loopBeats(),
        [&eventos, samplesPerBeat] (melody::Layer l, int pitch, int vel, double at)
        {
            eventos.push_back ({ (int) (at * samplesPerBeat), true, (int) l, pitch, vel });
        },
        [&eventos, samplesPerBeat] (melody::Layer l, int pitch, double at)
        {
            eventos.push_back ({ (int) (at * samplesPerBeat), false, (int) l, pitch, 0 });
        });

    std::sort (eventos.begin(), eventos.end(), [] (const Ev& a, const Ev& b)
    {
        return a.sample != b.sample ? a.sample < b.sample : (a.on ? 0 : 1) < (b.on ? 0 : 1);
    });

    const bool comConvidado = rack.hasGuest();
    const bool split = (int) *apvts.getRawParameterValue (pid::routing) == Routing::split;

    const bool camadaLigada[3] =
    {
        *apvts.getRawParameterValue (pid::melodyOn) > 0.5f,
        *apvts.getRawParameterValue (pid::chordsOn) > 0.5f,
        *apvts.getRawParameterValue (pid::bassOn)   > 0.5f
    };

    melody::Synth local;
    local.prepare (sr);
    local.setLayerGain (0, *apvts.getRawParameterValue (pid::melodyVol));
    local.setLayerGain (1, *apvts.getRawParameterValue (pid::chordsVol));
    local.setLayerGain (2, *apvts.getRawParameterValue (pid::bassVol));

    constexpr int block = 512;
    std::size_t proximo = 0;

    for (int inicio = 0; inicio < total; inicio += block)
    {
        const int n = juce::jmin (block, total - inicio);

        juce::MidiBuffer midi;

        while (proximo < eventos.size() && eventos[proximo].sample < inicio + n)
        {
            const auto& e = eventos[proximo++];

            if (! camadaLigada[e.layer])
                continue;

            const int desloc = juce::jlimit (0, n - 1, e.sample - inicio);
            const int canal = split ? e.layer + 1 : 1;

            if (comConvidado)
                midi.addEvent (e.on ? juce::MidiMessage::noteOn (canal, e.pitch,
                                                                 (juce::uint8) e.vel)
                                    : juce::MidiMessage::noteOff (canal, e.pitch),
                               desloc);
            else if (e.on)
                local.noteOn (e.layer, e.pitch, (float) e.vel * (1.0f / 127.0f));
            else
                local.noteOff (e.layer, e.pitch);
        }

        float* l = out.getWritePointer (0) + inicio;
        float* r = out.getWritePointer (1) + inicio;

        if (comConvidado)
        {
            juce::AudioBuffer<float> fatia (out.getArrayOfWritePointers(), 2, inicio, n);
            rack.renderBlock (fatia, midi);
        }
        else
        {
            local.render (l, r, n);
        }
    }

    if (comConvidado)
        rack.silenceGuest();

    // Normaliza so se estourou. Baixar um render que ja estava bom mudaria o
    // volume relativo entre um arraste e o proximo.
    float pico = 0.0f;

    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        pico = juce::jmax (pico, out.getMagnitude (ch, 0, total));

    if (pico > 0.99f)
        out.applyGain (0.99f / pico);

    return pico > 1.0e-5f;
}

//==============================================================================
void MelodyProcessor::startAudition()
{
    internalBeat = 0.0;
    internalOn.store (true, std::memory_order_release);
}

void MelodyProcessor::stopAudition()
{
    internalOn.store (false, std::memory_order_release);
    panic.store (true, std::memory_order_release);
}

//==============================================================================
void MelodyProcessor::emitBlock (juce::MidiBuffer& midi, double from, double to,
                                 int numSamples, double beatsPerSample)
{
    const auto& p = phrases[(std::size_t) live.load (std::memory_order_acquire)];

    const bool on[3] =
    {
        *apvts.getRawParameterValue (pid::melodyOn) > 0.5f,
        *apvts.getRawParameterValue (pid::chordsOn) > 0.5f,
        *apvts.getRawParameterValue (pid::bassOn)   > 0.5f
    };

    const bool internalSound = *apvts.getRawParameterValue (pid::internal) > 0.5f;

    const bool split = (int) *apvts.getRawParameterValue (pid::routing) == Routing::split;

    auto channelFor = [split] (melody::Layer layer)
    {
        return split ? melody::layerChannel (layer) : 1;
    };

    auto toSample = [numSamples, beatsPerSample] (double beatOffset)
    {
        const int s = beatsPerSample > 0.0 ? (int) (beatOffset / beatsPerSample) : 0;
        return s < 0 ? 0 : (s >= numSamples ? numSamples - 1 : s);
    };

    melody::advance (p, from, to,
        [&] (melody::Layer layer, int pitch, int vel, double at)
        {
            const int l = (int) layer;

            if (! on[l])
                return;

            midi.addEvent (juce::MidiMessage::noteOn (channelFor (layer),
                                                      pitch, (juce::uint8) vel),
                           toSample (at));

            if (internalSound)
                synth.noteOn (l, pitch, (float) vel * (1.0f / 127.0f));
        },
        [&] (melody::Layer layer, int pitch, double at)
        {
            // Com as camadas somadas num canal so, outra camada pode estar
            // segurando esta mesma altura -- e ai o note-off daqui cortaria a
            // dela. A pergunta e feita a frase, que e conhecida; o tocador
            // continua sem estado.
            const unsigned mask = split ? (1u << (int) layer) : 0b111u;

            // O EPSILON NAO E ENFEITE.
            //
            // Uma nota que termina exatamente no fim do laco tem `from + at`
            // igual a um multiplo do comprimento, e o `fmod` disso da ZERO --
            // ou seja, a pergunta "alguem mais segura esta altura?" seria feita
            // sobre o tempo zero da volta seguinte. Se houver uma nota da mesma
            // altura comecando no tempo zero (e ha, o tempo forte e onde as
            // camadas se encontram), a resposta e sim e o note-off e engolido:
            // nota presa para sempre.
            //
            // Tirar um bilionesimo antes do resto coloca a pergunta no instante
            // certo -- o fim do laco, onde a nota de fato esta.
            double t = std::fmod (from + at - 1.0e-9, p.loopBeats());
            if (t < 0.0) t += p.loopBeats();

            if (melody::heldAt (p, pitch, t, mask))
                return;

            // SEM CONDICAO DE CAMADA. O desligar estava atras do mesmo `on[l]`
            // do ligar, entao desligar ACORDES com o acorde soando pulava o
            // note-off junto e prendia a nota. Um note-off de nota que nunca
            // tocou nao custa nada; um note-on sem par custa uma nota eterna.
            midi.addEvent (juce::MidiMessage::noteOff (channelFor (layer), pitch),
                           toSample (at));

            synth.noteOff ((int) layer, pitch);
        });
}

//==============================================================================
void MelodyProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                    juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();

    // A ENTRADA MIDI VAI PARA O CONVIDADO, E SO PARA ELE.
    //
    // Sao dois destinos com regras opostas. O instrumento carregado tem de
    // receber o que o teclado do usuario tocou -- senao ele carrega um piano
    // dentro do melody e nao consegue tocar uma nota nele com a mao, que
    // qualquer um leria como plugin quebrado. Ja a saida MIDI para o resto do
    // projeto leva SO as notas geradas: repassar a entrada ali faria a frase
    // gravada trazer junto tudo o que a pessoa dedilhou enquanto procurava um
    // som.
    guestMidi.clear();
    guestMidi.addEvents (midi, 0, numSamples, 0);

    midi.clear();

    buffer.clear();

    synth.setLayerGain (0, *apvts.getRawParameterValue (pid::melodyVol));
    synth.setLayerGain (1, *apvts.getRawParameterValue (pid::chordsVol));
    synth.setLayerGain (2, *apvts.getRawParameterValue (pid::bassVol));

    //--------------------------------------------------------------------------
    // De onde vem o tempo.
    //
    // O host manda quando esta rodando -- e ai a frase fica travada na grade do
    // projeto, que e o unico jeito de ela servir para gravar. Sem host rodando,
    // o botao TOCAR usa o BPM do proprio trecho: o numero veio do arquivo de
    // origem, e a frase soa no andamento em que foi escrita.
    //--------------------------------------------------------------------------
    double hostBeat = -1.0;
    double hostBpm = 0.0;
    bool hostPlaying = false;

    if (auto* ph = getPlayHead())
    {
        if (const auto pos = ph->getPosition())
        {
            hostPlaying = pos->getIsPlaying();

            if (const auto ppq = pos->getPpqPosition())
                hostBeat = *ppq;

            if (const auto t = pos->getBpm())
                hostBpm = *t;
        }
    }

    const auto& p = phrases[(std::size_t) live.load (std::memory_order_acquire)];

    double from = 0.0, to = 0.0;
    bool running = false;

    if (hostPlaying && hostBeat >= 0.0 && hostBpm > 0.0)
    {
        const double beatsPerBlock = numSamples * hostBpm / (60.0 * sampleRate);

        // O LACO CURTO DO HOST NAO REINICIA A FRASE.
        //
        // Voltar para tras menos que o comprimento da frase e o padrao do FL
        // dando a volta: a frase continua, somando a volta ao deslocamento.
        // Voltar a frase inteira ou mais e um laco que ja contem a frase, e ai
        // seguir o host e o certo -- o deslocamento fica, a posicao salta, e o
        // salto pede silencio mais abaixo como sempre pediu.
        //
        // O custo, aceito: clicar na regua para voltar um trecho curto com o
        // transporte rodando nao volta a frase junto. Ela realinha ao parar e
        // dar play de novo, que zera o deslocamento.
        const bool seguiaHost = lastHostEnd >= 0.0;

        if (! seguiaHost)
        {
            phraseOffset = 0.0;
        }
        else
        {
            const double volta = lastHostEnd - hostBeat;

            if (volta > beatsPerBlock + 1.0e-9 && volta < p.loopBeats() - 1.0e-6)
                phraseOffset += volta;
        }

        lastHostEnd = hostBeat + beatsPerBlock;

        from = hostBeat + phraseOffset;
        to = from + beatsPerBlock;

        // O BLOCO EMENDA NO FIM DO ANTERIOR QUANDO A DIFERENCA E SO
        // ARREDONDAMENTO.
        //
        // `from` recalculado do host e o `to` do bloco anterior deveriam ser o
        // mesmo numero, e costumam diferir por um ou dois ulps -- mais ainda
        // depois de somar voltas ao deslocamento. Quando essa costura cai num
        // tempo forte (a 120 BPM com blocos de 512 ela cai em TODO tempo forte),
        // a nota do tempo forte fica dos dois lados e sai duas vezes, ou de
        // nenhum e nao sai.
        //
        // A TOLERANCIA E DE ARREDONDAMENTO, e nao de um bloco. A primeira versao
        // usou um bloco inteiro e engoliu nota: um host que repete a mesma
        // posicao em dois blocos (pre-roll, ou o inicio do transporte) tinha o
        // `from` colado no fim anterior enquanto o `to`, preso ao host, ficava
        // no mesmo lugar -- janela vazia, e a nota do tempo zero sumia. O caso
        // "com o host rodando sai som" pegou isso na hora.
        if (seguiaHost && lastEnd >= 0.0 && std::abs (from - lastEnd) <= 1.0e-6)
            from = lastEnd;

        running = true;

        bpm.store (hostBpm, std::memory_order_relaxed);
        sawHostTempo.store (true, std::memory_order_release);

        // O HOST GANHA, E DESLIGA A AUDICAO INTERNA.
        //
        // Sem isto, quem apertou TOCAR antes de rodar o projeto ficava com as
        // duas ligadas: o host tocava por cima, e ao apertar espaco para parar,
        // a audicao interna assumia e o som continuava. Da barra de transporte
        // nao havia como parar aquilo -- o defeito relatado foi exatamente
        // "clico no espaco e ele nao para".
        if (internalOn.exchange (false, std::memory_order_acq_rel))
            panic.store (true, std::memory_order_release);
    }
    else if (internalOn.load (std::memory_order_acquire))
    {
        // Fora do host o relogio da frase recomeca do zero na proxima vez que
        // o host rodar -- senao um deslocamento velho sobreviveria a um stop.
        lastHostEnd = -1.0;

        const double useBpm = p.bpm > 0 ? (double) p.bpm : 140.0;
        const double beatsPerBlock = numSamples * useBpm / (60.0 * sampleRate);

        from = internalBeat;
        to = internalBeat + beatsPerBlock;
        internalBeat = to;
        running = true;

        bpm.store (useBpm, std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------
    // QUEM PARA PEDE SILENCIO.
    //
    // Tres descontinuidades, e as tres deixavam nota presa:
    //
    //   PARAR  -- foi o defeito relatado, "o acorde fica tocando pra sempre".
    //             O acorde e a camada com as notas mais longas, entao e a que
    //             quase sempre esta soando na hora do stop.
    //   SALTAR -- laco voltando, ou clique na regua: a janela seguinte cai em
    //             outro lugar e nunca atravessa o fim da nota que ficou atras.
    //   COMECAR -- dar play com sobra de alguma coisa e barato de cobrir aqui.
    //
    // O salto e reconhecido por descontinuidade maior que UM BLOCO. Automacao
    // de andamento tambem faz o inicio do bloco nao bater exatamente com o fim
    // do anterior, e uma tolerancia menor viraria um corte audivel a cada
    // mudanca de BPM.
    // DESLIGAR UMA CAMADA CALA ELA AGORA.
    //
    // O note-off ja sai sempre, mas ele sai na hora em que a nota acabaria --
    // e uma nota longa podia levar meio laco para isso. Botao de silenciar que
    // demora doze compassos para agir nao parece silenciar, parece quebrado.
    const bool camadaAgora[3] =
    {
        *apvts.getRawParameterValue (pid::melodyOn) > 0.5f,
        *apvts.getRawParameterValue (pid::chordsOn) > 0.5f,
        *apvts.getRawParameterValue (pid::bassOn)   > 0.5f
    };

    bool camadaMudou = false;

    for (int i = 0; i < 3; ++i)
    {
        if (camadaAgora[i] != layerWas[i])
            camadaMudou = true;

        layerWas[i] = camadaAgora[i];
    }

    const double blockBeats = to - from;
    const bool jumped = running && wasRunning && lastEnd >= 0.0
                          && std::abs (from - lastEnd) > std::abs (blockBeats) + 1.0e-9;

    const bool silence = panic.exchange (false, std::memory_order_acq_rel)
                           || running != wasRunning
                           || jumped
                           || camadaMudou;

    // Trocar de instrumento tambem e uma descontinuidade: as vozes do
    // sintetizador interno ficariam paradas, vivas e invisiveis, e voltariam a
    // soar no instante em que o usuario descarregasse o convidado.
    const bool guestNow = rack.hasGuest();

    if (guestNow != guestWasLoaded)
    {
        guestWasLoaded = guestNow;
        synth.allNotesOff();
    }

    if (silence)
    {
        synth.allNotesOff();

        for (int ch = 1; ch <= (int) melody::Layer::count; ++ch)
            midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
    }

    wasRunning = running;
    lastEnd = running ? to : -1.0;

    if (! running)
        lastHostEnd = -1.0;

    moving.store (running, std::memory_order_relaxed);
    fromHost.store (hostPlaying && hostBeat >= 0.0 && hostBpm > 0.0,
                    std::memory_order_relaxed);

    if (running)
    {
        const double beatsPerSample = (to - from) / juce::jmax (1, numSamples);

        emitBlock (midi, from, to, numSamples, beatsPerSample);

        double loopPos = std::fmod (from, p.loopBeats());
        if (loopPos < 0.0) loopPos += p.loopBeats();

        position.store (loopPos, std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------
    // Quem faz o som: o instrumento carregado, se houver; o sintetizador
    // interno, se nao. Nunca os dois -- ouvir a frase duas vezes com timbres
    // diferentes soa como defeito, e e a primeira coisa que alguem reportaria.
    //
    // A CONTAGEM DE CANAIS E CONFERIDA, e nao assumida. Na versao de efeito
    // MIDI nao existe barramento de saida, e `getWritePointer (0)` num buffer
    // de zero canais e leitura fora do lugar -- que num plugin quer dizer o
    // host caindo, nao uma mensagem de erro.
    if (buffer.getNumChannels() == 0)
        return;

    guestMidi.addEvents (midi, 0, numSamples, 0);

    if (rack.process (buffer, guestMidi, getPlayHead()))
        return;

    if (*apvts.getRawParameterValue (pid::internal) > 0.5f)
    {
        float* left = buffer.getWritePointer (0);
        float* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : left;

        synth.render (left, right, numSamples);
    }
}

//==============================================================================
juce::AudioProcessorEditor* MelodyProcessor::createEditor()
{
    return new MelodyEditor (*this);
}

//==============================================================================
void MelodyProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto tree = apvts.copyState();

    // A SEMENTE VAI JUNTO. Sem ela, reabrir o projeto devolveria os knobs no
    // lugar certo e OUTRA MELODIA -- e a musica ja estaria escrita em cima da
    // primeira.
    tree.setProperty ("seed", (juce::int64) seed, nullptr);

    if (editorW > 0 && editorH > 0)
    {
        tree.setProperty ("editorW", editorW, nullptr);
        tree.setProperty ("editorH", editorH, nullptr);
    }

    // O instrumento carregado e o estado DELE viajam junto com o projeto.
    juce::MemoryBlock rackBlob;
    rack.getState (rackBlob);

    if (rackBlob.getSize() > 0)
        tree.setProperty ("rack", rackBlob.toBase64Encoding(), nullptr);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, dest);
}

void MelodyProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);

    if (xml == nullptr)
        return;

    auto tree = juce::ValueTree::fromXml (*xml);

    if (! tree.isValid())
        return;

    // A SEMENTE VOLTA COMO FOI GRAVADA.
    //
    // Era `... | 1u`, herdado de quando o Rng precisava de semente impar. O Rng
    // mistura internamente ha muito tempo e nao precisa mais -- mas o OR ficou,
    // e transformava toda semente PAR na vizinha impar. Efeito: metade dos
    // projetos salvos reabria com outra melodia, por cima da musica ja escrita.
    // Era invisivel porque o unico caso que cobria isto sorteava 987654321, que
    // e impar.
    if (tree.hasProperty ("seed"))
        seed = (std::uint32_t) (juce::int64) tree.getProperty ("seed");

    if (tree.hasProperty ("editorW") && tree.hasProperty ("editorH"))
    {
        editorW = (int) tree.getProperty ("editorW");
        editorH = (int) tree.getProperty ("editorH");
    }

    if (tree.hasProperty ("rack"))
    {
        juce::MemoryBlock rackBlob;

        if (rackBlob.fromBase64Encoding (tree.getProperty ("rack").toString()))
            rack.setState (rackBlob.getData(), (int) rackBlob.getSize());
    }

    apvts.replaceState (tree);
    refresh();
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MelodyProcessor();
}
