# Sanctuary Choir

**Turn one voice into an entire choir.** Sanctuary Choir is an audio plugin (VST3 / AU / Standalone) that listens to your singing and surrounds it with a full SATB choir: sopranos, altos, tenors and basses, harmonised in your key, placed on a cathedral stage and bathed in a real-sounding room.

![Sanctuary Choir](design/mockups/perform-33-singers.png)

## Features

**Perform page**
- **Choir Size** adds singers one at a time (up to 40). You watch them walk into the choir loft as you turn the knob.
- **Volume, Space, Reverb, Tone, Presence** shape the sound. Space spreads the singers across the stage and adds early reflections.
- **Harmony Mode**:
  - Full Choir: classic four-part hymn voicing with voice leading.
  - Gospel Stack: close harmony with 7ths.
  - Open Voicing.
  - Unison & Octaves.
  - Thirds & Sixths.
  - Organum: medieval parallel fourths over a drone.
  - Upper Voices (SSAA) and Lower Voices (TTBB).
  - Drone Pad.
  - MIDI Chords.
- **Key / Scale**: Auto key detection, or pick from 12 keys and 9 scales:
  - Major, Minor, Harmonic Minor
  - Dorian, Mixolydian, Lydian, Phrygian
  - Gospel/Blues, Chromatic
- **Style** sets how the choir performs (vibrato, looseness, scoops, chord colour): Sacred, Gospel, Gregorian, Cinematic, Worship, Chamber, Spiritual.
- **Vocal Type**:
  - Ah, Oh, Oo, Ee, Eh: pure sung vowels, synthesised per section with real SATB formants.
  - Mm: humming.
  - Words: the choir sings *your lyrics* (your own voice, pitch-shifted and re-voiced per singer).
- **Section panels** for Soprano / Alto / Tenor / Bass:
  - Each panel has a level bar, a live waveform, and a keyboard that lights up the notes being sung.
  - Drag on a keyboard to set that section's singing range.
  - Click the singer icon to mute the section; double-click to solo it.
- **LEARN / LOAD / CLEAR / RENDER**:
  - LEARN: press it and play your song. Sanctuary studies the whole take (notes, phrases, key) and writes smarter harmony with look-ahead: cadences at phrase ends, steady octaves, proper voice leading.
  - RENDER: renders every singer at the highest quality. The result plays back in sync with your DAW timeline.
  - The take is saved with your project.
- **FREEZE** holds the current chord forever, for pads, endings and transitions. In Words mode it sustains your own vowel.

**Beyond the original idea**
- **Response: Echo** (call & response). The choir waits while you sing a phrase, then answers it right back in harmony, gospel style. Switch it on in the MIX page or load the *Call & Response* preset.
- **MIX page**:
  - Per-section fader, pan, mute/solo and singing range.
  - Lead vocal and choir levels.
  - Humanize, Vibrato, Glide, Formant (gender/size), Pitch Lock and Breath.
- **FX page**:
  - Six sacred spaces: Grand Cathedral, Stone Abbey, Wooden Chapel, Concert Hall, Gospel Church, Studio Room.
  - Decay, pre-delay and brightness.
  - Lead reverb send.
  - Low/high cut, Warmth and a Glue compressor on the choir bus.
- **MIDI**: hold chords on a keyboard to dictate the harmony. With no voice coming in, the choir becomes a playable instrument.
- **Live monitoring**: your voice comes through instantly while you record; the choir follows about 15 ms behind (Settings → Monitoring).
- **Sanctuary Voices**: real recorded singers sing the vowel modes (Ah, Oh, Oo, Ee, Eh): 1,703 held notes from 19 professional singers, sorted into SATB sections, plus their real onsets and note-to-note slides, so legato lines join up and short notes start like sung notes. **Singing** (MIX page) switches them between natural, breathy and belted takes. Installed with the plug-in; switch back to the synthesised voices any time (Settings → Choir Voices).
- **Your own choir**: record a few held notes per section and the vowel modes sing with those real voices ([guide](docs/RECORDING_YOUR_CHOIR.md)).
- **Voice character**: in Words mode the upper sections sound like women when a man sings lead (and the lower sections like men with a female lead).
- **Chord Speed** and a one-click **key lock** for the detected key.
- **Chord lane**: after LEARN, see every chord of the take over your melody. Click one for ranked alternatives (with how well each fits the melody), pick one, and the harmony around it is worked out again. Chosen chords are kept with the project.
- **Drag-out stems and MIDI**: drag the parts (SATB on channels 1–4, melody, named chords) as MIDI, or the rendered Full Choir and section stems as 24-bit WAVs, straight into your DAW. They line up when dropped at the start of the song. Or save them all to a folder.
- **Ensemble**: an adult choir, a children's choir (every part sung by trebles), shouted gang vocals, or a whole congregation.
- **Rhythm** (FX page): tempo-synced gates and section arpeggios that follow your DAW: pulses, offbeats, gallops, trance gates, Arp Up/Down/Bounce and Call & Answer.
- **Follows the band**: route your band, keys or backing track to the side-chain and the choir sings the chords they play, inversions included. LEARN records the band too and harmonises every note with the chord under it.
- **Key changes**: LEARN follows a song that modulates (G MAJOR → A MAJOR) and harmonises each part in its own key.
- **MIDI parts and Program Change**: channels 1–4 sing exactly the soprano, alto, tenor and bass notes you write; Program Change picks a preset.
- **Sung words for MIDI**: type "Kyrie eleison", "Alleluia" or "Glory to God" and the MIDI choir sings it, a syllable per note, with real vowels and sung consonants (church Latin or English).
- **Scenes**: 8 stored sounds per song (verse, chorus, bridge), recalled with a click or by automating the Scene parameter.
- **The stage and its microphones**: drag the sections around the choir loft (left/right, forward/back, wide/narrow) or pick a formation, and mix Close, Tree and Room microphones like a recording engineer. Rendered takes follow without rendering again.
- **67 factory presets** in 8 categories (Sacred & Classical, Worship, Gospel, Cinematic, Pop & Backing, Pads & Drones, Rhythmic, Kids & Crowds) plus your own.

