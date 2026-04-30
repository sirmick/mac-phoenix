/*
 *  serial_ppc.cpp - Serial device driver for PPC/KPX
 *
 *  Ported from SheepShaver serial.cpp + serial_unix.cpp
 *  Original: (C) 1997-2008 Marc Hellwig and Christian Bauer
 *
 *  The key function of this driver is proper IOCommandIsComplete signaling.
 *  Without it, synchronous Device Manager calls (e.g. from .Infra driver)
 *  spin-wait forever on ioResult=1.
 */

#include "sysdeps.h"
#include "main.h"
#include "macos_util.h"
#include "serial.h"          // kpx/compat/serial.h: SERDPort + extern ::the_serd_port[2] + serdt enum
#include "serial_defs.h"

#define DEBUG 0
#include "debug.h"

namespace ppc {

// SerialInit / SerialExit are NOT provided here — they live at global
// scope (serial_adapter.cpp → g_platform.serial_init → serial_unix_init)
// and serve both PPC and m68k init paths.

// IOCommandIsComplete function pointer (resolved at first SerialOpen)
typedef int16 (*iocic_ptr)(uint32, int16);
static uint32 iocic_tvect = 0;
static inline int16 IOCommandIsComplete(uint32 arg1, int16 arg2)
{
	return (int16)CallMacOS2(iocic_ptr, iocic_tvect, arg1, arg2);
}


// ============================================================================
// Driver Open/Close (from legacy SheepShaver serial.cpp)
// ============================================================================

int16 SerialNothing(uint32 pb, uint32 dce)
{
	return noErr;
}

int16 SerialOpen(uint32 pb, uint32 dce)
{
	D(bug("SerialOpen pb %08lx, dce %08lx\n", pb, dce));

	// Get IOCommandIsComplete function (once)
	if (iocic_tvect == 0) {
		iocic_tvect = FindLibSymbol("\021DriverServicesLib", "\023IOCommandIsComplete");
		D(bug("IOCommandIsComplete TVECT at %08lx\n", iocic_tvect));
		if (iocic_tvect == 0) {
			fprintf(stderr, "WARNING: SerialOpen(): Can't find IOCommandIsComplete()\n");
			return openErr;
		}
	}

	// Do nothing if port is already open
	SERDPort *the_port = the_serd_port[(-(int16)ReadMacInt16(dce + dCtlRefNum)-6) >> 1];
	if (the_port->is_open)
		return noErr;

	// Init variables
	the_port->read_pending = the_port->write_pending = false;
	the_port->read_done = the_port->write_done = false;
	the_port->cum_errors = 0;

	// Open port
	int16 res = the_port->open(ReadMacInt16(0x1fc + ((-(int16)ReadMacInt16(dce + dCtlRefNum)-6) & 2)));
	if (res)
		return res;

	// Allocate Deferred Task structures
	if ((the_port->dt_store = Mac_sysalloc(SIZEOF_serdt * 2)) == 0)
		return openErr;
	uint32 input_dt = the_port->input_dt = the_port->dt_store;
	uint32 output_dt = the_port->output_dt = the_port->dt_store + SIZEOF_serdt;
	D(bug(" input_dt %08lx, output_dt %08lx\n", input_dt, output_dt));

	WriteMacInt16(input_dt + qType, dtQType);
	WriteMacInt32(input_dt + dtAddr, input_dt + serdtCode);
	WriteMacInt32(input_dt + dtParam, input_dt + serdtResult);
	// Deferred function: signal Prime complete (a1 -> mydtResult)
	WriteMacInt16(input_dt + serdtCode, 0x2019);		// move.l	(a1)+,d0	(result)
	WriteMacInt16(input_dt + serdtCode + 2, 0x2251);	// move.l	(a1),a1		(dce)
	WriteMacInt32(input_dt + serdtCode + 4, 0x207808fc);	// move.l	JIODone,a0
	WriteMacInt16(input_dt + serdtCode + 8, 0x4ed0);	// jmp		(a0)

	WriteMacInt16(output_dt + qType, dtQType);
	WriteMacInt32(output_dt + dtAddr, output_dt + serdtCode);
	WriteMacInt32(output_dt + dtParam, output_dt + serdtResult);
	WriteMacInt16(output_dt + serdtCode, 0x2019);
	WriteMacInt16(output_dt + serdtCode + 2, 0x2251);
	WriteMacInt32(output_dt + serdtCode + 4, 0x207808fc);
	WriteMacInt16(output_dt + serdtCode + 8, 0x4ed0);

	the_port->is_open = true;
	return noErr;
}


// ============================================================================
// Driver Prime (read/write) — with IOCommandIsComplete
// ============================================================================

int16 SerialPrimeIn(uint32 pb, uint32 dce)
{
	D(bug("SerialPrimeIn pb %08lx, dce %08lx\n", pb, dce));
	int16 res;

	SERDPort *the_port = the_serd_port[(-(int16)ReadMacInt16(dce + dCtlRefNum)-6) >> 1];
	if (!the_port->is_open)
		res = notOpenErr;
	else {
		if (the_port->read_pending) {
			printf("FATAL: SerialPrimeIn() called while request is pending\n");
			res = readErr;
		} else
			res = the_port->prime_in(pb, dce);
	}

	if (ReadMacInt16(pb + ioTrap) & 0x0200)
		if (res > 0) {
			WriteMacInt16(pb + ioResult, 0);
			return 0;	// Command in progress
		} else {
			WriteMacInt16(pb + ioResult, res);
			return res;
		}
	else
		if (res > 0)
			return 0;					// Command in progress
		else {
			IOCommandIsComplete(pb, res);
			return res;
		}
}

int16 SerialPrimeOut(uint32 pb, uint32 dce)
{
	D(bug("SerialPrimeOut pb %08lx, dce %08lx\n", pb, dce));
	int16 res;

	SERDPort *the_port = the_serd_port[(-(int16)ReadMacInt16(dce + dCtlRefNum)-6) >> 1];
	if (!the_port->is_open)
		res = notOpenErr;
	else {
		if (the_port->write_pending) {
			printf("FATAL: SerialPrimeOut() called while request is pending\n");
			res = writErr;
		} else
			res = the_port->prime_out(pb, dce);
	}

	if (ReadMacInt16(pb + ioTrap) & 0x0200)
		if (res > 0) {
			WriteMacInt16(pb + ioResult, 0);
			return 0;	// Command in progress
		} else {
			WriteMacInt16(pb + ioResult, res);
			return res;
		}
	else
		if (res > 0)
			return 0;					// Command in progress
		else {
			IOCommandIsComplete(pb, res);
			return res;
		}
}


// ============================================================================
// Driver Control / Status / Close — with IOCommandIsComplete
// ============================================================================

int16 SerialControl(uint32 pb, uint32 dce)
{
	uint16 code = ReadMacInt16(pb + csCode);
	D(bug("SerialControl %d, pb %08lx, dce %08lx\n", code, pb, dce));
	int16 res;

	SERDPort *the_port = the_serd_port[(-(int16)ReadMacInt16(dce + dCtlRefNum)-6) >> 1];
	if (!the_port->is_open)
		res = notOpenErr;
	else {
		switch (code) {
			case kSERDSetPollWrite:
				res = noErr;
				break;
			default:
				res = the_port->control(pb, dce, code);
				break;
		}
	}

	if (code == 1)
		return res;
	else if (ReadMacInt16(pb + ioTrap) & 0x0200) {
		WriteMacInt16(pb + ioResult, res);
		return res;
	} else {
		IOCommandIsComplete(pb, res);
		return res;
	}
}

int16 SerialStatus(uint32 pb, uint32 dce)
{
	uint16 code = ReadMacInt16(pb + csCode);
	D(bug("SerialStatus %d, pb %08lx, dce %08lx\n", code, pb, dce));
	int16 res;

	SERDPort *the_port = the_serd_port[(-(int16)ReadMacInt16(dce + dCtlRefNum)-6) >> 1];
	if (!the_port->is_open)
		res = notOpenErr;
	else {
		switch (code) {
			case kSERDVersion:
				WriteMacInt8(pb + csParam, 9);		// Second-generation SerialDMA driver
				res = noErr;
				break;

			case 0x8000:
				WriteMacInt8(pb + csParam, 9);
				WriteMacInt16(pb + csParam + 4, 0x1997);
				WriteMacInt16(pb + csParam + 6, 0x0616);
				res = noErr;
				break;

			default:
				res = the_port->status(pb, dce, code);
				break;
		}
	}

	if (ReadMacInt16(pb + ioTrap) & 0x0200) {
		WriteMacInt16(pb + ioResult, res);
		return res;
	} else {
		IOCommandIsComplete(pb, res);
		return res;
	}
}

int16 SerialClose(uint32 pb, uint32 dce)
{
	D(bug("SerialClose pb %08lx, dce %08lx\n", pb, dce));

	SERDPort *the_port = the_serd_port[(-(int16)ReadMacInt16(dce + dCtlRefNum)-6) >> 1];
	if (the_port->is_open) {
		Mac_sysfree(the_port->dt_store);
		int16 res = the_port->close();
		the_port->is_open = false;
		return res;
	} else
		return noErr;
}


// ============================================================================
// Serial interrupt — activate deferred tasks for completed I/O
// ============================================================================

void SerialInterrupt(void)
{
	D(bug("SerialIRQ\n"));

	// Port 0
	if (the_serd_port[0] && the_serd_port[0]->is_open) {
		if (the_serd_port[0]->read_pending && the_serd_port[0]->read_done) {
			Enqueue(the_serd_port[0]->input_dt, 0xd92);
			the_serd_port[0]->read_pending = the_serd_port[0]->read_done = false;
		}
		if (the_serd_port[0]->write_pending && the_serd_port[0]->write_done) {
			Enqueue(the_serd_port[0]->output_dt, 0xd92);
			the_serd_port[0]->write_pending = the_serd_port[0]->write_done = false;
		}
	}

	// Port 1
	if (the_serd_port[1] && the_serd_port[1]->is_open) {
		if (the_serd_port[1]->read_pending && the_serd_port[1]->read_done) {
			Enqueue(the_serd_port[1]->input_dt, 0xd92);
			the_serd_port[1]->read_pending = the_serd_port[1]->read_done = false;
		}
		if (the_serd_port[1]->write_pending && the_serd_port[1]->write_done) {
			Enqueue(the_serd_port[1]->output_dt, 0xd92);
			the_serd_port[1]->write_pending = the_serd_port[1]->write_done = false;
		}
	}
}

} // namespace ppc
