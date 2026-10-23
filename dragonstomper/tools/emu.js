// Minimal Atari 2600 + Starpath Supercharger emulator for reverse engineering.
// Every bus access is one CPU cycle (3 TIA colour clocks). Distinct-access
// counting follows Stella's CartAR so the Supercharger's RAM-write trick works.
'use strict';
const fs = require('fs');
const zlib = require('zlib');

// ---------- NTSC palette (approximation of Stella's) ----------
const NTSC = [
  0x000000,0x4a4a4a,0x6f6f6f,0x8e8e8e,0xaaaaaa,0xc0c0c0,0xd6d6d6,0xececec,
  0x484800,0x69690f,0x86861d,0xa2a22a,0xbbbb35,0xd2d240,0xe8e84a,0xfcfc54,
  0x7c2c00,0x904811,0xa26221,0xb47a30,0xc3903d,0xd2a44a,0xdfb755,0xecc860,
  0x901c00,0xa33915,0xb55328,0xc66c3a,0xd5824a,0xe39759,0xf0aa67,0xfcbc74,
  0x940000,0xa71a1a,0xb83232,0xc84848,0xd65c5c,0xe46f6f,0xf08080,0xfc9090,
  0x840064,0x97197a,0xa8308f,0xb846a2,0xc659b3,0xd46cc3,0xe07cd2,0xec8ce0,
  0x500084,0x68199a,0x7d30ad,0x9246c0,0xa459d0,0xb56ce0,0xc57cee,0xd48cfc,
  0x140090,0x331aa3,0x4e32b5,0x6848c6,0x7f5cd5,0x956fe3,0xa980f0,0xbc90fc,
  0x000094,0x181aa7,0x2d32b8,0x4248c8,0x545cd6,0x656fe4,0x7580f0,0x8490fc,
  0x001c88,0x183b9d,0x2d57b0,0x4272c2,0x548ad2,0x65a0e1,0x75b5ef,0x84c8fc,
  0x003064,0x185080,0x2d6d98,0x4288b0,0x54a0c5,0x65b7d9,0x75cceb,0x84e0fc,
  0x004030,0x18624e,0x2d8169,0x429e82,0x54b899,0x65d1ae,0x75e7c2,0x84fcd4,
  0x004400,0x1a661a,0x328432,0x48a048,0x5cba5c,0x6fd26f,0x80e880,0x90fc90,
  0x143c00,0x355f18,0x527e2d,0x6e9c42,0x87b754,0x9ed065,0xb4e775,0xc8fc84,
  0x303800,0x505916,0x6d762b,0x88923e,0xa0ab4f,0xb7c25f,0xccd86e,0xe0ec7c,
  0x482c00,0x694d14,0x866a26,0xa28638,0xbb9f47,0xd2b656,0xe8cc63,0xfce070,
];

