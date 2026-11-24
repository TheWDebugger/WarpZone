#include "Apu.hpp"
#include <cstdio>
#include <cstdlib>
static int risingEdges(std::vector<int16_t> const& s) { int n = 0; for (size_t i = 1; i < s.size(); i++) if (s[i-1] <= 0 && s[i] > 0) n++; return n; }
int main() {
    int fails = 0;
    { // pulse: 440 Hz => period = 1789773/(16*440)-1 = 253
        nes::Apu a; a.reset(); a.write(0x4015, 1); a.write(0x4000, 0xBF); a.write(0x4002, 253); a.write(0x4003, 0);
        a.run(1789773); auto& s = a.samples(); int e = risingEdges(s);
        printf("pulse: %zu samples, %d cycles/s (want ~440)\n", s.size(), e); if (abs(e - 440) > 3) fails++;
    }
    { // triangle: 220 Hz => period = 1789773/(32*220)-1 = 253
        nes::Apu a; a.reset(); a.write(0x4015, 4); a.write(0x4008, 0xFF); a.write(0x400A, 253); a.write(0x400B, 0);
        a.run(1789773); int e = risingEdges(a.samples());
        printf("triangle: %d cycles/s (want ~220)\n", e); if (abs(e - 220) > 3) fails++;
    }
    { // noise produces output; length counter silences it
        nes::Apu a; a.reset(); a.write(0x4015, 8); a.write(0x400C, 0x3F); a.write(0x400E, 5); a.write(0x400F, 0);
        a.run(200000); int peak = 0; for (auto v : a.samples()) peak = std::max(peak, abs((int)v));
        printf("noise peak %d (want >0)\n", peak); if (!peak) fails++;
        a.write(0x4015, 0); printf("status after disable: %d (want 0)\n", a.readStatus()); if (a.readStatus()) fails++;
    }
    { // length counter expires when not halted: idx 1 => 254 half-frame clocks (~ 1 s)
        nes::Apu a; a.reset(); a.write(0x4015, 1); a.write(0x4000, 0x9F); a.write(0x4002, 253); a.write(0x4003, 0x08); // len idx 1 = 254
        printf("len set: status=%d (want 1)\n", a.readStatus()); if (a.readStatus() != 1) fails++;
        a.run(1789773 * 3); printf("after 3s: status=%d (want 0)\n", a.readStatus()); if (a.readStatus() != 0) fails++;
    }
    printf(fails ? "FAIL\n" : "ALL OK\n"); return fails;
}
