/*
 * IconTest.c -- Icon Services from PowerPC code (IconServicesLib): system
 * icons by creator and type, owners, icons for a file, a folder and the
 * volume (its driver's media icon), alias files' icons, registering a
 * family, families and suites, hit testing, regions, composites. On Executor, iconservices.cpp; on the G3 with
 * 9.0.4, Apple's. One line per check, "PASS name" or "FAIL name: why";
 * "INFO" lines say what was seen; then "---", in Host:icon_results.txt
 * (tests/test_guest_suite.sh --results icon_results.txt). Built with
 * Retro68 by build-retro68.sh.
 */

#include <Types.h>
#include <Memory.h>
#include <Files.h>
#include <Icons.h>
#include <Quickdraw.h>
#include <Resources.h>
#include <Processes.h>
#include <Devices.h>
#include <CodeFragments.h>
#include <Finder.h>
#include <Script.h>
#include <Gestalt.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok, const char *why, long code)
{
    if(ok)
        printf("PASS %s\n", name);
    else
    {
        printf("FAIL %s: %s (%ld)\n", name, why, code);
        failures++;
    }
}

static void tagstr(char *out, OSType t)
{
    out[0] = (char)(t >> 24);
    out[1] = (char)(t >> 16);
    out[2] = (char)(t >> 8);
    out[3] = (char)t;
    out[4] = 0;
}

static UInt16 owners(IconRef ref)
{
    UInt16 n = 0;
    GetIconRefOwners(ref, &n);
    return n;
}

/* The family's elements: types and sizes. */
static void describeFamily(const char *what, IconFamilyHandle fam)
{
    long size, at;
    printf("INFO %s:", what);
    if(!fam)
    {
        printf(" none\n");
        return;
    }
    size = GetHandleSize((Handle)fam);
    {
        unsigned char *p = (unsigned char *)*fam;
        char t[5];
        tagstr(t, *(OSType *)p);
        printf(" %s %ld/%ld bytes:", t, *(long *)(p + 4), size);
        for(at = 8; at + 8 <= size;)
        {
            long n = *(long *)(p + at + 4);
            tagstr(t, *(OSType *)(p + at));
            printf(" %s=%ld", t, n);
            if(n < 8)
                break;
            at += n;
        }
    }
    printf("\n");
}

static void sysIcon(OSType type, IconRef *out)
{
    char t[5];
    OSErr err;
    tagstr(t, type);
    *out = NULL;
    err = GetIconRef(kOnSystemDisk, kSystemIconsCreator, type, out);
    {
        char name[32];
        sprintf(name, "system icon %s", t);
        check(name, err == noErr && *out != NULL && IsValidIconRef(*out), "err", err);
    }
}

/* The black-and-white icon of a ref (the 128 bytes before the mask). */
static int refICN(IconRef ref, unsigned char *out)
{
    IconFamilyHandle fam = NULL;
    Handle h;
    int ok = 0;
    if(IconRefToIconFamily(ref, svAllAvailableData, &fam) != noErr || !fam)
        return 0;
    h = NewHandle(0);
    if(GetIconFamilyData(fam, 'ICN#', h) == noErr && GetHandleSize(h) >= 128)
    {
        memcpy(out, *h, 128);
        ok = 1;
    }
    DisposeHandle(h);
    DisposeHandle((Handle)fam);
    return ok;
}

/* The volume's icon and its driver's: Control 21 (drive icon) and 22
   (media icon) give an ICN# with its mask. */
