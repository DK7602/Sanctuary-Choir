#include "Presets.h"
#include "Parameters.h"

namespace sanctuary
{
using namespace juce;

namespace
{
    // Choice indices (see sc::getHarmonyModeNames / getStyleNames / getVocalTypeNames / getVenueNames).
    enum { Full = 0, GospelStack, Open, Unison, Thirds, Organum, Upper, Lower, Drone };
    enum { Sacred = 0, Gospel, Gregorian, Cinematic, Worship, Chamber, Spiritual };
    enum { Ah = 0, Oh, Oo, Ee, Eh, Mm, Words };
    enum { Cathedral = 0, Abbey, Chapel, Hall, GospelChurch, Studio };

    using V = std::vector<std::pair<String, float>>;

    enum { Choir = 0, Kids, Gang, Congregation };
    enum { Natural = 0, Breathy, Belted };   // sc::getSingingNames
    // Rhythm patterns (sc::getRhythmPatternNames).
    enum { Pulse4 = 1, Pulse8, Pulse16, Offbeats, Dotted, Gallop, Syncopated, Stutter, TranceGate, Heartbeat, ArpUp, ArpDown, ArpBounce, CallAnswer };

    std::vector<PresetManager::Entry> factoryPresets()
    {
        using namespace ParamIDs;
        const char* category = "";
        auto p = [&category] (const char* name, V values)
        {
            PresetManager::Entry e;
            e.name = name;
            e.category = category;
            e.values = std::move (values);
            return e;
        };
        std::vector<PresetManager::Entry> list;
        auto add = [&list] (std::initializer_list<PresetManager::Entry> entries) { list.insert (list.end(), entries.begin(), entries.end()); };

        category = "Sacred & Classical";
        add ({
            p ("Heavenly Praise",   { { harmonyMode, Full }, { style, Sacred }, { vocalType, Ah }, { choirSize, 70 }, { space, 60 }, { reverb, 65 }, { presence, 30 }, { venue, Cathedral } }),
            p ("Cathedral Hymn",    { { harmonyMode, Full }, { style, Sacred }, { vocalType, Oh }, { choirSize, 90 }, { space, 75 }, { reverb, 78 }, { decay, 120 }, { presence, 18 }, { venue, Cathedral } }),
            p ("Angelic Voices",    { { harmonyMode, Upper }, { style, Sacred }, { vocalType, Ah }, { choirSize, 60 }, { space, 70 }, { reverb, 80 }, { brightness, 70 }, { presence, 45 }, { venue, Cathedral } }),
            p ("Gregorian Chant",   { { harmonyMode, Organum }, { style, Gregorian }, { vocalType, Words }, { choirSize, 30 }, { space, 70 }, { reverb, 82 }, { decay, 130 }, { presence, 15 }, { venue, Abbey } }),
            p ("Christmas Carols",  { { harmonyMode, Full }, { style, Sacred }, { vocalType, Words }, { choirSize, 40 }, { space, 50 }, { reverb, 55 }, { presence, 30 }, { venue, Chapel } }),
            p ("Chamber Choir",     { { harmonyMode, Full }, { style, Chamber }, { vocalType, Words }, { choirSize, 25 }, { space, 45 }, { reverb, 40 }, { humanize, 70 }, { venue, Hall } }),
            p ("Evensong",          { { harmonyMode, Full }, { style, Sacred }, { vocalType, Oh }, { choirSize, 40 }, { space, 65 }, { reverb, 75 }, { vibrato, 70 }, { presence, 22 }, { venue, Abbey } }),
            p ("Requiem",           { { harmonyMode, Full }, { style, Sacred }, { vocalType, Ah }, { choirSize, 85 }, { space, 80 }, { reverb, 80 }, { decay, 140 }, { tone, -3 }, { harmonySpeed, 30 }, { subOctave, 50 }, { venue, Cathedral } }),
            p ("Renaissance Motet", { { harmonyMode, Open }, { style, Chamber }, { vocalType, Oh }, { choirSize, 25 }, { space, 55 }, { reverb, 58 }, { humanize, 60 }, { vibrato, 40 }, { venue, Chapel } }),
            p ("Psalm Tone",        { { harmonyMode, Organum }, { style, Gregorian }, { vocalType, Ah }, { choirSize, 20 }, { space, 60 }, { reverb, 78 }, { decay, 120 }, { vibrato, 20 }, { venue, Abbey } }),
            // The stage and its microphones.
            p ("Intimate Chamber",  { { harmonyMode, Full }, { style, Chamber }, { vocalType, Words }, { choirSize, 25 }, { space, 45 }, { reverb, 40 }, { micClose, 0 }, { micTree, -8 }, { venue, Chapel } }),
            p ("Distant Cathedral", { { harmonyMode, Full }, { style, Sacred }, { vocalType, Oo }, { choirSize, 80 }, { space, 80 }, { reverb, 72 }, { micTree, -9 }, { micRoom, 0 }, { venue, Cathedral } }),
            p ("Choir in the Round", { { harmonyMode, Full }, { style, Sacred }, { vocalType, Ah }, { choirSize, 60 }, { space, 75 }, { reverb, 62 }, { micClose, -6 }, { micRoom, -6 }, { venue, Cathedral },
                                       { "sopPan", 0 }, { "altPan", 0 }, { "tenPan", 0 }, { "basPan", 0 },
                                       { "sopWidth", 95 }, { "altWidth", 95 }, { "tenWidth", 95 }, { "basWidth", 95 },
                                       { "sopDepth", 30 }, { "altDepth", 45 }, { "tenDepth", 60 }, { "basDepth", 75 } }),
        });

        category = "Worship";
        add ({
            p ("Worship Night",     { { harmonyMode, Full }, { style, Worship }, { vocalType, Words }, { choirSize, 35 }, { space, 55 }, { reverb, 45 }, { presence, 40 }, { venue, Hall } }),
            p ("Men of Praise",     { { harmonyMode, Lower }, { style, Spiritual }, { vocalType, Words }, { choirSize, 50 }, { space, 50 }, { reverb, 45 }, { presence, 30 }, { venue, Chapel } }),
            p ("Unison Anthem",     { { harmonyMode, Unison }, { style, Cinematic }, { vocalType, Words }, { choirSize, 80 }, { space, 85 }, { reverb, 60 }, { glue, 40 }, { venue, Cathedral } }),
            p ("Big Room Worship",  { { harmonyMode, Full }, { style, Worship }, { vocalType, Words }, { choirSize, 80 }, { space, 85 }, { reverb, 55 }, { glue, 45 }, { harmonySpeed, 40 }, { venue, Hall } }),
            p ("Altar Call",        { { harmonyMode, Thirds }, { style, Worship }, { vocalType, Words }, { choirSize, 25 }, { space, 45 }, { reverb, 40 }, { presence, 45 }, { humanize, 80 }, { venue, Chapel } }),
            p ("Prayer Room",       { { harmonyMode, Upper }, { style, Worship }, { vocalType, Mm }, { choirSize, 30 }, { space, 60 }, { reverb, 65 }, { presence, 15 }, { tone, -2 }, { energy, 30 }, { venue, Chapel } }),
            p ("Worship Pad",       { { harmonyMode, Full }, { style, Worship }, { vocalType, Oo }, { choirSize, 45 }, { space, 70 }, { reverb, 70 }, { presence, 25 }, { harmonySpeed, 30 }, { venue, Hall } }),
            p ("Bridge Swell",      { { harmonyMode, Open }, { style, Worship }, { vocalType, Oh }, { choirSize, 70 }, { space, 80 }, { reverb, 68 }, { harmonySpeed, 35 }, { warmth, 20 }, { glue, 40 }, { venue, Hall } }),
        });

        category = "Gospel";
        add ({
            p ("Sunday Gospel",     { { harmonyMode, GospelStack }, { style, Gospel }, { vocalType, Words }, { choirSize, 45 }, { space, 55 }, { reverb, 32 }, { presence, 48 }, { glue, 45 }, { venue, GospelChurch } }),
            p ("Call & Response",   { { harmonyMode, GospelStack }, { style, Gospel }, { vocalType, Words }, { response, 1 }, { choirSize, 40 }, { space, 60 }, { reverb, 38 }, { presence, 45 }, { glue, 40 }, { venue, GospelChurch } }),
            p ("Spiritual Echoes",  { { harmonyMode, Thirds }, { style, Spiritual }, { vocalType, Oh }, { choirSize, 40 }, { space, 60 }, { reverb, 62 }, { venue, Abbey } }),
            p ("Mass Choir",        { { harmonyMode, GospelStack }, { style, Gospel }, { vocalType, Words }, { choirSize, 100 }, { space, 70 }, { reverb, 40 }, { presence, 50 }, { glue, 50 }, { energy, 80 }, { venue, GospelChurch } }),
            p ("Praise Break",      { { harmonyMode, GospelStack }, { style, Gospel }, { vocalType, Words }, { choirSize, 60 }, { space, 55 }, { reverb, 30 }, { presence, 55 }, { glue, 55 }, { harmonySpeed, 70 }, { warmth, 15 }, { energy, 90 }, { venue, GospelChurch } }),
            p ("Gospel Quartet",    { { harmonyMode, Full }, { style, Gospel }, { vocalType, Words }, { choirSize, 10 }, { space, 30 }, { reverb, 25 }, { presence, 50 }, { venue, Studio } }),
            p ("Sweet Hour",        { { harmonyMode, Thirds }, { style, Gospel }, { vocalType, Oo }, { choirSize, 35 }, { space, 50 }, { reverb, 45 }, { warmth, 20 }, { singing, Breathy }, { venue, GospelChurch } }),
            p ("Lining Out",        { { harmonyMode, Unison }, { style, Spiritual }, { vocalType, Words }, { response, 1 }, { ensemble, Congregation }, { choirSize, 60 }, { space, 60 }, { reverb, 42 }, { venue, Chapel } }),
        });

        category = "Cinematic";
        add ({
            p ("Epic Cinematic",    { { harmonyMode, Open }, { style, Cinematic }, { vocalType, Ah }, { choirSize, 100 }, { space, 90 }, { reverb, 72 }, { decay, 140 }, { tone, -2 }, { warmth, 25 }, { glue, 45 }, { energy, 80 }, { subOctave, 60 }, { venue, Cathedral } }),
            p ("Dark Ritual",       { { harmonyMode, Lower }, { style, Cinematic }, { vocalType, Oh }, { choirSize, 80 }, { space, 80 }, { reverb, 75 }, { decay, 160 }, { tone, -5 }, { presence, 20 }, { warmth, 30 }, { venue, Cathedral } }),
            p ("Ethereal Rise",     { { harmonyMode, Upper }, { style, Cinematic }, { vocalType, Oo }, { choirSize, 50 }, { space, 90 }, { reverb, 85 }, { brightness, 75 }, { singing, Breathy }, { venue, Cathedral } }),
            p ("Battle Hymn",       { { harmonyMode, Open }, { style, Cinematic }, { vocalType, Words }, { choirSize, 100 }, { space, 85 }, { reverb, 60 }, { presence, 50 }, { warmth, 30 }, { glue, 55 }, { venue, Hall } }),
            p ("Heroes Unison",     { { harmonyMode, Unison }, { style, Cinematic }, { vocalType, Oh }, { choirSize, 90 }, { space, 80 }, { reverb, 65 }, { glue, 45 }, { singing, Belted }, { venue, Cathedral } }),
            p ("Mystery",           { { harmonyMode, Drone }, { style, Cinematic }, { vocalType, Mm }, { choirSize, 40 }, { space, 75 }, { reverb, 80 }, { decay, 150 }, { tone, -4 }, { venue, Abbey } }),
            p ("Trailer Hits",      { { harmonyMode, Unison }, { style, Cinematic }, { vocalType, Ah }, { choirSize, 100 }, { space, 90 }, { reverb, 70 }, { glue, 60 }, { rhythmPattern, Pulse4 }, { rhythmLength, 28 }, { singing, Belted }, { venue, Cathedral } }),
        });

        category = "Pop & Backing";
        add ({
            p ("Backing Stack",     { { harmonyMode, Thirds }, { style, Worship }, { vocalType, Words }, { choirSize, 12 }, { space, 40 }, { reverb, 20 }, { presence, 55 }, { humanize, 60 }, { venue, Studio } }),
            p ("Pop Doubles",       { { harmonyMode, Unison }, { style, Worship }, { vocalType, Words }, { choirSize, 10 }, { space, 50 }, { reverb, 15 }, { vibrato, 50 }, { venue, Studio } }),
            p ("Oohs & Aahs",       { { harmonyMode, Full }, { style, Worship }, { vocalType, Oo }, { choirSize, 20 }, { space, 45 }, { reverb, 30 }, { presence, 35 }, { singing, Breathy }, { venue, Studio } }),
            p ("Soul Harmonies",    { { harmonyMode, Thirds }, { style, Gospel }, { vocalType, Words }, { choirSize, 15 }, { space, 40 }, { reverb, 25 }, { presence, 50 }, { warmth, 20 }, { venue, Studio } }),
            p ("R&B Stack",         { { harmonyMode, GospelStack }, { style, Gospel }, { vocalType, Words }, { choirSize, 14 }, { space, 35 }, { reverb, 22 }, { presence, 52 }, { venue, Studio } }),
            p ("Wide Octaves",      { { harmonyMode, Unison }, { style, Cinematic }, { vocalType, Words }, { choirSize, 30 }, { space, 100 }, { reverb, 35 }, { presence, 40 }, { venue, Hall } }),
        });

        category = "Pads & Drones";
        add ({
            p ("Midnight Hum",      { { harmonyMode, Full }, { style, Sacred }, { vocalType, Mm }, { choirSize, 50 }, { space, 65 }, { reverb, 60 }, { presence, 10 }, { tone, -3 }, { venue, Abbey } }),
            p ("Heavenly Drone",    { { harmonyMode, Drone }, { style, Sacred }, { vocalType, Oo }, { choirSize, 55 }, { space, 80 }, { reverb, 85 }, { decay, 150 }, { singing, Breathy }, { venue, Cathedral } }),
            p ("Endless Ah",        { { harmonyMode, Full }, { style, Sacred }, { vocalType, Ah }, { choirSize, 70 }, { space, 80 }, { reverb, 85 }, { decay, 200 }, { glide, 200 }, { harmonySpeed, 10 }, { venue, Cathedral } }),
            p ("Choir Pad",         { { harmonyMode, Open }, { style, Cinematic }, { vocalType, Oo }, { choirSize, 60 }, { space, 85 }, { reverb, 80 }, { harmonySpeed, 15 }, { venue, Hall } }),
            p ("Warm Hum",          { { harmonyMode, Full }, { style, Worship }, { vocalType, Mm }, { choirSize, 50 }, { space, 60 }, { reverb, 60 }, { tone, -2 }, { warmth, 35 }, { venue, Chapel } }),
            p ("Shimmer Pad",       { { harmonyMode, Upper }, { style, Cinematic }, { vocalType, Ee }, { choirSize, 45 }, { space, 90 }, { reverb, 90 }, { decay, 180 }, { brightness, 85 }, { singing, Breathy }, { venue, Cathedral } }),
        });

        category = "Rhythmic";
        add ({
            p ("Pulse Choir",       { { harmonyMode, Full }, { style, Cinematic }, { vocalType, Ah }, { choirSize, 70 }, { space, 70 }, { reverb, 55 }, { rhythmPattern, Pulse8 }, { rhythmLength, 45 }, { venue, Hall } }),
            p ("Trance Gate",       { { harmonyMode, Full }, { style, Worship }, { vocalType, Ah }, { choirSize, 60 }, { space, 75 }, { reverb, 60 }, { rhythmPattern, TranceGate }, { rhythmLength, 70 }, { venue, Hall } }),
            p ("Choir Arpeggio",    { { harmonyMode, Full }, { style, Cinematic }, { vocalType, Oh }, { choirSize, 60 }, { space, 80 }, { reverb, 65 }, { rhythmPattern, ArpUp }, { rhythmLength, 85 }, { venue, Cathedral } }),
            p ("Bouncing Arp",      { { harmonyMode, Open }, { style, Worship }, { vocalType, Oo }, { choirSize, 50 }, { space, 80 }, { reverb, 60 }, { rhythmPattern, ArpBounce }, { rhythmLength, 90 }, { venue, Hall } }),
            p ("Stutter Gospel",    { { harmonyMode, GospelStack }, { style, Gospel }, { vocalType, Words }, { choirSize, 40 }, { space, 55 }, { reverb, 30 }, { presence, 50 }, { rhythmPattern, Stutter }, { rhythmLength, 60 }, { rhythmDepth, 90 }, { venue, GospelChurch } }),
            p ("Offbeat Choir",     { { harmonyMode, Thirds }, { style, Worship }, { vocalType, Ah }, { choirSize, 35 }, { space, 60 }, { reverb, 40 }, { rhythmPattern, Offbeats }, { rhythmLength, 60 }, { venue, Hall } }),
            p ("Heartbeat",         { { harmonyMode, Full }, { style, Cinematic }, { vocalType, Mm }, { choirSize, 60 }, { space, 70 }, { reverb, 65 }, { tone, -3 }, { rhythmPattern, Heartbeat }, { rhythmLength, 50 }, { venue, Cathedral } }),
            p ("Upper & Lower",     { { harmonyMode, Full }, { style, Gospel }, { vocalType, Words }, { choirSize, 50 }, { space, 60 }, { reverb, 35 }, { rhythmPattern, CallAnswer }, { rhythmRate, 0 }, { rhythmLength, 90 }, { venue, GospelChurch } }),
            p ("Galloping Choir",   { { harmonyMode, Open }, { style, Cinematic }, { vocalType, Ah }, { choirSize, 80 }, { space, 80 }, { reverb, 60 }, { glue, 50 }, { rhythmPattern, Gallop }, { rhythmLength, 55 }, { venue, Hall } }),
            p ("Dotted Echoes",     { { harmonyMode, Full }, { style, Worship }, { vocalType, Oh }, { choirSize, 45 }, { space, 70 }, { reverb, 62 }, { rhythmPattern, Dotted }, { rhythmLength, 70 }, { venue, Hall } }),
        });

        category = "Kids & Crowds";
        add ({
            p ("Children's Choir",  { { ensemble, Kids }, { harmonyMode, Full }, { style, Sacred }, { vocalType, Ah }, { choirSize, 50 }, { space, 65 }, { reverb, 65 }, { venue, Cathedral } }),
            p ("Kids Christmas",    { { ensemble, Kids }, { harmonyMode, Full }, { style, Sacred }, { vocalType, Words }, { choirSize, 40 }, { space, 55 }, { reverb, 55 }, { venue, Chapel } }),
            p ("School Assembly",   { { ensemble, Kids }, { harmonyMode, Unison }, { style, Worship }, { vocalType, Words }, { choirSize, 70 }, { space, 60 }, { reverb, 40 }, { venue, Hall } }),
            p ("Angel Children",    { { ensemble, Kids }, { harmonyMode, Full }, { style, Sacred }, { vocalType, Oo }, { choirSize, 45 }, { space, 75 }, { reverb, 80 }, { brightness, 70 }, { venue, Cathedral } }),
            p ("Gang Vocals",       { { ensemble, Gang }, { harmonyMode, Unison }, { style, Worship }, { vocalType, Words }, { choirSize, 40 }, { space, 60 }, { reverb, 20 }, { presence, 55 }, { glue, 50 }, { venue, Studio } }),
            p ("Stadium Chant",     { { ensemble, Gang }, { harmonyMode, Unison }, { style, Cinematic }, { vocalType, Words }, { choirSize, 100 }, { space, 100 }, { reverb, 45 }, { glue, 50 }, { venue, Hall } }),
            p ("Hey! Shouts",       { { ensemble, Gang }, { harmonyMode, Unison }, { style, Gospel }, { vocalType, Words }, { choirSize, 50 }, { space, 70 }, { reverb, 25 }, { presence, 55 }, { rhythmPattern, Pulse4 }, { rhythmLength, 25 }, { venue, Studio } }),
            p ("Congregation",      { { ensemble, Congregation }, { harmonyMode, Unison }, { style, Sacred }, { vocalType, Words }, { choirSize, 80 }, { space, 65 }, { reverb, 50 }, { venue, Chapel } }),
            p ("Church Sing-Along", { { ensemble, Congregation }, { harmonyMode, Full }, { style, Worship }, { vocalType, Words }, { choirSize, 70 }, { space, 60 }, { reverb, 45 }, { venue, Hall } }),
        });
        return list;
    }

