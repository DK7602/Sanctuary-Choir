#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace sanctuary
{
using namespace juce;

static constexpr int kStateMagic = 0x31484353; // "SCH1"

SanctuaryProcessor::SanctuaryProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)),
      state (*this, nullptr, "SanctuaryChoir", createParameterLayout()),
      cache (state),
      presets (state)
{
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            paramIds.add (rp->getParameterID());
    dirty = std::vector<std::atomic<bool>> ((size_t) paramIds.size());
    for (const auto& id : paramIds)
        state.addParameterListener (id, this);

    standaloneApp = wrapperType == wrapperType_Standalone || JUCEApplicationBase::isStandaloneApp();
    learn.getParams = [this] { return currentParams(); };
    learn.standalone = standaloneApp;
    learn.onStatusChanged = [this] { ++statusVersion; };
    learn.getVoices = [this] { return voices.getCurrent(); };
    learn.voicesBusy = [this] { return voices.isBusy(); };
    voices.onStatusChanged = [this] { ++statusVersion; };
    voices.onSetChanged = [this] { learn.renderSettingsChanged(); };   // rendered takes used the old voices
    // The recorded Sanctuary Voices are the default when they are installed.
    if (const auto bundled = ChoirVoices::findBundledVoices(); bundled.isDirectory())
        voices.loadFolder (bundled);
    startTimerHz (10);
}

SanctuaryProcessor::~SanctuaryProcessor()
{
    stopTimer();
    for (const auto& id : paramIds)
        state.removeParameterListener (id, this);
}

//==============================================================================
bool SanctuaryProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    const auto in = layouts.getMainInputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo())
        return false;
    return in == AudioChannelSet::mono() || in == AudioChannelSet::stereo() || in.isDisabled();
}

void SanctuaryProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    setLatencySamples (reportedLatency());
    if (const int key = rememberedKey.load(); key >= 0)
        engine.seedKey (key % 12, key >= 12);
    bypassDelay.setSize (2, engine.getLatencySamples() + 1);
    bypassDelay.clear();
    bypassPos = 0;
    learn.prepare (sampleRate);
    engineWritten = 0;
    heldVelocity.fill (0.0f);
    sustained.fill (false);
    sustain = false;
}

void SanctuaryProcessor::updateMidi (const MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            heldVelocity[(size_t) m.getNoteNumber()] = m.getFloatVelocity();
            sustained[(size_t) m.getNoteNumber()] = false;
        }
        else if (m.isNoteOff())
        {
            if (sustain) sustained[(size_t) m.getNoteNumber()] = true;
            else heldVelocity[(size_t) m.getNoteNumber()] = 0.0f;
        }
        else if (m.isSustainPedalOn())
        {
            sustain = true;
        }
        else if (m.isSustainPedalOff())
        {
            sustain = false;
            for (int n = 0; n < 128; ++n)
                if (sustained[(size_t) n]) { sustained[(size_t) n] = false; heldVelocity[(size_t) n] = 0.0f; }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            heldVelocity.fill (0.0f);
            sustained.fill (false);
        }
    }
}

void SanctuaryProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (n == 0 || numOut == 0)
        return;

    updateMidi (midi);
    const sc::EngineParams p = currentParams();
    engine.setParams (p);

    sc::NoteSet held;
    float velocity = 0.0f;
    for (int note = 0; note < 128; ++note)
    {
        if (heldVelocity[(size_t) note] > 0.0f)
        {
            held.add (note);
            velocity = std::max (velocity, heldVelocity[(size_t) note]);
        }
    }
    engine.setMidi (held, velocity);

    LearnSession::BlockInfo info;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            info.hostPlaying = pos->getIsPlaying();
            if (auto t = pos->getTimeInSamples())
            {
                info.hasPosition = true;
                info.hostPosition = *t;
            }
            // The song's tempo and beat, so the singers can move with it.
            const auto bpm = pos->getBpm();
            const auto ppq = pos->getPpqPosition();
            if (info.hostPlaying && bpm.hasValue() && ppq.hasValue() && *bpm > 20.0 && *bpm < 400.0)
            {
                songBeat.store (*ppq, std::memory_order_relaxed);
                songBeatTime.store (Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
                songBpm.store (*bpm, std::memory_order_release);
            }
            else
            {
                songBpm.store (0.0, std::memory_order_relaxed);
            }
        }
    }
    info.engineWritten = engineWritten;
    info.latency = engine.getReportedLatency();   // what the host compensates (0 in Live mode)

    if (numIn == 0)
        buffer.clear();
    else if (numIn == 1 && numOut > 1)
        buffer.clear (1, 0, n);
    writeBypassDelay (buffer, n, numIn);   // so engaging bypass starts from the current input

    float* chans[2] = { buffer.getWritePointer (0), numIn > 1 ? buffer.getWritePointer (1) : nullptr };
    learn.processInput (chans, std::max (1, std::min (numIn, 2)), n, info);

    const sc::ScoreSource* score = nullptr;
    const sc::StemPlayback* stems = learn.preparePlayback (info, n, score);
    engine.setScore (score);
    engine.setSampleSet (voices.getLive());

    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : nullptr;
    float* outL = buffer.getWritePointer (0);
    float* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    engine.process (inL, inR, outL, outR, n, stems);
    engineWritten += n;

    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
}

