#pragma once
// MOS 6502 (NES 2A03, no decimal mode). Official opcodes only; unknown opcodes act as 2-cycle NOPs.
// Pure C++, no engine dependencies.
#include <cstdint>

namespace nes {

struct CpuBus {
    virtual uint8_t read(uint16_t addr) = 0;
    virtual void write(uint16_t addr, uint8_t v) = 0;
    virtual ~CpuBus() = default;
};

class Cpu {
public:
    enum Flag : uint8_t { C = 1, Z = 2, I = 4, D = 8, B = 16, U = 32, V = 64, N = 128 };

    uint8_t a = 0, x = 0, y = 0, sp = 0xFD, p = 0x24;
    uint16_t pc = 0;
    uint64_t cycles = 0;
    CpuBus* bus = nullptr;

    void reset() {
        sp = 0xFD; p = 0x24; a = x = y = 0;
        pc = rd16(0xFFFC);
        cycles = 7;
    }

    // Returns cycles consumed.
    int nmi() { int c = interrupt(0xFFFA, false); cycles += c; return c; }
    int irq() { if (p & I) return 0; int c = interrupt(0xFFFE, false); cycles += c; return c; }

    int step() {
        uint8_t op = rd(pc++);
        int cyc = exec(op);
        cycles += cyc;
        return cyc;
    }

private:
    uint8_t rd(uint16_t a_) { return bus->read(a_); }
    void wr(uint16_t a_, uint8_t v) { bus->write(a_, v); }
    uint16_t rd16(uint16_t a_) { return rd(a_) | (rd(a_ + 1) << 8); }
    void push(uint8_t v) { wr(0x100 + sp--, v); }
    uint8_t pull() { return rd(0x100 + ++sp); }
    void setf(uint8_t f, bool on) { if (on) p |= f; else p &= ~f; }
    void zn(uint8_t v) { setf(Z, v == 0); setf(N, v & 0x80); }

    int interrupt(uint16_t vec, bool brk) {
        push(pc >> 8); push(pc & 0xFF);
        push((uint8_t)(((p | U) & ~B) | (brk ? B : 0)));
        p |= I;
        pc = rd16(vec);
        return 7; // caller adds to `cycles`
    }

    void adc(uint8_t v) {
        unsigned s = a + v + (p & C);
        setf(V, (~(a ^ v) & (a ^ s) & 0x80) != 0);
        setf(C, s > 0xFF);
        a = (uint8_t)s; zn(a);
    }
    void cmp(uint8_t r, uint8_t v) { setf(C, r >= v); zn((uint8_t)(r - v)); }

    uint8_t shift(int kind, uint8_t v) { // 0 ASL 1 ROL 2 LSR 3 ROR
        uint8_t c = p & C, r;
        switch (kind) {
            case 0: setf(C, v & 0x80); r = v << 1; break;
            case 1: setf(C, v & 0x80); r = (v << 1) | c; break;
            case 2: setf(C, v & 1);    r = v >> 1; break;
            default: setf(C, v & 1);   r = (v >> 1) | (c << 7); break;
        }
        zn(r); return r;
    }

