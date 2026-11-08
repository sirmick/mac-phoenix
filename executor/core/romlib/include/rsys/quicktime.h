#pragma once
#include <cstdint>
#include <QuickDraw.h>
/* MacPhoenix: Executor's QuickTime (docs/executor/QuickTime.md, route B):
   its own Image Compression Manager and Movie Toolbox over Apple's
   components. Gestalt 'icmp' and 'qtim' are answered only once each is
   there; until then the Movie Toolbox's old stubs fail. */
namespace Executor
{
/* The Image Compression Manager ($AAA3): Gestalt 'icmp'. */
bool ROMlib_icm_present();
/* The Movie Toolbox ($AAAA): Gestalt 'qtim'. */
bool ROMlib_movie_toolbox_present();
/* An ICM selector by its declaration (icm.cpp): its arguments' and
   result's bytes; false if none is declared. */
bool ROMlib_icm_unknown_selector(uint32_t sel, uint32_t *arg_bytes, uint32_t *result_bytes, const char **name);
/* A private ICM selector (no header names it) Apple's codecs call and
   Executor answers: its arguments at guest address args (the last
   pushed first), its result; false if Executor doesn't answer it. */
bool ROMlib_icm_private_selector(uint32_t sel, uint32_t args, int32_t *result);
/* DrawPicture's opcodes $8200 CompressedQuickTime and $8201
   UncompressedQuickTime: their n bytes of data at guest data (after the
   length), drawn into the current port; src_frame and dst_frame map the
   picture's coordinates to the port's. */
void ROMlib_draw_quicktime_opcode(uint16_t opcode, Ptr data, int32_t n, const Rect *src_frame,
                                  const Rect *dst_frame);
}
