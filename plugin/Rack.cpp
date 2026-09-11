/*
  ==============================================================================

    Rack.cpp

  ==============================================================================
*/

#include "plugin/Rack.h"

namespace
{

/** O buffer que o convidado precisa e o MAIOR entre o que ele le e o que ele
    escreve -- JUCE usa o mesmo buffer para os dois. */
/** Instrumento, sem instanciar nada.

    AU: o identificador do JUCE carrega o tipo de quatro letras -- `aumu` e
    music device. VST3: o bundle traz `moduleinfo.json` com as subcategorias
    declaradas, e uma busca por "Instrument" no texto responde sem parser. O
    arquivo e JSON com virgula sobrando no fim das listas, entao parser estrito
    falha nele -- e procurar a palavra nao falha.

    Quem nao declara nada entra na lista: esconder um plugin bom por falta de
    metadado e pior que mostrar um que nao serve. */
bool ehInstrumento (const juce::String& formato, const juce::String& id)
{
    if (formato.containsIgnoreCase ("AudioUnit"))
        return id.contains ("aumu");

    const juce::File bundle (id);
    const auto info = bundle.getChildFile ("Contents/Resources/moduleinfo.json");

    if (! info.existsAsFile())
        return true;

    return info.loadFileAsString().contains ("\"Instrument\"");
}

int channelsNeeded (const juce::AudioProcessor& p)
{
    return juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
}

} // namespace

//==============================================================================
Rack::Rack()
{
    // `addDefaultFormats` esta deletado no juce_audio_processors_headless do
    // JUCE 9: os formatos entram um a um.
    formats.addFormat (new juce::VST3PluginFormat());

   #if JUCE_PLUGINHOST_AU && JUCE_MAC
    formats.addFormat (new juce::AudioUnitPluginFormat());
   #endif
}

Rack::~Rack()
{
    unload();
}

//==============================================================================
void Rack::prepare (double sampleRate, int blockSize)
{
    sr = sampleRate > 0.0 ? sampleRate : 44100.0;
    block = juce::jmax (16, blockSize);

    const juce::ScopedLock sl (lock);

    if (guest != nullptr)
    {
        configure (*guest);
        scratch.setSize (channelsNeeded (*guest), block, false, true, true);
    }
}

void Rack::release()
{
    const juce::ScopedLock sl (lock);

    if (guest != nullptr)
        guest->releaseResources();
}

void Rack::configure (juce::AudioPluginInstance& inst) const
{
    inst.setRateAndBufferSizeDetails (sr, block);
    inst.prepareToPlay (sr, block);
}

//==============================================================================
bool Rack::process (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                    juce::AudioPlayHead* ph)
{
    // TENTA. Nao espera: a troca de instrumento acontece no outro thread e leva
    // o tempo que levar.
    const juce::ScopedTryLock sl (lock);

    if (! sl.isLocked() || guest == nullptr)
        return false;

    const int numSamples = buffer.getNumSamples();

    // O host prometeu blocos de ate `block` no preparo. Se ele quebrar a
    // promessa, sair em silencio e melhor que escrever fora do buffer.
    if (numSamples > scratch.getNumSamples() || scratch.getNumChannels() == 0)
        return false;

    // O convidado tambem quer saber onde o transporte esta: sem isto, um
    // arpejador ou um delay sincronizado dentro dele nao acompanha o projeto.
    if (guest->getPlayHead() != ph)
        guest->setPlayHead (ph);

    scratch.clear (0, numSamples);

    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                   scratch.getNumChannels(), numSamples);

    guest->processBlock (view, midi);

    const int copies = juce::jmin (buffer.getNumChannels(), view.getNumChannels());

    for (int ch = 0; ch < copies; ++ch)
        buffer.copyFrom (ch, 0, view, ch, 0, numSamples);

    // Convidado mono numa faixa estereo: o mesmo sinal nos dois lados, em vez
    // de som so na esquerda.
    for (int ch = copies; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, view, juce::jmax (0, copies - 1), 0, numSamples);

    return true;
}

void Rack::renderBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const juce::ScopedLock sl (lock);

    if (guest == nullptr)
        return;

    const int numSamples = buffer.getNumSamples();

    if (numSamples > scratch.getNumSamples() || scratch.getNumChannels() == 0)
        return;

    scratch.clear (0, numSamples);

    juce::AudioBuffer<float> view (scratch.getArrayOfWritePointers(),
                                   scratch.getNumChannels(), numSamples);

    guest->processBlock (view, midi);

    const int copies = juce::jmin (buffer.getNumChannels(), view.getNumChannels());

    for (int ch = 0; ch < copies; ++ch)
        buffer.copyFrom (ch, 0, view, ch, 0, numSamples);

    for (int ch = copies; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom (ch, 0, view, juce::jmax (0, copies - 1), 0, numSamples);
}

void Rack::silenceGuest()
{
    const juce::ScopedLock sl (lock);

    if (guest == nullptr)
        return;

    juce::MidiBuffer calar;

    for (int ch = 1; ch <= 16; ++ch)
        calar.addEvent (juce::MidiMessage::allNotesOff (ch), 0);

    juce::AudioBuffer<float> lixo (juce::jmax (2, scratch.getNumChannels()), 64);
    lixo.clear();
    guest->processBlock (lixo, calar);
}

