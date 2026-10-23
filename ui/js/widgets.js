// Sanctuary Choir - parameter model and widgets.
(function () {
  "use strict";

  const NOTE_NAMES = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"];
  const noteName = (m) => NOTE_NAMES[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1);

  //==========================================================================
  // Parameters
  const Params = {
    defs: {},
    values: {},
    listeners: {},
    gestures: new Set(),

    define(list) {
      list.forEach((d) => { this.defs[d.id] = d; this.values[d.id] = d.value; });
    },
    get(id) { return this.values[id]; },
    def(id) { return this.defs[id]; },
    on(id, fn) { (this.listeners[id] = this.listeners[id] || []).push(fn); if (id in this.values) fn(this.values[id]); },
    update(id, v) {
      this.values[id] = v;
      (this.listeners[id] || []).forEach((fn) => fn(v));
      (this.listeners["*"] || []).forEach((fn) => fn(id, v));
    },
    set(id, v) {
      const d = this.defs[id];
      if (!d) return;
      v = Math.min(d.max, Math.max(d.min, v));
      if (d.interval > 0 && d.type !== "float") v = Math.round((v - d.min) / d.interval) * d.interval + d.min;
      if (v === this.values[id]) return;
      this.update(id, v);
      SC.send({ type: "set", id, value: v });
    },
    begin(id) { if (!this.gestures.has(id)) { this.gestures.add(id); SC.send({ type: "gesture", id, begin: true }); } },
    end(id) { if (this.gestures.delete(id)) SC.send({ type: "gesture", id, begin: false }); },
    reset(id) { const d = this.defs[id]; if (d) this.set(id, d.default); },

    // JUCE NormalisableRange conventions (skew applied to the proportion).
    toNorm(id, v) {
      const d = this.defs[id];
      if (!d || d.max === d.min) return 0;
      const p = Math.min(1, Math.max(0, (v - d.min) / (d.max - d.min)));
      return d.skew && d.skew !== 1 ? Math.pow(p, d.skew) : p;
    },
    fromNorm(id, n) {
      const d = this.defs[id];
      n = Math.min(1, Math.max(0, n));
      const p = d.skew && d.skew !== 1 ? Math.pow(n, 1 / d.skew) : n;
      return d.min + (d.max - d.min) * p;
    },
    setNorm(id, n) { this.set(id, this.fromNorm(id, n)); },
    norm(id) { return this.toNorm(id, this.values[id]); },

    format(id, v = this.values[id]) {
      const d = this.defs[id];
      if (!d) return "";
      if (d.type === "choice") return d.choices[Math.round(v)] || "";
      if (d.type === "bool") return v > 0.5 ? "On" : "Off";
      if (d.type === "int") return noteName(Math.round(v));
      switch (id) {
        case "volume": case "tone": case "leadLevel": case "choirLevel": case "micClose": case "micTree": case "micRoom":
          return v <= -59.9 ? "-inf dB" : (Math.abs(v) < 0.05 ? "0.0" : v.toFixed(1)) + " dB";
        case "preDelay": return Math.round(v) + " ms";
        case "lowCut": case "highCut": return v >= 1000 ? (v / 1000).toFixed(1) + " kHz" : Math.round(v) + " Hz";
        case "tuning": return v.toFixed(1) + " Hz";
        case "formant": return Math.abs(v) < 0.5 ? "Natural" : (v > 0 ? "+" : "") + Math.round(v);
        case "energy": return v < 15 ? "Hushed" : v < 38 ? "Soft" : v < 63 ? "Natural" : v < 85 ? "Powerful" : "Massive";
        case "subOctave": return v < 0.5 ? "Off" : Math.round(v) + " %";
        case "rhythmDepth": return Math.round(v) + " %";
        default: break;
      }
      if (id.endsWith("Depth")) return v < 33 ? "Front" : v < 67 ? "Middle" : "Back";
      if (id.endsWith("Pan")) return Math.abs(v) < 0.01 ? "C" : (v < 0 ? "L" : "R") + Math.round(Math.abs(v) * 100);
      if (id.endsWith("Level")) { const g = Math.pow(v / 0.75, 2); return g < 0.0001 ? "-inf" : (20 * Math.log10(g)).toFixed(1) + " dB"; }
      return Math.round(v) + " %";
    },
  };

  //==========================================================================
  // Drag helper: vertical (or horizontal) drags change a normalised value.
  function dragValue(el, id, opts = {}) {
    const sensitivity = opts.sensitivity || 220;
    let start = null;
    el.addEventListener("pointerdown", (e) => {
      if (e.button !== 0) return;
      e.preventDefault();
      el.setPointerCapture(e.pointerId);
      start = { x: e.clientX, y: e.clientY, n: Params.norm(id) };
      Params.begin(id);
    });
    el.addEventListener("pointermove", (e) => {
      if (!start) return;
      const scale = window.SC_STAGE_SCALE || 1;
      const dy = (start.y - e.clientY) / scale, dx = (e.clientX - start.x) / scale;
      const delta = (opts.horizontal ? dx : dy + dx * 0.25) / (e.shiftKey ? sensitivity * 5 : sensitivity);
      Params.setNorm(id, start.n + delta);
    });
    const end = (e) => { if (start) { start = null; Params.end(id); try { el.releasePointerCapture(e.pointerId); } catch (_) {} } };
    el.addEventListener("pointerup", end);
    el.addEventListener("pointercancel", end);
    el.addEventListener("dblclick", () => { Params.begin(id); Params.reset(id); Params.end(id); });
    el.addEventListener("wheel", (e) => {
      e.preventDefault();
      const d = Params.def(id);
      const step = d && d.type !== "float" ? (1 / Math.max(1, d.max - d.min)) : 0.015;
      Params.begin(id);
      Params.setNorm(id, Params.norm(id) + (e.deltaY < 0 ? step : -step) * (e.shiftKey ? 0.2 : 1));
      clearTimeout(el._wheelEnd);
      el._wheelEnd = setTimeout(() => Params.end(id), 300);
    }, { passive: false });
  }

  function hiDpiCanvas(canvas, w, h) {
    const scale = (window.devicePixelRatio || 1) * Math.max(1, window.SC_STAGE_SCALE || 1);
    canvas.width = Math.round(w * scale);
    canvas.height = Math.round(h * scale);
    const ctx = canvas.getContext("2d");
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
    return ctx;
  }

  //==========================================================================
  // Big stained-glass knob: the ring art is part of the skin; we dim the unlit glass,
  // light the lit glass, and draw a clean cap with a live pointer.
  const ARC_START = 140, ARC_SPAN = 260;
  const R_IN = 41.5, R_OUT = 62;
  const rad = (deg) => (deg * Math.PI) / 180;

  class BigKnob {
    constructor(el) {
      this.el = el;
      this.id = el.dataset.param;
      this.bipolar = el.classList.contains("bipolar");
      el.innerHTML = '<canvas class="dim"></canvas><canvas class="glow"></canvas><img class="cap" src="assets/skin/cap.png" alt="" draggable="false"><canvas class="ptr"></canvas><div class="value"></div>';
      this.dim = el.querySelector(".dim");
      this.glow = el.querySelector(".glow");
      this.ptr = el.querySelector(".ptr");
      this.valueEl = el.querySelector(".value");
      el.title = (Params.def(this.id) || {}).name || "";
      dragValue(el, this.id);
      Params.on(this.id, () => this.draw());
      window.addEventListener("sc-rescale", () => this.draw());
    }

    sector(ctx, a0, a1, r0, r1) {
      ctx.beginPath();
      ctx.arc(70, 70, r1, rad(a0), rad(a1));
      ctx.arc(70, 70, r0, rad(a1), rad(a0), true);
      ctx.closePath();
    }

    draw() {
      const v = Params.norm(this.id);
      const a = ARC_START + ARC_SPAN * v;
      const end = ARC_START + ARC_SPAN;
      const mid = ARC_START + ARC_SPAN / 2;
      let lit0 = ARC_START, lit1 = a;
      if (this.bipolar) { lit0 = Math.min(mid, a); lit1 = Math.max(mid, a); }

      // Dim the unlit glass with a soft edge.
      const d = hiDpiCanvas(this.dim, 140, 140);
      d.clearRect(0, 0, 140, 140);
      const step = 1.5;
      for (let t = ARC_START - 4; t < end + 4; t += step) {
        const c = t + step / 2;
        let dist = c < lit0 ? lit0 - c : c > lit1 ? c - lit1 : 0;
        if (dist <= 0) continue;
        const alpha = 0.62 * Math.min(1, dist / 7);
        d.fillStyle = "rgba(7,4,3," + alpha.toFixed(3) + ")";
        this.sector(d, t, t + step + 0.4, R_IN, R_OUT);
        d.fill();
      }

      // Warm light through the lit glass (screen blended).
      const g = hiDpiCanvas(this.glow, 140, 140);
      g.clearRect(0, 0, 140, 140);
      if (lit1 - lit0 > 0.5) {
        const grad = g.createRadialGradient(70, 70, R_IN - 2, 70, 70, R_OUT + 6);
        grad.addColorStop(0, "rgba(255,190,110,0.10)");
        grad.addColorStop(0.5, "rgba(255,205,140,0.30)");
        grad.addColorStop(1, "rgba(255,170,90,0.0)");
        g.fillStyle = grad;
        this.sector(g, lit0, lit1, R_IN - 2, R_OUT + 6);
        g.fill();
      }

      // Pointer: gold needle on the collar + a bright line on the cap.
      const p = hiDpiCanvas(this.ptr, 140, 140);
      p.clearRect(0, 0, 140, 140);
      const ang = rad(a);
      const cos = Math.cos(ang), sin = Math.sin(ang);
      const px = (r, o = 0) => 70 + cos * r - sin * o;
      const py = (r, o = 0) => 70 + sin * r + cos * o;
      p.save();
      p.shadowColor = "rgba(0,0,0,0.8)";
      p.shadowBlur = 3;
      p.beginPath();
      p.moveTo(px(49), py(49));
      p.lineTo(px(35, 3.2), py(35, 3.2));
      p.lineTo(px(35, -3.2), py(35, -3.2));
      p.closePath();
      const ng = p.createLinearGradient(px(35), py(35), px(49), py(49));
      ng.addColorStop(0, "#8a6328");
      ng.addColorStop(0.5, "#ffe6a8");
      ng.addColorStop(1, "#c99a50");
      p.fillStyle = ng;
      p.fill();
      p.restore();

      p.save();
      p.lineCap = "round";
      p.shadowColor = "rgba(255,235,200,0.9)";
      p.shadowBlur = 4;
      p.strokeStyle = "rgba(255,248,232,0.95)";
      p.lineWidth = 2.4;
      p.beginPath();
      p.moveTo(px(11), py(11));
      p.lineTo(px(26), py(26));
      p.stroke();
      p.restore();

      this.valueEl.textContent = Params.format(this.id);
    }
  }

  //==========================================================================
  // Small knob for the MIX / FX / SETTINGS pages (fully drawn).
  class MiniKnob {
    constructor(parent, id, label, opts = {}) {
      this.id = id;
      this.opts = opts;
      const el = document.createElement("div");
      el.className = "mini-knob";
      el.innerHTML = '<canvas></canvas><div class="label"></div><div class="val"></div>';
      el.querySelector(".label").textContent = label;
      el.title = (Params.def(id) || {}).name || label;
      parent.appendChild(el);
      this.el = el;
      this.canvas = el.querySelector("canvas");
      this.val = el.querySelector(".val");
      dragValue(el, id, { sensitivity: 180 });
      Params.on(id, () => this.draw());
      window.addEventListener("sc-rescale", () => this.draw());
    }

    draw() {
      const c = hiDpiCanvas(this.canvas, 72, 72);
      const v = Params.norm(this.id);
      const cx = 36, cy = 36;
      c.clearRect(0, 0, 72, 72);
      const s = rad(135), e = rad(405);
      const a = s + (e - s) * v;
      c.lineCap = "round";
      c.lineWidth = 5;
      c.strokeStyle = "#0a0706";
      c.beginPath(); c.arc(cx, cy, 30, s, e); c.stroke();
      c.strokeStyle = "rgba(232,201,138,0.18)";
      c.lineWidth = 1;
      c.beginPath(); c.arc(cx, cy, 33.5, s, e); c.stroke();
      const bip = this.opts.bipolar;
      const from = bip ? rad(270) : s;
      c.save();
      c.shadowColor = this.opts.color || "rgba(255,200,120,0.9)";
      c.shadowBlur = 8;
      c.strokeStyle = this.opts.color || "#f3d08a";
      c.lineWidth = 4;
      c.beginPath();
      if (a >= from) c.arc(cx, cy, 30, from, a); else c.arc(cx, cy, 30, a, from);
      c.stroke();
      c.restore();
      // Cap
      const grad = c.createRadialGradient(cx - 7, cy - 9, 2, cx, cy, 23);
      grad.addColorStop(0, "#fff0c4");
      grad.addColorStop(0.35, "#d8b06a");
      grad.addColorStop(0.75, "#8c6430");
      grad.addColorStop(1, "#3d2a12");
      c.fillStyle = grad;
      c.beginPath(); c.arc(cx, cy, 22, 0, Math.PI * 2); c.fill();
      c.strokeStyle = "rgba(0,0,0,0.6)"; c.lineWidth = 1.5; c.stroke();
      c.strokeStyle = "rgba(255,248,232,0.95)";
      c.lineWidth = 2.2;
      c.beginPath();
      c.moveTo(cx + Math.cos(a) * 8, cy + Math.sin(a) * 8);
      c.lineTo(cx + Math.cos(a) * 19, cy + Math.sin(a) * 19);
      c.stroke();
      this.val.textContent = Params.format(this.id);
    }
  }

  //==========================================================================
  // Vertical fader for section levels.
  class Fader {
    constructor(parent, id, color, short = false) {
      this.id = id;
      this.height = short ? 112 : 150;
      const el = document.createElement("div");
      el.className = "fader" + (short ? " short" : "");
      if (color) el.style.setProperty("--c", color);
      el.innerHTML = '<div class="rail"></div><div class="lit"></div><div class="thumb"></div>';
      el.title = (Params.def(id) || {}).name || "";
      parent.appendChild(el);
      this.el = el;
      this.lit = el.querySelector(".lit");
      this.thumb = el.querySelector(".thumb");
      dragValue(el, id, { sensitivity: 140 });
      Params.on(id, () => this.draw());
    }
    draw() {
      const v = Params.norm(this.id);
      const h = this.height - 12;
      this.thumb.style.top = (6 + (1 - v) * h) + "px";
      this.lit.style.height = (v * h) + "px";
    }
  }

  //==========================================================================
  // Toggle pill bound to a bool parameter.
  function pill(parent, id, label, cls = "") {
    const b = document.createElement("button");
    b.className = "pill " + cls;
    b.textContent = label;
    b.title = (Params.def(id) || {}).name || label;
    b.addEventListener("click", () => Params.set(id, Params.get(id) > 0.5 ? 0 : 1));
    Params.on(id, (v) => b.classList.toggle("on", v > 0.5));
    parent.appendChild(b);
    return b;
  }

  function toggleSwitch(parent, id, label) {
    const el = document.createElement("label");
    el.className = "switch";
    el.innerHTML = '<span class="track"></span><span></span>';
    el.lastChild.textContent = label;
    el.addEventListener("click", (e) => { e.preventDefault(); Params.set(id, Params.get(id) > 0.5 ? 0 : 1); });
    Params.on(id, (v) => el.classList.toggle("on", v > 0.5));
    parent.appendChild(el);
    return el;
  }

  //==========================================================================
  // Menus (dropdown lists).
  const Menu = {
    current: null,
    open(anchorRect, build) {
      this.close();
      const layer = document.getElementById("menuLayer");
      const m = document.createElement("div");
      m.className = "menu";
      build(m);
      layer.appendChild(m);
      // Position below the anchor, inside the stage.
      let x = anchorRect.left, y = anchorRect.top + anchorRect.height + 6;
      const w = Math.max(anchorRect.width, m.offsetWidth);
      m.style.minWidth = anchorRect.width + "px";
      if (x + w > 1430) x = 1430 - w;
      if (y + m.offsetHeight > 1076) y = Math.max(10, anchorRect.top - m.offsetHeight - 6);
      m.style.left = x + "px";
      m.style.top = y + "px";
      this.current = m;
      setTimeout(() => {
        if (this.current !== m) return;   // already closed or replaced
        document.addEventListener("pointerdown", this._outside = (e) => { if (!m.contains(e.target)) this.close(); });
      }, 0);
      document.addEventListener("keydown", this._esc = (e) => { if (e.key === "Escape") this.close(); });
      const sel = m.querySelector(".selected");
      if (sel) sel.scrollIntoView({ block: "nearest" });
      return m;
    },
    close() {
      if (this.current) this.current.remove();
      this.current = null;
      if (this._outside) document.removeEventListener("pointerdown", this._outside);
      if (this._esc) document.removeEventListener("keydown", this._esc);
      this._outside = this._esc = null;
    },
    item(m, label, selected, onClick, cls = "") {
      const it = document.createElement("div");
      it.className = "item " + (selected ? "selected " : "") + cls;
      it.textContent = label;
      it.addEventListener("click", () => { onClick(); Menu.close(); });
      m.appendChild(it);
      return it;
    },
    heading(m, text) { const h = document.createElement("div"); h.className = "heading"; h.textContent = text; m.appendChild(h); },
    sep(m) { const s = document.createElement("div"); s.className = "sep"; m.appendChild(s); },
  };

  // Stage-space rectangle of an element.
  function stageRect(el) {
    let x = 0, y = 0;
    let n = el;
    while (n && n.id !== "stage") { x += n.offsetLeft; y += n.offsetTop; n = n.offsetParent; }
    return { left: x, top: y, width: el.offsetWidth, height: el.offsetHeight };
  }

  class Dropdown {
    constructor(el, opts = {}) {
      this.el = el;
      this.id = el.dataset.param;
      this.opts = opts;
      el.innerHTML = '<div class="text"></div>';
      this.text = el.querySelector(".text");
      el.title = (Params.def(this.id) || {}).name || "";
      el.addEventListener("click", () => this.open());
      el.addEventListener("wheel", (e) => {
        e.preventDefault();
        const d = Params.def(this.id);
        Params.set(this.id, Math.min(d.max, Math.max(d.min, Math.round(Params.get(this.id)) + (e.deltaY > 0 ? 1 : -1))));
      }, { passive: false });
      Params.on(this.id, () => this.render());
    }
    render() {
      const label = this.opts.label ? this.opts.label() : Params.format(this.id);
      this.text.innerHTML = "";
      this.text.append(label.main !== undefined ? label.main : label);
      if (label.extra) { const s = document.createElement("small"); s.textContent = label.extra; this.text.append(s); }
      if (this.opts.action) {
        // Optional one-click button inside the field (e.g. lock the detected key).
        const a = this.opts.action();
        if (!this.actionBtn) {
          this.actionBtn = document.createElement("button");
          this.actionBtn.className = "dd-action";
          this.actionBtn.innerHTML = '<svg viewBox="0 0 16 16" width="16" height="16"><path fill="currentColor" d="M4.5 7V5a3.5 3.5 0 0 1 7 0v2h.5a1 1 0 0 1 1 1v6a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V8a1 1 0 0 1 1-1zm1.6 0h3.8V5a1.9 1.9 0 0 0-3.8 0z"/></svg>';
          this.actionBtn.addEventListener("click", (e) => { e.stopPropagation(); const now = this.opts.action(); if (now.visible) now.onClick(); });
          this.el.appendChild(this.actionBtn);
        }
        this.actionBtn.style.display = a.visible ? "" : "none";
        this.actionBtn.title = a.title || "";
        this.el.classList.toggle("has-action", !!a.visible);
      }
    }
    open() {
      const d = Params.def(this.id);
      Menu.open(stageRect(this.el), (m) => {
        if (this.opts.menuTop) this.opts.menuTop(m);
        // opts.order lists the choices in another order than their values (e.g. Auto first).
        (this.opts.order || d.choices.map((_, i) => i))
          .forEach((i) => Menu.item(m, d.choices[i], Math.round(Params.get(this.id)) === i, () => Params.set(this.id, i)));
      });
    }
  }

  window.SCW = { Params, BigKnob, MiniKnob, Fader, Dropdown, Menu, pill, toggleSwitch, hiDpiCanvas, dragValue, stageRect, noteName };
})();
