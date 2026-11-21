# Ядро Hexen для Karabas Go / Mini / Mini G

[English version](README.md)

**В разработке.** Hexen для плат семейства Karabas Go (Xilinx Spartan-6 XC6SLX25): софт-процессор VexRiscv (RISC-V) исполняет Hexen из [Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom) 2.3.0 на прошивке ПЛИС ядер Doom и Heretic этого семейства (буфер кадра, палитра, микшер звука, OPL3 и MIDI-синтезатор платы — в ПЛИС).

Состояние: пробная сборка играет демо Hexen в симуляторе примерно на 16 кадрах в секунду (оценка при 75 МГц); цель — 20. Файла ядра пока нет. План, замеры и история (Heretic, Doom) — в `docs/PLAN.md`.

Данных игры в ядре не будет: пользователь кладёт на SD-карту свой `HEXEN.WAD` (или демо на 4 уровня).

## Лицензия

GPL-3.0 (`LICENSE`), как у других ядер karabas-go. Взятые части сохраняют свои лицензии: Chocolate Doom (и doomgeneric — образец порта Doom) — GPL-2.0-or-later, VexRiscv — MIT, opl3_fpga — LGPL-3.0.
