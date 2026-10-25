// Z80 CPU and 8253 timer: C++ port of the F.A.R.P. page's JS (which follows Zilog's UM0080).
#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

namespace farp {

struct Z80Bus {
    virtual ~Z80Bus() = default;
    virtual int memRead(int a) = 0;
    virtual void memWrite(int a, int v) = 0;
    virtual int ioRead(int p) = 0;
    virtual void ioWrite(int p, int v) = 0;
};

class Z80 {
public:
    Z80Bus* bus = nullptr;
    int A = 0xFF, F = 0xFF, B = 0, C = 0, D = 0, E = 0, H = 0, L = 0;
    int A2 = 0, F2 = 0, B2 = 0, C2 = 0, D2 = 0, E2 = 0, H2 = 0, L2 = 0;
    int IX = 0xFFFF, IY = 0xFFFF, SP = 0xFFFF, PC = 0, I = 0, R = 0, IM = 0;
    bool IFF1 = false, IFF2 = false, halted = false, irqLine = false, nmiPending = false, eiPending = false;
    double cycles = 0;
    uint8_t PT[256], SZ[256];

    Z80() {
        for (int v = 0; v < 256; v++) { int x = v; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1; PT[v] = (x & 1) ? 0 : 4; SZ[v] = (uint8_t)((v & 0xA8) | (v == 0 ? 0x40 : 0)); }
    }
    void reset() { PC = 0; I = R = 0; IM = 0; IFF1 = IFF2 = false; halted = false; A = F = 0xFF; SP = 0xFFFF; eiPending = false; }

    int read(int a) { return bus->memRead(a); }
    void write(int a, int v) { bus->memWrite(a, v); }
    int in(int p) { return bus->ioRead(p & 0xFF); }
    void out(int p, int v) { bus->ioWrite(p & 0xFF, v); }

