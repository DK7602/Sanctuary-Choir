#include "PluginEditor.h"
#include "SanctuaryAssets.h"

namespace sanctuary
{
using namespace juce;

static const char* mimeFor (const String& path)
{
    const auto ext = path.fromLastOccurrenceOf (".", false, false).toLowerCase();
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "css") return "text/css; charset=utf-8";
    if (ext == "js" || ext == "mjs") return "text/javascript; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "png") return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "webp") return "image/webp";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "woff2") return "font/woff2";
    if (ext == "woff") return "font/woff";
    if (ext == "ttf") return "font/ttf";
    if (ext == "otf") return "font/otf";
    return "application/octet-stream";
}

bool SanctuaryEditor::Browser::pageAboutToLoad (const String& url)
{
    // Single page app: never navigate away from the bundled UI.
    return url.startsWith (getResourceProviderRoot()) || url.startsWith ("about:");
}

SanctuaryEditor::SanctuaryEditor (SanctuaryProcessor& p) : AudioProcessorEditor (&p), processor (p)
{
    zipStream = std::make_unique<MemoryInputStream> (SanctuaryAssets::sanctuary_ui_zip, (size_t) SanctuaryAssets::sanctuary_ui_zipSize, false);
    zip = std::make_unique<ZipFile> (*zipStream);

    auto options = WebBrowserComponent::Options{}
                       .withBackend (WebBrowserComponent::Options::Backend::webview2)
                       .withWinWebView2Options (WebBrowserComponent::Options::WinWebView2{}
                                                    .withUserDataFolder (File::getSpecialLocation (File::tempDirectory).getChildFile ("SanctuaryChoirWebView"))
                                                    .withBackgroundColour (Colour (0xff0b0807))
                                                    .withStatusBarDisabled())
                       .withNativeIntegrationEnabled()
                       .withKeepPageLoadedWhenBrowserIsHidden()
                       .withResourceProvider ([this] (const String& url) { return getResource (url); })
                       .withEventListener ("ui", [this] (const var& v) { handleUiMessage (v); });

    web = std::make_unique<Browser> (options);
    addAndMakeVisible (*web);
    web->goToURL (WebBrowserComponent::getResourceProviderRoot());

    constrainer.setFixedAspectRatio ((double) designWidth / (double) designHeight);
    constrainer.setSizeLimits (designWidth / 2, designHeight / 2, designWidth * 3 / 2, designHeight * 3 / 2);
    setConstrainer (&constrainer);
    setResizable (true, true);

    int w = processor.editorWidth, h = processor.editorHeight;
    if (w < designWidth / 2 || h < designHeight / 2)
    {
        // Default to 75% of the design size, smaller on small screens.
        w = designWidth * 3 / 4;
        if (auto* display = Desktop::getInstance().getDisplays().getPrimaryDisplay())
            w = std::min (w, (int) (display->userArea.getWidth() * 0.85));
        h = roundToInt (w * (double) designHeight / designWidth);
    }
    setSize (w, h);
    startTimerHz (30);
}

SanctuaryEditor::~SanctuaryEditor()
{
    stopTimer();
    for (const auto& id : activeGestures)
        if (auto* param = processor.getState().getParameter (id))
            param->endChangeGesture();
}

void SanctuaryEditor::paint (Graphics& g)
{
    g.fillAll (Colour (0xff0b0807));
}

void SanctuaryEditor::resized()
{
    web->setBounds (getLocalBounds());
    processor.editorWidth = getWidth();
    processor.editorHeight = getHeight();
}

//==============================================================================
std::optional<WebBrowserComponent::Resource> SanctuaryEditor::getResource (const String& url)
{
    String path = url.upToFirstOccurrenceOf ("?", false, false);
    path = path == "/" || path.isEmpty() ? String ("index.html") : path.fromFirstOccurrenceOf ("/", false, false);
    path = URL::removeEscapeChars (path);

    if (zip != nullptr)
    {
        if (auto* entry = zip->getEntry (path))
        {
            std::unique_ptr<InputStream> stream (zip->createStreamForEntry (*entry));
            if (stream != nullptr)
            {
                std::vector<std::byte> data ((size_t) stream->getTotalLength());
                stream->read (data.data(), (int) data.size());
                return WebBrowserComponent::Resource { std::move (data), mimeFor (path) };
            }
        }
    }
    return std::nullopt;
}

