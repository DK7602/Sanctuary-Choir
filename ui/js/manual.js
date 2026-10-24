// Sanctuary Choir - the manual: every feature in plain words, opened from Settings or the gear.
(function () {
  "use strict";

  // How a control is written on screen, a tip, and a link that shows the page it talks about.
  const ui = (t) => '<span class="m-ui">' + t + "</span>";
  const tip = (html) => '<div class="m-tip"><span class="m-tip-label">Tip</span>' + html + "</div>";
  const note = (html) => '<div class="m-tip m-note"><span class="m-tip-label">Good to know</span>' + html + "</div>";
  const go = (page, label) => '<button class="m-go" data-go="' + page + '">' + label + " ›</button>";

  // The main page, numbered. Positions are in the window's own pixels (1448 x 1086).
  const PINS = [
    [57, 45, "The gear: the manual, settings and saving presets"],
    [382, 45, "Presets: pick a ready-made sound"],
    [425, 45, "Previous and next preset"],
    [1213, 13, "Pages: Perform, Mix, FX and Settings"],
    [494, 134, "The room the choir sings in"],
    [1166, 129, "Energy: how hard the choir sings"],
    [186, 199, "The key, and the chord being sung"],
    [1262, 199, "How many singers there are"],
    [725, 330, "The choir: click it to arrange the stage"],
    [137, 650, "Volume"],
    [328, 650, "Choir Size"],
    [520, 650, "Space"],
    [925, 650, "Reverb"],
    [1119, 650, "Tone"],
    [1312, 650, "Presence"],
    [284, 823, "Harmony Mode"],
    [534, 823, "Key"],
    [775, 823, "Scale"],
    [1050, 823, "Style"],
    [1362, 823, "Vocal Type"],
    [22, 910, "LEARN"],
    [22, 964, "LOAD"],
    [22, 1013, "CLEAR"],
    [722, 952, "The four sections"],
    [1428, 911, "FREEZE"],
    [1428, 991, "RENDER"],
  ];

  function tour() {
    let pins = "";
    PINS.forEach(([x, y, label], i) => {
      pins += '<span class="m-pin" data-pin="' + (i + 1) + '" title="' + label + '" style="left:' + (x / 14.48).toFixed(2) + "%;top:" + (y / 10.86).toFixed(2) + '%">' + (i + 1) + "</span>";
    });
    let legend = "";
    PINS.forEach(([, , label], i) => { legend += '<li data-pin="' + (i + 1) + '"><span class="m-num">' + (i + 1) + "</span>" + label + "</li>"; });
    return '<div class="m-figure"><img src="assets/manual/main-page.jpg" alt="The main page" draggable="false">' + pins + "</div>"
      + '<ol class="m-legend">' + legend + "</ol>";
  }

  const CHAPTERS = [
    {
      id: "welcome", title: "Welcome",
      summary: "Sanctuary Choir listens to you sing and surrounds your voice with a whole choir: sopranos, altos, tenors and basses, singing in harmony, in a real room.",
      body: `
<p>You sing one line. Sanctuary Choir works out the key and the chords, writes four-part harmony around your melody and has up to 40 singers sing it with you. You choose who sings (your own voice in four parts, or real recorded singers on vowels), the style, the room and how big the choir is.</p>
<h4>Two ways to use it</h4>
<ul>
<li><b>Live:</b> put it on your vocal and sing. The choir follows you as you go.</li>
<li><b>Learn and Render:</b> let it listen to your whole take first. It then knows every phrase in advance, chooses better chords and sings at the highest quality. This is the way to finish a song.</li>
</ul>
<h4>How to read this manual</h4>
<ul>
<li>The chapters on the left run from the first steps to the deep details. You don't need to read them in order.</li>
<li>Type in the search box at the top to find a word, such as <i>reverb</i>, <i>latency</i> or <i>MIDI</i>.</li>
<li>Names of buttons and knobs are written like ${ui("THIS")}, the way they appear on screen.</li>
<li>Buttons like ${go("mix", "Show me")} close the manual and take you to the page being described.</li>
<li>Almost everything in the plug-in also explains itself: rest the mouse on a control to see what it does.</li>
</ul>`,
    },
    {
      id: "quickstart", title: "Quick start", summary: "Your first choir in about a minute.",
      body: `
<ol class="m-steps">
<li><b>Put Sanctuary Choir on your vocal track</b> as an effect. Use a <b>stereo</b> track: the choir is spread across a stereo stage. (The next chapter shows where to find it in each DAW.)</li>
<li><b>Sing</b>, or play back a vocal you already recorded. You hear a choir around your voice straight away.</li>
<li><b>Turn ${ui("CHOIR SIZE")} up or down.</b> You see the singers walk in and out of the choir loft.</li>
<li><b>Pick a ${ui("VOCAL TYPE")}.</b> ${ui("Words (Your Voice)")} keeps your lyrics: every singer is your own voice, re-pitched. ${ui("Ah (Vowels)")}, ${ui("Oh")}, ${ui("Oo")}, ${ui("Ee")} and ${ui("Eh")} are sung by real recorded singers on a vowel, like a classic choir pad.</li>
<li><b>Pick a ${ui("STYLE")} and a room</b> (the room name at the top left of the church). Try ${ui("Gospel")} in the ${ui("Gospel Church")}, or ${ui("Sacred")} in the ${ui("Grand Cathedral")}.</li>
<li><b>For the best result, LEARN and RENDER:</b> press ${ui("LEARN")}, play your song from the start, press ${ui("LEARN")} again at the end, then press ${ui("RENDER")}. The choir now knows your whole song and sings it at full quality.</li>
</ol>
${tip("Not sure where to start? Open the preset menu at the top left and try a few presets: they set everything at once, and your song's key stays as it is.")}`,
    },
    {
      id: "daw", title: "Putting it on your track", summary: "Where to find Sanctuary Choir in your DAW, and why a stereo track matters.",
      body: `
<table class="m-table">
<tr><th>DAW</th><th>Where to find it</th><th>Worth knowing</th></tr>
<tr><td>Logic Pro, GarageBand</td><td>An Audio FX slot on the vocal track: <i>Audio Units › Sanctuary Audio › Sanctuary Choir</i></td><td>To play chords to it from a keyboard, insert it on a software instrument track as an <i>AU MIDI-controlled Effect</i> and choose the vocal track as its side-chain.</td></tr>
<tr><td>Ableton Live</td><td><i>Audio Effects › Plug-Ins › Sanctuary Choir</i> (VST3)</td><td>For MIDI chords, set a MIDI track's <i>MIDI To</i> to the vocal track, then Sanctuary Choir.</td></tr>
<tr><td>FL Studio</td><td>A mixer insert on the vocal (VST3)</td><td>Route a MIDI Out channel to the plug-in's MIDI port for chords.</td></tr>
<tr><td>Cubase, Studio One, Reaper</td><td>An insert on the vocal track (VST3)</td><td>Cubase records vocals on mono tracks, and a mono track can't carry a stereo choir: use a stereo track, or put Sanctuary Choir on an FX channel fed by a send.</td></tr>
<tr><td>No DAW</td><td>Open the standalone app and choose your microphone under <i>Options › Audio/MIDI Settings</i></td><td>Wear headphones, or the speakers feed back into the mic.</td></tr>
</table>
${note("On a mono track the choir is folded down to mono. Sanctuary notices and says so at the bottom of the Settings page.")}
<p>Pro Tools (AAX) isn't supported yet.</p>`,
    },
    {
      id: "tour", title: "The main page at a glance", summary: "Every part of the main (PERFORM) page, numbered. Point at a number to see its name.",
      body: tour() + `
<p>Everything you need while you sing is on this page. The ${ui("MIX")}, ${ui("FX")} and ${ui("SETTINGS")} pages open over the church and hold the finer controls. Click ${ui("PERFORM")} to come back.</p>
${go("perform", "Show me the main page")}`,
    },
    {
      id: "knobs", title: "The six big knobs", summary: "Volume, Choir Size, Space, Reverb, Tone and Presence: the sound in broad strokes.",
      body: `
<p>Drag a knob up or down to turn it. Double-click it to put it back to where it started.</p>
<dl class="m-dl">
<dt>${ui("VOLUME")}</dt><dd>The level of everything coming out: your voice, the choir and the room.</dd>
<dt>${ui("CHOIR SIZE")}</dt><dd>How many singers, from none to 40. You see them fill the room, front row first. More singers sound bigger and smoother; a handful sound like a small ensemble.</dd>
<dt>${ui("SPACE")}</dt><dd>How wide the stage is, and how much of the room's first echoes you hear. Low is narrow and close; high surrounds you.</dd>
<dt>${ui("REVERB")}</dt><dd>How much of the room you hear. The room itself (cathedral, chapel, studio...) is chosen at the top left of the church.</dd>
<dt>${ui("TONE")}</dt><dd>Tilts the choir darker (left) or brighter (right).</dd>
<dt>${ui("PRESENCE")}</dt><dd>Brings the choir forward in the mix: clearer words and more detail.</dd>
</dl>
${tip("The <b>Energy</b> plaque at the top right of the church is a seventh knob: drag it up or down, or click its arrow to choose <i>Hushed</i>, <i>Soft</i>, <i>Natural</i>, <i>Powerful</i> or <i>Massive</i>.")}`,
    },
    {
      id: "harmony", title: "Harmony, key, scale and style", summary: "What the choir sings, and how it performs it: the five menus under the knobs.",
      body: `
<h4>${ui("HARMONY MODE")}: the arrangement</h4>
<dl class="m-dl">
<dt>Full Choir</dt><dd>Classic four-part hymn harmony around your melody.</dd>
<dt>Gospel Stack</dt><dd>Close, rich chords stacked tight together, the way a gospel choir sings them.</dd>
<dt>Open Voicing</dt><dd>The parts spread wide apart: spacious and cinematic.</dd>
<dt>Unison &amp; Octaves</dt><dd>Everyone sings your melody, in their own octave. Big and strong, no harmony.</dd>
<dt>Thirds &amp; Sixths</dt><dd>Sweet parallel harmony that follows your line.</dd>
<dt>Organum</dt><dd>Medieval harmony: a fourth under your melody, an octave below, and a drone on the key's root and fifth. Perfect for chant.</dd>
<dt>Upper Voices / Lower Voices</dt><dd>Only the sopranos and altos, or only the tenors and basses.</dd>
<dt>Drone Pad</dt><dd>The choir holds the key's home chord under you, like an organ, whatever you sing.</dd>
<dt>MIDI Chords</dt><dd>The choir sings exactly the chords you play on a keyboard (see <i>Play the choir with MIDI</i>).</dd>
</dl>
<h4>${ui("KEY")} and ${ui("SCALE")}</h4>
<ul>
<li>Both start on <b>Auto</b>: Sanctuary listens and works out the key (usually within a phrase or two) and whether the song is major or minor. The field shows what it heard, such as <i>Auto · Eb</i>.</li>
<li>Click the small <b>padlock</b> beside the key once it's right, to keep it. The key is remembered with your project.</li>
<li>Know the key? Set it yourself and the harmony is right from the very first note.</li>
<li>Choose a scale yourself for the modes (Dorian, Mixolydian, Lydian, Phrygian), Harmonic Minor, Gospel (blues) or Chromatic.</li>
</ul>
<h4>${ui("STYLE")}: how they perform</h4>
<dl class="m-dl">
<dt>Sacred</dt><dd>A trained church choir: warm, blended, a little vibrato.</dd>
<dt>Gospel</dt><dd>7th chords, scoops into the notes and plenty of vibrato.</dd>
<dt>Gregorian</dt><dd>Pure, still voices with no vibrato, like monks.</dd>
<dt>Cinematic</dt><dd>Big, slow, dramatic: a film-score choir.</dd>
<dt>Worship</dt><dd>A modern worship team: clear and open.</dd>
<dt>Chamber</dt><dd>A small, precise ensemble with straight tone.</dd>
<dt>Spiritual</dt><dd>Warm, soulful and expressive.</dd>
</dl>`,
    },
    {
      id: "voices", title: "Who sings: words or vowels", summary: "Vocal Type, the recorded singers, Singing, Energy, Ensemble and the deep oktavists.",
      body: `
<h4>${ui("VOCAL TYPE")}</h4>
<dl class="m-dl">
<dt>Words (Your Voice)</dt><dd>Every singer is your own voice, re-pitched to their part, so the choir sings your lyrics. Each singer also takes on the voice of one of 19 real singers (their vocal tract, tone and vibrato), so it sounds like different people, not copies of you. Sanctuary hears whether you have a low or a high voice and gives the other sections the right kind of voice.</dd>
<dt>Ah, Oh, Oo, Ee, Eh</dt><dd>Real recorded singers holding a vowel: the classic choir pad. They slide from note to note the way they were recorded sliding, and a note that starts from silence begins with a real sung onset.</dd>
<dt>Mm (Hum)</dt><dd>A soft hum (always synthesised).</dd>
</dl>
${note("In Words mode the singers copy your singing, not your mouth noises: lip smacks and clicks are taken out of what they sing (your own voice is untouched). Where your voice creaks at the end of a phrase, they stay in the octave you were singing.")}
<h4>${ui("SINGING")} (MIX page)</h4>
<p>${ui("NATURAL")}, ${ui("BREATHY")} (airy and soft) or ${ui("BELT")} (bright and pressed, sung out). The recorded singers switch to takes they recorded that way. On Natural, some of them sing in their own classical voice with its vibrato, as in a real choir: how many depends on the Style and the ${ui("VIBRATO")} knob (none in Chamber or Gregorian, most of a Gospel choir).</p>
<h4>Energy (the plaque at the top right)</h4>
<p>How hard the choir sings, from <i>Hushed</i> to <i>Massive</i>. Lower is quieter, airier and slower to speak; higher is louder, brighter and more pressed. The recorded singers change takes too. Automate it to grow from a hushed verse to a huge last chorus.</p>
<h4>${ui("ENSEMBLE")} (MIX page)</h4>
<dl class="m-dl">
<dt>Choir</dt><dd>An adult choir.</dd>
<dt>Children</dt><dd>A children's choir: light, pure, straight voices on every part.</dd>
<dt>Gang</dt><dd>Shouted gang vocals: punchy and loose. Try it with Unison &amp; Octaves and Words.</dd>
<dt>Congregation</dt><dd>A church full of people: untrained voices, loose tuning, late entrances.</dd>
</dl>
<h4>${ui("SUB-OCTAVE")} (MIX page)</h4>
<p>Adds oktavists: basses singing the bass part an octave lower, the deep weight under a Russian or gospel mass choir. One joins for every two basses.</p>
<h4>Your own recorded singers</h4>
<p>${ui("SETTINGS")} › ${ui("CHOIR VOICES")} switches between ${ui("SANCTUARY")} (the recorded singers installed with the plug-in), ${ui("YOUR OWN…")} (a folder of your own recordings) and ${ui("SYNTH")} (synthesised voices).</p>
${go("mix", "Show me the MIX page")}`,
    },
    {
      id: "live", title: "Recording and performing live", summary: "Studio or Live: how the choir's small delay is handled, and what to use when.",
      body: `
<p>To re-pitch a voice, the choir has to hear a little of it first, so it runs about 40 thousandths of a second behind you (this small delay is called <i>latency</i>). ${ui("SETTINGS")} › ${ui("MONITORING")} chooses how that is handled, and shows the exact figures for your session:</p>
<dl class="m-dl">
<dt>${ui("STUDIO")} (the default)</dt><dd>Your voice and the choir line up exactly. Your DAW knows about the delay and makes up for it on playback. While you record, though, you hear yourself slightly late. <b>Use it for mixing and for singing over a track you already recorded.</b></dd>
<dt>${ui("LIVE")}</dt><dd>You hear your own voice almost at once (1.5 thousandths of a second, the time the output limiter looks ahead). The vowel choir follows about 16 thousandths of a second behind you, Words about 40, like a choir following its soloist. <b>Use it for recording, rehearsing and performing.</b></dd>
</dl>
${tip("Record in Live, then switch back to Studio before you mix or bounce, so the choir sits exactly on the beat. Learned and rendered takes stay in time in both.")}
<ul>
<li>Set your audio interface to a buffer of 128 or 256 samples while you record.</li>
<li>The standalone app always runs in Live mode.</li>
</ul>
${go("settings", "Show me Monitoring")}`,
    },
    {
      id: "learn", title: "Learn and Render", summary: "Let the choir hear your whole song first: smarter harmony, the best quality, almost no CPU.",
      body: `
<ol class="m-steps">
<li>Press ${ui("LEARN")}. It glows: <i>LEARN ARMED</i>.</li>
<li>Press play in your DAW. Sanctuary records your vocal as it plays (<i>LEARNING 0:12</i>).</li>
<li>Press ${ui("LEARN")} again, or stop your DAW. Sanctuary studies the whole take: every note, the key, and where each phrase starts and ends. The plaque in the church shows what it found, such as <i>LEARNED 0:43 · G MAJOR · 4 PHRASES · 105 NOTES</i>. From now on, when the song plays, the choir uses this smarter harmony: it sees each phrase coming, lands cadences on the right chord and keeps the voices moving smoothly.</li>
<li>Press ${ui("RENDER")}. Every singer is sung at the highest quality. Playback then uses the rendered choir, in time with your song, and costs almost no CPU.</li>
</ol>
<ul>
<li>Change a harmony or choir setting later and ${ui("RENDER")} glows again: press it to update. Changes to the stage, the mics or the room never need a new render.</li>
<li>You can press ${ui("RENDER")} while still learning: Sanctuary finishes the take, studies it and renders it.</li>
<li>If the song changes key (a last chorus a step up, say), Sanctuary follows it: the plaque shows <i>G MAJOR → A MAJOR</i>.</li>
<li>${ui("LOAD")} learns from an audio file instead, such as an a cappella stem. ${ui("CLEAR")} forgets the take.</li>
<li>The take is saved inside your project, and still works if you reopen the project at another sample rate.</li>
</ul>
${tip("Click the plaque after learning to see the chords the choir chose, change any of them, and drag the parts into your DAW (chapter <i>Chords &amp; Stems</i>).")}`,
    },
    {
      id: "passes", title: "Multi-take choir", summary: "Sing the part up to four times; the Words choir sings from every pass and sounds like many people.",
      body: `
<p>In Words mode the choir sings your own voice. One take gives every singer the same breaths, timing and colour. A real choir sounds bigger because every singer is a different performance, so: sing the part again.</p>
<ol class="m-steps">
<li>After LEARN, open ${ui("SETTINGS")} › ${ui("MULTI-TAKE")}.</li>
<li>Press ${ui("● RECORD")} on the next empty card. In your DAW, play from a bar or two before the take and sing the part again on the same track; it stops when your DAW stops. In the standalone app, your take plays through the choir for you to sing along with; press ${ui("■ STOP")} when done.</li>
<li>Sanctuary lines the pass up with your take, note by note. A pass that's a little early, late or quieter is fine.</li>
</ol>
<p>In each section the singers take the passes in turn: with three passes, a third of the sopranos sing from each. ${ui("LOAD…")} adds a pass from a file, ${ui("REMOVE")} takes one away. A pass that sings a different part is turned down. Passes change only the Words choir.</p>
${go("passes", "Show me Multi-Take")}`,
    },
    {
      id: "chords", title: "Chords & Stems", summary: "See and change the chords the choir sings, and drag the parts and audio into your DAW.",
      body: `
<p>After LEARN, click the plaque in the church (or ${ui("SETTINGS")} › ${ui("CHORDS & STEMS")}). You see the take's chords in a lane and, under them, your melody and the four parts the choir sings: soprano pink, alto gold, tenor blue, bass green. Click ${ui("MELODY")}, ${ui("SOPRANO")}, ${ui("ALTO")}, ${ui("TENOR")} or ${ui("BASS")} to hide or show a line.</p>
<h4>Change a chord</h4>
<ol class="m-steps">
<li>Click a chord in the lane. A menu lists the chords that could go there, best first; the bar beside each shows how much of your melody fits it.</li>
<li>Pick one. It turns gold with a padlock, and the chords around it are worked out again so they lead in and out of it.</li>
<li>To undo, click the gold chord and choose ${ui("LET THE CHOIR CHOOSE")}. ${ui("UNLOCK ALL")} undoes every chord you chose.</li>
</ol>
<p>Your chords are saved with the project and win over everything else. Press ${ui("RENDER")} to hear them in the rendered choir.</p>
<h4>Drag into your DAW</h4>
<p>Press on a chip, hold, and drag it onto a track or an empty part of your arrangement.</p>
<dl class="m-dl">
<dt>MIDI</dt><dd>Ready right after LEARN. Soprano, alto, tenor and bass on MIDI channels 1 to 4, your melody on 5 and the named chords on 6, in your song's tempo. Use it with another choir or orchestra library, or print a lead sheet.</dd>
<dt>Full Choir, Sopranos, Altos, Tenors, Basses</dt><dd>24-bit audio of the rendered choir (press RENDER first). Each section file is that section where it stands, through the mics, without the hall.</dd>
<dt>Room</dt><dd>The hall alone: the reverb, to put under the sections yourself.</dd>
<dt>Lead on Stage</dt><dd>Your own voice where it stands on the stage, with its reverb.</dd>
<dt>All Stems</dt><dd>All of them at once.</dd>
</dl>
${tip("Drop the files at bar 1 and they line up with your take. If your DAW won't accept the drag, use <b>SAVE TO FOLDER</b>: the files are kept in <i>Documents/Sanctuary Choir/Exports</i>.")}
${go("take", "Show me Chords &amp; Stems")}`,
    },
    {
      id: "band", title: "Follow the band", summary: "Let the choir hear your band or backing track and sing the chords they play.",
      body: `
<p>A melody alone often can't tell an F chord from a D minor; the band can. Send your keys, guitar, pads or backing track to Sanctuary's <b>side-chain</b> input and the choir sings their chords, even their inversions.</p>
<dl class="m-dl">
<dt>Cubase, Nuendo</dt><dd>Switch on side-chain at the top of the plug-in window, then add a send from the band to <i>Sanctuary Choir – Side-Chain</i>.</dd>
<dt>Logic Pro</dt><dd>Choose the band's track or bus in the <i>Side Chain</i> menu at the top right of the plug-in window.</dd>
<dt>Studio One</dt><dd>Click <i>Side-Chain</i> at the top of the plug-in window and tick the band's track.</dd>
<dt>Reaper</dt><dd>Send the band to channels 3/4 of the vocal track.</dd>
<dt>Ableton Live 11.1+</dt><dd>Open the plug-in's side-chain section and choose the band under <i>Audio From</i>.</dd>
<dt>FL Studio</dt><dd>Route the band's mixer track to the vocal's track as a side-chain and map it in the wrapper's <i>Processing</i> tab.</dd>
</dl>
<p>${ui("SETTINGS")} › ${ui("Follow the band")} is on by default. The Settings page shows what Sanctuary hears, and the main page shows the band's chord with a ${ui("BAND")} tag.</p>
<ul>
<li>Live, the choir takes about a third of a second to follow a chord change. ${ui("CHORD SPEED")} (MIX) makes it quicker or steadier.</li>
<li>For exact timing, LEARN with the band playing: every chord change is found and each note gets the chord under it (<i>· WITH THE BAND</i>).</li>
<li>MIDI chords you play still come first.</li>
</ul>`,
    },
    {
      id: "midi", title: "Play the choir with MIDI", summary: "Hold chords, write each part yourself, switch presets, or make the choir sing typed words.",
      body: `
<dl class="m-dl">
<dt>MIDI chords</dt><dd>Hold a chord on a keyboard and the choir sings it, whatever it heard. With no voice coming in, the choir becomes an instrument you play. Turn it on or off with ${ui("MIDI chords")} in Settings.</dd>
<dt>Parts on channels 1–4</dt><dd>Write every part yourself: notes on channel 1 are sung by the sopranos, 2 the altos, 3 the tenors and 4 the basses. Other channels still work as chords.</dd>
<dt>Program Change</dt><dd>Picks a preset by its number in the list, so a controller or a DAW track can switch sounds.</dd>
</dl>
<h4>Sung words</h4>
<p>${ui("SETTINGS")} › ${ui("SUNG WORDS")}: type words and the MIDI choir sings them, one syllable per note or chord you play.</p>
<ul>
<li>Type <i>Kyrie eleison</i>, <i>Alleluia</i> or <i>Glory to God</i>, or pick one of the ready texts. The page shows how the words are split and lights up the syllable being sung.</li>
<li>Latin is sung the church way (Ky-ri-e, ex-cel-sis as "ek-shel-sis"); anything else is read as English.</li>
<li>Write <b>-</b> inside a word to split it yourself (Ho-san-na), and <b>_</b> to hold a syllable over another note: <i>A _ _ men</i> sings "A" across three notes, then "men".</li>
<li>The words start again from the top when you change them or your DAW starts playing, and loop at the end.</li>
<li>With the recorded singers, many opening consonants (D, B, M, N, L, R, P, T, K, S, Ch, J, Y, Cr, Str, Dr) are real recorded consonants.</li>
<li>Clear the text to go back to the Vocal Type.</li>
</ul>
${go("words", "Show me Sung Words")}`,
    },
    {
      id: "mix", title: "The MIX page", summary: "Each section's level and place, the lead and choir balance, and how the singers perform.",
      body: `
<p>Click ${ui("MIX")} at the top right. Click it again to see the stage; once more to come back.</p>
<h4>The four sections</h4>
<ul>
<li>The <b>fader</b> sets the section's level; ${ui("PAN")} moves it left or right.</li>
<li>${ui("M")} mutes the section, ${ui("S")} solos it.</li>
<li>The arrows under it set the <b>lowest and highest notes</b> it sings.</li>
</ul>
<h4>${ui("LEAD")} and ${ui("CHOIR")}</h4>
<p>The balance between your own voice and the choir. Pull ${ui("LEAD")} all the way down to hear the choir alone.</p>
<h4>Performance</h4>
<dl class="m-dl">
<dt>${ui("SUB-OCTAVE")}</dt><dd>Deep basses an octave below the bass part. Off at the bottom.</dd>
<dt>${ui("HUMANIZE")}</dt><dd>How loose the singers are: each a little early or late and a touch off pitch, like real people. Lower is tighter.</dd>
<dt>${ui("VIBRATO")}</dt><dd>How much the voices waver. At 0 everyone sings straight.</dd>
<dt>${ui("GLIDE")}</dt><dd>How slowly the singers slide from one note to the next.</dd>
<dt>${ui("FORMANT")}</dt><dd>The size of the voices: turn left for larger, darker voices, right for smaller, brighter ones. The middle is natural.</dd>
<dt>${ui("PITCH LOCK")}</dt><dd>How firmly the singers stay on the exact notes. Lower lets them follow your own bends and inflections.</dd>
<dt>${ui("BREATH")}</dt><dd>How much breath is heard in the voices.</dd>
<dt>${ui("REAL VOICES")}</dt><dd>Words mode: how far each singer takes on a real singer's voice. 0 makes every singer your own voice again.</dd>
<dt>${ui("CHORD SPEED")}</dt><dd>How readily the choir changes chord. Low holds a chord through quick passing notes; high gives every note its own chord.</dd>
</dl>
<h4>${ui("ENSEMBLE")} and ${ui("RESPONSE")}</h4>
<p>Ensemble chooses who sings (Choir, Children, Gang, Congregation). Response chooses when: ${ui("WITH ME")} sings along with you; ${ui("ECHO")} answers each phrase right after you sing it, like call and response.</p>
${go("mix", "Show me the MIX page")}`,
    },
    {
      id: "stage", title: "The stage and the microphones", summary: "Arrange the sections, put yourself on the stage, and mix the microphones like an engineer.",
      body: `
<p>Click the choir in the church, ${ui("MIX")} › ${ui("STAGE & MICS")}, or click ${ui("MIX")} a second time. You see the stage from the hall: sopranos pink, altos gold, tenors blue, basses green. Each name shows how many singers that section has.</p>
<h4>Arrange the singers</h4>
<ul>
<li><b>Drag a section</b> to move it: left or right, and up the picture to move it further back. Further back sounds a little quieter, later and darker.</li>
<li><b>Drag the round handles</b> at the front corners of its risers to make it wider or narrower. A narrow section sounds like one spot; a wide one surrounds you.</li>
<li>The <b>formation</b> buttons set all four at once: ${ui("BLOCKS")} (women in front, men behind), ${ui("ACROSS")} (one wide row), ${ui("MEN CENTRE")} (women on the sides, men in the middle) and ${ui("MIXED")} (everyone spread out: the most blended).</li>
</ul>
<h4>Put yourself on the stage</h4>
<p><b>YOU</b>, the singer by the microphones, is your own voice. At the microphones it is dry and up front, like a lead vocal. <b>Drag YOU onto the stage</b> to sing from among the choir, in the hall with them. Double-click YOU, or drag it off the front of the stage, to go back to the microphones.</p>
<h4>Choose the microphones</h4>
<dl class="m-dl">
<dt>${ui("CLOSE")}</dt><dd>Spot mics in front of each section: near, dry and detailed.</dd>
<dt>${ui("TREE")}</dt><dd>The main pair above the conductor: the natural picture the audience hears. The only pair up at first.</dd>
<dt>${ui("ROOM")}</dt><dd>A pair far out in the hall: wide and distant, with much more of the room.</dd>
</dl>
<p>${ui("NATURAL")}, ${ui("INTIMATE")}, ${ui("CONCERT")} and ${ui("DISTANT")} are good starting points. Nothing here ever needs a new RENDER.</p>
${go("stage", "Show me the stage")}`,
    },
    {
      id: "fx", title: "The FX page: the room and effects", summary: "Six real rooms, the reverb controls, the choir bus and rhythmic effects.",
      body: `
<h4>The room</h4>
<p>Each room's reverb is a <b>real room, measured</b>: the sound of a pair of microphones in the hall. Rest the mouse on a room to see which. You can also pick the room at the top left of the church; each one shows its own choir.</p>
<dl class="m-dl">
<dt>Grand Cathedral</dt><dd>Vast stone, a 5 second tail. A church in Cremona, its decay lengthened.</dd>
<dt>Stone Abbey</dt><dd>Gothic stone, 4.3 s: the St. Nicholas Chapel of Aachen Cathedral. With the Gregorian style, monks sing here.</dd>
<dt>Wooden Chapel</dt><dd>Warm and close, 1.5 s: a wood-panelled hall in Detmold.</dd>
<dt>Concert Hall</dt><dd>Clear and wide, 2.1 s: the Arvedi Auditorium in Cremona.</dd>
<dt>Gospel Church</dt><dd>Lively, 1.7 s: the cathedral's church with its decay shortened, the sound of a full, lively church.</dd>
<dt>Studio Room</dt><dd>Dry and intimate: BBC Maida Vale Studio 5.</dd>
</dl>
<h4>Reverb</h4>
<dl class="m-dl">
<dt>${ui("DECAY")}</dt><dd>Stretches or shortens the room's own decay, from 30% to 250%.</dd>
<dt>${ui("PRE-DELAY")}</dt><dd>A short wait before the room answers. More keeps the words clearer.</dd>
<dt>${ui("BRIGHTNESS")}</dt><dd>Darkens or brightens the room. 50% is the room as measured.</dd>
<dt>${ui("LEAD SEND")}</dt><dd>How much of your own voice goes into the room.</dd>
</dl>
<h4>Choir bus</h4>
<dl class="m-dl">
<dt>${ui("LOW CUT")} / ${ui("HIGH CUT")}</dt><dd>Trim the lowest and highest frequencies of the choir.</dd>
<dt>${ui("WARMTH")}</dt><dd>Gentle saturation, like analog tape.</dd>
<dt>${ui("GLUE")}</dt><dd>A soft compressor that pulls the sections together.</dd>
</dl>
<h4>Rhythm</h4>
<p>Gates the choir in time with your song's tempo. <i>Pulses, Offbeats, Dotted, Gallop, Stutter</i> and <i>Trance Gate</i> chop the choir; <i>Arp Up, Arp Down</i> and <i>Arp Bounce</i> pass the chord from section to section; <i>Call &amp; Answer</i> gives the first half of the bar to the upper voices and the second to the lower. ${ui("RATE")} sets the speed, ${ui("SECTIONS")} who is chopped, ${ui("LENGTH")} how long each note lasts and ${ui("DEPTH")} how far it closes.</p>
${go("fx", "Show me the FX page")}`,
    },
    {
      id: "settings", title: "The Settings page", summary: "MIDI and the band, tuning, monitoring, your learned take, the voices, scenes and this manual.",
      body: `
<dl class="m-dl">
<dt>Harmony from · MIDI and the band</dt><dd>${ui("MIDI chords")}, ${ui("Parts on ch. 1–4")} and ${ui("Follow the band")} switch each source on or off. ${ui("SUNG WORDS")} opens the typed-words page.</dd>
<dt>${ui("TUNING A4")}</dt><dd>The reference pitch. Leave it at 440 Hz unless your song is tuned differently.</dd>
<dt>${ui("MONITORING")}</dt><dd>${ui("STUDIO")} or ${ui("LIVE")} (chapter <i>Recording and performing live</i>).</dd>
<dt>Learned take</dt><dd>${ui("PREVIEW")} plays your take through the choir, ${ui("CHORDS & STEMS")} opens the chords and exports, ${ui("PRESETS FOLDER")} opens your presets on disk, ${ui("MULTI-TAKE")} adds more passes.</dd>
<dt>${ui("CHOIR VOICES")}</dt><dd>Sanctuary's recorded singers, your own, or synthesised voices.</dd>
<dt>${ui("SCENES")}</dt><dd>See the next chapter.</dd>
<dt>${ui("MANUAL")}</dt><dd>Opens this manual. So does the gear at the top left.</dd>
</dl>
<p>The line at the bottom shows the version and build, the delay your DAW is told about (in Live mode, the 1.5 ms of the limiter), and a reminder if you're on a mono track.</p>
${go("settings", "Show me Settings")}`,
    },
    {
      id: "presets", title: "Presets, scenes and automation", summary: "Save sounds, switch them during a song, and let your DAW move the controls.",
      body: `
<h4>Presets</h4>
<ul>
<li>Click the preset name at the top left. Presets come in categories (Sacred &amp; Classical, Worship, Gospel, Cinematic, Pop &amp; Backing, Pads &amp; Drones, Rhythmic, Kids &amp; Crowds): point at one to see its presets. The arrows step through them.</li>
<li>Presets change the choir's sound, not your song: the key and scale you set stay as they are, and so does where you stand on the stage.</li>
<li>To save your own, open the preset menu (or the gear), type a name and choose ${ui("SAVE CURRENT SOUND")}. Your presets appear under <i>My Presets</i>.</li>
</ul>
<h4>Scenes</h4>
<p>Store up to 8 sounds for one song, such as verse, chorus and bridge: on the Settings page press ${ui("STORE")}, then a number. Click a number to bring that sound back, or automate the <b>Scene</b> parameter so the choir changes by itself as the song plays.</p>
<h4>Automation</h4>
<p>The knobs, menus and switches of the sound can be automated in your DAW (the set-up switches, such as MIDI, Follow the Band, Tuning and Monitoring, can't). Favourites: <b>Energy</b> (a hushed verse into a massive chorus), <b>Singing</b> (breathy verse, belted chorus), <b>Choir Size</b>, <b>Lead Depth</b> with Lead on Stage on (a soloist who walks forward) and <b>Freeze</b>.</p>
<h4>${ui("FREEZE")}</h4>
<p>Holds the chord the choir is singing, for as long as you like, while you sing over it. Great for a big final chord.</p>`,
    },
    {
      id: "recipes", title: "Recipes", summary: "Quick settings for sounds people often want.",
      body: `
<dl class="m-dl">
<dt>Big gospel chorus</dt><dd>Style <i>Gospel</i>, Harmony <i>Gospel Stack</i>, Vocal Type <i>Words</i>, room <i>Gospel Church</i>, Choir Size 70%, Energy <i>Powerful</i>. Or the <i>Mass Choir</i> preset.</dd>
<dt>Cathedral pad under a ballad</dt><dd>Vocal Type <i>Ah</i> or <i>Oo</i>, Harmony <i>Full Choir</i>, room <i>Grand Cathedral</i>, Reverb 75%, Energy <i>Soft</i>. Or <i>Heavenly Praise</i>.</dd>
<dt>Gregorian chant</dt><dd>The <i>Gregorian Chant</i> preset: Style <i>Gregorian</i>, Harmony <i>Organum</i>, Words, in the <i>Stone Abbey</i>.</dd>
<dt>Film-score choir</dt><dd>The <i>Epic Cinematic</i> preset, then Sub-Octave up and Energy to <i>Massive</i> for the climax.</dd>
<dt>Pop backing vocals</dt><dd><i>Backing Stack</i> or <i>Pop Doubles</i>, Studio Room, Choir Size low, Presence up.</dd>
<dt>Children's choir</dt><dd>Ensemble <i>Children</i> (MIX), or the <i>Children's Choir</i> preset.</dd>
<dt>A hymn played on a keyboard, with words</dt><dd>Settings › SUNG WORDS, type the text, then play the chords: one syllable per chord.</dd>
<dt>Choir answering you</dt><dd>Response <i>ECHO</i> (MIX), or the <i>Call &amp; Response</i> preset.</dd>
<dt>Rhythmic choir for dance music</dt><dd>FX › Rhythm <i>Trance Gate</i> or an <i>Arp</i>, or the presets in <i>Rhythmic</i>.</dd>
</dl>`,
    },
    {
      id: "trouble", title: "Troubleshooting", summary: "When something doesn't sound or behave the way you expect.",
      body: `
<dl class="m-dl">
<dt>I hear no choir</dt><dd>Check that ${ui("CHOIR SIZE")} is up, the ${ui("CHOIR")} fader on MIX is up, and no section is muted (a faded singer icon on the main page). The choir sings when it hears a voice, or when you play MIDI chords.</dd>
<dt>The choir is in mono</dt><dd>The track is mono. Use a stereo track, or an FX channel fed by a send.</dd>
<dt>I hear myself late while recording</dt><dd>Switch ${ui("MONITORING")} to ${ui("LIVE")}, and use a buffer of 128 or 256 samples.</dd>
<dt>The choir is slightly off the beat when I mix</dt><dd>Switch ${ui("MONITORING")} back to ${ui("STUDIO")}.</dd>
<dt>The harmony is in the wrong key</dt><dd>Set the ${ui("KEY")} (and ${ui("SCALE")}) yourself, or wait a phrase for Auto and then lock it. LEARN gives the most reliable result.</dd>
<dt>The chords change too often, or too slowly</dt><dd>Turn ${ui("CHORD SPEED")} (MIX) down or up. With a band, route it to the side-chain so the choir sings its chords.</dd>
<dt>Clicks, crackles or dropouts</dt><dd>Usually your computer running out of time: raise the audio buffer, lower ${ui("CHOIR SIZE")}, or LEARN and RENDER (rendered playback costs almost nothing). In Words mode the choir no longer copies your mouth clicks.</dd>
<dt>The choir gets quieter when it gets big</dt><dd>A very loud choir (a big ${ui("CHOIR SIZE")} at high Energy, with lots of ${ui("REVERB")} and a long Decay) goes past full scale, and the output limiter turns it down smoothly so it never distorts. For more headroom, turn ${ui("VOLUME")} down a few dB.</dd>
<dt>It uses a lot of CPU</dt><dd>RENDER your take. Words mode with a large choir is the heaviest setting while live.</dd>
<dt>RENDER is glowing</dt><dd>You changed something the rendered choir doesn't have yet. Press it to update.</dd>
<dt>The vowel modes sound synthetic</dt><dd>${ui("SETTINGS")} › ${ui("CHOIR VOICES")}: choose ${ui("SANCTUARY")}. If it is greyed out, the recorded voices aren't installed: run the installer again with <i>Sanctuary Voices</i> ticked.</dd>
<dt>My DAW won't take the drag from Chords &amp; Stems</dt><dd>Use ${ui("SAVE TO FOLDER")} and drag from the folder instead.</dd>
<dt>A pass was turned down</dt><dd>It sang too little of the part, or a different part. Sing the same part again from a bar or two before the take.</dd>
<dt>Reporting a problem</dt><dd>Say which build you have: it is at the bottom of ${ui("SETTINGS")} (for example <i>1.0.0 build e6eda58</i>). Save the sound as a preset (the gear › <i>Save this sound as a preset</i>) and send the file along with a short clip, so the problem can be heard with your exact settings.</dd>
</dl>`,
    },
    {
      id: "glossary", title: "Words used here", summary: "The music and studio words in this manual, in plain language.",
      body: `
<dl class="m-dl">
<dt>SATB</dt><dd>Soprano, alto, tenor, bass: the four parts of a choir, highest to lowest.</dd>
<dt>Section</dt><dd>All the singers of one part, such as the sopranos.</dd>
<dt>Key, scale</dt><dd>The home note of a song and the set of notes it uses (major sounds bright, minor darker).</dd>
<dt>Chord, cadence</dt><dd>Notes sung together; a cadence is the chord change that ends a phrase.</dd>
<dt>Unison</dt><dd>Everyone on the same melody.</dd>
<dt>Vibrato</dt><dd>The gentle waver in a held note.</dd>
<dt>Legato</dt><dd>Notes joined smoothly, sliding from one to the next.</dd>
<dt>Formant</dt><dd>The resonance that gives a voice its size and colour.</dd>
<dt>Oktavist</dt><dd>A very deep bass who sings an octave below the basses.</dd>
<dt>Latency</dt><dd>A small delay between a sound going in and coming out.</dd>
<dt>Render</dt><dd>Working out the whole choir in advance, at the best quality.</dd>
<dt>Stem</dt><dd>One part of a mix as its own audio file, such as the sopranos alone.</dd>
<dt>Side-chain</dt><dd>A second input that lets a plug-in listen to another track, here your band.</dd>
<dt>Reverb, decay, pre-delay</dt><dd>The sound of the room; how long it takes to die away; the short gap before it starts.</dd>
<dt>Automation</dt><dd>Your DAW moving a control over the course of a song.</dd>
<dt>Buffer size</dt><dd>How much audio your computer handles at a time. Smaller is quicker to respond but harder work.</dd>
</dl>`,
    },
    {
      id: "credits", title: "Credits", summary: "Who made the recordings and rooms Sanctuary Choir is built on.",
      body: `
<p><b>Sanctuary Voices</b>, the recorded singers, are cut from <i>VocalSet: A Singing Voice Dataset</i> by Julia Wilkins, Prem Seetharaman, Alison Wahl and Bryan Pardo (ISMIR 2018, zenodo.org/records/1442513), with note and lyric times from <i>Annotated-VocalSet</i> by Behnam Faghih and Joseph Timoney (zenodo.org/records/7061507). Both are under the Creative Commons Attribution 4.0 licence (creativecommons.org/licenses/by/4.0).</p>
<p><b>The rooms</b> are measured impulse responses, used under Creative Commons Attribution licences:</p>
<ul>
<li>Grand Cathedral and Gospel Church: the church of Santi Marcellino e Pietro, Cremona, from <i>ChurchIR: A Dataset of Multichannel Church Impulse Responses for Spatial Audio Applications</i> by Riccardo Giampiccolo, Sofia Parrinelli and Fabio Antonacci (zenodo.org/records/14901751, CC BY 4.0).</li>
<li>Stone Abbey: the St. Nicholas Chapel, Aachen Cathedral, from <i>Room acoustic measurement and decay analysis data of the St. Nicholas Chapel, Aachen Cathedral</i>, © 2026 Martin Zerwas and Selin Kayku (zenodo.org/records/23312556, CC BY 4.0).</li>
<li>Wooden Chapel: the Brahmssaal, Detmold University of Music, from <i>Open Database of Spatial Room Impulse Responses at Detmold University of Music</i> by Sebastia Vicenc Amengual Gari, Banu Sahin, Dusty Eddy and Malte Kob (zenodo.org/records/4116247, CC BY 4.0).</li>
<li>Concert Hall: the Giovanni Arvedi Auditorium, Cremona, from <i>The Sound of the Violin's Home: A Higher-Order Room Impulse Response Dataset of the Arvedi Auditorium in Cremona</i> by Federico Miotello and colleagues (zenodo.org/records/20098848, CC BY 4.0).</li>
<li>Studio Room: BBC Maida Vale Studio 5, from the <i>BBC Maida Vale Impulse Response Dataset</i> by Gavin Kearney, Helena Daffern and colleagues at the University of York, with BBC R&amp;D (zenodo.org/records/10020866, CC BY 3.0, creativecommons.org/licenses/by/3.0).</li>
</ul>
<p>The rooms were changed for Sanctuary Choir: direct sound removed, the noise floor replaced by each band's own decay, some decays lengthened or shortened (the Gospel Church is the Cremona church at 0.45 of its decay), high-passed at 40 Hz and evened out left to right. ROOMS-CREDITS.txt lists every change, room by room; the installers put it in the Sanctuary Choir folder (Windows: C:\ProgramData\Sanctuary Choir; Mac: /Library/Application Support/Sanctuary Choir). The licensors don't endorse Sanctuary Choir, and the material is provided as is.</p>
<p>Fonts: Cinzel, Cormorant Garamond and Montserrat (SIL Open Font License).</p>`,
    },
  ];

  //==========================================================================
  let root = null, toc = null, content = null, search = null, current = null, info = {};

  const escapeHtml = (s) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  const ENTITIES = { amp: "&", lt: "<", gt: ">", quot: '"', nbsp: " " };
  const plain = (html) => html.replace(/<[^>]+>/g, " ").replace(/&([a-z]+);/g, (m, e) => ENTITIES[e] || " ").replace(/\s+/g, " ");
  CHAPTERS.forEach((c) => { c.text = (c.title + " " + c.summary + " " + plain(c.body)).toLowerCase(); });

  // Wraps each match of the query inside the text of el (not inside its tags).
  function highlight(el, query) {
    if (!query) return;
    const walker = document.createTreeWalker(el, NodeFilter.SHOW_TEXT);
    const nodes = [];
    while (walker.nextNode()) nodes.push(walker.currentNode);
    const q = query.toLowerCase();
    nodes.forEach((n) => {
      const t = n.nodeValue, lower = t.toLowerCase();
      let i = lower.indexOf(q);
      if (i < 0) return;
      const frag = document.createDocumentFragment();
      let last = 0;
      while (i >= 0) {
        frag.appendChild(document.createTextNode(t.slice(last, i)));
        const m = document.createElement("mark");
        m.textContent = t.slice(i, i + q.length);
        frag.appendChild(m);
        last = i + q.length;
        i = lower.indexOf(q, last);
      }
      frag.appendChild(document.createTextNode(t.slice(last)));
      n.parentNode.replaceChild(frag, n);
    });
  }

  function showChapter(id) {
    const query = search.value.trim();
    const i = Math.max(0, CHAPTERS.findIndex((c) => c.id === id));
    const c = CHAPTERS[i];
    current = c.id;
    content.innerHTML = '<h3 class="m-title">' + escapeHtml(c.title) + "</h3>"
      + '<p class="m-summary">' + c.summary + "</p>" + c.body
      + '<div class="m-nav">'
      + (i > 0 ? '<button class="m-prev" data-id="' + CHAPTERS[i - 1].id + '">‹ ' + escapeHtml(CHAPTERS[i - 1].title) + "</button>" : "<span></span>")
      + (i < CHAPTERS.length - 1 ? '<button class="m-next" data-id="' + CHAPTERS[i + 1].id + '">' + escapeHtml(CHAPTERS[i + 1].title) + " ›</button>" : "<span></span>")
      + "</div>";
    if (query.length >= 2) highlight(content, query);
    content.scrollTop = 0;
    const first = content.querySelector("mark");
    if (first) first.scrollIntoView({ block: "center" });
    toc.querySelectorAll("button").forEach((b) => {
      b.classList.toggle("on", b.dataset.id === c.id);
      if (b.dataset.id === c.id) b.setAttribute("aria-current", "page"); else b.removeAttribute("aria-current");
    });
    try { localStorage.setItem("sc.manual.chapter", c.id); } catch (e) { /* storage may be unavailable */ }
  }

  function fillToc() {
    const query = search.value.trim().toLowerCase();
    toc.innerHTML = "";
    let shown = 0;
    CHAPTERS.forEach((c, i) => {
      if (query.length >= 2 && !c.text.includes(query)) return;
      const b = document.createElement("button");
      b.dataset.id = c.id;
      b.innerHTML = '<span class="m-toc-num">' + (i + 1) + "</span>" + escapeHtml(c.title);
      b.classList.toggle("on", c.id === current);
      if (c.id === current) b.setAttribute("aria-current", "page");
      b.addEventListener("click", () => showChapter(c.id));
      toc.appendChild(b);
      ++shown;
    });
    if (!shown) {
      const p = document.createElement("p");
      p.className = "m-none";
      p.textContent = "Nothing found. Try another word.";
      toc.appendChild(p);
    }
    return shown;
  }

  function build(stage, opts) {
    info = opts || {};
    root = document.createElement("div");
    root.id = "manual";
    root.className = "hidden";
    root.setAttribute("role", "dialog");
    root.setAttribute("aria-modal", "true");
    root.setAttribute("aria-label", "Manual");
    root.innerHTML = '<div class="m-head"><h2>THE MANUAL</h2>'
      + '<input class="m-search" type="search" placeholder="Search the manual…" aria-label="Search the manual">'
      + '<button class="page-close m-close" title="Close the manual">✕</button></div>'
      + '<div class="m-body"><nav class="m-toc" aria-label="Chapters"></nav><article class="m-content"></article></div>';
    stage.appendChild(root);
    toc = root.querySelector(".m-toc");
    content = root.querySelector(".m-content");
    search = root.querySelector(".m-search");
    root.querySelector(".m-close").addEventListener("click", close);
    search.addEventListener("input", () => {
      const shown = fillToc();
      const query = search.value.trim().toLowerCase();
      const cur = CHAPTERS.find((c) => c.id === current);
      if (shown && query.length >= 2 && cur && !cur.text.includes(query)) showChapter(toc.querySelector("button").dataset.id);
      else showChapter(current);
    });
    search.addEventListener("keydown", (e) => {
      e.stopPropagation();
      // Escape clears the search; with nothing to clear it closes the manual.
      if (e.key === "Escape" && !search.value) close();
    });
    // Keyboard focus stays inside the manual while it is open.
    root.addEventListener("keydown", (e) => {
      if (e.key !== "Tab") return;
      const items = Array.from(root.querySelectorAll("button, input, [tabindex]")).filter((x) => x.offsetParent !== null);
      if (!items.length) return;
      const first = items[0], last = items[items.length - 1];
      if (e.shiftKey && document.activeElement === first) { last.focus(); e.preventDefault(); }
      else if (!e.shiftKey && document.activeElement === last) { first.focus(); e.preventDefault(); }
    });
    content.addEventListener("click", (e) => {
      const nav = e.target.closest(".m-prev, .m-next");
      if (nav) { showChapter(nav.dataset.id); return; }
      const g = e.target.closest(".m-go");
      if (g) { close(); if (info.goto) info.goto(g.dataset.go); }
    });
    // The tour: pointing at a number or its line lights both.
    content.addEventListener("pointerover", (e) => {
      const t = e.target.closest("[data-pin]");
      content.querySelectorAll(".lit").forEach((x) => x.classList.remove("lit"));
      if (t) content.querySelectorAll('[data-pin="' + t.dataset.pin + '"]').forEach((x) => x.classList.add("lit"));
    });
    document.addEventListener("keydown", (e) => {
      if (e.key === "Escape" && isOpen() && !document.querySelector("#menuLayer .menu")) close();
    });
    let saved = null;
    try { saved = localStorage.getItem("sc.manual.chapter"); } catch (e) { /* storage may be unavailable */ }
    current = CHAPTERS.some((c) => c.id === saved) ? saved : CHAPTERS[0].id;
    fillToc();
  }

  let returnFocus = null;
  function open(id) {
    if (!root) return;
    if (!isOpen()) returnFocus = document.activeElement;
    root.classList.remove("hidden");
    showChapter(id || current);
    search.focus({ preventScroll: true });
  }
  function close() {
    if (!isOpen()) return;
    root.classList.add("hidden");
    if (returnFocus && returnFocus.focus && document.contains(returnFocus)) returnFocus.focus({ preventScroll: true });
    returnFocus = null;
  }
  function isOpen() { return !!root && !root.classList.contains("hidden"); }

  window.SCManual = { build, open, close, isOpen, chapters: CHAPTERS };
})();
