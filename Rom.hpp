#pragma once
#include <Geode/Geode.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <filesystem>
#include <vector>

namespace smb {

inline uint32_t crc32(uint8_t const* d, size_t n) {
    uint32_t c = 0xFFFFFFFF;
    for (size_t i = 0; i < n; i++) {
        c ^= d[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & (0 - (c & 1)));
    }
    return ~c;
}

// Parsed, validated SMB1 ROM (iNES, mapper 0 / NROM-256, 8KB CHR).
struct Rom {
    std::vector<uint8_t> prg; // 32 KB
    std::vector<uint8_t> chr; // 8 KB
    bool verticalMirror = true;

    // CRC32 of the PRG and CHR sections of the original Super Mario Bros. (World) dump.
    // VERIFY these against your own dump (e.g. with a No-Intro DAT) before shipping.
    static constexpr uint32_t PRG_CRC = 0x5CF548D3;
    static constexpr uint32_t CHR_CRC = 0x867B51AD;

    static geode::Result<Rom> load(std::filesystem::path const& path, bool strict = true) {
        auto res = geode::utils::file::readBinary(path);
        if (!res) return geode::Err("Could not read the file.");
        auto const& b = res.unwrap();

        if (b.size() < 16 || b[0] != 'N' || b[1] != 'E' || b[2] != 'S' || b[3] != 0x1A)
            return geode::Err("Not an iNES (.nes) file.");

        size_t prgBanks = b[4], chrBanks = b[5];
        int mapper = (b[6] >> 4) | (b[7] & 0xF0);
        size_t trainer = (b[6] & 0x04) ? 512 : 0;
        if (prgBanks != 2 || chrBanks != 1 || mapper != 0)
            return geode::Err("This is not a Super Mario Bros. 1 ROM (expected NROM-256 with 8KB CHR).");

        size_t prgOff = 16 + trainer, chrOff = prgOff + 0x8000;
        if (b.size() < chrOff + 0x2000) return geode::Err("ROM file is truncated.");

        Rom rom;
        rom.prg.assign(b.begin() + prgOff, b.begin() + prgOff + 0x8000);
        rom.chr.assign(b.begin() + chrOff, b.begin() + chrOff + 0x2000);

        rom.verticalMirror = (b[6] & 1) != 0;

        if (strict && (crc32(rom.prg.data(), rom.prg.size()) != PRG_CRC ||
                       crc32(rom.chr.data(), rom.chr.size()) != CHR_CRC))
            return geode::Err("ROM checksum mismatch. This does not look like an unmodified SMB1 dump.");

        return geode::Ok(std::move(rom));
    }
};

// The loaded ROM lives here for the session. Empty means the game cannot be played.
inline std::optional<Rom>& romStore() {
    static std::optional<Rom> s;
    return s;
}

struct LoadResult {
    std::optional<Rom> rom;
    std::string error;
};

// Convenience wrapper used by main.cpp.
inline LoadResult loadRom(std::string const& path, bool strict = true) {
    auto res = Rom::load(path, strict);
    if (!res) return {std::nullopt, std::string(res.unwrapErr())};
    return {std::move(res).unwrap(), {}};
}

} // namespace smb
