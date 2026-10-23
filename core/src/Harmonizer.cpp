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

float passingNoteTime (float speed)
{
    speed = clampv (speed, 0.0f, 1.0f);
    return speed <= 0.5f ? 0.45f - 0.5f * speed           // 0.45 s .. 0.2 s
                         : 0.2f - 0.3f * (speed - 0.5f);  // 0.2 s .. 0.05 s
}

//==============================================================================
void LiveHarmonizer::reset()
{
    leadNote = candidate = -1;
    lastBand = -1;
    melodyCentre = -1.0f;
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
    buildChordVocabulary (ctx.scale, ctx.style, vocab);
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
    const int previousLead = leadNote;

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
            const float newDistance = std::abs (f.midi - (float) q);
            if (q == candidate) candidateTime += dt;
            else { candidate = q; candidateTime = dt; }

            // A pitch between two scale notes (an out-of-key note, or a wobble) must not
            // flip the melody back and forth: the new note has to be clearly closer.
            const bool clearlyCloser = newDistance + 0.3f < distance;
            // Sung pitch near no scale note at all: hold on longer before moving.
            const bool between = newDistance > 0.6f;
            // Jumps of an octave or so that last only a few frames are tracking errors.
            const bool bigJump = std::abs (q - leadNote) >= 8;
            const bool accept = bigJump ? candidateTime >= 0.06f
                              : distance > 1.6f ? candidateTime >= 0.01f
                              : between ? clearlyCloser && candidateTime >= 0.15f
                              : clearlyCloser && ((distance > 0.7f && candidateTime >= 0.03f) || candidateTime >= 0.09f);
            if (accept)
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

        if (leadNote >= 0)
            melodyCentre = melodyCentre < 0.0f ? (float) leadNote
                                               : melodyCentre + ((float) leadNote - melodyCentre) * (1.0f - std::exp (-dt / 4.0f));
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
    // The band's chord (side-chain) decides the harmony unless MIDI does. Only a different triad
    // or bass re-voices the choir: C and Cmaj7 are the same chord to sing.
    const bool bandOn = ! midiOn && ctx.band != nullptr;
    bool bandChanged = false;
    if (bandOn)
    {
        const Chord& b = *ctx.band;
        const int triad = b.root + 12 * wrapPc (b.tones[1] - b.root) + 144 * wrapPc (b.tones[2] - b.root) + 1728 * (ctx.bandBass + 1);
        if (triad != lastBand)
        {
            lastBand = triad;
            bandChanged = changed = true;
        }
    }
    else if (lastBand >= 0)
    {
        lastBand = -1;
        changed = true;
    }
    if (ctx.mode != lastMode || ctx.active != lastActive)
    {
        lastMode = ctx.mode;
        lastActive = ctx.active;
        changed = true;
    }

    if (leadNote >= 0 && haveChord && ! midiOn && ! bandOn && ! chord.contains (leadNote))
        outOfChordTime += dt;
    else
        outOfChordTime = 0.0f;
    const float holdTime = passingNoteTime (ctx.speed);
    if (outOfChordTime > holdTime)
        changed = true;

    if (changed && (leadNote >= 0 || midiOn || ctx.mode == HarmonyMode::Drone))
    {
        // A quick stepwise note outside the chord is a passing note: keep the chord.
        if (! newPhrase && ! midiOn && ! bandChanged && outOfChordTime <= holdTime && holdThroughPassingNote (ctx, previousLead))
            return;
        choose (ctx, newPhrase);
    }
}

bool LiveHarmonizer::holdThroughPassingNote (const HarmonyContext& ctx, int previousLead)
{
    if (! haveChord || ! voicing.valid || leadNote < 0 || previousLead < 0 || chord.contains (leadNote))
        return false;
    if (ctx.mode != HarmonyMode::FullChoir && ctx.mode != HarmonyMode::OpenVoicing)
        return false;
    // Slow harmony also lets small leaps (thirds) ride over the chord; the fastest setting
    // gives every note its own chord.
    const int maxStep = ctx.speed < 0.3f ? 4 : 2;
    if (ctx.speed >= 0.95f || std::abs (leadNote - previousLead) > maxStep || ctx.scale.type == ScaleType::Chromatic)
        return false;
    if (ctx.active[Soprano] && voicing.count[Soprano] > 0)
        voicing.set (Soprano, leadNote + sopShift);
    ++voicingId;
    return true;
}

