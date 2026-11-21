# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

A Heretic core for the Karabas Go, Mini and Mini G boards (Xilinx Spartan-6 XC6SLX25, ISE 14.7, XST), cloned on
2026-11-21 from karabas-go-core-doom (history kept; private GitHub repo Caasper911/karabas-go-core-heretic). The same SoC: a soft CPU (VexRiscv,
MIT); video, the SFX mixer, OPL3 FM and the MIDI path to the SAM2695 are RTL. The game: `src/heretic` of
Chocolate Doom 2.3.0 (`upstream/chocolate-doom`, GPL-2+) with its common `src/` files; doomgeneric (Doom only) stays
as the reference of the Doom port until Heretic runs. Docs, in Russian: `docs/PLAN.md` (Heretic phases on top,
the Doom history below). Follow the phases; record a changed decision in PLAN.md.

No WAD files in git or in the .kg: the user loads an IWAD from SD1 (OSD file item). Test WADs stay local: the
shareware `~/Downloads/heretic-sw/HERETIC1.WAD` (v1.2, md5 ae779722390ec32fa37b0d361f7d82f8).

## Rules of the core

The rules of the other karabas-go cores apply: standard video timing with syncs always present, `FT_CLK_OUT` about 8 MHz on Mini / Mini G, Go `FDC_*` outputs 0 when unused, Mini / Mini G drive T3 (`FLASH_CS_N`) = 0, OSD switch index = item position unless `"index"`, an OSD value shows 10 characters at most, FL slots 0..7 only, Verilog-2001 for XST, imported code marked `karabas:`.

- **Ask the user before every check on the board** (and the HDMI capture). Ask before a firmware flash.
- One branch per phase of `docs/PLAN.md`, one PR at its end.
- Heavy builds: Mini G only while working on one feature, all boards for a release; map with cost table 2 first.
- Tests and sims run in seconds; longer runs only after asking.

## Memory

The Claude Code memory of this repo is shared with all karabas-go repos: `~/.claude/projects/<this project>/memory` is a symlink to `../karabas-go-notes` (private repo `andykarpov/karabas-go-notes`). Its `MEMORY.md` is split by repo; commit and push it after every memory change.
