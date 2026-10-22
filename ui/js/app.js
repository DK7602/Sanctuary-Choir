// Sanctuary Choir - main UI wiring.
(function () {
  "use strict";
  const { Params, BigKnob, Dropdown, Menu, hiDpiCanvas, stageRect, noteName } = window.SCW;
  const { SECTIONS, buildMix, buildFx, buildSettings } = window.SCPages;
  const Choir = window.SCChoir;
  const $ = (id) => document.getElementById(id);

  const KEY_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
  const MAX_ENGINE_SINGERS = 48;

  // Layout of the four section panels (design-space pixels, measured from the mockup).
  const SECTION_LAYOUT = [
    { icon: [253, 888], level: [304, 912, 146], wave: [243, 930, 207, 26], keys: [233, 960, 239, 49], range: [356, 1012] },
    { icon: [509, 888], level: [559, 912, 138], wave: [490, 930, 207, 26], keys: [481, 960, 239, 49], range: [606, 1012] },
    { icon: [758, 888], level: [808, 912, 137], wave: [741, 930, 215, 26], keys: [731, 960, 238, 49], range: [848, 1012] },
    { icon: [1005, 888], level: [1055, 912, 139], wave: [987, 930, 207, 26], keys: [979, 960, 238, 49], range: [1095, 1012] },
  ];

  const state = { info: {}, tab: "perform", meters: null, status: null, detectedKey: 0, detectedMinor: false, sections: [], built: false, lastSizeChange: 0, lastActivity: 0 };

  //==========================================================================
  // Stage scaling
  function rescale() {
    const vw = window.innerWidth, vh = window.innerHeight;
    const s = Math.min(vw / 1448, vh / 1086);
    const stage = $("stage");
    stage.style.transform = "scale(" + s + ")";
    stage.style.left = Math.max(0, (vw - 1448 * s) / 2) + "px";
    stage.style.top = Math.max(0, (vh - 1086 * s) / 2) + "px";
    const changed = Math.abs((window.SC_STAGE_SCALE || 0) - s) > 0.01;
    window.SC_STAGE_SCALE = s;
    if (changed) {
      clearTimeout(rescale.t);
      rescale.t = setTimeout(() => window.dispatchEvent(new Event("sc-rescale")), 120);
    }
  }
  window.addEventListener("resize", rescale);
  rescale();
  document.addEventListener("contextmenu", (e) => e.preventDefault());

  //==========================================================================
  // Sections: icon (mute), level bar, waveform, keyboard, range label
  class SectionPanel {
    constructor(index) {
      const L = SECTION_LAYOUT[index], S = SECTIONS[index];
      this.index = index;
      this.prefix = S.prefix;
      this.color = S.color;
      this.history = new Array(110).fill(0);
      this.notes = [];
      const root = document.createElement("div");
      root.className = "section";
      root.style.cssText = "--c:" + S.color + ";--cglow:" + S.glow + ";--clight:" + lighten(S.color);
      $("sections").appendChild(root);
      this.root = root;

      const icon = document.createElement("div");
      icon.className = "icon";
      icon.style.left = L.icon[0] + "px";
      icon.style.top = L.icon[1] + "px";
      icon.innerHTML = '<div class="mute-shade"></div>';
      icon.title = S.name.charAt(0) + S.name.slice(1).toLowerCase() + ": click to mute, double-click to solo";
      icon.addEventListener("click", () => {
        clearTimeout(this.clickT);
        this.clickT = setTimeout(() => Params.set(this.prefix + "Mute", Params.get(this.prefix + "Mute") > 0.5 ? 0 : 1), 220);
      });
      icon.addEventListener("dblclick", () => { clearTimeout(this.clickT); Params.set(this.prefix + "Solo", Params.get(this.prefix + "Solo") > 0.5 ? 0 : 1); });
      root.appendChild(icon);

      const level = document.createElement("div");
      level.className = "level";
      level.style.cssText = "left:" + L.level[0] + "px;top:" + L.level[1] + "px;width:" + L.level[2] + "px";
      level.innerHTML = '<div class="track"></div><div class="fill"></div>';
      level.title = S.name.charAt(0) + S.name.slice(1).toLowerCase() + " level";
      root.appendChild(level);
      this.fill = level.querySelector(".fill");
      this.levelWidth = L.level[2];
      const id = this.prefix + "Level";
      let dragging = false;
      const setFromX = (e) => {
        const r = level.getBoundingClientRect();
        Params.setNorm(id, (e.clientX - r.left) / r.width);
      };
      level.addEventListener("pointerdown", (e) => { dragging = true; level.setPointerCapture(e.pointerId); Params.begin(id); setFromX(e); });
      level.addEventListener("pointermove", (e) => { if (dragging) setFromX(e); });
      const endDrag = () => { if (dragging) { dragging = false; Params.end(id); } };
      level.addEventListener("pointerup", endDrag);
      level.addEventListener("pointercancel", endDrag);
      level.addEventListener("dblclick", () => { Params.begin(id); Params.reset(id); Params.end(id); });

      this.wave = document.createElement("canvas");
      this.wave.style.cssText = "left:" + L.wave[0] + "px;top:" + L.wave[1] + "px;width:" + L.wave[2] + "px;height:" + L.wave[3] + "px";
      this.waveSize = [L.wave[2], L.wave[3]];
      root.appendChild(this.wave);

      this.keys = document.createElement("canvas");
      this.keys.className = "keys";
      this.keys.style.cssText = "left:" + L.keys[0] + "px;top:" + L.keys[1] + "px;width:" + L.keys[2] + "px;height:" + L.keys[3] + "px";
      this.keysSize = [L.keys[2], L.keys[3]];
      this.keys.title = "Drag to set the singing range";
      root.appendChild(this.keys);
      this.bindRangeDrag();

      this.range = document.createElement("div");
      this.range.className = "range";
      this.range.style.left = L.range[0] + "px";
      this.range.style.top = L.range[1] + "px";
      root.appendChild(this.range);

      Params.on(this.prefix + "Level", () => this.drawLevel());
      Params.on(this.prefix + "Mute", (v) => root.classList.toggle("muted", v > 0.5));
      Params.on(this.prefix + "Solo", (v) => root.classList.toggle("soloed", v > 0.5));
      Params.on(this.prefix + "Low", () => this.drawKeys());
      Params.on(this.prefix + "High", () => this.drawKeys());
      window.addEventListener("sc-rescale", () => { this.drawKeys(); this.drawWave(); });
    }

    drawLevel() { this.fill.style.width = (Params.norm(this.prefix + "Level") * this.levelWidth) + "px"; }

    span() {
      const lo = Params.get(this.prefix + "Low"), hi = Params.get(this.prefix + "High");
      const start = 12 * Math.floor(((lo + hi) / 2 - 18) / 12);
      return { start, end: start + 36, lo, hi };
    }

    keyGeometry() {
      const { start, end } = this.span();
      const [w, h] = this.keysSize;
      const whites = [];
      for (let n = start; n <= end; ++n) if (![1, 3, 6, 8, 10].includes(((n % 12) + 12) % 12)) whites.push(n);
      const ww = (w - 4) / whites.length;
      const geo = [];
      whites.forEach((n, i) => geo.push({ n, black: false, x: 2 + i * ww, w: ww, h: h - 4 }));
      for (let n = start; n <= end; ++n) {
        if (![1, 3, 6, 8, 10].includes(((n % 12) + 12) % 12)) continue;
        const left = whites.indexOf(n - 1);
        if (left < 0) continue;
        geo.push({ n, black: true, x: 2 + (left + 1) * ww - ww * 0.31, w: ww * 0.62, h: (h - 4) * 0.6 });
      }
      return geo;
    }

    drawKeys() {
      const [w, h] = this.keysSize;
      const c = hiDpiCanvas(this.keys, w, h);
      const { lo, hi } = this.span();
      const active = new Set(this.notes);
      c.clearRect(0, 0, w, h);
      roundRect(c, 0, 0, w, h, 4);
      c.fillStyle = "#070505";
      c.fill();
      const geo = this.keyGeometry();
      const col = this.color;
      for (const k of geo) {
        const inRange = k.n >= lo && k.n <= hi;
        const on = active.has(k.n);
        const y = 2;
        if (!k.black) {
          const g = c.createLinearGradient(0, y, 0, y + k.h);
          if (on) { g.addColorStop(0, "#ffffff"); g.addColorStop(1, col); }
          else if (inRange) { g.addColorStop(0, mix("#f6f1e7", col, 0.45)); g.addColorStop(1, mix("#cbbfae", col, 0.75)); }
          else { g.addColorStop(0, "#9b958c"); g.addColorStop(1, "#6d675f"); }
          c.fillStyle = g;
          roundRect(c, k.x + 0.5, y, k.w - 1, k.h, 2);
          c.fill();
          if (on) {
            c.save(); c.shadowColor = col; c.shadowBlur = 10; c.fillStyle = "rgba(255,255,255,0.35)";
            roundRect(c, k.x + 1, y + k.h * 0.55, k.w - 2, k.h * 0.4, 2); c.fill(); c.restore();
          }
        }
      }
      for (const k of geo) {
        if (!k.black) continue;
        const inRange = k.n >= lo && k.n <= hi;
        const on = active.has(k.n);
        const g = c.createLinearGradient(0, 2, 0, 2 + k.h);
        if (on) { g.addColorStop(0, lighten(col)); g.addColorStop(1, col); }
        else if (inRange) { g.addColorStop(0, mix("#2b2421", col, 0.35)); g.addColorStop(1, "#0d0a09"); }
        else { g.addColorStop(0, "#26221f"); g.addColorStop(1, "#0b0909"); }
        c.fillStyle = g;
        roundRect(c, k.x, 2, k.w, k.h, 1.5);
        c.fill();
      }
      // Soft glow over the sung keys.
      if (active.size) {
        c.save();
        c.globalCompositeOperation = "screen";
        for (const k of geo) {
          if (!active.has(k.n)) continue;
          const gx = k.x + k.w / 2, gy = h * 0.55;
          const g = c.createRadialGradient(gx, gy, 0, gx, gy, 18);
          g.addColorStop(0, hexA(col, 0.55)); g.addColorStop(1, hexA(col, 0));
          c.fillStyle = g;
          c.fillRect(gx - 18, 0, 36, h);
        }
        c.restore();
      }
      this.range.textContent = noteName(lo) + " - " + noteName(hi);
    }

    bindRangeDrag() {
      let dragging = null;
      const noteAt = (e) => {
        const r = this.keys.getBoundingClientRect();
        const x = (e.clientX - r.left) / r.width * this.keysSize[0];
        const y = (e.clientY - r.top) / r.height * this.keysSize[1];
        const geo = this.keyGeometry();
        const black = geo.find((k) => k.black && x >= k.x && x <= k.x + k.w && y <= k.h + 2);
        if (black) return black.n;
        const white = geo.find((k) => !k.black && x >= k.x && x <= k.x + k.w);
        return white ? white.n : null;
      };
      this.keys.addEventListener("pointerdown", (e) => {
        const n = noteAt(e);
        if (n === null) return;
        const { lo, hi } = this.span();
        dragging = Math.abs(n - lo) <= Math.abs(n - hi) ? "Low" : "High";
        this.keys.setPointerCapture(e.pointerId);
        Params.begin(this.prefix + dragging);
        this.setEnd(dragging, n);
      });
      this.keys.addEventListener("pointermove", (e) => { if (dragging) { const n = noteAt(e); if (n !== null) this.setEnd(dragging, n); } });
      const end = () => { if (dragging) { Params.end(this.prefix + dragging); dragging = null; } };
      this.keys.addEventListener("pointerup", end);
      this.keys.addEventListener("pointercancel", end);
    }

    setEnd(which, n) {
      const lo = Params.get(this.prefix + "Low"), hi = Params.get(this.prefix + "High");
      if (which === "Low") Params.set(this.prefix + "Low", Math.min(n, hi - 5));
      else Params.set(this.prefix + "High", Math.max(n, lo + 5));
    }

    pushMeter(peak, notes) {
      this.history.push(Math.min(1, Math.sqrt(peak) * 1.15));
      this.history.shift();
      const key = notes.join(",");
      if (key !== this.notesKey) { this.notesKey = key; this.notes = notes; this.drawKeys(); }
      this.drawWave();
    }

    drawWave() {
      const [w, h] = this.waveSize;
      const c = this.waveCtx && this.waveScale === window.SC_STAGE_SCALE ? this.waveCtx : (this.waveCtx = hiDpiCanvas(this.wave, w, h));
      this.waveScale = window.SC_STAGE_SCALE;
      c.clearRect(0, 0, w, h);
      const mid = h / 2;
      c.fillStyle = hexA(this.color, 0.28);
      c.fillRect(0, mid - 0.5, w, 1);
      c.save();
      c.shadowColor = this.color;
      c.shadowBlur = 5;
      c.fillStyle = hexA(this.color, 0.9);
      const n = this.history.length, step = w / n;
      for (let i = 0; i < n; ++i) {
        const v = this.history[i];
        if (v < 0.01) continue;
        const jitter = 0.55 + 0.45 * Math.abs(Math.sin(i * 12.9898 + this.index));
        const hh = Math.max(1, v * jitter * (h / 2 - 1));
        c.globalAlpha = 0.35 + 0.65 * (i / n);
        c.fillRect(i * step, mid - hh, Math.max(1, step * 0.6), hh * 2);
      }
      c.restore();
    }
  }

  //==========================================================================
  // Helpers
  function roundRect(c, x, y, w, h, r) {
    c.beginPath();
    c.moveTo(x + r, y); c.lineTo(x + w - r, y); c.quadraticCurveTo(x + w, y, x + w, y + r);
    c.lineTo(x + w, y + h - r); c.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    c.lineTo(x + r, y + h); c.quadraticCurveTo(x, y + h, x, y + h - r);
    c.lineTo(x, y + r); c.quadraticCurveTo(x, y, x + r, y); c.closePath();
  }
  function hexToRgb(h) { const n = parseInt(h.slice(1), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; }
  function hexA(h, a) { const [r, g, b] = hexToRgb(h); return "rgba(" + r + "," + g + "," + b + "," + a + ")"; }
  function mix(a, b, t) { const A = hexToRgb(a), B = hexToRgb(b); return "rgb(" + A.map((v, i) => Math.round(v + (B[i] - v) * t)).join(",") + ")"; }
  function lighten(h) { return mix(h, "#ffffff", 0.45); }

  let toastTimer = null;
  function toast(text) {
    const t = $("toast");
    t.textContent = text;
    t.classList.remove("hidden");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => t.classList.add("hidden"), 3800);
  }

  //==========================================================================
  // Tabs and pages
  function setTab(tab) {
    state.tab = tab;
    document.querySelectorAll(".tab").forEach((b) => b.classList.toggle("active", b.dataset.tab === tab));
    ["mix", "fx", "settings"].forEach((p) => $("page-" + p).classList.toggle("hidden", p !== tab));
    SC.send({ type: "cmd", name: "tab", value: tab });
    Menu.close();
  }

  //==========================================================================
  // Presets
  function openPresetMenu() {
    const st = state.status || {};
    const presets = st.presets || [];
    Menu.open(stageRect($("presetBox")), (m) => {
      Menu.heading(m, "FACTORY");
      presets.forEach((p, i) => { if (p.factory) Menu.item(m, p.name, i === st.presetIndex, () => SC.send({ type: "cmd", name: "presetLoad", index: i })); });
      const user = presets.map((p, i) => [p, i]).filter(([p]) => !p.factory);
      if (user.length) {
        Menu.sep(m);
        Menu.heading(m, "MY PRESETS");
        user.forEach(([p, i]) => {
          const it = Menu.item(m, p.name, i === st.presetIndex, () => SC.send({ type: "cmd", name: "presetLoad", index: i }));
          const del = document.createElement("span");
          del.textContent = "✕";
          del.title = "Delete preset";
          del.style.cssText = "margin-left:auto;color:#a8977c;font-size:13px;padding:0 4px";
          del.addEventListener("click", (e) => { e.stopPropagation(); SC.send({ type: "cmd", name: "presetDelete", index: i }); Menu.close(); });
          it.appendChild(del);
        });
      }
      Menu.sep(m);
      const input = document.createElement("input");
      input.placeholder = "Name your sound...";
      input.addEventListener("keydown", (e) => {
        e.stopPropagation();
        if (e.key === "Enter" && input.value.trim()) { SC.send({ type: "cmd", name: "presetSave", value: input.value.trim() }); Menu.close(); }
      });
      m.appendChild(input);
      Menu.item(m, "SAVE CURRENT SOUND", false, () => { if (input.value.trim()) SC.send({ type: "cmd", name: "presetSave", value: input.value.trim() }); }, "action");
      Menu.item(m, "OPEN PRESETS FOLDER", false, () => SC.send({ type: "cmd", name: "presetFolder" }), "action");
    });
  }

  //==========================================================================
  // Build after the backend describes the parameters.
  function build(info) {
    state.info = info;
    Params.define(info.params);
    if (state.built) { Object.keys(Params.values).forEach((id) => Params.update(id, Params.values[id])); return; }
    state.built = true;

    document.querySelectorAll(".knob.big").forEach((el) => new BigKnob(el));
    document.querySelectorAll(".dropdown").forEach((el) => {
      const opts = {};
      if (el.dataset.param === "key") {
        opts.label = () => {
          const v = Math.round(Params.get("key"));
          if (v > 0) return Params.format("key");
          return { main: "Auto", extra: "· " + KEY_NAMES[state.detectedKey] + (state.detectedMinor ? "m" : "") };
        };
      }
      const dd = new Dropdown(el, opts);
      if (el.dataset.param === "key") state.keyDropdown = dd;
    });
    state.sections = SECTIONS.map((_, i) => new SectionPanel(i));

    buildMix($("page-mix"));
    buildFx($("page-fx"));
    const settingsInfo = { version: info.version, latencyMs: info.latencyMs };
    buildSettings($("page-settings"), settingsInfo);
    state.settingsInfo = settingsInfo;

    document.querySelectorAll(".tab").forEach((b) => b.addEventListener("click", () => setTab(b.dataset.tab)));
    $("gearBtn").addEventListener("click", () => setTab(state.tab === "settings" ? "perform" : "settings"));
    $("presetBox").addEventListener("click", openPresetMenu);
    $("presetPrev").addEventListener("click", () => SC.send({ type: "cmd", name: "presetPrev" }));
    $("presetNext").addEventListener("click", () => SC.send({ type: "cmd", name: "presetNext" }));
    $("learnBtn").addEventListener("click", () => SC.send({ type: "cmd", name: "learn" }));
    $("loadBtn").addEventListener("click", () => SC.send({ type: "cmd", name: "load" }));
    $("clearBtn").addEventListener("click", () => SC.send({ type: "cmd", name: "clear" }));
    $("renderBtn").addEventListener("click", () => SC.send({ type: "cmd", name: "render" }));
    $("freezeBtn").addEventListener("click", () => Params.set("freeze", Params.get("freeze") > 0.5 ? 0 : 1));
    $("medallion").addEventListener("click", () => toast("Sanctuary Choir " + (info.version || "") + " · one voice, a whole choir"));
    Params.on("freeze", (v) => $("freezeBtn").classList.toggle("active", v > 0.5));

    Params.on("choirSize", (v) => {
      const engineCount = Math.round(v / 100 * (info.maxSingers || MAX_ENGINE_SINGERS));
      Choir.setCount(Math.min(engineCount, Choir.maxSingers() || engineCount));
      $("singerCount").textContent = engineCount;
      state.lastSizeChange = performance.now();
    });

    Choir.init($("choirCanvas")).then(() => Choir.setCount(Math.min(Math.round(Params.get("choirSize") / 100 * (info.maxSingers || 48)), Choir.maxSingers() || 48), true));
    setTab(info.tab && ["perform", "mix", "fx", "settings"].includes(info.tab) ? info.tab : "perform");
  }

  //==========================================================================
  function onMeters(m) {
    state.meters = m;
    if (!state.built) return;
    const levels = [];
    m.sections.forEach((s, i) => {
      state.sections[i].pushMeter(s.peak || 0, s.notes || []);
      levels.push(Math.min(1, Math.sqrt(s.peak || 0)));
    });
    Choir.setLevels(levels);
    const out = Math.min(1, Math.sqrt(m.out || 0) * 1.1);
    $("medallion").style.setProperty("--glow", out.toFixed(3));
    $("sceneGlow").style.opacity = (out * 0.8).toFixed(3);
    if (m.keyTonic !== state.detectedKey || m.keyMinor !== state.detectedMinor) {
      state.detectedKey = m.keyTonic;
      state.detectedMinor = m.keyMinor;
      if (state.keyDropdown) state.keyDropdown.render();
    }
    const scaleName = (Params.format("scale") || "").toUpperCase();
    $("keyReadout").textContent = "KEY " + KEY_NAMES[m.keyTonic] + " " + scaleName;
    $("chordReadout").textContent = m.chord ? "·  " + m.chord : "";
    const now = performance.now();
    if (m.leadVoiced || out > 0.05) state.lastActivity = now;
    $("harmonyBadge").style.opacity = now - state.lastActivity < 4000 ? "1" : "0";
    $("singerBadge").style.opacity = now - state.lastSizeChange < 2500 ? "1" : "0";
  }

  function onStatus(st) {
    state.status = st;
    if (st.presetName !== undefined) $("presetName").textContent = st.presetName || "Custom";
    const l = st.learn || {};
    const learnBtn = $("learnBtn");
    learnBtn.classList.toggle("recording", l.phase === "recording" && !l.armed);
    learnBtn.classList.toggle("armed", l.phase === "recording" && !!l.armed);
    learnBtn.classList.toggle("busy", l.phase === "analysing");
    const render = $("renderBtn");
    render.classList.toggle("busy", l.phase === "rendering");
    render.classList.toggle("stale", !!l.stale || (l.phase === "learned" && !l.hasStems));
    render.classList.toggle("done", l.phase === "rendered");
    render.querySelector(".render-progress > div").style.width = ((l.progress || 0) * 100) + "%";

    const plaque = $("statusPlaque");
    const busy = ["recording", "analysing", "rendering", "failed"].includes(l.phase);
    if (l.phase !== state.lastPhase) { state.lastPhase = l.phase; state.phaseChanged = performance.now(); }
    const recent = performance.now() - (state.phaseChanged || 0) < 7000;
    plaque.classList.toggle("hidden", !(busy || (recent && l.phase !== "empty")));
    plaque.classList.toggle("recording", l.phase === "recording");
    plaque.classList.toggle("error", l.phase === "failed");
    $("statusText").textContent = l.text || "";
    const prog = $("statusProgress");
    prog.classList.toggle("hidden", l.phase !== "rendering");
    prog.firstElementChild.style.width = ((l.progress || 0) * 100) + "%";
    if (state.settingsInfo && state.settingsInfo.onStatus) state.settingsInfo.onStatus(st);
    clearTimeout(state.plaqueTimer);
    if (!busy) state.plaqueTimer = setTimeout(() => onStatus(state.status), 7100);
  }

  SC.on("init", build);
  SC.on("params", (obj) => Object.keys(obj).forEach((id) => { if (!Params.gestures.has(id)) Params.update(id, obj[id]); }));
  SC.on("meters", onMeters);
  SC.on("status", onStatus);
  SC.on("toast", (t) => toast(t.text));

  // Tell the backend we're ready (fonts first, so the canvases measure correctly).
  const ready = () => SC.send({ type: "ready" });
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(ready); else ready();
})();