    int step() {
        int t = 0;
        if (nmiPending) { nmiPending = false; halted = false; IFF2 = IFF1; IFF1 = false; push(PC); PC = 0x66; t = 11; incR(); cycles += t; return t; }
        if (irqLine && IFF1 && !eiPending) {
            halted = false; IFF1 = IFF2 = false; incR(); const int v = 0xFF;
            if (IM == 2) { push(PC); PC = rd16(((I << 8) | (v & 0xFE)) & 0xFFFF); t = 19; }
            else if (IM == 1) { push(PC); PC = 0x38; t = 13; }
            else { if ((v & 0xC7) == 0xC7) { push(PC); PC = v & 0x38; } t = 13; }
            cycles += t; return t;
        }
        eiPending = false;
        if (halted) { incR(); cycles += 4; return 4; }
        t = exec(fetch(), 0); cycles += t; return t;
    }
    void incR() { R = (R & 0x80) | ((R + 1) & 0x7F); }
    int fetch() { incR(); int v = read(PC); PC = (PC + 1) & 0xFFFF; return v; }
    int imm() { int v = read(PC); PC = (PC + 1) & 0xFFFF; return v; }
    int imm16() { int lo = imm(); return lo | (imm() << 8); }
    int simm() { int d = imm(); return d < 128 ? d : d - 256; }
    int rd16(int a) { return read(a) | (read((a + 1) & 0xFFFF) << 8); }
    void wr16(int a, int v) { write(a, v & 0xFF); write((a + 1) & 0xFFFF, (v >> 8) & 0xFF); }
    void push(int v) { SP = (SP - 1) & 0xFFFF; write(SP, (v >> 8) & 0xFF); SP = (SP - 1) & 0xFFFF; write(SP, v & 0xFF); }
    int pop() { int lo = read(SP); SP = (SP + 1) & 0xFFFF; int hi = read(SP); SP = (SP + 1) & 0xFFFF; return lo | (hi << 8); }
    int BC() const { return (B << 8) | C; } void setBC(int v) { B = (v >> 8) & 0xFF; C = v & 0xFF; }
    int DE() const { return (D << 8) | E; } void setDE(int v) { D = (v >> 8) & 0xFF; E = v & 0xFF; }
    int HL() const { return (H << 8) | L; } void setHL(int v) { H = (v >> 8) & 0xFF; L = v & 0xFF; }
    int AF() const { return (A << 8) | F; } void setAF(int v) { A = (v >> 8) & 0xFF; F = v & 0xFF; }
    int sz53p(int v) { return SZ[v & 0xFF] | PT[v & 0xFF]; }
    void add8(int v, bool carry) { int c = carry && (F & 1) ? 1 : 0, a = A, r = a + v + c; F = SZ[r & 0xFF] | ((r >> 8) & 1) | ((a ^ v ^ r) & 0x10) | ((((a ^ ~v) & (a ^ r)) & 0x80) ? 4 : 0); A = r & 0xFF; }
    void sub8(int v, bool carry, bool store) {
        int c = carry && (F & 1) ? 1 : 0, a = A, r = a - v - c, res = r & 0xFF;
        int f = (res & 0x80) | (res == 0 ? 0x40 : 0) | 2 | ((r >> 8) & 1) | ((a ^ v ^ r) & 0x10) | ((((a ^ v) & (a ^ r)) & 0x80) ? 4 : 0);
        if (store) { A = res; f = (f & ~0x28) | (res & 0x28); } else f = (f & ~0x28) | (v & 0x28);
        F = f;
    }
    void and8(int v) { A &= v; F = sz53p(A) | 0x10; }
    void xor8(int v) { A ^= v; F = sz53p(A); }
    void or8(int v) { A |= v; F = sz53p(A); }
    int inc8(int v) { int r = (v + 1) & 0xFF; F = (F & 1) | SZ[r] | ((r & 0x0F) == 0 ? 0x10 : 0) | (v == 0x7F ? 4 : 0); return r; }
    int dec8(int v) { int r = (v - 1) & 0xFF; F = (F & 1) | SZ[r] | 2 | ((v & 0x0F) == 0 ? 0x10 : 0) | (v == 0x80 ? 4 : 0); return r; }
    void alu(int op, int v) {
        switch (op) { case 0: add8(v, false); break; case 1: add8(v, true); break; case 2: sub8(v, false, true); break; case 3: sub8(v, true, true); break;
            case 4: and8(v); break; case 5: xor8(v); break; case 6: or8(v); break; default: sub8(v, false, false); }
    }
    int add16(int a, int b) { int r = a + b; F = (F & (0x80 | 0x40 | 4)) | ((r >> 16) & 1) | (((a ^ b ^ r) >> 8) & 0x10) | ((r >> 8) & 0x28); return r & 0xFFFF; }
    void adc16(int v) { int hl = HL(), r = hl + v + (F & 1), res = r & 0xFFFF; F = ((res >> 8) & 0xA8) | (res == 0 ? 0x40 : 0) | ((r >> 16) & 1) | (((hl ^ v ^ r) >> 8) & 0x10) | ((((hl ^ ~v) & (hl ^ r)) & 0x8000) ? 4 : 0); setHL(res); }
    void sbc16(int v) { int hl = HL(), r = hl - v - (F & 1), res = r & 0xFFFF; F = ((res >> 8) & 0xA8) | (res == 0 ? 0x40 : 0) | 2 | ((r >> 16) & 1) | (((hl ^ v ^ r) >> 8) & 0x10) | ((((hl ^ v) & (hl ^ r)) & 0x8000) ? 4 : 0); setHL(res); }
    int rot(int op, int v) {
        int r = 0, c = 0;
        switch (op) {
            case 0: c = v >> 7; r = ((v << 1) | c) & 0xFF; break; case 1: c = v & 1; r = (v >> 1) | (c << 7); break;
            case 2: c = v >> 7; r = ((v << 1) | (F & 1)) & 0xFF; break; case 3: c = v & 1; r = (v >> 1) | ((F & 1) << 7); break;
            case 4: c = v >> 7; r = (v << 1) & 0xFF; break; case 5: c = v & 1; r = (v >> 1) | (v & 0x80); break;
            case 6: c = v >> 7; r = ((v << 1) | 1) & 0xFF; break; default: c = v & 1; r = v >> 1;
        }
        F = sz53p(r) | c; return r;
    }
    bool cond(int cc) {
        switch (cc) { case 0: return !(F & 0x40); case 1: return (F & 0x40) != 0; case 2: return !(F & 1); case 3: return (F & 1) != 0;
            case 4: return !(F & 4); case 5: return (F & 4) != 0; case 6: return !(F & 0x80); default: return (F & 0x80) != 0; }
    }
    int getR(int r, int idx) {
        switch (r) { case 0: return B; case 1: return C; case 2: return D; case 3: return E;
            case 4: return idx == 1 ? IX >> 8 : idx == 2 ? IY >> 8 : H; case 5: return idx == 1 ? IX & 0xFF : idx == 2 ? IY & 0xFF : L; case 7: return A; default: return 0; }
    }
    void setR(int r, int idx, int v) {
        switch (r) { case 0: B = v; break; case 1: C = v; break; case 2: D = v; break; case 3: E = v; break;
            case 4: if (idx == 1) IX = (IX & 0xFF) | (v << 8); else if (idx == 2) IY = (IY & 0xFF) | (v << 8); else H = v; break;
            case 5: if (idx == 1) IX = (IX & 0xFF00) | v; else if (idx == 2) IY = (IY & 0xFF00) | v; else L = v; break; case 7: A = v; break; }
    }
    int getRR(int p, int idx) { switch (p) { case 0: return BC(); case 1: return DE(); case 2: return idx == 1 ? IX : idx == 2 ? IY : HL(); default: return SP; } }
    void setRR(int p, int idx, int v) { switch (p) { case 0: setBC(v); break; case 1: setDE(v); break; case 2: if (idx == 1) IX = v; else if (idx == 2) IY = v; else setHL(v); break; default: SP = v; } }
    int getRR2(int p, int idx) { return p == 3 ? AF() : getRR(p, idx); }
    void setRR2(int p, int idx, int v) { if (p == 3) setAF(v); else setRR(p, idx, v); }
    int idxReg(int idx) { return idx == 1 ? IX : IY; }

