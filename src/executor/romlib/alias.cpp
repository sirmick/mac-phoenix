/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#include <base/common.h>

#include <stdarg.h>

#include <FileMgr.h>
#include <AliasMgr.h>
#include <ResourceMgr.h>
#include <MemoryMgr.h>
#include <ToolboxUtil.h>

#include <file/file.h>
#include <hfs/hfs.h>
#include <util/string.h>

#include <rsys/alias.h>

#include <algorithm>

using namespace Executor;

/* NOTE: if we want to be more like the Mac, we should have a 'fld#',0
   resource that will have in it: type, four bytes of 0, pascal string,
   potential padding to even things up, type, four bytes of 0, ... */

static const char *
find_sub_dir(OSType folderType)
{
    typedef struct
    {
        OSType type;
        const char *name;
    } sys_sub_dir_match_t;

    static sys_sub_dir_match_t matches[] = {
        {
            kPrintMonitorDocsFolderType, "PrintMonitor Documents",
        },
        {
            kStartupFolderType, "Startup Items",
        },
        {
            kAppleMenuFolderType, "Apple Menu Items",
        },
        {
            kExtensionFolderType, "Extensions",
        },
        {
            kPreferencesFolderType, "Preferences",
        },
        {
            kControlPanelFolderType, "Control Panels",
        },
        {
            kFontFolderType, "Fonts",
        },
        /* MacPhoenix: the folders Finder Scripting Extension adds through
           AliasDispatch $A (FindFolderWithTable, below). */
        {
            "shdf"_4, "Shutdown Items",
        },
        {
            "timf"_4, "Timer Items",
        },
    };
    int i;
    const char *retval;

    for(i = 0; i < (int)std::size(matches) && matches[i].type != folderType; ++i)
        ;
    if(i < (int)std::size(matches))
        retval = matches[i].name;
    else
        retval = 0;
    return retval;
}

static OSErr
get_sys_vref_and_dirid(INTEGER *sys_vrefp, LONGINT *sys_diridp)
{
    OSErr err;
    WDPBRec wdp;

    wdp.ioVRefNum = LM(BootDrive);
    wdp.ioWDIndex = 0;
    wdp.ioNamePtr = nullptr;
    err = PBGetWDInfo(&wdp, false);
    if(err == noErr)
    {
        *sys_vrefp = wdp.ioWDVRefNum;
        *sys_diridp = wdp.ioWDDirID;
    }
    return err;
}

static OSErr
test_directory(INTEGER vref, LONGINT dirid, const char *sub_dirp,
               LONGINT *new_idp)
{
    OSErr err;
    CInfoPBRec cpb;
    Str255 file_name;

    str255_from_c_string(file_name, sub_dirp);
    cpb.hFileInfo.ioNamePtr = (StringPtr)file_name;
    cpb.hFileInfo.ioVRefNum = vref;
    cpb.hFileInfo.ioFDirIndex = 0;
    cpb.hFileInfo.ioDirID = dirid;
    err = PBGetCatInfo(&cpb, false);
    if(err == noErr && !(cpb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR))
        err = dupFNErr;
    if(err == noErr)
        *new_idp = cpb.dirInfo.ioDrDirID;
    return err;
}


static OSErr
create_directory(INTEGER sys_vref, LONGINT sys_dirid, const char *sub_dirp,
                 LONGINT *new_idp)
{
    /* MacPhoenix: FindFolder with kCreateFolder (Finder creates the
       Extensions folder at start-up this way). */
    HParamBlockRec pb;
    Str255 name;

    str255_from_c_string(name, sub_dirp);
    memset(&pb, 0, sizeof pb);
    pb.fileParam.ioNamePtr = (StringPtr)name;
    pb.fileParam.ioVRefNum = sys_vref;
    pb.fileParam.ioDirID = sys_dirid;
    OSErr err = PBDirCreate(&pb, false);
    if(err == noErr)
        *new_idp = pb.fileParam.ioDirID;
    return err;
}

/* The folders at a volume's root, named as in Apple's Folder Manager
   (System 7.5.5 'lpch' 31); 'empt' is "Network Trash Folder" only on
   shared volumes. */
