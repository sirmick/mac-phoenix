/*
 *  macos_util_ppc.cpp - MacOS utility functions for PPC
 *
 *  Lifted from SheepShaver's macos_util.cpp.
 *  Provides FindLibSymbol, InitCallUniversalProc, Mac_sysalloc/sysfree.
 *
 *  SheepShaver (C) Christian Bauer and Marc Hellwig
 *  Licensed under GPL v2+
 */

#include <ctime>
#include <cstdio>

#include "sysdeps.h"
#include "kpx_cpu_emulation.h"
#include "main.h"
#include "sony.h"
#include "disk.h"
#include "cdrom.h"
#include "xlowmem.h"
#include "emul_op.h"
#include "macos_util.h"
#include "thunks.h"

#define DEBUG 0
#include "debug.h"


// Function pointers (resolved at runtime via FindLibSymbol)
typedef long (*cu_ptr)(void *, uint32);
static uint32 cu_tvect = 0;
static inline long CallUniversal(void *arg1, uint32 arg2)
{
	return (long)CallMacOS2(cu_ptr, cu_tvect, arg1, arg2);
}
typedef int16 (*gsl_ptr)(char *, uint32, uint32, uint32 *, void **, char *);
static uint32 gsl_tvect = 0;
static inline int16 GetSharedLibrary(uintptr arg1, uint32 arg2, uint32 arg3, uintptr arg4, uintptr arg5, uintptr arg6)
{
	return (int16)CallMacOS6(gsl_ptr, gsl_tvect, (char *)arg1, arg2, arg3, (uint32 *)arg4, (void **)arg5, (char *)arg6);
}
typedef int16 (*fs_ptr)(uint32, char *, void **, uint32 *);
static uint32 fs_tvect = 0;
static inline int16 FindSymbol(uint32 arg1, uintptr arg2, uintptr arg3, uintptr arg4)
{
	return (int16)CallMacOS4(fs_ptr, fs_tvect, arg1, (char *)arg2, (void **)arg3, (uint32 **)arg4);
}
typedef uint32 (*nps_ptr)(uint32);
static uint32 nps_tvect = 0;
static inline uint32 NewPtrSys(uint32 arg1)
{
	return CallMacOS1(nps_ptr, nps_tvect, arg1);
}
typedef void (*d_ptr)(uint32);
static uint32 d_tvect = 0;
static inline void DisposePtr(uint32 arg1)
{
	CallMacOS1(d_ptr, d_tvect, arg1);
}


/*
 *  Reset MacOS utilities
 */

void MacOSUtilReset(void)
{
	cu_tvect = 0;
	gsl_tvect = 0;
	fs_tvect = 0;
	nps_tvect = 0;
	d_tvect = 0;
}


/*
 *  Enqueue QElem to list
 */

void Enqueue(uint32 elem, uint32 list)
{
	WriteMacInt32(elem + qLink, 0);
	if (!ReadMacInt32(list + qTail)) {
		WriteMacInt32(list + qHead, elem);
		WriteMacInt32(list + qTail, elem);
	} else {
		WriteMacInt32(ReadMacInt32(list + qTail) + qLink, elem);
		WriteMacInt32(list + qTail, elem);
	}
}

static void InsertQueueEntry(uint32 elem, uint32 at, uint32 list) {
	uint32 next = ReadMacInt32(at);
	WriteMacInt32(at, elem);
	WriteMacInt32(elem + qLink, next);
	if (next == 0) {
		// inserted at end
		WriteMacInt32(list + qTail, elem);
	}
}

static void RemoveQueueEntry(uint32 at, uint32 list) {
	uint32 e = ReadMacInt32(at);
	uint32 next = ReadMacInt32(e + qLink);

	if (next == 0) {
		// removing from end
		if (at == list + qHead) {
			WriteMacInt32(list + qTail, 0);
		} else {
			WriteMacInt32(list + qTail, at - qLink);
		}
	}

	WriteMacInt32(at, next);
	WriteMacInt32(e + qLink, 0);
}

// FindFreeDriveNumber: provided by src/core/macos_util.cpp (libcore)

/*
 *  Move drives of the given driver to the front of the drive queue
 */

void MoveDrivesFromDriverToFront(uint32 driverRefNum) {

	const uint32 DrvQHdr = 0x308; // drive queue address

	uint32 nextInsertPos = DrvQHdr + qHead;

	uint32 ptrToElem = DrvQHdr + qHead;
	uint32 e = ReadMacInt32(ptrToElem);
	while (e) {
		uint32 next = ReadMacInt32(e + qLink);

		uint32 d = e - dsQLink;
		uint32 curRefNum = ReadMacInt16(d + dsQRefNum);

		if ((curRefNum & 0xffff) == (driverRefNum & 0xffff)) {
			RemoveQueueEntry(ptrToElem, DrvQHdr);
			InsertQueueEntry(e, nextInsertPos, DrvQHdr);

			nextInsertPos = e + qLink;

			// after the removal, ptrToElem already points to next
		} else {
			ptrToElem = e + qLink;
		}

		e = next;
	}
}

// MountVolume and FileDiskLayout: provided by src/core/macos_util.cpp (libcore)

/*
 *  Allocate/release memory in MacOS system heap
 */

uint32 Mac_sysalloc(uint32 size)
{
	return NewPtrSys(size);
}

void Mac_sysfree(uint32 addr)
{
	DisposePtr(addr);
}


/*
 *  Find symbol in shared library (using CFM)
 *  lib and sym must be Pascal strings!
 */

