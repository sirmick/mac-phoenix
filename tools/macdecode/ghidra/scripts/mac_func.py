# @runtime PyGhidra
# Disassemble and decompile the function at ADDR; print with "MD| ".
# Args: addr(hex) labels.json max_lines
import sys, os
sys.path.insert(0, os.path.dirname(getSourceFile().getAbsolutePath()))
from macdecode_common import load_labels, annotate_atraps
from ghidra.app.decompiler import DecompInterface

addr_s, labels_path, max_lines = getScriptArgs()
labels = load_labels(labels_path)
a = currentProgram.getAddressFactory().getDefaultAddressSpace().getAddress(int(addr_s, 16))
disassemble(a)
f = getFunctionContaining(a) or createFunction(a, None)
listing = currentProgram.getListing()
if f is None:
    print(f"MD| no function at {a}")
    instrs = []
    ins = getInstructionAt(a)
    while ins is not None and len(instrs) < int(max_lines):
        instrs.append(ins); ins = ins.getNext()
else:
    instrs = list(listing.getInstructions(f.getBody(), True))
annotate_atraps(setEOLComment, labels, instrs)
print(f"MD| function {f.getName() if f else '?'} @ {f.getEntryPoint() if f else a}  "
      f"({len(instrs)} instructions)")
for ins in instrs[:int(max_lines)]:
    ad = ins.getAddress()
    lab = getSymbolAt(ad)
    if lab is not None and not lab.getName().startswith("LAB_"):
        print(f"MD| {lab.getName()}:")
    cmt = listing.getComment(0, ad) or ""   # EOL
    raw = " ".join(f"{x & 0xFF:02x}" for x in ins.getBytes())
    print(f"MD|   {ad}  {raw:<20} {str(ins):<34} {('; ' + cmt) if cmt else ''}")
if f is not None:
    di = DecompInterface(); di.openProgram(currentProgram)
    res = di.decompileFunction(f, 60, monitor)
    if res.decompileCompleted():
        print("MD| ---- decompiled ----")
        for line in res.getDecompiledFunction().getC().splitlines():
            print("MD| " + line)
    else:
        print("MD| decompile failed: " + str(res.getErrorMessage()))
