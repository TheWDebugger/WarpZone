#pragma once
// NES 2C02 PPU: dot-based background pipeline (loopy registers + shifters) with
// sprite evaluation per scanline, sprite-0 hit, vblank/NMI. Pure C++.
#include <cstdint>
#include <cstring>
#include <vector>

namespace nes {

// Standard 2C02 palette as ABGR (little-endian RGBA bytes) for direct GPU upload.
inline uint32_t nesColor(int i) {
    static const uint32_t rgb[64] = {
        0x666666,0x002A88,0x1412A7,0x3B00A4,0x5C007E,0x6E0040,0x6C0600,0x561D00,
        0x333500,0x0B4800,0x005200,0x004F08,0x00404D,0x000000,0x000000,0x000000,
        0xADADAD,0x155FD9,0x4240FF,0x7527FE,0xA01ACC,0xB71E7B,0xB53120,0x994E00,
        0x6B6D00,0x388700,0x0D9300,0x008F32,0x007C8D,0x000000,0x000000,0x000000,
        0xFFFEFF,0x64B0FF,0x9290FF,0xC676FF,0xF36AFF,0xFE6ECC,0xFE8170,0xEA9E22,
        0xBCBE00,0x88D800,0x5CE430,0x45E082,0x48CDDE,0x4F4F4F,0x000000,0x000000,
        0xFFFEFF,0xC0DFFF,0xD3D2FF,0xE8C8FF,0xFBC2FF,0xFEC4EA,0xFECCC5,0xF7D8A5,
        0xE4E594,0xCFEF96,0xBDF4AB,0xB3F3CC,0xB5EBF2,0xB8B8B8,0x000000,0x000000};
    uint32_t c = rgb[i & 63];
    return ((c >> 16) & 0xFF) | (c & 0xFF00) | ((c & 0xFF) << 16) | 0xFF000000u;
}

class Ppu {
public:
    uint32_t fb[256 * 240] = {};
    bool frameReady = false;
    bool nmiPending = false;
    uint8_t oam[256] = {};

    void init(std::vector<uint8_t> chr, bool vertical) {
        chrMem = std::move(chr);
        chrRam = chrMem.empty();
        if (chrRam) chrMem.assign(0x2000, 0);
        verticalMirror = vertical;
    }

    // ---- CPU-facing registers ($2000-$2007) ----
    uint8_t regRead(int r) {
        switch (r & 7) {
            case 2: {
                uint8_t v = (status & 0xE0) | (dataBuf & 0x1F);
                status &= ~0x80; w = 0;
                return v;
            }
            case 4: return oam[oamAddr];
            case 7: {
                uint8_t v = dataBuf;
                dataBuf = read(vaddr);
                if ((vaddr & 0x3FFF) >= 0x3F00) { v = dataBuf; dataBuf = read(vaddr & 0x2FFF); }
                vaddr += (ctrl & 4) ? 32 : 1;
                return v;
            }
        }
        return 0;
    }

    void regWrite(int r, uint8_t d) {
        switch (r & 7) {
            case 0: ctrl = d; taddr = (taddr & 0xF3FF) | ((d & 3) << 10); break;
            case 1: mask = d; break;
            case 3: oamAddr = d; break;
            case 4: oam[oamAddr++] = d; break;
            case 5:
                if (!w) { fineX = d & 7; taddr = (taddr & ~0x1F) | (d >> 3); }
                else    { taddr = (taddr & ~0x73E0) | ((d & 7) << 12) | ((d >> 3) << 5); }
                w ^= 1; break;
            case 6:
                if (!w) { taddr = (taddr & 0x00FF) | ((d & 0x3F) << 8); }
                else    { taddr = (taddr & 0xFF00) | d; vaddr = taddr; }
                w ^= 1; break;
            case 7:
                write(vaddr, d);
                vaddr += (ctrl & 4) ? 32 : 1;
                break;
        }
    }

    void dmaWrite(uint8_t i, uint8_t v) { oam[(uint8_t)(oamAddr + i)] = v; }

