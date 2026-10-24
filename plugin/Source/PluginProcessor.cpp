#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace sanctuary
{
using namespace juce;

static constexpr int kStateMagic = 0x31484353; // "SCH1"

SanctuaryProcessor::SanctuaryProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", AudioChannelSet::stereo(), true)
                          .withOutput ("Output", AudioChannelSet::stereo(), true)
                          .withInput ("Side-chain (band)", AudioChannelSet::stereo(), false)),
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
    learn.getHostBpm = [this] { return hostBpm.load (std::memory_order_relaxed); };
    learn.getMixSignature = [this]
    {
        // Every parameter's value: an exported mix is out of date as soon as any of them moves.
        uint64 h = 1469598103934665603ull;
        for (auto* p : getParameters())
        {
            const float v = p->getValue();
            uint32 bits = 0;
            std::memcpy (&bits, &v, sizeof (bits));
            h = (h ^ bits) * 1099511628211ull;
        }
        return String::toHexString ((int64) h);
    };
    rooms.getVenue = [this] { return currentParams().venue; };
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
    if (layouts.inputBuses.size() > 1)
    {
        // The side-chain (the band): off, mono or stereo.
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != AudioChannelSet::mono() && sc != AudioChannelSet::stereo())
            return false;
    }
    return in == AudioChannelSet::mono() || in == AudioChannelSet::stereo() || in.isDisabled();
}

void SanctuaryProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    rooms.prepare (sampleRate);   // a fresh room for this rate, ready for the first block
    setLatencySamples (reportedLatency());
    if (const int key = rememberedKey.load(); key >= 0)
        engine.seedKey (key % 12, key >= 12);
    bypassDelay.setSize (2, engine.getLatencySamples() + engine.getOutputDelay() + 1);
    bypassDelay.clear();
    sidechainCopy.setSize (2, std::max (samplesPerBlock * 2, 16384));
    sidechainCopy.clear();
    bypassPos = 0;
    learn.prepare (sampleRate);
    engineWritten = 0;
    for (auto& ch : heldVelocity) ch.fill (0.0f);
    for (auto& ch : sustained) ch.fill (false);
    sustain = false;
}

void SanctuaryProcessor::reset()
{
    // The host stopped processing (bypass, a track suspended while silent): start again clean,
    // rather than replaying the lead, room tail and look-ahead from before. The key heard so far
    // is kept.
    engine.reset();
    if (const int key = rememberedKey.load(); key >= 0)
        engine.seedKey (key % 12, key >= 12);
    bypassDelay.clear();
    bypassPos = 0;
}

void SanctuaryProcessor::updateMidi (const MidiBuffer& midi)
{
    noteOns.fill (0);
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        const int ch = m.getChannel() - 1;
        if (ch < 0 || ch > 15)
            continue;
        auto& held = heldVelocity[(size_t) ch];
        auto& sus = sustained[(size_t) ch];
        if (m.isNoteOn())
        {
            ++noteOns[(size_t) ch];
            held[(size_t) m.getNoteNumber()] = m.getFloatVelocity();
            sus[(size_t) m.getNoteNumber()] = false;
        }
        else if (m.isNoteOff())
        {
            if (sustain) sus[(size_t) m.getNoteNumber()] = true;
            else held[(size_t) m.getNoteNumber()] = 0.0f;
        }
        else if (m.isSustainPedalOn())
        {
            sustain = true;
        }
        else if (m.isSustainPedalOff())
        {
            sustain = false;
            for (size_t c = 0; c < 16; ++c)
                for (size_t n = 0; n < 128; ++n)
                    if (sustained[c][n]) { sustained[c][n] = false; heldVelocity[c][n] = 0.0f; }
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
        {
            held.fill (0.0f);
            sus.fill (false);
        }
        else if (m.isProgramChange())
        {
            pendingProgram.store (m.getProgramChangeNumber());
            triggerAsyncUpdate();
        }
    }
}

void SanctuaryProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numIn = getMainBusNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    if (n == 0 || numOut == 0)
    {
        updateMidi (midi);   // hosts can send events in an empty block: keep note-offs and pedals
        return;
    }

    // The band on the side-chain. Copied first: its channels share the buffer with the outputs.
    const float* scL = nullptr;
    const float* scR = nullptr;
    if (getBusCount (true) > 1)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0 && n <= sidechainCopy.getNumSamples())
        {
            for (int ch = 0; ch < std::min (2, sc.getNumChannels()); ++ch)
                sidechainCopy.copyFrom (ch, 0, sc, ch, 0, n);
            scL = sidechainCopy.getReadPointer (0);
            scR = sc.getNumChannels() > 1 ? sidechainCopy.getReadPointer (1) : nullptr;
        }
    }
    sidechainConnected.store (scL != nullptr, std::memory_order_relaxed);
    learn.bandConnected.store (scL != nullptr, std::memory_order_relaxed);
    engine.setSidechain (scL, scR);

    updateMidi (midi);
    const sc::EngineParams p = currentParams();
    engine.setParams (p);

    // Chords from any channel; in Parts mode channels 1-4 are the four sections instead.
    sc::NoteSet held;
    std::array<sc::NoteSet, sc::NumSections> parts;
    float velocity = 0.0f, partsVelocity = 0.0f;
    for (int note = 0; note < 128; ++note)
    {
        bool inChord = false;
        for (int ch = 0; ch < 16; ++ch)
        {
            const float v = heldVelocity[(size_t) ch][(size_t) note];
            if (v <= 0.0f)
                continue;
            if (p.midiParts && ch < sc::NumSections)
            {
                parts[(size_t) ch].add (note);
                partsVelocity = std::max (partsVelocity, v);
            }
            else
            {
                inChord = true;
                velocity = std::max (velocity, v);
            }
        }
        if (inChord)
            held.add (note);
    }
    // Typed words: each new note (chord) sings the next syllable; in Parts mode each part keeps
    // its own place in the words.
    engine.setLyrics (liveLyrics.load (std::memory_order_acquire));
    int chordOns = 0;
    std::array<int, sc::NumSections> partOns {};
    for (int ch = 0; ch < 16; ++ch)
    {
        if (p.midiParts && ch < sc::NumSections) partOns[(size_t) ch] = noteOns[(size_t) ch];
        else chordOns += noteOns[(size_t) ch];
    }
    engine.setMidi (held, velocity, chordOns);
    engine.setMidiParts (parts, partsVelocity, partOns);

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
            // The song's tempo and beat, so the singers (and the rhythm effects) move with it.
            const auto bpm = pos->getBpm();
            const auto ppq = pos->getPpqPosition();
            if (bpm.hasValue() && *bpm > 20.0 && *bpm < 400.0)
                hostBpm.store (*bpm, std::memory_order_relaxed);
            if (auto sig = pos->getTimeSignature())
            {
                info.sigNum = sig->numerator;
                info.sigDen = sig->denominator;
            }
            if (info.hostPlaying && bpm.hasValue() && ppq.hasValue() && *bpm > 20.0 && *bpm < 400.0)
            {
                info.bpm = *bpm;
                info.ppq = *ppq;
                songBeat.store (*ppq, std::memory_order_relaxed);
                songBeatTime.store (Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
                songBpm.store (*bpm, std::memory_order_release);
                engine.setTransport (true, *bpm, *ppq);
            }
            else
            {
                songBpm.store (0.0, std::memory_order_relaxed);
                engine.setTransport (false, bpm.hasValue() ? *bpm : 0.0, 0.0);
            }
        }
    }
    info.engineWritten = engineWritten;
    info.latency = engine.getReportedLatency();   // the timeline offset (0 in Live mode); the limiter's look-ahead delays everything alike
    info.sidechainL = scL;
    info.sidechainR = scR;

    if (numIn == 0)
        buffer.clear();
    else if (numIn == 1 && numOut > 1)
        buffer.clear (1, 0, n);
    writeBypassDelay (buffer, n, numIn);   // so engaging bypass starts from the current input

    float* chans[2] = { buffer.getWritePointer (0), numIn > 1 ? buffer.getWritePointer (1) : nullptr };
    learn.processInput (chans, std::max (1, std::min (numIn, 2)), n, info);

    const sc::ScoreSource* score = nullptr;
    const float* const* passes = nullptr;
    int numPasses = 0;
    const sc::StemPlayback* stems = learn.preparePlayback (info, n, score, passes, numPasses);
    engine.setScore (score);
    engine.setPasses (passes, numPasses);
    engine.setSampleSet (voices.getLive());
    engine.setRoom (rooms.getLive());

    const float* inL = buffer.getReadPointer (0);
    const float* inR = numIn > 1 ? buffer.getReadPointer (1) : nullptr;
    float* outL = buffer.getWritePointer (0);
    float* outR = numOut > 1 ? buffer.getWritePointer (1) : nullptr;
    engine.process (inL, inR, outL, outR, n, stems);
    engineWritten += n;
    const auto roomsHeld = engine.getRoomsInUse();
    rooms.setInUse (roomsHeld.first, roomsHeld.second);

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
    const int numIn = getMainBusNumInputChannels();
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
    return (currentParams().lowLatency ? 0 : engine.getLatencySamples()) + engine.getOutputDelay();
}

void SanctuaryProcessor::parameterChanged (const String& id, float)
{
    const int idx = paramIds.indexOf (id);
    if (idx >= 0)
        dirty[(size_t) idx].store (true, std::memory_order_relaxed);
    if (id == ParamIDs::scene)
    {
        sceneDirty = true;
        triggerAsyncUpdate();
    }
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

void SanctuaryProcessor::handleAsyncUpdate()
{
    // MIDI Program Change selects a preset.
    if (const int program = pendingProgram.exchange (-1); program >= 0 && program < (int) presets.getEntries().size())
    {
        presets.load (program);
        ++statusVersion;
    }
    // The Scene parameter moved (automation or the UI): bring that scene back.
    if (sceneDirty.exchange (false))
    {
        if (auto* p = state.getParameter (ParamIDs::scene))
        {
            const int slot = roundToInt (p->convertFrom0to1 (p->getValue())) - 1;
            if (slot >= 0)
                recallScene (slot);
        }
    }
}

void SanctuaryProcessor::storeScene (int slot)
{
    if (! isPositiveAndBelow (slot, kNumScenes))
        return;
    auto o = std::make_unique<DynamicObject>();
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (PresetManager::isSoundParameter (rp->getParameterID()))
                o->setProperty (rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()));
    scenes[(size_t) slot] = var (o.release());
    ++statusVersion;
}

