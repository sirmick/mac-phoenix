/*
 * IconTest.c -- Icon Services from PowerPC code (IconServicesLib): system
 * icons by creator and type, owners, icons for a file, a folder and the
 * volume, registering a family, families and suites, hit testing,
 * regions, composites. On Executor, iconservices.cpp; on the G3 with
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
            ReleaseIconRef(ref);
    }

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
