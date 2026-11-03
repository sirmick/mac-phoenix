# Gestalt probe

`gprobe.c` asks Gestalt for every selector Gestalt.Appl 2.7.9 lists on the
Quadra (7.6.1, 111) or the G3 (8.6, 158), plus difftest sys's, and writes
`SEL ERR VALUE` lines to `Host:gestalt.txt`. Build with Retro68
(`cmake -S . -B b68k -DCMAKE_TOOLCHAIN_FILE=.../retro68.toolchain.cmake`,
or the retroppc one) and run with `tests/test_guest_suite.sh --app
GProbe.bin --results gestalt.txt` on the ROM machines; on Executor launch
it from the Host volume.

`ref/`: the answers measured 2026-11-03 on the Quadra 650 ROM machine
(7.6.1, 68k build) and mame-ppc's G3 (7.6.1 and 8.6, PowerPC build).
Machine-specific values (RAM, ROM, table addresses, bug-fix bits) differ
by run; the identity (mach, proc, cput, sysa, fpu, mmu, addr, pclk, bclk)
is what Executor matches (romlib/gestalt.cpp).
