#include "sanctuary/Harmonizer.h"

namespace sc
{
StyleProfile getStyleProfile (Style s)
{
    //                     vib  rate onset detune drift timing glide scoop  att    rel   breath soft
    switch (s)
    {
        case Style::Gospel:    return { 40.0f, 5.8f, 0.25f, 8.0f, 6.0f, 14.0f, 55.0f, 70.0f, 0.05f, 0.30f, 0.20f, 0.25f };
        case Style::Gregorian: return {  4.0f, 4.8f, 0.60f, 4.0f, 4.0f, 28.0f, 110.0f, 0.0f, 0.14f, 0.60f, 0.35f, 0.70f };
        case Style::Cinematic: return { 28.0f, 5.4f, 0.40f, 7.0f, 6.0f, 30.0f, 80.0f, 0.0f, 0.12f, 0.70f, 0.20f, 0.35f };
        case Style::Worship:   return { 25.0f, 5.5f, 0.30f, 6.0f, 5.0f, 18.0f, 60.0f, 25.0f, 0.07f, 0.40f, 0.30f, 0.45f };
        case Style::Chamber:   return { 14.0f, 5.6f, 0.40f, 3.0f, 3.0f, 9.0f, 50.0f, 0.0f, 0.07f, 0.35f, 0.20f, 0.50f };
        case Style::Spiritual: return { 32.0f, 5.4f, 0.30f, 7.0f, 6.0f, 20.0f, 75.0f, 40.0f, 0.08f, 0.50f, 0.25f, 0.45f };
        case Style::Sacred:
        default:               return { 22.0f, 5.3f, 0.35f, 6.0f, 5.0f, 22.0f, 70.0f, 0.0f, 0.09f, 0.45f, 0.25f, 0.55f };
    }
}

//==============================================================================
void LiveHarmonizer::reset()
{
    leadNote = candidate = -1;
    candidateTime = outOfChordTime = 0.0f;
    silence = 10.0f;
    sopShift = 0;
    haveChord = false;
    voicing = Voicing{};
    ++voicingId;
    lastMidi.clear();
}

void LiveHarmonizer::ensureVocabulary (const HarmonyContext& ctx)
{
    if (vocabTonic == ctx.scale.tonic && vocabScale == ctx.scale.type && vocabStyle == ctx.style)
        return;
    vocab = buildChordVocabulary (ctx.scale, ctx.style);
    vocabTonic = ctx.scale.tonic;
    vocabScale = ctx.scale.type;
    vocabStyle = ctx.style;
    haveChord = false;
}

void LiveHarmonizer::process (const PitchFrame& f, const HarmonyContext& ctx, float dt)
{
    ensureVocabulary (ctx);
    if (ctx.freeze)
        return;

    bool changed = false, newPhrase = false;

    if (f.voiced)
    {
        silence = 0.0f;
        const int q = ctx.scale.quantise (f.midi);
        if (leadNote < 0)
        {
            leadNote = q;
            candidate = -1;
            candidateTime = 0.0f;
            changed = newPhrase = true;
        }
        else if (q != leadNote)
        {
            const float distance = std::abs (f.midi - (float) leadNote);
            if (q == candidate) candidateTime += dt;
            else { candidate = q; candidateTime = dt; }
            if ((distance > 0.7f && candidateTime >= 0.03f) || distance > 1.6f || candidateTime >= 0.09f)
            {
                leadNote = q;
                candidate = -1;
                candidateTime = 0.0f;
                changed = true;
            }
        }
        else
        {
            candidate = -1;
            candidateTime = 0.0f;
        }
    }
    else
    {
        silence += dt;
        if (silence > 0.35f && leadNote >= 0)
        {
            leadNote = -1; // phrase ended; keep the last voicing for the release tail
            outOfChordTime = 0.0f;
        }
    }

    // MIDI chord changes re-voice immediately (also when the singer is silent: instrument mode).
    const bool midiOn = ctx.midiControlsHarmony && ! ctx.midi.empty();
    if (! (ctx.midi == lastMidi))
    {
        lastMidi = ctx.midi;
        changed = true;
    }
    if (ctx.mode != lastMode || ctx.active != lastActive)
    {
        lastMode = ctx.mode;
        lastActive = ctx.active;
        changed = true;
    }

    if (leadNote >= 0 && haveChord && ! midiOn && ! chord.contains (leadNote))
        outOfChordTime += dt;
    else
        outOfChordTime = 0.0f;
    if (outOfChordTime > 0.25f)
        changed = true;

    if (changed && (leadNote >= 0 || midiOn || ctx.mode == HarmonyMode::Drone))
        choose (ctx, newPhrase);
}

void LiveHarmonizer::choose (const HarmonyContext& ctx, bool newPhrase)
{
    const bool midiOn = ctx.midiControlsHarmony && ! ctx.midi.empty();
    const bool minor = ctx.scale.isMinorFamily();

    if (leadNote >= 0)
    {
        const auto& sr = ctx.ranges[Soprano];
        const int placed = leadNote + sopShift;
        if (newPhrase || placed < sr.low - 3 || placed > sr.high + 3)
            sopShift = chooseOctaveShift (leadNote, sr);
    }

    Chord next = chord;
    if (midiOn)
    {
        next = chordFromPitchClasses (ctx.midi.notes.data(), ctx.midi.count);
    }
    else if (ctx.mode == HarmonyMode::Drone)
    {
        next = vocab.empty() ? chordFromPitchClasses (&ctx.scale.tonic, 1) : vocab.front();
    }
    else if (ctx.scale.type == ScaleType::Chromatic && leadNote >= 0)
    {
        const int tones[] = { leadNote - 7, leadNote - 3, leadNote };
        next = chordFromPitchClasses (tones, 3);
    }
    else if (leadNote >= 0)
    {
        const int pc = wrapPc (leadNote);
        float bestScore = -1.0e9f;
        const Chord* best = nullptr;
        for (const auto& c : vocab)
        {
            const int role = c.roleOf (pc);
            if (role < 0)
                continue;
            static const float roleBonus[] = { 0.15f, 0.1f, 0.05f, -0.05f };
            float score = c.prior + roleBonus[role];
            if (haveChord)
            {
                if (c.name == chord.name) score += newPhrase ? 0.1f : 0.45f;
                score += progressionBonus (chord, c, minor);
            }
            else if (c.degree == 0)
            {
                score += 0.3f; // start phrases on the tonic when it fits
            }
            if (score > bestScore) { bestScore = score; best = &c; }
        }

        if (best != nullptr)
            next = *best;
        else if (! haveChord || outOfChordTime > 0.25f)
        {
            // Chromatic note outside the key: harmonise it with a major chord on that note.
            const int tones[] = { leadNote, leadNote + 4, leadNote + 7 };
            next = chordFromPitchClasses (tones, 3);
        }
    }

    chord = next;
    haveChord = true;
    outOfChordTime = 0.0f;

    VoicingRequest req;
    req.mode = ctx.mode;
    req.style = ctx.style;
    req.scale = ctx.scale;
    req.chord = chord;
    req.melody = leadNote;
    req.sopranoOctaveShift = sopShift;
    req.ranges = ctx.ranges;
    req.active = ctx.active;
    req.previous = voicing.valid ? &voicing : nullptr;
    req.wordsMode = ctx.wordsMode;
    if (midiOn && ctx.midi.count >= 3)
    {
        req.numExplicit = ctx.midi.count;
        for (int i = 0; i < ctx.midi.count; ++i)
            req.explicitNotes[(size_t) i] = ctx.midi.notes[(size_t) i];
    }

    voicing = voiceChord (req);
    ++voicingId;
}

//==============================================================================
void KeyTracker::reset()
{
    hist.fill (0.0f);
    sinceEstimate = totalWeight = confidence = 0.0f;
    pending = -1;
    pendingCount = 0;
}

void KeyTracker::addNote (int midiNote, float seconds, float weight)
{
    const float w = seconds * weight;
    hist[(size_t) wrapPc (midiNote)] += w;
    totalWeight += w;
}

int KeyTracker::update (float seconds, bool minorFamily)
{
    // Slow forgetting so the key can follow a song that modulates.
    const float decay = std::exp (-seconds / 45.0f);
    for (auto& h : hist) h *= decay;
    totalWeight *= decay;

    sinceEstimate += seconds;
    if (sinceEstimate < 0.4f && minorFamily == lastFamilyMinor)
        return tonic;
    sinceEstimate = 0.0f;
    lastFamilyMinor = minorFamily;

    if (totalWeight < 0.25f)
        return tonic;

    const KeyEstimate est = estimateKey (hist, minorFamily, ! minorFamily);
    confidence = clampv (est.score, 0.0f, 1.0f) * clampv (totalWeight / 6.0f, 0.0f, 1.0f);
    if (est.tonic == tonic)
    {
        pending = -1;
        pendingCount = 0;
        return tonic;
    }
    if (totalWeight < 2.5f)
    {
        tonic = est.tonic; // early on, follow the evidence quickly
        return tonic;
    }
    if (est.tonic == pending) ++pendingCount;
    else { pending = est.tonic; pendingCount = 1; }
    if (pendingCount >= 4)
    {
        tonic = est.tonic;
        pending = -1;
        pendingCount = 0;
    }
    return tonic;
}
} // namespace sc
