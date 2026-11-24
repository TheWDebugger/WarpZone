#pragma once
// NES 2A03 APU: 2 pulse channels, triangle, noise (DMC is not emulated; SMB1 doesn't use it).
// Produces mono 16-bit samples at a configurable rate by box-averaging the mixer output over
// each sample period. Pure C++, no Geode dependency.
#include <algorithm>
#include <cstdint>
#include <vector>

namespace nes {

class Apu {
public:
    static constexpr double CPU_HZ = 1789773.0;

    void setSampleRate(int hz) { rate = hz; }
    std::vector<int16_t>& samples() { return out; }

    void reset() {
        p1 = Pulse{}; p2 = Pulse{}; tri = Triangle{}; noise = Noise{};
        p1.ones = true;
        fiveStep = false; frameCycle = 0; odd = false;
        acc = 0; accN = 0; frac = 0; hpX = hpY = 0;
        out.clear();
    }

    void write(uint16_t a, uint8_t v) {
        switch (a) {
            case 0x4000: p1.writeCtrl(v); break;
            case 0x4001: p1.writeSweep(v); break;
            case 0x4002: p1.period = (p1.period & 0x700) | v; break;
            case 0x4003: p1.writeHi(v); break;
            case 0x4004: p2.writeCtrl(v); break;
            case 0x4005: p2.writeSweep(v); break;
            case 0x4006: p2.period = (p2.period & 0x700) | v; break;
            case 0x4007: p2.writeHi(v); break;
            case 0x4008: tri.halt = v & 0x80; tri.linearReload = v & 0x7F; break;
            case 0x400A: tri.period = (tri.period & 0x700) | v; break;
            case 0x400B:
                tri.period = (tri.period & 0xFF) | ((v & 7) << 8);
                if (tri.enabled) tri.len = LEN[v >> 3];
                tri.linearFlag = true;
                break;
            case 0x400C: noise.writeCtrl(v); break;
            case 0x400E: noise.mode = v & 0x80; noise.period = NOISE_PERIOD[v & 15]; break;
            case 0x400F:
                if (noise.enabled) noise.len = LEN[v >> 3];
                noise.env.start = true;
                break;
            case 0x4015:
                p1.enabled = v & 1;    if (!p1.enabled) p1.len = 0;
                p2.enabled = v & 2;    if (!p2.enabled) p2.len = 0;
                tri.enabled = v & 4;   if (!tri.enabled) tri.len = 0;
                noise.enabled = v & 8; if (!noise.enabled) noise.len = 0;
                break;
            case 0x4017:
                fiveStep = v & 0x80;
                frameCycle = 0;
                if (fiveStep) { quarter(); half(); }
                break;
            default: break; // DMC registers ($4010-$4013)
        }
    }

    uint8_t readStatus() const {
        return (p1.len ? 1 : 0) | (p2.len ? 2 : 0) | (tri.len ? 4 : 0) | (noise.len ? 8 : 0);
    }

    void run(int cpuCycles) { for (int i = 0; i < cpuCycles; i++) cycle(); }

private:
    static constexpr uint8_t LEN[32] = {10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
                                        12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30};
    static constexpr uint16_t NOISE_PERIOD[16] = {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068};
    static constexpr uint8_t DUTY[4][8] = {{0, 1, 0, 0, 0, 0, 0, 0}, {0, 1, 1, 0, 0, 0, 0, 0},
                                           {0, 1, 1, 1, 1, 0, 0, 0}, {1, 0, 0, 1, 1, 1, 1, 1}};

    struct Envelope {
        bool start = false, loop = false, constant = false;
        uint8_t period = 0, divider = 0, decay = 0;
        void clock() {
            if (start) { start = false; decay = 15; divider = period; }
            else if (divider == 0) { divider = period; if (decay > 0) decay--; else if (loop) decay = 15; }
            else divider--;
        }
        uint8_t volume() const { return constant ? period : decay; }
    };

    struct Pulse {
        bool enabled = false, ones = false, halt = false;
        uint8_t duty = 0, step = 0, len = 0;
        uint16_t timer = 0, period = 0;
        Envelope env;
        bool sweepEn = false, sweepNeg = false, sweepReload = false;
        uint8_t sweepPeriod = 0, sweepDiv = 0, sweepShift = 0;

