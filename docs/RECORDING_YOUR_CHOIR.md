# Recording your own choir voices

Sanctuary Choir's vowel modes (Ah, Oh, Oo, Ee, Eh, Mm) can sing with recordings of real singers instead of the built-in synthesised voices. Record a few held notes per section, point Sanctuary at the folder, and the whole choir sings with those voices, in any key and with the harmony it writes for you.

You don't need a studio or a big session. One singer per section already works. Two to four singers per section, each recorded on their own, gives a fuller and more natural choir.

## What to record

For each section, sing **held notes on one vowel**, stepping up through the section's range:

| Section | Range | Suggested notes |
| --- | --- | --- |
| Soprano | C4 – A5 | C4, D#4, F#4, A4, C5, D#5, F#5, A5 |
| Alto | F3 – D5 | F3, G#3, B3, D4, F4, G#4, B4, D5 |
| Tenor | C3 – A4 | C3, D#3, F#3, A3, C4, D#4, F#4, A4 |
| Bass | E2 – E4 | E2, G2, A#2, C#3, E3, G3, A#3, C#4, E4 |

- **Hold each note for 3–4 seconds** at a steady, comfortable volume (mezzo-forte). Don't swell or fade.
- **Breathe between notes**: leave half a second to a second of silence. Sanctuary finds and cuts the notes by itself.
- **Sing with your natural, light vibrato.** A straight tone also works.
- **Start each note cleanly on pitch.** Avoid scooping up into it. A piano or tuner app for the reference pitch helps.
- Every 2–3 semitones is plenty. Sanctuary fills in the notes between.

Start with **Ah**. Add **Oo** and **Mm** (humming) later if you like; any vowel you haven't recorded falls back to Ah.

## Recording tips

- A quiet room with little echo. Dry recordings are best, because Sanctuary adds the cathedral.
- About 50 cm (20 inches) from the microphone, and keep the distance steady.
- Leave headroom: peaks around −10 dBFS, never clipping.
- Any sample rate, mono or stereo, as WAV, AIFF or FLAC.

## Naming the files

Sanctuary reads the **section** and the **vowel** from the folder and file names, so any of these work:

```
My Choir/
  Sopranos/soprano_ah_mary.wav
  Sopranos/soprano_ah_grace.wav      <- a second singer: a different voice in the choir
  Altos/alto_ah.wav
  Tenors/tenor_ah.wav
  Tenors/tenor_oo.wav
  Basses/bass_ah.wav
  Basses/bass hum.wav                <- "hum" or "mm" is the humming voice
```

- Section words: `soprano`, `alto`, `tenor`, `bass`.
- Vowel words: `ah`, `oh`, `oo`, `ee`, `eh`, `mm` (or `hum`).
- One file can hold all the notes of a section and vowel, or you can use one file per note. Both work.

## Loading them

1. Open **Settings → Choir Voices → LOAD RECORDINGS** and choose the folder (`My Choir` above).
2. Sanctuary cuts, tunes, loops and levels every note. The panel then shows something like *My Choir · 4 SECTIONS · 2 VOWELS · 64 NOTES*.
3. Choose a vowel **Vocal Type** (Ah, Oo…). The choir now sings with your singers. **Words** mode still uses your own lead voice.
4. **BUILT-IN** switches back to the synthesised voices.

Your project remembers the folder, so keep it where it is (or load it again after moving it). If you rendered a take before loading the voices, press **RENDER** again to hear the take with them.

## Permission

The voices belong to the people who sang them. If you share or sell anything made with them, ask your singers first.