enum { kOnSystemDisk = -32768 };

static OSErr
find_volume_folder(int16_t vRefNum, OSType folderType, Boolean createFolder,
                   GUEST<int16_t> *foundVRefNum, GUEST<int32_t> *foundDirID)
{
    const char *name = folderType == kDesktopFolderType ? "Desktop Folder"
                     : folderType == kTemporaryFolderType ? "Temporary Items"
                     : "Trash";

    HParamBlockRec vpb;
    memset(&vpb, 0, sizeof vpb);
    vpb.volumeParam.ioVRefNum = vRefNum == kOnSystemDisk ? (int16_t)LM(BootDrive)
                                                          : vRefNum;
    OSErr err = PBHGetVInfo(&vpb, false);
    if(err != noErr)
        return err;
    INTEGER vref = vpb.volumeParam.ioVRefNum;

    LONGINT dirid;
    err = test_directory(vref, 2, name, &dirid);
    if(err == fnfErr && createFolder)
    {
        err = create_directory(vref, 2, name, &dirid);
        if(err == noErr)
        {
            CInfoPBRec cpb;
            Str255 pname;
            str255_from_c_string(pname, name);
            memset(&cpb, 0, sizeof cpb);
            cpb.dirInfo.ioNamePtr = pname;
            cpb.dirInfo.ioVRefNum = vref;
            cpb.dirInfo.ioDrDirID = 2;
            if(PBGetCatInfo(&cpb, false) == noErr)
            {
                cpb.dirInfo.ioDrUsrWds.frFlags = cpb.dirInfo.ioDrUsrWds.frFlags | fInvisible;
                cpb.dirInfo.ioDrDirID = 2;
                PBSetCatInfo(&cpb, false);
            }
        }
    }
    if(err == noErr)
    {
        *foundVRefNum = vref;
        *foundDirID = dirid;
    }
    return err;
}

/* MacPhoenix: AliasDispatch $A (name a guess, learned.yaml). FindFolder
   with one more argument: a list of folder descriptions to add, {type.l,
   name length.l, name} entries. Finder Scripting Extension's FindFolder
   patch calls it for 'shdf' (Shutdown Items) and 'timf' (Timer Items); on
   7.5.5 it is never reached by a trap instruction (patches jump to it), so
   no trace shows it. The two names are built in above; the list is not
   read. */
OSErr Executor::C_FindFolderWithTable(int16_t vRefNum, OSType folderType,
                                      Boolean createFolder,
                                      GUEST<int16_t> *foundVRefNum,
                                      GUEST<int32_t> *foundDirID, Ptr table)
{
    (void)table;
    return C_FindFolder(vRefNum, folderType, createFolder, foundVRefNum, foundDirID);
}

OSErr Executor::C_FindFolder(int16_t vRefNum, OSType folderType,
                             Boolean createFolder,
                             GUEST<int16_t> *foundVRefNum,
                             GUEST<int32_t> *foundDirID)
{
    OSErr retval;
    const char *sub_dir;

    sub_dir = find_sub_dir(folderType);
    if(sub_dir)
    {
        INTEGER sys_vref;
        LONGINT sys_dirid, new_id;

        retval = get_sys_vref_and_dirid(&sys_vref, &sys_dirid);
        if(retval == noErr)
        {
            retval = test_directory(sys_vref, sys_dirid, sub_dir, &new_id);
            if(retval == fnfErr && createFolder)
                retval = create_directory(sys_vref, sys_dirid, sub_dir, &new_id);
            if(retval == noErr)
            {
                *foundVRefNum = sys_vref;
                *foundDirID = new_id;
            }
        }
    }
    else
        switch(folderType)
        {
            case kSystemFolderType:
            {
                INTEGER sys_vref;
                LONGINT sys_dirid;

                retval = get_sys_vref_and_dirid(&sys_vref, &sys_dirid);
                if(retval == noErr)
                {
                    /* NOTE: IMVI 9-44 tells us to not create System Folder if it
	       doesn't already exist */
                    *foundVRefNum = sys_vref;
                    *foundDirID = sys_dirid;
                }
            }
            break;
            case kDesktopFolderType:
            case kTrashFolderType:
            case kWhereToEmptyTrashFolderType:
            case kTemporaryFolderType:
                retval = find_volume_folder(vRefNum, folderType, createFolder,
                                            foundVRefNum, foundDirID);
                break;
            default:
                warning_unexpected("unknown folderType");
                retval = fnfErr;
                break;
        }
    return retval;
}

