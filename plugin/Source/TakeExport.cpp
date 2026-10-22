#include "TakeExport.h"

namespace sanctuary
{
using namespace juce;

namespace
{
constexpr int kTicksPerQuarter = 960;

struct Note
{
    int pitch = 60;
    double start = 0.0, end = 0.0;   // ticks
};

/** Notes of one track into a sequence. Notes of the same pitch never overlap: a held note is
    tied to the next one on the same pitch (tie), or ends just before it. */
MidiMessageSequence toSequence (std::vector<Note> notes, int channel, const String& name, uint8 velocity, bool tie)
{
    MidiMessageSequence seq;
    seq.addEvent (MidiMessage::textMetaEvent (3, name), 0.0);
    std::sort (notes.begin(), notes.end(), [] (const Note& a, const Note& b)
               { return a.pitch != b.pitch ? a.pitch < b.pitch : a.start < b.start; });
    std::vector<Note> merged;
    for (const auto& n : notes)
    {
        if (n.end <= n.start || n.pitch < 0 || n.pitch > 127)
            continue;
        if (! merged.empty() && merged.back().pitch == n.pitch && n.start <= merged.back().end + 1.0)
        {
            if (tie || n.start <= merged.back().start + 1.0)
            {
                merged.back().end = std::max (merged.back().end, n.end);
                continue;
            }
            merged.back().end = n.start - 1.0;
        }
        merged.push_back (n);
    }
    for (const auto& n : merged)
    {
        seq.addEvent (MidiMessage::noteOn (channel, n.pitch, velocity), std::round (n.start));
        seq.addEvent (MidiMessage::noteOff (channel, n.pitch), std::round (n.end));
    }
    seq.updateMatchedPairs();
    return seq;
}
} // namespace

MidiFile makeTakeMidi (const sc::TakeAnalysis& take, const sc::LearnedScore& score, const TakeTiming& timing,
                       bool separateSyllables)
{
    MidiFile file;
    file.setTicksPerQuarterNote (kTicksPerQuarter);

    const double bpm = timing.bpm > 20.0 && timing.bpm < 400.0 ? timing.bpm : 120.0;
    const double startBeats = timing.ppqStart > 0.0 ? timing.ppqStart : 0.0;
    const double rate = take.sampleRate > 0.0 ? take.sampleRate : 48000.0;
    auto tick = [&] (int64_t t) { return (startBeats + (double) t / rate * bpm / 60.0) * kTicksPerQuarter; };

    MidiMessageSequence conductor;
    conductor.addEvent (MidiMessage::textMetaEvent (3, "Sanctuary Choir"), 0.0);
    conductor.addEvent (MidiMessage::tempoMetaEvent (roundToInt (60.0e6 / bpm)), 0.0);
    conductor.addEvent (MidiMessage::timeSignatureMetaEvent (jlimit (1, 32, timing.sigNum), jlimit (1, 32, timing.sigDen)), 0.0);
    file.addTrack (conductor);

    // The four sections, each held until the next sung note (as the choir sings them).
    static const char* names[] = { "Soprano", "Alto", "Tenor", "Bass" };
    for (int s = 0; s < sc::NumSections; ++s)
    {
        std::vector<Note> notes;
        for (const auto& e : score.events)
            for (int k = 0; k < e.voicing.count[(size_t) s]; ++k)
                notes.push_back ({ e.voicing.notes[(size_t) s][(size_t) k], tick (e.start), tick (e.end) });
        if (! notes.empty())
            file.addTrack (toSequence (std::move (notes), s + 1, names[s], 90, ! separateSyllables));
    }

    // The melody as sung (in the key, as the choir heard it).
    {
        std::vector<Note> notes;
        const bool aligned = score.events.size() == take.notes.size();
        for (size_t i = 0; i < take.notes.size(); ++i)
        {
            const auto& n = take.notes[i];
            const int pitch = aligned && score.events[i].melody >= 0 ? score.events[i].melody : (int) std::lround (n.pitch);
            notes.push_back ({ pitch, tick (n.start), tick (n.end) });
        }
        if (! notes.empty())
            file.addTrack (toSequence (std::move (notes), 5, "Melody", 100, false));
    }

    // The chords: one block chord per change, root in the octave below middle C, with its name.
    {
        std::vector<Note> notes;
        MidiMessageSequence chordNames;
        for (const auto& span : score.chordLane())
        {
            const int root = 48 + span.chord.root;
            for (int k = 0; k < span.chord.numTones; ++k)
                notes.push_back ({ root + sc::wrapPc (span.chord.tones[(size_t) k] - span.chord.root), tick (span.start), tick (span.end) });
            chordNames.addEvent (MidiMessage::textMetaEvent (1, String (CharPointer_UTF8 (span.chord.name.c_str()))), std::round (tick (span.start)));
        }
        if (! notes.empty())
        {
            auto seq = toSequence (std::move (notes), 6, "Chords", 72, false);
            seq.addSequence (chordNames, 0.0);
            seq.updateMatchedPairs();
            file.addTrack (seq);
        }
    }
    return file;
}

bool StereoWavWriter::open (const File& f, double sampleRate)
{
    close();
    file = f;
    file.deleteFile();
    auto fileStream = std::make_unique<FileOutputStream> (file);
    if (fileStream->failedToOpen())
        return ok = false;
    std::unique_ptr<OutputStream> stream = std::move (fileStream);
    WavAudioFormat wav;
    writer = wav.createWriterFor (stream, AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24));
    return ok = writer != nullptr;
}

bool StereoWavWriter::writeSilence (int64_t n)
{
    std::vector<float> zeros ((size_t) std::min<int64_t> (n, 65536), 0.0f);
    for (int64_t pos = 0; pos < n && ok; pos += (int64_t) zeros.size())
        ok = write (zeros.data(), zeros.data(), (int) std::min<int64_t> ((int64_t) zeros.size(), n - pos));
    return ok;
}

bool StereoWavWriter::write (const float* left, const float* right, int n)
{
    if (! ok || writer == nullptr)
        return ok = false;
    const float* chans[] = { left, right };
    return ok = writer->writeFromFloatArrays (chans, 2, n);
}

bool StereoWavWriter::close()
{
    if (writer == nullptr)
        return ok;
    writer.reset();   // flushes and closes the file
    if (! ok)
        file.deleteFile();
    return ok;
}
} // namespace sanctuary
