# Ядро Hexen для Karabas Go / Mini / Mini G

[English version](README.md)

**В разработке.** Hexen для плат семейства Karabas Go (Xilinx Spartan-6 XC6SLX25): софт-процессор VexRiscv (RISC-V) исполняет Hexen из [Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom) 2.3.0 на прошивке ПЛИС ядер Doom и Heretic этого семейства (буфер кадра, палитра, микшер звука, OPL3 и MIDI-синтезатор платы — в ПЛИС).

Состояние: `src/hexen.kg` играет в Hexen на Mini G (проверено на плате): кэши процессора по 32 КБ на 79,5 МГц и рисователь вида в ПЛИС дают около 35 кадров в секунду в обычных сценах и 23 в самых тяжёлых из виденных. Сохранения игры (сжатые) и настройки хранятся в файле сохранений на SD-карте. Битстримы Go и Mini в файле ядра пока прежние, медленнее. План, замеры и история (Heretic, Doom) — в `docs/PLAN.md`.

Данных игры в ядре не будет: пользователь кладёт на SD-карту свой `HEXEN.WAD` (или демо на 4 уровня).

## Лицензия

GPL-3.0 (`LICENSE`), как у других ядер karabas-go. Взятые части сохраняют свои лицензии: Chocolate Doom (и doomgeneric — образец порта Doom) — GPL-2.0-or-later, VexRiscv — MIT, opl3_fpga — LGPL-3.0.
