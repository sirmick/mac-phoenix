/* ressize.c — what SizeResource (GetResourceSizeOnDisk, $A9A5) and
 * MaxSizeRsrc ($A821) answer for a resource that isn't loaded and for one
 * that is loaded with its handle resized.
 *
 * ClarisWorks 4 sums SizeResource over its CODE resources at launch and
 * says the copy was "modified by another program or virus" when the sum is
 * off: its loaded segments hold unpacked code, and the sum expects their
 * size in memory. Results to Host:ressize_test.txt. */

#include <ConditionalMacros.h>
#include <Files.h>
#include <Fonts.h>
#include <Memory.h>
#include <Resources.h>
#include <Quickdraw.h>
#include <Windows.h>
#include <stdio.h>

QDGlobals qd;

static FILE *out;
static int passes, fails;

static void check(const char *name, long got, long want)
{
    if(got == want)
    {
        fprintf(out, "PASS %s\n", name);
        passes++;
    }
    else
    {
        fprintf(out, "FAIL %s got %ld, want %ld\n", name, got, want);
        fails++;
    }
}

int main(void)
{
    FSSpec spec;
    Handle h;
    short ref;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();

    if((out = fopen("Host:ressize_test.txt", "w")) == NULL)
        return 1;

    FSMakeFSSpec(0, 0, "\pHost:ressize.rsrc", &spec);
    FSpDelete(&spec);
    FSpCreateResFile(&spec, 'MPrs', 'rsrc', 0);
    ref = FSpOpenResFile(&spec, fsRdWrPerm);
    if(ref == -1)
    {
        fprintf(out, "FAIL open ResError %d\n", ResError());
        fails++;
    }
    else
    {
        h = NewHandleClear(100);
        AddResource(h, 'TEST', 128, "\p");
        CloseResFile(ref);
        ref = FSpOpenResFile(&spec, fsRdPerm);

        SetResLoad(false);
        h = Get1Resource('TEST', 128);
        SetResLoad(true);
        check("handle_empty", h && !*h, 1);
        check("size_unloaded", SizeResource(h), 100);
        /* the gap to the next resource's data, length word included */
        check("maxsize_unloaded", MaxSizeRsrc(h), 104);

        LoadResource(h);
        check("size_loaded", SizeResource(h), 100);
        SetHandleSize(h, 300);
        check("size_loaded_resized", SizeResource(h), 300);
        check("maxsize_loaded_resized", MaxSizeRsrc(h), 300);

        CloseResFile(ref);
        FSpDelete(&spec);
    }

    fprintf(out, "---\n%d passed, %d failed, 0 skipped\n", passes, fails);
    fclose(out);
    return 0;
}
