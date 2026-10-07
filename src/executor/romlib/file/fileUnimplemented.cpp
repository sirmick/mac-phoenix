#include <FileMgr.h>
#include <error/error.h>
#include <file/file.h>

using namespace Executor;

OSErr Executor::PBHGetLogInInfo(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHGetDirAccess(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHCopyFile(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHMapName(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHMapID(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHSetDirAccess(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBHMoveRename(HParmBlkPtr pb, Boolean a)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::PBExchangeFiles(ParmBlkPtr pb, Boolean async)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    FAKEASYNC(pb, async, retval);
}

OSErr Executor::PBCatSearch(ParmBlkPtr pb, Boolean async)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    FAKEASYNC(pb, async, retval);
}

OSErr Executor::PBCreateFileIDRef(ParmBlkPtr pb, Boolean async)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    FAKEASYNC(pb, async, retval);
}

OSErr Executor::PBDeleteFileIDRef(ParmBlkPtr pb, Boolean async)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    FAKEASYNC(pb, async, retval);
}

OSErr Executor::PBResolveFileIDRef(ParmBlkPtr pb, Boolean async)
{
    OSErr retval;

    warning_unimplemented("");
    retval = paramErr;
    FAKEASYNC(pb, async, retval);
}
