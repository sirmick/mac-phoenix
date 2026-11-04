/* vminfo: GetVolumeVirtualMemoryInfo (MacMemory.h, InterfaceLib 8.6),
   differential (tools/difftest/run.py vminfo): what it says of the boot
   volume, of a volume that isn't there, for record versions 0, 1 and 2.
   Every 9.0.4 session calls it at startup. */
#include "probe.h"
#include <Files.h>

typedef struct
{
    unsigned long version;
    short volumeRefNum;
    unsigned char inUse;
    unsigned char fill;
    unsigned long vmOptions;
} VMInfo;

typedef pascal OSErr (*GetVMInfo)(VMInfo *);

static void ask(GetVMInfo f, const char *key, unsigned long version, short vref)
{
    VMInfo info;
    OSErr err;
    char k[64];
    memset(&info, SENTINEL, sizeof info);
    info.version = version;
    info.volumeRefNum = vref;
    err = f(&info);
    sprintf(k, "%s.err", key);
    out(k, "%d", err);
    sprintf(k, "%s.record", key);
    out_hex(k, &info, sizeof info);
}

int main(void)
{
    GetVMInfo f;
    short boot = 0;
    long dir;

    probe_open("vminfo");
    f = (GetVMInfo)probe_bind("InterfaceLib", "GetVolumeVirtualMemoryInfo");
    if(f && SECTION("ask"))
    {
        FindFolder(kOnSystemDisk, kSystemFolderType, false, &boot, &dir);
        ask(f, "boot.v1", 1, boot);
        ask(f, "boot.v0", 0, boot);
        ask(f, "boot.v2", 2, boot);
        ask(f, "none.v1", 1, -99);
        ask(f, "zero.v1", 1, 0);
    }
    probe_close();
    return 0;
}
