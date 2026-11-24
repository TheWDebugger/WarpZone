#pragma once
// Headless emulator self-test. No Geode dependency, so it can be compiled standalone.
// Boots the ROM, presses Start, holds Right, and checks that the picture and the game
// state react the way SMB1 should.
#include "nes/Nes.hpp"
#include <chrono>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace smb {

struct SelfTestLine {
    bool ok;
    std::string text;
};

struct SelfTestResult {
    std::vector<SelfTestLine> lines;
    bool passed = true;
    void add(bool ok, std::string text) {
        if (!ok) passed = false;
        lines.push_back({ok, std::move(text)});
    }
};

namespace detail {
inline uint64_t hashFrame(uint32_t const* fb) {
    uint64_t h = 1469598103934665603ull; // FNV-1a
    for (int i = 0; i < 256 * 240; i++) { h ^= fb[i]; h *= 1099511628211ull; }
    return h;
}
inline size_t distinctColors(uint32_t const* fb) {
    std::set<uint32_t> s;
    for (int i = 0; i < 256 * 240 && s.size() < 64; i++) s.insert(fb[i]);
    return s.size();
}
inline void runFrames(nes::Nes& n, int frames, uint8_t buttons) {
    n.setButtons(buttons);
    for (int i = 0; i < frames; i++) n.runFrame();
}
} // namespace detail

// SMB1 RAM addresses (from the public disassembly). Only meaningful for an unmodified dump.
constexpr uint16_t SMB_OPER_MODE = 0x0770;  // 0 = title screen, 1 = game
constexpr uint16_t SMB_PLAYER_X  = 0x0086;  // player X position within the screen page

inline SelfTestResult runEmulatorSelfTest(std::vector<uint8_t> const& prg,
                                          std::vector<uint8_t> const& chr,
                                          bool verticalMirror) {
    using namespace detail;
    SelfTestResult r;
    auto const t0 = std::chrono::steady_clock::now();

    // 1. ROM sanity
    if (prg.size() != 0x8000 || chr.size() != 0x2000) {
        r.add(false, "ROM sections have the wrong size.");
        return r;
    }
    uint16_t resetVec = prg[0x7FFC] | (prg[0x7FFD] << 8);
    r.add(resetVec >= 0x8000, "Reset vector $" + [&] { char b[8]; snprintf(b, sizeof b, "%04X", resetVec); return std::string(b); }() +
                                  (resetVec >= 0x8000 ? " points into PRG ROM" : " is outside PRG ROM"));
    if (resetVec < 0x8000) return r;

    nes::Nes n(prg, chr, verticalMirror);

    // 2. Boot to the title screen (no input)
    runFrames(n, 120, 0);
    uint64_t titleHash = hashFrame(n.framebuffer());
    size_t colors = distinctColors(n.framebuffer());
    r.add(n.cpu.pc >= 0x8000, "CPU is still executing ROM code after 120 frames");
    r.add(colors >= 3, "Title screen draws " + std::to_string(colors) + " distinct colors");

    // 3. Title screen must be animating-or-stable but then change when Start is pressed
    bool title = n.read(SMB_OPER_MODE) == 0;
    r.add(title, "Game is in title-screen mode ($0770 = 0)");

    runFrames(n, 4, nes::BTN_START);
    runFrames(n, 60, 0);
    uint64_t afterStart = hashFrame(n.framebuffer());
    r.add(afterStart != titleHash, "Pressing Start changes the picture");
    r.add(n.read(SMB_OPER_MODE) == 1, "Game mode switched to gameplay ($0770 = 1)");

    // 4. Let the "WORLD 1-1" intro finish, then run right
    runFrames(n, 240, 0);
    uint8_t x0 = n.read(SMB_PLAYER_X);
    uint64_t beforeRun = hashFrame(n.framebuffer());
    runFrames(n, 180, nes::BTN_RIGHT | nes::BTN_B);
    uint8_t x1 = n.read(SMB_PLAYER_X);
    r.add(x1 != x0, "Holding Right moves Mario (X $" + [&] { char b[16]; snprintf(b, sizeof b, "%02X -> %02X", x0, x1); return std::string(b); }() + ")");
    r.add(hashFrame(n.framebuffer()) != beforeRun, "Picture scrolls/changes while running");
    r.add(distinctColors(n.framebuffer()) >= 3, "Gameplay frame is not blank");

    // 5. Speed
    int totalFrames = 120 + 4 + 60 + 240 + 180;
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    double fps = secs > 0 ? totalFrames / secs : 0;
    char buf[96];
    snprintf(buf, sizeof buf, "Speed: %.0f frames/s (%.1fx realtime, needs 60.1)", fps, fps / 60.0988);
    r.add(fps >= 60.1, buf);
    return r;
}

} // namespace smb
