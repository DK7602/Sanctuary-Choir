# Sanctuary Choir: user guide

The plug-in has its own manual: **Settings → Manual → Open the manual**, or the **gear** at the top left → *The manual*. It explains every part of Sanctuary Choir step by step, with a numbered tour of the main page, recipes, troubleshooting and a search. This guide covers the same ground for reading outside the plug-in.

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
- **Scale** is *Auto* by default too: Sanctuary hears whether your song is major or minor, and in a minor song whether you sing the raised seventh (harmonic minor) or not (natural minor). The field shows what it heard, e.g. *Auto · Minor*. With the key set by hand, Auto still decides major or minor for it. Locking the key also locks the scale it heard. Choose a scale yourself for the modes (Dorian, Mixolydian...), Gospel (blues) or Chromatic.
- **Harmony Mode** chooses the arrangement. *Full Choir* is the classic four-part hymn sound.
- **Style** changes how the choir performs. *Gospel* adds 7th chords, scoops and more vibrato; *Gregorian* gives pure, still voices.
- **Vocal Type**:
  - *Words* keeps your lyrics: every singer is your own voice, re-pitched. Sanctuary hears whether you have a low or a high voice. With a man singing lead, the sopranos and altos get a woman's vocal tract and lose the male "ring"; with a woman singing lead, the tenors and basses gain it.
  - In *Words* each singer also takes on the voice of one of 19 real singers: the size of their vocal tract, the colour of their voice and their vibrato, measured from their recordings. The choir sounds like different people singing your words, not copies of you. The sopranos and tenors get the singers with the shortest vocal tracts, the altos and basses the longest. **Real Voices** (MIX page) sets how far: 0% makes every singer your own voice again, 150% exaggerates the differences.
  - The singers copy your singing, not your mouth noises: lip smacks and clicks are taken out of what they sing (your own voice stays untouched), so the choir stays clean even with the lead muted. Where your voice creaks or splits at the end of a phrase, the singers keep to the octave you were singing in.
  - *Ah, Oh, Oo, Ee, Eh* sing pure vowels.
  - *Mm* hums.
  - The vowel modes are sung by **real recorded singers** (Sanctuary Voices, installed with the plug-in). *Settings → Choir Voices* switches between **SANCTUARY** (recorded), **YOUR OWN…** (your singers; see [Recording your own choir voices](RECORDING_YOUR_CHOIR.md)) and **SYNTH** (the synthesised voices). *Mm* (humming) is always synthesised.
  - The recorded singers sing like singers, not like a keyboard. When the melody steps to the next note they **slide into it the way they were recorded sliding**, so legato lines join up. A note that starts from silence, such as a short or detached note, begins with a **real sung onset**.
  - **Singing** (top left of the MIX page) sets how they sing: **Natural**, **Breathy** (airy and soft) or **Belt** (bright and pressed, sung out). With Sanctuary Voices the singers switch to takes recorded that way. The synthesised voices change their tone to match. You can automate it, for example breathy verses and a belted last chorus.
  - On **Natural**, Sanctuary Voices sing the way a real choir does: most singers in a straight, blended tone, and some in their own classical voice with its natural vibrato, recorded that way. How many depends on the Style and the **Vibrato** knob: none in *Chamber* or *Gregorian*, about a fifth in *Sacred*, most of a *Gospel* choir; Vibrato at 0% keeps everyone straight. Up to five different singers are recorded on every note of every vowel, and the altos include a male alto (countertenor).