//==============================================================================
var SanctuaryEditor::describeParameter (RangedAudioParameter& p) const
{
    auto o = std::make_unique<DynamicObject>();
    const auto range = p.getNormalisableRange();
    o->setProperty ("id", p.getParameterID());
    o->setProperty ("name", p.getName (64));
    o->setProperty ("value", p.convertFrom0to1 (p.getValue()));
    o->setProperty ("default", p.convertFrom0to1 (p.getDefaultValue()));
    o->setProperty ("min", range.start);
    o->setProperty ("max", range.end);
    o->setProperty ("interval", range.interval);
    o->setProperty ("skew", range.skew);
    o->setProperty ("label", p.getLabel());
    if (auto* choice = dynamic_cast<AudioParameterChoice*> (&p))
    {
        o->setProperty ("type", "choice");
        Array<var> items;
        for (const auto& c : choice->choices)
            items.add (c);
        o->setProperty ("choices", items);
    }
    else if (dynamic_cast<AudioParameterBool*> (&p) != nullptr)
        o->setProperty ("type", "bool");
    else if (dynamic_cast<AudioParameterInt*> (&p) != nullptr)
        o->setProperty ("type", "int");
    else
        o->setProperty ("type", "float");
    return var (o.release());
}

void SanctuaryEditor::sendInit()
{
    auto o = std::make_unique<DynamicObject>();
    Array<var> params;
    for (auto* p : processor.getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            params.add (describeParameter (*rp));
    o->setProperty ("params", params);
    o->setProperty ("tab", processor.uiTab);
    o->setProperty ("version", JucePlugin_VersionString);
    o->setProperty ("standalone", processor.isStandaloneApp());
    addLatencyInfo (*o);
    o->setProperty ("maxSingers", sc::kMaxSingers);
    web->emitEventIfBrowserIsVisible ("init", var (o.release()));
    processor.takeChangedParameters();
    lastStatusVersion = -1;
    sendStatus();
    if (processor.getTotalNumOutputChannels() < 2)
        toast ("This track is mono, so the choir is folded to mono. Put Sanctuary Choir on a stereo track "
               "(or an FX channel) to hear the full stage.", 9000);
}

void SanctuaryEditor::sendParams (const StringArray& ids)
{
    if (ids.isEmpty())
        return;
    auto o = std::make_unique<DynamicObject>();
    for (const auto& id : ids)
        if (auto* p = processor.getState().getParameter (id))
            o->setProperty (id, p->convertFrom0to1 (p->getValue()));
    web->emitEventIfBrowserIsVisible ("params", var (o.release()));
}

void SanctuaryEditor::sendMeters()
{
    const auto snap = processor.takeSnapshot();
    auto o = std::make_unique<DynamicObject>();
    Array<var> sections;
    for (int s = 0; s < sc::NumSections; ++s)
    {
        auto so = std::make_unique<DynamicObject>();
        so->setProperty ("peak", snap.sectionPeak[(size_t) s]);
        Array<var> notes;
        for (int k = 0; k < 2; ++k)
            if (snap.sectionNotes[(size_t) s][(size_t) k] >= 0)
                notes.add (snap.sectionNotes[(size_t) s][(size_t) k]);
        so->setProperty ("notes", notes);
        so->setProperty ("singers", snap.sectionSingers[(size_t) s]);
        sections.add (var (so.release()));
    }
    o->setProperty ("sections", sections);
    o->setProperty ("singers", snap.totalSingers);
    o->setProperty ("leadMidi", snap.leadMidi);
    o->setProperty ("leadVoiced", snap.leadVoiced);
    o->setProperty ("leadNote", snap.leadNote);
    o->setProperty ("keyTonic", snap.keyTonic);
    o->setProperty ("keyMinor", snap.keyMinor);
    o->setProperty ("keyConfidence", snap.keyConfidence);
    o->setProperty ("chord", String (snap.chordName));
    o->setProperty ("in", snap.inputPeak);
    o->setProperty ("out", snap.outputPeak);
    o->setProperty ("frozen", snap.frozen);
    // The band on the side-chain: connected, heard, and the chord the choir follows.
    o->setProperty ("sidechain", processor.sidechainConnected.load (std::memory_order_relaxed));
    o->setProperty ("bandHeard", snap.bandHeard);
    o->setProperty ("followingBand", snap.followingBand);
    o->setProperty ("bandChord", String (snap.bandChord));
    // Beat now, extrapolated from the last audio block, for singers that move with the song.
    // No audio for a moment (host stopped, plug-in bypassed or suspended) means no beat.
    double bpm = processor.songBpm.load (std::memory_order_acquire);
    const double sinceBlock = Time::getMillisecondCounterHiRes() - processor.songBeatTime.load (std::memory_order_relaxed);
    if (sinceBlock > 250.0)
        bpm = 0.0;
    o->setProperty ("bpm", bpm);
    if (bpm > 0.0)
        o->setProperty ("beat", processor.songBeat.load (std::memory_order_relaxed) + sinceBlock / 60000.0 * bpm);
    web->emitEventIfBrowserIsVisible ("meters", var (o.release()));
}

void SanctuaryEditor::sendStatus()
{
    auto o = std::make_unique<DynamicObject>();
    o->setProperty ("learn", processor.getLearn().getStatus());
    auto& presets = processor.getPresets();
    o->setProperty ("presets", presets.toVar());
    o->setProperty ("presetIndex", presets.getCurrentIndex());
    o->setProperty ("presetName", presets.getCurrentName());
    o->setProperty ("monoOutput", processor.getTotalNumOutputChannels() < 2);
    o->setProperty ("voices", processor.getVoices().getStatus());
    o->setProperty ("scenes", processor.getSceneSlots());
    addLatencyInfo (*o);
    web->emitEventIfBrowserIsVisible ("status", var (o.release()));
}

void SanctuaryEditor::addLatencyInfo (DynamicObject& o)
{
    const double rate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0;
    o.setProperty ("latencyMs", 1000.0 * processor.getAlignedLatency() / rate);
    o.setProperty ("liveChoirMs", 1000.0 * processor.getLiveChoirLatency() / rate);
    sentLatency = processor.getAlignedLatency();
    sentRate = processor.getSampleRate();
}

void SanctuaryEditor::toast (const String& text, int milliseconds)
{
    auto o = std::make_unique<DynamicObject>();
    o->setProperty ("text", text);
    if (milliseconds > 0)
        o->setProperty ("ms", milliseconds);
    web->emitEventIfBrowserIsVisible ("toast", var (o.release()));
}

void SanctuaryEditor::timerCallback()
{
    if (! pageReady)
        return;
    sendParams (processor.takeChangedParameters());
    sendMeters();
    const int v = processor.statusVersion.load();
    const bool latencyChanged = std::abs (processor.getSampleRate() - sentRate) > 0.5 || processor.getAlignedLatency() != sentLatency;
    if (v != lastStatusVersion || latencyChanged)
    {
        lastStatusVersion = v;
        sendStatus();
    }
}

//==============================================================================
void SanctuaryEditor::handleUiMessage (const var& message)
{
    const String type = message.getProperty ("type", "").toString();

    if (type == "ready")
    {
        pageReady = true;
        sendInit();
        return;
    }

    if (type == "set")
    {
        const String id = message.getProperty ("id", "").toString();
        if (auto* p = processor.getState().getParameter (id))
        {
            const float value = (float) (double) message.getProperty ("value", 0.0);
            const bool inGesture = activeGestures.count (id) > 0;
            if (! inGesture) p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (value));
            if (! inGesture) p->endChangeGesture();
        }
        return;
    }

    if (type == "gesture")
    {
        const String id = message.getProperty ("id", "").toString();
        const bool begin = (bool) message.getProperty ("begin", false);
        if (auto* p = processor.getState().getParameter (id))
        {
            if (begin && activeGestures.insert (id).second)
                p->beginChangeGesture();
            else if (! begin && activeGestures.erase (id) > 0)
                p->endChangeGesture();
        }
        return;
    }

    if (type == "cmd")
        handleCommand (message.getProperty ("name", "").toString(), message);
}

