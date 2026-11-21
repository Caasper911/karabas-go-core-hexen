#!/usr/bin/env python3
# Ф0 cycle model over the iss per-frame counters of run_f0.sh (timedemo: one frame per game tic),
# calibrated on Verilator (Ф0.4, cal.py, VexRiscv 4K/4K, 3 segments of demo1):
#   cycles = BASE*instr + MISS*(I$ + D$ line fills)
# BASE 1.50 (CPI without memory stalls, fit 1.48-1.54), MISS 28 clocks per line fill with the SDRAM
# at the CPU clock (first word 8, next words 2: CL2 + 16-bit) and write-through stores (SRAM 6 /
# SDRAM 7 clocks; their cost hides behind the pipeline: a posted write buffer gains < 1 %).
# Bigger caches: same BASE / MISS (needs a regenerated VexRiscv to confirm).
# usage: model.py <out_dir of run_f0.sh> [skip frames]   (the frame hash, field 11+, is not used)
import glob, os, sys
OUT = sys.argv[1]
SKIP = int(sys.argv[2]) if len(sys.argv) > 2 else 5
BASE, MISS = 1.50, 28
def frames(fn):
    return [list(map(int, l.strip().split(',')[1:11])) for l in open(fn) if l.startswith('F,')][SKIP:]
MHZ = [50, 60, 70, 75, 80]
print(f"{'I$/D$':>10} {'Minstr/fr':>9} {'miss/kI':>8}  " + "".join(f"{m:>10} MHz" for m in MHZ) + "   (fps mean / p95)")
for fn in sorted(glob.glob(os.path.join(OUT, 'sweep_*.txt')), key=lambda f: [int(x) for x in os.path.basename(f)[6:-4].split('_')]):
    fr = frames(fn)
    i, d = os.path.basename(fn)[6:-4].split('_')
    per = sorted(BASE * f[1] + MISS * (f[2] + f[3] + f[4] + f[5]) for f in fr)
    mean, p95 = sum(per) / len(per), per[len(per) * 95 // 100]
    ins = sum(f[1] for f in fr) / len(fr); miss = sum(f[2] + f[3] + f[4] + f[5] for f in fr) / len(fr)
    print(f"{int(i)//1024:>4}K/{int(d)//1024:>3}K {ins/1e6:9.2f} {1000*miss/ins:8.1f}  " +
          "".join(f"{m*1e6/mean:8.1f}/{m*1e6/p95:4.1f}" for m in MHZ))
print("(Doom caps at 35 fps)")