static void volumeIcon(short vref, IconRef ref)
{
    HParamBlockRec vpb;
    ParamBlockRec cpb;
    unsigned char icn[128];
    int code, have = refICN(ref, icn);
    memset(&vpb, 0, sizeof vpb);
    vpb.volumeParam.ioVRefNum = vref;
    PBHGetVInfoSync(&vpb);
    for(code = 21; code <= 22; code++)
    {
        OSErr err;
        Ptr icon;
        memset(&cpb, 0, sizeof cpb);
        cpb.cntrlParam.ioVRefNum = vpb.volumeParam.ioVDrvInfo;
        cpb.cntrlParam.ioCRefNum = vpb.volumeParam.ioVDRefNum;
        cpb.cntrlParam.csCode = code;
        err = PBControlSync(&cpb);
        icon = *(Ptr *)cpb.cntrlParam.csParam;
        printf("INFO volume driver Control %d: err %d, the volume's icon is it %d\n", code, err,
               err == noErr && icon && have && memcmp(icon, icn, 128) == 0);
    }
    {
        /* what the volume shows: the driver's media icon, else its drive icon */
        Ptr want = NULL;
        for(code = 22; code >= 21 && !want; code--)
        {
            memset(&cpb, 0, sizeof cpb);
            cpb.cntrlParam.ioVRefNum = vpb.volumeParam.ioVDrvInfo;
            cpb.cntrlParam.ioCRefNum = vpb.volumeParam.ioVDRefNum;
            cpb.cntrlParam.csCode = code;
            if(PBControlSync(&cpb) == noErr)
                want = *(Ptr *)cpb.cntrlParam.csParam;
        }
        if(want)
            check("the volume's icon is its driver's", have && memcmp(want, icn, 128) == 0, "have", have);
    }
}

/* A file with icon families at the IDs given (0: none), its Finder flags. */
static void makeIconFile(FSSpec *spec, short vref, long dirID, ConstStr255Param name, OSType type,
                         unsigned short flags, short id1, unsigned char fill1, short id2, unsigned char fill2)
{
    FInfo fi;
    short rn, i;
    FSMakeFSSpec(vref, dirID, name, spec);
    FSpDelete(spec);
    FSpCreateResFile(spec, 'ITst', type, smSystemScript);
    rn = FSpOpenResFile(spec, fsRdWrPerm);
    for(i = 0; i < 2; i++)
    {
        short id = i ? id2 : id1;
        unsigned char fill = i ? fill2 : fill1;
        Handle h;
        if(!id)
            continue;
        h = NewHandle(256);
        memset(*h, fill, 128);
        memset(*h + 128, 0xFF, 128);
        AddResource(h, 'ICN#', id, "\p");
    }
    CloseResFile(rn);
    FSpGetFInfo(spec, &fi);
    fi.fdFlags |= flags;
    FSpSetFInfo(spec, &fi);
}

/* What GetIconRefFromFile gives for the file: 1 its first family, 2 its
   second, 0 neither. */
static int whichIcon(FSSpec *spec, unsigned char fill1, unsigned char fill2)
{
    IconRef ref = NULL;
    SInt16 label;
    unsigned char icn[128];
    int which = 0;
    if(GetIconRefFromFile(spec, &ref, &label) == noErr && ref)
    {
        if(refICN(ref, icn))
            which = icn[0] == fill1 && icn[127] == fill1 ? 1 : icn[0] == fill2 && icn[127] == fill2 ? 2 : 0;
        ReleaseIconRef(ref);
    }
    return which;
}

/* An alias file keeps its target's icon at -16496 (the Finder copies it
   there when it makes the alias), a custom icon is at -16455. 9.0.4: with
   kHasCustomIcon, -16455, else -16496, alias or not; without it, neither. */