OSErr Executor::C_NewAlias(FSSpecPtr fromFile, FSSpecPtr target,
                           GUEST<AliasHandle> *alias)
{
    OSErr retval;

    *alias = 0;
    warning_unimplemented("poorly implemented");

    retval = NewAliasMinimal(target, alias);
    return retval;
}

OSErr Executor::C_UpdateAlias(FSSpecPtr fromFile, FSSpecPtr target,
                              AliasHandle alias, Boolean *wasChanged)
{
    warning_unimplemented("");
    return paramErr;
}

enum
{
    FULL_PATH_TAG = 0x0002,
    TAIL_TAG = 0x0009,
};

/* MacPhoenix: resolve an alias record as the Alias Manager does, by
 * the means the record carries, in this order:
 *
 *   1. relative to fromFile: up nlvlFrom levels from the alias file to the
 *      folder both share, then down the last nlvlTo names of the path;
 *   2. the volume by name, then the full path (tag 2);
 *   3. the volume, then the parent directory ID and the target's name.
 *
 * The relative path comes first: Executor runs a copy of a System Folder
 * whose volume is named after its directory, and an alias in it (the
 * Apple menu's "Control Panels") means the folder next to it, not the
 * one on the disk image it was copied from.
 *
 * The record: userType.l, size.w, version.w (2), kind.w (0 file, 1
 * folder), volume name (Str27), volume created.l, signature.w, drive.w,
 * parent dir ID.l, name (Str63), file/dir ID.l, created.l, type.l,
 * creator.l, nlvlFrom.w, nlvlTo.w, ... at 150 tagged data (tag.w, len.w,
 * bytes padded to even; -1 ends): 0 parent name, 1 dir IDs, 2 full path. */
