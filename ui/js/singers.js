// Sanctuary Choir - the singers in the choir loft.
// Singer sprites cut from the design mockups appear one by one as Choir Size grows.
// Four arrangements ("tiers") are used, from an intimate group to the full choir;
// within a tier people are revealed front row first, centre outwards.
(function () {
  "use strict";

  const SCENE = { x: 140, y: 80, w: 1170, h: 495 };

  const Choir = {
    canvas: null, ctx: null,
    manifest: null, tiers: [], fg: null,
    tierIndex: -1, count: 0,
    levels: [0, 0, 0, 0],
    bpm: 0, beat: 0, beatAt: 0,
    animating: false, lastFrame: 0, time: 0,

    async init(canvas) {
      this.canvas = canvas;
      this.resize();
      window.addEventListener("sc-rescale", () => { this.resize(); this.draw(); });
      try {
        const res = await fetch("assets/choir/manifest.json");
        if (!res.ok) throw new Error(res.status);
        this.manifest = await res.json();
      } catch (e) {
        console.warn("Choir sprites unavailable", e);
        return;
      }
      const base = "assets/choir/";
      const load = (file) => new Promise((resolve) => {
        const img = new Image();
        img.onload = () => resolve(img);
        img.onerror = () => resolve(null);
        img.src = base + file;
      });
      if (this.manifest.foreground) {
        const f = this.manifest.foreground;
        this.fg = Object.assign({ img: await load(f.file) }, f);
      }
      this.tiers = (this.manifest.tiers || []).slice().sort((a, b) => a.count - b.count).map((t) => ({
        count: t.count,
        name: t.name,
        alpha: 0, target: 0,
        people: t.people.slice().sort((a, b) => a.order - b.order).map((p) => Object.assign({ img: null, alpha: 0, target: 0, phase: Math.random() * 6.28 }, p)),
      }));
      await Promise.all(this.tiers.flatMap((t) => t.people.map(async (p) => { p.img = await load(p.file); })));
      this.tierIndex = -1;
      this.setCount(this.count, true);
    },

    maxSingers() { return this.tiers.length ? this.tiers[this.tiers.length - 1].count : 0; },

    resize() {
      if (!this.canvas) return;
      const scale = (window.devicePixelRatio || 1) * Math.max(0.5, window.SC_STAGE_SCALE || 1);
      this.canvas.width = Math.round(1448 * scale);
      this.canvas.height = Math.round(1086 * scale);
      this.ctx = this.canvas.getContext("2d");
      this.ctx.setTransform(scale, 0, 0, scale, 0, 0);
      this.ctx.imageSmoothingQuality = "high";
    },

    /** Show n singers (0 = empty loft). */
    setCount(n, instant = false) {
      this.count = n;
      if (!this.tiers.length) return;
      let ti = this.tiers.findIndex((t) => t.count >= n);
      if (ti < 0) ti = this.tiers.length - 1;
      this.tiers.forEach((t, i) => {
        t.target = i === ti && n > 0 ? 1 : 0;
        t.people.forEach((p) => { p.target = i === ti && p.order < n ? 1 : 0; });
      });
      this.tierIndex = ti;
      if (instant) this.tiers.forEach((t) => { t.alpha = t.target; t.people.forEach((p) => { p.alpha = p.target; }); });
      this.kick();
    },

    // Song tempo from the host: the singers move on the beat while the song plays.
    setBeat(bpm, beat) {
      if (bpm > 0 && beat !== undefined) {
        this.bpm = bpm;
        this.beat = beat;
        this.beatAt = performance.now();
      } else {
        this.bpm = 0;
      }
    },

    beatNow() {
      return this.beat + (performance.now() - this.beatAt) / 60000 * this.bpm;
    },

    setLevels(levels) {
      const changed = levels.some((l, i) => Math.abs(l - this.levels[i]) > 0.01);
      this.levels = levels;
      if (changed || levels.some((l) => l > 0.01)) this.kick();
    },

    kick() {
      if (this.animating) return;
      this.animating = true;
      this.lastFrame = performance.now();
      requestAnimationFrame((t) => this.frame(t));
    },

    frame(now) {
      const dt = Math.min(0.1, (now - this.lastFrame) / 1000);
      this.lastFrame = now;
      this.time += dt;
      let moving = false;
      const step = (cur, target, speed) => {
        if (cur === target) return cur;
        const next = cur < target ? Math.min(target, cur + dt * speed) : Math.max(target, cur - dt * speed);
        moving = true;
        return next;
      };
      this.tiers.forEach((t) => {
        t.alpha = step(t.alpha, t.target, 2.2);
        t.people.forEach((p) => { p.alpha = step(p.alpha, p.target, 3.0); });
      });
      this.draw();
      const singing = this.levels.some((l) => l > 0.01);
      if (moving || singing) requestAnimationFrame((t) => this.frame(t));
      else this.animating = false;
    },

    sectionOf(p) {
      const front = (p.row || 0) === 0;
      const left = (p.cx !== undefined ? p.cx : p.x + p.w / 2) < 724;
      return front ? (left ? 0 : 1) : (left ? 2 : 3);
    },

    draw() {
      const c = this.ctx;
      if (!c) return;
      c.clearRect(SCENE.x, SCENE.y, SCENE.w, SCENE.h);
      let any = false;
      // Fading-out tiers first, then the current one, each back row first.
      const order = this.tiers.map((t, i) => i).sort((a, b) => (a === this.tierIndex) - (b === this.tierIndex));
      for (const i of order) {
        const t = this.tiers[i];
        if (t.alpha <= 0.001) continue;
        // Paint from the highest reveal order down to 0: a newly revealed singer stands
        // behind the ones already shown (the art pipeline guarantees occluders come first).
        const people = t.people.slice().sort((a, b) => b.order - a.order);
        const beat = this.bpm > 0 ? this.beatNow() : 0;
        for (const p of people) {
          const a = p.alpha * t.alpha;
          if (a <= 0.001 || !p.img) continue;
          any = true;
          const lvl = this.levels[this.sectionOf(p)] || 0;
          const depth = Math.min(1, lvl * 2.5);
          let bob;
          if (this.bpm > 0) {
            // A soft dip on every beat; each singer a touch early or late, like real people.
            const b = beat + (p.phase - 3.14) * 0.012;
            bob = (0.5 - 0.5 * Math.cos(2 * Math.PI * b)) * depth * 1.4 - depth * 0.7;
          } else {
            bob = Math.sin(this.time * 4.2 + p.phase) * depth * 0.9;
          }
          const rise = (1 - p.alpha) * 8;
          c.globalAlpha = a;
          c.drawImage(p.img, p.x, p.y + rise + bob, p.w, p.h);
        }
      }
      c.globalAlpha = 1;
      if (any && this.fg && this.fg.img)
        c.drawImage(this.fg.img, this.fg.x, this.fg.y, this.fg.w, this.fg.h);
    },
  };

  window.SCChoir = Choir;
})();