- **Energy** (the plaque at the top right of the church, opposite the room) is how hard the choir sings. Drag it up or down like a knob, or click its arrow to pick a level: *Hushed* and *Soft* sing quieter, airier and slower to speak; *Powerful* and *Massive* sing louder, brighter and pressed, and at the very top push the choir a little harder. The middle (*Natural*) leaves the style as it is. With Singing on Natural, the recorded voices also change takes: at the bottom the straight singers turn breathy and the classical ones sing softly; at the top a *Gospel*, *Worship* or *Spiritual* choir belts, the other styles open into full classical voice (*Gregorian* chant and a children's choir keep their own voice). Automate it to build a song from a hushed verse to a massive last chorus.
- **Sub-Octave** (MIX page) adds oktavists: basses singing the bass part an octave lower, the deep weight under a Russian or a gospel mass choir. One joins for every two basses. Turn it up from *Off* to bring them in; they sing everything the basses sing, so it works with every harmony mode (a children's choir has none). Sub-Octave opens the choir's **Low Cut** (FX page) in proportion, so the low notes come through. The *Requiem* and *Epic Cinematic* presets use it.

### Latency: Studio or Live

To re-pitch your voice, the choir has to hear a little of it first, so it runs about 39 ms behind you. Pick how that delay is handled under **Settings → Monitoring**:

| Mode | What you hear | Use it for |
| --- | --- | --- |
| **Studio** (default) | Your voice and the choir line up exactly. The plug-in reports its 39 ms to your DAW, which compensates on playback. While recording, though, you hear yourself 39 ms late. | Mixing, and singing over a track you already recorded. |
| **Live** | Your own voice comes through instantly (1.5 ms). The vowel choir (Ah, Oh, Oo, Ee, Eh, Mm) follows about 15 ms behind, *Words* about 40 ms behind, like a choir following its soloist. | Recording, rehearsing and performing. |

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

The learned take is saved inside your project. If you reopen the project at another sample rate, Sanctuary converts the take so it stays in tune and in time.

### Multi-take choir: sing the part again

In **Words** mode the choir sings your own voice. One take gives every singer the same breaths, the same timing and the same colour; real choirs sound bigger because every singer is a different performance. So sing the part again, up to four times in all, and the Words choir sings from every pass.

Open **Settings → Multi-Take** after LEARN:

1. Press **● RECORD** on the next empty card.
   - In your DAW: press play from a bar or two before the take and sing the part again, on the same track. It stops when your DAW stops, like LEARN.
   - In the standalone app: your take plays through the choir from the top for you to sing along with. Press **■ STOP** when you're done.
2. Sanctuary lines the pass up with your take, note by note ("LINING UP PASS 2 WITH YOUR TAKE..."). Sing it as you normally would: it follows your timing, and a pass that's a little early, late or quieter is fine. The card then shows how much of the take the pass sings and how far it had to be moved.
3. In each section the singers take the passes in turn: with three passes, a third of the sopranos sing from each one, and the same for every section. *Who sings from which pass* shows it.

**LOAD…** adds another recording of the same part from a file instead. **REMOVE** takes a pass away. A pass that leaves out most of the part, or sings a different one, is turned down ("That pass does not follow the take").

The passes are saved with the project, and taking them away or adding one makes RENDER glow in Words mode. They only change the Words choir: the vowel modes sound the same with or without them. **Use Words** on the page switches Vocal Type to Words. A new take starts again with one pass.

### Chords & Stems: change a chord, drag the parts into your DAW

Click the plaque after LEARN (*LEARNED 0:43 · ...*), or **Settings → Chords & Stems**. The page shows the take's chords in a lane, and under them your melody and the four parts the choir sings (soprano pink, alto gold, tenor blue, bass green), plus where the take starts in the song (e.g. *starts at bar 17*). Click **MELODY**, **SOPRANO**, **ALTO**, **TENOR** or **BASS** above the lane to hide or show a line.

**Change a chord**

1. Click a chord in the lane. A menu lists the chords that could go there, best first. The bar next to each one shows how much of your melody sits on its notes. The menu offers the key's chords, their colours (7ths, sus4) and borrowed chords such as bVII or iv.
2. Pick one. It turns gold with a padlock: that part of the song now sings your chord. Sanctuary works out the chords around it again so they lead in and out of it.
3. Click a gold chord and choose **Let the choir choose** to undo it. **Unlock all** undoes every chord you chose.

Your chords are saved with the project and win over everything else, including the band on the side-chain. Changing a chord makes RENDER glow; press it to hear the change in the rendered choir. Chords can be chosen in the chord harmony modes (Full Choir, Gospel Stack, Open Voicing and so on), not in Drone or MIDI Chords.

**Drag into your DAW**

Press on a chip, hold the mouse button, and drag it onto a track or an empty part of your DAW's arrangement:

- **MIDI**: one MIDI file, ready right after LEARN with no render needed. It has soprano, alto, tenor and bass on MIDI channels 1–4 (so *Parts on ch. 1–4* plays them back), your melody on channel 5, and the chords on channel 6 with their names. It uses your song's tempo. Use it with another choir or orchestra library, print a lead sheet, or edit the parts and send them back to Sanctuary.
- **Full Choir**, **Sopranos**, **Altos**, **Tenors**, **Basses**, **Room**, **Lead on Stage** and **All Stems**: 24-bit WAV files of the rendered choir, so they need a RENDER first. The page gets them ready in the background the first time it opens after a render ("Getting the audio ready…").
  - Each **section** file is that section where it stands on the stage, through the microphones, without the hall.
  - **Room** is the hall alone: the choir's reverb, to put under the sections and mix yourself.
  - **Lead on Stage** is your own voice where it stands (at the microphones, or on the stage: see [The stage](#6-the-stage-and-the-microphones)), with its reverb.
  - **Full Choir** is the whole choir as you hear it, with the hall.
  - **All Stems** is all of them at once.

When the take was learned with your DAW playing, the files start at the very beginning of your song. **Drop them at bar 1** and they line up with your take. A take loaded from a file starts at its own beginning; drop those files where the take starts.

Files are kept in *Documents/Sanctuary Choir/Exports*, one folder per take. A new render or mix gets new files, and files your DAW already uses are never overwritten. Delete old folders there when you no longer need them.

**Save to folder** writes everything (MIDI, sections, Room, Lead on Stage and Full Choir) and opens the folder, if you would rather drag from there. If your DAW doesn't accept the drag, Sanctuary opens the folder for you.

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

## 6. The stage and the microphones

Click the choir loft on the main page, **MIX → Stage & Mics**, or simply click **MIX** again while on the MIX page (clicking it once more goes back to the knobs), to see the stage from the hall: the four sections on their risers (sopranos in pink, altos in gold, tenors in blue, basses in green), further back higher up and smaller. Each section's name shows how many singers it has at the current Choir Size; a section with nobody singing is faded.

**Arrange the singers**

- **Drag a section** (grab its singers) to move it: left or right across the stage, and up the picture to move it back. Further back sounds further away: a little quieter, a little later and a little darker.
- **Drag a section's edges** (the round handles at the front corners of its risers) to make it wider or narrower. The coloured mark on the floor shows exactly how wide it stands. A narrow section sounds like one spot; a wide one surrounds you.
- **Formation** buttons set all four at once:
  - **Blocks** (the default): sopranos and altos in front, tenors and basses behind.
  - **Across**: one wide row, left to right.
  - **Men Centre**: women on the sides, men in the middle at the back.
  - **Mixed**: everyone spread across the whole stage, the most blended sound.
- **Space** on the main page makes the whole stage wider or narrower. The **PAN** knobs on the MIX page move the sections left and right too.

**Put yourself on the stage**

**YOU** (the lead singer in front of the stage, beside the tree, facing the choir) is your own voice. It starts at the microphones: dry and up front, like a lead vocal, with its own **Lead Reverb** (FX page).

- **Drag YOU onto the stage** to sing from there: in front of the choir, among the altos, at the back wall. On the stage he turns round to face the hall, and shows faintly through any singers standing in front of him. You are then placed and heard by the microphones like a section, and you are in the hall with the choir: further back sounds further away and more in the room. You are never late: the front of the choir stays in time with you.
- **Double-click YOU**, or drag it off the front of the stage, to go back to the microphones. Double-clicking at the microphones puts you at the front centre of the stage.
- **Lead on Stage**, **Lead Position** and **Lead Depth** can be automated, for a soloist who walks to the front for the last chorus.
- Where you stand belongs to your song: presets don't move you.

**Choose the microphones**

Three pairs of microphones pick up the choir. Mix them like a recording engineer:

- **Close**: spot mics in front of each section. Near, dry and detailed, and every row sounds as close as the front.
- **Tree**: the main pair above the conductor. The natural picture, as the audience hears it. This is the only pair up by default, so the choir sounds as it always has.
- **Room**: a pair far out in the hall. Wide and distant, with much more of the hall.

The **Natural**, **Intimate**, **Concert** and **Distant** buttons are good starting points. The **Intimate Chamber**, **Distant Cathedral** and **Choir in the Round** presets use them.

Moving the stage or changing the mics never needs a new RENDER: rendered takes follow straight away. Section stems you drag out of **Chords & Stems** come from where each section stands, through the mics, without the hall.

## 7. More things to try

- **The gear** (top left, beside the preset) opens a small menu: the manual, Settings, *Save this sound as a preset…* and the presets folder.
- **Presets** (top left) come in categories: Sacred & Classical, Worship, Gospel, Cinematic, Pop & Backing, Pads & Drones, Rhythmic, and Kids & Crowds. Point at a category to see its presets. Presets change the choir's sound, not your song: the Key and Scale you set stay as they are.
- **Ensemble** (MIX page) chooses who sings:
  - **Choir**: an adult choir.
  - **Children**: light, straight voices; every part is sung by trebles. They sound like children rather than grown-ups sped up: a child's pure tone (none of a trained singer's ring, no chest), a little higher in the vocal tract than the women, and some airy voices among them.
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
- **Sung words** (Settings → **SUNG WORDS**): type words and the MIDI choir sings them.
  - Type "Kyrie eleison", "Alleluia" or "Glory to God", or pick one of the ready texts.
  - Each new note or chord you play sings the next syllable, with its vowel and its consonants. The page shows how the words are split and lights up the syllable being sung.
  - With Sanctuary Voices, a syllable opening on a consonant the singers were recorded singing (D, B, M, N, L, R, P, T, K, S, Ch, J, Y, and Cr, Str, Dr) is sung from those recordings: a real singer's consonant, then the held vowel. The others are shaped on the voices as before.
  - Latin is sung the church way: Ky-ri-e, Chri-ste, ex-cel-sis ("ek-shel-sis"), gra-ti-a ("gra-tsi-a"). Anything else is read as English.
  - Write **-** inside a word to split the syllables yourself (Ho-san-na). Write **_** to hold a syllable over one more note: *A _ _ men* sings "A" across three notes, then "men".
  - The words start again from the top when you change them or your DAW starts playing, and they loop at the end. In Parts mode each part keeps its own place, so the sopranos can sing a melisma while the basses move on.
  - It works with the recorded voices and the synthesised ones. Clear the text to go back to the Vocal Type.
- **Section panels**:
  - Click a singer icon to mute that section; double-click to solo it.
  - Drag a keyboard to change that section's range.
  - Drag the coloured bar to set the section's level.
- **The room**: choose where the choir sings with the picker at the top left of the main page, or on the FX page. Each room has its own choir, and its reverb is a real room, measured: the sound of a spaced pair of microphones in the hall (hover over a room on the FX page to see which).
  - **Grand Cathedral**: a large robed choir with its conductor. Vast stone, a 5 s tail: the church of Santi Marcellino e Pietro in Cremona, heard from 21 m down the nave, its decay lengthened.
  - **Stone Abbey**: a robed choir in the cloisters. With the *Gregorian* style, monks sing there instead. The Gothic St. Nicholas Chapel of Aachen Cathedral, 4.3 s.
  - **Wooden Chapel**: a small choir, warm and close. The wood-panelled Brahmssaal of the Detmold University of Music, 1.5 s.
  - **Concert Hall**: a choir in black on risers, clear and wide. The Giovanni Arvedi Auditorium in Cremona, 2.1 s.
  - **Gospel Church**: a gospel choir in purple robes with its director, lively. The Cremona church again, nearer the front and with a shorter, fuller-house decay of 1.7 s.
  - **Studio Room**: five singers at their microphones, dry and intimate. BBC Maida Vale Studio 5.
  - Choir Size fills each room in turn, front row first.
- **FX page**: the reverb of the room, and the choir bus (low/high cut, warmth, glue). **Decay** stretches or shortens the room's own decay (30 to 250%), **Pre-Delay** waits before the room answers, **Brightness** darkens or brightens it (50% is the room as measured). Changing rooms lets the old one ring out briefly while the new one takes over. The measurements come from research datasets under Creative Commons Attribution licences; their authors are credited in ROOMS-CREDITS.txt next to the plug-in.