namespace
{
uint16_t be16(const uint8_t *p) { return p[0] << 8 | p[1]; }
uint32_t be32(const uint8_t *p) { return (uint32_t)be16(p) << 16 | be16(p + 2); }

struct AliasInfo
{
    Str27 volume;
    int32_t parent;
    Str63 name;
    int16_t nlvl_from, nlvl_to;
    /* the full path, split at colons: [0] is the volume */
    int ncomps = 0;
    Str63 comps[32];
};

bool parse_alias(AliasHandle alias, AliasInfo &a)
{
    Size size = GetHandleSize((Handle)alias);
    const uint8_t *p = (const uint8_t *)*alias;
    if(size < 150 || be16(p + 6) != 2)
        return false;
    memcpy(a.volume, p + 10, std::min<int>(p[10], 27) + 1);
    a.parent = be32(p + 46);
    memcpy(a.name, p + 50, std::min<int>(p[50], 63) + 1);
    a.nlvl_from = be16(p + 130);
    a.nlvl_to = be16(p + 132);
    Size end = std::min<Size>(size, be16(p + 4));
    for(Size off = 150; off + 4 <= end;)
    {
        int16_t tag = be16(p + off);
        uint16_t len = be16(p + off + 2);
        if(tag == -1 || off + 4 + len > end)
            break;
        if(tag == 2)
        {
            const uint8_t *s = p + off + 4;
            int start = 0;
            for(int i = 0; i <= len && a.ncomps < 32; i++)
                if(i == len || s[i] == ':')
                {
                    int n = std::min(i - start, 63);
                    if(n > 0)
                    {
                        a.comps[a.ncomps][0] = n;
                        memcpy(a.comps[a.ncomps] + 1, s + start, n);
                        a.ncomps++;
                    }
                    start = i + 1;
                }
        }
        off += 4 + ((len + 1) & ~1);
    }
    return true;
}

/* The item named in a directory: its ID if a folder (0 for a file). */
bool lookup(INTEGER vref, int32_t dir, ConstStringPtr name, int32_t *child)
{
    Str63 n;
    memcpy(n, name, name[0] + 1);
    CInfoPBRec pb = {};
    pb.hFileInfo.ioNamePtr = n;
    pb.hFileInfo.ioVRefNum = vref;
    pb.hFileInfo.ioDirID = dir;
    if(PBGetCatInfo(&pb, false) != noErr)
        return false;
    *child = (pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR) ? (int32_t)pb.dirInfo.ioDrDirID : 0;
    return true;
}

int32_t parent_of(INTEGER vref, int32_t dir)
{
    CInfoPBRec pb = {};
    Str63 n;
    n[0] = 0;
    pb.dirInfo.ioNamePtr = n;
    pb.dirInfo.ioVRefNum = vref;
    pb.dirInfo.ioDrDirID = dir;
    pb.dirInfo.ioFDirIndex = -1;
    if(PBGetCatInfo(&pb, false) != noErr)
        return 0;
    return pb.dirInfo.ioDrParID;
}

/* Down names[first..ncomps) from dir; the last is the target. */
bool walk(INTEGER vref, int32_t dir, const AliasInfo &a, int first, FSSpec *out)
{
    if(first >= a.ncomps)
        return false;
    for(int i = first; i < a.ncomps - 1; i++)
    {
        int32_t child;
        if(!lookup(vref, dir, a.comps[i], &child) || !child)
            return false;
        dir = child;
    }
    int32_t child;
    if(!lookup(vref, dir, a.comps[a.ncomps - 1], &child))
        return false;
    out->vRefNum = vref;
    out->parID = dir;
    memcpy(out->name, a.comps[a.ncomps - 1], a.comps[a.ncomps - 1][0] + 1);
    return true;
}

bool volume_named(ConstStringPtr name, INTEGER *vref)
{
    Str255 n;
    memcpy(n, name, name[0] + 1);
    HParamBlockRec pb = {};
    pb.volumeParam.ioNamePtr = n;
    pb.volumeParam.ioVolIndex = -1;
    if(PBHGetVInfo(&pb, false) != noErr)
        return false;
    *vref = pb.volumeParam.ioVRefNum;
    return true;
}

OSErr resolve(const FSSpec *fromFile, AliasHandle alias, FSSpec *target)
{
    AliasInfo a;
    if(!alias || !*alias || !parse_alias(alias, a))
        return paramErr;

    FSSpec out;
    if(fromFile && a.nlvl_from > 0 && a.nlvl_to > 0 && a.nlvl_to < a.ncomps)
    {
        int32_t dir = fromFile->parID;
        for(int i = 1; i < a.nlvl_from && dir; i++)
            dir = parent_of(fromFile->vRefNum, dir);
        if(dir && walk(fromFile->vRefNum, dir, a, a.ncomps - a.nlvl_to, &out))
        {
            *target = out;
            return noErr;
        }
    }

    INTEGER vref;
    if(volume_named(a.ncomps ? a.comps[0] : a.volume, &vref)
       && walk(vref, 2 /* the root */, a, 1, &out))
    {
        *target = out;
        return noErr;
    }
    if(volume_named(a.volume, &vref))
    {
        int32_t child;
        if(lookup(vref, a.parent, a.name, &child))
        {
            out.vRefNum = vref;
            out.parID = a.parent;
            memcpy(out.name, a.name, a.name[0] + 1);
            *target = out;
            return noErr;
        }
    }
    return fnfErr;
}
}

OSErr Executor::C_ResolveAlias(FSSpecPtr fromFile, AliasHandle alias,
                               FSSpecPtr target, Boolean *wasAliased)
{
    OSErr retval;
    alias_head_t *headp;
    Str255 volname;
    FSSpec fs;
    HParamBlockRec pb;

    if(resolve(fromFile, alias, target) == noErr)
    {
        *wasAliased = false; /* "needs update" */
        return noErr;
    }

    /* Executor's own records (NewAliasMinimal, for the Apple events it
       sends when launching): the volume, parent directory and name. */
    retval = noErr;
    headp = (decltype(headp))*alias;
    str255assign(volname, headp->volumeName);
    fs.parID = headp->ioDirID; /* NOT VALID IF THIS IS A FULL PATH SPEC */
    str255assign(fs.name, headp->fileName);

    pb.volumeParam.ioNamePtr = (StringPtr)volname;
    pb.volumeParam.ioVolIndex = -1;
    pb.volumeParam.ioVRefNum = 0;
    retval = PBHGetVInfo(&pb, false);
    if(retval == noErr)
    {
        fs.vRefNum = pb.volumeParam.ioVRefNum;
        *wasAliased = false;
        *target = fs;
    }

    return retval;
}

