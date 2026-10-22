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
| Cubase / Studio One / Reaper | Insert effect on the vocal track (VST3) | Cubase records vocals on *mono* tracks, and a mono track can't carry a stereo choir. Use a stereo audio track, or put Sanctuary Choir on an FX channel fed by a send. Reaper: add a MIDI send from a MIDI track to the vocal track. |
| No DAW | Run the standalone app, pick your mic in *Options → Audio/MIDI Settings* | Use headphones to avoid feedback. |

Pro Tools (AAX) isn't supported yet.

**Use a stereo track.** The choir is spread across a stereo stage. On a mono track it gets folded to mono, and Sanctuary shows a reminder when that happens.

## 3. Sing

- **Turn up Choir Size** to add singers. You'll see each one walk into the loft. While your song plays, the singers move with its beat (each a touch loose, like real people); when the transport stops they sway on their own.
- **Key** is *Auto* by default: Sanctuary listens to your melody and works out the key, usually within the first phrase or two. It remembers the key with your project, so the next playback starts right. If you already know the key, set it yourself for perfect harmony from the very first note. Once Auto has found it, click the small **lock** next to *Auto · E♭* to keep it.
- **Harmony Mode** chooses the arrangement. *Full Choir* is the classic four-part hymn sound.
- **Style** changes how the choir performs. *Gospel* adds 7th chords, scoops and more vibrato; *Gregorian* gives pure, still voices.
- **Vocal Type**:
  - *Words* keeps your lyrics: every singer is your own voice, re-pitched. Sanctuary hears whether you have a low or a high voice. With a man singing lead, the sopranos and altos get a woman's vocal tract and lose the male "ring"; with a woman singing lead, the tenors and basses gain it.
  - *Ah, Oh, Oo, Ee, Eh* sing pure vowels.
  - *Mm* hums.
  - The vowel modes are sung by **real recorded singers** (Sanctuary Voices, installed with the plug-in). *Settings → Choir Voices* switches between **SANCTUARY** (recorded), **YOUR OWN…** (your singers; see [Recording your own choir voices](RECORDING_YOUR_CHOIR.md)) and **SYNTH** (the synthesised voices). *Mm* (humming) is always synthesised.

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
   - If the song changes key, for example a last chorus a step up, Sanctuary follows it: the plaque shows *G MAJOR → A MAJOR*. One phrase with a few odd notes doesn't count as a key change. Choosing a Key yourself turns this off.
4. Press **RENDER** to sing every voice at the highest quality. Playback then uses the rendered choir, in sync with your song, and costs almost no CPU.
   - If you later change the harmony or choir settings, RENDER glows again. Press it to update.
   - You can press **RENDER** while still learning: Sanctuary finishes the take, studies it, then renders it.

Other buttons:

- **LOAD** learns from an audio file instead, such as an a cappella stem.
- **CLEAR** forgets the take.
- **Settings → Export Stems** writes Sopranos, Altos, Tenors, Basses, Room and Full Choir WAVs to *Documents/Sanctuary Choir/Exports*.

The learned take is saved inside your project. If you reopen the project at another sample rate, Sanctuary converts the take so it stays in tune and in time.

## 5. Follow the band

Sanctuary can listen to your band, keys or backing track and sing **the chords they play**, instead of guessing them from your melody. A melody alone often can't tell an F chord from a D minor; the band can.

1. Send the band to Sanctuary's **side-chain** input. A chordal part works best: keys, guitar, pads, or a backing track without vocals. The full mix also works.
   - **Cubase / Nuendo**: in the plug-in window, switch on side-chain (the side-chain button at the top). On the band's track or group, add a send to *Sanctuary Choir – Side-Chain*.
   - **Logic Pro**: in the Sanctuary Choir window, pick the band's track or bus in the **Side Chain** menu at the top right.
   - **Studio One**: click **Side-Chain** at the top of the plug-in window and tick the band's track, or add a send to the plug-in's side-chain.
   - **Reaper**: send the band's track to channels 3/4 of the vocal track. The plug-in's side-chain uses inputs 3/4 (check the pin connector if needed).
   - **Ableton Live 11.1 or later**: open the plug-in's side-chain section in the device and choose the band's track under *Audio From*.
   - **FL Studio**: route the band's mixer track to the vocal's mixer track as a side-chain, then map it to the plug-in's side-chain input in the wrapper's *Processing* tab.
   - Menus differ between DAW versions. Look for "side-chain" in your DAW's manual if yours looks different.
