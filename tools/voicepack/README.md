# Sanctuary Voices pack builder

Rebuilds `voices/SanctuaryVoices` from the [VocalSet](https://zenodo.org/records/1442513) singing dataset (CC BY 4.0).

1. Get VocalSet 1.1 (`VocalSet11.zip`, 2.1 GB) or 1.2 and unzip its `FULL` folder. The tool reads each singer's `long_tones/{straight,pp,forte}`, `scales/` and `arpeggios/` `{straight,fast_forte,fast_piano,breathy,belt,slow_piano,slow_forte}` folders. It leaves out the exaggerated *vibrato* (molto vibrato), trill, lip trill, vocal fry, inhaled and messa di voce takes, and the song excerpts.
2. Build and run the tool against the dataset's `FULL` folder:

   ```bash
   g++ -std=c++20 -O2 -Icore/include -Itests tools/voicepack/BuildVoicePack.cpp tests/TestSignals.cpp \
       build/core/libsanctuary_core.a -o voicepack -lpthread
   ./voicepack /path/to/VocalSet/FULL /tmp/pack_wav
   ```

   It sorts everything into sections by voice and pitch (women: sopranos/altos, men: tenors/basses, the countertenor `male9`: altos, from G#3 to C5) and writes, under `<Section>/<vowel>/`:

   | Folder | What | Kept |
   |---|---|---|
   | (the folder itself) | steady held notes in straight tone, `<singer>_<vowel>_<n>_m<pitch*100>.wav` | up to 5 singers per semitone (`kHeldStraight`) |
   | `short/` | notes sung from silence, cut with their onset (up to 0.45 s) | 3 per semitone, the quickest to speak first |
   | `legato/` | one note sliding into the next, `..._m<from*100>_to<to*100>_x<ms>.wav`, where `x` is when the pitch is halfway there | 2 per starting semitone and interval |
   | `breathy/`, `belt/` | the same three, from the breathy and belted scales and arpeggios | 2 held, 1 onset, 1 slide |
   | `vibrato/` | the same three in the singers' own classical voice, sung softly: slow piano scales and arpeggios, pianissimo long tones | 2 held, 2 onsets, 1 slide |
   | `fullvoice/` | the classical voice in full voice: slow forte scales and arpeggios, forte long tones | 2 held, 2 onsets, 1 slide |

   VocalSet's classical voice carries its vibrato (about 45 cents of pitch deviation against 15 for straight tone, measured from Annotated-VocalSet): a note counts as steady there when its centre does not drift between its two halves.

   The plug-in reads all of this from the paths and names, so nothing is analysed when it loads.
3. Resample to 32 kHz and encode to Ogg Vorbis (quality 5) into `voices/SanctuaryVoices`, keeping the folder layout, for example with
   `ffmpeg -i in.wav -af aresample=resampler=soxr:precision=28 -ar 32000 -c:a libvorbis -q:a 5 out.ogg`. Keep `CREDITS.txt`.

The pack holds about 10,000 recordings, 4,950 s in all: about 86 MB on disk and about 320 MB in memory once loaded (32 kHz keeps everything up to 16 kHz and a quarter of the memory 44.1 kHz would take). Loading takes a few seconds on several threads. Every instance of the plug-in in a project shares one copy.