OSErr Executor::C_FollowFinderAlias(FSSpecPtr fromFile, AliasHandle alias,
                                    Boolean logon, FSSpecPtr target,
                                    Boolean *wasChanged)
{
    (void)logon;
    *wasChanged = false;
    return resolve(fromFile, alias, target);
}

OSErr Executor::C_ResolveAliasFile(FSSpecPtr theSpec,
                                   Boolean resolveAliasChains,
                                   Boolean *targetIsFolder, Boolean *wasAliased)
{
    *wasAliased = false;
    for(int hops = 0; hops < 10; hops++)
    {
        CInfoPBRec pb = {};
        Str63 name;
        memcpy(name, theSpec->name, theSpec->name[0] + 1);
        pb.hFileInfo.ioNamePtr = name;
        pb.hFileInfo.ioVRefNum = theSpec->vRefNum;
        pb.hFileInfo.ioDirID = theSpec->parID;
        OSErr err = PBGetCatInfo(&pb, false);
        if(err != noErr)
            return err;
        *targetIsFolder = !!(pb.hFileInfo.ioFlAttrib & ATTRIB_ISADIR);
        /* A Finder alias file: kIsAlias in its Finder flags, the record
           in its 'alis' 0. */
        if(*targetIsFolder || !(pb.hFileInfo.ioFlFndrInfo.fdFlags & 0x8000))
            return noErr;
        if(*wasAliased && !resolveAliasChains)
            return noErr;
        INTEGER saved = CurResFile();
        INTEGER rn = FSpOpenResFile(theSpec, fsRdPerm);
        if(rn == -1)
            return ResError() ? ResError() : fnfErr;
        UseResFile(rn);
        AliasHandle alias = (AliasHandle)Get1Resource("alis"_4, 0);
        OSErr rerr = fnfErr;
        FSSpec target;
        if(alias)
            rerr = resolve(theSpec, alias, &target);
        UseResFile(saved);
        CloseResFile(rn);
        if(rerr != noErr)
            return rerr;
        *theSpec = target;
        *wasAliased = true;
    }
    return noErr;
}

OSErr Executor::C_MatchAlias(FSSpecPtr fromFile, int32_t rulesMask,
                             AliasHandle alias, GUEST<int16_t> *aliasCount,
                             FSSpecArrayPtr aliasList, Boolean *needsUpdate,
                             AliasFilterUPP aliasFilter, Ptr yourDataPtr)
{
    warning_unimplemented("");
    return paramErr;
}

OSErr Executor::C_GetAliasInfo(AliasHandle alias, AliasTypeInfo index,
                               Str63 theString)
{
    warning_unimplemented("");
    return paramErr;
}

static int
EVENUP(int n)
{
    int retval = n;

    if(retval & 1)
        ++retval;
    return retval;
}

#if 0
static OSErr
parse2 (AliasHandle ah, const void *addrs[], int count)
{
  OSErr retval;
  Size size;
	
  size = GetHandleSize ((Handle) ah);
  if (size < sizeof (alias_head_t) + sizeof (INTEGER))
    retval = paramErr;
  else if (count < 0)
    retval = paramErr;
  else
    {
      const alias_head_t *headp;
      const INTEGER *partp, *ep;
		
      headp = (alias_head_t *) *ah;
      partp = (INTEGER *) (&headp[1]);
      ep = (INTEGER *) ((char *) headp + std::min(size, headp->length));
      memset (addrs, 0, count * sizeof addrs[0]);
      for (; partp < ep && *partp != -1;
	   partp = (INTEGER *) ((char *) partp + EVENUP (4 + partp[1])))
	{
	  int part;
			
	  part = *partp;
	  if (part < count)
	    addrs[part] = partp + 1;
	}
      retval = *partp == -1 ? noErr : paramErr;
    }
	
  return retval;
}
#endif

