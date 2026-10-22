#include "Parameters.h"

namespace sanctuary
{
using namespace juce;
using APF = AudioParameterFloat;
using APC = AudioParameterChoice;
using APB = AudioParameterBool;
using API = AudioParameterInt;

static const char* prefixes[] = { "sop", "alt", "ten", "bas" };
static const char* names[] = { "Soprano", "Alto", "Tenor", "Bass" };

const char* sectionPrefix (int s) { return prefixes[jlimit (0, 3, s)]; }
const char* sectionName (int s) { return names[jlimit (0, 3, s)]; }
String sectionParamID (int s, const char* suffix) { return String (sectionPrefix (s)) + suffix; }

static StringArray toStringArray (const char* const* items, int count)
{
    StringArray a;
    for (int i = 0; i < count; ++i)
        a.add (items[i]);
    return a;
}

static AudioParameterFloatAttributes percentAttrs()
{
    return AudioParameterFloatAttributes()
        .withLabel ("%")
        .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " %"; });
}

float panFromOffset (int section, float offset)
{
    return jlimit (-1.0f, 1.0f, sc::defaultSectionPlacement (section).pan + offset);
}

static AudioParameterFloatAttributes dbAttrs()
{
    return AudioParameterFloatAttributes()
        .withLabel ("dB")
        .withStringFromValueFunction ([] (float v, int) { return v <= -59.9f ? String ("-inf dB") : String (v, 1) + " dB"; });
}

AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto pid = [] (const char* id) { return ParameterID { id, 1 }; };

    // Perform -------------------------------------------------------------
    auto perform = std::make_unique<AudioProcessorParameterGroup> ("perform", "Perform", "|");
    perform->addChild (std::make_unique<APF> (pid (ParamIDs::volume), "Volume", NormalisableRange<float> (-60.0f, 12.0f, 0.1f, 2.2f), 0.0f, dbAttrs()));
    perform->addChild (std::make_unique<APF> (pid (ParamIDs::choirSize), "Choir Size", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 70.0f, percentAttrs()));
    perform->addChild (std::make_unique<APF> (pid (ParamIDs::space), "Space", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, percentAttrs()));
    perform->addChild (std::make_unique<APF> (pid (ParamIDs::reverb), "Reverb", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 65.0f, percentAttrs()));
    perform->addChild (std::make_unique<APF> (pid (ParamIDs::tone), "Tone", NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f, dbAttrs()));
    perform->addChild (std::make_unique<APF> (pid (ParamIDs::presence), "Presence", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 30.0f, percentAttrs()));
    perform->addChild (std::make_unique<APC> (pid (ParamIDs::harmonyMode), "Harmony Mode", toStringArray (sc::getHarmonyModeNames(), (int) sc::HarmonyMode::Count), 0));
    perform->addChild (std::make_unique<APC> (pid (ParamIDs::key), "Key", toStringArray (sc::getKeyNames(), 13), 0));
    perform->addChild (std::make_unique<APC> (pid (ParamIDs::scale), "Scale", toStringArray (sc::getScaleNames(), (int) sc::ScaleType::Count), 0));
    perform->addChild (std::make_unique<APC> (pid (ParamIDs::style), "Style", toStringArray (sc::getStyleNames(), (int) sc::Style::Count), 0));
    perform->addChild (std::make_unique<APC> (pid (ParamIDs::vocalType), "Vocal Type", toStringArray (sc::getVocalTypeNames(), (int) sc::VocalType::Count), 0));
    perform->addChild (std::make_unique<APB> (pid (ParamIDs::freeze), "Freeze", false));
    perform->addChild (std::make_unique<APC> (pid (ParamIDs::response), "Response", toStringArray (sc::getResponseNames(), 2), 0));
    perform->addChild (std::make_unique<APC> (pid (ParamIDs::ensemble), "Ensemble", toStringArray (sc::getEnsembleNames(), (int) sc::Ensemble::Count), 0));
    perform->addChild (std::make_unique<APC> (pid (ParamIDs::scene), "Scene", StringArray { "None", "Scene 1", "Scene 2", "Scene 3", "Scene 4", "Scene 5", "Scene 6", "Scene 7", "Scene 8" }, 0));

    const auto ranges = sc::defaultSectionRanges();
    auto noteAttrs = AudioParameterIntAttributes().withStringFromValueFunction ([] (int v, int) { return String (sc::getNoteName (v)); });
    for (int s = 0; s < sc::NumSections; ++s)
    {
        auto group = std::make_unique<AudioProcessorParameterGroup> (sectionPrefix (s), sectionName (s), "|");
        const String n = sectionName (s);
        group->addChild (std::make_unique<APF> (ParameterID { sectionParamID (s, "Level"), 1 }, n + " Level", NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.75f,
                                            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                            {
                                                const float g = (v / 0.75f) * (v / 0.75f);
                                                return g <= 0.0001f ? String ("-inf dB") : String (Decibels::gainToDecibels (g), 1) + " dB";
                                            })));
        const auto home = sc::defaultSectionPlacement (s);
        group->addChild (std::make_unique<APF> (ParameterID { sectionParamID (s, "Pan"), 1 }, n + " Position", NormalisableRange<float> (-1.0f, 1.0f, 0.01f), home.pan,
                                            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                            {
                                                const int pct = roundToInt (std::abs (v) * 100.0f);
                                                return pct == 0 ? String ("C") : (v < 0.0f ? "L" : "R") + String (pct);
                                            })));
        group->addChild (std::make_unique<APF> (ParameterID { sectionParamID (s, "Width"), 1 }, n + " Width", NormalisableRange<float> (0.0f, 100.0f, 1.0f), home.width * 100.0f, percentAttrs()));
        group->addChild (std::make_unique<APF> (ParameterID { sectionParamID (s, "Depth"), 1 }, n + " Depth", NormalisableRange<float> (0.0f, 100.0f, 1.0f), home.depth * 100.0f,
                                            AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                            {
                                                return v < 33.0f ? String ("Front") : (v < 67.0f ? String ("Middle") : String ("Back"));
                                            })));
        group->addChild (std::make_unique<APB> (ParameterID { sectionParamID (s, "Mute"), 1 }, n + " Mute", false));
        group->addChild (std::make_unique<APB> (ParameterID { sectionParamID (s, "Solo"), 1 }, n + " Solo", false));
        group->addChild (std::make_unique<API> (ParameterID { sectionParamID (s, "Low"), 1 }, n + " Range Low", 24, 96, ranges[(size_t) s].low, noteAttrs));
        group->addChild (std::make_unique<API> (ParameterID { sectionParamID (s, "High"), 1 }, n + " Range High", 24, 96, ranges[(size_t) s].high, noteAttrs));
        perform->addChild (std::move (group));
    }
    perform->addChild (std::make_unique<APF> (pid (ParamIDs::micClose), "Close Mics", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.2f), -60.0f, dbAttrs()));
    perform->addChild (std::make_unique<APF> (pid (ParamIDs::micTree), "Tree Mics", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.2f), 0.0f, dbAttrs()));
    perform->addChild (std::make_unique<APF> (pid (ParamIDs::micRoom), "Room Mics", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.2f), -60.0f, dbAttrs()));
    layout.add (std::move (perform));

    // Mix -----------------------------------------------------------------
    auto mix = std::make_unique<AudioProcessorParameterGroup> ("mix", "Mix", "|");
    mix->addChild (std::make_unique<APF> (pid (ParamIDs::leadLevel), "Lead Vocal", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.2f), 0.0f, dbAttrs()));
    mix->addChild (std::make_unique<APF> (pid (ParamIDs::choirLevel), "Choir Level", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.2f), 0.0f, dbAttrs()));
    mix->addChild (std::make_unique<APF> (pid (ParamIDs::humanize), "Humanize", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, percentAttrs()));
    mix->addChild (std::make_unique<APF> (pid (ParamIDs::vibrato), "Vibrato", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, percentAttrs()));
    mix->addChild (std::make_unique<APF> (pid (ParamIDs::glide), "Glide", NormalisableRange<float> (0.0f, 300.0f, 1.0f), 100.0f, percentAttrs()));
    mix->addChild (std::make_unique<APF> (pid (ParamIDs::formant), "Formant", NormalisableRange<float> (-100.0f, 100.0f, 1.0f), 0.0f,
                                          AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int)
                                          {
                                              return std::abs (v) < 0.5f ? String ("Natural") : (v > 0 ? "+" : "") + String (roundToInt (v));
                                          })));
    mix->addChild (std::make_unique<APF> (pid (ParamIDs::pitchCorrection), "Pitch Correction", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 60.0f, percentAttrs()));
    mix->addChild (std::make_unique<APF> (pid (ParamIDs::harmonySpeed), "Chord Speed", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 50.0f, percentAttrs()));
    mix->addChild (std::make_unique<APF> (pid (ParamIDs::breath), "Breath", NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, percentAttrs()));
    layout.add (std::move (mix));

    // FX ------------------------------------------------------------------
    auto fx = std::make_unique<AudioProcessorParameterGroup> ("fx", "FX", "|");
    fx->addChild (std::make_unique<APC> (pid (ParamIDs::venue), "Venue", toStringArray (sc::getVenueNames(), (int) sc::Venue::Count), 0));
    fx->addChild (std::make_unique<APF> (pid (ParamIDs::decay), "Decay", NormalisableRange<float> (30.0f, 250.0f, 1.0f), 100.0f, percentAttrs()));
    fx->addChild (std::make_unique<APF> (pid (ParamIDs::preDelay), "Pre-Delay", NormalisableRange<float> (0.0f, 150.0f, 0.1f), 30.0f,
                                         AudioParameterFloatAttributes().withLabel ("ms").withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })));
    fx->addChild (std::make_unique<APF> (pid (ParamIDs::brightness), "Brightness", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 50.0f, percentAttrs()));
    fx->addChild (std::make_unique<APF> (pid (ParamIDs::leadReverb), "Lead Reverb", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 15.0f, percentAttrs()));
    auto hz = AudioParameterFloatAttributes().withLabel ("Hz").withStringFromValueFunction ([] (float v, int)
    {
        return v >= 1000.0f ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz";
    });
    fx->addChild (std::make_unique<APF> (pid (ParamIDs::lowCut), "Low Cut", NormalisableRange<float> (20.0f, 400.0f, 1.0f, 0.5f), 70.0f, hz));
    fx->addChild (std::make_unique<APF> (pid (ParamIDs::highCut), "High Cut", NormalisableRange<float> (3000.0f, 20000.0f, 10.0f, 0.5f), 18000.0f, hz));
    fx->addChild (std::make_unique<APF> (pid (ParamIDs::warmth), "Warmth", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, percentAttrs()));
    fx->addChild (std::make_unique<APF> (pid (ParamIDs::glue), "Glue", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 25.0f, percentAttrs()));
    fx->addChild (std::make_unique<APC> (pid (ParamIDs::rhythmPattern), "Rhythm", toStringArray (sc::getRhythmPatternNames(), sc::kNumRhythmPatterns), 0));
    fx->addChild (std::make_unique<APC> (pid (ParamIDs::rhythmRate), "Rhythm Rate", toStringArray (sc::getRhythmRateNames(), 3), 1));
    fx->addChild (std::make_unique<APF> (pid (ParamIDs::rhythmLength), "Rhythm Length", NormalisableRange<float> (5.0f, 100.0f, 1.0f), 60.0f, percentAttrs()));
    fx->addChild (std::make_unique<APF> (pid (ParamIDs::rhythmDepth), "Rhythm Depth", NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, percentAttrs()));
    fx->addChild (std::make_unique<APC> (pid (ParamIDs::rhythmSections), "Rhythm Sections", toStringArray (sc::getRhythmSectionNames(), 3), 0));
    layout.add (std::move (fx));

    // Settings ------------------------------------------------------------
    auto settings = std::make_unique<AudioProcessorParameterGroup> ("settings", "Settings", "|");
    settings->addChild (std::make_unique<APB> (pid (ParamIDs::midiEnabled), "MIDI Control", true,
                                               AudioParameterBoolAttributes().withAutomatable (false)));
    settings->addChild (std::make_unique<APB> (pid (ParamIDs::midiParts), "MIDI Parts (Ch 1-4)", false,
                                               AudioParameterBoolAttributes().withAutomatable (false)));
    settings->addChild (std::make_unique<APB> (pid (ParamIDs::followBand), "Follow the Band", true,
                                               AudioParameterBoolAttributes().withAutomatable (false)));
    settings->addChild (std::make_unique<APF> (pid (ParamIDs::tuning), "Tuning (A4)", NormalisableRange<float> (415.0f, 466.0f, 0.1f), 440.0f,
                                               AudioParameterFloatAttributes().withAutomatable (false).withLabel ("Hz")
                                                   .withStringFromValueFunction ([] (float v, int) { return String (v, 1) + " Hz"; })));
    settings->addChild (std::make_unique<APC> (pid (ParamIDs::latencyMode), "Monitoring", StringArray { "Studio (aligned)", "Live (low latency)" }, 0,
                                               AudioParameterChoiceAttributes().withAutomatable (false)));
    layout.add (std::move (settings));

    return layout;
}

