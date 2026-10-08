# @runtime PyGhidra
# Set up a macdecode snapshot program: ROM block, low-memory globals,
# trap-table entries as functions, block origins, A-line comments.
# Args: labels.json rom.bin rom_base(hex)
import sys, os
sys.path.insert(0, os.path.dirname(getSourceFile().getAbsolutePath()))
from java.io import FileInputStream
from java.io import File
from macdecode_common import load_labels, annotate_atraps

labels_path, rom_path, rom_base = getScriptArgs()
labels = load_labels(labels_path)
space = currentProgram.getAddressFactory().getDefaultAddressSpace()
A = lambda x: space.getAddress(x)
mem = currentProgram.getMemory()

base = int(rom_base, 16)
if mem.getBlock(A(base)) is None:
    size = File(rom_path).length()
    blk = createMemoryBlock("ROM", A(base), FileInputStream(rom_path), size, False)
    blk.setWrite(False); blk.setExecute(True)
ram = mem.getBlock(A(0))
if ram is not None and ram.getName() != "RAM":
    ram.setName("RAM")

for g in labels["lowmem"]:
    a = A(g["addr"])
    createLabel(a, g["name"], True)
    try:
        {1: createByte, 2: createWord, 4: createDWord}.get(g["size"], lambda x: None)(a)
    except Exception:
        pass

made = 0
for e in labels["trap_entries"]:
    a = A(e["addr"])
    try:
        disassemble(a)
        f = getFunctionAt(a) or createFunction(a, e["name"])
        if f is not None:
            made += 1
        createLabel(a, e["name"], False)
    except Exception:
        pass

for o in labels["origins"]:
    setPlateComment(A(o["start"]), o["label"])

n = annotate_atraps(setEOLComment, labels, currentProgram.getListing().getInstructions(True))
print(f"MD| setup: {len(labels['lowmem'])} globals, {made} trap entry functions, "
      f"{len(labels['origins'])} origins, {n} A-line traps annotated")