    bool isPresetParam (const String& id)
    {
        // Settings are global preferences, and the key and your own voice belong to the song:
        // none of them is part of a sound.
        return id != ParamIDs::midiEnabled && id != ParamIDs::midiParts && id != ParamIDs::followBand && id != ParamIDs::tuning && id != ParamIDs::freeze
            && id != ParamIDs::latencyMode && id != ParamIDs::key && id != ParamIDs::scale && id != ParamIDs::scene
            && id != ParamIDs::leadOnStage && id != ParamIDs::leadPan && id != ParamIDs::leadDepth;
    }
}

bool PresetManager::isSoundParameter (const String& id)
{
    return isPresetParam (id);
}

PresetManager::PresetManager (AudioProcessorValueTreeState& s) : state (s)
{
    refresh();
    currentName = entries.empty() ? String() : entries.front().name;
}

File PresetManager::getUserPresetFolder()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory)
        .getChildFile ("Sanctuary Choir").getChildFile ("Presets");
}

void PresetManager::refresh()
{
    entries = factoryPresets();
    numFactory = (int) entries.size();

    auto folder = getUserPresetFolder();
    if (folder.isDirectory())
    {
        auto files = folder.findChildFiles (File::findFiles, false, "*.json");
        files.sort();
        for (const auto& f : files)
        {
            const var json = JSON::parse (f);
            if (! json.isObject())
                continue;
            Entry e;
            e.factory = false;
            e.file = f;
            e.name = json.getProperty ("name", f.getFileNameWithoutExtension()).toString();
            // Presets from before the stage stored each section's pan as an offset from its home.
            const bool panIsOffset = (int) json.getProperty ("version", 1) < kStageVersion;
            if (auto* obj = json.getProperty ("params", var()).getDynamicObject())
            {
                for (const auto& prop : obj->getProperties())
                {
                    float value = (float) prop.value;
                    for (int sec = 0; sec < sc::NumSections && panIsOffset; ++sec)
                        if (prop.name.toString() == sectionParamID (sec, "Pan"))
                            value = panFromOffset (sec, value);
                    e.values.push_back ({ prop.name.toString(), value });
                }
            }
            entries.push_back (std::move (e));
        }
    }
    setCurrentByName (currentName);
}