class Emu {
  constructor(file) {
    const d = fs.readFileSync(file);
    this.loads = [];
    for (let o = 0; o + 8448 <= d.length; o += 8448) this.loads.push(d.subarray(o, o + 8448));
    this.ram = new Uint8Array(6144);      // three 2K banks
    this.rom = new Uint8Array(2048);      // BIOS stand-in (never really executed)
    this.zp = new Uint8Array(128);
    this.off = [0, 0]; this.romHi = true;
    this.we = false; this.wp = false; this.dhr = 0; this.nda = 0;
    this.distinct = 0; this.last = -1; this.cyc = 0;
    // RIOT
    this.swcha = 0xff; this.swchb = 0x0b; this.timer = 0; this.tint = 1024; this.tsub = 0; this.tflag = 0;
    this.fire = false;
    this.tiaInit();
    this.loadHook = null;
    this.a = 0; this.x = 0; this.y = 0; this.s = 0xff; this.p = 0x24; this.pc = 0;
    this.loadNum(this.loads[0][0x2005]);
  }
  bankCfg(v) {
    this.we = !!(v & 2);
    const t = [[2,3],[0,3],[2,0],[0,2],[2,3],[1,3],[2,1],[1,2]][(v >> 2) & 7];
    this.off = t; this.romHi = t[1] === 3;
  }
  loadNum(n) {
    const L = this.loads.find(l => l[0x2005] === n);
    if (!L) throw new Error('no load ' + n);
    const h = L.subarray(0x2000);
    for (let i = 0; i < h[3]; i++) {
      const b = h[0x10 + i], bank = b & 3, pg = (b >> 2) & 7;
      this.ram.set(L.subarray(i * 256, i * 256 + 256), bank * 2048 + pg * 256);
    }
    this.bankCfg(h[2]); this.zp[0] = h[2];
    this.pc = h[0] | h[1] << 8;
    this.curLoad = n;
    if (this.loadHook) this.loadHook(n);
  }
  // ---------- bus ----------
  tick(a) {
    if (a !== this.last) { this.distinct++; this.last = a; }
    this.cyc++;
    this.riotTick();
    this.tiaClock(3);
  }
  cartAccess(a, isWrite) {
    if (this.wp && this.distinct > this.nda + 5) this.wp = false;
    const lo = a & 0x0fff;
    if ((lo & 0x0f00) === 0 && (!this.we || !this.wp)) {
      this.dhr = a & 0xff; this.nda = this.distinct; this.wp = true;
    } else if (lo === 0x0ff8) {
      this.wp = false; this.bankCfg(this.dhr);
    } else if (this.we && this.wp && this.distinct === this.nda + 5) {
      if (this.onWrite) this.onWrite(a, this.dhr, this.off[(lo & 0x800) ? 1 : 0] * 2048 + (lo & 0x7ff));
      if ((lo & 0x800) === 0) this.ram[this.off[0] * 2048 + (lo & 0x7ff)] = this.dhr;
      else if (!this.romHi) this.ram[this.off[1] * 2048 + (lo & 0x7ff)] = this.dhr;
      this.wp = false;
    }
  }
  cartRead(a) {
    const lo = a & 0xfff, bank = this.off[lo >> 11];
    return bank === 3 ? 0xea : this.ram[bank * 2048 + (lo & 0x7ff)];
  }
  rd(a) {
    a &= 0x1fff;
    this.tick(a);
    if (a & 0x1000) { const v = this.cartRead(a); this.cartAccess(a, false); return v; }
    if (!(a & 0x80)) return this.tiaRead(a);
    if (!(a & 0x200)) return this.zp[a & 0x7f];
    return this.riotRead(a);
  }
  wr(a, v) {
    a &= 0x1fff;
    this.tick(a);
    if (a & 0x1000) { this.cartAccess(a, true); return; }
    if (!(a & 0x80)) return this.tiaWrite(a & 0x3f, v);
    if (!(a & 0x200)) { this.zp[a & 0x7f] = v; return; }
    this.riotWrite(a, v);
  }
  // ---------- RIOT ----------
  riotTick() {
    if (--this.tsub <= 0) {
      this.tsub = this.tflag ? 1 : this.tint;
      this.timer = (this.timer - 1) & 0xff;
      if (this.timer === 0xff) this.tflag = 0x80, this.tsub = 1;
    }
  }
  riotRead(a) {
    if (a & 4) {
      if (a & 1) { const f = this.tflag | (this.tflag ? 0x40 : 0); return f; }
      if (this.tflag && this.timer !== 0xff) {}
      return this.timer;
    }
    switch (a & 3) {
      case 0: return this.swcha;
      case 1: return 0;
      case 2: return this.swchb;
      default: return 0;
    }
  }
  riotWrite(a, v) {
    if ((a & 0x14) === 0x14) {
      this.tint = [1, 8, 64, 1024][a & 3];
      this.timer = v; this.tsub = this.tint; this.tflag = 0;
    }
  }
  // ---------- TIA ----------
  tiaInit() {
    this.clk = 0; this.line = 0; this.frame = 0;
    this.fb = new Uint32Array(160 * 262);
    this.lastFrame = null;
    this.r = new Uint8Array(64);
    this.pos = [0, 0, 0, 0, 0]; // P0 P1 M0 M1 BL
    this.grp = [0, 0]; this.grpOld = [0, 0]; this.enabl = 0; this.enablOld = 0;
    this.hm = [0, 0, 0, 0, 0];
    this.cx = 0; this.hmoveBlank = false; this.vsync = false;
    this.audio = [];
  }
  tiaRead(a) {
    const r = a & 0x0f, c = this.cx;
    const bit = (n) => (c >> n) & 1;
    switch (r) {
      case 0: return bit(0) << 7 | bit(1) << 6;  // M0P1 M0P0
      case 1: return bit(2) << 7 | bit(3) << 6;  // M1P0 M1P1
      case 2: return bit(4) << 7 | bit(5) << 6;  // P0PF P0BL
      case 3: return bit(6) << 7 | bit(7) << 6;  // P1PF P1BL
      case 4: return bit(8) << 7 | bit(9) << 6;  // M0PF M0BL
      case 5: return bit(10) << 7 | bit(11) << 6;// M1PF M1BL
      case 6: return bit(12) << 7;               // BLPF
      case 7: return bit(13) << 7 | bit(14) << 6;// P0P1 M0M1
      case 12: return this.fire ? 0 : 0x80;
      case 13: return 0x80;
      default: return 0x80;
    }
  }
  tiaWrite(a, v) {
    const R = this.r; R[a] = v;
    const vis = this.clk - 68;
    const resPos = (d) => vis < 0 ? (d === 5 ? 3 : 2) : (vis + d) % 160;
    switch (a) {
      case 0x00: // VSYNC
        if ((v & 2) && !this.vsync) this.endFrame();
        this.vsync = !!(v & 2); break;
      case 0x02: // WSYNC
        while (this.clk !== 0) { this.cyc++; this.riotTick(); this.tiaClock(3); }
        break;
      case 0x10: this.pos[0] = resPos(5); break;
      case 0x11: this.pos[1] = resPos(5); break;
      case 0x12: this.pos[2] = resPos(4); break;
      case 0x13: this.pos[3] = resPos(4); break;
      case 0x14: this.pos[4] = resPos(4); break;
      case 0x15: case 0x16: case 0x17: case 0x18: case 0x19: case 0x1a:
        this.audio.push([this.frame, a, v]); break;
      case 0x1b: this.grp[0] = v; this.grpOld[1] = this.grp[1]; break;
      case 0x1c: this.grp[1] = v; this.grpOld[0] = this.grp[0]; this.enablOld = this.enabl; break;
      case 0x1d: case 0x1e: break;
      case 0x1f: this.enabl = v & 2; break;
      case 0x20: case 0x21: case 0x22: case 0x23: case 0x24:
        this.hm[a - 0x20] = ((v >> 4) ^ 8) - 8; break;
      case 0x28: if (v & 2) this.pos[2] = (this.pos[0] + 3) % 160; break; // RESMP0 (approx)
      case 0x29: if (v & 2) this.pos[3] = (this.pos[1] + 3) % 160; break;
      case 0x2a: // HMOVE
        for (let i = 0; i < 5; i++) this.pos[i] = (this.pos[i] - this.hm[i] + 160) % 160;
        if (this.clk < 68) this.hmoveBlank = true;
        break;
      case 0x2b: this.hm.fill(0); break;
      case 0x2c: this.cx = 0; break;
    }
  }
  endFrame() {
    this.lastFrame = { fb: this.fb.slice(), lines: this.line };
    this.fb.fill(0); this.line = 0; this.frame++;
  }
  playerBit(i, x) {
    const R = this.r;
    let d = (x - this.pos[i] + 160) % 160;
    const n = R[4 + i] & 7;
    const g = R[0x25 + i] & 1 ? this.grpOld[i] : this.grp[i];
    if (!g) return false;
    let sz = 1;
    if (n === 5) sz = 2; else if (n === 7) sz = 4;
    let ok = false, px = 0;
    const copies = { 0: [0], 1: [0, 16], 2: [0, 32], 3: [0, 16, 32], 4: [0, 64], 5: [0], 6: [0, 32, 64], 7: [0] }[n];
    for (const c of copies) { const e = d - c; if (e >= 0 && e < 8 * sz) { ok = true; px = (e / sz) | 0; break; } }
    if (!ok) return false;
    const b = R[0x0b + i] & 8 ? px : 7 - px;
    return !!((g >> b) & 1);
  }
  missileBit(i, x) {
    const R = this.r;
    if (!(R[0x1d + i] & 2) || (R[0x28 + i] & 2)) return false;
    const d = (x - this.pos[2 + i] + 160) % 160, n = R[4 + i];
    const w = 1 << ((n >> 4) & 3);
    const copies = { 0: [0], 1: [0, 16], 2: [0, 32], 3: [0, 16, 32], 4: [0, 64], 5: [0], 6: [0, 32, 64], 7: [0] }[n & 7];
    return copies.some(c => d - c >= 0 && d - c < w);
  }
  ballBit(x) {
    const R = this.r;
    const en = R[0x27] & 1 ? this.enablOld : this.enabl;
    if (!en) return false;
    const d = (x - this.pos[4] + 160) % 160;
    return d < (1 << ((R[0x0a] >> 4) & 3));
  }
  pfBit(x) {
    const R = this.r;
    let p = x >> 2; // 0..39
    if (p >= 20) p = R[0x0a] & 1 ? 39 - p : p - 20;
    if (p < 4) return !!((R[0x0d] >> (4 + p)) & 1);
    if (p < 12) return !!((R[0x0e] >> (11 - p)) & 1);
    return !!((R[0x0f] >> (p - 12)) & 1);
  }
  tiaClock(n) {
    for (; n > 0; n--) {
      const c = this.clk;
      if (c >= 68 && this.line < 262) {
        const x = c - 68, R = this.r;
        let col = R[0x09];
        if (!(R[0x01] & 2) && !(this.hmoveBlank && x < 8)) {
          const pf = this.pfBit(x), bl = this.ballBit(x);
          const p0 = this.playerBit(0, x), p1 = this.playerBit(1, x);
          const m0 = this.missileBit(0, x), m1 = this.missileBit(1, x);
          // collisions
          let cx = 0;
          if (m0 && p1) cx |= 1; if (m0 && p0) cx |= 2; if (m1 && p0) cx |= 4; if (m1 && p1) cx |= 8;
          if (p0 && pf) cx |= 16; if (p0 && bl) cx |= 32; if (p1 && pf) cx |= 64; if (p1 && bl) cx |= 128;
          if (m0 && pf) cx |= 256; if (m0 && bl) cx |= 512; if (m1 && pf) cx |= 1024; if (m1 && bl) cx |= 2048;
          if (bl && pf) cx |= 4096; if (p0 && p1) cx |= 8192; if (m0 && m1) cx |= 16384;
          this.cx |= cx;
          const score = R[0x0a] & 2, prio = R[0x0a] & 4;
          const pfc = score ? (x < 80 ? R[0x06] : R[0x07]) : R[0x08];
          if (prio) {
            if (pf || bl) col = pf ? pfc : R[0x08];
            else if (p0 || m0) col = R[0x06];
            else if (p1 || m1) col = R[0x07];
          } else {
            if (p0 || m0) col = R[0x06];
            else if (p1 || m1) col = R[0x07];
            else if (pf || bl) col = pf ? pfc : R[0x08];
          }
        } else col = -1;
        this.fb[this.line * 160 + x] = col < 0 ? 0 : NTSC[col >> 1];
      }
      if (++this.clk === 228) { this.clk = 0; this.line++; this.hmoveBlank = false; }
    }
  }
  // ---------- CPU ----------
  push(v) { this.wr(0x100 | this.s, v); this.s = (this.s - 1) & 0xff; }
  pull() { this.s = (this.s + 1) & 0xff; return this.rd(0x100 | this.s); }
  nz(v) { this.p = (this.p & 0x7d) | (v & 0x80) | (v ? 0 : 2); return v; }
  fetch() { const v = this.rd(this.pc); this.pc = (this.pc + 1) & 0xffff; return v; }
  adc(v) {
    const c = this.p & 1;
    if (this.p & 8) {
      let lo = (this.a & 15) + (v & 15) + c, hi = (this.a >> 4) + (v >> 4);
      if (lo > 9) { lo += 6; hi++; }
      const bin = (this.a + v + c) & 0xff;
      this.p = (this.p & 0x3c) | (bin ? 0 : 2);
      if (hi & 8) this.p |= 0x80;
      if (~(this.a ^ v) & (this.a ^ (hi << 4)) & 0x80) this.p |= 0x40;
      if (hi > 9) { hi += 6; }
      if (hi > 15) this.p |= 1;
      this.a = ((hi << 4) | (lo & 15)) & 0xff;
    } else {
      const r = this.a + v + c;
      this.p = (this.p & 0x3c) | (r > 255 ? 1 : 0) | ((~(this.a ^ v) & (this.a ^ r) & 0x80) ? 0x40 : 0);
      this.a = this.nz(r & 0xff);
    }
  }
  sbc(v) {
    const c = this.p & 1;
    if (this.p & 8) {
      const bin = this.a - v - (1 - c);
      let lo = (this.a & 15) - (v & 15) - (1 - c), hi = (this.a >> 4) - (v >> 4);
      if (lo < 0) { lo -= 6; hi--; }
      if (hi < 0) hi -= 6;
      this.p = (this.p & 0x3c) | (bin >= 0 ? 1 : 0) | ((bin & 0xff) ? 0 : 2) | (bin & 0x80) | (((this.a ^ v) & (this.a ^ bin) & 0x80) ? 0x40 : 0);
      this.a = ((hi << 4) | (lo & 15)) & 0xff;
    } else {
      const r = this.a - v - (1 - c);
      this.p = (this.p & 0x3c) | (r >= 0 ? 1 : 0) | (((this.a ^ v) & (this.a ^ r) & 0x80) ? 0x40 : 0);
      this.a = this.nz(r & 0xff);
    }
  }
  cmp(r, v) { const t = r - v; this.p = (this.p & 0x7c) | (t >= 0 ? 1 : 0) | (t & 0x80) | ((t & 0xff) ? 0 : 2); }
  step() {
    const pc0 = this.pc;
    if (this.romHi && pc0 >= 0xf800) { // BIOS called: perform multiload
      this.loadNum(this.zp[0xfa - 0x80]);
      return;
    }
    this.curPC = pc0;
    if (this.trace) this.trace(pc0);
    const op = this.fetch();
    const M = OPS[op];
    if (!M) throw new Error('bad op ' + op.toString(16) + ' at ' + pc0.toString(16));
    M.call(this);
  }
  runFrame() {
    const f = this.frame;
    let guard = 0;
    while (this.frame === f) { this.step(); if (++guard > 200000) break; }
    return this.lastFrame;
  }
  savePNG(path, frame = this.lastFrame, y0 = 0, h = 262) {
    const W = 320, H = h, raw = Buffer.alloc((W * 3 + 1) * H);
    for (let y = 0; y < H; y++) {
      raw[y * (W * 3 + 1)] = 0;
      for (let x = 0; x < W; x++) {
        const c = frame.fb[(y + y0) * 160 + (x >> 1)] || 0, o = y * (W * 3 + 1) + 1 + x * 3;
        raw[o] = c >> 16; raw[o + 1] = (c >> 8) & 255; raw[o + 2] = c & 255;
      }
    }
    fs.writeFileSync(path, png(W, H, raw));
  }
}

