#include <Displays.h>
#include <error/error.h>

Executor::OSErr Executor::C_DMRegisterNotifyProc(DMNotificationUPP proc, ProcessSerialNumber *psn)
{
    warning_unimplemented("ignoring DMRegisterNotifyProc - no notifications will happen.");
    return noErr;
}
