/*
 * snapshot.h - Guest memory snapshots for reverse engineering.
 *
 * Writes RAM, ROM and a small JSON description of a running guest to
 * <storage>/snapshots/<name>/ so tools/memmap can walk the trap tables,
 * heaps and resource chain offline. Reference material for the Executor
 * System reconstruction (docs/executor/MEMORY_MAP.md).
 *
 * Parent: snapshot_request_path() names the directory and writes the
 * request; the IPC child services it from the 60Hz IRQ so the dump is
 * taken on the emulator thread at an instruction boundary.
 */

#ifndef SNAPSHOT_H
#define SNAPSHOT_H

#include <cstdint>
#include <string>

struct M68kRegisters;

// Parent side: create <storage>/snapshots/<name>/ and record it as the
// pending request. Returns the directory, or "" on failure.
std::string snapshot_prepare(const std::string& storage_dir, const std::string& name);

// Child side: flag a pending request (control IPC thread).
void snapshot_request();

// Child side: is a request waiting?
bool snapshot_pending();

// What to dump. ram/rom are host pointers to the guest's RAM/ROM; ram may
// be address 0 (Executor maps guest RAM there).
struct SnapshotMemory {
    const uint8_t* ram = nullptr;
    uint32_t ram_base = 0, ram_size = 0;
    const uint8_t* rom = nullptr;
    uint32_t rom_base = 0, rom_size = 0;
    const char* context = "";
    const M68kRegisters* regs = nullptr;   // optional
};

// Child side: write the pending snapshot (emulator thread, at a safe point).
void snapshot_service(const SnapshotMemory& mem);

// Child side, ROM backends: service a pending request from the IRQ EmulOp.
void snapshot_service_from_irq(M68kRegisters* r);

// A-trap trace (--trace-atraps, UAE). Every A-line trap is recorded; each
// distinct (trap word, caller PC, enclosing trap) is kept with a hit count,
// first-seen order, interrupt mask, CurApName and the code bytes around the
// call, so a caller can be matched to its resource even after its segment
// moved or was purged. The enclosing trap comes from a shadow stack keyed
// on A7. Snapshots write it as atraps.tsv.
void atrap_trace_enable();

#endif // SNAPSHOT_H
