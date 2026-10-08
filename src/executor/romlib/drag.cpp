/* MacPhoenix: Drag Manager entry points Executor needs before it has a
 * Drag Manager. Private selectors were identified from a traced 7.5.5 boot
 * and Apple's code (tools/macdecode); names are best guesses. */

#include <base/common.h>
#include <DragMgr.h>

using namespace Executor;

/* Finder's hooks (two routines in a 7-long record), registered at start-up
   and removed at quit. Kept for when Executor grows drag support. */
static Ptr finder_drag_hooks;

OSErr Executor::C_SetFinderDragHooks(Ptr hooks, Boolean install)
{
    finder_drag_hooks = install ? hooks : nullptr;
    return noErr;
}
