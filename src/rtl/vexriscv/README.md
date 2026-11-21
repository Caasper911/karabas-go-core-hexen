Pre-generated VexRiscv (SpinalHDL/VexRiscv, MIT) from litex-hub/pythondata-cpu-vexriscv 642ecfe.
Generator options per file: see that repo verilog/Makefile.
- VexRiscv_Min: rv32i, no caches, no bypass
- VexRiscv_Lite: rv32im (iterative mul/div), I$ 2K, no D$
- VexRiscv: rv32im (single-cycle mul), **I$ 16K, D$ 16K** (direct mapped, 32-byte lines, D$ write-through)
- VexRiscv_IMAC: VexRiscv + C + A, full CSR
IO range = address bit 31 (uncached).

VexRiscv.v of the Hexen core (Ф0б, 2026-11-21) is the litex default variant with bigger caches, generated in
pythondata-cpu-vexriscv 642ecfe (submodule ext/VexRiscv b6118e5, SpinalHDL 1.9.4) with sbt 1.9.7 + Java 8:

    cd pythondata_cpu_vexriscv/verilog
    sbt -batch "runMain vexriscv.GenCoreDefault --iCacheSize 16384 --dCacheSize 16384 --outputFile VexRiscv_16K"

(Windows: --outputFile takes a plain name, the files land in the current dir.) The same command without the
cache options gives the former 4K / 4K VexRiscv.v byte for byte (only the "Git hash" comment differs). The
ports are the same; only the tag / index widths change.
