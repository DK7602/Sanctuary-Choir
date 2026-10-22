# Voice prints

`build_voiceprints.py` measures how each VocalSet singer differs from the average singer of
their sex, and writes `core/src/VoicePrints.inc`. In Words mode the engine gives every choir
member one of these prints (the **Real Voices** knob sets how far), so the choir sounds like
different people instead of copies of the lead.

A print holds:

| | measured from | used as |
|---|---|---|
| formant scale | the singer's spectral envelope against the average, over the five vowels | the PSOLA formant factor (vocal tract length) |
| tone (6 bands) | what remains of the envelope after the formant scale | a per-singer EQ: low shelf 180 Hz, peaks 420 / 850 / 1700 / 2900 Hz, high shelf 5 kHz |
| vibrato | rate and width in their vibrato scales | the singer's vibrato, against the average |
| centre | the median pitch they sang | (for reference) |

The envelope of each singer and vowel comes from the harmonics of their straight-tone long
tones, scales and arpeggios. Every steady 20 ms frame gives the level of each harmonic, and
these are averaged into 1/6-octave bins. Many pitches sample the envelope at many frequencies,
so it is the vocal tract's response and not one note's harmonics. The EQ is fitted from 300 Hz
(women) or 170 Hz (men) up, where there are harmonics to measure. It is limited to +/-5 dB, and
to +/-4 dB on the shelves, which partly measure the microphone. The countertenor (male9) is
left out.

## Running

It needs the VocalSet `FULL` folder, or just the `long_tones/straight`, `scales/straight`,
`arpeggios/straight` and `scales/vibrato` folders of each singer, and numpy, scipy and
soundfile (the art pipeline's virtualenv has them):

```sh
tools/art/.venv/bin/python tools/voiceprints/build_voiceprints.py /path/to/VocalSet/FULL core/src/VoicePrints.inc
```

It takes about two and a half minutes and prints each singer's print.

VocalSet: Wilkins, Seetharaman, Wahl and Pardo (ISMIR 2018), https://zenodo.org/records/1442513,
CC BY 4.0.
