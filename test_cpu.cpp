#include "Cpu.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
struct B : nes::CpuBus {
    uint8_t ram[0x10000] = {};
    uint8_t read(uint16_t a) override { return ram[a]; }
    void write(uint16_t a, uint8_t v) override { ram[a] = v; }
};
int main() {
    B bus; std::ifstream f("./nestest.nes", std::ios::binary);
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), {});
    memcpy(&bus.ram[0x8000], &d[16], 0x4000); memcpy(&bus.ram[0xC000], &d[16], 0x4000);
    nes::Cpu c; c.bus = &bus; c.pc = 0xC000; c.sp = 0xFD; c.p = 0x24; c.cycles = 7;
    std::ifstream lg("./nestest.log"); std::string line; int n = 0;
    while (std::getline(lg, line) && n < 5003) {
        unsigned pc, a, x, y, p, sp; unsigned long long cyc;
        sscanf(line.c_str(), "%x", &pc);
        const char* q = strstr(line.c_str(), "A:"); sscanf(q, "A:%x X:%x Y:%x P:%x SP:%x", &a, &x, &y, &p, &sp);
        q = strstr(line.c_str(), "CYC:"); sscanf(q, "CYC:%llu", &cyc);
        if (c.pc != pc || c.a != a || c.x != x || c.y != y || c.p != p || c.sp != sp || c.cycles != cyc) {
            printf("MISMATCH line %d\n  log: %s\n  got: PC:%04X A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu\n", n + 1, line.c_str(), c.pc, c.a, c.x, c.y, c.p, c.sp, (unsigned long long)c.cycles);
            return 1;
        }
        c.step(); n++;
    }
    printf("OK: %d official-opcode lines match (regs + flags + cycles). result bytes $02=%02X $03=%02X\n", n, bus.ram[2], bus.ram[3]);
}
