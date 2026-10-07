/*
 * Copyright 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *

 */

#include <FontMgr.h>

namespace Executor
{
struct fatabentry
{
    GUEST_STRUCT;
    GUEST<INTEGER> size;
    GUEST<INTEGER> style;
    GUEST<INTEGER> fontresid;
};

struct widentry_t
{
    GUEST_STRUCT;
    GUEST<unsigned short> style;
    GUEST<INTEGER[1]> table; /* actually more */
};

typedef WidthTable *WPtr;

typedef GUEST<WPtr> *WHandle;

typedef FamRec *FPtr;

typedef GUEST<FPtr> *FHandle;

typedef GUEST<WHandle> *WHandlePtr;

typedef GUEST<WHandlePtr> *WHandleHandle;

#define FONTRESID(font, size) (((font) << 7) | (size))

#define WIDTHPTR ((WPtr)LM(WidthPtr))
#define WIDTHTABHANDLE ((WHandle)LM(WidthTabHandle))
#define WIDTHLISTHAND ((WHandleHandle)LM(WidthListHand))

extern Fixed font_width_expand(Fixed width, Fixed extra,
                               Fixed hOutputInverse);

extern void ROMlib_shutdown_font_manager(void);

static_assert(sizeof(fatabentry) == 6);
static_assert(sizeof(widentry_t) == 4);
}
