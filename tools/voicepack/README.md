# Sanctuary Voices pack builder

Rebuilds `voices/SanctuaryVoices` from the [VocalSet](https://zenodo.org/records/1442513) singing dataset (CC BY 4.0).

1. Get VocalSet 1.1/1.2 and unzip it. The tool reads each singer's `long_tones/straight`, `scales/straight`, `arpeggios/straight`, `scales/fast_forte`, `scales/fast_piano`, `scales/breathy`, `arpeggios/breathy`, `scales/belt` and `arpeggios/belt` folders (about 900 MB).
2. Build and run the tool against the dataset's `FULL` folder:

   ```bash
   g++ -std=c++20 -O2 -Icore/include -Itests tools/voicepack/BuildVoicePack.cpp tests/TestSignals.cpp \
       build/core/libsanctuary_core.a -o voicepack -lpthread
   ./voicepack /path/to/VocalSet/FULL /tmp/pack_wav
   ```

   It sorts everything into sections by voice and pitch (women: sopranos/altos, men: tenors/basses; the countertenor is left out) and writes, under `<Section>/<vowel>/`:

   | Folder | What | Kept |
   |---|---|---|
   | (the folder itself) | steady held notes, `<singer>_<vowel>_<n>_m<pitch*100>.wav` | up to 3 singers per semitone |
   | `short/` | notes sung from silence, cut with their onset (up to 0.45 s) | 2 per semitone, the quickest to speak first |
   | `legato/` | one note sliding into the next, `..._m<from*100>_to<to*100>_x<ms>.wav`, where `x` is when the pitch is halfway there | 1 per starting semitone and interval |
   | `breathy/`, `belt/` | the same three, from the breathy and belted scales and arpeggios | 2 held, 1 onset, 1 slide |

   The plug-in reads all of this from the paths and names, so nothing is analysed when it loads.
3. Encode to Ogg Vorbis (quality 5) into `voices/SanctuaryVoices`, keeping the folder layout, for example with
   `ffmpeg -i in.wav -c:a libvorbis -q:a 5 out.ogg`. Keep `CREDITS.txt`.

The pack is about 41 MB on disk and about 200 MB in memory once loaded, which takes a few seconds on several threads. Every instance of the plug-in in a project shares one copy.