    int exec(int op, int idx) {
        const int x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1, extra = idx ? 4 : 0;
        switch (x) {
        case 0:
            switch (z) {
            case 0:
                if (y == 0) return 4 + extra;
                if (y == 1) { std::swap(A, A2); std::swap(F, F2); return 4 + extra; }
                if (y == 2) { int d = simm(); B = (B - 1) & 0xFF; if (B) { PC = (PC + d) & 0xFFFF; return 13 + extra; } return 8 + extra; }
                if (y == 3) { int d = simm(); PC = (PC + d) & 0xFFFF; return 12 + extra; }
                { int d = simm(); if (cond(y - 4)) { PC = (PC + d) & 0xFFFF; return 12 + extra; } return 7 + extra; }
            case 1:
                if (!q) { setRR(p, idx, imm16()); return 10 + extra; }
                setRR(2, idx, add16(getRR(2, idx), getRR(p, idx))); return 11 + extra;
            case 2:
                switch (y) {
                case 0: write(BC(), A); return 7 + extra;
                case 1: A = read(BC()); return 7 + extra;
                case 2: write(DE(), A); return 7 + extra;
                case 3: A = read(DE()); return 7 + extra;
                case 4: wr16(imm16(), getRR(2, idx)); return 16 + extra;
                case 5: setRR(2, idx, rd16(imm16())); return 16 + extra;
                case 6: write(imm16(), A); return 13 + extra;
                default: A = read(imm16()); return 13 + extra;
                }
            case 3: setRR(p, idx, (getRR(p, idx) + (q ? -1 : 1)) & 0xFFFF); return 6 + extra;
            case 4: case 5: {
                if (y == 6) { int a; if (!idx) a = HL(); else { int d = simm(); a = (idxReg(idx) + d) & 0xFFFF; } int v = read(a); write(a, z == 4 ? inc8(v) : dec8(v)); return idx ? 23 : 11; }
                int v = getR(y, idx); setR(y, idx, z == 4 ? inc8(v) : dec8(v)); return 4 + extra;
            }
            case 6:
                if (y == 6) { if (!idx) { write(HL(), imm()); return 10; } int d = simm(), n = imm(); write((idxReg(idx) + d) & 0xFFFF, n); return 19; }
                setR(y, idx, imm()); return 7 + extra;
            default: {
                const int f = F, a = A;
                switch (y) {
                case 0: { int c = a >> 7; A = ((a << 1) | c) & 0xFF; F = (f & 0xC4) | (A & 0x28) | c; return 4 + extra; }
                case 1: { int c = a & 1; A = (a >> 1) | (c << 7); F = (f & 0xC4) | (A & 0x28) | c; return 4 + extra; }
                case 2: { int c = a >> 7; A = ((a << 1) | (f & 1)) & 0xFF; F = (f & 0xC4) | (A & 0x28) | c; return 4 + extra; }
                case 3: { int c = a & 1; A = (a >> 1) | ((f & 1) << 7); F = (f & 0xC4) | (A & 0x28) | c; return 4 + extra; }
                case 4: { int corr = 0, c = f & 1; if ((f & 0x10) || (a & 0x0F) > 9) corr |= 6; if (c || a > 0x99) { corr |= 0x60; c = 1; }
                    A = (f & 2) ? (a - corr) & 0xFF : (a + corr) & 0xFF; F = sz53p(A) | (f & 2) | c | ((a ^ A) & 0x10); return 4 + extra; }
                case 5: A = (~a) & 0xFF; F = (f & 0xC5) | 0x10 | 2 | (A & 0x28); return 4 + extra;
                case 6: F = (f & 0xC4) | 1 | (a & 0x28); return 4 + extra;
                default: F = (f & 0xC4) | ((f & 1) ? 0x10 : 1) | (a & 0x28); return 4 + extra;
                }
            }
            }
        case 1:
            if (y == 6 && z == 6) { halted = true; return 4; }
            if (y == 6) { int t = 7, a; if (!idx) a = HL(); else { int d = simm(); t += 8; a = (idxReg(idx) + d) & 0xFFFF; } write(a, getR(z, 0)); return t + extra; }
            if (z == 6) { int t = 7, a; if (!idx) a = HL(); else { int d = simm(); t += 8; a = (idxReg(idx) + d) & 0xFFFF; } setR(y, 0, read(a)); return t + extra; }
            setR(y, idx, getR(z, idx)); return 4 + extra;
        case 2:
            if (z == 6) { int t = 7, a; if (!idx) a = HL(); else { int d = simm(); t += 8; a = (idxReg(idx) + d) & 0xFFFF; } alu(y, read(a)); return t + extra; }
            alu(y, getR(z, idx)); return 4 + extra;
        default:
            switch (z) {
            case 0: if (cond(y)) { PC = pop(); return 11 + extra; } return 5 + extra;
            case 1:
                if (!q) { setRR2(p, idx, pop()); return 10 + extra; }
                switch (p) {
                case 0: PC = pop(); return 10 + extra;
                case 1: std::swap(B, B2); std::swap(C, C2); std::swap(D, D2); std::swap(E, E2); std::swap(H, H2); std::swap(L, L2); return 4 + extra;
                case 2: PC = getRR(2, idx); return 4 + extra;
                default: SP = getRR(2, idx); return 6 + extra;
                }
            case 2: { int a = imm16(); if (cond(y)) PC = a; return 10 + extra; }
            case 3:
                switch (y) {
                case 0: PC = imm16(); return 10 + extra;
                case 1: return execCB(idx);
                case 2: out(((A << 8) | imm()) & 0xFFFF, A); return 11 + extra;
                case 3: { int n = imm(); A = in(((A << 8) | n) & 0xFFFF) & 0xFF; return 11 + extra; }
                case 4: { int v = rd16(SP); wr16(SP, getRR(2, idx)); setRR(2, idx, v); return 19 + extra; }
                case 5: { int de = DE(); setDE(HL()); setHL(de); return 4; }
                case 6: IFF1 = IFF2 = false; return 4 + extra;
                default: IFF1 = IFF2 = true; eiPending = true; return 4 + extra;
                }
            case 4: { int a = imm16(); if (cond(y)) { push(PC); PC = a; return 17 + extra; } return 10 + extra; }
            case 5:
                if (!q) { push(getRR2(p, idx)); return 11 + extra; }
                switch (p) {
                case 0: { int a = imm16(); push(PC); PC = a; return 17 + extra; }
                case 1: return exec(fetch(), 1);
                case 2: return execED();
                default: return exec(fetch(), 2);
                }
            case 6: alu(y, imm()); return 7 + extra;
            default: push(PC); PC = y * 8; return 11 + extra;
            }
        }
    }
    int execCB(int idx) {
        if (idx) {
            int d = simm(), op = imm(), a = (idxReg(idx) + d) & 0xFFFF, x = op >> 6, y = (op >> 3) & 7, z = op & 7; int v = read(a);
            if (x == 1) { F = (F & 1) | 0x10 | (sz53p(v & (1 << y)) & ~0x28) | ((a >> 8) & 0x28); return 20; }
            if (x == 0) v = rot(y, v); else if (x == 2) v = v & ~(1 << y) & 0xFF; else v = (v | (1 << y)) & 0xFF;
            write(a, v); if (z != 6) setR(z, 0, v); return 23;
        }
        int op = fetch(), x = op >> 6, y = (op >> 3) & 7, z = op & 7;
        if (z == 6) {
            int v = read(HL());
            if (x == 1) { F = (F & 1) | 0x10 | (sz53p(v & (1 << y)) & ~0x28) | (v & 0x28); return 12; }
            if (x == 0) v = rot(y, v); else if (x == 2) v = v & ~(1 << y) & 0xFF; else v = (v | (1 << y)) & 0xFF;
            write(HL(), v); return 15;
        }
        int v = getR(z, 0);
        if (x == 1) { F = (F & 1) | 0x10 | (sz53p(v & (1 << y)) & ~0x28) | (v & 0x28); return 8; }
        if (x == 0) v = rot(y, v); else if (x == 2) v = v & ~(1 << y) & 0xFF; else v = (v | (1 << y)) & 0xFF;
        setR(z, 0, v); return 8;
    }
    int execED() {
        int op = fetch(), x = op >> 6, y = (op >> 3) & 7, z = op & 7, p = y >> 1, q = y & 1;
        if (x == 1) {
            switch (z) {
            case 0: { int v = in(BC()) & 0xFF; if (y != 6) setR(y, 0, v); F = (F & 1) | sz53p(v); return 12; }
            case 1: out(BC(), y == 6 ? 0 : getR(y, 0)); return 12;
            case 2: if (q) adc16(getRR(p, 0)); else sbc16(getRR(p, 0)); return 15;
            case 3: { int a = imm16(); if (q) setRR(p, 0, rd16(a)); else wr16(a, getRR(p, 0)); return 20; }
            case 4: { int v = A; A = 0; sub8(v, false, true); return 8; }
            case 5: PC = pop(); IFF1 = IFF2; return 14;
            case 6: { int m = y & 3; IM = m == 0 || m == 1 ? 0 : m == 2 ? 1 : 2; return 8; }
            default:
                switch (y) {
                case 0: I = A; return 9;
                case 1: R = A; return 9;
                case 2: A = I; F = (F & 1) | SZ[A] | (IFF2 ? 4 : 0); return 9;
                case 3: A = R; F = (F & 1) | SZ[A] | (IFF2 ? 4 : 0); return 9;
                case 4: { int m = read(HL()); write(HL(), ((A << 4) | (m >> 4)) & 0xFF); A = (A & 0xF0) | (m & 0x0F); F = (F & 1) | sz53p(A); return 18; }
                case 5: { int m = read(HL()); write(HL(), ((m << 4) | (A & 0x0F)) & 0xFF); A = (A & 0xF0) | (m >> 4); F = (F & 1) | sz53p(A); return 18; }
                default: return 8;
                }
            }
        }
        if (x == 2 && z <= 3 && y >= 4) {
            const bool inc = (y & 1) == 0, repeat = y >= 6; const int dir = inc ? 1 : -1;
            switch (z) {
            case 0: { int v = read(HL()); write(DE(), v); setHL((HL() + dir) & 0xFFFF); setDE((DE() + dir) & 0xFFFF); setBC((BC() - 1) & 0xFFFF);
                int n = (v + A) & 0xFF; F = (F & (0x80 | 0x40 | 1)) | (BC() ? 4 : 0) | (n & 8) | ((n & 2) ? 0x20 : 0);
                if (repeat && BC()) { PC = (PC - 2) & 0xFFFF; return 21; } return 16; }
            case 1: { int v = read(HL()), r = (A - v) & 0xFF; bool h = ((A ^ v ^ r) & 0x10) != 0; setHL((HL() + dir) & 0xFFFF); setBC((BC() - 1) & 0xFFFF);
                int n = (r - (h ? 1 : 0)) & 0xFF; F = (F & 1) | 2 | (r & 0x80) | (r == 0 ? 0x40 : 0) | (h ? 0x10 : 0) | (BC() ? 4 : 0) | (n & 8) | ((n & 2) ? 0x20 : 0);
                if (repeat && BC() && r != 0) { PC = (PC - 2) & 0xFFFF; return 21; } return 16; }
            case 2: { int v = in(BC()) & 0xFF; write(HL(), v); B = (B - 1) & 0xFF; setHL((HL() + dir) & 0xFFFF); F = SZ[B] | 2;
                if (repeat && B) { PC = (PC - 2) & 0xFFFF; return 21; } return 16; }
            default: { int v = read(HL()); B = (B - 1) & 0xFF; out(BC(), v); setHL((HL() + dir) & 0xFFFF); F = SZ[B] | 2;
                if (repeat && B) { PC = (PC - 2) & 0xFFFF; return 21; } return 16; }
            }
        }
        return 8;
    }
};

// 8253 programmable interval timer (all six modes, binary)
class Pit8253 {
public:
    struct Ch { int reload = 0, count = 0, latched = 0, mode = 0, rw = 3; bool out = true, gate = true, loaded = false, armed = false, latchedValid = false, writeHigh = false, readHigh = false, pendingReload = false, triggerSeen = false; };
    Ch c[3];
    struct Listener { virtual ~Listener() = default; virtual void pitOut(int i, bool v) = 0; };
    Listener* onOut = nullptr;