static OSErr
decompose_full_path(INTEGER path_len, Ptr fullPath, Str27 volumeName,
                    Str31 fileName)
{
    OSErr retval;
    char *first_colon;
    char *last_colon;
    char *p, *ep;
    int volume_len;
    int file_len;

    for(p = (char *)fullPath,
    ep = p + path_len, first_colon = 0, last_colon = 0;
        p != ep;
        ++p)
    {
        if(*p == ':')
        {
            if(!first_colon)
                first_colon = p;
            last_colon = p;
        }
    }
    if(!first_colon)
        retval = paramErr;
    else
    {
        volume_len = first_colon - (char *)fullPath;
        file_len = ep - last_colon - 1;
        if(volume_len > 27 || file_len > 31)
            retval = paramErr;
        else
        {
            volumeName[0] = volume_len;
            memcpy(volumeName + 1, fullPath, volume_len);
            fileName[0] = file_len;
            memcpy(fileName + 1, last_colon + 1, file_len);
            retval = noErr;
        }
    }
    return retval;
}

static void
init_head(alias_head_t *headp, Str27 volumeName, Str31 fileName)
{
    memset(headp, 0, sizeof *headp);
    headp->usually_2 = 2;
    memcpy(headp->volumeName, volumeName, volumeName[0] + 1);
    memcpy(headp->fileName, fileName, fileName[0] + 1);
    headp->mystery_words[0] = -1;
    headp->mystery_words[1] = -1;
    headp->mystery_words[3] = 17;
}

static void
init_tail(alias_tail_t *tailp, Str32 zoneName, Str31 serverName,
          Str27 volumeName)
{
    Handle h;

    memset(tailp, 0, sizeof *tailp);
    memcpy(tailp->zone, zoneName, zoneName[0] + 1);
    memcpy(tailp->server, serverName, serverName[0] + 1);
    memcpy(tailp->volumeName, volumeName, volumeName[0] + 1);
    h = (Handle)GetString(-16096);
    if(!h)
        tailp->network_identity_owner_name[0] = 0;
    else
    {
        int name_len;

        name_len = std::min(GetHandleSize(h), 31);
        memcpy(tailp->network_identity_owner_name, *h, name_len);
    }
    tailp->weird_info[0] = 0x00A8;
    tailp->weird_info[1] = 0x6166;
    tailp->weird_info[2] = 0x706D;
    tailp->weird_info[5] = 0x0003;
    tailp->weird_info[6] = 0x0018;
    tailp->weird_info[7] = 0x0039;
    tailp->weird_info[8] = 0x0059;
    tailp->weird_info[9] = 0x0075;
    tailp->weird_info[10] = 0x0095;
    tailp->weird_info[11] = 0x009E;
}

static OSErr
assemble_pieces(GUEST<AliasHandle> *ahp, alias_head_t *headp, int n_pieces, ...)
{
    Size n_bytes_needed;
    va_list va;
    int i;
    Handle h;
    OSErr retval;

    n_bytes_needed = sizeof *headp;
    va_start(va, n_pieces);
    for(i = 0; i < n_pieces; ++i)
    {
        INTEGER tag;
        INTEGER length;
        void *p;

        tag = va_arg(va, int);
        length = va_arg(va, int);
        p = va_arg(va, void *);
        n_bytes_needed += sizeof(INTEGER) + sizeof(INTEGER) + EVENUP(length);
    }
    va_end(va);
    n_bytes_needed += sizeof(INTEGER) + sizeof(INTEGER);
    h = NewHandle(n_bytes_needed);
    if(!h)
        retval = MemError();
    else
    {
        char *op;

        headp->length = n_bytes_needed;
        op = (char *)*h;
        memcpy(op, headp, sizeof(*headp));
        op += sizeof(*headp);
        va_start(va, n_pieces);
        for(i = 0; i < n_pieces; ++i)
        {
            INTEGER tag;
            GUEST<INTEGER> tag_x;
            INTEGER length;
            GUEST<INTEGER> length_x;
            void *p;

            tag = va_arg(va, int);
            tag_x = tag;
            length = va_arg(va, int);
            length_x = length;
            p = va_arg(va, void *);
            memcpy(op, &tag_x, sizeof tag_x);
            op += sizeof tag_x;
            memcpy(op, &length_x, sizeof length_x);
            op += sizeof length_x;
            memcpy(op, p, length);
            op += length;
            if(length & 1)
                *op++ = 0;
        }
        va_end(va);
        memset(op, -1, sizeof(INTEGER));
        op += sizeof(INTEGER);
        memset(op, 0, sizeof(INTEGER));

        *ahp = (AliasHandle)h;
        retval = noErr;
    }
    return retval;
}