    int exec(uint8_t op) {
        // ---- branches (xxy10000) ----
        if ((op & 0x1F) == 0x10) {
            int8_t off = (int8_t)rd(pc++);
            static const uint8_t flags[4] = {N, V, C, Z};
            bool want = (op >> 5) & 1;
            bool take = ((p & flags[op >> 6]) != 0) == want;
            if (!take) return 2;
            uint16_t t = pc + off;
            int cyc = 3 + (((pc ^ t) & 0xFF00) ? 1 : 0);
            pc = t;
            return cyc;
        }
        // ---- single-byte / special ----
        switch (op) {
            case 0x00: pc++; return interrupt(0xFFFE, true) ;
            case 0x20: { uint16_t t = rd16(pc); push((pc + 1) >> 8); push((pc + 1) & 0xFF); pc = t; return 6; }
            case 0x40: p = (pull() & ~B) | U; pc = pull(); pc |= pull() << 8; return 6;
            case 0x60: pc = pull(); pc |= pull() << 8; pc++; return 6;
            case 0x08: push(p | B | U); return 3;
            case 0x28: p = (pull() & ~B) | U; return 4;
            case 0x48: push(a); return 3;
            case 0x68: a = pull(); zn(a); return 4;
            case 0x88: y--; zn(y); return 2;
            case 0xA8: y = a; zn(y); return 2;
            case 0xC8: y++; zn(y); return 2;
            case 0xE8: x++; zn(x); return 2;
            case 0x18: p &= ~C; return 2;
            case 0x38: p |= C; return 2;
            case 0x58: p &= ~I; return 2;
            case 0x78: p |= I; return 2;
            case 0x98: a = y; zn(a); return 2;
            case 0xB8: p &= ~V; return 2;
            case 0xD8: p &= ~D; return 2;
            case 0xF8: p |= D; return 2;
            case 0x8A: a = x; zn(a); return 2;
            case 0x9A: sp = x; return 2;
            case 0xAA: x = a; zn(x); return 2;
            case 0xBA: x = sp; zn(x); return 2;
            case 0xCA: x--; zn(x); return 2;
            case 0xEA: return 2;
        }

        int aaa = op >> 5, bbb = (op >> 2) & 7, cc = op & 3;
        uint16_t addr = 0;
        bool cross = false;
        auto zpx = [&](uint8_t idx) { return (uint16_t)((rd(pc++) + idx) & 0xFF); };
        auto absi = [&](uint8_t idx) {
            uint16_t base = rd16(pc); pc += 2;
            addr = base + idx; cross = (base ^ addr) & 0xFF00; return addr;
        };

        if (cc == 1) {
            bool store = aaa == 4;
            if (store && bbb == 2) { pc++; return 2; } // unofficial NOP #imm
            int cyc;
            switch (bbb) {
                case 0: { uint8_t z = rd(pc++) + x; addr = rd(z) | (rd((uint8_t)(z + 1)) << 8); cyc = 6; break; }
                case 1: addr = rd(pc++); cyc = 3; break;
                case 2: addr = pc++; cyc = 2; break;
                case 3: addr = rd16(pc); pc += 2; cyc = 4; break;
                case 4: { uint8_t z = rd(pc++); uint16_t base = rd(z) | (rd((uint8_t)(z + 1)) << 8);
                          addr = base + y; cross = (base ^ addr) & 0xFF00; cyc = store ? 6 : 5 + cross; break; }
                case 5: addr = zpx(x); cyc = 4; break;
                case 6: absi(y); cyc = store ? 5 : 4 + cross; break;
                default: absi(x); cyc = store ? 5 : 4 + cross; break;
            }
            if (store) { wr(addr, a); return cyc; }
            uint8_t v = rd(addr);
            switch (aaa) {
                case 0: a |= v; zn(a); break;
                case 1: a &= v; zn(a); break;
                case 2: a ^= v; zn(a); break;
                case 3: adc(v); break;
                case 5: a = v; zn(a); break;
                case 6: cmp(a, v); break;
                case 7: adc(v ^ 0xFF); break;
            }
            return cyc;
        }

        if (cc == 2) {
            bool rmw = aaa <= 3 || aaa >= 6;
            if (bbb == 2 && aaa <= 3) { a = shift(aaa, a); return 2; }
            int cyc;
            switch (bbb) {
                case 0: if (aaa == 5) { x = rd(pc++); zn(x); return 2; } pc++; return 2;
                case 1: addr = rd(pc++); cyc = rmw ? 5 : 3; break;
                case 3: addr = rd16(pc); pc += 2; cyc = rmw ? 6 : 4; break;
                case 5: addr = zpx((aaa == 4 || aaa == 5) ? y : x); cyc = rmw ? 6 : 4; break;
                case 7: absi((aaa == 5) ? y : x); cyc = rmw ? 7 : 4 + cross; break;
                default: return 2;
            }
            if (aaa == 4) { wr(addr, x); return cyc; }
            uint8_t v = rd(addr);
            if (aaa == 5) { x = v; zn(x); return cyc; }
            if (aaa <= 3) v = shift(aaa, v);
            else if (aaa == 6) { v--; zn(v); }
            else { v++; zn(v); }
            wr(addr, v);
            return cyc;
        }

        // cc == 0
        int cyc;
        switch (bbb) {
            case 0: addr = pc++; cyc = 2; break;
            case 1: addr = rd(pc++); cyc = 3; break;
            case 3:
                if (aaa == 2) { pc = rd16(pc); return 3; }                   // JMP abs
                if (aaa == 3) {                                              // JMP (ind), with page-wrap bug
                    uint16_t ptr = rd16(pc);
                    pc = rd(ptr) | (rd((ptr & 0xFF00) | ((ptr + 1) & 0xFF)) << 8);
                    return 5;
                }
                addr = rd16(pc); pc += 2; cyc = 4; break;
            case 5: addr = zpx(x); cyc = 4; break;
            case 7: absi(x); cyc = 4 + cross; break;
            default: return 2;
        }
        switch (aaa) {
            case 1: { uint8_t v = rd(addr); setf(Z, (a & v) == 0); setf(V, v & 0x40); setf(N, v & 0x80); break; }
            case 4: wr(addr, y); return (bbb == 7) ? 5 : cyc;
            case 5: y = rd(addr); zn(y); break;
            case 6: cmp(y, rd(addr)); break;
            case 7: cmp(x, rd(addr)); break;
            default: return 2; // unofficial
        }
        return cyc;
    }
};

} // namespace nes