## Download

Every push builds the plug-in on GitHub Actions for **macOS (universal), Windows and Linux**:
1. Open the repository's **Actions** tab and pick the latest green **Build** run.
2. Download `Sanctuary-Choir-macOS`, `Sanctuary-Choir-Windows` or `Sanctuary-Choir-Linux` under *Artifacts*.
3. Run the installer inside the zip (`Sanctuary Choir Setup.exe` on Windows, `Sanctuary Choir.pkg` on Mac) or follow `INSTALL.txt`.

New here? Read the [user guide](docs/USER_GUIDE.md).

Pushing a tag such as `v1.0.0` also publishes the zips as a GitHub Release.

## Building from source

Requirements: CMake 3.22+, a C++20 compiler (Xcode 15+, Visual Studio 2022, GCC 11+/Clang 14+). JUCE 9 is downloaded automatically.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release          # DSP unit tests
```

- **macOS universal**: add `-G Ninja "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"`.
- **Windows**: install the WebView2 SDK first (`nuget install Microsoft.Web.WebView2 -OutputDirectory C:/webview2`) and add `-DJUCE_WEBVIEW2_PACKAGE_LOCATION=C:/webview2`.
- **Linux**: install the packages listed in `.github/workflows/build.yml`.

The plug-ins land in `build/plugin/SanctuaryChoir_artefacts/Release/`.

There is also a command-line renderer, handy for testing the engine without a DAW:

```bash
./build/tools/render/sanctuary_render vocal.wav choir.wav --learn --voice words --style gospel --size 60
```

## How it works

```
input ─► YIN pitch tracker ─► harmoniser ─────────► control timeline (notes per section, per 5 ms)
   │        (key detection)    (live, or the                     │
   │                            learned score)                   ▼
   └────► PSOLA analyser ─► up to 40 singers ─► S/A/T/B buses ─► stage · Close/Tree/Room mics ─► tone · glue ─► cathedral FDN reverb ─► + lead ─► limiter
                            (Words: pitch-shifted grains of your voice;
                             vowels: glottal source + SATB formant filters;
                             each singer with its own timing, detune, drift and vibrato)
```

| Folder | What's inside |
| --- | --- |
| `core/` | The DSP engine in plain C++20 (no framework). Pitch tracking, PSOLA, vowel synthesis, music theory and voice leading, harmoniser, reverb, Learn analysis and offline rendering. |
| `plugin/` | The JUCE wrapper: parameters, presets, the Learn session (timeline capture, background analysis/render, playback, saving takes with the project) and the WebView editor. |
| `ui/` | The interface: HTML/CSS/JS drawn over artwork taken from the design mockups. Opening `ui/index.html` through a local web server runs it in a browser with a simulated engine. |
| `tools/art/` | Scripts that turn the mockups into the UI skin and the singer sprites. |
| `tests/` | DSP unit tests. These cover pitch accuracy, voicing rules, key detection, section pitches in every mode, latency alignment, learn and render, and a CPU budget check. |

The plugin reports its latency (about 39 ms) so your DAW keeps everything in time. For recording and live use, **Settings → Monitoring → Live** passes your voice through with no delay at all and runs the vowel choir about 15 ms behind you (Words keep the full 39 ms); the plugin then reports zero latency. The standalone app always runs live.

## Licensing notes

- Sanctuary Choir is built on [JUCE](https://juce.com). JUCE is available under the AGPLv3 or a commercial JUCE licence (free "Personal" tier for small revenue). If you sell the plug-in closed-source, get the appropriate JUCE licence.
- Fonts: Cinzel, Cormorant Garamond and Montserrat (SIL Open Font License, see `ui/assets/fonts`).
- Sanctuary Voices (`voices/SanctuaryVoices`) are cut from [VocalSet](https://zenodo.org/records/1442513) by Julia Wilkins, Prem Seetharaman, Alison Wahl and Bryan Pardo (ISMIR 2018), licensed [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). Credit them wherever you distribute the plug-in; `voices/SanctuaryVoices/CREDITS.txt` says what was changed. `tools/voicepack` rebuilds the pack from the dataset.
- Artwork: the UI is built from the Sanctuary Choir design mockups in `design/mockups/`.
