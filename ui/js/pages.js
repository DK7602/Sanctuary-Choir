// Sanctuary Choir - MIX, FX and SETTINGS pages.
(function () {
  "use strict";
  const { Params, MiniKnob, Fader, Dropdown, pill, toggleSwitch, noteName } = window.SCW;

  const SECTIONS = [
    { prefix: "sop", name: "SOPRANO", color: "#f27fa0", glow: "rgba(242,127,160,0.55)" },
    { prefix: "alt", name: "ALTO", color: "#f3cf72", glow: "rgba(243,207,114,0.55)" },
    { prefix: "ten", name: "TENOR", color: "#82b8f2", glow: "rgba(130,184,242,0.55)" },
    { prefix: "bas", name: "BASS", color: "#7fe2c6", glow: "rgba(127,226,198,0.55)" },
  ];

  const el = (tag, cls, parent, text) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text !== undefined) e.textContent = text;
    if (parent) parent.appendChild(e);
    return e;
  };

  // A row of pills that pick one value of a choice parameter, with a small label in front.
  function choicePills(parent, id, label, options) {
    const row = el("div", "pill-row", parent);
    row.style.alignItems = "center";
    if (label) el("span", "pill-label", row, label);
    options.forEach(([text, v, tip]) => {
      const b = el("button", "pill", row, text);
      if (tip) b.title = tip;
      b.addEventListener("click", () => Params.set(id, v));
      Params.on(id, (x) => b.classList.toggle("on", Math.round(x) === v));
    });
    return row;
  }

  function noteStepper(parent, id, label) {
    const wrap = el("div", "stepper", parent);
    wrap.title = label === "LOW" ? "Lowest note this section sings" : "Highest note this section sings";
    const down = el("button", "", wrap, "\u25c2");
    const val = el("span", "", wrap);
    const up = el("button", "", wrap, "\u25b8");
    down.title = "Lower";
    up.title = "Higher";
    down.addEventListener("click", () => Params.set(id, Params.get(id) - 1));
    up.addEventListener("click", () => Params.set(id, Params.get(id) + 1));
    Params.on(id, (v) => { val.textContent = noteName(Math.round(v)); });
  }

  function buildMix(page) {
    page.innerHTML = "";
    el("h2", "", page, "MIX");
    const top = el("div", "row", page);
    top.style.justifyContent = "space-between";

    SECTIONS.forEach((s) => {
      const g = el("div", "group", top);
      g.style.cssText = "width:212px;display:flex;flex-direction:column;align-items:center;gap:4px;padding:9px 12px;--c:" + s.color + ";--cglow:" + s.glow;
      el("div", "strip-name", g, s.name).style.cssText = "font-family:Montserrat,sans-serif;font-weight:700;font-size:12px;letter-spacing:2.6px;color:" + s.color + ";text-shadow:0 0 10px " + s.glow;
      const body = el("div", "", g);
      body.style.cssText = "display:flex;gap:14px;align-items:center";
      const fcol = el("div", "", body);
      fcol.style.cssText = "display:flex;flex-direction:column;align-items:center;gap:4px";
      new Fader(fcol, s.prefix + "Level", s.color, true);
      const fv = el("div", "fader-val", fcol);
      Params.on(s.prefix + "Level", () => { fv.textContent = Params.format(s.prefix + "Level"); });
      const right = el("div", "", body);
      right.style.cssText = "display:flex;flex-direction:column;align-items:center;gap:8px";
      new MiniKnob(right, s.prefix + "Pan", "PAN", { bipolar: true, color: s.color });
      const pills = el("div", "pill-row", right);
      pill(pills, s.prefix + "Mute", "M", "mute");
      pill(pills, s.prefix + "Solo", "S");
      const rng = el("div", "", g);
      rng.style.cssText = "display:flex;align-items:center;gap:6px;margin-top:2px";
      noteStepper(rng, s.prefix + "Low", "LOW");
      el("span", "", rng, "\u2013").style.color = "#a8977c";
      noteStepper(rng, s.prefix + "High", "HIGH");
    });

    const master = el("div", "group", top);
    master.style.cssText = "width:190px;display:flex;justify-content:space-around";
    [["leadLevel", "LEAD"], ["choirLevel", "CHOIR"]].forEach(([id, name]) => {
      const col = el("div", "", master);
      col.style.cssText = "display:flex;flex-direction:column;align-items:center;gap:6px";
      el("div", "", col, name).style.cssText = "font-family:Montserrat,sans-serif;font-weight:700;font-size:12px;letter-spacing:2.6px;color:#ffe7b0";
      new Fader(col, id, "#f3d08a", true);
      const v = el("div", "fader-val", col);
      Params.on(id, () => { v.textContent = Params.format(id); });
    });

    const bottom = el("div", "group", page);
    bottom.style.cssText = "margin-top:10px;padding:8px 14px 2px";
    const head = el("div", "", bottom);
    head.style.cssText = "display:flex;align-items:center;gap:22px;margin-bottom:2px";
    el("h3", "", head, "PERFORMANCE").style.margin = "0";
    const ens = choicePills(head, "ensemble", "ENSEMBLE", [
      ["CHOIR", 0, "An adult choir: sopranos, altos, tenors and basses"],
      ["CHILDREN", 1, "A children's choir: light, straight voices, every part sung by trebles"],
      ["GANG", 2, "Gang vocals: shouted, punchy and loose, no vibrato"],
      ["CONGREGATION", 3, "A congregation: untrained voices, wide tuning, late entrances"],
    ]);
    ens.style.marginLeft = "auto";
    choicePills(head, "response", "RESPONSE", [
      ["WITH ME", 0, "The choir sings along with you"],
      ["ECHO", 1, "Call & response: the choir answers each phrase right after you sing it"],
    ]);
    const knobs = el("div", "knob-row", bottom);
    const tips = {
      harmonySpeed: "How readily the choir changes chord. Low holds chords through passing notes; high gives every note its own chord.",
    };
    [["humanize", "HUMANIZE"], ["vibrato", "VIBRATO"], ["glide", "GLIDE"], ["formant", "FORMANT"], ["pitchCorrection", "PITCH LOCK"], ["breath", "BREATH"], ["harmonySpeed", "CHORD SPEED"]]
      .forEach(([id, label]) => {
        const k = new MiniKnob(knobs, id, label, { bipolar: id === "formant" });
        if (tips[id] && k.el) k.el.title = tips[id];
      });
  }

  const VENUES = [
    ["Grand Cathedral", "Vast stone, 6 s tail"],
    ["Stone Abbey", "Cloisters, 4 s"],
    ["Wooden Chapel", "Warm & close, 2 s"],
    ["Concert Hall", "Clear & wide, 2.4 s"],
    ["Gospel Church", "Lively, 1.6 s"],
    ["Studio Room", "Dry & intimate"],
  ];

  function buildFx(page) {
    page.innerHTML = "";
    el("h2", "", page, "THE ROOM");
    const venues = el("div", "venues", page);
    VENUES.forEach(([name, desc], i) => {
      const card = el("div", "venue", venues);
      el("div", "vname", card, name);
      el("div", "vdesc", card, desc);
      card.addEventListener("click", () => Params.set("venue", i));
      Params.on("venue", (v) => card.classList.toggle("on", Math.round(v) === i));
    });
    const row = el("div", "row", page);
    const rev = el("div", "group", row);
    rev.style.flex = "1";
    el("h3", "", rev, "REVERB");
    const rk = el("div", "knob-row", rev);
    [["decay", "DECAY"], ["preDelay", "PRE-DELAY"], ["brightness", "BRIGHTNESS"], ["leadReverb", "LEAD SEND"]].forEach(([id, l]) => new MiniKnob(rk, id, l));
    const bus = el("div", "group", row);
    bus.style.flex = "1";
    el("h3", "", bus, "CHOIR BUS");
    const bk = el("div", "knob-row", bus);
    [["lowCut", "LOW CUT"], ["highCut", "HIGH CUT"], ["warmth", "WARMTH"], ["glue", "GLUE"]].forEach(([id, l]) => new MiniKnob(bk, id, l));
    venues.title = "SPACE spreads the singers across the stage and adds early reflections; REVERB sets how much of the room you hear";

    // Rhythm: tempo-synced gates and arpeggios on the choir.
    const rh = el("div", "group", page);
    rh.style.cssText = "margin-top:12px;padding:8px 14px 4px;display:flex;align-items:center;gap:22px";
    const rhLeft = el("div", "", rh);
    rhLeft.style.cssText = "display:flex;flex-direction:column;gap:8px;width:250px";
    el("h3", "", rhLeft, "RHYTHM").style.margin = "0";
    const dd = el("div", "dropdown field", rhLeft);
    dd.dataset.param = "rhythmPattern";
    dd.title = "Gate the choir in time with your song. The Arp patterns pass the chord from section to section.";
    new Dropdown(dd);
    const rhMid = el("div", "", rh);
    rhMid.style.cssText = "display:flex;flex-direction:column;gap:10px;--label-w:78px";
    choicePills(rhMid, "rhythmRate", "RATE", [["1/8", 0, "Half speed: eighth notes"], ["1/16", 1, "Sixteenth notes"], ["1/32", 2, "Double speed: thirty-second notes"]]);
    choicePills(rhMid, "rhythmSections", "SECTIONS", [["ALL", 0, "The whole choir"], ["S + A", 1, "Only sopranos and altos; tenors and basses keep singing"], ["T + B", 2, "Only tenors and basses; sopranos and altos keep singing"]]);
    const rhKnobs = el("div", "knob-row", rh);
    rhKnobs.style.cssText = "flex:1";
    new MiniKnob(rhKnobs, "rhythmLength", "LENGTH");
    new MiniKnob(rhKnobs, "rhythmDepth", "DEPTH");
    const rhNote = el("p", "", rh);
    rhNote.style.cssText = "width:250px;font-size:15px;line-height:1.3;color:#a8977c";
    rhNote.textContent = "Follows your DAW's tempo while it plays. Rendered takes play it live; exported stems are without it.";
  }

  function buildSettings(page, info) {
    page.innerHTML = "";
    el("h2", "", page, "SETTINGS");
    const row = el("div", "row", page);
    const left = el("div", "", row);
    left.style.cssText = "width:520px;display:flex;flex-direction:column;gap:12px";
    let foot = null;

    const midi = el("div", "group", left);
    el("h3", "", midi, "HARMONY FROM \u00b7 MIDI AND THE BAND");
    const midiRow = el("div", "", midi);
    midiRow.style.cssText = "display:flex;gap:10px 22px;flex-wrap:wrap";
    toggleSwitch(midiRow, "midiEnabled", "MIDI chords").title = "Hold chords to choose the harmony; with no voice coming in, the choir sings what you play. Program Change picks a preset.";
    toggleSwitch(midiRow, "midiParts", "Parts on ch. 1\u20134").title = "Channels 1, 2, 3 and 4 sing exactly the soprano, alto, tenor and bass notes you play";
    toggleSwitch(midiRow, "followBand", "Follow the band").title = "Sing the chords of the band, keys or backing track you route to Sanctuary's side-chain input";
    const bandText = el("p", "", midi);
    bandText.style.cssText = "font-size:15.5px;line-height:1.3";
    let lastBand = "";
    info.onMeters = (m) => {
      const follow = Params.get("followBand") > 0.5;
      const html = !m.sidechain ? "Route your band, keys or a backing track to the <b>side-chain</b> input and the choir sings its chords."
        : !m.bandHeard ? "Side-chain connected: play the band, keys or track into it."
        : !follow ? "Hearing the band; <b>Follow the band</b> is off."
        : "<b>Band</b>: the choir sings " + (m.bandChord ? "<b>" + m.bandChord.replace(/[<>&]/g, "") + "</b>" : "along") + ", the chords it hears.";
      if (html !== lastBand) { lastBand = html; bandText.innerHTML = html; }
    };
    info.onMeters({});

    const row2 = el("div", "", left);
    row2.style.cssText = "display:flex;gap:12px";
    const tune = el("div", "group", row2);
    tune.style.cssText = "display:flex;align-items:center;justify-content:center;padding:8px 10px";
    tune.title = "Reference pitch for the harmony. Leave at 440 Hz unless your song is tuned differently.";
    new MiniKnob(tune, "tuning", "TUNING A4");

    const ms = (v) => (v ? Math.round(v) : "?") + " ms";
    const mon = el("div", "group", row2);
    mon.style.cssText = "flex:1;padding:10px 14px";
    const monHead = el("div", "", mon);
    monHead.style.cssText = "display:flex;align-items:center;justify-content:space-between;margin-bottom:4px";
    el("h3", "", monHead, "MONITORING").style.margin = "0";
    const monPills = el("div", "pill-row", monHead);
    const monText = el("p", "", mon);
    monText.style.cssText = "font-size:16px;line-height:1.3;margin-top:6px";
    const modes = [
      ["STUDIO", 0, "Voice and choir perfectly aligned; your DAW compensates the delay"],
      ["LIVE", 1, "Hear your own voice instantly while you sing"],
    ];
    modes.forEach(([label, v, tip]) => {
      const b = el("button", "pill", monPills, label);
      b.title = tip;
      b.style.minWidth = "64px";
      if (info.standalone) b.disabled = true;
      else b.addEventListener("click", () => Params.set("latencyMode", v));
      const sync = (x) => b.classList.toggle("on", info.standalone ? v === 1 : Math.round(x) === v);
      Params.on("latencyMode", sync);
      sync(Params.get("latencyMode"));
    });
    const describe = (x) => {
      const live = info.standalone || Math.round(x) === 1;
      monText.innerHTML = live
        ? "<b>Live</b>: you hear your voice instantly. The choir follows about " + ms(info.liveChoirMs) + " behind (Words about " + ms(info.latencyMs) + ")."
          + (info.standalone ? " The standalone app always runs live." : " Switch to Studio for mixing.")
        : "<b>Studio</b>: voice and choir line up perfectly; your DAW compensates the " + ms(info.latencyMs) + " delay. Use Live while you record.";
      foot.textContent = "Sanctuary Choir " + (info.version || "") + "  \u00b7  " + (live ? "live monitoring" : "latency " + (info.latencyMs ? info.latencyMs.toFixed(1) : "?") + " ms")
        + "  \u00b7  " + (SC.isPlugin ? (info.standalone ? "standalone" : "plugin") : "browser preview")
        + (info.monoOutput ? "  \u00b7  mono track (use a stereo track for the full stage)" : "");
    };

    const take = el("div", "group", left);
    el("h3", "", take, "LEARNED TAKE");
    const btns = el("div", "", take);
    btns.style.cssText = "display:flex;gap:10px;flex-wrap:wrap";
    const preview = el("button", "big-btn", btns, "\u25b6  PREVIEW");
    preview.addEventListener("click", () => SC.send({ type: "cmd", name: "preview" }));
    const exp = el("button", "big-btn", btns, "EXPORT STEMS");
    exp.addEventListener("click", () => SC.send({ type: "cmd", name: "export" }));
    const presetFolder = el("button", "big-btn", btns, "PRESETS FOLDER");
    presetFolder.addEventListener("click", () => SC.send({ type: "cmd", name: "presetFolder" }));
    const takeInfo = el("p", "", take, "");
    info.onStatus = (st) => {
      if (st.latencyMs !== undefined && (st.latencyMs !== info.latencyMs || st.liveChoirMs !== info.liveChoirMs || !!st.monoOutput !== !!info.monoOutput)) {
        info.latencyMs = st.latencyMs;
        info.liveChoirMs = st.liveChoirMs;
        info.monoOutput = !!st.monoOutput;
        describe(Params.get("latencyMode"));
      }
      showVoices(st.voices);
      storedSlots = st.scenes || [];
      showScenes();
      const l = st.learn || {};
      preview.disabled = !l.hasTake;
      preview.textContent = l.previewing ? "\u25a0  STOP PREVIEW" : "\u25b6  PREVIEW";
      exp.disabled = !l.hasStems;
      takeInfo.textContent = l.hasTake ? (l.text || "") : "No take yet. Press LEARN and sing, or LOAD a vocal file.";
    };

    const rightCol = el("div", "", row);
    rightCol.style.cssText = "flex:1;display:flex;flex-direction:column;gap:12px;min-width:0";

    // Recorded choir voices for the vowel modes.
    const voices = el("div", "group", rightCol);
    voices.style.cssText = "padding:10px 14px";
    const vHead = el("div", "", voices);
    vHead.style.cssText = "display:flex;align-items:center;justify-content:space-between;gap:8px";
    el("h3", "", vHead, "CHOIR VOICES").style.margin = "0";
    const vBtns = el("div", "pill-row", vHead);
    const vChoice = (label, tip, cmd) => {
      const b = el("button", "pill", vBtns, label);
      b.title = tip;
      b.addEventListener("click", () => SC.send({ type: "cmd", name: cmd }));
      return b;
    };
    const vSanctuary = vChoice("SANCTUARY", "Real recorded singers, installed with Sanctuary Choir", "voicesBundled");
    const vOwn = vChoice("YOUR OWN\u2026", "Load a folder with recordings of your own singers (see the guide)", "voicesLoad");
    const vBuiltIn = vChoice("SYNTH", "The built-in synthesised voices", "voicesClear");
    const vText = el("p", "", voices);
    vText.style.cssText = "font-size:15px;line-height:1.3;margin-top:6px";
    const showVoices = (v) => {
      v = v || {};
      const own = !!v.name && !v.usingBundled;
      vSanctuary.classList.toggle("on", !!v.usingBundled);
      vOwn.classList.toggle("on", own || !!v.loading && !v.usingBundled);
      vBuiltIn.classList.toggle("on", !v.name && !v.loading);
      vSanctuary.disabled = v.bundled === false;
      if (v.loading) vText.innerHTML = "<b>Loading</b> the recorded voices\u2026";
      else if (v.error) vText.textContent = v.error;
      else if (v.usingBundled) vText.innerHTML = "<b>Sanctuary Voices</b>: real singers sing the vowel modes (Ah, Oh, Oo, Ee, Eh). Recordings from VocalSet by Wilkins, Seetharaman, Wahl & Pardo, CC BY 4.0.";
      else if (own) vText.innerHTML = "<b>" + v.name.replace(/[<>&]/g, "") + "</b> \u00b7 " + (v.text || "") + ": your singers sing the vowel modes.";
      else vText.innerHTML = "Synthesised voices sing the vowel modes." + (v.bundled === false ? " Install Sanctuary Voices for real recorded singers." : "");
    };
    showVoices(null);

    // Scenes: store the sound in a slot, recall it with a click or by automating Scene.
    const scenes = el("div", "group", rightCol);
    scenes.style.cssText = "padding:10px 14px";
    const sHead = el("div", "", scenes);
    sHead.style.cssText = "display:flex;align-items:center;gap:10px";
    el("h3", "", sHead, "SCENES").style.margin = "0";
    const slotRow = el("div", "pill-row", sHead);
    slotRow.style.marginLeft = "auto";
    let sceneMode = "recall";
    const slots = [];
    for (let i = 0; i < 8; i++) {
      const b = el("button", "pill scene", slotRow, String(i + 1));
      b.addEventListener("click", () => {
        SC.send({ type: "cmd", name: sceneMode === "store" ? "sceneStore" : sceneMode === "clear" ? "sceneClear" : "sceneRecall", index: i });
        setMode("recall");
      });
      slots.push(b);
    }
    const modeRow = el("div", "pill-row", sHead);
    const storeBtn = el("button", "pill", modeRow, "STORE");
    storeBtn.title = "Then click a slot to store the current sound in it";
    const clearBtn = el("button", "pill", modeRow, "CLEAR");
    clearBtn.title = "Then click a slot to empty it";
    const sText = el("p", "", scenes);
    sText.style.cssText = "font-size:15px;line-height:1.3;margin-top:6px";
    function setMode(m) {
      sceneMode = m;
      storeBtn.classList.toggle("on", m === "store");
      clearBtn.classList.toggle("on", m === "clear");
      sText.innerHTML = m === "store" ? "Click a slot to store the current sound in it."
        : m === "clear" ? "Click a slot to empty it."
        : "<b>Store</b> a sound for each part of your song, then click a slot (or automate <b>Scene</b> in your DAW) to switch.";
    }
    storeBtn.addEventListener("click", () => setMode(sceneMode === "store" ? "recall" : "store"));
    clearBtn.addEventListener("click", () => setMode(sceneMode === "clear" ? "recall" : "clear"));
    setMode("recall");
    let storedSlots = [];
    const showScenes = () => {
      const cur = Math.round(Params.get("scene")) - 1;
      slots.forEach((b, i) => {
        b.classList.toggle("stored", !!storedSlots[i]);
        b.classList.toggle("on", i === cur && !!storedSlots[i]);
        b.title = storedSlots[i] ? "Scene " + (i + 1) + ": click to bring it back" : "Scene " + (i + 1) + " is empty: STORE, then click here";
      });
    };
    Params.on("scene", showScenes);

    const right = el("div", "group", rightCol);
    right.style.cssText = "flex:1";
    el("h3", "", right, "HOW TO USE SANCTUARY CHOIR");
    const ol = el("ol", "", right);
    ol.style.cssText = "margin:4px 0 0;padding-left:22px";
    [
      "<b>Sing</b> into the plugin on a stereo track; <b>Learn</b>, then <b>Render</b> for the best quality.",
      "<b>Ensemble</b> and <b>Chord Speed</b> are on MIX, <b>Rhythm</b> on FX.",
    ].forEach((html) => { const li = el("li", "", ol); li.innerHTML = html; li.style.fontSize = "15.5px"; });
    foot = el("p", "", right);
    foot.style.cssText = "margin-top:8px;color:#8f7f66;font-size:14px";
    Params.on("latencyMode", describe);
    describe(Params.get("latencyMode"));
  }

  window.SCPages = { SECTIONS, buildMix, buildFx, buildSettings };
})();