String PresetManager::getCurrentName() const { return currentName; }

void PresetManager::setCurrentByName (const String& name)
{
    currentName = name;
    current = -1;
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].name == name)
            current = i;
}

void PresetManager::load (int index)
{
    if (! isPositiveAndBelow (index, (int) entries.size()))
        return;
    const auto& e = entries[(size_t) index];

    // Start from defaults so a preset fully defines the sound.
    for (auto* p : state.processor.getParameters())
    {
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
        {
            if (! isPresetParam (rp->getParameterID()))
                continue;
            // User presets store every parameter they knew of; one added since starts at its default.
            const auto id = rp->getParameterID();
            if (! e.factory && std::any_of (e.values.begin(), e.values.end(), [&id] (const auto& v) { return v.first == id; }))
                continue;
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->getDefaultValue());
            rp->endChangeGesture();
        }
    }
    for (const auto& [id, value] : e.values)
    {
        if (! isPresetParam (id))
            continue;
        if (auto* rp = state.getParameter (id))
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->convertTo0to1 (value));
            rp->endChangeGesture();
        }
    }
    current = index;
    currentName = e.name;
}

void PresetManager::next()
{
    if (! entries.empty())
        load (current < 0 ? 0 : (current + 1) % (int) entries.size());
}

void PresetManager::previous()
{
    if (! entries.empty())
        load (current <= 0 ? (int) entries.size() - 1 : current - 1);
}