//==============================================================================
struct ParameterCache::Ptrs
{
    std::atomic<float>* volume, *choirSize, *space, *reverb, *tone, *presence, *harmonyMode, *key, *scale, *style, *vocalType, *freeze, *response;
    std::atomic<float>* leadLevel, *choirLevel, *humanize, *vibrato, *glide, *formant, *pitchCorrection, *breath, *harmonySpeed;
    std::atomic<float>* ensemble, *micClose, *micTree, *micRoom;
    std::atomic<float>* venue, *decay, *preDelay, *brightness, *leadReverb, *lowCut, *highCut, *warmth, *glue;
    std::atomic<float>* rhythmPattern, *rhythmRate, *rhythmLength, *rhythmDepth, *rhythmSections;
    std::atomic<float>* midiEnabled, *midiParts, *followBand, *tuning, *latencyMode;
    std::array<std::atomic<float>*, sc::NumSections> level, pan, width, depth, mute, solo, low, high;
};

ParameterCache::ParameterCache (AudioProcessorValueTreeState& state) : ptrs (std::make_shared<Ptrs>())
{
    auto get = [&state] (const String& id)
    {
        auto* p = state.getRawParameterValue (id);
        jassert (p != nullptr);
        return p;
    };
    auto& p = *ptrs;
    p.volume = get (ParamIDs::volume); p.choirSize = get (ParamIDs::choirSize); p.space = get (ParamIDs::space);
    p.reverb = get (ParamIDs::reverb); p.tone = get (ParamIDs::tone); p.presence = get (ParamIDs::presence);
    p.harmonyMode = get (ParamIDs::harmonyMode); p.key = get (ParamIDs::key); p.scale = get (ParamIDs::scale);
    p.style = get (ParamIDs::style); p.vocalType = get (ParamIDs::vocalType); p.freeze = get (ParamIDs::freeze);
    p.response = get (ParamIDs::response);
    p.ensemble = get (ParamIDs::ensemble);
    p.micClose = get (ParamIDs::micClose); p.micTree = get (ParamIDs::micTree); p.micRoom = get (ParamIDs::micRoom);
    p.leadLevel = get (ParamIDs::leadLevel); p.choirLevel = get (ParamIDs::choirLevel); p.humanize = get (ParamIDs::humanize);
    p.vibrato = get (ParamIDs::vibrato); p.glide = get (ParamIDs::glide); p.formant = get (ParamIDs::formant);
    p.pitchCorrection = get (ParamIDs::pitchCorrection); p.breath = get (ParamIDs::breath);
    p.harmonySpeed = get (ParamIDs::harmonySpeed);
    p.venue = get (ParamIDs::venue); p.decay = get (ParamIDs::decay); p.preDelay = get (ParamIDs::preDelay);
    p.brightness = get (ParamIDs::brightness); p.leadReverb = get (ParamIDs::leadReverb); p.lowCut = get (ParamIDs::lowCut);
    p.highCut = get (ParamIDs::highCut); p.warmth = get (ParamIDs::warmth); p.glue = get (ParamIDs::glue);
    p.rhythmPattern = get (ParamIDs::rhythmPattern); p.rhythmRate = get (ParamIDs::rhythmRate);
    p.rhythmLength = get (ParamIDs::rhythmLength); p.rhythmDepth = get (ParamIDs::rhythmDepth);
    p.rhythmSections = get (ParamIDs::rhythmSections);
    p.midiEnabled = get (ParamIDs::midiEnabled); p.midiParts = get (ParamIDs::midiParts); p.followBand = get (ParamIDs::followBand); p.tuning = get (ParamIDs::tuning);
    p.latencyMode = get (ParamIDs::latencyMode);
    for (int s = 0; s < sc::NumSections; ++s)
    {
        p.level[(size_t) s] = get (sectionParamID (s, "Level"));
        p.pan[(size_t) s] = get (sectionParamID (s, "Pan"));
        p.width[(size_t) s] = get (sectionParamID (s, "Width"));
        p.depth[(size_t) s] = get (sectionParamID (s, "Depth"));
        p.mute[(size_t) s] = get (sectionParamID (s, "Mute"));
        p.solo[(size_t) s] = get (sectionParamID (s, "Solo"));
        p.low[(size_t) s] = get (sectionParamID (s, "Low"));
        p.high[(size_t) s] = get (sectionParamID (s, "High"));
    }
}

