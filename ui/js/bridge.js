// Sanctuary Choir - bridge between the web UI and the plugin.
// Inside the plugin, messages travel through JUCE's WebBrowserComponent native integration.
// Opened in a normal browser, a demo backend simulates the plugin so the UI can be previewed.
(function () {
  "use strict";

  const listeners = {};
  function on(event, fn) { (listeners[event] = listeners[event] || []).push(fn); }
  function dispatch(event, payload) { (listeners[event] || []).forEach((fn) => fn(payload)); }

  const backend = window.__JUCE__ && window.__JUCE__.backend;
  const isPlugin = !!(backend && typeof backend.emitEvent === "function" && window.__JUCE__.postMessage);
  let send;

  if (isPlugin) {
    ["init", "params", "meters", "status", "toast", "lane", "chordOptions", "dragDone"].forEach((e) => backend.addEventListener(e, (p) => dispatch(e, p)));
    send = (msg) => backend.emitEvent("ui", msg);
  } else {
    const demo = createDemoBackend(dispatch);
    send = (msg) => demo.receive(msg);
  }

  window.SC = { on, send, isPlugin };

  //==========================================================================
  // Demo backend (browser preview only)
  function createDemoBackend(emit) {
    const pct = (id, name, def, max = 100) => ({ id, name, type: "float", min: 0, max, interval: 0.1, skew: 1, default: def, label: "%" });
    const db = (id, name, def, min, max) => ({ id, name, type: "float", min, max, interval: 0.1, skew: 2.2, default: def, label: "dB" });
    const choice = (id, name, choices, def = 0) => ({ id, name, type: "choice", min: 0, max: choices.length - 1, interval: 1, skew: 1, default: def, choices });
    const bool = (id, name, def = false) => ({ id, name, type: "bool", min: 0, max: 1, interval: 1, skew: 1, default: def ? 1 : 0 });
    const int = (id, name, min, max, def) => ({ id, name, type: "int", min, max, interval: 1, skew: 1, default: def });

    const defs = [
      db("volume", "Volume", 0, -60, 12),
      { id: "choirSize", name: "Choir Size", type: "float", min: 0, max: 100, interval: 1, skew: 1, default: 70, label: "%" },
      pct("space", "Space", 60), pct("reverb", "Reverb", 65),
      { id: "tone", name: "Tone", type: "float", min: -12, max: 12, interval: 0.1, skew: 1, default: 0, label: "dB" },
      pct("presence", "Presence", 30),
      choice("harmonyMode", "Harmony Mode", ["Full Choir", "Gospel Stack", "Open Voicing", "Unison & Octaves", "Thirds & Sixths", "Organum", "Upper Voices", "Lower Voices", "Drone Pad", "MIDI Chords"]),
      choice("key", "Key", ["Auto", "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]),
      choice("scale", "Scale", ["Major", "Minor", "Harmonic Minor", "Dorian", "Mixolydian", "Lydian", "Phrygian", "Gospel (Blues)", "Chromatic"]),
      choice("style", "Style", ["Sacred", "Gospel", "Gregorian", "Cinematic", "Worship", "Chamber", "Spiritual"]),
      choice("vocalType", "Vocal Type", ["Ah (Vowels)", "Oh (Vowels)", "Oo (Vowels)", "Ee (Vowels)", "Eh (Vowels)", "Mm (Hum)", "Words (Your Voice)"]),
      bool("freeze", "Freeze"),
      choice("response", "Response", ["With Me", "Echo (Right After)"]),
      choice("ensemble", "Ensemble", ["Choir", "Children", "Gang Vocals", "Congregation"]),
      choice("scene", "Scene", ["None", "Scene 1", "Scene 2", "Scene 3", "Scene 4", "Scene 5", "Scene 6", "Scene 7", "Scene 8"]),
    ];
    const ranges = [[60, 81], [53, 74], [48, 69], [40, 64]];
    ["sop", "alt", "ten", "bas"].forEach((p, i) => {
      const n = ["Soprano", "Alto", "Tenor", "Bass"][i];
      defs.push({ id: p + "Level", name: n + " Level", type: "float", min: 0, max: 1, interval: 0.001, skew: 1, default: 0.75 });
      defs.push({ id: p + "Pan", name: n + " Pan", type: "float", min: -1, max: 1, interval: 0.01, skew: 1, default: 0 });
      defs.push(bool(p + "Mute", n + " Mute"), bool(p + "Solo", n + " Solo"));
      defs.push(int(p + "Low", n + " Range Low", 24, 96, ranges[i][0]), int(p + "High", n + " Range High", 24, 96, ranges[i][1]));
    });
    defs.push(
      db("leadLevel", "Lead Vocal", 0, -60, 6), db("choirLevel", "Choir Level", 0, -60, 6),
      pct("humanize", "Humanize", 100, 200), pct("vibrato", "Vibrato", 100, 200), pct("glide", "Glide", 100, 300),
      { id: "formant", name: "Formant", type: "float", min: -100, max: 100, interval: 1, skew: 1, default: 0 },
      pct("pitchCorrection", "Pitch Correction", 60), pct("breath", "Breath", 100, 200), pct("harmonySpeed", "Chord Speed", 50),
      choice("venue", "Venue", ["Grand Cathedral", "Stone Abbey", "Wooden Chapel", "Concert Hall", "Gospel Church", "Studio Room"]),
      { id: "decay", name: "Decay", type: "float", min: 30, max: 250, interval: 1, skew: 1, default: 100, label: "%" },
      { id: "preDelay", name: "Pre-Delay", type: "float", min: 0, max: 150, interval: 0.1, skew: 1, default: 30, label: "ms" },
      pct("brightness", "Brightness", 50), pct("leadReverb", "Lead Reverb", 15),
      { id: "lowCut", name: "Low Cut", type: "float", min: 20, max: 400, interval: 1, skew: 0.5, default: 70, label: "Hz" },
      { id: "highCut", name: "High Cut", type: "float", min: 3000, max: 20000, interval: 10, skew: 0.5, default: 18000, label: "Hz" },
      pct("warmth", "Warmth", 0), pct("glue", "Glue", 25),
      choice("rhythmPattern", "Rhythm", ["Off", "Pulse 1/4", "Pulse 1/8", "Pulse 1/16", "Offbeats", "Dotted", "Gallop", "Syncopated", "Stutter",
        "Trance Gate", "Heartbeat", "Arp Up", "Arp Down", "Arp Bounce", "Call & Answer"]),
      choice("rhythmRate", "Rhythm Rate", ["1/8", "1/16", "1/32"], 1),
      { id: "rhythmLength", name: "Rhythm Length", type: "float", min: 5, max: 100, interval: 1, skew: 1, default: 60, label: "%" },
      pct("rhythmDepth", "Rhythm Depth", 100),
      choice("rhythmSections", "Rhythm Sections", ["Whole Choir", "Sopranos & Altos", "Tenors & Basses"]),
      bool("midiEnabled", "MIDI Control", true),
      bool("midiParts", "MIDI Parts (Ch 1-4)", false),
      bool("followBand", "Follow the Band", true),
      { id: "tuning", name: "Tuning (A4)", type: "float", min: 415, max: 466, interval: 0.1, skew: 1, default: 440, label: "Hz" },
      choice("latencyMode", "Monitoring", ["Studio (aligned)", "Live (low latency)"])
    );
    const values = {};
    defs.forEach((d) => { values[d.id] = d.default; d.value = d.default; });

    // Same list as the plugin's factory presets (plugin/Source/Presets.cpp).
    const presets = [
      ["Sacred & Classical", "Heavenly Praise"],
      ["Sacred & Classical", "Cathedral Hymn"],
      ["Sacred & Classical", "Angelic Voices"],
      ["Sacred & Classical", "Gregorian Chant"],
      ["Sacred & Classical", "Christmas Carols"],
      ["Sacred & Classical", "Chamber Choir"],
      ["Sacred & Classical", "Evensong"],
      ["Sacred & Classical", "Requiem"],
      ["Sacred & Classical", "Renaissance Motet"],
      ["Sacred & Classical", "Psalm Tone"],
      ["Worship", "Worship Night"],
      ["Worship", "Men of Praise"],
      ["Worship", "Unison Anthem"],
      ["Worship", "Big Room Worship"],
      ["Worship", "Altar Call"],
      ["Worship", "Prayer Room"],
      ["Worship", "Worship Pad"],
      ["Worship", "Bridge Swell"],
      ["Gospel", "Sunday Gospel"],
      ["Gospel", "Call & Response"],
      ["Gospel", "Spiritual Echoes"],
      ["Gospel", "Mass Choir"],
      ["Gospel", "Praise Break"],
      ["Gospel", "Gospel Quartet"],
      ["Gospel", "Sweet Hour"],
      ["Gospel", "Lining Out"],
      ["Cinematic", "Epic Cinematic"],
      ["Cinematic", "Dark Ritual"],
      ["Cinematic", "Ethereal Rise"],
      ["Cinematic", "Battle Hymn"],
      ["Cinematic", "Heroes Unison"],
      ["Cinematic", "Mystery"],
      ["Cinematic", "Trailer Hits"],
      ["Pop & Backing", "Backing Stack"],
      ["Pop & Backing", "Pop Doubles"],
      ["Pop & Backing", "Oohs & Aahs"],
      ["Pop & Backing", "Soul Harmonies"],
      ["Pop & Backing", "R&B Stack"],
      ["Pop & Backing", "Wide Octaves"],
      ["Pads & Drones", "Midnight Hum"],
      ["Pads & Drones", "Heavenly Drone"],
      ["Pads & Drones", "Endless Ah"],
      ["Pads & Drones", "Choir Pad"],
      ["Pads & Drones", "Warm Hum"],
      ["Pads & Drones", "Shimmer Pad"],
      ["Rhythmic", "Pulse Choir"],
      ["Rhythmic", "Trance Gate"],
      ["Rhythmic", "Choir Arpeggio"],
      ["Rhythmic", "Bouncing Arp"],
      ["Rhythmic", "Stutter Gospel"],
      ["Rhythmic", "Offbeat Choir"],
      ["Rhythmic", "Heartbeat"],
      ["Rhythmic", "Upper & Lower"],
      ["Rhythmic", "Galloping Choir"],
      ["Rhythmic", "Dotted Echoes"],
      ["Kids & Crowds", "Children's Choir"],
      ["Kids & Crowds", "Kids Christmas"],
      ["Kids & Crowds", "School Assembly"],
      ["Kids & Crowds", "Angel Children"],
      ["Kids & Crowds", "Gang Vocals"],
      ["Kids & Crowds", "Stadium Chant"],
      ["Kids & Crowds", "Hey! Shouts"],
      ["Kids & Crowds", "Congregation"],
      ["Kids & Crowds", "Church Sing-Along"],
    ].map(([category, name]) => ({ name, category, factory: true }));
    let presetIndex = 0;
    const learn = { phase: "empty", text: "Press LEARN and sing, or LOAD a vocal", progress: 0, hasTake: false, hasStems: false, stale: false, previewing: false };
    let learnTimer = null;

    const voices = { loading: false, name: "Sanctuary Voices", text: "4 SECTIONS \u00b7 5 VOWELS \u00b7 753 NOTES", error: "", bundled: true, usingBundled: true };
    const scenes = [true, true, false, false, false, false, false, false];
    // The chord lane of the simulated take: the hymn progression, two melody notes per chord.
    const RATE = 48000;
    const vocab = [["C", 0, [0, 4, 7]], ["Dm", 2, [0, 3, 7]], ["Em", 4, [0, 3, 7]], ["F", 5, [0, 4, 7]], ["G", 7, [0, 4, 7]], ["Am", 9, [0, 3, 7]],
      ["G7", 7, [0, 4, 7, 10]], ["Fmaj7", 5, [0, 4, 7, 11]], ["Bb", 10, [0, 4, 7]], ["Gsus4", 7, [0, 5, 7]], ["Bdim", 11, [0, 3, 6]]];
    const laneChords = ["C", "F", "G", "C", "Am", "Dm", "G", "C"];
    const laneMelody = [[64, 67], [69, 65], [67, 62], [64, 60], [64, 69], [65, 62], [62, 67], [64, 60]];
    let laneSpans = null, laneVersion = 0, exportState = "none";
    function makeLane() {
      laneSpans = laneChords.map((name, i) => {
        const v = vocab.find((c) => c[0] === name);
        return { s: i * 2 * RATE, e: (i + 1) * 2 * RATE, name, root: v[1], iv: v[2], locked: false, band: false, original: name };
      });
    }
    function sendLane() {
      laneVersion++;
      if (!learn.hasTake || !laneSpans) { emit("lane", null); return; }
      const melody = [];
      laneMelody.forEach((pair, i) => pair.forEach((p, k) => melody.push({ s: (i * 2 + k) * RATE + 2400, e: (i * 2 + k + 1) * RATE - 4800, p })));
      emit("lane", { version: laneVersion, rate: RATE, length: 16 * RATE, key: "C MAJOR", band: false, editable: true, songStart: 8, bar: 5, beat: 1,
        spans: laneSpans.map((sp) => ({ s: sp.s, e: sp.e, name: sp.name, root: sp.root, iv: sp.iv, locked: sp.locked, band: sp.band })), melody });
    }
    function chordOptions(s, e) {
      const i = Math.round(s / (2 * RATE));
      const sp = laneSpans[i];
      const pcs = laneMelody[i].map((p) => p % 12);
      const tones = (c) => c[2].map((iv) => (c[1] + iv) % 12);
      const options = vocab.map((c) => ({ name: c[0], root: c[1], iv: c[2], fit: pcs.filter((pc) => tones(c).includes(pc)).length / pcs.length, current: c[0] === sp.name }))
        .sort((a, b) => (b.current - a.current) * 0.5 + (b.fit - a.fit)).slice(0, 8);
      emit("chordOptions", { s, e, options });
    }

    function status() {
      learn.export = !learn.hasStems || learn.stale ? "none" : exportState;
      learn.tiedToTimeline = true;
      emit("status", { learn: Object.assign({}, learn), presets, presetIndex, presetName: presets[presetIndex] ? presets[presetIndex].name : "", voices: Object.assign({}, voices), scenes: scenes.slice() });
    }
    function setParam(id, v) {
      values[id] = v;
      const o = {}; o[id] = v;
      emit("params", o);
    }

    // Hymn progression in C for the simulated meters: [S, A, T, B] per chord.
    const progression = [[72, 67, 64, 48], [72, 69, 65, 53], [71, 67, 62, 55], [72, 67, 64, 48], [76, 72, 67, 57], [74, 69, 65, 50], [74, 71, 67, 55], [72, 67, 64, 48]];
    const chordNames = ["C", "F", "G", "C", "Am", "Dm", "G", "C"];
    const chordNamesG = ["G", "C", "D", "G", "Em", "Am", "D", "G"];
    let t0 = performance.now();
    function meters() {
      const t = (performance.now() - t0) / 1000;
      const beat = Math.floor(t / 1.4) % progression.length;
      const phase = (t % 11.2) / 11.2;
      const demoLevel = window.SC_DEMO_LEVEL;
      const singing = demoLevel !== undefined ? demoLevel > 0.02 : (phase < 0.9 && (t % 1.4) < 1.3);
      const env = demoLevel !== undefined ? Math.min(1, demoLevel * 2.2) : singing ? 0.55 + 0.25 * Math.sin(t * 2.1) + 0.12 * Math.random() : 0.02 * Math.random();
      const total = Math.round(values.choirSize / 100 * 40);
      const counts = [Math.round(total * 0.28), Math.round(total * 0.26), Math.round(total * 0.22)];
      counts.push(Math.max(0, total - counts[0] - counts[1] - counts[2]));
      const shift = demoLevel !== undefined ? -5 : 0;
      const sections = progression[beat].map((n0, i) => ({
        peak: counts[i] > 0 && !values[["sop", "alt", "ten", "bas"][i] + "Mute"] ? env * (0.8 + 0.4 * Math.random()) * values[["sop", "alt", "ten", "bas"][i] + "Level"] / 0.75 : 0,
        notes: singing && counts[i] > 0 ? [n0 + shift] : [],
        singers: counts[i],
      }));
      emit("meters", {
        sections, singers: total, leadVoiced: singing, leadMidi: progression[beat][0] - 12, leadNote: progression[beat][0] - 12,
        keyTonic: values.key > 0 ? values.key - 1 : (demoLevel !== undefined ? 7 : 0), keyMinor: [1, 2, 3, 6].includes(values.scale), keyConfidence: 0.8,
        chord: singing ? (demoLevel !== undefined ? chordNamesG : chordNames)[beat] : "", in: env * 0.6, out: env * 0.8, frozen: !!values.freeze,
        bpm: window.SC_DEMO_BEAT !== undefined ? 120 : 0, beat: window.SC_DEMO_BEAT,
        // The preview pretends a band is on the side-chain, playing the same progression.
        sidechain: true, bandHeard: singing, followingBand: singing && !!values.followBand,
        bandChord: singing ? (demoLevel !== undefined ? chordNamesG : chordNames)[beat] : "",
      });
    }

    function startLearnSim() {
      clearInterval(learnTimer);
      learn.phase = "recording"; learn.armed = false;
      let secs = 0;
      learnTimer = setInterval(() => {
        secs += 0.25;
        learn.text = "LEARNING 0:" + String(Math.floor(secs)).padStart(2, "0") + "  - press LEARN to finish";
        status();
      }, 250);
      status();
    }
    function stopLearnSim() {
      clearInterval(learnTimer);
      learn.phase = "analysing"; learn.text = "LISTENING TO YOUR TAKE..."; status();
      setTimeout(() => {
        learn.phase = "learned"; learn.hasTake = true; learn.stale = learn.hasStems;
        learn.text = "LEARNED 0:16 \u00b7 C MAJOR \u00b7 4 PHRASES \u00b7 16 NOTES";
        makeLane();
        status();
        sendLane();
      }, 1400);
    }
    function renderSim() {
      if (!learn.hasTake) { emit("toast", { text: "Learn or load a vocal take first" }); return; }
      clearInterval(learnTimer);
      learn.phase = "rendering"; learn.progress = 0;
      learnTimer = setInterval(() => {
        learn.progress = Math.min(1, learn.progress + 0.04);
        learn.text = "RENDERING " + Math.round(learn.progress * 100) + "%";
        if (learn.progress >= 1) {
          clearInterval(learnTimer);
          learn.phase = "rendered"; learn.hasStems = true; learn.stale = false; exportState = "stale";
          learn.text = "LEARNED 0:16 \u00b7 C MAJOR \u00b7 4 PHRASES \u00b7 16 NOTES  \u00b7  " + Math.round(values.choirSize / 100 * 40) + " SINGERS RENDERED IN 3.1 S";
        }
        status();
      }, 120);
    }

    function receive(msg) {
      if (msg.type === "ready") {
        emit("init", { params: defs.map((d) => Object.assign({}, d, { value: values[d.id] })), tab: "perform", version: "1.0.0 (preview)", standalone: false, latencyMs: 38.4, liveChoirMs: 14.8, maxSingers: 40 });
        status();
        setInterval(meters, 33);
      } else if (msg.type === "set") {
        setParam(msg.id, msg.value);
        if (learn.hasStems && ["harmonyMode", "key", "scale", "style", "vocalType", "choirSize"].includes(msg.id)) {
          learn.stale = true; if (learn.phase === "rendered") learn.phase = "learned";
          learn.text = "LEARNED 0:16 \u00b7 C MAJOR \u00b7 4 PHRASES \u00b7 16 NOTES  \u00b7  RENDER TO UPDATE";
          status();
        }
      } else if (msg.type === "cmd") {
        switch (msg.name) {
          case "learn": learn.phase === "recording" ? stopLearnSim() : startLearnSim(); break;
          case "clear": clearInterval(learnTimer); Object.assign(learn, { phase: "empty", text: "Press LEARN and sing, or LOAD a vocal", hasTake: false, hasStems: false, stale: false }); exportState = "none"; status(); sendLane(); break;
          case "lane": sendLane(); break;
          case "chordOptions": if (laneSpans) chordOptions(msg.s, msg.e); break;
          case "chordLock": case "chordUnlock": {
            const sp = laneSpans && laneSpans.find((x) => x.s === msg.s);
            if (sp) {
              const v = msg.name === "chordUnlock" ? null : vocab.find((c) => c[1] === msg.root && c[2].join() === msg.iv.join());
              Object.assign(sp, v ? { name: v[0], root: v[1], iv: v[2], locked: true } : { name: sp.original, locked: false });
              const o = vocab.find((c) => c[0] === sp.name); if (o) { sp.root = o[1]; sp.iv = o[2]; }
              if (learn.hasStems) { learn.stale = true; learn.phase = "learned"; learn.text = "LEARNED 0:16 \u00b7 C MAJOR \u00b7 4 PHRASES \u00b7 16 NOTES  \u00b7  RENDER TO UPDATE"; }
              status(); sendLane();
            }
            break;
          }
          case "chordUnlockAll": if (laneSpans) { laneSpans.forEach((sp) => { const o = vocab.find((c) => c[0] === sp.original); Object.assign(sp, { name: sp.original, root: o[1], iv: o[2], locked: false }); }); status(); sendLane(); } break;
          case "exportPrepare": if (exportState !== "ready") { exportState = "preparing"; status(); setTimeout(() => { exportState = "ready"; status(); }, 1200); } break;
          case "dragOut":
            emit("toast", { text: msg.item === "midi" ? "In the plugin, the MIDI file (parts and chords) drops onto a track in your DAW"
              : exportState === "ready" ? "In the plugin, the audio drops into your DAW, lined up with the start of your song"
              : "Render first: the audio comes from the rendered choir" });
            setTimeout(() => emit("dragDone", null), 50);
            break;
          case "render": renderSim(); break;
          case "load": emit("toast", { text: "In the plugin this opens a file browser for a vocal take" }); setTimeout(stopLearnSim, 300); break;
          case "export": emit("toast", { text: !learn.hasTake ? "Learn or load a take first" : learn.hasStems && !learn.stale ? "Saved to Documents/Sanctuary Choir/Exports" : "MIDI saved. RENDER to save the choir's audio too." }); break;
          case "voicesLoad":
            Object.assign(voices, { loading: true, name: "", text: "", error: "", usingBundled: false }); status();
            setTimeout(() => { Object.assign(voices, { loading: false, name: "St. Mark's Choir", text: "4 SECTIONS \u00b7 2 VOWELS \u00b7 96 NOTES" }); status(); }, 900);
            break;
          case "voicesBundled": Object.assign(voices, { loading: false, name: "Sanctuary Voices", text: "4 SECTIONS \u00b7 5 VOWELS \u00b7 753 NOTES", error: "", usingBundled: true }); status(); break;
          case "voicesClear": Object.assign(voices, { loading: false, name: "", text: "", error: "", usingBundled: false }); status(); break;
          case "preview": learn.previewing = !learn.previewing; status(); break;
          case "presetLoad": presetIndex = msg.index; status(); break;
          case "presetNext": presetIndex = (presetIndex + 1) % presets.length; status(); break;
          case "presetPrev": presetIndex = (presetIndex - 1 + presets.length) % presets.length; status(); break;
          case "sceneStore": scenes[msg.index] = true; status(); emit("toast", { text: "Scene " + (msg.index + 1) + " stored" }); break;
          case "sceneClear": scenes[msg.index] = false; status(); break;
          case "sceneRecall": setParam("scene", msg.index + 1); break;
          case "presetSave": presets.push({ name: msg.value, factory: false, category: "My Presets" }); presetIndex = presets.length - 1; status(); emit("toast", { text: "Saved preset \"" + msg.value + "\"" }); break;
          case "presetDelete": presets.splice(msg.index, 1); presetIndex = 0; status(); break;
          case "resetParam": { const d = defs.find((x) => x.id === msg.id); if (d) setParam(d.id, d.default); break; }
          default: break;
        }
      }
    }
    return { receive };
  }
})();