static void aliasIcons(short vref, long dirID)
{
    FSSpec spec;
    int w;
    makeIconFile(&spec, vref, dirID, "\pIconTest alias", 'adrp', kIsAlias | kHasCustomIcon, -16496, 0xA5, 0, 0);
    w = whichIcon(&spec, 0xA5, 0xC3);
    printf("INFO alias with its icon at -16496: %d\n", w);
    check("an alias file's icon (-16496)", w == 1, "which", w);
    FSpDelete(&spec);
    makeIconFile(&spec, vref, dirID, "\pIconTest alias 2", 'adrp', kIsAlias | kHasCustomIcon, -16455, 0x5A, -16496,
                 0xA5);
    w = whichIcon(&spec, 0x5A, 0xA5);
    printf("INFO alias with -16455 and -16496: %d\n", w);
    check("-16455 before -16496", w == 1, "which", w);
    FSpDelete(&spec);
    makeIconFile(&spec, vref, dirID, "\pIconTest file -16496", 'TEXT', kHasCustomIcon, -16496, 0xA5, 0, 0);
    w = whichIcon(&spec, 0xA5, 0xC3);
    printf("INFO a file (no alias) with its icon at -16496: %d\n", w);
    check("a file's icon at -16496 (no alias)", w == 1, "which", w);
    FSpDelete(&spec);
    makeIconFile(&spec, vref, dirID, "\pIconTest alias 3", 'adrp', kIsAlias, -16496, 0xA5, 0, 0);
    w = whichIcon(&spec, 0xA5, 0xC3);
    printf("INFO alias without kHasCustomIcon, icon at -16496: %d\n", w);
    check("without kHasCustomIcon, no custom icon", w == 0, "which", w);
    FSpDelete(&spec);
}

/* 8.5 and later: the badges are icons of their own (the Finder composites
   an alias's icon with 'abdg'), not some other system icon. */
static void badges(void)
{
    static const OSType types[] = { 'abdg', 'lbdg', 'mbdg', 'sbdg', 'scrp' };
    unsigned char suit[128], icn[128];
    long sysv = 0;
    IconRef ref = NULL;
    int i;
    Gestalt(gestaltSystemVersion, &sysv);
    if(sysv < 0x0850)
    {
        printf("INFO badges: System %lx, before 8.5\n", sysv);
        return;
    }
    if(GetIconRef(kOnSystemDisk, kSystemIconsCreator, kGenericSuitcaseIcon, &ref) != noErr || !refICN(ref, suit))
        memset(suit, 0, sizeof suit);
    if(ref)
        ReleaseIconRef(ref);
    for(i = 0; i < 5; i++)
    {
        char name[40], t[5];
        OSErr err;
        int have = 0;
        ref = NULL;
        err = GetIconRef(kOnSystemDisk, kSystemIconsCreator, types[i], &ref);
        if(err == noErr && ref)
        {
            have = refICN(ref, icn);
            ReleaseIconRef(ref);
        }
        tagstr(t, types[i]);
        sprintf(name, "badge %s: its own large icon", t);
        check(name, err == noErr && have && memcmp(icn, suit, 128) != 0, "err", err);
    }
}

