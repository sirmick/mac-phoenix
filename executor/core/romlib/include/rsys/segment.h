#if !defined(_SEGMENT_H_)
#define _SEGMENT_H_


#include <SegmentLdr.h>

namespace Executor
{
extern bool ROMlib_exit;

struct finderinfo
{
    GUEST_STRUCT;
    GUEST<INTEGER> message;
    GUEST<INTEGER> count;
    GUEST<AppFile[1]> files;
};

extern void InitAppFiles(const std::vector<std::string>&);
extern void empty_timer_queues();

extern void C_LoadSeg(INTEGER segno);

static_assert(sizeof(finderinfo) == 268);
}
#endif /* !_SEGMENT_H_ */
