// Sanctuary Choir - MIX, FX and SETTINGS pages.
(function () {
  "use strict";
  const { Params, MiniKnob, Fader, pill, toggleSwitch, noteName } = window.SCW;

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

  function noteStepper(parent, id, label) {
    const wrap = el("div", "stepper", parent);
    wrap.title = label === "LOW" ? "Lowest note this section sings" : "Highest note this section sings";
    const down = el("button", "", wrap, "◂");
    const val = el("span", "", wrap);
    const up = el("button", "", wrap, "▸");
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
    el("h3", "", bottom, "PERFORMANCE · HOW THE CHOIR SINGS");
    const knobs = el("div", "knob-row", bottom);
    [["humanize", "HUMANIZE"], ["vibrato", "VIBRATO"], ["glide", "GLIDE"], ["formant", "FORMANT"], ["pitchCorrection", "PITCH LOCK"], ["breath", "BREATH"]]
      .forEach(([id, label]) => new MiniKnob(knobs, id, label, { bipolar: id === "formant" }));
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
    const hint = el("p", "", page);
    hint.style.cssText = "text-align:center;margin-top:14px;color:#a8977c";
    hint.innerHTML = "SPACE spreads the singers across the stage and adds early reflections · REVERB sets how much of the room you hear";
  }

  function buildSettings(page, info) {
    page.innerHTML = "";
    el("h2", "", page, "SETTINGS");
    const row = el("div", "row", page);
    const left = el("div", "", row);
    left.style.cssText = "width:520px;display:flex;flex-direction:column;gap:12px";

    const midi = el("div", "group", left);
    el("h3", "", midi, "MIDI");
    toggleSwitch(midi, "midiEnabled", "MIDI keyboard controls the harmony");
    el("p", "", midi, "Hold chords to choose the choir's harmony. With no voice coming in, the choir sings the notes you play like an instrument.");

    const tune = el("div", "group", left);
    tune.style.cssText = "display:flex;align-items:center;gap:16px";
    new MiniKnob(tune, "tuning", "TUNING A4");
    const tt = el("p", "", tune, "Reference pitch for the harmony. Leave at 440 Hz unless your song is tuned differently.");
    tt.style.flex = "1";

    const take = el("div", "group", left);
    el("h3", "", take, "LEARNED TAKE");
    const btns = el("div", "", take);
    btns.style.cssText = "display:flex;gap:10px;flex-wrap:wrap";
    const preview = el("button", "big-btn", btns, "▶  PREVIEW");
    preview.addEventListener("click", () => SC.send({ type: "cmd", name: "preview" }));
    const exp = el("button", "big-btn", btns, "EXPORT STEMS");
    exp.addEventListener("click", () => SC.send({ type: "cmd", name: "export" }));
    const presetFolder = el("button", "big-btn", btns, "PRESETS FOLDER");
    presetFolder.addEventListener("click", () => SC.send({ type: "cmd", name: "presetFolder" }));
    const takeInfo = el("p", "", take, "");
    info.onStatus = (st) => {
      const l = st.learn || {};
      preview.disabled = !l.hasTake;
      preview.textContent = l.previewing ? "■  STOP PREVIEW" : "▶  PREVIEW";
      exp.disabled = !l.hasStems;
      takeInfo.textContent = l.hasTake ? (l.text || "") : "No take yet. Press LEARN and sing, or LOAD a vocal file.";
    };

    const right = el("div", "group", row);
    right.style.cssText = "flex:1";
    el("h3", "", right, "HOW TO USE SANCTUARY CHOIR");
    const ol = el("ol", "", right);
    ol.style.cssText = "margin:4px 0 0;padding-left:22px";
    [
      "<b>Sing</b> into the plugin on your vocal track. The choir harmonises you live in the key you choose (or Auto).",
      "<b>Choir Size</b> adds singers one by one. Up to 48 voices fill the loft.",
      "<b>Learn</b>: press it, play your track and press it again. Sanctuary studies the whole melody for smarter, phrase-aware harmony.",
      "<b>Render</b> sings every voice at the highest quality. It plays back in sync with your song, and you can export stems.",
      "<b>Freeze</b> holds the current chord forever, for pads and endings.",
      "<b>Vocal Type</b>: Words keeps your lyrics; Ah, Oh, Oo and Mm sing pure vowels.",
    ].forEach((html) => { const li = el("li", "", ol); li.innerHTML = html; });
    const foot = el("p", "", right);
    foot.style.cssText = "margin-top:12px;color:#8f7f66;font-size:15px";
    foot.textContent = "Sanctuary Choir " + (info.version || "") + "  ·  latency " + (info.latencyMs ? info.latencyMs.toFixed(1) : "?") + " ms  ·  " + (SC.isPlugin ? "plugin" : "browser preview");
  }

  window.SCPages = { SECTIONS, buildMix, buildFx, buildSettings };
})();
