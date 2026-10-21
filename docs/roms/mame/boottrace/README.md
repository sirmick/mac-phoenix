# Boot traces of the real ROMs in MAME

Which ROM routines run, in what order, with which hardware accesses, from
reset to the System 6.0.8 Finder, for the Mac II family and the IIci.

- `mactrace-m68kcpu.patch`: a tracer in MAME's 68k core
  (`src/devices/cpu/m68000/m68kcpu.cpp`), off unless `MACTRACE=<file>` is
  set. It logs the first execution of every PC (with the previous PC,
  instruction count and cycles), every A-trap, bus errors, and dumps low
  memory ($0-$2FFF, logical) at the first execution of the PCs in
  `MACTRACE_SNAP`.
- `boottrace.lua`: MAME autoboot script. Taps I/O space (`$50xxxxxx`) and
  slot space (`$Fsxxxxxx`, `$s0000000`), logs accesses with the PC, and quits
  8 s after `CurApName` becomes `Finder`.
- `run_trace.sh <machine> [mame args]`: one run, into
  `~/storage/tmp/boottrace/<machine>/`.
- `analyze.py`: names everything with Apple's labels (listing + map) and
  writes `routines.tsv`, `flow.txt`, `ramexec.tsv`, `traps.tsv`,
  `hwprobes.tsv`, `hwunique.tsv`, `slots.txt`, `lowmem-*.txt`.
- `hwdiff.py A B`: hardware accesses one machine makes and the other
  doesn't, or with other values.

```bash
# MAME with the tracer: macii.cpp + maciici.cpp in one binary (-j4!)
cd docs/roms/mame/src && git apply ../boottrace/mactrace-m68kcpu.patch
make SUBTARGET=mactrace SOURCES=src/mame/apple/macii.cpp,src/mame/apple/maciici.cpp -j4
ln -sf ../src/mactrace ../bin/mactrace
# ROMs (CRCs match MAME): bin/roms/macii/{9779d2c4,97851db6}.rom,
# bin/roms/mac2fdhd/97221136.rom, bin/roms/macse30/se30vrom.uk6,
# bin/roms/nb_mdc824/3410868.bin, plus maciici/ and adbmodem/ as before.

# Boot floppy: System Tools from the Legacy CD's 6.0.8 800K set, raw 800K
# (provisioning/containers.py: take + open_container), at
# ~/storage/tmp/boottrace/media/System_Tools.img

T=docs/roms/mame/boottrace
SNAP=408004b6=1-bootglobs,40806d80=2-initadb,40800d9e=3-initiomgr,40800e96=4-bootme,40801036=5-bootblocks,408010be=6-first-sysINIT,408011a6=7-launch-finder
for m in macii maciihmu mac2fdhd maciix maciicx macse30; do
    BT_TRAPS=1000000 BT_SNAP=$SNAP $T/run_trace.sh $m
done
BT_SLOTALL=1 BT_TRAPS=1000000 BT_SNAP=$SNAP BT_NAME=macii-revA $T/run_trace.sh macii -bios original
BT_TRAPS=1000000 BT_SNAP=40809e56=1-bootglobs,408010f0=2-initiomgr,4080a802=3-initadb,40801d10=4-bootme,40801ea4=5-bootblocks,40801f52=6-first-sysINIT,40802036=7-launch-finder \
    $T/run_trace.sh maciici

D=docs/roms/dissam; M=$D/tools/vendor/68k-mac-rom-maps/rom-maps
python3 $T/analyze.py ~/storage/tmp/boottrace/macii --lst $D/macii/rom.lst --map $M/MacIIROM.lst.txt
python3 $T/analyze.py ~/storage/tmp/boottrace/maciix --lst $D/maciix/rom.lst --map $M/MacIIROM.lst.txt
python3 $T/analyze.py ~/storage/tmp/boottrace/maciici --lst $D/iici/rom.lst --map $M/MacIIciROM.lst.txt --romsize 0x80000
```

Each run takes 12-21 s of wall time (MAME unthrottled, about 3.5x real
time) and writes about 20 MB. Runs are deterministic: the same instruction
counts every time.