2. **Settings → Follow the band** is on by default. The Settings page shows what Sanctuary hears, and the main page shows the band's chord with a **BAND** tag, e.g. *G/B*.
3. Sing as usual. The choir sings the band's chords, follows inversions (the choir's basses sing the band's bass note) and uses the band to find the key.

**Live** the choir takes about a third of a second to follow a chord change. **Chord Speed** (MIX) sets the balance: turn it up for quicker changes, down for steadier chords with busy players. For exact timing, use **LEARN** with the band on the side-chain. Sanctuary records the band along with your voice, finds every chord change in it, and harmonises each note with the chord under it. The plaque then shows *· WITH THE BAND*. The band's chords are saved with your project.

MIDI chords still come first: hold a chord and the choir sings it, whatever the band plays.

## 6. More things to try

- **Presets** (top left) come in categories: Sacred & Classical, Worship, Gospel, Cinematic, Pop & Backing, Pads & Drones, Rhythmic, and Kids & Crowds. Point at a category to see its presets. Presets change the choir's sound, not your song: the Key and Scale you set stay as they are.
- **Ensemble** (MIX page) chooses who sings:
  - **Choir**: an adult choir.
  - **Children**: light, straight voices; every part is sung by trebles.
  - **Gang**: shouted gang vocals, punchy and loose with no vibrato. Try it with Unison and Words.
  - **Congregation**: untrained voices, wide tuning and late entrances, like a church full of people.
- **Rhythm** (FX page) gates the choir in time with your song, following your DAW's tempo:
  - **Pulses, Offbeats, Dotted, Gallop, Stutter and Trance Gate** chop the whole choir, or only the upper or lower sections (SECTIONS).
  - **Arp Up, Arp Down and Arp Bounce** pass the chord from section to section, like an arpeggio.
  - **Call & Answer**: the upper voices sing the first half of the bar, the lower voices the second.
  - **RATE** sets the speed (1/8, 1/16 or 1/32), **LENGTH** how long each note lasts, and **DEPTH** how far the gate closes.
  - Rendered takes play the rhythm live. Exported stems are without it.
- **Scenes** (Settings page) store up to 8 sounds per song, such as verse, chorus and bridge. Press **STORE**, then a slot. Click a slot to bring it back, or automate the **Scene** parameter in your DAW so the choir changes with the song.
- **FREEZE**: hold a big final chord, then keep it ringing while you sing over it.
- **Chord Speed** (MIX page) sets how often the choir changes chord. Turn it down and quick passing notes ride over one chord, like a real choir; turn it up and every note gets its own chord.
- **Response: Echo** (MIX page, or the *Call & Response* preset): the choir answers each phrase right after you sing it.
- **MIDI chords**: hold chords on a keyboard to decide the harmony yourself. With no voice coming in, the choir becomes an instrument you can play.
- **MIDI parts** (Settings → *Parts on channels 1–4*): write each part yourself. Notes on channel 1 are sung by the sopranos, channel 2 the altos, channel 3 the tenors and channel 4 the basses. Other channels still work as chords.
- **Program Change** messages pick a preset by its number in the list, so a MIDI controller or a DAW track can switch sounds.
- **Section panels**:
  - Click a singer icon to mute that section; double-click to solo it.
  - Drag a keyboard to change that section's range.
  - Drag the coloured bar to set the section's level.
- **FX page**: choose the room, from a vast Grand Cathedral to a dry Studio Room, and shape the choir bus (low/high cut, warmth, glue).