    void write(int a, int v) {
        if (a == 3) { int sc = v >> 6; if (sc == 3) return; Ch& k = c[sc]; int rw = (v >> 4) & 3;
            if (rw == 0) { k.latched = k.count; k.latchedValid = true; k.readHigh = false; return; }
            k.rw = rw; k.mode = (v >> 1) & 7; if (k.mode > 5) k.mode -= 4; k.writeHigh = false; k.readHigh = false; k.loaded = false; k.armed = false; setOut(sc, k.mode != 0); return; }
        Ch& k = c[a];
        if (k.rw == 1) k.reload = (k.reload & 0xFF00) | v;
        else if (k.rw == 2) k.reload = (k.reload & 0x00FF) | (v << 8);
        else { if (!k.writeHigh) { k.reload = (k.reload & 0xFF00) | v; k.writeHigh = true; if (k.mode == 0) setOut(a, false); return; }
            k.reload = (k.reload & 0x00FF) | (v << 8); k.writeHigh = false; }
        startCount(a);
    }
    int read(int a) {
        if (a == 3) return 0xFF; Ch& k = c[a]; int v = k.latchedValid ? k.latched : k.count; int r;
        if (k.rw == 1) r = v & 0xFF; else if (k.rw == 2) r = (v >> 8) & 0xFF;
        else { r = k.readHigh ? (v >> 8) & 0xFF : v & 0xFF; k.readHigh = !k.readHigh; if (!k.readHigh) k.latchedValid = false; return r; }
        k.latchedValid = false; return r;
    }
    void setGate(int i, bool g) {
        Ch& k = c[i]; bool rising = g && !k.gate; k.gate = g;
        if (rising && k.loaded && (k.mode == 2 || k.mode == 3)) k.pendingReload = true;
        if (rising && k.loaded && (k.mode == 1 || k.mode == 5)) k.triggerSeen = true;
        if (!g && (k.mode == 2 || k.mode == 3)) setOut(i, true);
    }
    void clockRise(int i) { Ch& k = c[i]; if (k.triggerSeen) { k.triggerSeen = false; k.pendingReload = true; } }
    void clock(int i, int n, bool fallOnly = false) {
        Ch& k = c[i]; if (!k.loaded) return;
        if (!fallOnly && !k.pendingReload && !k.triggerSeen && n > 1) {
            if ((k.mode == 0 || k.mode == 4) && !k.gate) return;
            if ((k.mode == 1 || k.mode == 5) && !k.armed) return;
            if ((k.mode == 2 || k.mode == 3) && !k.gate) return;
            int half = k.mode == 3 ? ((k.reload == 0 ? 65536 : k.reload) >> 1) : -1;
            int cc = k.count == 0 ? 65536 : k.count;
            int lim = cc - 2; if (half > 0 && cc > half) lim = std::min(lim, cc - half - 1);
            if (k.mode == 0 && k.out) { k.count = (k.count - n) & 0xFFFF; return; }
            if (lim >= n) { k.count = (k.count - n) & 0xFFFF; return; }
        }
        for (int s = 0; s < n; s++) {
            if (!fallOnly && k.triggerSeen) { k.triggerSeen = false; k.pendingReload = true; }
            if (k.pendingReload) {
                if ((k.mode == 1 || k.mode == 5) || k.gate) { k.pendingReload = false; k.count = k.reload;
                    if (k.mode == 1) { k.armed = true; setOut(i, false); }
                    if (k.mode == 5 || k.mode == 4) k.armed = true; continue; }
            }
            switch (k.mode) {
            case 0: if (!k.gate) break; k.count = (k.count - 1) & 0xFFFF; if (k.count == 0 && !k.out) setOut(i, true); break;
            case 1: if (!k.armed) break; k.count = (k.count - 1) & 0xFFFF; if (k.count == 0) { setOut(i, true); k.armed = false; } break;
            case 2: if (!k.gate) break; k.count = (k.count - 1) & 0xFFFF; if (k.count == 1) setOut(i, false); else if (k.count == 0) { setOut(i, true); k.count = k.reload; } break;
            case 3: { if (!k.gate) break; int full = k.reload == 0 ? 65536 : k.reload; k.count = (k.count - 1) & 0xFFFF;
                if (k.count == 0) { k.count = k.reload; setOut(i, true); } else if (k.count == (full >> 1)) setOut(i, false); break; }
            case 4: if (!k.gate) break; k.count = (k.count - 1) & 0xFFFF; if (k.count == 0 && k.armed) { setOut(i, false); setOut(i, true); k.armed = false; } break;
            default: if (!k.armed) break; k.count = (k.count - 1) & 0xFFFF; if (k.count == 0) { setOut(i, false); setOut(i, true); k.armed = false; } break;
            }
            if (k.mode == 1 && !k.armed && !k.pendingReload) break;
        }
    }
    void setOut(int i, bool v) { if (c[i].out != v) { c[i].out = v; if (onOut) onOut->pitOut(i, v); } }
    void startCount(int i) {
        Ch& k = c[i]; k.loaded = true;
        switch (k.mode) { case 0: setOut(i, false); k.pendingReload = true; break; case 1: case 5: k.armed = false; break;
            case 2: case 3: setOut(i, true); k.pendingReload = true; break; default: setOut(i, true); k.pendingReload = true; }
    }
};

} // namespace farp
