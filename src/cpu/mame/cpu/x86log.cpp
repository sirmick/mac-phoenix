// license:BSD-3-Clause
// copyright-holders:Aaron Giles
/***************************************************************************

    x86log.c

    x86/x64 code logging helpers.

***************************************************************************/

#include "emu.h"
#include "x86log.h"

/* MacPhoenix: no i386 disassembler lifted; disasm_code_range() dumps bytes. */

#include <sstream>


/* (x86_buf / x86_config removed with the disassembler: MacPhoenix) */

/*-------------------------------------------------
    x86log_context::create- create a new context
-------------------------------------------------*/

x86log_context::ptr x86log_context::create(std::string_view filename)
{
	try
	{
		// allocate the log
		auto log = std::make_unique<x86log_context>();
		log->data_range.reserve(MAX_DATA_RANGES);
		log->comment_list.reserve(MAX_COMMENTS);

		// allocate the filename
		log->filename = filename;

		// reset things
		log->reset_log();
		return log;
	}
	catch (std::bad_alloc const &)
	{
		return nullptr;
	}
}


/*-------------------------------------------------
    ~x86log_context - release a context
-------------------------------------------------*/

x86log_context::~x86log_context()
{
	// close any open files
	if (file)
		std::fclose(file);
}


/*-------------------------------------------------
    x86log_context::mark_as_data - mark a given
    range as data for logging purposes
-------------------------------------------------*/

void x86log_context::mark_as_data(x86code const *base, x86code const *end, int size) noexcept
{
	assert(end >= base);
	assert((size == 1) || (size == 2) || (size == 4) || (size == 8));

	// we assume data ranges are registered in order; enforce this
	assert(data_range.empty() || (base > data_range.back().end));

	// fill in the new range
	try
	{
		data_range.emplace_back(data_range_t{ base, end, size });
	}
	catch (std::bad_alloc const &)
	{
	}
}


/*-------------------------------------------------
    x86log_context::disasm_code_range - disassemble
    a range of code and reset accumulated
    information
-------------------------------------------------*/

void x86log_context::disasm_code_range(const char *label, x86code const *start, x86code const *stop)
{
	auto const lastcomment = comment_list.cend();
	auto curcomment = comment_list.cbegin();
	auto const lastdata = data_range.cend();
	auto curdata = data_range.cbegin();
	x86code const *cur = start;

	// print the optional label
	if (label)
		printf("\n%s\n", label);

	// MacPhoenix: no disassembler; print data ranges as data and everything
	// else as bytes, with the comments where they were attached.
	std::stringstream strbuffer;
	while (cur < stop)
	{
		strbuffer.str("");
		int bytes;

		while ((curdata != lastdata) && (cur > curdata->end))
			++curdata;
		while ((curcomment != lastcomment) && (cur > curcomment->base))
			++curcomment;

		if ((curdata != lastdata) && (cur >= curdata->base) && (cur <= curdata->end))
		{
			bytes = curdata->size;
			switch (curdata->size)
			{
				default:
				case 1: util::stream_format(strbuffer, "db      %02X", *cur); break;
				case 2: util::stream_format(strbuffer, "dw      %04X", *reinterpret_cast<uint16_t const *>(cur)); break;
				case 4: util::stream_format(strbuffer, "dd      %08X", *reinterpret_cast<uint32_t const *>(cur)); break;
				case 8: util::stream_format(strbuffer, "dq      %016X", *reinterpret_cast<uint64_t const *>(cur)); break;
			}
		}
		else if (*cur == 0xcc)
		{
			cur++;
			continue;
		}
		else
		{
			bytes = 1;
			util::stream_format(strbuffer, "%02X", *cur);
		}

		if ((curcomment != lastcomment) && (cur == curcomment->base))
		{
			for ( ; ((curcomment + 1) != lastcomment) && (cur == curcomment[1].base); curcomment++)
				printf("%p: %-50s; %s\n", cur, "", curcomment->string);
			printf("%p: %-50s; %s\n", cur, std::move(strbuffer).str().c_str(), curcomment->string);
		}
		else
		{
			printf("%p: %s\n", cur, std::move(strbuffer).str().c_str());
		}
		cur += bytes;
	}

	// reset our state
	reset_log();
}



/*-------------------------------------------------
    reset_log - reset the state of the log
-------------------------------------------------*/

void x86log_context::reset_log() noexcept
{
	data_range.clear();
	comment_list.clear();
	comment_pool_next = comment_pool;
}