void SanctuaryProcessor::writeBypassDelay (const AudioBuffer<float>& buffer, int n, int numIn)
{
    const int size = bypassDelay.getNumSamples();
    if (size <= 1)
        return;
    for (int ch = 0; ch < 2; ++ch)
    {
        const int src = std::min ({ ch, numIn - 1, buffer.getNumChannels() - 1 });
        const float* x = src >= 0 ? buffer.getReadPointer (src) : nullptr;
        float* ring = bypassDelay.getWritePointer (ch);
        int pos = bypassPos;
        for (int i = 0; i < n; ++i)
        {
            ring[pos] = x != nullptr ? x[i] : 0.0f;
            pos = pos + 1 < size ? pos + 1 : 0;
        }
    }
    bypassPos = (bypassPos + n) % size;
}

void SanctuaryProcessor::processBlockBypassed (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    // Keep following the keyboard, so notes released while bypassed don't hang on afterwards.
    updateMidi (midi);
    songBpm.store (0.0, std::memory_order_relaxed);

    const int n = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (numIn == 0)
        buffer.clear();
    else if (numIn == 1 && numOut > 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, n);

    const int size = bypassDelay.getNumSamples();
    const int lat = std::min (getLatencySamples(), size - 1);
    const int channels = std::min (numOut, 2);
    if (lat > 0)
    {
        for (int ch = 0; ch < channels; ++ch)
        {
            float* x = buffer.getWritePointer (ch);
            float* ring = bypassDelay.getWritePointer (ch);
            int pos = bypassPos;
            for (int i = 0; i < n; ++i)
            {
                const float in = x[i];
                x[i] = ring[(pos - lat + size) % size];
                ring[pos] = in;
                pos = (pos + 1) % size;
            }
        }
        bypassPos = (bypassPos + n) % size;
    }
    for (int ch = 2; ch < numOut; ++ch)
        buffer.clear (ch, 0, n);
}

//==============================================================================
sc::EngineParams SanctuaryProcessor::currentParams() const
{
    sc::EngineParams p;
    cache.fill (p);
    if (standaloneApp)
        p.lowLatency = true;    // nothing to compensate against: always monitor live
    return p;
}

int SanctuaryProcessor::reportedLatency() const
{
    return currentParams().lowLatency ? 0 : engine.getLatencySamples();
}

void SanctuaryProcessor::parameterChanged (const String& id, float)
{
    const int idx = paramIds.indexOf (id);
    if (idx >= 0)
        dirty[(size_t) idx].store (true, std::memory_order_relaxed);
    if (affectsHarmony (id))
        harmonyDirty = true;
    else if (affectsRender (id))
        renderDirty = true;
}

StringArray SanctuaryProcessor::takeChangedParameters()
{
    StringArray changed;
    for (size_t i = 0; i < dirty.size(); ++i)
        if (dirty[i].exchange (false, std::memory_order_relaxed))
            changed.add (paramIds[(int) i]);
    return changed;
}

void SanctuaryProcessor::timerCallback()
{
    const auto now = Time::getMillisecondCounter();
    if (harmonyDirty.exchange (false))
        lastHarmonyChange = now;
    if (lastHarmonyChange != 0 && now - lastHarmonyChange > 300)
    {
        lastHarmonyChange = 0;
        learn.harmonySettingsChanged();
    }
    if (renderDirty.exchange (false))
        learn.renderSettingsChanged();

    if (const int key = engine.getDetectedKey(); key >= 0)
        rememberedKey = key;

    // Studio <-> Live monitoring changes the latency the host has to compensate.
    if (const int lat = reportedLatency(); lat != getLatencySamples())
        setLatencySamples (lat);
}