//==============================================================================
bool Rack::load (const juce::String& id, juce::String& error)
{
    error.clear();

    if (id.isEmpty())
    {
        error = "identificador vazio";
        return false;
    }

    juce::OwnedArray<juce::PluginDescription> found;

    for (int i = 0; i < formats.getNumFormats(); ++i)
    {
        auto* format = formats.getFormat (i);

        if (format->fileMightContainThisPluginType (id))
            format->findAllTypesForFile (found, id);
    }

    if (found.isEmpty())
    {
        error = "nenhum plugin em " + id;
        return false;
    }

    // O primeiro que for INSTRUMENTO. Um bundle pode trazer mais de um tipo, e
    // carregar o efeito de audio de um pacote quando se pediu o instrumento
    // daria um slot que nunca soa e nenhuma pista do motivo.
    const juce::PluginDescription* wanted = found.getFirst();

    for (const auto* d : found)
        if (d->isInstrument)
        {
            wanted = d;
            break;
        }

    auto inst = formats.createPluginInstance (*wanted, sr, block, error);

    if (inst == nullptr)
    {
        if (error.isEmpty())
            error = "nao consegui instanciar " + wanted->name;

        return false;
    }

    configure (*inst);

    juce::AudioBuffer<float> fresh (channelsNeeded (*inst), block);

    // A troca em si e curta de proposito: tudo o que e caro -- instanciar,
    // preparar, dimensionar -- ja aconteceu fora do lock.
    {
        const juce::ScopedLock sl (lock);

        guest = std::move (inst);
        scratch = std::move (fresh);
        identifier = id;
    }

    present.store (true, std::memory_order_relaxed);
    churn.fetch_add (1, std::memory_order_release);
    return true;
}

void Rack::unload()
{
    std::unique_ptr<juce::AudioPluginInstance> dying;

    present.store (false, std::memory_order_relaxed);
    churn.fetch_add (1, std::memory_order_release);

    {
        const juce::ScopedLock sl (lock);

        dying = std::move (guest);
        identifier.clear();
    }

    // O destrutor roda FORA do lock: destruir um plugin pode abrir janela,
    // fechar arquivo e levar tempo, e nada disso pode acontecer com o thread de
    // audio esperando na porta.
    if (dying != nullptr)
        dying->releaseResources();
}

//==============================================================================
bool Rack::loaded() const
{
    const juce::ScopedLock sl (lock);
    return guest != nullptr;
}

juce::String Rack::name() const
{
    const juce::ScopedLock sl (lock);
    return guest != nullptr ? guest->getName() : juce::String();
}

juce::String Rack::source() const
{
    const juce::ScopedLock sl (lock);
    return identifier;
}

juce::AudioProcessorEditor* Rack::createGuestEditor() const
{
    const juce::ScopedLock sl (lock);

    if (guest == nullptr || ! guest->hasEditor())
        return nullptr;

    return guest->createEditorIfNeeded();
}

//==============================================================================
juce::Array<Rack::Found> Rack::installed() const
{
    juce::Array<Found> out;

    for (int i = 0; i < formats.getNumFormats(); ++i)
    {
        auto* format = formats.getFormat (i);

        const auto ids = format->searchPathsForPlugins (
            format->getDefaultLocationsToSearch(), true, false);

        for (const auto& id : ids)
        {
            auto nome = format->getNameOfPluginFromIdentifier (id);

            // Para VST3 o identificador e o caminho do bundle, e o nome volta
            // como caminho; o nome util e o do arquivo.
            if (nome.contains ("/"))
                nome = juce::File (nome).getFileNameWithoutExtension();

            if (nome.isNotEmpty())
                out.add ({ format->getName(), nome, id,
                           ehInstrumento (format->getName(), id) });
        }
    }

    std::sort (out.begin(), out.end(), [] (const Found& a, const Found& b)
    {
        if (a.format != b.format)
            return a.format < b.format;

        return a.name.compareIgnoreCase (b.name) < 0;
    });

    return out;
}

//==============================================================================
void Rack::getState (juce::MemoryBlock& dest) const
{
    juce::ValueTree tree ("rack");

    const juce::ScopedLock sl (lock);

    if (guest != nullptr)
    {
        tree.setProperty ("id", identifier, nullptr);

        // O ESTADO DO CONVIDADO VAI JUNTO. Sem ele, reabrir o projeto devolveria
        // o instrumento certo no preset de fabrica -- e o som que a pessoa
        // ajustou some sem nada explicando.
        juce::MemoryBlock inner;
        guest->getStateInformation (inner);

        if (inner.getSize() > 0)
            tree.setProperty ("state", inner.toBase64Encoding(), nullptr);
    }

    juce::MemoryOutputStream out (dest, false);
    tree.writeToStream (out);
}

void Rack::setState (const void* data, int size)
{
    if (data == nullptr || size <= 0)
        return;

    juce::MemoryInputStream in (data, (std::size_t) size, false);
    const auto tree = juce::ValueTree::readFromStream (in);

    if (! tree.isValid() || ! tree.hasProperty ("id"))
    {
        unload();
        return;
    }

    const juce::String wanted = tree.getProperty ("id").toString();

    if (wanted == source() && loaded())
        return;                                  // ja e esse, nao recarrega

    juce::String error;

    if (! load (wanted, error))
        return;                                  // instrumento sumiu da maquina

    if (! tree.hasProperty ("state"))
        return;

    juce::MemoryBlock inner;

    if (! inner.fromBase64Encoding (tree.getProperty ("state").toString()))
        return;

    const juce::ScopedLock sl (lock);

    if (guest != nullptr)
        guest->setStateInformation (inner.getData(), (int) inner.getSize());
}
