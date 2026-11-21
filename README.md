# Hexen core for Karabas Go / Mini / Mini G

[Русская версия](README.ru.md)

**In development.** Hexen for the Karabas Go board family (Xilinx Spartan-6 XC6SLX25): a VexRiscv RISC-V soft CPU running the Hexen of [Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom) 2.3.0, on the gateware of the Doom and Heretic cores of the same family (frame buffer, palette, sound mixer, OPL3 FM music and the MIDI synth of the board in the FPGA).

Status: `src/hexen.kg` plays Hexen on the Mini G (checked on the board): 32 KB CPU caches at 79.5 MHz and a drawer of the view in the FPGA give about 35 frames per second in common scenes and 23 in the heaviest seen. Savegames (packed) and settings are kept in the save file on the SD card. The Go and Mini bitstreams have the same design (built, not yet tried on those boards). The plan, the measurements and the history (Heretic, Doom) are in `docs/PLAN.md` (Russian).

The core will contain no game data: the user puts an own `HEXEN.WAD` (or the 4-level demo) on the SD card.

## Licence

GPL-3.0 (`LICENSE`), as the other karabas-go cores. Imported parts keep their own licences: Chocolate Doom (and doomgeneric, the reference of the Doom port) are GPL-2.0-or-later, VexRiscv is MIT, opl3_fpga is LGPL-3.0.