        void writeCtrl(uint8_t v) { duty = v >> 6; halt = v & 0x20; env.loop = halt; env.constant = v & 0x10; env.period = v & 15; }
        void writeSweep(uint8_t v) { sweepEn = v & 0x80; sweepPeriod = (v >> 4) & 7; sweepNeg = v & 8; sweepShift = v & 7; sweepReload = true; }
        void writeHi(uint8_t v) {
            period = (period & 0xFF) | ((v & 7) << 8);
            if (enabled) len = LEN[v >> 3];
            step = 0; env.start = true;
        }
        int target() const {
            int ch = period >> sweepShift;
            if (sweepNeg) ch = -ch - (ones ? 1 : 0);
            return period + ch;
        }
        bool muted() const { return period < 8 || target() > 0x7FF; }
        void clockSweep() {
            if (sweepDiv == 0 && sweepEn && sweepShift && !muted()) period = (uint16_t)target();
            if (sweepDiv == 0 || sweepReload) { sweepDiv = sweepPeriod; sweepReload = false; }
            else sweepDiv--;
        }
        void clockTimer() { if (timer == 0) { timer = period; step = (step + 1) & 7; } else timer--; }
        void clockLen() { if (!halt && len) len--; }
        int output() const { return (!enabled || !len || muted() || !DUTY[duty][step]) ? 0 : env.volume(); }
    };

    struct Triangle {
        bool enabled = false, halt = false, linearFlag = false;
        uint8_t len = 0, linearReload = 0, linear = 0, step = 0;
        uint16_t period = 0, timer = 0;
        void clockLinear() {
            if (linearFlag) linear = linearReload; else if (linear) linear--;
            if (!halt) linearFlag = false;
        }
        void clockLen() { if (!halt && len) len--; }
        void clockTimer() {
            if (timer == 0) { timer = period; if (len && linear && period >= 2) step = (step + 1) & 31; }
            else timer--;
        }
        int output() const { return step < 16 ? 15 - step : step - 16; }
    };

    struct Noise {
        bool enabled = false, halt = false, mode = false;
        uint8_t len = 0;
        uint16_t period = 4, timer = 0, shift = 1;
        Envelope env;
        void writeCtrl(uint8_t v) { halt = v & 0x20; env.loop = halt; env.constant = v & 0x10; env.period = v & 15; }
        void clockLen() { if (!halt && len) len--; }
        void clockTimer() {
            if (timer == 0) {
                timer = period - 1;
                uint16_t bit = (shift & 1) ^ ((shift >> (mode ? 6 : 1)) & 1);
                shift = (shift >> 1) | (bit << 14);
            } else timer--;
        }
        int output() const { return (!enabled || !len || (shift & 1)) ? 0 : env.volume(); }
    };

    void quarter() { p1.env.clock(); p2.env.clock(); noise.env.clock(); tri.clockLinear(); }
    void half() {
        p1.clockLen(); p2.clockLen(); tri.clockLen(); noise.clockLen();
        p1.clockSweep(); p2.clockSweep();
    }

    void cycle() {
        frameCycle++;
        if (frameCycle == 3729 || frameCycle == 11186) quarter();
        else if (frameCycle == 7457) { quarter(); half(); }
        else if (!fiveStep && frameCycle == 14915) { quarter(); half(); frameCycle = 0; }
        else if (fiveStep && frameCycle == 18641) { quarter(); half(); frameCycle = 0; }

        tri.clockTimer();
        noise.clockTimer();
        if (odd) { p1.clockTimer(); p2.clockTimer(); }
        odd = !odd;

        // Nonlinear mixer (nesdev formulas), averaged over the sample period.
        float pulse = (float)(p1.output() + p2.output());
        float pOut = pulse > 0 ? 95.88f / (8128.f / pulse + 100.f) : 0.f;
        float t = tri.output() / 8227.f + noise.output() / 12241.f;
        float tndOut = t > 0 ? 159.79f / (1.f / t + 100.f) : 0.f;
        acc += pOut + tndOut; accN++;

        frac += rate;
        if (frac >= CPU_HZ) {
            frac -= CPU_HZ;
            float s = acc / accN; acc = 0; accN = 0;
            float y = s - hpX + 0.995f * hpY; // remove DC offset
            hpX = s; hpY = y;
            out.push_back((int16_t)std::clamp(y * 40000.f, -32767.f, 32767.f));
            if (out.size() > (1u << 20)) out.clear(); // nobody is draining (headless test); stay bounded
        }
    }

    Pulse p1{.ones = true}, p2;
    Triangle tri;
    Noise noise;
    bool fiveStep = false, odd = false;
    int frameCycle = 0;
    int rate = 44100;
    double frac = 0;
    float acc = 0, hpX = 0, hpY = 0;
    int accN = 0;
    std::vector<int16_t> out;
};

} // namespace nes
