#pragma once
// NES console: CPU + PPU + mapper 0 (NROM) + controller 1. No audio yet. Pure C++.
#include "Cpu.hpp"
#include "Ppu.hpp"
#include <vector>

namespace nes {

enum Button : uint8_t { BTN_A = 1, BTN_B = 2, BTN_SELECT = 4, BTN_START = 8, BTN_UP = 16, BTN_DOWN = 32, BTN_LEFT = 64, BTN_RIGHT = 128 };

class Nes : public CpuBus {
public:
    Nes(std::vector<uint8_t> prgRom, std::vector<uint8_t> chrRom, bool verticalMirror)
        : prg(std::move(prgRom)) {
        ppu.init(std::move(chrRom), verticalMirror);
        cpu.bus = this;
        reset();
    }

    void reset() {
        std::fill(std::begin(ram), std::end(ram), 0);
        cpu.reset();
        dmaStall = 0;
    }

    void setButtons(uint8_t b) { buttons = b; }
    uint32_t const* framebuffer() const { return ppu.fb; } // 256x240, RGBA bytes

    void runFrame() {
        ppu.frameReady = false;
        while (!ppu.frameReady) stepInstruction();
    }

    void stepInstruction() {
        int cyc;
        if (ppu.nmiPending) { ppu.nmiPending = false; cyc = cpu.nmi(); }
        else cyc = cpu.step();
        if (dmaStall) { cyc += dmaStall; cpu.cycles += dmaStall; dmaStall = 0; }
        for (int i = 0; i < cyc * 3; i++) ppu.tick();
    }

    Cpu cpu;
    Ppu ppu;

    // ---- CPU bus ----
    uint8_t read(uint16_t a) override {
        if (a < 0x2000) return ram[a & 0x7FF];
        if (a < 0x4000) return ppu.regRead(a & 7);
        if (a == 0x4016) {
            uint8_t r = shift & 1;
            if (strobe) shift = buttons; else shift = (shift >> 1) | 0x80;
            return r | 0x40;
        }
        if (a >= 0x6000 && a < 0x8000) return prgRam[a - 0x6000];
        if (a >= 0x8000) return prg[(a - 0x8000) % prg.size()];
        return 0;
    }

    void write(uint16_t a, uint8_t v) override {
        if (a < 0x2000) { ram[a & 0x7FF] = v; return; }
        if (a < 0x4000) { ppu.regWrite(a & 7, v); return; }
        if (a == 0x4014) {
            for (int i = 0; i < 256; i++) ppu.dmaWrite(i, read((v << 8) | i));
            dmaStall = 513 + (cpu.cycles & 1);
            return;
        }
        if (a == 0x4016) { strobe = v & 1; if (strobe) shift = buttons; return; }
        if (a >= 0x6000 && a < 0x8000) { prgRam[a - 0x6000] = v; return; }
        // $4000-$4013, $4015, $4017: APU (not implemented)
    }

private:
    std::vector<uint8_t> prg;
    uint8_t ram[0x800] = {};
    uint8_t prgRam[0x2000] = {};
    uint8_t buttons = 0, shift = 0;
    bool strobe = false;
    int dmaStall = 0;
};

} // namespace nes