//==============================================================================
int SanctuaryProcessor::getNumPrograms() { return std::max (1, (int) presets.getEntries().size()); }
int SanctuaryProcessor::getCurrentProgram() { return std::max (0, presets.getCurrentIndex()); }
void SanctuaryProcessor::setCurrentProgram (int index)
{
    presets.load (index);
    ++statusVersion;
}
const String SanctuaryProcessor::getProgramName (int index)
{
    const auto& e = presets.getEntries();
    return isPositiveAndBelow (index, (int) e.size()) ? e[(size_t) index].name : String();
}

//==============================================================================
void SanctuaryProcessor::getStateInformation (MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty ("preset", presets.getCurrentName(), nullptr);
    tree.setProperty ("uiTab", uiTab, nullptr);
    tree.setProperty ("editorWidth", editorWidth, nullptr);
    tree.setProperty ("editorHeight", editorHeight, nullptr);
    tree.setProperty ("detectedKey", rememberedKey.load(), nullptr);   // Auto key starts right next time
    tree.setProperty ("voicesFolder", voices.getFolder().getFullPathName(), nullptr);
    tree.setProperty ("voicesBuiltIn", voices.userChoseBuiltIn(), nullptr);
    std::unique_ptr<XmlElement> xml (tree.createXml());

    MemoryOutputStream out (destData, false);
    out.writeInt (kStateMagic);
    MemoryBlock xmlData;
    copyXmlToBinary (*xml, xmlData);
    out.writeInt64 ((int64) xmlData.getSize());
    out.write (xmlData.getData(), xmlData.getSize());
    learn.writeState (out);
}

void SanctuaryProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    MemoryInputStream in (data, (size_t) sizeInBytes, false);
    std::unique_ptr<XmlElement> xml;
    bool hasLearn = false;
    if (sizeInBytes >= 12 && in.readInt() == kStateMagic)
    {
        const int64 xmlSize = in.readInt64();
        if (xmlSize > 0 && xmlSize <= in.getNumBytesRemaining())
        {
            MemoryBlock xmlData;
            in.readIntoMemoryBlock (xmlData, (ssize_t) xmlSize);
            xml = getXmlFromBinary (xmlData.getData(), (int) xmlData.getSize());
            hasLearn = true;
        }
    }
    else
    {
        xml = getXmlFromBinary (data, sizeInBytes);
    }

    if (xml != nullptr && xml->hasTagName (state.state.getType()))
    {
        auto tree = ValueTree::fromXml (*xml);
        state.replaceState (tree);

        // replaceState skips parameters whose denormalised value looks unchanged, which can
        // leave e.g. a bool sitting at a raw 0.49. Push every stored value explicitly.
        for (auto* p : getParameters())
        {
            if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            {
                const auto stored = tree.getChildWithProperty ("id", rp->getParameterID()).getProperty ("value");
                if (stored.isVoid())
                    continue;
                const float normalised = rp->convertTo0to1 ((float) stored);
                if (std::abs (rp->getValue() - normalised) > 1.0e-6f)
                    rp->setValueNotifyingHost (normalised);
            }
        }
        presets.setCurrentByName (tree.getProperty ("preset").toString());
        uiTab = tree.getProperty ("uiTab", "perform").toString();
        editorWidth = tree.getProperty ("editorWidth", 0);
        editorHeight = tree.getProperty ("editorHeight", 0);
        // Choir voices: the user's own folder, the built-in synth on purpose, or (default) the
        // installed Sanctuary Voices that the constructor already started loading.
        const String voicesFolder = tree.getProperty ("voicesFolder", "").toString();
        if ((bool) tree.getProperty ("voicesBuiltIn", false))
        {
            if (! voices.userChoseBuiltIn())
                voices.clear();
        }
        else if (voicesFolder.isNotEmpty() && File::isAbsolutePath (voicesFolder) && File (voicesFolder).isDirectory())
        {
            if (File (voicesFolder) != voices.getFolder())
                voices.loadFolder (File (voicesFolder));
        }
        const int key = tree.getProperty ("detectedKey", -1);
        if (key >= 0 && key < 24)
        {
            rememberedKey = key;
            engine.seedKey (key % 12, key >= 12);
        }
    }
    if (hasLearn)
        learn.readState (in);
    ++statusVersion;
}

AudioProcessorEditor* SanctuaryProcessor::createEditor()
{
    return new SanctuaryEditor (*this);
}
} // namespace sanctuary

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new sanctuary::SanctuaryProcessor();
}
