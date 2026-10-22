// Sanctuary Choir - files from a learned take, to drag into the DAW: a MIDI file of the parts
// and the chords, and WAV files of the rendered choir.
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <sanctuary/Learn.h>

namespace sanctuary
{
/** Where the take sits in the song, so the files line up when dropped at the song's start. */
struct TakeTiming
{
    double bpm = 0.0;          // the song's tempo while the take was learned (0 = unknown)
    double ppqStart = -1.0;    // song position of the take's first sample, in quarter notes (-1 = unknown)
    int sigNum = 4, sigDen = 4;
};

/** A type-1 MIDI file: Soprano, Alto, Tenor and Bass on channels 1-4 (as "Parts on ch. 1-4"
    plays them back), the sung melody on 5 and the chords, with their names, on 6. It starts at
    the song's start when the take was learned in time with the song, else at the take's start.
    separateSyllables: a new note for every sung note (Words), else held notes stay tied. */
juce::MidiFile makeTakeMidi (const sc::TakeAnalysis& take, const sc::LearnedScore& score, const TakeTiming& timing,
                             bool separateSyllables);

/** 24-bit stereo WAV, written a block at a time. */
class StereoWavWriter
{
public:
    /** Replaces the file. */
    bool open (const juce::File& file, double sampleRate);
    bool writeSilence (int64_t numSamples);
    bool write (const float* left, const float* right, int numSamples);
    /** Finishes the file (also on destruction); deletes it if anything failed. */
    bool close();
    ~StereoWavWriter() { close(); }

private:
    juce::File file;
    std::unique_ptr<juce::AudioFormatWriter> writer;
    bool ok = false;
};
} // namespace sanctuary
