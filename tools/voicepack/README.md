# Sanctuary Voices pack builder

Rebuilds `voices/SanctuaryVoices` from the [VocalSet](https://zenodo.org/records/1442513) singing dataset (CC BY 4.0).

1. Get VocalSet 1.1/1.2 and unzip it (only the `long_tones/straight`, `scales/straight` and `arpeggios/straight` folders are used, about 260 MB).
2. Build and run the tool against the dataset's `FULL` folder:

   ```bash
   g++ -std=c++20 -O2 -Icore/include -Itests tools/voicepack/BuildVoicePack.cpp tests/TestSignals.cpp \
       build/core/libsanctuary_core.a -o voicepack -lpthread
   ./voicepack /path/to/VocalSet/FULL /tmp/pack_wav
   ```

   It keeps steady, clean held notes, sorts them into sections by voice and pitch (women: sopranos/altos, men: tenors/basses; the countertenor is left out), keeps up to three singers per semitone, and writes `<Section>/<vowel>/<singer>_<vowel>_<n>_m<pitch*100>.wav`.
3. Encode to Ogg Vorbis (quality 5) into `voices/SanctuaryVoices`, keeping the folder layout, for example with
   `ffmpeg -i in.wav -c:a libvorbis -q:a 5 out.ogg`.
