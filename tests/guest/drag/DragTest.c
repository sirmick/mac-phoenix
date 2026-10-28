/*
 * DragTest.c -- the Drag Manager from PowerPC code (DragLib): a drag made,
 * an item and its flavors added and read back, bounds set and read, the
 * drag disposed; no tracking (that needs the mouse). On Executor, DragLib
 * is glue through DragDispatch's table entry to Apple's 68k Drag Manager
 * (TRAP_GLUE, docs/executor/MacOS8-9.md); on the G3, Apple's own.
 * One line per check, "PASS name" or "FAIL name: why"; then "---", in
 * Host:drag_results.txt (tests/test_guest_suite.sh --results
 * drag_results.txt). Built with Retro68 by build-retro68.sh.
 */

#include <Types.h>
#include <Gestalt.h>
#include <Drag.h>
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

int main(void)
{
    long drag = 0;
    DragRef d = 0;
    OSErr err;
    UInt16 items = 0, flavors = 0;
    ItemReference ref = 0;
    FlavorType type = 0;
    Size size = 0;
    char buf[16];
    Rect r = { 10, 20, 30, 40 }, back;

    freopen("Host:drag_results.txt", "w", stdout);
    err = Gestalt('drag', &drag) /* gestaltDragMgrAttr */;
    check("gestalt", err == noErr && (drag & 1), "Gestalt 'drag' says no Drag Manager", drag);

    err = NewDrag(&d);
    check("new", err == noErr && d != 0, "NewDrag", err);
    if(err == noErr)
    {
        err = AddDragItemFlavor(d, 1, 'TEXT', "hello", 5, 0);
        check("add_flavor", err == noErr, "AddDragItemFlavor", err);
        err = AddDragItemFlavor(d, 1, 'styl', "\0\0", 2, 0);
        check("add_second_flavor", err == noErr, "AddDragItemFlavor 'styl'", err);
        err = CountDragItems(d, &items);
        check("count_items", err == noErr && items == 1, "CountDragItems", items);
        err = GetDragItemReferenceNumber(d, 1, &ref);
        check("item_ref", err == noErr && ref == 1, "GetDragItemReferenceNumber", ref);
        err = CountDragItemFlavors(d, ref, &flavors);
        check("count_flavors", err == noErr && flavors == 2, "CountDragItemFlavors", flavors);
        err = GetFlavorType(d, ref, 1, &type);
        check("flavor_type", err == noErr && type == 'TEXT', "GetFlavorType", type);
        err = GetFlavorDataSize(d, ref, 'TEXT', &size);
        check("flavor_size", err == noErr && size == 5, "GetFlavorDataSize", size);
        memset(buf, 0, sizeof buf);
        size = sizeof buf;
        err = GetFlavorData(d, ref, 'TEXT', buf, &size, 0);
        check("flavor_data", err == noErr && size == 5 && !memcmp(buf, "hello", 5), "GetFlavorData", size);
        err = GetFlavorDataSize(d, ref, 'PICT', &size);
        check("missing_flavor", err == badDragFlavorErr, "GetFlavorDataSize of a flavor not there", err);
        err = SetDragItemBounds(d, ref, &r);
        check("set_bounds", err == noErr, "SetDragItemBounds", err);
        err = GetDragItemBounds(d, ref, &back);
        check("get_bounds", err == noErr && back.top == 10 && back.left == 20 && back.bottom == 30
                  && back.right == 40, "GetDragItemBounds", back.top);
        err = DisposeDrag(d);
        check("dispose", err == noErr, "DisposeDrag", err);
    }
    printf("DONE %d\n---\n", failures);
    fclose(stdout);
    return 0;
}