    void tick() {
        bool rendering = mask & 0x18;

        if (scanline >= -1 && scanline < 240) {
            if (scanline == 0 && cycle == 0 && oddFrame && rendering) cycle = 1; // odd-frame skip
            if (scanline == -1 && cycle == 1) status &= ~0xE0;

            if (rendering) {
                if ((cycle >= 2 && cycle < 258) || (cycle >= 321 && cycle < 338)) {
                    updateShifters();
                    switch ((cycle - 1) & 7) {
                        case 0: loadShifters(); ntByte = read(0x2000 | (vaddr & 0x0FFF)); break;
                        case 2: {
                            uint8_t at = read(0x23C0 | (vaddr & 0x0C00) | ((vaddr >> 4) & 0x38) | ((vaddr >> 2) & 7));
                            if (vaddr & 0x40) at >>= 4;
                            if (vaddr & 0x02) at >>= 2;
                            atByte = at & 3; break;
                        }
                        case 4: lsb = read(bgBase() + ntByte * 16 + ((vaddr >> 12) & 7)); break;
                        case 6: msb = read(bgBase() + ntByte * 16 + ((vaddr >> 12) & 7) + 8); break;
                        case 7: incX(); break;
                    }
                }
                if (cycle == 256) incY();
                if (cycle == 257) { loadShifters(); vaddr = (vaddr & ~0x041F) | (taddr & 0x041F); }
                if (scanline == -1 && cycle >= 280 && cycle < 305) vaddr = (vaddr & ~0x7BE0) | (taddr & 0x7BE0);
            }

            if (cycle == 257) evaluateSprites(rendering);
            if (cycle == 340) { std::memcpy(cur, nxt, sizeof(cur)); curCount = nxtCount; curHas0 = nxtHas0; }
        }

        if (scanline == 241 && cycle == 1) { status |= 0x80; frameReady = true; }

        if (scanline >= 0 && scanline < 240 && cycle >= 1 && cycle <= 256) composePixel();

        bool line = (status & 0x80) && (ctrl & 0x80);
        if (line && !prevNmiLine) nmiPending = true;
        prevNmiLine = line;

        if (++cycle >= 341) {
            cycle = 0;
            if (++scanline >= 261) { scanline = -1; oddFrame = !oddFrame; }
        }
    }

private:
    struct Spr { uint8_t lo, hi, attr, x; };

    std::vector<uint8_t> chrMem;
    bool chrRam = false, verticalMirror = true;
    uint8_t vram[0x800] = {};
    uint8_t pal[32] = {};

    uint8_t ctrl = 0, mask = 0, status = 0, oamAddr = 0, dataBuf = 0, fineX = 0, w = 0;
    uint16_t vaddr = 0, taddr = 0;
    int scanline = 0, cycle = 0;
    bool oddFrame = false, prevNmiLine = false;

    uint8_t ntByte = 0, atByte = 0, lsb = 0, msb = 0;
    uint16_t shLo = 0, shHi = 0, atLo = 0, atHi = 0;

    Spr cur[8] = {}, nxt[8] = {};
    int curCount = 0, nxtCount = 0;
    bool curHas0 = false, nxtHas0 = false;

    uint16_t bgBase() const { return (ctrl & 0x10) ? 0x1000 : 0; }

    uint8_t read(uint16_t a) {
        a &= 0x3FFF;
        if (a < 0x2000) return chrMem[a];
        if (a < 0x3F00) return vram[ntIndex(a)];
        int i = a & 0x1F;
        if ((i & 3) == 0 && i >= 16) i -= 16;
        return pal[i];
    }
    void write(uint16_t a, uint8_t v) {
        a &= 0x3FFF;
        if (a < 0x2000) { if (chrRam) chrMem[a] = v; return; }
        if (a < 0x3F00) { vram[ntIndex(a)] = v; return; }
        int i = a & 0x1F;
        if ((i & 3) == 0 && i >= 16) i -= 16;
        pal[i] = v & 0x3F;
    }
    int ntIndex(uint16_t a) const {
        int nt = (a >> 10) & 3;
        int page = verticalMirror ? (nt & 1) : (nt >> 1);
        return page * 0x400 + (a & 0x3FF);
    }