int main(void)
{
    IconRef folder = NULL, folder2 = NULL, doc = NULL, trash = NULL, disk = NULL, app = NULL;
    IconRef ref = NULL, reg = NULL, comp = NULL, bg = NULL, fg = NULL;
    IconFamilyHandle fam = NULL;
    IconSelectorValue sizes = 0;
    Handle suite = NULL, h;
    OSErr err;
    SInt16 label;
    FSSpec spec;
    ProcessSerialNumber psn = { 0, kCurrentProcess };
    ProcessInfoRec info;
    Rect r = { 0, 0, 32, 32 };
    RgnHandle rgn;
    Point pt;
    short vref;
    long dirID;
    int i;

    freopen("Host:icon_results.txt", "w", stdout);

    /* ---- system icons ---- */
    sysIcon(kGenericFolderIcon, &folder);
    sysIcon(kGenericDocumentIcon, &doc);
    sysIcon(kTrashIcon, &trash);
    sysIcon(kGenericHardDiskIcon, &disk);
    sysIcon(kGenericApplicationIcon, &app);
    /* System icons are shared: others (the Finder) may hold them too, so
       owners are counted from what they were. */
    {
        UInt16 base = owners(folder);
        printf("INFO owners: folder %u doc %u\n", owners(folder), owners(doc));
        err = GetIconRef(kOnSystemDisk, kSystemIconsCreator, kGenericFolderIcon, &folder2);
        printf("INFO folder again: err %d same %d owners %u\n", err, folder2 == folder, owners(folder));
        check("the same ref, one more owner", err == noErr && folder2 == folder && owners(folder) == base + 1,
              "owners", owners(folder));
        err = AcquireIconRef(folder);
        check("acquire", err == noErr && owners(folder) == base + 2, "owners", owners(folder));
        err = ReleaseIconRef(folder);
        err = ReleaseIconRef(folder);
        check("release", err == noErr && owners(folder) == base, "owners", owners(folder));
    }
    err = GetIconRef(kOnSystemDisk, 'zzzz', 'zzzz', &ref);
    printf("INFO unknown creator/type: err %d ref %d valid %d\n", err, ref != NULL, ref ? IsValidIconRef(ref) : -1);
    check("unknown creator/type: a (generic) icon", err == noErr && ref && IsValidIconRef(ref), "err", err);
    if(err == noErr && ref)
        ReleaseIconRef(ref);
    check("IsValidIconRef(nil)", !IsValidIconRef(NULL), "valid", 1);

    /* ---- sizes, families, suites ---- */
    err = GetIconSizesFromIconRef(svAllAvailableData, &sizes, kIconServicesNormalUsageFlag, folder);
    printf("INFO folder sizes: err %d %08lx\n", err, (unsigned long)sizes);
    check("folder has the large black-and-white icon", err == noErr && (sizes & svLarge1Bit),
          "sizes", (long)sizes);
    err = IconRefToIconFamily(folder, svAllAvailableData, &fam);
    check("family of the folder icon", err == noErr && fam != NULL, "err", err);
    describeFamily("folder family", fam);
    if(fam)
    {
        h = NewHandle(0);
        err = GetIconFamilyData(fam, 'ICN#', h);
        check("ICN# from the family", err == noErr && GetHandleSize(h) == 256, "size", GetHandleSize(h));
        err = GetIconFamilyData(fam, 'zzzz', h);
        printf("INFO missing element: err %d\n", err);
        check("missing element: paramErr", err == paramErr, "err", err);
        err = IconFamilyToIconSuite(fam, svAllAvailableData, &suite);
        check("family to suite", err == noErr && suite != NULL, "err", err);
        DisposeHandle(h);
    }
    {
        /* IconServicesLib's private NewIconSuiteFromIconRef, by name: the
           8.6 Finder makes the Apple menu items' icons with it (which
           icons, the suite, 0, the IconRef) */
        typedef OSErr (*NewSuiteProc)(IconSelectorValue, Handle *, UInt32, IconRef);
        ConnectionID conn;
        Ptr main, sym = NULL;
        SymClass cls;
        Str255 msg;
        Handle s = NULL, ics = NULL;
        err = GetSharedLibrary("\pIconServicesLib", kPowerPCArch, kReferenceCFrag, &conn, &main, msg);
        if(err == noErr)
            err = FindSymbol(conn, "\pNewIconSuiteFromIconRef", &sym, &cls);
        printf("INFO NewIconSuiteFromIconRef: err %d\n", err);
        if(err == noErr && sym)
        {
            err = ((NewSuiteProc)sym)(svSmall1Bit | svSmall8Bit, &s, 0, folder);
            if(err == noErr && s)
                GetIconFromSuite(&ics, s, 'ics#');
            check("NewIconSuiteFromIconRef: small icons of the folder", err == noErr && s && ics,
                  "err", err);
            ics = NULL;
            if(err == noErr && s)
                GetIconFromSuite(&ics, s, 'ICN#');
            check("NewIconSuiteFromIconRef: only the icons asked for", !ics, "ICN#", 1);
            if(s)
                DisposeIconSuite(s, true);
            s = NULL;
            err = ((NewSuiteProc)sym)(-1, &s, 0, folder);
            ics = NULL;
            if(err == noErr && s)
                GetIconFromSuite(&ics, s, 'ICN#');
            check("NewIconSuiteFromIconRef(-1): all of them", err == noErr && s && ics, "err", err);
            if(s)
                DisposeIconSuite(s, true);
        }
        else
            check("NewIconSuiteFromIconRef exported", 0, "err", err);
    }

    /* ---- registering a family ---- */
    if(fam)
    {
        err = RegisterIconRefFromIconFamily('ITst', 'test', fam, &reg);
        check("register a family", err == noErr && reg != NULL, "err", err);
        printf("INFO registered: owners %u\n", owners(reg));
        err = GetIconRef(kOnSystemDisk, 'ITst', 'test', &ref);
        check("registered ref found by creator/type", err == noErr && ref == reg, "err", err);
        printf("INFO registered, after GetIconRef: owners %u\n", owners(reg));
        if(err == noErr)
            ReleaseIconRef(ref);
        err = RegisterIconRefFromIconFamily('ITst', 'test', fam, &ref);
        printf("INFO registered twice: err %d same %d owners %u\n", err, ref == reg, owners(reg));
        check("registered twice: the same ref, one more owner", err == noErr && ref == reg && owners(reg) == 2,
              "owners", owners(reg));
        if(err == noErr)
            ReleaseIconRef(ref);
        err = UnregisterIconRef('ITst', 'test');
        check("unregister", err == noErr, "err", err);
        printf("INFO after unregister: valid %d owners %u\n", IsValidIconRef(reg), owners(reg));
        check("unregistered: no longer valid", !IsValidIconRef(reg), "valid", 1);
        ref = NULL;
        err = GetIconRef(kOnSystemDisk, 'ITst', 'test', &ref);
        printf("INFO unregistered, GetIconRef: err %d ref %d\n", err, ref != NULL);
        check("unregistered: GetIconRef gives the generic icon", err == noErr && ref != NULL && ref != reg, "err", err);
        if(err == noErr && ref)
            ReleaseIconRef(ref);
        ReleaseIconRef(reg);
        DisposeHandle((Handle)fam);
        fam = NULL;
    }

    /* ---- files, folders, the volume ---- */
    info.processInfoLength = sizeof info;
    info.processName = NULL;
    info.processAppSpec = &spec;
    GetProcessInformation(&psn, &info);
    err = GetIconRefFromFile(&spec, &ref, &label);
    check("the application's icon", err == noErr && ref != NULL, "err", err);
    printf("INFO application: label %d\n", label);
    if(err == noErr)
        ReleaseIconRef(ref);
    vref = spec.vRefNum;
    dirID = spec.parID;
    {
        FSSpec docSpec;
        FSMakeFSSpec(vref, dirID, "\pIconTest doc", &docSpec);
        FSpDelete(&docSpec);
        FSpCreate(&docSpec, 'ttxt', 'TEXT', smSystemScript);
        err = GetIconRefFromFile(&docSpec, &ref, &label);
        check("a document's icon", err == noErr && ref != NULL, "err", err);
        printf("INFO document: label %d\n", label);
        if(err == noErr)
            ReleaseIconRef(ref);
        FSpDelete(&docSpec);
    }
    {
        FSSpec dirSpec;
        CInfoPBRec pb;
        Str63 name;
        memset(&pb, 0, sizeof pb);
        FSMakeFSSpec(vref, dirID, "\p", &dirSpec);
        err = GetIconRefFromFile(&dirSpec, &ref, &label);
        check("the folder's icon (from its FSSpec)", err == noErr && ref != NULL, "err", err);
        printf("INFO folder from FSSpec: same as the generic folder %d\n", ref == folder);
        if(err == noErr)
            ReleaseIconRef(ref);
        name[0] = 0;
        pb.dirInfo.ioNamePtr = name;
        pb.dirInfo.ioVRefNum = vref;
        pb.dirInfo.ioDrDirID = dirID;
        pb.dirInfo.ioFDirIndex = -1;
        PBGetCatInfoSync(&pb);
        err = GetIconRefFromFolder(vref, pb.dirInfo.ioDrParID, dirID, 0, 0, &ref);
        check("GetIconRefFromFolder", err == noErr && ref != NULL, "err", err);
        printf("INFO GetIconRefFromFolder: same as the generic folder %d\n", ref == folder);
        if(err == noErr)
            ReleaseIconRef(ref);
        FSMakeFSSpec(vref, fsRtDirID, "\p", &dirSpec);
        err = GetIconRefFromFile(&dirSpec, &ref, &label);
        printf("INFO the volume's root: err %d ref %d\n", err, ref != NULL);
        if(err == noErr)
        {
            volumeIcon(vref, ref);
            ReleaseIconRef(ref);
        }
    }
    aliasIcons(vref, dirID);
    badges();

    /* ---- hit testing, regions ---- */
    pt.v = 16;
    pt.h = 16;
    check("PtInIconRef in the middle", PtInIconRef(&pt, &r, kAlignNone, kIconServicesNormalUsageFlag, doc),
          "in", 0);
    pt.v = 1;
    pt.h = 1;
    printf("INFO PtInIconRef at the corner: %d\n", PtInIconRef(&pt, &r, kAlignNone, kIconServicesNormalUsageFlag, doc));
    rgn = NewRgn();
    err = IconRefToRgn(rgn, &r, kAlignNone, kIconServicesNormalUsageFlag, doc);
    check("IconRefToRgn", err == noErr && !EmptyRgn(rgn), "err", err);
    printf("INFO document region: %d,%d,%d,%d\n", (*rgn)->rgnBBox.top, (*rgn)->rgnBBox.left,
           (*rgn)->rgnBBox.bottom, (*rgn)->rgnBBox.right);
    {
        Rect small = { 4, 4, 8, 8 };
        printf("INFO RectInIconRef middle: %d\n",
               RectInIconRef(&small, &r, kAlignNone, kIconServicesNormalUsageFlag, doc));
    }
    check("mask not empty", !IsIconRefMaskEmpty(doc), "empty", 1);
    DisposeRgn(rgn);

    /* ---- composites ---- */
    {
        UInt16 folderBefore = owners(folder);
    err = CompositeIconRef(folder, doc, &comp);
    check("composite", err == noErr && comp != NULL, "err", err);
    if(err == noErr)
    {
        err = IsIconRefComposite(comp, &bg, &fg);
        check("is composite", err == noErr && bg == folder && fg == doc, "err", err);
        printf("INFO composite owners %u, folder now %u\n", owners(comp), owners(folder));
        check("composite: one owner, and it holds its parts", owners(comp) == 1 && owners(folder) == folderBefore + 1,
              "owners", owners(comp));
        ReleaseIconRef(comp);
    }
    }
    err = IsIconRefComposite(doc, &bg, &fg);
    printf("INFO plain ref IsIconRefComposite: err %d bg %d fg %d\n", err, bg != NULL, fg != NULL);
    check("a plain ref is no composite", err == noErr && !bg && !fg, "err", err);

    /* ---- drawing, into the port as it is ---- */
    err = PlotIconRef(&r, kAlignNone, kTransformNone, kIconServicesNormalUsageFlag, doc);
    check("PlotIconRef", err == noErr, "err", err);

    /* ---- releasing to nothing ---- */
    for(i = 0; i < 1; i++)
        ReleaseIconRef(doc);
    printf("INFO doc after releasing its one owner: valid %d\n", IsValidIconRef(doc));
    ReleaseIconRef(folder);
    ReleaseIconRef(trash);
    ReleaseIconRef(disk);
    ReleaseIconRef(app);
    err = ReleaseIconRef(NULL);
    printf("INFO ReleaseIconRef(nil): err %d\n", err);
    check("ReleaseIconRef(nil)", err == invalidIconRefErr, "err", err);

    printf("DONE %d\n---\n", failures);
    return 0;
}
