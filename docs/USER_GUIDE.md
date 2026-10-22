# Sanctuary Choir: user guide

## 1. Install

1. Open the repository's **Actions** tab, click the newest green **Build**, and download the zip for your computer under *Artifacts*.
2. Run the installer inside the zip:
   - **Windows**: run `Sanctuary Choir Setup.exe`. If SmartScreen appears, choose *More info → Run anyway*.
   - **Mac**: open `Sanctuary Choir.pkg`. The first time, right-click → *Open*, or go to *System Settings → Privacy & Security → Open Anyway*.
3. Restart your DAW and let it rescan plug-ins.

## 2. Put it on your vocal

| DAW | Where to find it | Tip |
| --- | --- | --- |
| Logic Pro / GarageBand | Audio FX slot on the vocal track → *Audio Units → Sanctuary Audio → Sanctuary Choir* | Logic only: to steer it with MIDI chords, insert it on a software-instrument track as an *AU MIDI-controlled Effect* and pick the vocal track as its side-chain input. |
| Ableton Live | Audio Effects → Plug-Ins → *Sanctuary Choir* (VST3) | For MIDI chords, set a MIDI track's *MIDI To* to the vocal track → Sanctuary Choir. |
| FL Studio | Mixer insert → *Sanctuary Choir* (VST3) | Route a MIDI Out channel to the plug-in's MIDI input port for chords. |
| Cubase / Studio One / Reaper | Insert effect on the vocal track (VST3) | Reaper: add a MIDI send from a MIDI track to the vocal track. |
| No DAW | Run the standalone app, pick your mic in *Options → Audio/MIDI Settings* | Use headphones to avoid feedback. |

Pro Tools (AAX) isn't supported yet.

## 3. Sing

- **Turn up Choir Size** to add singers. You'll see each one walk into the loft.
- **Key** is *Auto* by default: Sanctuary listens to your melody and works out the key. If you already know the key, set it yourself for instant, perfect harmony from the first note.
- **Harmony Mode** chooses the arrangement. *Full Choir* is the classic four-part hymn sound.
- **Style** changes how the choir performs. *Gospel* adds 7th chords, scoops and more vibrato; *Gregorian* gives pure, still voices.
- **Vocal Type**:
  - *Words* keeps your lyrics: every singer is your own voice, re-pitched.
  - *Ah, Oh, Oo, Ee, Eh* sing pure vowels.
  - *Mm* hums.

### Latency: Studio or Live

To re-pitch your voice, the choir has to hear a little of it first, so it runs about 39 ms behind you. Pick how that delay is handled under **Settings → Monitoring**:

| Mode | What you hear | Use it for |
| --- | --- | --- |
| **Studio** (default) | Your voice and the choir line up exactly. The plug-in reports its 39 ms to your DAW, which compensates on playback. While recording, though, you hear yourself 39 ms late. | Mixing, and singing over a track you already recorded. |
| **Live** | Your own voice comes through instantly. The vowel choir (Ah, Oh, Oo, Ee, Eh, Mm) follows about 15 ms behind, *Words* about 39 ms behind, like a choir following its soloist. | Recording, rehearsing and performing. |

- Learned and rendered takes stay in sync with your song in both modes.
- The standalone app always runs in **Live** mode.
- Your audio interface adds its own delay on top. Use a buffer size of 128 or 256 samples while you record.
- Switch back to **Studio** before you bounce or mix, so the live choir lines up with the beat.

## 4. Learn and Render (best quality)

1. Press **LEARN**. It glows: "LEARN ARMED".
2. Press play in your DAW. Sanctuary records your vocal track as it plays ("LEARNING 0:12").
3. Press **LEARN** again, or stop the DAW. Sanctuary studies the whole take: every note, the key, and where each phrase starts and ends. The plaque shows something like *LEARNED 0:43 · G MAJOR · 4 PHRASES · 105 NOTES*.
   - From now on, when the song plays, the choir uses this smarter harmony. It sees each phrase in advance, lands cadences on the right chord, and keeps the voices moving smoothly.
4. Press **RENDER** to sing every voice at the highest quality. Playback then uses the rendered choir, in sync with your song, and costs almost no CPU.
   - If you later change the harmony or choir settings, RENDER glows again. Press it to update.

Other buttons:

- **LOAD** learns from an audio file instead, such as an a cappella stem.
- **CLEAR** forgets the take.
- **Settings → Export Stems** writes Sopranos, Altos, Tenors, Basses, Room and Full Choir WAVs to *Documents/Sanctuary Choir/Exports*.

The learned take is saved inside your project.

## 5. More things to try

- **FREEZE**: hold a big final chord, then keep it ringing while you sing over it.
- **Response: Echo** (MIX page, or the *Call & Response* preset): the choir answers each phrase right after you sing it.
- **MIDI chords**: hold chords on a keyboard to decide the harmony yourself. With no voice coming in, the choir becomes an instrument you can play.
- **Section panels**:
  - Click a singer icon to mute that section; double-click to solo it.
  - Drag a keyboard to change that section's range.
  - Drag the coloured bar to set the section's level.
- **FX page**: choose the room, from a vast Grand Cathedral to a dry Studio Room, and shape the choir bus (low/high cut, warmth, glue).
