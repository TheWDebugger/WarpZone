# WarpZone (Geode mod for Geometry Dash)

Runs the **real Super Mario Bros.** inside Geometry Dash using a built-in NES emulator.
The mod ships **no Nintendo code or assets**. On first launch it asks for your own dumped
`.nes` file, validates it (iNES header, NROM-256, CRC32 of PRG and CHR), remembers the
path, and then executes the ROM itself. Without a valid ROM the game will not start.

## Requirements

Built against **Geode SDK 5.11.2 for Geometry Dash 2.2081**. You need GD 2.2081 and Geode 5.11.2
(or newer 5.x) installed. The mod will not load on other game versions.

## Build

Push this folder to GitHub and run the **Build Mod** workflow (Actions tab), or install the
Geode SDK, set `GEODE_SDK`, and run `geode build`. Targets: Windows and macOS (keyboard only).

## Controls

| NES | Keys |
|-----|------|
| D-pad | Arrows / WASD |
| A (jump) | X or Space |
| B (run / fire) | Z or Shift |
| Start | Enter |
| Select | Tab or Backspace |

F5 resets the console. Esc exits.

## Architecture

- `src/nes/Cpu.hpp`  6502 core (official opcodes), cycle-counted
- `src/nes/Ppu.hpp`  2C02 PPU: loopy scroll registers, background pipeline, sprites, sprite-0 hit, NMI
- `src/nes/Nes.hpp`  console: RAM, mapper 0 (NROM), OAM DMA, controller 1
- `src/Rom.hpp`      ROM loading + validation
- `src/EmuLayer.hpp` GD scene: runs frames at 60.0988 Hz, uploads the framebuffer as a texture
- `src/main.cpp`     main-menu button, ROM picker, launch gate

The emulator core has no Geode dependency, so it can be built and tested standalone.

## Testing status (what was and wasn't verified)

Verified in a standalone build (g++), not inside GD:
- CPU: matches the `nestest` reference log for all 5,003 official-opcode lines (registers, flags, cycle counts). `tests/test_cpu.cpp` reproduces this.
- PPU: passes blargg's `sprite_hit_tests` 01-08, 10, 11 (sprite-0 hit basics, alignment, corners, flip, clipping, edge, timing), plus `palette_ram`, `sprite_ram` and `vram_access`. Sprite-0 hit is what SMB1 uses for its status-bar scroll split.
- Fails `vbl_clear_time` (code $03): the PPU is ticked per CPU instruction, not per cycle, so cycle-exact vblank-clear timing is off. SMB1 doesn't depend on this.

NOT verified:
- **No actual SMB1 ROM was run** (none was available), so full-game behaviour is untested.
- The Geode glue (`main.cpp`, `EmuLayer.hpp`) has never been compiled. Expect small API fixes on the first build (e.g. `keyDown/keyUp(enumKeyCodes, double)` signatures, file-picker types).
- The CRC32 constants in `Rom.hpp` are from memory. If your valid dump is rejected, enable the "Skip ROM checksum" setting and tell me the real values.

## Not implemented

- **Audio (APU).** The game runs silent. Audio output through GD's FMOD is the next step.
- Touch controls (so no Android/iOS), mappers other than NROM, save states.
- Cycle-exact PPU/CPU interleaving (see above).
- Don't distribute ROMs with this mod.
