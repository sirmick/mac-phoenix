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

#include <string>

struct M68kRegisters;

// Parent side: create <storage>/snapshots/<name>/ and record it as the
// pending request. Returns the directory, or "" on failure.
std::string snapshot_prepare(const std::string& storage_dir, const std::string& name);

// Child side: flag a pending request (control IPC thread).
void snapshot_request();

// Child side: service a pending request (emulator thread, IRQ EmulOp).
void snapshot_service_from_irq(M68kRegisters* r);

#endif // SNAPSHOT_H