function crc32(buf) {
  let c, crc = 0xffffffff;
  for (let n = 0; n < buf.length; n++) {
    c = (crc ^ buf[n]) & 0xff;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    crc = (crc >>> 8) ^ c;
  }
  return (crc ^ 0xffffffff) >>> 0;
}
function png(w, h, raw) {
  const chunk = (t, d) => {
    const len = Buffer.alloc(4); len.writeUInt32BE(d.length);
    const td = Buffer.concat([Buffer.from(t), d]);
    const crc = Buffer.alloc(4); crc.writeUInt32BE(crc32(td));
    return Buffer.concat([len, td, crc]);
  };
  const ihdr = Buffer.alloc(13); ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4); ihdr[8] = 8; ihdr[9] = 2;
  return Buffer.concat([Buffer.from([137, 80, 78, 71, 13, 10, 26, 10]), chunk('IHDR', ihdr), chunk('IDAT', zlib.deflateSync(raw)), chunk('IEND', Buffer.alloc(0))]);
}

// ---------- opcode table ----------
const OPS = new Array(256);
function addrMode(mode, kind) {
  // returns function(emu) -> effective address, doing the right bus accesses
  return {
    zp: e => e.fetch(),
    zpx: e => { const z = e.fetch(); e.rd(z); return (z + e.x) & 0xff; },
    zpy: e => { const z = e.fetch(); e.rd(z); return (z + e.y) & 0xff; },
    abs: e => { const l = e.fetch(); return l | e.fetch() << 8; },
    abx: e => { const l = e.fetch(), h = e.fetch(), b = (h << 8 | l); const a = (b + e.x) & 0xffff;
      if (kind !== 'r' || (a & 0xff00) !== (b & 0xff00)) e.rd((b & 0xff00) | (a & 0xff)); return a; },
    aby: e => { const l = e.fetch(), h = e.fetch(), b = (h << 8 | l); const a = (b + e.y) & 0xffff;
      if (kind !== 'r' || (a & 0xff00) !== (b & 0xff00)) e.rd((b & 0xff00) | (a & 0xff)); return a; },
    izx: e => { const z = e.fetch(); e.rd(z); const p = (z + e.x) & 0xff; return e.rd(p) | e.rd((p + 1) & 0xff) << 8; },
    izy: e => { const z = e.fetch(); const b = e.rd(z) | e.rd((z + 1) & 0xff) << 8; const a = (b + e.y) & 0xffff;
      if (kind !== 'r' || (a & 0xff00) !== (b & 0xff00)) e.rd((b & 0xff00) | (a & 0xff)); return a; },
  }[mode];
}
const RMODES = { 0x09: 'imm', 0x05: 'zp', 0x15: 'zpx', 0x0d: 'abs', 0x1d: 'abx', 0x19: 'aby', 0x01: 'izx', 0x11: 'izy' };
function defRead(base, fn) {
  for (const [off, mode] of Object.entries(RMODES)) {
    const op = base + +off;
    if (mode === 'imm') OPS[op] = function () { fn(this, this.fetch()); };
    else { const am = addrMode(mode, 'r'); OPS[op] = function () { fn(this, this.rd(am(this))); }; }
  }
}
defRead(0x00, (e, v) => { e.a = e.nz(e.a | v); });
defRead(0x20, (e, v) => { e.a = e.nz(e.a & v); });
defRead(0x40, (e, v) => { e.a = e.nz(e.a ^ v); });
defRead(0x60, (e, v) => e.adc(v));
defRead(0xa0, (e, v) => { e.a = e.nz(v); });
defRead(0xc0, (e, v) => e.cmp(e.a, v));
defRead(0xe0, (e, v) => e.sbc(v));
// STA
for (const [off, mode] of Object.entries(RMODES)) if (mode !== 'imm') {
  const am = addrMode(mode, 'w'); OPS[0x80 + +off] = function () { this.wr(am(this), this.a); };
}
function def(op, mode, kind, fn) {
  if (mode === 'imm') { OPS[op] = function () { fn(this, this.fetch()); }; return; }
  const am = addrMode(mode, kind);
  if (kind === 'r') OPS[op] = function () { fn(this, this.rd(am(this))); };
  else if (kind === 'w') OPS[op] = function () { this.wr(am(this), fn(this)); };
  else OPS[op] = function () { const a = am(this); const v = this.rd(a); this.wr(a, v); this.wr(a, fn(this, v)); };
}
// LDX/LDY/STX/STY/CPX/CPY/BIT
def(0xa2, 'imm', 'r', (e, v) => e.x = e.nz(v)); def(0xa6, 'zp', 'r', (e, v) => e.x = e.nz(v));
def(0xb6, 'zpy', 'r', (e, v) => e.x = e.nz(v)); def(0xae, 'abs', 'r', (e, v) => e.x = e.nz(v)); def(0xbe, 'aby', 'r', (e, v) => e.x = e.nz(v));
def(0xa0, 'imm', 'r', (e, v) => e.y = e.nz(v)); def(0xa4, 'zp', 'r', (e, v) => e.y = e.nz(v));
def(0xb4, 'zpx', 'r', (e, v) => e.y = e.nz(v)); def(0xac, 'abs', 'r', (e, v) => e.y = e.nz(v)); def(0xbc, 'abx', 'r', (e, v) => e.y = e.nz(v));
def(0x86, 'zp', 'w', e => e.x); def(0x96, 'zpy', 'w', e => e.x); def(0x8e, 'abs', 'w', e => e.x);
def(0x84, 'zp', 'w', e => e.y); def(0x94, 'zpx', 'w', e => e.y); def(0x8c, 'abs', 'w', e => e.y);
def(0xe0, 'imm', 'r', (e, v) => e.cmp(e.x, v)); def(0xe4, 'zp', 'r', (e, v) => e.cmp(e.x, v)); def(0xec, 'abs', 'r', (e, v) => e.cmp(e.x, v));
def(0xc0, 'imm', 'r', (e, v) => e.cmp(e.y, v)); def(0xc4, 'zp', 'r', (e, v) => e.cmp(e.y, v)); def(0xcc, 'abs', 'r', (e, v) => e.cmp(e.y, v));
const bit = (e, v) => { e.p = (e.p & 0x3d) | (v & 0xc0) | ((e.a & v) ? 0 : 2); };
def(0x24, 'zp', 'r', bit); def(0x2c, 'abs', 'r', bit);
// shifts / inc / dec
const shifts = {
  0x00: (e, v) => { e.p = (e.p & 0xfe) | (v >> 7); return e.nz((v << 1) & 0xff); },
  0x20: (e, v) => { const c = e.p & 1; e.p = (e.p & 0xfe) | (v >> 7); return e.nz(((v << 1) | c) & 0xff); },
  0x40: (e, v) => { e.p = (e.p & 0xfe) | (v & 1); return e.nz(v >> 1); },
  0x60: (e, v) => { const c = e.p & 1; e.p = (e.p & 0xfe) | (v & 1); return e.nz((v >> 1) | (c << 7)); },
  0xc0: (e, v) => e.nz((v - 1) & 0xff),
  0xe0: (e, v) => e.nz((v + 1) & 0xff),
};
for (const [b, fn] of Object.entries(shifts)) {
  const base = +b;
  def(base + 0x06, 'zp', 'm', fn); def(base + 0x16, 'zpx', 'm', fn); def(base + 0x0e, 'abs', 'm', fn); def(base + 0x1e, 'abx', 'm', fn);
  if (base < 0x80) OPS[base + 0x0a] = function () { this.rd(this.pc); this.a = fn(this, this.a); };
}
// implied
const imp = (op, fn) => { OPS[op] = function () { this.rd(this.pc); fn(this); }; };
imp(0x18, e => e.p &= ~1); imp(0x38, e => e.p |= 1); imp(0x58, e => e.p &= ~4); imp(0x78, e => e.p |= 4);
imp(0xb8, e => e.p &= ~0x40); imp(0xd8, e => e.p &= ~8); imp(0xf8, e => e.p |= 8); imp(0xea, e => {});
imp(0xaa, e => e.x = e.nz(e.a)); imp(0x8a, e => e.a = e.nz(e.x)); imp(0xa8, e => e.y = e.nz(e.a)); imp(0x98, e => e.a = e.nz(e.y));
imp(0xba, e => e.x = e.nz(e.s)); imp(0x9a, e => e.s = e.x);
imp(0xca, e => e.x = e.nz((e.x - 1) & 0xff)); imp(0xe8, e => e.x = e.nz((e.x + 1) & 0xff));
imp(0x88, e => e.y = e.nz((e.y - 1) & 0xff)); imp(0xc8, e => e.y = e.nz((e.y + 1) & 0xff));
OPS[0x48] = function () { this.rd(this.pc); this.push(this.a); };
OPS[0x08] = function () { this.rd(this.pc); this.push(this.p | 0x30); };
OPS[0x68] = function () { this.rd(this.pc); this.rd(0x100 | this.s); this.a = this.nz(this.pull()); };
OPS[0x28] = function () { this.rd(this.pc); this.rd(0x100 | this.s); this.p = (this.pull() & 0xef) | 0x20; };
// branches
[[0x10, e => !(e.p & 0x80)], [0x30, e => e.p & 0x80], [0x50, e => !(e.p & 0x40)], [0x70, e => e.p & 0x40],
 [0x90, e => !(e.p & 1)], [0xb0, e => e.p & 1], [0xd0, e => !(e.p & 2)], [0xf0, e => e.p & 2]].forEach(([op, c]) => {
  OPS[op] = function () {
    const o = this.fetch();
    if (c(this)) {
      this.rd(this.pc);
      const t = (this.pc + (o > 127 ? o - 256 : o)) & 0xffff;
      if ((t & 0xff00) !== (this.pc & 0xff00)) this.rd((this.pc & 0xff00) | (t & 0xff));
      this.pc = t;
    }
  };
});
OPS[0x4c] = function () { const l = this.fetch(); this.pc = l | this.fetch() << 8; };
OPS[0x6c] = function () { const l = this.fetch(), h = this.fetch(); const a = h << 8 | l;
  this.pc = this.rd(a) | this.rd((a & 0xff00) | ((a + 1) & 0xff)) << 8; };
OPS[0x20] = function () { const l = this.fetch(); this.rd(0x100 | this.s); this.push(this.pc >> 8); this.push(this.pc & 0xff); this.pc = l | this.rd(this.pc) << 8; };
OPS[0x60] = function () { this.rd(this.pc); this.rd(0x100 | this.s); const l = this.pull(); this.pc = (l | this.pull() << 8); this.rd(this.pc); this.pc = (this.pc + 1) & 0xffff; };
OPS[0x40] = function () { this.rd(this.pc); this.rd(0x100 | this.s); this.p = (this.pull() & 0xef) | 0x20; const l = this.pull(); this.pc = l | this.pull() << 8; };
OPS[0x00] = function () { this.fetch(); this.push(this.pc >> 8); this.push(this.pc & 0xff); this.push(this.p | 0x30); this.p |= 4;
  this.pc = this.rd(0xfffe) | this.rd(0xffff) << 8; };
// common illegal NOPs (skip operand)
for (const op of [0x04, 0x44, 0x64]) OPS[op] = function () { this.rd(this.fetch()); };
for (const op of [0x0c]) OPS[op] = function () { const l = this.fetch(); this.rd(l | this.fetch() << 8); };

module.exports = { Emu, NTSC, png };