/*
FULL_PATH_TAG, path_len, fullPath,
TAIL_TAG     , sizeof (tail)-2, &tail.weird_info)
*/

OSErr Executor::C_NewAliasMinimalFromFullPath(
    INTEGER path_len, Ptr fullPath, Str32 zoneName, Str31 serverName,
    GUEST<AliasHandle> *ahp)
{
    OSErr retval;

    warning_unimplemented("not tested much");
    if(zoneName[0] > 32 || serverName[0] > 31 || !ahp)
        retval = paramErr;
    else
    {
        Str27 volumeName;
        Str63 fileName;

        retval = decompose_full_path(path_len, fullPath, volumeName, fileName);
        if(retval == noErr)
        {
            alias_head_t head;
            alias_tail_t tail;

            init_head(&head, volumeName, fileName);
            if(volumeName[0] < 27)
                head.volumeName[volumeName[0] + 1] = ':';
            head.zero_or_one = 1;
            head.zero_or_neg_one = -1;
            head.ioDirID = -1;
            head.ioFlCrDat = 0;
            init_tail(&tail, zoneName, serverName, volumeName);
            retval = assemble_pieces(ahp, &head, 2,
                                     FULL_PATH_TAG, path_len, (void *)fullPath,
                                     TAIL_TAG, (int)sizeof(tail) - 2,
                                     (void *)&tail.weird_info);
        }
    }

    if(retval != noErr)
        *ahp = nullptr;

    return retval;
}

OSErr Executor::C_NewAliasMinimal(FSSpecPtr fsp, GUEST<AliasHandle> *ahp)
{
    HParamBlockRec hpb;
    OSErr retval;
    Str27 volName;

    warning_unimplemented("not tested much");
    memset(&hpb, 0, sizeof hpb);
    hpb.ioParam.ioNamePtr = &volName[0];
    hpb.ioParam.ioVRefNum = fsp->vRefNum;
    retval = PBHGetVInfo(&hpb, false);
    if(retval == noErr)
    {
        alias_head_t head;

        init_head(&head, volName, fsp->name);
        head.ioVCrDate = hpb.volumeParam.ioVCrDate;
        head.ioVSigWord = hpb.volumeParam.ioVSigWord;
        memset(&hpb, 0, sizeof hpb);
        hpb.ioParam.ioNamePtr = &fsp->name[0];
        hpb.ioParam.ioVRefNum = fsp->vRefNum;
        hpb.fileParam.ioDirID = fsp->parID;
        retval = PBHGetFInfo(&hpb, false);
        if(retval == noErr)
        {
            alias_tail_t tail;
            Handle h;
            Str31 serverName;

            head.ioDirID = hpb.fileParam.ioDirID;
            head.ioFlCrDat = hpb.fileParam.ioFlCrDat;
            head.type_info = hpb.fileParam.ioFlFndrInfo.fdType;
            head.creator = hpb.fileParam.ioFlFndrInfo.fdCreator;
            h = (Handle)GetString(-16413);
            if(!h)
                serverName[0] = 0;
            else
            {
                int len;

                len = std::min(GetHandleSize(h), 32);
                memcpy(serverName, *h, len);
            }
            init_tail(&tail, (StringPtr) "\1*", serverName, volName);
            retval = assemble_pieces(ahp, &head, 1,
                                     TAIL_TAG, (int)sizeof(tail) - 2,
                                     (void *)&tail.weird_info);
        }
    }

    if(retval != noErr)
        *ahp = nullptr;
    return retval;
}