uint32 FindLibSymbol(const char *lib_str, const char *sym_str)
{
	SheepVar32 conn_id = 0;
	SheepVar32 main_addr = 0;
	SheepArray<256> err;
	WriteMacInt8(err.addr(), 0);
	SheepVar32 sym_addr = 0;
	SheepVar32 sym_class = 0;

	SheepString lib(lib_str);
	SheepString sym(sym_str);

	D(bug("FindLibSymbol %s in %s...\n", sym.value()+1, lib.value()+1));

	if (ReadMacInt32(XLM_RUN_MODE) == MODE_EMUL_OP) {
		M68kRegisters r;

		// Find shared library (uses CFMDispatch trap 0xAA5A)
		static const uint8 proc1_template[] = {
			0x55, 0x8f,							// subq.l	#2,a7
			0x2f, 0x08,							// move.l	a0,-(a7)
			0x2f, 0x3c, 0x70, 0x77, 0x70, 0x63,	// move.l	#'pwpc',-(a7)
			0x2f, 0x3c, 0x00, 0x00, 0x00, 0x01,	// move.l	#kReferenceCFrag,-(a7)
			0x2f, 0x09,							// move.l	a1,-(a7)
			0x2f, 0x0a,							// move.l	a2,-(a7)
			0x2f, 0x0b,							// move.l	a3,-(a7)
			0x3f, 0x3c, 0x00, 0x01,				// (GetSharedLibrary)
			0xaa, 0x5a,							// CFMDispatch
			0x30, 0x1f,							// move.w	(a7)+,d0
			M68K_RTS >> 8, M68K_RTS & 0xff
		};
		BUILD_SHEEPSHAVER_PROCEDURE(proc1);
		r.a[0] = lib.addr();
		r.a[1] = conn_id.addr();
		r.a[2] = main_addr.addr();
		r.a[3] = err.addr();
		Execute68k(proc1, &r);
		D(bug(" GetSharedLibrary: ret %d, connection ID %ld, main %p\n", (int16)r.d[0], conn_id.value(), main_addr.value()));
		if (r.d[0])
			return 0;

		// Find symbol (uses CFMDispatch trap 0xAA5A)
		static const uint8 proc2_template[] = {
			0x55, 0x8f,					// subq.l	#2,a7
			0x2f, 0x00,					// move.l	d0,-(a7)
			0x2f, 0x08,					// move.l	a0,-(a7)
			0x2f, 0x09,					// move.l	a1,-(a7)
			0x2f, 0x0a,					// move.l	a2,-(a7)
			0x3f, 0x3c, 0x00, 0x05,		// (FindSymbol)
			0xaa, 0x5a,					// CFMDispatch
			0x30, 0x1f,					// move.w	(a7)+,d0
			M68K_RTS >> 8, M68K_RTS & 0xff
		};
		BUILD_SHEEPSHAVER_PROCEDURE(proc2);
		r.d[0] = conn_id.value();
		r.a[0] = sym.addr();
		r.a[1] = sym_addr.addr();
		r.a[2] = sym_class.addr();
		Execute68k(proc2, &r);
		D(bug(" FindSymbol: ret %d, sym_addr %p, sym_class %ld\n", (int16)r.d[0], sym_addr.value(), sym_class.value()));
		if (r.d[0])
			return 0;
		else
			return sym_addr.value();

	} else {

		if (gsl_tvect == 0 || fs_tvect == 0) {
			printf("FATAL: FindLibSymbol() called too early\n");
			return 0;
		}
		int16 res;
		res = GetSharedLibrary(lib.addr(), FOURCC('p','w','p','c'), 1, conn_id.addr(), main_addr.addr(), err.addr());
		D(bug(" GetSharedLibrary: ret %d, connection ID %ld, main %p\n", res, conn_id.value(), main_addr.value()));
		if (res)
			return 0;
		res = FindSymbol(conn_id.value(), sym.addr(), sym_addr.addr(), sym_class.addr());
		D(bug(" FindSymbol: ret %d, sym_addr %p, sym_class %ld\n", res, sym_addr.value(), sym_class.value()));
		if (res)
			return 0;
		else
			return sym_addr.value();

	}
}


/*
 *  Find CallUniversalProc() TVector
 */

void InitCallUniversalProc()
{
	cu_tvect = FindLibSymbol("\014InterfaceLib", "\021CallUniversalProc");
	if (cu_tvect == 0) { fprintf(stderr, "FATAL: Can't find CallUniversalProc()\n"); return; }

	gsl_tvect = FindLibSymbol("\014InterfaceLib", "\020GetSharedLibrary");
	if (gsl_tvect == 0) { fprintf(stderr, "FATAL: Can't find GetSharedLibrary()\n"); return; }

	fs_tvect = FindLibSymbol("\014InterfaceLib", "\012FindSymbol");
	if (fs_tvect == 0) { fprintf(stderr, "FATAL: Can't find FindSymbol()\n"); return; }

	nps_tvect = FindLibSymbol("\014InterfaceLib", "\011NewPtrSys");
	if (nps_tvect == 0) { fprintf(stderr, "FATAL: Can't find NewPtrSys()\n"); return; }

	d_tvect = FindLibSymbol("\014InterfaceLib", "\012DisposePtr");
	if (d_tvect == 0) { fprintf(stderr, "FATAL: Can't find DisposePtr()\n"); return; }
}


/*
 *  Timer functions
 */

uint32 TimerDateTime(void)
{
	return (uint32)time(NULL) + 0x7C25B080;
}