void SanctuaryProcessor::recallScene (int slot)
{
    if (! isPositiveAndBelow (slot, kNumScenes))
        return;
    if (auto* obj = scenes[(size_t) slot].getDynamicObject())
    {
        for (const auto& prop : obj->getProperties())
        {
            auto* rp = state.getParameter (prop.name.toString());
            if (rp == nullptr || ! PresetManager::isSoundParameter (rp->getParameterID()))
                continue;
            const float v = rp->convertTo0to1 ((float) prop.value);
            if (std::abs (rp->getValue() - v) < 1.0e-6f)
                continue;
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (v);
            rp->endChangeGesture();
        }
    }
    ++statusVersion;
}

void SanctuaryProcessor::clearScene (int slot)
{
    if (! isPositiveAndBelow (slot, kNumScenes))
        return;
    scenes[(size_t) slot] = var();
    ++statusVersion;
}

var SanctuaryProcessor::getSceneSlots() const
{
    Array<var> slots;
    for (const auto& s : scenes)
        slots.add (s.isObject());
    return slots;
}

void SanctuaryProcessor::setLyrics (const String& text)
{
    const String t = text.substring (0, 2000);
    if (t == lyricsText && (lyricsScript != nullptr || t.trim().isEmpty()))
        return;
    lyricsText = t;
    lyricsParsed = sc::Lyrics::parse (t.toStdString());
    if (lyricsScript != nullptr)
        retiredScripts.push_back ({ lyricsScript, Time::getMillisecondCounter() });
    lyricsScript = lyricsParsed.empty() ? nullptr : std::make_shared<sc::LyricScript> (sc::LyricScript::make (lyricsParsed));
    liveLyrics.store (lyricsScript.get(), std::memory_order_release);
    ++statusVersion;
}

var SanctuaryProcessor::getLyricsStatus() const
{
    auto o = std::make_unique<DynamicObject>();
    o->setProperty ("text", lyricsText);
    Array<var> sung, starts;
    for (const auto& s : lyricsParsed.syllables())
    {
        sung.add (String (CharPointer_UTF8 (s.text.c_str())));
        starts.add (s.wordStart || s.hold);
    }
    o->setProperty ("sung", sung);
    o->setProperty ("starts", starts);
    o->setProperty ("latin", lyricsParsed.isLatin());
    return var (o.release());
}

void SanctuaryProcessor::timerCallback()
{
    const auto now = Time::getMillisecondCounter();
    // The audio thread picks up new words at its next block; free old ones well after that.
    retiredScripts.erase (std::remove_if (retiredScripts.begin(), retiredScripts.end(), [now] (const auto& r) { return now - r.second > 2000; }),
                          retiredScripts.end());
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
    // Some hosts change programs from the audio thread: hand it to the message thread.
    if (! MessageManager::existsAndIsCurrentThread())
    {
        pendingProgram.store (index);
        triggerAsyncUpdate();
        return;
    }
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
    tree.setProperty ("lyrics", lyricsText, nullptr);
    tree.setProperty ("stageVersion", kStageVersion, nullptr);
    {
        Array<var> sceneList;
        for (const auto& s : scenes)
            sceneList.add (s);
        tree.setProperty ("scenes", JSON::toString (var (sceneList), true), nullptr);
    }
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
        // Projects from before the stage stored each section's pan as an offset from its home.
        const bool panIsOffset = (int) tree.getProperty ("stageVersion", 1) < kStageVersion;
        if (panIsOffset)
        {
            for (int s = 0; s < sc::NumSections; ++s)
            {
                auto child = tree.getChildWithProperty ("id", sectionParamID (s, "Pan"));
                if (child.isValid())
                    child.setProperty ("value", panFromOffset (s, (float) child.getProperty ("value", 0.0f)), nullptr);
            }
            tree.setProperty ("stageVersion", kStageVersion, nullptr);
        }
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
        setLyrics (tree.getProperty ("lyrics", "").toString());
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
        // Scenes come back with the project (restoring the Scene parameter must not recall one
        // over the settings that were just restored).
        {
            const var sceneList = JSON::parse (tree.getProperty ("scenes", "").toString());
            for (int i = 0; i < kNumScenes; ++i)
            {
                scenes[(size_t) i] = sceneList.isArray() && i < sceneList.size() && sceneList[i].isObject() ? sceneList[i] : var();
                if (auto* obj = scenes[(size_t) i].getDynamicObject(); obj != nullptr && panIsOffset)
                    for (int s = 0; s < sc::NumSections; ++s)
                        if (const Identifier id (sectionParamID (s, "Pan")); obj->hasProperty (id))
                            obj->setProperty (id, panFromOffset (s, (float) obj->getProperty (id)));
            }
            sceneDirty = false;
            cancelPendingUpdate();
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
