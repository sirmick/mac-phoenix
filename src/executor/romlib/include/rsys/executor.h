#if !defined(__RSYS_EXECUTOR__)
#define __RSYS_EXECUTOR__

/*
 * Copyright 1995 by Abacus Research and Development, Inc.
 * All rights reserved.
 *
 */

#include <ExMacTypes.h>

#define BROWSER_NAME "Browser"
namespace Executor
{
extern LONGINT ROMlib_creator;
extern void executor_main();
extern void InitLowMem();
extern void InitPerProcessLowMem();
}
#endif
