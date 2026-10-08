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

/* MacPhoenix: multiversal has no FIDParam; its fields as Inside Macintosh
   lays them out after the I/O header. */
OSErr Executor::PBExchangeFiles(ParmBlkPtr pb, Boolean async)
{
    OSErr retval;
    char *p = (char *)pb;
    FSSpec src, dst;

    StringPtr src_name = pb->ioParam.ioNamePtr;
    StringPtr dst_name = *(GUEST<StringPtr> *)(p + 28);   /* ioDestNamePtr */
    LONGINT dst_dir = *(GUEST<LONGINT> *)(p + 36);        /* ioDestDirID */
    LONGINT src_dir = *(GUEST<LONGINT> *)(p + 48);        /* ioSrcDirID */

    retval = FSMakeFSSpec(pb->ioParam.ioVRefNum, src_dir, src_name, &src);
    if(retval == noErr)
        retval = FSMakeFSSpec(pb->ioParam.ioVRefNum, dst_dir, dst_name, &dst);
    if(retval == noErr)
        retval = FSpExchangeFiles(&src, &dst);
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
