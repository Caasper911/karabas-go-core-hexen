# Hexen core for Karabas Go / Mini / Mini G

[Русская версия](README.ru.md)

**In development.** Hexen for the Karabas Go board family (Xilinx Spartan-6 XC6SLX25): a VexRiscv RISC-V soft CPU running the Hexen of [Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom) 2.3.0, on the gateware of the Doom and Heretic cores of the same family (frame buffer, palette, sound mixer, OPL3 FM music and the MIDI synth of the board in the FPGA).

Status: a survey build runs the Hexen demo in the simulator at about 16 frames per second (estimated at 75 MHz); the goal is 20. No core file yet. The plan, the measurements and the history (Heretic, Doom) are in `docs/PLAN.md` (Russian).

The core will contain no game data: the user puts an own `HEXEN.WAD` (or the 4-level demo) on the SD card.

## Licence

GPL-3.0 (`LICENSE`), as the other karabas-go cores. Imported parts keep their own licences: Chocolate Doom (and doomgeneric, the reference of the Doom port) are GPL-2.0-or-later, VexRiscv is MIT, opl3_fpga is LGPL-3.0.