void LiveHarmonizer::choose (const HarmonyContext& ctx, bool newPhrase)
{
    const bool midiOn = ctx.midiControlsHarmony && ! ctx.midi.empty();
    const bool minor = ctx.scale.isMinorFamily();

    if (leadNote >= 0)
    {
        // Place the doubled melody by where the tune sits overall (not by a pickup note), in the
        // comfortable part of the soprano range, and avoid two-octave shifts: in Words mode they
        // turn your voice into a cartoon.
        const auto& sr = ctx.ranges[Soprano];
        const SectionRange melodyRange { sr.low, std::max (sr.low + 7, sr.high - 6) };
        const float extraOctave = ctx.wordsMode ? 6.0f : 2.0f;
        const int centre = melodyCentre >= 0.0f ? (int) std::lround (melodyCentre) : leadNote;
        const int preferred = chooseOctaveShift (centre, melodyRange, extraOctave);
        // Scoops from below and short low notes are fine; only a melody that really leaves the
        // range moves the octave, and it comes back as soon as the melody does.
        auto fits = [&] (int shift) { const int n = leadNote + shift; return n >= sr.low - 7 && n <= sr.high + 3; };
        if (fits (preferred))
            sopShift = preferred;
        else if (newPhrase || ! fits (sopShift))
            sopShift = chooseOctaveShift (leadNote, melodyRange, extraOctave);
    }

    Chord next = chord;
    const bool bandOn = ! midiOn && ctx.band != nullptr;
    if (midiOn)
    {
        next = chordFromPitchClasses (ctx.midi.notes.data(), ctx.midi.count);
    }
    else if (bandOn)
    {
        next = *ctx.band;
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
                if (c.name == chord.name) score += newPhrase ? 0.1f : 0.45f * (1.6f - 1.2f * clampv (ctx.speed, 0.0f, 1.0f));
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
    if (bandOn)
        req.bassPc = ctx.bandBass;
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

void KeyTracker::seed (int tonicIn, bool minorIn, float weight)
{
    static const int majorSteps[] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minorSteps[] = { 0, 2, 3, 5, 7, 8, 10 };
    static const float stepWeight[] = { 1.6f, 0.8f, 1.2f, 0.9f, 1.4f, 0.8f, 0.6f };
    hist.fill (0.0f);
    for (int i = 0; i < 7; ++i)
        hist[(size_t) wrapPc (tonicIn + (minorIn ? minorSteps[i] : majorSteps[i]))] = stepWeight[i] * weight / 7.3f;
    totalWeight = weight;
    tonic = wrapPc (tonicIn);
    minor = minorIn;
    pending = -1;
    pendingCount = 0;
    sinceEstimate = 0.0f;
}

void KeyTracker::addNote (int midiNote, float seconds, float weight)
{
    const float w = seconds * weight;
    hist[(size_t) wrapPc (midiNote)] += w;
    totalWeight += w;
}

int KeyTracker::update (float seconds, KeyFamily family, int fixedTonic)
{
    // Slow forgetting so the key can follow a song that modulates.
    const float decay = std::exp (-seconds / 45.0f);
    for (auto& h : hist) h *= decay;
    totalWeight *= decay;

    // A family asked for by hand is the answer straight away.
    if (family != KeyFamily::Either)
        minor = family == KeyFamily::Minor;
    if (fixedTonic >= 0)
        tonic = wrapPc (fixedTonic);

    sinceEstimate += seconds;
    if (sinceEstimate < 0.4f && family == lastFamily && fixedTonic == lastFixed)
        return tonic;
    sinceEstimate = 0.0f;
    lastFamily = family;
    lastFixed = fixedTonic;

    if (totalWeight < 0.25f)
        return tonic;

    KeyEstimate est;
    if (fixedTonic >= 0)
    {
        // The key is set: only whether it is major or minor is open.
        const bool m = family == KeyFamily::Either ? keyFitness (hist, tonic, true) > keyFitness (hist, tonic, false)
                                                   : family == KeyFamily::Minor;
        est = { tonic, m, keyCorrelation (hist, tonic, m) };
    }
    else
    {
        est = estimateKey (hist, family == KeyFamily::Minor, family == KeyFamily::Major);
    }
    confidence = clampv (est.score, 0.0f, 1.0f) * clampv (totalWeight / 6.0f, 0.0f, 1.0f);
    // Keys counted 0..23: tonic, + 12 when minor.
    const int estKey = est.tonic + (est.minor ? 12 : 0);
    const int curKey = tonic + (minor ? 12 : 0);
    if (estKey == curKey)
    {
        pending = -1;
        pendingCount = 0;
        return tonic;
    }
    if (totalWeight < 2.5f)
    {
        tonic = est.tonic; // early on, follow the evidence quickly
        minor = est.minor;
        return tonic;
    }
    if (estKey == pending) ++pendingCount;
    else { pending = estKey; pendingCount = 1; }

    // Switch sooner when the current key leaves out notes the singer keeps using.
    float outside = 0.0f, total = 0.0f;
    const Scale current { tonic, minor ? ScaleType::HarmonicMinor : ScaleType::Major };
    for (int pc = 0; pc < 12; ++pc)
    {
        total += hist[(size_t) pc];
        if (! current.contains (pc) && ! (minor && pc == wrapPc (tonic + 10)))
            outside += hist[(size_t) pc];
    }
    const int needed = (total > 0.0f && outside / total > 0.15f) ? 2 : 4;
    if (pendingCount >= needed)
    {
        tonic = est.tonic;
        minor = est.minor;
        pending = -1;
        pendingCount = 0;
    }
    return tonic;
}
} // namespace sc