    void incX() {
        if ((vaddr & 0x1F) == 31) { vaddr &= ~0x1F; vaddr ^= 0x0400; } else vaddr++;
    }
    void incY() {
        if ((vaddr & 0x7000) != 0x7000) { vaddr += 0x1000; return; }
        vaddr &= ~0x7000;
        int y = (vaddr & 0x03E0) >> 5;
        if (y == 29) { y = 0; vaddr ^= 0x0800; } else if (y == 31) y = 0; else y++;
        vaddr = (vaddr & ~0x03E0) | (y << 5);
    }
    void loadShifters() {
        shLo = (shLo & 0xFF00) | lsb;
        shHi = (shHi & 0xFF00) | msb;
        atLo = (atLo & 0xFF00) | ((atByte & 1) ? 0xFF : 0);
        atHi = (atHi & 0xFF00) | ((atByte & 2) ? 0xFF : 0);
    }
    void updateShifters() {
        if (mask & 0x08) { shLo <<= 1; shHi <<= 1; atLo <<= 1; atHi <<= 1; }
        if ((mask & 0x10) && cycle >= 1 && cycle < 258) {
            for (int i = 0; i < curCount; i++) {
                if (cur[i].x > 0) cur[i].x--;
                else { cur[i].lo <<= 1; cur[i].hi <<= 1; }
            }
        }
    }

    static uint8_t rev(uint8_t b) {
        b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
        b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
        b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
        return b;
    }

    void evaluateSprites(bool rendering) {
        nxtCount = 0; nxtHas0 = false;
        if (!rendering || scanline < 0) return;
        int h = (ctrl & 0x20) ? 16 : 8;
        for (int n = 0; n < 64; n++) {
            int diff = scanline - oam[n * 4];
            if (diff < 0 || diff >= h) continue;
            if (nxtCount >= 8) { status |= 0x20; break; }
            uint8_t tile = oam[n * 4 + 1], attr = oam[n * 4 + 2];
            int row = (attr & 0x80) ? (h - 1 - diff) : diff;
            uint16_t addr;
            if (h == 8) addr = ((ctrl & 0x08) ? 0x1000 : 0) + tile * 16 + row;
            else {
                uint16_t table = (tile & 1) ? 0x1000 : 0;
                tile &= 0xFE;
                if (row >= 8) { tile++; row -= 8; }
                addr = table + tile * 16 + row;
            }
            uint8_t lo = read(addr), hi = read(addr + 8);
            if (attr & 0x40) { lo = rev(lo); hi = rev(hi); }
            nxt[nxtCount++] = {lo, hi, attr, oam[n * 4 + 3]};
            if (n == 0) nxtHas0 = true;
        }
    }

    void composePixel() {
        int x = cycle - 1;
        uint8_t bgPix = 0, bgPal = 0;
        if ((mask & 0x08) && (x >= 8 || (mask & 0x02))) {
            uint16_t bit = 0x8000 >> fineX;
            bgPix = ((shLo & bit) ? 1 : 0) | ((shHi & bit) ? 2 : 0);
            bgPal = ((atLo & bit) ? 1 : 0) | ((atHi & bit) ? 2 : 0);
        }
        uint8_t fgPix = 0, fgPal = 0; bool fgFront = false, is0 = false;
        if ((mask & 0x10) && (x >= 8 || (mask & 0x04))) {
            for (int i = 0; i < curCount; i++) {
                if (cur[i].x != 0) continue;
                uint8_t px = ((cur[i].hi & 0x80) ? 2 : 0) | ((cur[i].lo & 0x80) ? 1 : 0);
                if (!px) continue;
                fgPix = px; fgPal = (cur[i].attr & 3) + 4; fgFront = !(cur[i].attr & 0x20);
                is0 = (i == 0 && curHas0);
                break;
            }
        }
        uint8_t pix = 0, pl = 0;
        if (!bgPix && fgPix) { pix = fgPix; pl = fgPal; }
        else if (bgPix && !fgPix) { pix = bgPix; pl = bgPal; }
        else if (bgPix && fgPix) {
            if (fgFront) { pix = fgPix; pl = fgPal; } else { pix = bgPix; pl = bgPal; }
            if (is0 && (mask & 0x08) && (mask & 0x10) && x != 255) status |= 0x40;
        }
        uint8_t c = read(0x3F00 + (pl << 2) + pix) & 0x3F;
        if (mask & 0x01) c &= 0x30;
        fb[scanline * 256 + x] = nesColor(c);
    }
};

} // namespace nes
