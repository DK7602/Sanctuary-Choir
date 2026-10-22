#include "sanctuary/Learn.h"

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
std::string LearnedScore::keyName() const
{
    const bool flats = scale.tonic == 5 || scale.tonic == 10 || scale.tonic == 3 || scale.tonic == 8 || scale.tonic == 1;
    std::string k = getPitchClassName (scale.tonic, flats || (scale.isMinorFamily() && (scale.tonic == 0 || scale.tonic == 7 || scale.tonic == 2)));
    std::string s = getScaleNames()[(int) scale.type];
    for (auto& ch : s) ch = (char) std::toupper ((unsigned char) ch);
    return k + " " + s;
}

std::string LearnedScore::describe() const
{
    char buf[160];
    const int secs = (int) std::lround (durationSeconds);
    std::snprintf (buf, sizeof (buf), "LEARNED %d:%02d \xC2\xB7 %s \xC2\xB7 %d PHRASE%s \xC2\xB7 %d NOTES",
                   secs / 60, secs % 60, keyName().c_str(), numPhrases, numPhrases == 1 ? "" : "S", numNotes);
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

LearnedScore harmoniseTake (const TakeAnalysis& take, const HarmonySettings& hs)
{
    LearnedScore score;
    score.numNotes = (int) take.notes.size();
    score.numPhrases = (int) take.phrases.size();
    score.durationSeconds = (float) ((double) take.length / take.sampleRate);

    Scale scale;
    scale.type = hs.scale;
    if (hs.key > 0)
        scale.tonic = (hs.key - 1) % 12;
    else
    {
        const bool minor = scale.isMinorFamily();
        scale.tonic = estimateKey (take.pcHistogram, minor, ! minor).tonic;
        score.keyWasDetected = true;
    }
    score.scale = scale;
    if (take.notes.empty())
        return score;

    const auto vocab = buildChordVocabulary (scale, hs.style);
    const int numChords = (int) vocab.size();
    const int N = (int) take.notes.size();
    const double sr = take.sampleRate;

    std::vector<int> q ((size_t) N);
    for (int i = 0; i < N; ++i)
        q[(size_t) i] = scale.quantise (take.notes[(size_t) i].pitch);

    // Viterbi over chords, one step per note.
    std::vector<int> chordOf ((size_t) N, 0);
    const bool chordModes = hs.mode != HarmonyMode::Drone && scale.type != ScaleType::Chromatic && numChords > 0;
    if (chordModes)
    {
        const float inf = std::numeric_limits<float>::max() / 4.0f;
        std::vector<float> cost ((size_t) N * (size_t) numChords, inf);
        std::vector<int> back ((size_t) N * (size_t) numChords, 0);
        auto emission = [&] (int i, int c)
        {
            const auto& note = take.notes[(size_t) i];
            const float dur = (float) ((double) (note.end - note.start) / sr);
            const float w = clampv (dur / 0.25f, 0.3f, 2.0f);
            const Chord& ch = vocab[(size_t) c];
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

        for (int c = 0; c < numChords; ++c)
            cost[(size_t) c] = emission (0, c) - (vocab[(size_t) c].degree == 0 ? 0.3f : 0.0f);

        for (int i = 1; i < N; ++i)
        {
            const auto& note = take.notes[(size_t) i];
            const bool phraseStart = take.phrases[(size_t) note.phrase].firstNote == i;
            const float dur = (float) ((double) (note.end - note.start) / sr);
            for (int b = 0; b < numChords; ++b)
            {
                const float e = emission (i, b);
                float best = inf;
                int arg = 0;
                for (int a = 0; a < numChords; ++a)
                {
                    float tr = 0.0f;
                    if (a != b)
                    {
                        tr = (phraseStart ? 0.15f : 0.5f * (dur < 0.15f ? 1.5f : 1.0f)) * changeCost;
                        tr -= progressionBonus (vocab[(size_t) a], vocab[(size_t) b], scale.isMinorFamily()) * 1.5f;
                    }
                    const float v = cost[(size_t) (i - 1) * (size_t) numChords + (size_t) a] + tr;
                    if (v < best) { best = v; arg = a; }
                }
                cost[(size_t) i * (size_t) numChords + (size_t) b] = best + e;
                back[(size_t) i * (size_t) numChords + (size_t) b] = arg;
            }
        }

        int bestEnd = 0;
        for (int c = 1; c < numChords; ++c)
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

    // Voice each note with voice leading from the previous one.
    Voicing prev;
    for (int i = 0; i < N; ++i)
    {
        const auto& note = take.notes[(size_t) i];
        VoicingRequest req;
        req.mode = hs.mode;
        req.style = hs.style;
        req.scale = scale;
        if (chordModes)
            req.chord = vocab[(size_t) chordOf[(size_t) i]];
        else if (scale.type == ScaleType::Chromatic)
        {
            const int tones[] = { q[(size_t) i] - 7, q[(size_t) i] - 3, q[(size_t) i] };
            req.chord = chordFromPitchClasses (tones, 3);
        }
        else
            req.chord = vocab.empty() ? chordFromPitchClasses (&scale.tonic, 1) : vocab.front();
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
        prev = ev.voicing;
        score.events.push_back (std::move (ev));
    }
    return score;
}

//==============================================================================
bool renderTake (const float* x, int64_t n, double sampleRate, const EngineParams& paramsIn,
                 const LearnedScore& score, RenderedStems& out, const ProgressFn& progress,
                 const std::atomic<bool>* cancel, const SampleSet* voices)
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
        for (int64_t pos = 0; pos + block <= total; pos += block)
        {
            if (cancelled (cancel)) { failed = true; return; }
            for (int i = 0; i < block; ++i)
                in[(size_t) i] = pos + i < n ? x[pos + i] : 0.0f;
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

std::array<std::vector<float>, 2> mixdownStems (const RenderedStems& stems, const EngineParams& paramsIn,
                                                bool roomOnly, float tailSeconds)
{
    std::array<std::vector<float>, 2> result;
    if (stems.isEmpty())
        return result;

    EngineParams params = paramsIn;
    params.freeze = false;
    params.midiEnabled = false;
    params.wetOnly = roomOnly;
    params.leadDb = -60.0f;
    params.lowLatency = false;

    const int block = 512;
    ChoirEngine engine;
    engine.prepare (stems.sampleRate, block);
    engine.setParams (params);

    const int64_t total = stems.length + (int64_t) (tailSeconds * stems.sampleRate);
    result[0].assign ((size_t) total, 0.0f);
    result[1].assign ((size_t) total, 0.0f);

    std::vector<float> zeros ((size_t) block, 0.0f);
    std::array<std::vector<float>, NumSections * 2> chunk;
    for (auto& c : chunk) c.assign ((size_t) block, 0.0f);

    for (int64_t pos = 0; pos < total; pos += block)
    {
        const int len = (int) std::min<int64_t> (block, total - pos);
        StemPlayback pb;
        for (int ch = 0; ch < NumSections * 2; ++ch)
        {
            auto& c = chunk[(size_t) ch];
            std::fill (c.begin(), c.end(), 0.0f);
            const auto& src = stems.channels[(size_t) ch];
            for (int i = 0; i < len; ++i)
                if (pos + i < (int64_t) src.size())
                    c[(size_t) i] = src[(size_t) (pos + i)];
            pb.sections[(size_t) ch] = c.data();
        }
        engine.process (zeros.data(), nullptr, result[0].data() + pos, result[1].data() + pos, len, &pb);
    }
    return result;
}
} // namespace sc
