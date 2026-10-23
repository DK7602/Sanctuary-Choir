// Sanctuary Choir - main UI wiring.
(function () {
  "use strict";
  const { Params, BigKnob, Dropdown, Menu, hiDpiCanvas, stageRect, noteName, dragValue } = window.SCW;
  const { SECTIONS, VENUES, buildMix, buildFx, buildSettings, buildTake, buildStage, buildWords, buildPasses } = window.SCPages;

  // Each venue has its own room and choir; monks sing Gregorian chant in the abbey.
  const ROOMS = ["cathedral", "abbey", "chapel", "hall", "gospel", "studio"];
  const GREGORIAN = 2;
  const VENUE_ICONS = [
    // Grand Cathedral: spires
    '<path d="M3 21V12l3-4 3 4v9M15 21v-9l3-4 3 4v9M9 21V9l3-6 3 6v12M2 21h20M12 1.5v3M10.8 2.7h2.4M11 21v-4h2v4"/>',
    // Stone Abbey: round arches
    '<path d="M3 21v-9a4 4 0 0 1 8 0v9M13 21v-9a4 4 0 0 1 8 0v9M2 21h20M12 2v5M10 3.8h4M5.5 21v-6M18.5 21v-6"/>',
    // Wooden Chapel: a little church
    '<path d="M4 21V11l8-6 8 6v10M2 21h20M12 1.5v5M10 3.2h4M10 21v-5a2 2 0 0 1 4 0v5"/>',
    // Concert Hall: pipes under a shell
    '<path d="M3 9.5Q12 2 21 9.5M6 20v-6M9.5 20v-9M13 20v-7M16.5 20v-8M19 20v-4M3 21h18"/>',
    // Gospel Church: a cross in light
    '<path d="M12 4v17M7.5 9h9M4 4.5l2 2M20 4.5l-2 2M2.5 11h2M19.5 11h2M8 21h8"/>',
    // Studio Room: a microphone
    '<path d="M12 3a3 3 0 0 1 3 3v6a3 3 0 0 1-6 0V6a3 3 0 0 1 3-3zM6 11a6 6 0 0 0 12 0M12 17v4M8 21h8"/>',
  ];
  const svgIcon = (paths) => '<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">' + paths + "</svg>";
  const Choir = window.SCChoir;
  const $ = (id) => document.getElementById(id);

  const KEY_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
  const MAX_ENGINE_SINGERS = 40;

  // Layout of the four section panels (design-space pixels, measured from the mockup).
  const SECTION_LAYOUT = [
    { icon: [253, 888], level: [304, 912, 146], wave: [243, 930, 207, 26], keys: [233, 960, 239, 49], range: [356, 1012] },
    { icon: [509, 888], level: [559, 912, 138], wave: [490, 930, 207, 26], keys: [481, 960, 239, 49], range: [606, 1012] },
    { icon: [758, 888], level: [808, 912, 137], wave: [741, 930, 215, 26], keys: [731, 960, 238, 49], range: [848, 1012] },
    { icon: [1005, 888], level: [1055, 912, 139], wave: [987, 930, 207, 26], keys: [979, 960, 238, 49], range: [1095, 1012] },
  ];

  const state = { info: {}, tab: "perform", meters: null, status: null, detectedKey: 0, detectedMinor: false, scaleType: 0, keyConfidence: 0, sections: [], built: false, lastSizeChange: 0, lastActivity: 0 };
  // The Scale parameter's last choice is Auto (listen for major or minor); it is listed first.
  const scaleAuto = () => { const d = Params.def("scale"); return !!d && Math.round(Params.get("scale")) === d.choices.length - 1; };
  const scaleName = (type) => { const d = Params.def("scale"); return (d && d.choices[type]) || ""; };

  //==========================================================================
  // Stage scaling
  function rescale() {
    const preview = !SC.isPlugin;
    const panel = document.getElementById("previewPanel");
    const gutter = preview ? 16 : 0;
    const panelH = preview && panel ? panel.offsetHeight + 12 : 0;
    const vw = window.innerWidth - 2 * gutter, vh = window.innerHeight - panelH - (preview ? 12 : 0);
    const s = Math.max(0.05, Math.min(vw / 1448, vh / 1086));
    const stage = $("stage");
    stage.style.transform = "scale(" + s + ")";
    stage.style.left = (gutter + Math.max(0, (vw - 1448 * s) / 2)) + "px";
    stage.style.top = (preview ? 8 : Math.max(0, (vh - 1086 * s) / 2)) + "px";
    if (panel) panel.style.top = (8 + 1086 * s + 10) + "px";
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
  function toast(text, ms) {
    const t = $("toast");
    t.textContent = text;
    t.classList.remove("hidden");
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => t.classList.add("hidden"), ms || 3800);
  }

  //==========================================================================
  // Tabs and pages
  // Pages opened from another page (not a tab) keep that page's tab lit.
  const SUB_PAGES = { take: "settings", stage: "mix", words: "settings", passes: "settings" };
  function setTab(tab) {
    if (!SUB_PAGES[tab] && !SUB_PAGES[state.tab]) state.returnTab = state.tab;
    state.tab = tab;
    document.querySelectorAll(".tab").forEach((b) => b.classList.toggle("active", b.dataset.tab === (SUB_PAGES[tab] || tab)));
    const mixTab = document.querySelector('.tab[data-tab="mix"]');
    if (mixTab) mixTab.title = tab === "mix" ? "Click again for the stage" : tab === "stage" ? "Click again for the mix knobs" : "";
    ["mix", "fx", "settings", "take", "stage", "words", "passes"].forEach((p) => $("page-" + p).classList.toggle("hidden", p !== tab));
    if (tab === "take") SC.send({ type: "cmd", name: "lane" });
    SC.send({ type: "cmd", name: "tab", value: tab });
    Menu.close();
  }

  //==========================================================================
  // Presets
  // Presets: categories on the left, the presets of the chosen category on the right.
  function openPresetMenu(nameIt) {
    const st = state.status || {};
    const presets = st.presets || [];
    const catOf = (p) => p.category || (p.factory ? "Factory" : "My Presets");
    const cats = [];
    presets.forEach((p) => { if (!cats.includes(catOf(p))) cats.push(catOf(p)); });
    const current = presets[st.presetIndex];
    let active = current ? catOf(current) : cats[0];
    const div = (cls, parent, text) => { const d = document.createElement("div"); d.className = cls; if (text !== undefined) d.textContent = text; parent.appendChild(d); return d; };

    Menu.open(stageRect($("presetBox")), (m) => {
      m.classList.add("preset-menu");
      const cols = div("pm-cols", m);
      const catCol = div("pm-cats", cols);
      const list = div("pm-list", cols);
      const catItems = {};
      const showList = () => {
        list.innerHTML = "";
        Object.keys(catItems).forEach((c) => catItems[c].classList.toggle("active", c === active));
        presets.forEach((p, i) => {
          if (catOf(p) !== active) return;
          const it = Menu.item(list, p.name, i === st.presetIndex, () => SC.send({ type: "cmd", name: "presetLoad", index: i }));
          if (!p.factory) {
            const del = document.createElement("span");
            del.textContent = "\u2715";
            del.title = "Delete preset";
            del.style.cssText = "margin-left:auto;color:#a8977c;font-size:13px;padding:0 4px";
            del.addEventListener("click", (e) => { e.stopPropagation(); SC.send({ type: "cmd", name: "presetDelete", index: i }); Menu.close(); });
            it.appendChild(del);
          }
        });
      };
      cats.forEach((c) => {
        const it = div("item", catCol, c.toUpperCase());
        const n = presets.filter((p) => catOf(p) === c).length;
        div("count", it, String(n));
        it.addEventListener("mouseenter", () => { active = c; showList(); });
        it.addEventListener("click", () => { active = c; showList(); });
        catItems[c] = it;
      });
      showList();

      Menu.sep(m);
      const input = document.createElement("input");
      input.placeholder = "Name your sound...";
      input.addEventListener("keydown", (e) => {
        e.stopPropagation();
        if (e.key === "Enter" && input.value.trim()) { SC.send({ type: "cmd", name: "presetSave", value: input.value.trim() }); Menu.close(); }
      });
      m.appendChild(input);
      const actions = div("", m);
      actions.style.cssText = "display:flex";
      Menu.item(actions, "SAVE CURRENT SOUND", false, () => { if (input.value.trim()) SC.send({ type: "cmd", name: "presetSave", value: input.value.trim() }); }, "action");
      Menu.item(actions, "OPEN PRESETS FOLDER", false, () => SC.send({ type: "cmd", name: "presetFolder" }), "action");
      if (nameIt) setTimeout(() => input.focus(), 0);
    });
  }

  // The gear beside the preset: the manual, the settings and saving a sound.
  function openGearMenu() {
    Menu.open(stageRect($("gearBtn")), (m) => {
      m.classList.add("gear-menu");
      Menu.item(m, "\u2756  THE MANUAL", false, () => window.SCManual.open(), "action");
      Menu.item(m, state.tab === "settings" ? "BACK TO PERFORM" : "SETTINGS", false, () => setTab(state.tab === "settings" ? "perform" : "settings"), "action");
      Menu.sep(m);
      // Opened once this menu has closed (picking an item closes the menu it is in).
      Menu.item(m, "SAVE THIS SOUND AS A PRESET\u2026", false, () => setTimeout(() => openPresetMenu(true), 0), "action");
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
          return { main: "Auto", extra: "\u00b7 " + KEY_NAMES[state.detectedKey] + (state.detectedMinor ? "m" : "") };
        };
        // One click locks the key Auto has found, so the harmony is right from the first note.
        const canLock = () => Math.round(Params.get("key")) === 0 && state.keyConfidence >= 0.3;
        const lockKey = () => {
          Params.set("key", state.detectedKey + 1);
          if (scaleAuto()) Params.set("scale", state.scaleType);   // and the scale it heard
          toast("Key locked to " + KEY_NAMES[state.detectedKey] + " " + (Params.format("scale") || "") + ". Choose Auto to let Sanctuary listen again.");
        };
        opts.action = () => ({ visible: canLock(), title: "Lock the detected key (" + KEY_NAMES[state.detectedKey] + ")", onClick: lockKey });
        opts.menuTop = (m) => {
          if (canLock()) Menu.item(m, "LOCK TO " + KEY_NAMES[state.detectedKey] + " (DETECTED)", false, lockKey, "action");
        };
      }
      if (el.dataset.param === "scale") {
        opts.label = () => scaleAuto() ? { main: "Auto", extra: "\u00b7 " + (state.scaleType === 0 ? "Major" : scaleName(state.scaleType)) } : Params.format("scale");
        opts.order = (() => { const n = Params.def("scale").choices.length; return [n - 1, ...Array.from({ length: n - 1 }, (_, i) => i)]; })();
      }
      const dd = new Dropdown(el, opts);
      if (el.dataset.param === "key") state.keyDropdown = dd;
      if (el.dataset.param === "scale") state.scaleDropdown = dd;
    });
    state.sections = SECTIONS.map((_, i) => new SectionPanel(i));

    buildMix($("page-mix"), { openStage: () => setTab("stage") });
    const stageInfo = { close: () => setTab(state.returnTab && !SUB_PAGES[state.returnTab] ? state.returnTab : "mix") };
    buildStage($("page-stage"), stageInfo);
    state.stageInfo = stageInfo;
    $("loftHit").addEventListener("click", () => setTab("stage"));
    buildFx($("page-fx"));
    const settingsInfo = { version: info.version, latencyMs: info.latencyMs, liveChoirMs: info.liveChoirMs, standalone: !!info.standalone,
                           openTake: () => setTab("take"), openWords: () => setTab("words"), openPasses: () => setTab("passes") };
    buildSettings($("page-settings"), settingsInfo);
    state.settingsInfo = settingsInfo;
    const wordsInfo = { close: () => setTab("settings") };
    buildWords($("page-words"), wordsInfo);
    state.wordsInfo = wordsInfo;
    const passesInfo = { close: () => setTab("settings"), standalone: !!info.standalone, maxSingers: info.maxSingers };
    buildPasses($("page-passes"), passesInfo);
    state.passesInfo = passesInfo;
    window.SCManual.build($("stage"), { goto: (p) => setTab(p) });
    settingsInfo.openManual = (id) => window.SCManual.open(id);
    const takeInfo = { toast, close: () => setTab(state.returnTab && !SUB_PAGES[state.returnTab] ? state.returnTab : "settings"), visible: () => state.tab === "take" };
    buildTake($("page-take"), takeInfo);
    state.takeInfo = takeInfo;
    // The learned-take plaque opens the chords.
    $("statusPlaque").addEventListener("click", () => {
      const l = (state.status || {}).learn || {};
      if (l.hasTake && ["learned", "rendered"].includes(l.phase)) setTab("take");
    });

    // MIX clicked again cycles between the knobs and the stage.
    document.querySelectorAll(".tab").forEach((b) => b.addEventListener("click", () => {
      const t = b.dataset.tab;
      if (t === "mix" && state.tab === "mix") setTab("stage");
      else if (t === "mix" && state.tab === "stage") setTab("mix");
      else setTab(t);
    }));
    $("gearBtn").addEventListener("click", openGearMenu);
    $("presetBox").addEventListener("click", openPresetMenu);
    $("presetPrev").addEventListener("click", () => SC.send({ type: "cmd", name: "presetPrev" }));
    $("presetNext").addEventListener("click", () => SC.send({ type: "cmd", name: "presetNext" }));
    $("learnBtn").addEventListener("click", () => SC.send({ type: "cmd", name: "learn" }));
    $("loadBtn").addEventListener("click", () => SC.send({ type: "cmd", name: "load" }));
    $("clearBtn").addEventListener("click", () => SC.send({ type: "cmd", name: "clear" }));
    $("renderBtn").addEventListener("click", () => SC.send({ type: "cmd", name: "render" }));
    $("freezeBtn").addEventListener("click", () => Params.set("freeze", Params.get("freeze") > 0.5 ? 0 : 1));
    $("medallion").addEventListener("click", () => toast("Sanctuary Choir " + (info.version || "") + " \u00b7 one voice, a whole choir"));
    Params.on("freeze", (v) => $("freezeBtn").classList.toggle("active", v > 0.5));

    Params.on("choirSize", (v) => {
      const engineCount = Math.round(v / 100 * (info.maxSingers || MAX_ENGINE_SINGERS));
      Choir.setCount(Math.min(engineCount, Choir.maxSingers() || engineCount));
      $("singerCount").textContent = engineCount;
      state.lastSizeChange = performance.now();
    });

    // The room: the picker at the top of the scene, and the scene itself.
    const picker = $("venuePicker");
    const showRoom = (instant) => {
      const v = Math.max(0, Math.min(VENUES.length - 1, Math.round(Params.get("venue"))));
      const style = Math.round(Params.get("style"));
      picker.querySelector(".vp-icon").innerHTML = svgIcon(VENUE_ICONS[v]);
      picker.querySelector(".vp-name").textContent = VENUES[v][0];
      picker.querySelector(".vp-desc").textContent = VENUES[v][1];
      Choir.setRoom(ROOMS[v] === "abbey" && style === GREGORIAN ? "abbey-monks" : ROOMS[v], instant);
    };
    picker.addEventListener("click", () => {
      Menu.open(stageRect(picker), (m) => {
        Menu.heading(m, "THE ROOM");
        VENUES.forEach(([name, desc], i) => {
          const it = Menu.item(m, name, Math.round(Params.get("venue")) === i, () => Params.set("venue", i));
          const d = document.createElement("span");
          d.className = "idesc";
          d.textContent = desc;
          it.appendChild(d);
        });
      });
    });
    Params.on("venue", () => showRoom(false));
    Params.on("style", () => showRoom(false));

    // Energy: the plaque opposite the room's. Drag it like a knob; the chevron picks a level.
    const energy = $("energyPlaque"), energyDial = energy.querySelector(".ep-dial");
    const ENERGY_LEVELS = [["Hushed", 0, "Quiet, airy, slow to speak"], ["Soft", 25, "Gentle and warm"], ["Natural", 50, "As the style sings"],
                           ["Powerful", 75, "Louder, brighter, pressed"], ["Massive", 100, "Everything they have"]];
    const drawEnergy = () => {
      const c = hiDpiCanvas(energyDial, 50, 50);
      const v = Params.norm("energy"), s = 0.75 * Math.PI, e = 2.25 * Math.PI, a = s + (e - s) * v;
      c.clearRect(0, 0, 50, 50);
      c.lineCap = "round";
      c.lineWidth = 4;
      c.strokeStyle = "#0a0706";
      c.beginPath(); c.arc(25, 25, 20, s, e); c.stroke();
      c.save();
      c.shadowColor = "rgba(255, 200, 120, 0.9)";
      c.shadowBlur = 7;
      c.strokeStyle = "#f3d08a";
      c.lineWidth = 3.2;
      c.beginPath(); c.arc(25, 25, 20, s, a); c.stroke();
      c.restore();
      const g = c.createRadialGradient(20, 18, 1.5, 25, 25, 14);
      g.addColorStop(0, "#fff0c4"); g.addColorStop(0.4, "#d8b06a"); g.addColorStop(0.8, "#8c6430"); g.addColorStop(1, "#3d2a12");
      c.fillStyle = g;
      c.beginPath(); c.arc(25, 25, 13.5, 0, Math.PI * 2); c.fill();
      c.strokeStyle = "rgba(255, 248, 232, 0.95)";
      c.lineWidth = 1.8;
      c.beginPath(); c.moveTo(25 + Math.cos(a) * 5, 25 + Math.sin(a) * 5); c.lineTo(25 + Math.cos(a) * 11.5, 25 + Math.sin(a) * 11.5); c.stroke();
      energy.querySelector(".ep-desc").textContent = Params.format("energy") + " \u00b7 " + Math.round(Params.get("energy")) + " %";
    };
    dragValue(energy, "energy", { sensitivity: 180 });
    const chev = energy.querySelector(".ep-chev");
    chev.addEventListener("pointerdown", (e) => e.stopPropagation());
    chev.addEventListener("dblclick", (e) => e.stopPropagation());
    chev.addEventListener("click", (e) => {
      e.stopPropagation();
      Menu.open(stageRect(energy), (m) => {
        Menu.heading(m, "ENERGY");
        ENERGY_LEVELS.forEach(([name, v, desc]) => {
          const it = Menu.item(m, name, Params.format("energy") === name, () => { Params.begin("energy"); Params.set("energy", v); Params.end("energy"); });
          const d = document.createElement("span");
          d.className = "idesc";
          d.textContent = desc;
          it.appendChild(d);
        });
      });
    });
    Params.on("energy", drawEnergy);
    window.addEventListener("sc-rescale", drawEnergy);

    Choir.init($("choirCanvas")).then(() => {
      Choir.setCount(Math.min(Math.round(Params.get("choirSize") / 100 * (info.maxSingers || MAX_ENGINE_SINGERS)), Choir.maxSingers() || MAX_ENGINE_SINGERS), true);
      showRoom(true);
    });
    setTab(info.tab && ["perform", "mix", "fx", "settings", "take", "stage"].includes(info.tab) ? info.tab : "perform");
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
    Choir.setBeat(m.bpm || 0, m.beat);
    const out = Math.min(1, Math.sqrt(m.out || 0) * 1.1);
    $("medallion").style.setProperty("--glow", out.toFixed(3));
    $("sceneGlow").style.opacity = (out * 0.8).toFixed(3);
    const confident = (m.keyConfidence || 0) >= 0.3;
    const scaleType = m.scaleType || 0;
    if (m.keyTonic !== state.detectedKey || m.keyMinor !== state.detectedMinor || scaleType !== state.scaleType || confident !== (state.keyConfidence >= 0.3)) {
      state.detectedKey = m.keyTonic;
      state.detectedMinor = m.keyMinor;
      state.scaleType = scaleType;
      state.keyConfidence = m.keyConfidence || 0;
      if (state.keyDropdown) state.keyDropdown.render();
      if (state.scaleDropdown) state.scaleDropdown.render();
    }
    state.keyConfidence = m.keyConfidence || 0;
    $("keyReadout").textContent = "KEY " + KEY_NAMES[m.keyTonic] + " " + scaleName(scaleType).toUpperCase();
    // Following the band: its chord (with the bass note), marked BAND.
    const band = !!m.followingBand && !!m.bandChord;
    $("chordReadout").textContent = band ? "\u00b7  " + m.bandChord : (m.chord ? "\u00b7  " + m.chord : "");
    $("chordReadout").classList.toggle("band", band);
    if (state.settingsInfo && state.settingsInfo.onMeters) state.settingsInfo.onMeters(m);
    if (state.stageInfo && state.stageInfo.onMeters) state.stageInfo.onMeters(m);
    if (state.wordsInfo && state.wordsInfo.onMeters) state.wordsInfo.onMeters(m);
    const now = performance.now();
    if (m.leadVoiced || out > 0.05 || band) state.lastActivity = now;
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
    plaque.classList.toggle("clickable", !!l.hasTake && ["learned", "rendered"].includes(l.phase));
    plaque.title = plaque.classList.contains("clickable") ? "See the chords, choose others, and drag the stems into your DAW" : "";
    if (state.settingsInfo && state.settingsInfo.onStatus) state.settingsInfo.onStatus(st);
    if (state.wordsInfo && state.wordsInfo.onStatus) state.wordsInfo.onStatus(st);
    if (state.passesInfo && state.passesInfo.onStatus) state.passesInfo.onStatus(st);
    // One-off messages from the take (a pass that could not be used).
    if (typeof l.noticeId === "number") {
      if (state.noticeId !== undefined && l.noticeId > state.noticeId && l.notice) toast(l.notice, 5500);
      state.noticeId = l.noticeId;
    }
    if (state.takeInfo && state.takeInfo.onTakeStatus) state.takeInfo.onTakeStatus(st);
    clearTimeout(state.plaqueTimer);
    if (!busy) state.plaqueTimer = setTimeout(() => onStatus(state.status), 7100);
  }

  //==========================================================================
  // Browser preview only: a panel under the plugin window to hear audio rendered by the engine.
  function buildDemoPlayer() {
    if (SC.isPlugin || $("previewPanel")) return;
    // [file, label, beats per second, Choir Size while it plays]
    const demos = [["demo-input.mp3", "Solo voice", 2], ["demo-ah.mp3", "Recorded choir \u00b7 Ah", 2, 70], ["demo-massive.mp3", "Massive \u00b7 oktavists", 2, 70], ["demo-gospel.mp3", "Gospel choir \u00b7 Words", 2, 45],
                   ["demo-passes.mp3", "Words \u00b7 four passes", 2, 45], ["demo-gospel-plain.mp3", "Words \u00b7 your voice only", 2, 45], ["demo-kyrie.mp3", "Sung words \u00b7 Kyrie", 1.25, 80], ["demo-children.mp3", "Children\u2019s choir", 2, 50]];
    const panel = document.createElement("div");
    panel.id = "previewPanel";
    panel.innerHTML = '<div class="pp-text"><b>Sanctuary Choir</b> interface preview. The knobs, menus and pages all work; the sound engine is simulated here. Turn <b>Choir Size</b> to add singers.</div><div class="pp-demos"><span class="pp-label">Hear the engine</span></div><div class="pp-note">"Amazing Grace" sung by the built-in test voice, then learned and rendered by Sanctuary Choir; "Kyrie eleison" typed as words and played on MIDI chords. "Four passes" is the same choir singing from the take and three more passes of it. "Your voice only" is the gospel choir with Real Voices at 0, every singer your own voice. "Massive" is the recorded choir with Energy at 90 and Sub-Octave at 80. "Children\u2019s choir" is the choir alone, Ensemble on Children. Recorded choir: Sanctuary Voices (VocalSet, CC BY 4.0).</div>';
    const row = panel.querySelector(".pp-demos");
    const audio = new Audio();
    audio.preload = "none";
    const urls = {};
    const urlFor = (file) => {
      if (urls[file]) return urls[file];
      const b64 = window.SC_DEMO_AUDIO && window.SC_DEMO_AUDIO[file];
      if (!b64) return "demo/" + file;
      const bin = atob(b64);
      const bytes = new Uint8Array(bin.length);
      for (let i = 0; i < bin.length; ++i) bytes[i] = bin.charCodeAt(i);
      urls[file] = URL.createObjectURL(new Blob([bytes], { type: "audio/mpeg" }));
      return urls[file];
    };
    let ctx = null, analyser = null, data = null, current = null;
    let beatRate = 2;
    const buttons = demos.map(([file, label, bps, size]) => {
      const b = document.createElement("button");
      b.className = "demo-btn";
      b.textContent = "\u25B6 " + label;
      b.addEventListener("click", () => {
        if (current === file && !audio.paused) { audio.pause(); return; }
        if (!ctx) {
          const AC = window.AudioContext || window.webkitAudioContext;
          if (AC) {
            try {
              ctx = new AC();
              const src = ctx.createMediaElementSource(audio);
              analyser = ctx.createAnalyser();
              analyser.fftSize = 1024;
              data = new Float32Array(analyser.fftSize);
              src.connect(analyser);
              analyser.connect(ctx.destination);
            } catch (e) { ctx = null; analyser = null; }
          }
        }
        if (ctx && ctx.state === "suspended") ctx.resume();
        current = file;
        beatRate = bps;
        audio.src = urlFor(file);
        audio.play().catch(() => toast("Tap again to play"));
        if (size) Params.set("choirSize", size);
      });
      row.appendChild(b);
      return [b, file, label];
    });
    const sync = () => buttons.forEach(([b, f, label]) => {
      const playing = current === f && !audio.paused;
      b.classList.toggle("on", playing);
      b.textContent = (playing ? "\u25A0 " : "\u25B6 ") + label;
    });
    audio.addEventListener("play", sync);
    audio.addEventListener("pause", sync);
    audio.addEventListener("ended", () => { sync(); window.SC_DEMO_LEVEL = undefined; });
    (function level() {
      if (!audio.paused) {
        if (analyser) {
          analyser.getFloatTimeDomainData(data);
          let sum = 0;
          for (let i = 0; i < data.length; ++i) sum += data[i] * data[i];
          window.SC_DEMO_LEVEL = Math.sqrt(sum / data.length);
        } else {
          window.SC_DEMO_LEVEL = 0.15;
        }
        window.SC_DEMO_BEAT = (audio.currentTime - 0.15) * beatRate;
      } else {
        window.SC_DEMO_LEVEL = undefined;
        window.SC_DEMO_BEAT = undefined;
      }
      requestAnimationFrame(level);
    })();
    document.body.appendChild(panel);
    rescale();
  }

  SC.on("init", buildDemoPlayer);
  SC.on("init", build);
  SC.on("params", (obj) => Object.keys(obj).forEach((id) => { if (!Params.gestures.has(id)) Params.update(id, obj[id]); }));
  SC.on("meters", onMeters);
  SC.on("status", onStatus);
  SC.on("toast", (t) => toast(t.text, t.ms));

  // Tell the backend we're ready (fonts first, so the canvases measure correctly).
  const ready = () => SC.send({ type: "ready" });
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(ready); else ready();
})();
