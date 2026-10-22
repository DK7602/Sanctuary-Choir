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

    std::vector<PresetManager::Entry> factoryPresets()
    {
        using namespace ParamIDs;
        auto p = [] (const char* name, V values) { PresetManager::Entry e; e.name = name; e.values = std::move (values); return e; };
        return {
            p ("Heavenly Praise",   { { harmonyMode, Full }, { style, Sacred }, { vocalType, Ah }, { choirSize, 70 }, { space, 60 }, { reverb, 65 }, { presence, 30 }, { venue, Cathedral } }),
            p ("Cathedral Hymn",    { { harmonyMode, Full }, { style, Sacred }, { vocalType, Oh }, { choirSize, 90 }, { space, 75 }, { reverb, 78 }, { decay, 120 }, { presence, 18 }, { venue, Cathedral } }),
            p ("Sunday Gospel",     { { harmonyMode, GospelStack }, { style, Gospel }, { vocalType, Words }, { choirSize, 45 }, { space, 55 }, { reverb, 32 }, { presence, 48 }, { glue, 45 }, { venue, GospelChurch } }),
            p ("Gregorian Chant",   { { harmonyMode, Organum }, { style, Gregorian }, { vocalType, Words }, { choirSize, 30 }, { space, 70 }, { reverb, 82 }, { decay, 130 }, { presence, 15 }, { venue, Abbey } }),
            p ("Angelic Voices",    { { harmonyMode, Upper }, { style, Sacred }, { vocalType, Ah }, { choirSize, 60 }, { space, 70 }, { reverb, 80 }, { brightness, 70 }, { presence, 45 }, { venue, Cathedral } }),
            p ("Men of Praise",     { { harmonyMode, Lower }, { style, Spiritual }, { vocalType, Words }, { choirSize, 50 }, { space, 50 }, { reverb, 45 }, { presence, 30 }, { venue, Chapel } }),
            p ("Epic Cinematic",    { { harmonyMode, Open }, { style, Cinematic }, { vocalType, Ah }, { choirSize, 100 }, { space, 90 }, { reverb, 72 }, { decay, 140 }, { tone, -2 }, { warmth, 25 }, { glue, 45 }, { venue, Cathedral } }),
            p ("Worship Night",     { { harmonyMode, Full }, { style, Worship }, { vocalType, Words }, { choirSize, 35 }, { space, 55 }, { reverb, 45 }, { presence, 40 }, { venue, Hall } }),
            p ("Christmas Carols",  { { harmonyMode, Full }, { style, Sacred }, { vocalType, Words }, { choirSize, 40 }, { space, 50 }, { reverb, 55 }, { presence, 30 }, { venue, Chapel } }),
            p ("Chamber Choir",     { { harmonyMode, Full }, { style, Chamber }, { vocalType, Words }, { choirSize, 25 }, { space, 45 }, { reverb, 40 }, { humanize, 70 }, { venue, Hall } }),
            p ("Midnight Hum",      { { harmonyMode, Full }, { style, Sacred }, { vocalType, Mm }, { choirSize, 50 }, { space, 65 }, { reverb, 60 }, { presence, 10 }, { tone, -3 }, { venue, Abbey } }),
            p ("Spiritual Echoes",  { { harmonyMode, Thirds }, { style, Spiritual }, { vocalType, Oh }, { choirSize, 40 }, { space, 60 }, { reverb, 62 }, { venue, Abbey } }),
            p ("Unison Anthem",     { { harmonyMode, Unison }, { style, Cinematic }, { vocalType, Words }, { choirSize, 80 }, { space, 85 }, { reverb, 60 }, { glue, 40 }, { venue, Cathedral } }),
            p ("Heavenly Drone",    { { harmonyMode, Drone }, { style, Sacred }, { vocalType, Oo }, { choirSize, 55 }, { space, 80 }, { reverb, 85 }, { decay, 150 }, { venue, Cathedral } }),
            p ("Call & Response",   { { harmonyMode, GospelStack }, { style, Gospel }, { vocalType, Words }, { response, 1 }, { choirSize, 40 }, { space, 60 }, { reverb, 38 }, { presence, 45 }, { glue, 40 }, { venue, GospelChurch } }),
        };
    }

    bool isPresetParam (const String& id)
    {
        // Settings are global preferences, and the key belongs to the song: neither is part of a sound.
        return id != ParamIDs::midiEnabled && id != ParamIDs::tuning && id != ParamIDs::freeze
            && id != ParamIDs::latencyMode && id != ParamIDs::key && id != ParamIDs::scale;
    }
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
            if (auto* obj = json.getProperty ("params", var()).getDynamicObject())
                for (const auto& prop : obj->getProperties())
                    e.values.push_back ({ prop.name.toString(), (float) prop.value });
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
            if (! e.factory)
                continue; // user presets store every parameter
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
    root->setProperty ("version", 1);
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
        list.add (var (o.release()));
    }
    return list;
}
} // namespace sanctuary