void SanctuaryEditor::handleCommand (const String& name, const var& message)
{
    auto& learn = processor.getLearn();
    auto& presets = processor.getPresets();

    if (name == "learn")
        learn.toggleLearn();
    else if (name == "clear")
        learn.clear();
    else if (name == "render")
    {
        if (learn.getPhase() == LearnSession::Phase::Rendering)
            learn.cancelWork();
        else
            learn.render();
    }
    else if (name == "preview")
        learn.togglePreview();
    else if (name == "load")
    {
        chooser = std::make_unique<FileChooser> ("Load a vocal take", File::getSpecialLocation (File::userMusicDirectory),
                                                 "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  const auto file = fc.getResult();
                                  if (file.existsAsFile())
                                  {
                                      processor.getLearn().loadFile (file);
                                      toast ("Loaded " + file.getFileName());
                                  }
                              });
    }
    else if (name == "voicesLoad")
    {
        chooser = std::make_unique<FileChooser> ("Choose the folder with your choir recordings", processor.getVoices().getFolder().exists()
                                                     ? processor.getVoices().getFolder() : File::getSpecialLocation (File::userMusicDirectory));
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [this] (const FileChooser& fc)
                              {
                                  const auto dir = fc.getResult();
                                  if (dir.isDirectory())
                                  {
                                      processor.getVoices().loadFolder (dir);
                                      toast ("Loading the recordings in " + dir.getFileName() + "...");
                                  }
                              });
    }
    else if (name == "voicesClear")
    {
        processor.getVoices().clear();
        toast ("Built-in (synthesised) voices");
    }
    else if (name == "voicesBundled")
    {
        const auto bundled = ChoirVoices::findBundledVoices();
        if (bundled.isDirectory())
            processor.getVoices().loadFolder (bundled);
        else
            toast ("Sanctuary Voices are not installed. Run the installer again and keep \"Sanctuary Voices\" ticked.");
    }
    else if (name == "export")
    {
        if (! (bool) learn.getStatus().getProperty ("hasStems", false))
        {
            toast ("Render first, then export the stems");
            return;
        }
        toast ("Exporting stems...");
        SafePointer<SanctuaryEditor> safe (this);
        learn.exportStems ([safe] (File folder)
        {
            folder.revealToUser();
            if (safe != nullptr)
                safe->toast ("Stems saved to " + folder.getFullPathName());
        });
    }
    else if (name == "presetLoad")
        presets.load ((int) message.getProperty ("index", 0));
    else if (name == "presetNext")
        presets.next();
    else if (name == "presetPrev")
        presets.previous();
    else if (name == "presetSave")
    {
        const String presetName = message.getProperty ("value", "").toString();
        toast (presets.saveUser (presetName) ? "Saved preset \"" + presetName + "\"" : String ("Could not save the preset"));
    }
    else if (name == "presetDelete")
        presets.deleteUser ((int) message.getProperty ("index", -1));
    else if (name == "presetFolder")
    {
        auto folder = PresetManager::getUserPresetFolder();
        folder.createDirectory();
        folder.revealToUser();
    }
    else if (name == "sceneStore" || name == "sceneRecall" || name == "sceneClear")
    {
        const int slot = (int) message.getProperty ("index", -1);
        if (name == "sceneStore")
        {
            processor.storeScene (slot);
            toast ("Scene " + String (slot + 1) + " stored");
        }
        else if (name == "sceneClear")
        {
            processor.clearScene (slot);
        }
        else
        {
            // Through the Scene parameter, so the host can record it as automation.
            if (auto* p = processor.getState().getParameter (ParamIDs::scene))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 ((float) (slot + 1)));
                p->endChangeGesture();
            }
            processor.recallScene (slot);
        }
    }
    else if (name == "tab")
        processor.uiTab = message.getProperty ("value", "perform").toString();
    else if (name == "resetParam")
    {
        if (auto* p = processor.getState().getParameter (message.getProperty ("id", "").toString()))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->getDefaultValue());
            p->endChangeGesture();
        }
    }

    ++processor.statusVersion;
}
} // namespace sanctuary
