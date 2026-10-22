// Sanctuary Choir - the singers in the choir loft.
// Singer sprites cut from the design mockups appear one by one as Choir Size grows.
// Each room (venue) has its own scene and its own choir: the empty room, its singers
// revealed front row first, centre outwards, and the scenery standing in front of them.
// Without the room art the original loft is used: four arrangements ("tiers"), from an
// intimate group to the full choir.
(function () {
  "use strict";

  const SCENE = { x: 140, y: 80, w: 1170, h: 495 };

  const Choir = {
    canvas: null, ctx: null,
    manifest: null, tiers: [], fg: null,
    tierIndex: -1, count: 0,
    rooms: null, roomKey: "", maxEngine: 40,
    levels: [0, 0, 0, 0],
    bpm: 0, beat: 0, beatAt: 0,
    animating: false, lastFrame: 0, time: 0,

    async init(canvas) {
      this.canvas = canvas;
      this.resize();
      window.addEventListener("sc-rescale", () => { this.resize(); this.draw(); });
      this.load = (file, dir = "assets/choir/") => new Promise((resolve) => {
        const img = new Image();
        img.onload = () => resolve(img);
        img.onerror = () => resolve(null);
        img.src = dir + file;
      });
      const load = this.load;
      await this.initRooms();
      if (this.rooms) {
        this.setCount(this.count, true);
        return;
      }
      try {
        const res = await fetch("assets/choir/manifest.json");
        if (!res.ok) throw new Error(res.status);
        this.manifest = await res.json();
      } catch (e) {
        console.warn("Choir sprites unavailable", e);
        return;
      }
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

    // The rooms: loaded on first use (the current one first), so opening the plug-in is quick.
    async initRooms() {
      try {
        const res = await fetch("assets/rooms/manifest.json");
        if (!res.ok) throw new Error(res.status);
        const m = await res.json();
        this.rooms = {};
        Object.entries(m.rooms || {}).forEach(([key, r]) => {
          this.rooms[key] = {
            key, count: r.count, base: Object.assign({ img: null }, r.base), fg: Object.assign({ img: null }, r.fg),
            alpha: 0, target: 0, loaded: null,
            people: r.people.slice().sort((a, b) => a.order - b.order).map((p) => Object.assign({ img: null, alpha: 0, target: 0, phase: Math.random() * 6.28 }, p)),
          };
        });
      } catch (e) {
        console.warn("Room art unavailable, using the loft", e);
        this.rooms = null;
      }
    },

    loadRoom(room) {
      if (!room.loaded) {
        const dir = "assets/rooms/";
        room.loaded = Promise.all([
          this.load(room.base.file, dir).then((img) => { room.base.img = img; }),
          this.load(room.fg.file, dir).then((img) => { room.fg.img = img; }),
          ...room.people.map((p) => this.load(p.file, dir).then((img) => { p.img = img; })),
        ]).then(() => this.kick());
      }
      return room.loaded;
    },

    /** Show a room's scene and choir (crossfading from the last one). */
    setRoom(key, instant = false) {
      if (!this.rooms || !this.rooms[key]) return;
      this.roomKey = key;
      Object.values(this.rooms).forEach((r) => { r.target = r.key === key ? 1 : 0; });
      const room = this.rooms[key];
      this.loadRoom(room).then(() => {
        // Then the others, quietly, so switching rooms later is instant.
        Object.values(this.rooms).forEach((r) => setTimeout(() => this.loadRoom(r), 400));
      });
      if (instant) Object.values(this.rooms).forEach((r) => { r.alpha = r.target; });
      this.setCount(this.count, instant);
    },

    maxSingers() {
      if (this.rooms) return this.maxEngine;
      return this.tiers.length ? this.tiers[this.tiers.length - 1].count : 0;
    },

    resize() {
      if (!this.canvas) return;
      const scale = (window.devicePixelRatio || 1) * Math.max(0.5, window.SC_STAGE_SCALE || 1);
      this.canvas.width = Math.round(1448 * scale);
      this.canvas.height = Math.round(1086 * scale);
      this.ctx = this.canvas.getContext("2d");
      this.ctx.setTransform(scale, 0, 0, scale, 0, 0);
      this.ctx.imageSmoothingQuality = "high";
    },

    /** Show n singers (0 = empty loft). In a room, n of the engine's singers stand for the
        same share of the room's choir. */
    setCount(n, instant = false) {
      this.count = n;
      if (this.rooms) {
        Object.values(this.rooms).forEach((r) => {
          const shown = n > 0 ? Math.max(1, Math.round(r.count * Math.min(1, n / this.maxEngine))) : 0;
          r.people.forEach((p) => {
            p.target = p.order < shown ? 1 : 0;
            if (instant || r.key !== this.roomKey) p.alpha = p.target;
          });
        });
        this.kick();
        return;
      }
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
      if (this.rooms)
        Object.values(this.rooms).forEach((r) => {
          // A room fades in only once its pictures are there.
          if (r.target > 0 && !r.base.img) { moving = true; return; }
          r.alpha = step(r.alpha, r.target, 2.5);
          r.people.forEach((p) => { p.alpha = step(p.alpha, p.target, 3.0); });
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

    /** One singer, moving with the music. */
    drawPerson(c, p, a, beat) {
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
    },

    drawRooms(c) {
      const beat = this.bpm > 0 ? this.beatNow() : 0;
      // The room fading out first, the current one over it.
      const list = Object.values(this.rooms).filter((r) => r.alpha > 0.001 && r.base.img)
        .sort((a, b) => (a.key === this.roomKey) - (b.key === this.roomKey));
      for (const r of list) {
        c.globalAlpha = r.alpha;
        c.drawImage(r.base.img, r.base.x, r.base.y, r.base.w, r.base.h);
        // From the highest reveal order down: a newly revealed singer stands behind the ones
        // already shown (the art pipeline makes sure occluders come first).
        for (let i = r.people.length - 1; i >= 0; --i) {
          const p = r.people[i];
          const a = p.alpha * r.alpha;
          if (a > 0.001 && p.img) this.drawPerson(c, p, a, beat);
        }
        if (r.fg.img) {
          c.globalAlpha = r.alpha;
          c.drawImage(r.fg.img, r.fg.x, r.fg.y, r.fg.w, r.fg.h);
        }
      }
      c.globalAlpha = 1;
    },

    draw() {
      const c = this.ctx;
      if (!c) return;
      c.clearRect(SCENE.x, SCENE.y, SCENE.w, SCENE.h);
      if (this.rooms) {
        c.clearRect(120, 64, 1210, 528);
        this.drawRooms(c);
        return;
      }
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
          this.drawPerson(c, p, a, beat);
        }
      }
      c.globalAlpha = 1;
      if (any && this.fg && this.fg.img)
        c.drawImage(this.fg.img, this.fg.x, this.fg.y, this.fg.w, this.fg.h);
    },
  };

  window.SCChoir = Choir;
})();
