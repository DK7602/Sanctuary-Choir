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
    ["init", "params", "meters", "status", "toast"].forEach((e) => backend.addEventListener(e, (p) => dispatch(e, p)));
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
      bool("midiEnabled", "MIDI Control", true),
      { id: "tuning", name: "Tuning (A4)", type: "float", min: 415, max: 466, interval: 0.1, skew: 1, default: 440, label: "Hz" },
      choice("latencyMode", "Monitoring", ["Studio (aligned)", "Live (low latency)"])
    );
    const values = {};
    defs.forEach((d) => { values[d.id] = d.default; d.value = d.default; });

    const presets = ["Heavenly Praise", "Cathedral Hymn", "Sunday Gospel", "Gregorian Chant", "Angelic Voices", "Men of Praise",
      "Epic Cinematic", "Worship Night", "Christmas Carols", "Chamber Choir", "Midnight Hum", "Spiritual Echoes", "Unison Anthem", "Heavenly Drone", "Call & Response"]
      .map((name) => ({ name, factory: true }));
    let presetIndex = 0;
    const learn = { phase: "empty", text: "Press LEARN and sing, or LOAD a vocal", progress: 0, hasTake: false, hasStems: false, stale: false, previewing: false };
    let learnTimer = null;

    const voices = { loading: false, name: "Sanctuary Voices", text: "4 SECTIONS \u00b7 5 VOWELS \u00b7 753 NOTES", error: "", bundled: true, usingBundled: true };
    function status() {
      emit("status", { learn: Object.assign({}, learn), presets, presetIndex, presetName: presets[presetIndex] ? presets[presetIndex].name : "", voices: Object.assign({}, voices) });
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
        learn.text = "LEARNED 0:43 \u00b7 C MAJOR \u00b7 4 PHRASES \u00b7 105 NOTES";
        status();
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
          learn.phase = "rendered"; learn.hasStems = true; learn.stale = false;
          learn.text = "LEARNED 0:43 \u00b7 C MAJOR \u00b7 4 PHRASES \u00b7 105 NOTES  \u00b7  " + Math.round(values.choirSize / 100 * 40) + " SINGERS RENDERED IN 3.1 S";
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
          learn.text = "LEARNED 0:43 \u00b7 C MAJOR \u00b7 4 PHRASES \u00b7 105 NOTES  \u00b7  RENDER TO UPDATE";
          status();
        }
      } else if (msg.type === "cmd") {
        switch (msg.name) {
          case "learn": learn.phase === "recording" ? stopLearnSim() : startLearnSim(); break;
          case "clear": clearInterval(learnTimer); Object.assign(learn, { phase: "empty", text: "Press LEARN and sing, or LOAD a vocal", hasTake: false, hasStems: false, stale: false }); status(); break;
          case "render": renderSim(); break;
          case "load": emit("toast", { text: "In the plugin this opens a file browser for a vocal take" }); setTimeout(stopLearnSim, 300); break;
          case "export": emit("toast", { text: learn.hasStems ? "Stems saved to Documents/Sanctuary Choir/Exports" : "Render first, then export the stems" }); break;
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
          case "presetSave": presets.push({ name: msg.value, factory: false }); presetIndex = presets.length - 1; status(); emit("toast", { text: "Saved preset \"" + msg.value + "\"" }); break;
          case "presetDelete": presets.splice(msg.index, 1); presetIndex = 0; status(); break;
          case "resetParam": { const d = defs.find((x) => x.id === msg.id); if (d) setParam(d.id, d.default); break; }
          default: break;
        }
      }
    }
    return { receive };
  }
})();
