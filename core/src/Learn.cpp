#include "sanctuary/Learn.h"

#include <algorithm>
#include <cstring>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <limits>
#include <thread>

namespace sc
{
static bool cancelled (const std::atomic<bool>* c) { return c != nullptr && c->load(); }

//==============================================================================
TakeAnalysis analyseTake (const float* x, int64_t n, double sampleRate, float a4, const std::atomic<bool>* cancel)
{
    TakeAnalysis t;
    t.sampleRate = sampleRate;
    t.length = n;
    if (n <= 0)
        return t;

    PitchTracker::Settings ps;
    ps.minHz = 70.0f;
    ps.maxHz = 1150.0f;
    t.frames = PitchTracker::analyse (x, n, sampleRate, ps);
    if (cancelled (cancel) || t.frames.empty())
        return t;

    const int numFrames = (int) t.frames.size();
    const double hop = t.frames.size() > 1 ? (double) (t.frames[1].centre - t.frames[0].centre) : sampleRate * 0.005;

    // Median-smoothed pitch (MIDI relative to a4) for voiced frames.
    std::vector<float> midi ((size_t) numFrames, 0.0f);
    for (int i = 0; i < numFrames; ++i)
        if (t.frames[(size_t) i].voiced)
            midi[(size_t) i] = hzToMidi (t.frames[(size_t) i].hz, a4);
    std::vector<float> smooth = midi;
    for (int i = 0; i < numFrames; ++i)
    {
        if (! t.frames[(size_t) i].voiced) continue;
        float w[5];
        int k = 0;
        for (int j = i - 2; j <= i + 2; ++j)
            if (j >= 0 && j < numFrames && t.frames[(size_t) j].voiced)
                w[k++] = midi[(size_t) j];
        std::sort (w, w + k);
        smooth[(size_t) i] = w[k / 2];
    }

    // Segment into notes: voiced runs, split where the pitch settles somewhere new.
    struct Seg { int a, b; };
    std::vector<Seg> segs;
    const int minSplitFrames = std::max (3, (int) std::lround (0.045 * sampleRate / hop));
    const int maxGapFrames = std::max (2, (int) std::lround (0.03 * sampleRate / hop));
    int i = 0;
    while (i < numFrames)
    {
        if (! t.frames[(size_t) i].voiced) { ++i; continue; }
        int start = i;
        float anchor = smooth[(size_t) i];
        int anchorCount = 1;
        int devStart = -1;
        int gap = 0;
        int j = i + 1;
        for (; j < numFrames; ++j)
        {
            if (! t.frames[(size_t) j].voiced)
            {
                if (++gap > maxGapFrames) break;
                continue;
            }
            gap = 0;
            const float m = smooth[(size_t) j];
            if (std::abs (m - anchor) > 0.75f)
            {
                if (devStart < 0) devStart = j;
                if (j - devStart + 1 >= minSplitFrames)
                {
                    segs.push_back ({ start, devStart - 1 });
                    start = devStart;
                    anchor = smooth[(size_t) devStart];
                    anchorCount = 1;
                    devStart = -1;
                }
            }
            else
            {
                devStart = -1;
                // Running anchor over the settled part of the note.
                if (anchorCount < 40)
                {
                    anchor += (m - anchor) / (float) (++anchorCount);
                }
            }
        }
        int end = j - 1 - gap;
        if (end >= start)
            segs.push_back ({ start, end });
        i = j;
    }

    // Build notes with median pitch; drop blips.
    const double minDur = 0.06 * sampleRate;
    for (const auto& s : segs)
    {
        std::vector<float> vals;
        for (int k = s.a; k <= s.b; ++k)
            if (t.frames[(size_t) k].voiced)
                vals.push_back (midi[(size_t) k]);
        if (vals.empty()) continue;
        std::nth_element (vals.begin(), vals.begin() + (long) vals.size() / 2, vals.end());
        LearnedNote note;
        note.start = std::max<int64_t> (0, t.frames[(size_t) s.a].centre - (int64_t) (hop * 0.5));
        note.end = std::min<int64_t> (n, t.frames[(size_t) s.b].centre + (int64_t) (hop * 0.5));
        note.pitch = vals[vals.size() / 2];
        if ((double) (note.end - note.start) < minDur)
        {
            // Merge very short segments into the previous note if they are adjacent and close in pitch.
            if (! t.notes.empty() && note.start - t.notes.back().end < (int64_t) (0.03 * sampleRate)
                && std::abs (note.pitch - t.notes.back().pitch) < 1.5f)
                t.notes.back().end = note.end;
            continue;
        }
        t.notes.push_back (note);
    }

    // Octave slips: a blip far from both neighbours is the tracker locking onto the wrong
    // octave (or vocal fry), not a sung leap. Fold it back if it fits, otherwise drop it.
    {
        std::vector<LearnedNote> kept;
        const double shortNote = 0.13 * sampleRate;
        for (size_t k = 0; k < t.notes.size(); ++k)
        {
            LearnedNote note = t.notes[k];
            const bool isShort = (double) (note.end - note.start) < shortNote;
            const LearnedNote* prev = kept.empty() ? nullptr : &kept.back();
            const LearnedNote* next = k + 1 < t.notes.size() ? &t.notes[k + 1] : nullptr;
            const int64_t near = (int64_t) (0.4 * sampleRate);
            if (prev != nullptr && note.start - prev->end > near) prev = nullptr;
            if (next != nullptr && next->start - note.end > near) next = nullptr;
            if (isShort && (prev != nullptr || next != nullptr))
            {
                const float ref = prev != nullptr && next != nullptr ? 0.5f * (prev->pitch + next->pitch)
                                                                     : (prev != nullptr ? prev->pitch : next->pitch);
                const bool farFromPrev = prev == nullptr || std::abs (note.pitch - prev->pitch) >= 7.0f;
                const bool farFromNext = next == nullptr || std::abs (note.pitch - next->pitch) >= 7.0f;
                if (farFromPrev && farFromNext)
                {
                    const float folded = note.pitch + 12.0f * std::round ((ref - note.pitch) / 12.0f);
                    if (std::abs (folded - ref) > 3.5f)
                        continue;
                    note.pitch = folded;
                }
            }
            kept.push_back (note);
        }
        t.notes = std::move (kept);
    }

    // Phrases: separated by gaps of more than 300 ms.
    const int64_t phraseGap = (int64_t) (0.3 * sampleRate);
    for (size_t k = 0; k < t.notes.size(); ++k)
    {
        if (k == 0 || t.notes[k].start - t.notes[k - 1].end > phraseGap)
        {
            LearnedPhrase p;
            p.start = t.notes[k].start;
            p.firstNote = (int) k;
            t.phrases.push_back (p);
        }
        auto& p = t.phrases.back();
        p.end = t.notes[k].end;
        p.lastNote = (int) k;
        t.notes[k].phrase = (int) t.phrases.size() - 1;
    }

    for (const auto& note : t.notes)
    {
        const float dur = (float) ((double) (note.end - note.start) / sampleRate);
        t.pcHistogram[(size_t) wrapPc ((int) std::lround (note.pitch))] += dur;
        t.voicedSeconds += dur;
    }
    return t;
}

//==============================================================================
static std::string scaleName (const Scale& scale)
{
    const bool flats = scale.tonic == 5 || scale.tonic == 10 || scale.tonic == 3 || scale.tonic == 8 || scale.tonic == 1;
    std::string k = getPitchClassName (scale.tonic, flats || (scale.isMinorFamily() && (scale.tonic == 0 || scale.tonic == 7 || scale.tonic == 2)));
    std::string s = getScaleNames()[(int) scale.type];
    for (auto& ch : s) ch = (char) std::toupper ((unsigned char) ch);
    return k + " " + s;
}

std::string LearnedScore::keyName() const
{
    if (keys.size() < 2)
        return scaleName (scale);
    const char* arrow = " \xE2\x86\x92 ";
    if (keys.size() == 2)
        return scaleName (keys.front().scale) + arrow + scaleName (keys.back().scale);
    return scaleName (keys.front().scale) + arrow + "\xE2\x80\xA6" + arrow + scaleName (keys.back().scale);
}

std::string LearnedScore::describe() const
{
    char buf[160];
    const int secs = (int) std::lround (durationSeconds);
    std::snprintf (buf, sizeof (buf), "LEARNED %d:%02d \xC2\xB7 %s \xC2\xB7 %d PHRASE%s \xC2\xB7 %d NOTES%s",
                   secs / 60, secs % 60, keyName().c_str(), numPhrases, numPhrases == 1 ? "" : "S", numNotes,
                   followsBand ? " \xC2\xB7 WITH THE BAND" : "");
    return buf;
}

bool LearnedScore::voicingAt (int64_t t, Voicing& out, int& leadNote, int& sopShift) const
{
    if (events.empty() || t < events.front().start)
        return false;
    // Last event starting at or before t.
    size_t lo = 0, hi = events.size() - 1;
    while (lo < hi)
    {
        const size_t mid = (lo + hi + 1) / 2;
        if (events[mid].start <= t) lo = mid;
        else hi = mid - 1;
    }
    const auto& e = events[lo];
    out = e.voicing;
    sopShift = e.sopShift;
    leadNote = t < e.end ? e.melody : -1;
    return true;
}

/** The key of every phrase. Songs often change key (the last chorus a step up): each phrase is
    scored against every key, and a Viterbi path pays a price for each change, so one phrase
    with a few odd notes never moves the key but a run of phrases in a new key does. */
/** Pitch classes the band played between a and b (seconds of each chord note, half weight). */
static void addBandNotes (const TakeAnalysis& take, int64_t a, int64_t b, std::array<float, 12>& hist)
{
    for (const auto& bc : take.bandChords)
    {
        const int64_t overlap = std::min (b, bc.end) - std::max (a, bc.start);
        if (overlap <= 0)
            continue;
        const float secs = (float) ((double) overlap / take.sampleRate);
        const Chord& ch = bc.chord();
        for (int i = 0; i < ch.numTones; ++i)
            hist[(size_t) ch.tones[(size_t) i]] += 0.5f * secs * (i == 0 ? 1.4f : 1.0f);
    }
}

static std::vector<int> phraseKeys (const TakeAnalysis& take, Scale base)
{
    const bool minor = base.isMinorFamily();
    const int P = (int) take.phrases.size();
    auto overall = take.pcHistogram;
    addBandNotes (take, 0, take.length, overall);
    const int global = estimateKey (overall, minor, ! minor).tonic;
    std::vector<int> key ((size_t) P, global);
    if (P < 3 || base.type == ScaleType::Chromatic)
        return key;

    std::vector<std::array<float, 12>> hist ((size_t) P);
    std::vector<float> dur ((size_t) P, 0.0f);
    for (auto& h : hist) h.fill (0.0f);
    for (const auto& note : take.notes)
    {
        const float d = (float) ((double) (note.end - note.start) / take.sampleRate);
        hist[(size_t) note.phrase][(size_t) wrapPc ((int) std::lround (note.pitch))] += d;
        dur[(size_t) note.phrase] += d;
    }
    for (int p = 0; p < P; ++p)
        addBandNotes (take, take.phrases[(size_t) p].start, take.phrases[(size_t) p].end, hist[(size_t) p]);

    constexpr float switchCost = 1.6f;
    std::vector<std::array<float, 12>> cost ((size_t) P);
    std::vector<std::array<int, 12>> back ((size_t) P);
    for (int p = 0; p < P; ++p)
    {
        std::array<float, 12> fit {};
        float best = -1.0e9f;
        for (int k = 0; k < 12; ++k)
        {
            fit[(size_t) k] = keyFitness (hist[(size_t) p], k, minor);
            best = std::max (best, fit[(size_t) k]);
        }
        const float w = clampv (dur[(size_t) p] / 3.0f, 0.4f, 1.5f);
        for (int k = 0; k < 12; ++k)
        {
            const float e = w * (best - fit[(size_t) k]);
            if (p == 0)
            {
                cost[0][(size_t) k] = e + (k == global ? 0.0f : 0.3f);
                continue;
            }
            float bestPrev = 1.0e9f;
            int arg = k;
            for (int j = 0; j < 12; ++j)
            {
                const float v = cost[(size_t) p - 1][(size_t) j] + (j == k ? 0.0f : switchCost);
                if (v < bestPrev) { bestPrev = v; arg = j; }
            }
            cost[(size_t) p][(size_t) k] = bestPrev + e;
            back[(size_t) p][(size_t) k] = arg;
        }
    }
    int k = (int) (std::min_element (cost.back().begin(), cost.back().end()) - cost.back().begin());
    for (int p = P - 1; p >= 0; --p)
    {
        key[(size_t) p] = k;
        if (p > 0)
            k = back[(size_t) p][(size_t) k];
    }
    return key;
}

void removeChordLocks (std::vector<ChordLock>& locks, int64_t start, int64_t end)
{
    if (end <= start)
        return;
    std::vector<ChordLock> kept;
    kept.reserve (locks.size() + 1);
    for (const auto& l : locks)
    {
        if (l.end <= start || l.start >= end)
        {
            kept.push_back (l);
            continue;
        }
        if (l.start < start) { auto before = l; before.end = start; kept.push_back (before); }
        if (l.end > end)     { auto after = l;  after.start = end;  kept.push_back (after); }
    }
    locks = std::move (kept);
}

void addChordLock (std::vector<ChordLock>& locks, const ChordLock& lock)
{
    if (lock.end <= lock.start)
        return;
    removeChordLocks (locks, lock.start, lock.end);
    locks.push_back (lock);
    std::sort (locks.begin(), locks.end(), [] (const ChordLock& a, const ChordLock& b) { return a.start < b.start; });
}

static bool sameChord (const Chord& a, const Chord& b) { return a.root == b.root && a.mask() == b.mask(); }

/** The scale degree a chord stands on in a key (so a chosen Cmaj7 still leads like a I chord),
    or 20 when it is not built on the key's own notes. */
static int degreeInKey (const Chord& c, const Scale& scale)
{
    const auto dia = scale.diatonic();
    for (int d = 0; d < 7; ++d)
        if (dia[(size_t) d] == c.root)
            return c.contains (dia[(size_t) ((d + 2) % 7)]) ? d : 20;
    return 20;
}

const Scale& LearnedScore::scaleAt (int64_t t) const
{
    for (size_t k = keys.size(); k-- > 0;)
        if (keys[k].start <= t)
            return keys[k].scale;
    return scale;
}

std::vector<ChordSpan> LearnedScore::chordLane() const
{
    std::vector<ChordSpan> lane;
    const int64_t pause = (int64_t) (0.5 * sampleRate);
    for (const auto& e : events)
    {
        if (! lane.empty())
        {
            auto& last = lane.back();
            if (sameChord (last.chord, e.chord) && last.locked == e.locked && last.fromBand == e.fromBand
                && e.start - last.end <= pause)
            {
                last.end = std::max (last.end, e.end);
                continue;
            }
        }
        ChordSpan span;
        span.start = e.start;
        span.end = e.end;
        span.chord = e.chord;
        span.locked = e.locked;
        span.fromBand = e.fromBand;
        lane.push_back (std::move (span));
    }
    return lane;
}

LearnedScore harmoniseTake (const TakeAnalysis& take, const HarmonySettings& hs)
{
    LearnedScore score;
    score.numNotes = (int) take.notes.size();
    score.numPhrases = (int) take.phrases.size();
    score.durationSeconds = (float) ((double) take.length / take.sampleRate);
    score.sampleRate = take.sampleRate;

    Scale scale;
    scale.type = hs.scale;
    if (hs.scaleAuto)
    {
        // Scale Auto: major or minor, heard from the whole take (and the band).
        auto hist = take.pcHistogram;
        addBandNotes (take, 0, take.length, hist);
        int tonic = 0;
        bool minor = false;
        if (hs.key > 0)
        {
            tonic = (hs.key - 1) % 12;
            minor = keyFitness (hist, tonic, true) > keyFitness (hist, tonic, false);
        }
        else
        {
            const auto est = estimateKey (hist, false, false);
            tonic = est.tonic;
            minor = est.minor;
        }
        scale.type = autoScaleType (hist, tonic, minor);
    }
    std::vector<int> keyOfPhrase (take.phrases.size(), 0);
    if (hs.key > 0)
    {
        scale.tonic = (hs.key - 1) % 12;
        std::fill (keyOfPhrase.begin(), keyOfPhrase.end(), scale.tonic);
    }
    else if (! take.phrases.empty())
    {
        keyOfPhrase = phraseKeys (take, scale);
        scale.tonic = keyOfPhrase.front();
        score.keyWasDetected = true;
    }
    else
    {
        const bool minor = scale.isMinorFamily();
        auto hist = take.pcHistogram;
        addBandNotes (take, 0, take.length, hist);
        scale.tonic = estimateKey (hist, minor, ! minor).tonic;
        score.keyWasDetected = true;
    }
    score.scale = scale;
    score.keys.push_back ({ 0, scale });
    if (take.notes.empty())
        return score;

    // One chord vocabulary per key the song visits.
    std::vector<std::vector<Chord>> vocabs;
    std::vector<int> regionOfPhrase (take.phrases.size(), 0);
    vocabs.push_back (buildChordVocabulary (scale, hs.style));
    for (size_t p = 1; p < take.phrases.size(); ++p)
    {
        if (keyOfPhrase[p] != score.keys.back().scale.tonic)
        {
            Scale s = scale;
            s.tonic = keyOfPhrase[p];
            score.keys.push_back ({ take.phrases[p].start, s });
            vocabs.push_back (buildChordVocabulary (s, hs.style));
        }
        regionOfPhrase[p] = (int) score.keys.size() - 1;
    }

    const int N = (int) take.notes.size();
    const double sr = take.sampleRate;
    std::vector<int> region ((size_t) N);
    std::vector<int> q ((size_t) N);
    int maxChords = 0;
    for (const auto& v : vocabs) maxChords = std::max (maxChords, (int) v.size());
    for (int i = 0; i < N; ++i)
    {
        region[(size_t) i] = regionOfPhrase[(size_t) take.notes[(size_t) i].phrase];
        q[(size_t) i] = score.keys[(size_t) region[(size_t) i]].scale.quantise (take.notes[(size_t) i].pitch);
    }
    auto scaleAt = [&] (int i) -> const Scale& { return score.keys[(size_t) region[(size_t) i]].scale; };
    auto vocabAt = [&] (int i) -> const std::vector<Chord>& { return vocabs[(size_t) region[(size_t) i]]; };

    // Chords chosen by hand: a locked note has that one chord to choose from (the key's own copy
    // of it when there is one, so it keeps its function), and the path around it adapts.
    std::vector<Chord> lockedChord ((size_t) N);
    std::vector<char> locked ((size_t) N, 0);
    for (int i = 0; i < N && ! hs.locks.empty(); ++i)
    {
        const auto& note = take.notes[(size_t) i];
        const int64_t mid = note.start + (note.end - note.start) / 2;
        for (const auto& l : hs.locks)
        {
            if (mid < l.start || mid >= l.end)
                continue;
            locked[(size_t) i] = 1;
            lockedChord[(size_t) i] = l.chord;
            lockedChord[(size_t) i].degree = degreeInKey (l.chord, scaleAt (i));
            for (const auto& c : vocabAt (i))
                if (sameChord (c, l.chord))
                    lockedChord[(size_t) i] = c;
            break;
        }
    }
    auto candidates = [&] (int i) -> const Chord* { return locked[(size_t) i] ? &lockedChord[(size_t) i] : vocabAt (i).data(); };
    auto chordsAt = [&] (int i) { return locked[(size_t) i] ? 1 : (int) vocabAt (i).size(); };

    // Viterbi over chords, one step per note.
    std::vector<int> chordOf ((size_t) N, 0);
    const bool chordModes = hs.mode != HarmonyMode::Drone && scale.type != ScaleType::Chromatic && maxChords > 0;
    if (chordModes)
    {
        const int numChords = maxChords;   // row stride; a key with fewer chords leaves the rest unused
        const float inf = std::numeric_limits<float>::max() / 4.0f;
        std::vector<float> cost ((size_t) N * (size_t) numChords, inf);
        std::vector<int> back ((size_t) N * (size_t) numChords, 0);
        auto emission = [&] (int i, int c)
        {
            const auto& note = take.notes[(size_t) i];
            const float dur = (float) ((double) (note.end - note.start) / sr);
            const float w = clampv (dur / 0.25f, 0.3f, 2.0f);
            const Chord& ch = candidates (i)[c];
            const int role = ch.roleOf (q[(size_t) i]);
            static const float roleCost[] = { 0.0f, 0.05f, 0.15f, 0.35f };
            float e = role < 0 ? 2.5f * w : roleCost[role];
            e += (1.0f - ch.prior) * 0.8f;
            const auto& ph = take.phrases[(size_t) note.phrase];
            if (i == ph.firstNote && ch.degree == 0) e -= 0.3f;
            if (i == ph.lastNote)
            {
                if (ch.degree == 0) e -= 0.6f;
                else if (ch.degree == 4 || ch.degree == 16) e -= 0.25f;
            }
            return e;
        };

        // Harmony speed scales the price of changing chord (slow = longer chords, passing notes).
        const float speed = clampv (hs.speed, 0.0f, 1.0f);
        const float changeCost = speed <= 0.5f ? 2.2f - 2.4f * speed : 1.0f - 1.2f * (speed - 0.5f);

        for (int c = 0; c < chordsAt (0); ++c)
            cost[(size_t) c] = emission (0, c) - (candidates (0)[c].degree == 0 ? 0.3f : 0.0f);

        for (int i = 1; i < N; ++i)
        {
            const auto& note = take.notes[(size_t) i];
            const bool phraseStart = take.phrases[(size_t) note.phrase].firstNote == i;
            const float dur = (float) ((double) (note.end - note.start) / sr);
            const Chord* from = candidates (i - 1);
            const Chord* to = candidates (i);
            const int numFrom = chordsAt (i - 1), numTo = chordsAt (i);
            const bool sameKey = region[(size_t) i] == region[(size_t) i - 1];
            for (int b = 0; b < numTo; ++b)
            {
                const float e = emission (i, b);
                float best = inf;
                int arg = 0;
                for (int a = 0; a < numFrom; ++a)
                {
                    float tr = 0.0f;
                    if (! sameChord (from[a], to[b]))
                    {
                        tr = (phraseStart ? 0.15f : 0.5f * (dur < 0.15f ? 1.5f : 1.0f)) * changeCost;
                        if (sameKey)
                            tr -= progressionBonus (from[a], to[b], scaleAt (i).isMinorFamily()) * 1.5f;
                    }
                    const float v = cost[(size_t) (i - 1) * (size_t) numChords + (size_t) a] + tr;
                    if (v < best) { best = v; arg = a; }
                }
                cost[(size_t) i * (size_t) numChords + (size_t) b] = best + e;
                back[(size_t) i * (size_t) numChords + (size_t) b] = arg;
            }
        }

        int bestEnd = 0;
        for (int c = 1; c < chordsAt (N - 1); ++c)
            if (cost[(size_t) (N - 1) * (size_t) numChords + (size_t) c] < cost[(size_t) (N - 1) * (size_t) numChords + (size_t) bestEnd])
                bestEnd = c;
        chordOf[(size_t) N - 1] = bestEnd;
        for (int i = N - 1; i > 0; --i)
            chordOf[(size_t) i - 1] = back[(size_t) i * (size_t) numChords + (size_t) chordOf[(size_t) i]];
    }

    // Soprano octave per phrase: keep the doubled melody in one octave for the whole phrase,
    // in the comfortable part of the range, weighted by how long each note is held. Two-octave
    // shifts are avoided (in Words mode they turn the singer's voice into a cartoon).
    std::vector<int> phraseShift (take.phrases.size(), 0);
    {
        const auto& r = hs.ranges[Soprano];
        const float comfortable = 0.5f * (float) (r.low + std::max (r.low + 7, r.high - 6));
        const float extraOctave = hs.wordsMode ? 1.5f : 0.5f;
        for (size_t p = 0; p < take.phrases.size(); ++p)
        {
            float bestCost = 1.0e9f;
            for (int k = -3; k <= 3; ++k)
            {
                float c = 0.0f, wsum = 0.0f;
                for (int i = take.phrases[p].firstNote; i <= take.phrases[p].lastNote; ++i)
                {
                    const auto& note = take.notes[(size_t) i];
                    const float w = clampv ((float) ((double) (note.end - note.start) / sr) / 0.25f, 0.2f, 2.0f);
                    const int nn = q[(size_t) i] + 12 * k;
                    float e = 0.0f;
                    if (nn < r.low) e += (float) (r.low - nn) * 0.5f;
                    if (nn > r.high) e += (float) (nn - r.high) * 0.5f;
                    e = std::min (e, 4.0f) + 0.08f * std::abs ((float) nn - comfortable);
                    c += w * e;
                    wsum += w;
                }
                c = c / std::max (wsum, 1.0e-3f) + extraOctave * (float) std::max (0, std::abs (k) - 1);
                if (c < bestCost) { bestCost = c; phraseShift[p] = 12 * k; }
            }
        }
    }

    // The band's chord under each note (the one sounding over most of it), if the take has one.
    const bool band = hs.followBand && ! take.bandChords.empty() && hs.mode != HarmonyMode::Drone;
    score.followsBand = band;
    auto bandChordFor = [&take] (const LearnedNote& note) -> const BandChord*
    {
        const BandChord* best = nullptr;
        int64_t bestOverlap = 0;
        for (const auto& bc : take.bandChords)
        {
            if (bc.start >= note.end) break;
            const int64_t overlap = std::min (note.end, bc.end) - std::max (note.start, bc.start);
            if (overlap > bestOverlap) { bestOverlap = overlap; best = &bc; }
        }
        return best;
    };

    // Voice each note with voice leading from the previous one.
    Voicing prev;
    for (int i = 0; i < N; ++i)
    {
        const auto& note = take.notes[(size_t) i];
        VoicingRequest req;
        req.mode = hs.mode;
        req.style = hs.style;
        req.scale = scaleAt (i);
        const auto& vocab = vocabAt (i);
        if (chordModes)
            req.chord = candidates (i)[chordOf[(size_t) i]];
        else if (scale.type == ScaleType::Chromatic)
        {
            const int tones[] = { q[(size_t) i] - 7, q[(size_t) i] - 3, q[(size_t) i] };
            req.chord = chordFromPitchClasses (tones, 3);
        }
        else
            req.chord = vocab.empty() ? chordFromPitchClasses (&req.scale.tonic, 1) : vocab.front();
        const BandChord* bc = band ? bandChordFor (note) : nullptr;
        if (locked[(size_t) i])
        {
            req.chord = lockedChord[(size_t) i];
            req.bassPc = bc != nullptr && req.chord.contains (bc->bass) ? bc->bass : -1;
        }
        else if (bc != nullptr)
        {
            req.chord = bc->chord();
            req.bassPc = bc->bass;
        }
        req.melody = q[(size_t) i];
        req.sopranoOctaveShift = phraseShift[(size_t) note.phrase];
        req.ranges = hs.ranges;
        req.active = hs.active;
        req.previous = prev.valid ? &prev : nullptr;
        req.wordsMode = hs.wordsMode;

        LearnedEvent ev;
        ev.start = note.start;
        const bool lastInPhrase = take.phrases[(size_t) note.phrase].lastNote == i;
        ev.end = (i + 1 < N && ! lastInPhrase) ? take.notes[(size_t) i + 1].start : note.end;
        ev.chord = req.chord;
        ev.voicing = voiceChord (req);
        ev.melody = q[(size_t) i];
        ev.sopShift = req.sopranoOctaveShift;
        ev.locked = locked[(size_t) i] != 0;
        ev.fromBand = ! ev.locked && bc != nullptr;
        prev = ev.voicing;
        score.events.push_back (std::move (ev));
    }
    return score;
}

//==============================================================================
std::vector<ChordOption> chordAlternatives (const TakeAnalysis& take, const LearnedScore& score,
                                            const HarmonySettings& hs, int64_t start, int64_t end, int maxOptions)
{
    std::vector<ChordOption> out;
    if (end <= start || score.events.empty() || maxOptions <= 0)
        return out;

    // The sung notes in the span (by their middle), as the harmony hears them.
    struct Sung { int pc; float secs; bool cadence; };
    std::vector<Sung> sung;
    for (size_t i = 0; i < take.notes.size(); ++i)
    {
        const auto& note = take.notes[i];
        const int64_t mid = note.start + (note.end - note.start) / 2;
        if (mid < start || mid >= end)
            continue;
        const bool last = note.phrase < (int) take.phrases.size() && take.phrases[(size_t) note.phrase].lastNote == (int) i;
        sung.push_back ({ wrapPc (score.scaleAt (mid).quantise (note.pitch)),
                          (float) ((double) (note.end - note.start) / take.sampleRate), last });
    }
    if (sung.empty())
        return out;

    const Scale& key = score.scaleAt (start + (end - start) / 2);
    const bool flats = keyPrefersFlats (key);
    const bool minor = key.isMinorFamily();

    // The chords either side, and what plays here now.
    const LearnedEvent* before = nullptr;
    const LearnedEvent* after = nullptr;
    const LearnedEvent* now = nullptr;
    for (const auto& e : score.events)
    {
        if (e.end <= start || e.start < start) { if (e.start < start) before = &e; continue; }
        if (e.start >= end) { after = &e; break; }
        if (now == nullptr) now = &e;
    }
    if (now == nullptr)
        return out;

    // Candidates: the key's chords, their colours (sevenths, sus4), common borrowed chords, the
    // band's chord and the current one.
    std::vector<Chord> cands = buildChordVocabulary (key, hs.style);
    auto add = [&cands] (Chord c, int degree, float prior)
    {
        for (const auto& x : cands)
            if (sameChord (x, c))
                return;
        c.degree = degree;
        c.prior = prior;
        cands.push_back (std::move (c));
    };
    const auto dia = key.diatonic();
    if (key.type != ScaleType::Chromatic)
    {
        for (int d = 0; d < 7; ++d)
        {
            const int root = dia[(size_t) d];
            const int third = wrapPc (dia[(size_t) ((d + 2) % 7)] - root);
            const int fifth = wrapPc (dia[(size_t) ((d + 4) % 7)] - root);
            const int seventh = wrapPc (dia[(size_t) ((d + 6) % 7)] - root);
            if (fifth != 7)
                continue;
            const int triad[] = { 0, third, fifth };
            const int sev[] = { 0, third, fifth, seventh };
            const int sus[] = { 0, 5, 7 };
            add (makeChordFromIntervals (root, triad, 3, flats), d, 0.4f);
            add (makeChordFromIntervals (root, sev, 4, flats), d, 0.35f);
            if (d == 0 || d == 3 || d == 4)
                add (makeChordFromIntervals (root, sus, 3, flats), d, 0.3f);
        }
        const int t = key.tonic;
        auto borrowed = [&] (int offset, ChordQuality q, int degree, bool flat = false)
        {
            add (makeChordOfQuality (t + offset, q, flats || flat), degree, 0.15f);
        };
        if (! minor)
        {
            borrowed (5, ChordQuality::Minor, 10);           // iv
            borrowed (2, ChordQuality::Major, 11);           // II (V of V)
            borrowed (10, ChordQuality::Major, 12, true);    // bVII
            borrowed (8, ChordQuality::Major, 20, true);     // bVI
            borrowed (4, ChordQuality::Major, 20);           // III (V of vi)
        }
        else
        {
            borrowed (7, ChordQuality::Major, 16);           // V
            borrowed (5, ChordQuality::Major, 17);           // IV
            borrowed (0, ChordQuality::Major, 20);           // a major I to end on
        }
    }
    const BandChord* bandHere = nullptr;
    if (hs.followBand && ! take.bandChords.empty())
    {
        int64_t bestOverlap = 0;
        for (const auto& bc : take.bandChords)
        {
            const int64_t overlap = std::min (end, bc.end) - std::max (start, bc.start);
            if (overlap > bestOverlap) { bestOverlap = overlap; bandHere = &bc; }
        }
        if (bandHere != nullptr && bestOverlap * 2 < end - start)
            bandHere = nullptr;
        if (bandHere != nullptr)
            add (bandHere->chord(), degreeInKey (bandHere->chord(), key), 0.5f);
    }
    add (now->chord, degreeInKey (now->chord, key), 0.5f);

    // Rank: melody notes on chord tones (roots best), the chord's place in the key, how it leads
    // from the chord before and into the one after, and what the band plays.
    struct Ranked { size_t index; float cost, fit; };
    std::vector<Ranked> ranked;
    float total = 0.0f;
    for (const auto& n : sung) total += n.secs;
    auto inKey = [&key, &score] (const LearnedEvent* e)
    {
        return e != nullptr && score.scaleAt (e->start).tonic == key.tonic && score.scaleAt (e->start).type == key.type;
    };
    const bool sameKeyBefore = inKey (before);
    const bool sameKeyAfter = inKey (after);
    for (size_t c = 0; c < cands.size(); ++c)
    {
        const Chord& ch = cands[c];
        static const float roleCost[] = { 0.0f, 0.05f, 0.15f, 0.35f };
        float cost = 0.0f, on = 0.0f;
        for (const auto& n : sung)
        {
            const float w = clampv (n.secs / 0.25f, 0.3f, 2.0f);
            const int role = ch.roleOf (n.pc);
            cost += role < 0 ? 2.5f * w : roleCost[role];
            cost += (1.0f - ch.prior) * 0.8f;
            if (role >= 0) on += n.secs;
            if (n.cadence && ch.degree == 0) cost -= 0.6f;
        }
        if (sameKeyBefore && ! sameChord (before->chord, ch))
            cost -= progressionBonus (before->chord, ch, minor);
        if (sameKeyAfter && ! sameChord (after->chord, ch))
            cost -= progressionBonus (ch, after->chord, minor);
        if (bandHere != nullptr)
            cost += sameChord (bandHere->chord(), ch) ? -1.0f * (float) sung.size() : 0.0f;
        ranked.push_back ({ c, cost, total > 0.0f ? on / total : 0.0f });
    }
    std::stable_sort (ranked.begin(), ranked.end(), [] (const Ranked& a, const Ranked& b) { return a.cost < b.cost; });

    // Best first, but varied: each further chord on a root already offered (G7 after G) moves
    // down, so the first few choices are different chords rather than colours of one.
    std::vector<char> taken (ranked.size(), 0);
    while ((int) out.size() < maxOptions)
    {
        int bestK = -1;
        float bestCost = 0.0f;
        for (size_t k = 0; k < ranked.size(); ++k)
        {
            if (taken[k])
                continue;
            float c = ranked[k].cost;
            for (const auto& o : out)
                if (o.chord.root == cands[ranked[k].index].root)
                    c += 0.6f;
            if (bestK < 0 || c < bestCost) { bestK = (int) k; bestCost = c; }
        }
        if (bestK < 0)
            break;
        taken[(size_t) bestK] = 1;
        const auto& r = ranked[(size_t) bestK];
        out.push_back ({ cands[r.index], r.fit, sameChord (cands[r.index], now->chord) });
    }
    if (std::none_of (out.begin(), out.end(), [] (const ChordOption& o) { return o.current; }))
    {
        for (const auto& r : ranked)
            if (sameChord (cands[r.index], now->chord))
                out.back() = { cands[r.index], r.fit, true };
    }
    return out;
}

//==============================================================================
bool renderTake (const float* x, int64_t n, double sampleRate, const EngineParams& paramsIn,
                 const LearnedScore& score, RenderedStems& out, const ProgressFn& progress,
                 const std::atomic<bool>* cancel, const SampleSet* voices, const std::vector<const float*>& passes)
{
    out = RenderedStems{};
    if (n <= 0)
        return false;

    EngineParams params = paramsIn;
    params.freeze = false;
    params.midiEnabled = false;
    params.lowLatency = false;   // offline: everything aligned, then the latency is removed below

    const int block = 512;
    // Probe the latency once.
    int latency = 0;
    {
        ChoirEngine probe;
        probe.prepare (sampleRate, block);
        latency = probe.getLatencySamples();
    }
    // Echo mode answers each phrase after it ends, so leave room for the last answer.
    const int64_t tail = params.response == 1 ? (int64_t) (sampleRate * 11.5) : 0;
    const int64_t outLength = n + tail;
    const int64_t total = outLength + latency + block;

    out.sampleRate = sampleRate;
    out.length = outLength;
    std::array<std::vector<float>, NumSections * 2> raw;
    for (auto& ch : raw)
        ch.assign ((size_t) total, 0.0f);

    std::array<std::atomic<float>, NumSections> sectionProgress {};
    for (auto& p : sectionProgress) p.store (0.0f);
    std::atomic<bool> failed { false };

    auto work = [&] (int section)
    {
        ScopedFlushDenormals noDenormals;
        ChoirEngine engine;
        engine.prepare (sampleRate, block);
        engine.setParams (params);
        engine.setScore (&score);
        engine.setSampleSet (voices);
        engine.setRenderSections ((uint8_t) (1u << section));
        float* taps[NumSections * 2] {};
        taps[section * 2] = raw[(size_t) section * 2].data();
        taps[section * 2 + 1] = raw[(size_t) section * 2 + 1].data();
        engine.setStemTap (taps);

        std::vector<float> in ((size_t) block), outL ((size_t) block), outR ((size_t) block);
        // The other passes (lined up with the take, n samples each), block by block with it.
        const int numPasses = std::min ((int) passes.size(), kMaxExtraPasses);
        std::vector<std::vector<float>> passBlocks ((size_t) numPasses, std::vector<float> ((size_t) block));
        std::array<const float*, kMaxExtraPasses> passPtrs {};
        for (int k = 0; k < numPasses; ++k)
            passPtrs[(size_t) k] = passBlocks[(size_t) k].data();
        for (int64_t pos = 0; pos + block <= total; pos += block)
        {
            if (cancelled (cancel)) { failed = true; return; }
            for (int i = 0; i < block; ++i)
                in[(size_t) i] = pos + i < n ? x[pos + i] : 0.0f;
            for (int k = 0; k < numPasses; ++k)
                for (int i = 0; i < block; ++i)
                    passBlocks[(size_t) k][(size_t) i] = pos + i < n && passes[(size_t) k] != nullptr ? passes[(size_t) k][pos + i] : 0.0f;
            engine.setPasses (passPtrs.data(), numPasses);
            engine.process (in.data(), nullptr, outL.data(), outR.data(), block);
            sectionProgress[(size_t) section].store ((float) ((double) pos / (double) total));
        }
        sectionProgress[(size_t) section].store (1.0f);
    };

    std::vector<std::thread> threads;
    for (int s = 1; s < NumSections; ++s)
        threads.emplace_back (work, s);

    // Section 0 on this thread, reporting overall progress while it runs.
    std::thread mainWork (work, 0);
    while (true)
    {
        float p = 0.0f;
        bool done = true;
        for (auto& sp : sectionProgress)
        {
            const float v = sp.load();
            p += v;
            done = done && v >= 1.0f;
        }
        if (progress) progress (p / (float) NumSections);
        if (done || failed.load()) break;
        std::this_thread::sleep_for (std::chrono::milliseconds (40));
    }
    mainWork.join();
    for (auto& t : threads)
        t.join();

    if (failed.load())
        return false;

    // Align to the take timeline (remove engine latency).
    for (int ch = 0; ch < NumSections * 2; ++ch)
    {
        auto& dst = out.channels[(size_t) ch];
        dst.assign ((size_t) outLength, 0.0f);
        std::memcpy (dst.data(), raw[(size_t) ch].data() + latency, sizeof (float) * (size_t) outLength);
    }
    if (progress) progress (1.0f);
    return true;
}

bool mixdownStemsStreaming (double sampleRate, int64_t length, const EngineParams& paramsIn, bool roomOnly,
                            float tailSeconds, const StemFill& fill, const MixOut& out,
                            const float* lead, int64_t leadLength)
{
    ScopedFlushDenormals noDenormals;

    EngineParams params = paramsIn;
    params.freeze = false;
    params.midiEnabled = false;
    params.wetOnly = roomOnly;
    params.rhythmPattern = 0;   // no song position here: exports are without the rhythm effect
    if (lead == nullptr)
        params.leadDb = -60.0f;
    // The stems are already in time; Live monitoring passes the lead straight through, so it
    // stays in time with them.
    params.lowLatency = lead != nullptr;

    const int block = 512;
    ChoirEngine engine;
    engine.prepare (sampleRate, block);
    engine.setParams (params);

    const int64_t total = length + (int64_t) (tailSeconds * sampleRate);
    std::vector<float> zeros ((size_t) block, 0.0f), left ((size_t) block), right ((size_t) block), leadBlock ((size_t) block, 0.0f);
    std::array<std::vector<float>, NumSections * 2> chunk;
    std::array<float*, NumSections * 2> ptrs {};
    for (size_t ch = 0; ch < chunk.size(); ++ch)
    {
        chunk[ch].assign ((size_t) block, 0.0f);
        ptrs[ch] = chunk[ch].data();
    }

    for (int64_t pos = 0; pos < total; pos += block)
    {
        const int len = (int) std::min<int64_t> (block, total - pos);
        for (auto& c : chunk)
            std::fill (c.begin(), c.end(), 0.0f);
        if (pos < length)
            fill (pos, (int) std::min<int64_t> (len, length - pos), ptrs.data());
        StemPlayback pb;
        for (size_t ch = 0; ch < chunk.size(); ++ch)
            pb.sections[ch] = ptrs[ch];
        const float* in = zeros.data();
        if (lead != nullptr && pos < leadLength)
        {
            const int have = (int) std::min<int64_t> (len, leadLength - pos);
            std::copy (lead + pos, lead + pos + have, leadBlock.begin());
            std::fill (leadBlock.begin() + have, leadBlock.end(), 0.0f);
            in = leadBlock.data();
        }
        engine.process (in, nullptr, left.data(), right.data(), len, &pb);
        if (! out (left.data(), right.data(), len))
            return false;
    }
    return true;
}

std::array<std::vector<float>, 2> mixdownStems (const RenderedStems& stems, const EngineParams& params,
                                                bool roomOnly, float tailSeconds, const float* lead, int64_t leadLength)
{
    std::array<std::vector<float>, 2> result;
    if (stems.isEmpty())
        return result;
    result[0].reserve ((size_t) (stems.length + (int64_t) (tailSeconds * stems.sampleRate)));
    result[1].reserve (result[0].capacity());
    mixdownStemsStreaming (stems.sampleRate, stems.length, params, roomOnly, tailSeconds,
        [&stems] (int64_t pos, int len, float* const* channels)
        {
            for (int ch = 0; ch < NumSections * 2; ++ch)
            {
                const auto& src = stems.channels[(size_t) ch];
                for (int i = 0; i < len; ++i)
                    channels[ch][i] = pos + i < (int64_t) src.size() ? src[(size_t) (pos + i)] : 0.0f;
            }
        },
        [&result] (const float* l, const float* r, int len)
        {
            result[0].insert (result[0].end(), l, l + len);
            result[1].insert (result[1].end(), r, r + len);
            return true;
        }, lead, leadLength);
    return result;
}
} // namespace sc