void ParameterCache::fill (sc::EngineParams& e) const
{
    const auto& p = *ptrs;
    auto f = [] (std::atomic<float>* a) { return a->load (std::memory_order_relaxed); };
    auto i = [&f] (std::atomic<float>* a) { return (int) std::lround (f (a)); };

    e.volumeDb = f (p.volume);
    e.choirSize = f (p.choirSize);
    e.space = f (p.space);
    e.reverb = f (p.reverb);
    e.toneDb = f (p.tone);
    e.presence = f (p.presence);
    e.harmonyMode = (sc::HarmonyMode) jlimit (0, (int) sc::HarmonyMode::Count - 1, i (p.harmonyMode));
    e.key = jlimit (0, 12, i (p.key));
    e.scale = (sc::ScaleType) jlimit (0, (int) sc::ScaleType::Count - 1, i (p.scale));
    e.style = (sc::Style) jlimit (0, (int) sc::Style::Count - 1, i (p.style));
    e.vocalType = (sc::VocalType) jlimit (0, (int) sc::VocalType::Count - 1, i (p.vocalType));
    e.freeze = f (p.freeze) > 0.5f;
    e.response = jlimit (0, 1, i (p.response));
    e.ensemble = (sc::Ensemble) jlimit (0, (int) sc::Ensemble::Count - 1, i (p.ensemble));
    e.micCloseDb = f (p.micClose);
    e.micTreeDb = f (p.micTree);
    e.micRoomDb = f (p.micRoom);

    for (int s = 0; s < sc::NumSections; ++s)
    {
        auto& sp = e.sections[(size_t) s];
        sp.level = f (p.level[(size_t) s]);
        sp.pan = f (p.pan[(size_t) s]);
        sp.width = f (p.width[(size_t) s]) / 100.0f;
        sp.depth = f (p.depth[(size_t) s]) / 100.0f;
        sp.mute = f (p.mute[(size_t) s]) > 0.5f;
        sp.solo = f (p.solo[(size_t) s]) > 0.5f;
        int lo = i (p.low[(size_t) s]), hi = i (p.high[(size_t) s]);
        if (hi < lo + 5) hi = lo + 5;
        sp.range = { lo, hi };
    }

    e.leadDb = f (p.leadLevel);
    e.choirDb = f (p.choirLevel);
    e.humanize = f (p.humanize);
    e.vibrato = f (p.vibrato);
    e.glide = f (p.glide);
    e.formant = f (p.formant);
    e.pitchCorrection = f (p.pitchCorrection);
    e.harmonySpeed = f (p.harmonySpeed);
    e.breath = f (p.breath);

    e.venue = (sc::Venue) jlimit (0, (int) sc::Venue::Count - 1, i (p.venue));
    e.decay = f (p.decay);
    e.preDelayMs = f (p.preDelay);
    e.brightness = f (p.brightness);
    e.leadReverb = f (p.leadReverb);
    e.lowCutHz = f (p.lowCut);
    e.highCutHz = f (p.highCut);
    e.warmth = f (p.warmth);
    e.glue = f (p.glue);
    e.rhythmPattern = jlimit (0, sc::kNumRhythmPatterns - 1, i (p.rhythmPattern));
    e.rhythmRate = jlimit (0, 2, i (p.rhythmRate));
    e.rhythmLength = f (p.rhythmLength);
    e.rhythmDepth = f (p.rhythmDepth);
    e.rhythmSections = jlimit (0, 2, i (p.rhythmSections));

    e.midiEnabled = f (p.midiEnabled) > 0.5f;
    e.midiParts = f (p.midiParts) > 0.5f;
    e.followBand = f (p.followBand) > 0.5f;
    e.a4 = f (p.tuning);
    e.lowLatency = i (p.latencyMode) == 1;
}

bool affectsHarmony (const String& id)
{
    return id == ParamIDs::harmonyMode || id == ParamIDs::key || id == ParamIDs::scale || id == ParamIDs::style
        || id == ParamIDs::ensemble || id == ParamIDs::followBand || id == ParamIDs::vocalType || id == ParamIDs::choirSize || id == ParamIDs::tuning || id == ParamIDs::harmonySpeed
        || id.endsWith ("Low") || id.endsWith ("High");
}

bool affectsRender (const String& id)
{
    return affectsHarmony (id) || id == ParamIDs::response || id == ParamIDs::humanize || id == ParamIDs::vibrato || id == ParamIDs::glide
        || id == ParamIDs::formant || id == ParamIDs::pitchCorrection || id == ParamIDs::breath;
}
} // namespace sanctuary