bool PresetManager::saveUser (const String& nameIn)
{
    const String name = nameIn.trim().substring (0, 64);
    if (name.isEmpty())
        return false;
    auto folder = getUserPresetFolder();
    if (! folder.createDirectory())
        return false;

    auto params = std::make_unique<DynamicObject>();
    for (auto* p : state.processor.getParameters())
        if (auto* rp = dynamic_cast<RangedAudioParameter*> (p))
            if (isPresetParam (rp->getParameterID()))
                params->setProperty (rp->getParameterID(), rp->convertFrom0to1 (rp->getValue()));

    auto root = std::make_unique<DynamicObject>();
    root->setProperty ("name", name);
    root->setProperty ("plugin", "Sanctuary Choir");
    root->setProperty ("version", kStageVersion);
    root->setProperty ("params", var (params.release()));

    const auto file = folder.getChildFile (File::createLegalFileName (name) + ".json");
    if (! file.replaceWithText (JSON::toString (var (root.release()))))
        return false;
    currentName = name;
    refresh();
    return true;
}

bool PresetManager::deleteUser (int index)
{
    if (! isPositiveAndBelow (index, (int) entries.size()) || entries[(size_t) index].factory)
        return false;
    const bool ok = entries[(size_t) index].file.deleteFile();
    refresh();
    return ok;
}

var PresetManager::toVar() const
{
    Array<var> list;
    for (const auto& e : entries)
    {
        auto o = std::make_unique<DynamicObject>();
        o->setProperty ("name", e.name);
        o->setProperty ("factory", e.factory);
        o->setProperty ("category", e.factory ? e.category : String ("My Presets"));
        list.add (var (o.release()));
    }
    return list;
}
} // namespace sanctuary
