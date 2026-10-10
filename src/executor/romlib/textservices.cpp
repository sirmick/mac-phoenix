/* MacPhoenix: the Text Services Manager's trap ($AA54), as far as Apple's
 * Help Manager needs it (TextServices.yaml). */
#include <base/common.h>
#include <rsys/process.h>
#include <TextServices.h>
#include <WindowMgr.h>
#include <ProcessMgr.h>

using namespace Executor;

INTEGER Executor::C_TSMFindWindow(Point pt, GUEST<WindowPtr> *window)
{
    (void)pt;
    *window = nullptr;
    return 0;
}

/* OSDispatch $64 (ProcessMgr.yaml): the same through the Process Manager. */
INTEGER Executor::C_FindInputWindow(Point pt, GUEST<WindowPtr> *window)
{
    return C_TSMFindWindow(pt, window);
}

OSErr Executor::C_TSMInputWindowOp(Ptr record, INTEGER op)
{
    (void)record, (void)op;
    return paramErr;
}
