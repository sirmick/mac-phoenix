/* Copyright 1994, 1995 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

#if !defined(NDEBUG)

/* dump.c; convenience functions for dumping various mac datastructures */

#if !defined(THINK_C)
/* executor */
#include <base/common.h>
#include <QuickDraw.h>
#include <CQuickDraw.h>
#include <MemoryMgr.h>
#include <DialogMgr.h>
#include <ControlMgr.h>
#include <MenuMgr.h>
#include <FileMgr.h>

#include <quickdraw/cquick.h>
#include <wind/wind.h>
#include <ctl/ctl.h>
#include <dial/itm.h>
#include <menu/menu.h>
#include <rsys/dump.h>
#include <util/string.h>
#include <mman/mman_private.h>
#include <textedit/textedit.h>

#include <print/print.h>

#define deref(x) toHost(*(x))

#define pmWindow(p) ((p)->pmWindow)
#define pmPrivate(p) ((p)->pmPrivate)
#define pmDevices(p) ((p)->pmDevices)
#define pmSeeds(p) ((p)->pmSeeds)

#define ciFlags(ci) ((ci)->ciFlags)
#define ciPrivate(ci) ((ci)->ciPrivate)

#else /* mac */

#define deref(x) (*(x))
#define x (x)
#define x (x)
#define x (x)
#define x (x)
#define x (x)
#define theCPort ((CGrafPtr)thePort)
#define CGrafPort_p(port) (((char *)(port))[6] & 0xC0)

#define ROWBYTES_VALUE_BITS (0x3FFF)

#define RECT_HEIGHT(r) ((r)->bottom - (r)->top)
#define RECT_WIDTH(r) ((r)->right - (r)->left)

/* window accessors */
#define WINDOW_PORT(wp) (&((WindowPeek)(wp))->port)
#define CWINDOW_PORT(wp) (&((WindowPeek)(wp))->port)

/* big endian byte order */
#define WINDOW_KIND(wp) (((WindowPeek)(wp))->windowKind)
#define WINDOW_VISIBLE(wp) (((WindowPeek)(wp))->visible)
#define WINDOW_HILITED(wp) (((WindowPeek)(wp))->hilited)
#define WINDOW_GO_AWAY_FLAG(wp) (((WindowPeek)(wp))->goAwayFlag)
#define WINDOW_SPARE_FLAG(wp) (((WindowPeek)(wp))->spareFlag)

#define WINDOW_STRUCT_REGION(wp) (((WindowPeek)(wp))->strucRgn)
#define WINDOW_CONT_REGION(wp) (((WindowPeek)(wp))->contRgn)
#define WINDOW_UPDATE_REGION(wp) (((WindowPeek)(wp))->updateRgn)
#define WINDOW_DEF_PROC(wp) (((WindowPeek)(wp))->windowDefProc)
#define WINDOW_DATA(wp) (((WindowPeek)(wp))->dataHandle)
#define WINDOW_TITLE(wp) (((WindowPeek)(wp))->titleHandle)
#define WINDOW_TITLE_WIDTH(wp) (((WindowPeek)(wp))->titleWidth)
#define WINDOW_CONTROL_LIST(wp) (((WindowPeek)(wp))->controlList)
#define WINDOW_NEXT_WINDOW(wp) (((WindowPeek)(wp))->nextWindow)
#define WINDOW_PIC(wp) (((WindowPeek)(wp))->windowPic)
#define WINDOW_REF_CON(wp) (((WindowPeek)(wp))->refCon)

/* native byte order */
#define WINDOW_KIND(wp) (WINDOW_KIND(wp))
#define WINDOW_VISIBLE(wp) (WINDOW_VISIBLE(wp))
#define WINDOW_HILITED(wp) (WINDOW_HILITED(wp))
#define WINDOW_GO_AWAY_FLAG(wp) (WINDOW_GO_AWAY_FLAG(wp))
#define WINDOW_SPARE_FLAG(wp) (WINDOW_SPARE_FLAG(wp))

#define WINDOW_STRUCT_REGION(wp) (WINDOW_STRUCT_REGION(wp))
#define WINDOW_CONT_REGION(wp) (WINDOW_CONT_REGION(wp))
#define WINDOW_UPDATE_REGION(wp) (WINDOW_UPDATE_REGION(wp))
#define WINDOW_DEF_PROC(wp) (WINDOW_DEF_PROC(wp))
#define WINDOW_DATA(wp) (WINDOW_DATA(wp))
#define WINDOW_TITLE(wp) (WINDOW_TITLE(wp))
#define WINDOW_TITLE_WIDTH(wp) (WINDOW_TITLE_WIDTH(wp))
#define WINDOW_CONTROL_LIST(wp) (WINDOW_CONTROL_LIST(wp))
#define WINDOW_NEXT_WINDOW(wp) (WINDOW_NEXT_WINDOW(wp))
#define WINDOW_PIC(wp) (WINDOW_PIC(wp))
#define WINDOW_REF_CON(wp) (WINDOW_REF_CON(wp))

/* dialog accessors */
#define DIALOG_WINDOW(dialog) ((WindowPtr) & ((DialogPeek)(dialog))->window)

#define DIALOG_ITEMS(dialog) (((DialogPeek)(dialog))->items)
#define DIALOG_TEXTH(dialog) (((DialogPeek)(dialog))->textH)
#define DIALOG_EDIT_FIELD(dialog) (((DialogPeek)(dialog))->editField)
#define DIALOG_EDIT_OPEN(dialog) (((DialogPeek)(dialog))->editOpen)
#define DIALOG_ADEF_ITEM(dialog) (((DialogPeek)(dialog))->aDefItem)

#define DIALOG_ITEMS(dialog) (DIALOG_ITEMS(dialog))
#define DIALOG_TEXTH(dialog) (DIALOG_TEXTH(dialog))
#define DIALOG_EDIT_FIELD(dialog) (DIALOG_EDIT_FIELD(dialog))
#define DIALOG_EDIT_OPEN(dialog) (DIALOG_EDIT_OPEN(dialog))
#define DIALOG_ADEF_ITEM(dialog) (DIALOG_ADEF_ITEM(dialog))

enum pixpat_pattern_types
{
    pixpat_type_orig = 0,
    pixpat_type_color = 1,
    pixpat_type_rgb = 2
};

#define pmWindow(p) (*(GrafPtr *)(&(p)->pmDataFields[0]))
#define pmPrivate(p) (*(short *)(&(p)->pmDataFields[2]))
#define pmDevices(p) (*(long *)(&(p)->pmDataFields[3]))
#define pmSeeds(p) (*(long *)(&(p)->pmDataFields[5]))

#define ciFlags(ci) (*(short *)(&(ci)->ciDataFields[0]))
#define ciPrivate(ci) (*(long *)(&(ci)->ciDataFields[1]))

#include "dump.h"
#endif /* mac */

#include <stdlib.h>
#include <stdio.h>

#include <string.h>

using namespace Executor;

FILE *Executor::o_fp = nullptr;

static int indent = 0;

std::string field_name = "";

/* dump everything recursively; but no ctab tables... */
enum dump_flags
{
    dump_normal_flag = 1,
    dump_bitmap_data_flag = 2,
    dump_ctab_flag = 4,
    dump_xfields_flag = 8,
    dump_pixpat_fields_flag = 16
};

int Executor::dump_verbosity = 1;

void dump_set_field(int field)
{
    dump_verbosity |= field;
}

void dump_clear_field(int field)
{
    dump_verbosity &= ~field;
}

#define dump_field(dump_fn, dump_arg, field) \
    do                                       \
    {                                        \
        field_name = field " ";              \
        dump_fn(dump_arg);                   \
        field_name = "";                     \
    } while(0)

#define iprintf(args)        \
    do                       \
    {                        \
        dump_spaces(indent); \
        fprintf args;        \
    } while(0)

Rect big_rect = { (INTEGER)-32767, (INTEGER)-32767, 32767, 32767 };

void Executor::dump_init(char *dst)
{
    if(dst)
    {
        o_fp = fopen(dst, "w");
        if(o_fp == nullptr)
            exit(1);
    }
    else
        o_fp = stderr;
}

void dump_finish(void)
{
    fflush(o_fp);
    if(!(o_fp == stderr))
        fclose(o_fp);
}

static void
dump_spaces(int nspaces)
{
    while(nspaces--)
        fputc(' ', o_fp);
    fflush(o_fp);
}

void Executor::dump_ptr_real(Ptr x)
{
    if(x)
        iprintf((o_fp, "%s%p[0x%lx]\n", field_name.c_str(), x,
                 (long)GetPtrSize(x)));
    else
        iprintf((o_fp, "%s%p\n", field_name.c_str(), x));
    fflush(o_fp);
}

void Executor::dump_handle_real(Handle x)
{
    if(x)
        iprintf((o_fp, "%s%p[0x%x] (%p)\n", field_name.c_str(), x,
                 GetHandleSize(x), deref(x)));
    else
        iprintf((o_fp, "%s%p\n", field_name.c_str(), x));
    fflush(o_fp);
}

void Executor::dump_rect(Rect *r)
{
    iprintf((o_fp, "%s(Rect *%p) {\n", field_name.c_str(), r));
    indent += 2;
    iprintf((o_fp, "top 0x%x; left 0x%x;\n", toHost(r->top), toHost(r->left)));
    iprintf((o_fp, "bottom 0x%x; right 0x%x; }\n",
             toHost(r->bottom), toHost(r->right)));
    indent -= 2;
    fflush(o_fp);
}

void Executor::dump_pattern(Pattern x)
{
    iprintf((o_fp, "%s0x%02x%02x%02x%02x%02x%02x%02x%02x;\n", field_name.c_str(),
             x.pat[0], x.pat[1], x.pat[2], x.pat[3],
             x.pat[4], x.pat[5], x.pat[6], x.pat[7]));
    fflush(o_fp);
}

void Executor::dump_point(GUEST<Point> x)
{
    iprintf((o_fp, "%s(Point) { v 0x%x; h 0x%x; }\n",
             field_name.c_str(), toHost(x.v), toHost(x.h)));
    fflush(o_fp);
}

void Executor::dump_bitmap_data(BitMap *x, int depth, Rect *rect)
{
    int rows, bytes_per_row;
    int row_bytes;
    int r, rb;
    char *addr;

    iprintf((o_fp, "...%s data\n", field_name.c_str()));
    indent += 2;

    if(!rect)
        rect = &x->bounds;

    row_bytes = x->rowBytes & ROWBYTES_VALUE_BITS;
    addr = (char *)&x->baseAddr[(rect->top - x->bounds.top) * row_bytes
                                    + ((rect->left - x->bounds.left)
                                       * depth)
                                        / 8];
    rows = RECT_HEIGHT(&x->bounds);
    bytes_per_row = (RECT_WIDTH(&x->bounds) * depth + 7) / 8;

    for(r = 0; r < rows; r++)
    {
        iprintf((o_fp, "%p: ", addr));
        for(rb = 0; rb < bytes_per_row; rb++)
        {
            char byte;
            static const char x_digit[] = {
                '0', '1', '2', '3', '4', '5', '6', '7',
                '8', '9', 'A', 'B', 'C', 'D', 'E', 'F',
            };

            byte = addr[rb];
            fprintf(o_fp, "%c%c",
                    x_digit[(byte >> 4) & 15], x_digit[byte & 15]);
        }
        fprintf(o_fp, "\n");
        addr += row_bytes;
    }
    indent -= 2;
    fflush(o_fp);
}

void Executor::dump_bits16(GUEST<Bits16> data)
{
    if(dump_verbosity >= 3)
    {
        BitMap x;

        x.baseAddr = (Ptr)data;
        x.rowBytes = 2;
        x.bounds.top = x.bounds.left = 0;
        x.bounds.bottom = 16;
        x.bounds.right = 16;

        dump_bitmap_data(&x, 1, nullptr);
    }
    else
        iprintf((o_fp, "[%s field omitted]\n", field_name.c_str()));
    fflush(o_fp);
}

void Executor::dump_bitmap(BitMap *x, Rect *rect)
{
    iprintf((o_fp, "%s(BitMap *%p) {\n", field_name.c_str(), x));
    indent += 2;
    iprintf((o_fp, "baseAddr %p;\n", toHost(x->baseAddr)));
    if(dump_verbosity >= 3)
        dump_bitmap_data(x, 1, rect);
    iprintf((o_fp, "rowBytes 0x%hx;\n", (unsigned short)x->rowBytes));
    dump_field(dump_rect, &x->bounds, "bounds");
    indent -= 2;
    iprintf((o_fp, "}\n"));
    fflush(o_fp);
}

void Executor::dump_bitmap_null_rect(BitMap *x)
{
    dump_bitmap(x, nullptr);
}

void Executor::dump_grafport(GrafPtr x)
{
    if(CGrafPort_p(x))
        dump_cgrafport_real((CGrafPtr)x);
    else
        dump_grafport_real(x);
    fflush(o_fp);
}

void Executor::dump_qdprocs(QDProcsPtr x)
{
    iprintf((o_fp, "%s(QDProcsPtr *%p) {\n", field_name.c_str(), x));
    indent += 2;
    if(x != nullptr)
    {
        iprintf((o_fp, "textProc    %p;\n", (ProcPtr)x->textProc));
        iprintf((o_fp, "lineProc    %p;\n", (ProcPtr)x->lineProc));
        iprintf((o_fp, "rectProc    %p;\n", (ProcPtr)x->rectProc));
        iprintf((o_fp, "rRectProc   %p;\n", (ProcPtr)x->rRectProc));
        iprintf((o_fp, "ovalProc    %p;\n", (ProcPtr)x->ovalProc));
        iprintf((o_fp, "arcProc     %p;\n", (ProcPtr)x->arcProc));
        iprintf((o_fp, "polyProc    %p;\n", (ProcPtr)x->polyProc));
        iprintf((o_fp, "rgnProc     %p;\n", (ProcPtr)x->rgnProc));
        iprintf((o_fp, "bitsProc    %p;\n", (ProcPtr)x->bitsProc));
        iprintf((o_fp, "commentProc %p;\n", (ProcPtr)x->commentProc));
        iprintf((o_fp, "txMeasProc  %p;\n", (ProcPtr)x->txMeasProc));
        iprintf((o_fp, "getPicProc  %p;\n", (ProcPtr)x->getPicProc));
        iprintf((o_fp, "putPicProc  %p;\n", (ProcPtr)x->putPicProc));
    }
    else
        iprintf((o_fp, "<default grafprocs used>\n"));
    indent -= 2;
    iprintf((o_fp, "}\n"));
    fflush(o_fp);
}

void Executor::dump_grafport_real(GrafPtr x)
{
    iprintf((o_fp, "%s(GrafPort *%p) {\n", field_name.c_str(), x));
    indent += 2;
    iprintf((o_fp, "device %d;\n", toHost(x->device)));
    dump_field(dump_bitmap_null_rect, &x->portBits, "portBits");
    dump_field(dump_rect, &x->portRect, "portRect");
    dump_field(dump_handle, x->visRgn, "visRgn");
    dump_field(dump_handle, x->clipRgn, "clipRgn");
    dump_field(dump_pattern, x->bkPat, "bkPat");
    dump_field(dump_pattern, x->fillPat, "fillPat");
    dump_field(dump_point, x->pnLoc, "pnLoc");
    dump_field(dump_point, x->pnSize, "pnSize");
    iprintf((o_fp, "pnMode %d;\n", toHost(x->pnMode)));
    dump_field(dump_pattern, x->pnPat, "pnPat");
    iprintf((o_fp, "pnVis %d;\n", toHost(x->pnVis)));
    iprintf((o_fp, "txFont %d;\n", toHost(x->txFont)));
    iprintf((o_fp, "txFace %d;\n", x->txFace));
    iprintf((o_fp, "txMode %d;\n", toHost(x->txMode)));
    iprintf((o_fp, "txSize %d;\n", toHost(x->txSize)));
    iprintf((o_fp, "spExtra %d;\n", toHost(x->spExtra)));
    iprintf((o_fp, "fgColor 0x%x;\n", toHost(x->fgColor)));
    iprintf((o_fp, "bkColor 0x%x;\n", toHost(x->bkColor)));
    iprintf((o_fp, "colrBit %d;\n", toHost(x->colrBit)));
    iprintf((o_fp, "patStretch %d;\n", toHost(x->patStretch)));
    dump_field(dump_handle, x->picSave, "picSave");
    dump_field(dump_handle, x->rgnSave, "rgnSave");
    dump_field(dump_handle, x->polySave, "polySave");
    dump_field(dump_qdprocs, x->grafProcs, "grafProcs");
    indent -= 2;
    iprintf((o_fp, "}\n"));
    fflush(o_fp);
}

GrafPtr
theport(void)
{
    return qdGlobals().thePort;
}

void Executor::dump_theport(void)
{
    dump_grafport(qdGlobals().thePort);
}

void Executor::dump_rgb_color(RGBColor *x)
{
    iprintf((o_fp, "%s(RGBColor) { red 0x%lx; green 0x%lx, blue 0x%lx; }\n",
             field_name.c_str(),
             (long)x->red,
             (long)x->green,
             (long)x->blue));
    fflush(o_fp);
}

void Executor::dump_ctab(CTabHandle ctab)
{
    CTabPtr x = deref(ctab);

    iprintf((o_fp, "%s(ColorTable **%p) {\n", field_name.c_str(), ctab));
    indent += 2;
    iprintf((o_fp, "ctSeed 0x%x;\n", toHost(x->ctSeed)));
    iprintf((o_fp, "ctFlags 0x%x;\n", toHost(x->ctFlags)));
    iprintf((o_fp, "ctSize %d;\n", toHost(x->ctSize)));
    if(dump_verbosity >= 2)
    {
        int i;

        iprintf((o_fp, "ctTable\n"));
        for(i = 0; i <= x->ctSize; i++)
        {
            iprintf((o_fp, "%d:[0x%x] { 0x%lx, 0x%lx, 0x%lx }\n",
                     i, toHost(x->ctTable[i].value),
                     (long)x->ctTable[i].rgb.red,
                     (long)x->ctTable[i].rgb.green,
                     (long)x->ctTable[i].rgb.blue));
        }
        indent -= 2;
        iprintf((o_fp, "}\n"));
    }
    else
    {
        iprintf((o_fp, "[ctTable field omitted]; }\n"));
        indent -= 2;
    }
    fflush(o_fp);
}

void Executor::dump_itab(ITabHandle itab)
{
    ITabPtr x = deref(itab);

    iprintf((o_fp, "%s(ITab **%p) {\n", field_name.c_str(), itab));
    indent += 2;
    iprintf((o_fp, "iTabSeed 0x%x;\n", toHost(x->iTabSeed)));
    iprintf((o_fp, "iTabRes %d;\n", toHost(x->iTabRes)));

    /* we always omit the inverse table... */
    iprintf((o_fp, "[iTTable field omitted]; }\n"));
    indent -= 2;
    fflush(o_fp);
}

void Executor::dump_pixpat(PixPatHandle pixpat)
{
    PixPatPtr x = deref(pixpat);

    iprintf((o_fp, "%s(PixPat **%p) {\n", field_name.c_str(), pixpat));
    indent += 2;
    iprintf((o_fp, "patType %s;\n",
             x->patType == pixpat_old_style_pattern
                 ? "old_style_pattern"
                 : (x->patType == pixpat_color_pattern
                        ? "color_pattern"
                        : (x->patType == pixpat_rgb_pattern
                               ? "rgb_pattern"
                               : "<unknown!>"))));

    if(x->patType != pixpat_type_orig)
    {
        if(dump_verbosity
           && x->patMap)
            dump_field(dump_pixmap_null_rect, x->patMap, "patMap");
        else
            dump_field(dump_handle, x->patMap, "patMap");
        dump_field(dump_handle, x->patData, "patData");
    }
    else
    {
        iprintf((o_fp, "[pat{Map, Data} field omitted]; }\n"));
    }
    dump_field(dump_handle, x->patXData, "patXData");
    iprintf((o_fp, "patXValid %d;\n", toHost(x->patXValid)));
    if(dump_verbosity
       && x->patXMap
       && !x->patXValid)
        dump_field(dump_pixmap_null_rect, (PixMapHandle)x->patXMap,
                   "patXMap");
    else
        dump_field(dump_handle, x->patXMap, "patXMap");
    dump_field(dump_pattern, x->pat1Data, "pat1Data");
    indent -= 2;
    iprintf((o_fp, "}\n"));
    fflush(o_fp);
}

void dump_pixmap_ptr(PixMapPtr x, Rect *rect)
{
    iprintf((o_fp, "%s(PixMap *%p) {\n", field_name.c_str(), x));
    indent += 2;
    iprintf((o_fp, "baseAddr %p;\n", toHost(x->baseAddr)));
    if(dump_verbosity >= 3
       && x->baseAddr)
    {
        if(!rect)
            rect = &x->bounds;
        dump_bitmap_data((BitMap *)x, x->pixelSize, rect);
    }
    iprintf((o_fp, "rowBytes 0x%hx;\n", (unsigned short)x->rowBytes));
    dump_field(dump_rect, &x->bounds, "bounds");
    iprintf((o_fp, "pmVersion 0x%x;\n", toHost(x->pmVersion)));
    iprintf((o_fp, "packType 0x%x;\n", toHost(x->packType)));
    iprintf((o_fp, "packSize 0x%x;\n", toHost(x->packSize)));
    iprintf((o_fp, "hRes 0x%x, vRes 0x%x;\n",
             toHost(x->hRes), toHost(x->vRes)));
    iprintf((o_fp, "pixelType 0x%x;\n", toHost(x->pixelType)));
    iprintf((o_fp, "pixelSize %d;\n", toHost(x->pixelSize)));
    iprintf((o_fp, "cmpCount %d;\n", toHost(x->cmpCount)));
    iprintf((o_fp, "cmpSize %d;\n", toHost(x->cmpSize)));
    iprintf((o_fp, "planeBytes 0x%x;\n", toHost(x->planeBytes)));
    if(dump_verbosity
       && x->pmTable)
        dump_field(dump_ctab, x->pmTable, "pmTable");
    else
        dump_field(dump_handle, x->pmTable, "pmTable");
    iprintf((o_fp, "[Reserved field omitted]; }\n"));
    indent -= 2;
    fflush(o_fp);
}

void Executor::dump_pixmap_null_rect(PixMapHandle pixmap)
{
    dump_pixmap(pixmap, nullptr);
}

void Executor::dump_pixmap(PixMapHandle pixmap, Rect *rect)
{
    PixMapPtr x = deref(pixmap);

    iprintf((o_fp, "%s(PixMap **%p) {\n", field_name.c_str(), pixmap));
    indent += 2;
    iprintf((o_fp, "baseAddr %p;\n", toHost(x->baseAddr)));
    if(dump_verbosity >= 3
       && x->baseAddr)
    {
        if(!rect)
            rect = &x->bounds;
        dump_bitmap_data((BitMap *)x, x->pixelSize, rect);
    }
    iprintf((o_fp, "rowBytes 0x%hx;\n", (unsigned short)x->rowBytes));
    dump_field(dump_rect, &x->bounds, "bounds");
    iprintf((o_fp, "pmVersion 0x%x;\n", toHost(x->pmVersion)));
    iprintf((o_fp, "packType 0x%x;\n", toHost(x->packType)));
    iprintf((o_fp, "packSize 0x%x;\n", toHost(x->packSize)));
    iprintf((o_fp, "hRes 0x%x, vRes 0x%x;\n",
             toHost(x->hRes), toHost(x->vRes)));
    iprintf((o_fp, "pixelType 0x%x;\n", toHost(x->pixelType)));
    iprintf((o_fp, "pixelSize %d;\n", toHost(x->pixelSize)));
    iprintf((o_fp, "cmpCount %d;\n", toHost(x->cmpCount)));
    iprintf((o_fp, "cmpSize %d;\n", toHost(x->cmpSize)));
    iprintf((o_fp, "planeBytes 0x%x;\n", toHost(x->planeBytes)));
    if(dump_verbosity
       && x->pmTable)
        dump_field(dump_ctab, x->pmTable, "pmTable");
    else
        dump_field(dump_handle, x->pmTable, "pmTable");
    iprintf((o_fp, "[Reserved field omitted]; }\n"));
    indent -= 2;
    fflush(o_fp);
}

void Executor::dump_cqdprocs(CQDProcsPtr x)
{
    iprintf((o_fp, "%s(CQDProcsPtr *%p) {\n", field_name.c_str(), x));
    indent += 2;
    if(x != nullptr)
    {
        iprintf((o_fp, "textProc     %p;\n", (ProcPtr)x->textProc));
        iprintf((o_fp, "lineProc     %p;\n", (ProcPtr)x->lineProc));
        iprintf((o_fp, "rectProc     %p;\n", (ProcPtr)x->rectProc));
        iprintf((o_fp, "rRectProc    %p;\n", (ProcPtr)x->rRectProc));
        iprintf((o_fp, "ovalProc     %p;\n", (ProcPtr)x->ovalProc));
        iprintf((o_fp, "arcProc      %p;\n", (ProcPtr)x->arcProc));
        iprintf((o_fp, "polyProc     %p;\n", (ProcPtr)x->polyProc));
        iprintf((o_fp, "rgnProc      %p;\n", (ProcPtr)x->rgnProc));
        iprintf((o_fp, "bitsProc     %p;\n", (ProcPtr)x->bitsProc));
        iprintf((o_fp, "commentProc  %p;\n", (ProcPtr)x->commentProc));
        iprintf((o_fp, "txMeasProc   %p;\n", (ProcPtr)x->txMeasProc));
        iprintf((o_fp, "getPicProc   %p;\n", (ProcPtr)x->getPicProc));
        iprintf((o_fp, "putPicProc   %p;\n", (ProcPtr)x->putPicProc));
        iprintf((o_fp, "newProc1Proc %p;\n", (ProcPtr)x->newProc1Proc));
        iprintf((o_fp, "newProc2Proc %p;\n", (ProcPtr)x->newProc2Proc));
        iprintf((o_fp, "newProc3Proc %p;\n", (ProcPtr)x->newProc3Proc));
        iprintf((o_fp, "newProc4Proc %p;\n", (ProcPtr)x->newProc4Proc));
        iprintf((o_fp, "newProc5Proc %p;\n", (ProcPtr)x->newProc5Proc));
        iprintf((o_fp, "newProc6Proc %p;\n", (ProcPtr)x->newProc6Proc));
    }
    else
        iprintf((o_fp, "<default grafprocs used>\n"));
    indent -= 2;
    iprintf((o_fp, "}\n"));
    fflush(o_fp);
}

void Executor::dump_cgrafport_real(CGrafPtr x)
{
    iprintf((o_fp, "%s(CGrafPort *%p) {\n", field_name.c_str(), x));
    indent += 2;
    iprintf((o_fp, "device 0x%x;\n", toHost(x->device)));
    if(dump_verbosity
       && x->portPixMap)
        dump_field(dump_pixmap_null_rect, x->portPixMap, "portPixMap");
    else
        dump_field(dump_handle, x->portPixMap, "portPixMap");
    iprintf((o_fp, "portVersion 0x%x;\n", toHost(x->portVersion)));
    dump_field(dump_handle, x->grafVars, "grafVars");
    iprintf((o_fp, "chExtra %d;\n", toHost(x->chExtra)));
    iprintf((o_fp, "pnLocHFrac 0x%x;\n", toHost(x->pnLocHFrac)));
    dump_field(dump_rect, &x->portRect, "portRect");
    dump_field(dump_handle, x->visRgn, "visRgn");
    dump_field(dump_handle, x->clipRgn, "clipRgn");
    if(dump_verbosity
       && x->bkPixPat)
        dump_field(dump_pixpat, x->bkPixPat, "bkPixPat");
    else
        dump_field(dump_handle, x->bkPixPat, "bkPixPat");
    dump_field(dump_rgb_color, &x->rgbFgColor, "rgbFgColor");
    dump_field(dump_rgb_color, &x->rgbBkColor, "rgbBkColor");
    dump_field(dump_point, x->pnLoc, "pnLoc");
    dump_field(dump_point, x->pnSize, "pnSize");
    iprintf((o_fp, "pnMode %d;\n", toHost(x->pnMode)));
    if(dump_verbosity
       && x->pnPixPat)
        dump_field(dump_pixpat, x->pnPixPat, "pnPixPat");
    else
        dump_field(dump_handle, x->pnPixPat, "pnPixPat");
    if(dump_verbosity
       && x->fillPixPat)
        dump_field(dump_pixpat, x->fillPixPat, "fillPixPat");
    else
        dump_field(dump_handle, x->fillPixPat, "fillPixPat");
    iprintf((o_fp, "pnVis %d;\n", toHost(x->pnVis)));
    iprintf((o_fp, "txFont %d;\n", toHost(x->txFont)));
    iprintf((o_fp, "txFace %d;\n", x->txFace));
    iprintf((o_fp, "txMode %d;\n", toHost(x->txMode)));
    iprintf((o_fp, "txSize %d;\n", toHost(x->txSize)));
    iprintf((o_fp, "spExtra %d;\n", toHost(x->spExtra)));
    iprintf((o_fp, "fgColor 0x%x;\n", toHost(x->fgColor)));
    iprintf((o_fp, "bkColor 0x%x;\n", toHost(x->bkColor)));
    iprintf((o_fp, "colrBit %d;\n", toHost(x->colrBit)));
    iprintf((o_fp, "patStretch %x;\n", toHost(x->patStretch)));
    dump_field(dump_handle, x->picSave, "picSave");
    dump_field(dump_handle, x->rgnSave, "rgnSave");
    dump_field(dump_handle, x->polySave, "polySave");
    dump_field(dump_cqdprocs, x->grafProcs, "grafProcs");
    indent -= 2;
    iprintf((o_fp, "}\n"));
    fflush(o_fp);
}

void Executor::dump_gdevice(GDHandle gdev)
{
    GDPtr x = deref(gdev);
    SProcHndl proc;

    iprintf((o_fp, "%s(GDevice **%p) {\n", field_name.c_str(), gdev));
    indent += 2;
    iprintf((o_fp, "gdID 0x%x;\n", toHost(x->gdID)));
    iprintf((o_fp, "gdType 0x%x;\n", toHost(x->gdType)));
    if(dump_verbosity
       && x->gdITable)
        dump_field(dump_itab, x->gdITable, "gdITable");
    else
        dump_field(dump_handle, x->gdITable, "gdITable");
    iprintf((o_fp, "gdResPref 0x%x;\n", toHost(x->gdResPref)));
#if 0
  dump_field (dump_handle, x->gdSearchProc, "gdSearchProc");
#else
    iprintf((o_fp, "gdSearchProc %p;\n", toHost(x->gdSearchProc)));
    for(proc = x->gdSearchProc; proc; proc = (*proc)->nxtSrch)
        iprintf((o_fp, "  proc [%p, %p]; %p\n",
                 proc, toHost((*proc)->nxtSrch), toHost((*proc)->srchProc)));
#endif
    dump_field(dump_handle, x->gdCompProc, "gdCompProc");
    iprintf((o_fp, "gdFlags 0x%hx;\n", (unsigned short)x->gdFlags));
    if(dump_verbosity
       && x->gdPMap)
        dump_field(dump_pixmap_null_rect, x->gdPMap, "gdPMap");
    else
        dump_field(dump_handle, x->gdPMap, "gdPMap");
    iprintf((o_fp, "gdRefCon 0x%x;\n", toHost(x->gdRefCon)));
    if(dump_verbosity
       && x->gdNextGD)
        dump_field(dump_gdevice, (GDHandle)x->gdNextGD, "gdNextGD");
    else
        dump_field(dump_handle, x->gdNextGD, "gdNextGD");
    dump_field(dump_rect, &x->gdRect, "gdRect");
    iprintf((o_fp, "gdMode 0x%x;\n", toHost(x->gdMode)));
    iprintf((o_fp, "[CC, Reserved fields omitted]; }\n"));
    indent -= 2;
    fflush(o_fp);
}

void Executor::dump_thegdevice(void)
{
    dump_gdevice(LM(TheGDevice));
}

void Executor::dump_maindevice(void)
{
    dump_gdevice(LM(MainDevice));
}

void Executor::dump_string(unsigned char *s)
{
    /* pascal string */
    unsigned char t[256];
    int len;

    len = *s;
    strncpy((char *)t, (char *)&s[1], len);
    t[len] = '\0';
    iprintf((o_fp, "%s %p \"%s\";\n",
             field_name.c_str(), s, t));
}

void Executor::dump_palette(PaletteHandle palette)
{
    PalettePtr x = deref(palette);

    iprintf((o_fp, "%s(PaletteHandle **%p) {\n", field_name.c_str(), palette));
    indent += 2;
    iprintf((o_fp, "pmEntries 0x%x;\n", toHost(x->pmEntries)));
    if(pmWindow(x)
       && dump_verbosity >= 2
       && 0)
        dump_grafport((GrafPtr)pmWindow(x));
    else
        dump_field(dump_handle, pmWindow(x), "pmWindow");
    iprintf((o_fp, "pmPrivate 0x%x\n", toHost(pmPrivate(x))));
    iprintf((o_fp, "pmDevices 0x%x\n", toHost(pmDevices(x))));
    iprintf((o_fp, "pmSeeds %p\n", toHost(pmSeeds(x))));
    if(dump_verbosity >= 2)
    {
        int i;

        iprintf((o_fp, "pmInfo\n"));
        for(i = 0; i < x->pmEntries; i++)
        {
            iprintf((o_fp, "%3x { rgb { 0x%lx, 0x%lx, 0x%lx }\n",
                     i,
                     (long)x->pmInfo[i].ciRGB.red,
                     (long)x->pmInfo[i].ciRGB.green,
                     (long)x->pmInfo[i].ciRGB.blue));
            iprintf((o_fp, "      usage 0x%x; tolerance 0x%x;\n",
                     toHost(x->pmInfo[i].ciUsage),
                     toHost(x->pmInfo[i].ciTolerance)));
            iprintf((o_fp, "      flags 0x%x; private 0x%lx; };\n",
                     toHost(ciFlags(&x->pmInfo[i])),
                     (unsigned long)ciPrivate(&x->pmInfo[i])));
        }
        indent -= 2;
        iprintf((o_fp, "}\n"));
    }
    else
    {
        indent -= 2;
        iprintf((o_fp, "[pmInfo field omitted]; }\n"));
    }
    fflush(o_fp);
}

void Executor::dump_ccrsr(CCrsrHandle ccrsr)
{
    CCrsrPtr x = deref(ccrsr);

    iprintf((o_fp, "%s(CCrsrHandle **%p) {\n", field_name.c_str(), ccrsr));
    indent += 2;
    iprintf((o_fp, "crsrType 0x%hx;\n", toHost(x->crsrType)));
    if(x->crsrMap
       && dump_verbosity >= 1)
        dump_field(dump_pixmap_null_rect, x->crsrMap, "crsrMap");
    else
        dump_field(dump_handle, x->crsrMap, "crsrMap");
    dump_field(dump_handle, x->crsrData, "crsrData");
    if(dump_verbosity >= 3
       && x->crsrXData
       && x->crsrXValid != 0)
    {
        BitMap bm;
        int depth;
        /* dump the expanded pixel data */

        depth = x->crsrXValid;
        bm.baseAddr = deref(x->crsrXData);
        bm.rowBytes = 2 * depth;
        bm.bounds.top = bm.bounds.left = 0;
        bm.bounds.bottom = 16;
        bm.bounds.right = 16;

        field_name = "crsrXData";
        dump_bitmap_data(&bm, depth, nullptr);
        field_name = "";
    }
    else
        dump_field(dump_handle, x->crsrXData, "crsrXData");
    iprintf((o_fp, "crsrXValid %d;\n", toHost(x->crsrXValid)));
    dump_field(dump_handle, x->crsrXHandle, "crsrXHandle");
    dump_field(dump_bits16, x->crsr1Data, "crsr1Data");
    dump_field(dump_bits16, x->crsrMask, "crsrMask");
    dump_field(dump_point, x->crsrHotSpot, "crsrHotSpot");
    iprintf((o_fp, "crsrXTable %x\n", toHost(x->crsrXTable)));
    iprintf((o_fp, "crsrID 0x%x; }\n", toHost(x->crsrID)));
    indent -= 2;
    fflush(o_fp);
}

void Executor::dump_wmgrport(void)
{
    dump_grafport(LM(WMgrPort));
}

void Executor::dump_wmgrcport(void)
{
    dump_grafport((GrafPtr)LM(WMgrCPort));
}

void Executor::dump_string_handle(StringHandle sh)
{
    /* pascal string */
    unsigned char *s, t[256];
    int size, len;

    s = deref(sh);
    size = GetHandleSize((Handle)sh);
    len = *s;
    strncpy((char *)t, (char *)&s[1], len);
    t[len] = '\0';
    iprintf((o_fp, "%s %p[%d] \"%s\";\n",
             field_name.c_str(), sh, size, t));
}

void Executor::dump_window_peek(WindowPeek w)
{
    iprintf((o_fp, "%s(WindowPeek %p) {\n", field_name.c_str(), w));
    indent += 2;

    dump_field(dump_grafport, WINDOW_PORT(w), "port");
    iprintf((o_fp, "windowKind %lx;\n", (long)WINDOW_KIND(w)));
    iprintf((o_fp, "visible %x;\n", WINDOW_VISIBLE(w)));
    iprintf((o_fp, "hilited %x;\n", WINDOW_HILITED(w)));
    iprintf((o_fp, "goAwayFlag %x;\n", WINDOW_GO_AWAY_FLAG(w)));
    iprintf((o_fp, "spareFlag %x;\n", WINDOW_SPARE_FLAG(w)));

    dump_field(dump_handle, WINDOW_STRUCT_REGION(w), "strucRgn");
    dump_field(dump_handle, WINDOW_CONT_REGION(w), "contRgn");
    dump_field(dump_handle, WINDOW_UPDATE_REGION(w), "updateRgn");

    dump_field(dump_handle, WINDOW_DEF_PROC(w), "windowDefProc");
    dump_field(dump_handle, WINDOW_DATA(w), "dataHandle");
    dump_field(dump_string_handle, WINDOW_TITLE(w), "titleHandle");

    iprintf((o_fp, "titleWidth %lx;\n", (long)WINDOW_TITLE_WIDTH(w)));

    dump_field(dump_handle, WINDOW_CONTROL_LIST(w), "controlList");
    dump_field(dump_ptr, WINDOW_NEXT_WINDOW(w), "nextWindow");
    dump_field(dump_handle, WINDOW_PIC(w), "picHandle");

    iprintf((o_fp, "refCon %lx; }\n", (long)WINDOW_REF_CON(w)));
    indent -= 2;

    fflush(o_fp);
}

void Executor::dump_dialog_peek(DialogPeek d)
{
    iprintf((o_fp, "%s(DialogPeek %p) {\n", field_name.c_str(), d));
    indent += 2;

    dump_field(dump_window_peek, (WindowPeek)DIALOG_WINDOW(d), "window");
    dump_field(dump_handle, DIALOG_ITEMS(d), "items");
    dump_field(dump_handle, DIALOG_TEXTH(d), "textH");
    iprintf((o_fp, "editField 0x%x;\n", toHost(DIALOG_EDIT_FIELD(d))));
    iprintf((o_fp, "editOpen 0x%x;\n", toHost(DIALOG_EDIT_OPEN(d))));
    iprintf((o_fp, "aDefItem 0x%x; }\n", toHost(DIALOG_ADEF_ITEM(d))));
    indent -= 2;

    fflush(o_fp);
}

void dump_dialog_items(DialogPeek dp)
{
    GUEST<int16_t> *item_data;
    int n_items;
    itmp items;
    int i;

    item_data = (GUEST<int16_t> *)*DIALOG_ITEMS(dp);
    n_items = *item_data;
    items = (itmp)&item_data[1];
    fprintf(o_fp, "%d items:\n", n_items);
    for(i = 0; i < n_items; i++)
    {
        itmp item;

        item = items;
        fprintf(o_fp, "item %d; type %d, hand %p, (%d, %d, %d, %d)\n",
                i, (int)(item->itmtype),
                toHost(item->itmhand),
                toHost(item->itmr.top), toHost(item->itmr.left),
                toHost(item->itmr.bottom), toHost(item->itmr.right));
        BUMPIP(items);
    }
}

void Executor::dump_aux_win_list(void)
{
    AuxWinHandle t;

    for(t = LM(AuxWinHead); t; t = deref(t)->awNext)
    {
        dump_aux_win(t);
    }
}

void Executor::dump_aux_win(AuxWinHandle awh)
{
    AuxWinPtr aw = deref(awh);

    iprintf((o_fp, "%s(AuxWinHandle **%p) {\n", field_name.c_str(), awh));
    indent += 2;
    iprintf((o_fp, "awNext %p;\n", toHost(aw->awNext)));
    iprintf((o_fp, "awOwner %p;\n", toHost(aw->awOwner)));
    dump_field(dump_ctab, aw->awCTable, "awCTable");
    dump_field(dump_handle, aw->dialogCItem, "dialogCItem");
    iprintf((o_fp, "awFlags 0x%lx;\n", (long)aw->awFlags));
    iprintf((o_fp, "awReserved %p;\n", toHost(aw->awReserved)));
    iprintf((o_fp, "awRefCon 0x%lx; }\n", (long)aw->awRefCon));
    indent -= 2;

    fflush(o_fp);
}

void Executor::dump_window_list(WindowPeek w)
{
    WindowPeek t_w;
    WindowPeek front_w;

    front_w = (WindowPeek)FrontWindow();
    for(t_w = LM(WindowList);
        t_w;
        t_w = WINDOW_NEXT_WINDOW(t_w))
    {
        fprintf(o_fp, "%p", t_w);
        if(t_w == w)
            fprintf(o_fp, " arg");
        if(t_w == front_w)
            fprintf(o_fp, " front");
        fprintf(o_fp, "\n");
        dump_string_handle(WINDOW_TITLE(t_w));
    }
    fflush(o_fp);
}

void Executor::dump_rgn(RgnHandle rgn)
{
    RgnPtr x;

    iprintf((o_fp, "%s(RgnHandle **%p) {\n", field_name.c_str(), rgn));
    indent += 2;
    x = deref(rgn);
    iprintf((o_fp, "rgnSize %d\n", toHost(x->rgnSize)));
    dump_field(dump_rect, &x->rgnBBox, "rgnBBox");
    iprintf((o_fp, "[special data omitted]; }\n"));
    indent -= 2;
    fflush(o_fp);
}

void dump_menu_info(MenuHandle x)
{
    dump_field(dump_handle, x, "MenuHandle");
    iprintf((o_fp, "%s(MenuHandle **%p) {\n", field_name.c_str(), x));
    indent += 2;
    iprintf((o_fp, "menuID %d\n", toHost(MI_ID(x))));
    iprintf((o_fp, "menuWidth %d\n", toHost(MI_WIDTH(x))));
    iprintf((o_fp, "menuHeight %d\n", toHost(MI_HEIGHT(x))));
    dump_field(dump_handle, MI_PROC(x), "menuProc");
    iprintf((o_fp, "enableFlags %x\n", toHost(MI_ENABLE_FLAGS(x))));
    dump_field(dump_string, MI_DATA(x), "menuTitle");

    indent += 2;
    {
        unsigned char *p;

        p = MI_DATA(x) + ((unsigned char *)MI_DATA(x))[0] + 1;
        while(*p)
        {
            dump_field(dump_string, p, "item name");
            p += p[0] + 1;
            iprintf((o_fp, "icon = 0x%02x, keyeq = 0x%02x, marker = 0x%02x,"
                           " style = 0x%02x\n",
                     p[0], p[1], p[2], p[3]));
            p += 4;
        }
        indent -= 2;
        iprintf((o_fp, "total chars = %ld\n", (long)(p - (unsigned char *)*x)));
    }
    indent -= 2;
}

void dump_control_list(WindowPeek w)
{
    ControlHandle x;

    for(x = WINDOW_CONTROL_LIST(w); x; x = CTL_NEXT_CONTROL(x))
    {
        char buf[256];
        int len;

        len = *CTL_TITLE(x);
        strncpy(buf, (char *)&CTL_TITLE(x)[1], len);
        buf[len] = '\0';

        fprintf(o_fp, "%p %s\n", x, buf);
    }
}

void dump_control(ControlHandle x)
{
    iprintf((o_fp, "%s(ControlHandle **%p) {\n", field_name.c_str(), x));
    indent += 2;

    dump_field(dump_handle, CTL_NEXT_CONTROL(x), "nextControl");
    dump_field(dump_ptr, CTL_OWNER(x), "contrlOwner");
    dump_field(dump_rect, &CTL_RECT(x), "contrlRect");
    iprintf((o_fp, "contrlVis %d\n", (int)CTL_VIS(x)));
    iprintf((o_fp, "contrlHilite %d\n", (int)CTL_HILITE(x)));

    iprintf((o_fp, "contrlValue %d\n", (int)CTL_VALUE(x)));
    iprintf((o_fp, "contrlMin %d\n", (int)CTL_MIN(x)));
    iprintf((o_fp, "contrlMax %d\n", (int)CTL_MAX(x)));

    dump_field(dump_handle, CTL_DEFPROC(x), "contrlDefProc");
    dump_field(dump_ptr, CTL_DATA(x), "contrlData");

    iprintf((o_fp, "contrlAction %p\n", (ProcPtr)CTL_ACTION(x)));
    iprintf((o_fp, "contrlRfCon %d\n", (int)CTL_REF_CON(x)));
    dump_field(dump_string, CTL_TITLE(x), "contrlTitle");
    indent -= 2;
    iprintf((o_fp, "}\n"));
}

void dump_memlocs(uint32_t to_look_for, int size, const void *start_addr,
                  const void *end_addr)
{
    const uint8_t *ucp;
    const uint16_t *usp;
    const uint32_t *ulp;

    switch(size)
    {
        case 1:
            for(ucp = (const uint8_t *)start_addr; ucp < (uint8_t *)end_addr; ++ucp)
                if(*ucp == (uint8_t)to_look_for)
                    iprintf((o_fp, "%p\n", ucp));
            break;
        case 2:
            for(usp = (const uint16_t *)start_addr; usp < (uint16_t *)end_addr; ++usp)
                if(*usp == (uint16_t)to_look_for)
                    iprintf((o_fp, "%p\n", usp));
            break;
        case 4:
            for(ulp = (const uint32_t *)start_addr; ulp < (uint32_t *)end_addr;
                ulp = (const uint32_t *)((char *)ulp + 2))
                if(*ulp == to_look_for)
                    iprintf((o_fp, "%p\n", ulp));
            break;
        default:
            iprintf((o_fp, "Bad Size\n"));
            break;
    }
}

void dump_te(TEHandle te)
{
    int i;

    iprintf((o_fp, "%s(TEHandle **%p) {\n", field_name.c_str(), te));
    indent += 2;

    dump_field(dump_rect, &TE_DEST_RECT(te), "destRect");
    dump_field(dump_rect, &TE_VIEW_RECT(te), "viewRect");
    dump_field(dump_rect, &TE_SEL_RECT(te), "selRect");

    iprintf((o_fp, "lineHeight %d\n", toHost(TE_LINE_HEIGHT(te))));
    iprintf((o_fp, "fontAscent %d\n", toHost(TE_FONT_ASCENT(te))));
    iprintf((o_fp, "selStart %d\n", toHost(TE_SEL_START(te))));
    iprintf((o_fp, "selEnd %d\n", toHost(TE_SEL_END(te))));
    iprintf((o_fp, "caretState %d\n", toHost(TE_CARET_STATE(te))));

    iprintf((o_fp, "just %d\n", toHost(TE_JUST(te))));
    iprintf((o_fp, "teLength %d\n", toHost(TE_LENGTH(te))));

    iprintf((o_fp, "txFont %d\n", toHost(TE_TX_FONT(te))));
    iprintf((o_fp, "txFace %d\n", TE_TX_FACE(te)));
    iprintf((o_fp, "txMode %d\n", toHost(TE_TX_MODE(te))));
    iprintf((o_fp, "txSize %d\n", toHost(TE_TX_SIZE(te))));

    iprintf((o_fp, "nLines %d\n", toHost(TE_N_LINES(te))));

    {
        char buf[40000];
        int16_t length;

        length = TE_LENGTH(te);
        memcpy(buf, *TE_HTEXT(te), length);
        buf[length] = '\0';

        fprintf(o_fp, "`%s'\n", buf);
    }

    {
        int16_t n_lines;
        GUEST<int16_t> *line_starts;

        n_lines = TE_N_LINES(te);
        line_starts = TE_LINE_STARTS(te);

        for(i = 0; i <= n_lines; i++)
            iprintf((o_fp, "lineStart[%d]: %d\n", i, toHost(line_starts[i])));
    }

    {
        STHandle style_table;
        StyleRun *runs;
        int16_t n_lines, n_runs, n_styles;
        TEStyleHandle te_style;
        LHHandle lh_table;
        LHElement *lh;

        n_lines = TE_N_LINES(te);
        te_style = TE_GET_STYLE(te);
        lh_table = TE_STYLE_LH_TABLE(te_style);
        n_runs = TE_STYLE_N_RUNS(te_style);
        lh = *lh_table;

        n_styles = TE_STYLE_N_STYLES(te_style);

        iprintf((o_fp, "(TEStyleHandle **%p) {\n", te_style));
        indent += 2;

        iprintf((o_fp, "nRuns %d\n", toHost(TE_STYLE_N_RUNS(te_style))));
        iprintf((o_fp, "nStyles %d\n", n_styles));

        for(i = 0; i <= n_lines; i++)
            iprintf((o_fp, "lhTab[%d]: lhHeight %d, lhAscent %d\n",
                     i, toHost(LH_HEIGHT(&lh[i])), toHost(LH_ASCENT(&lh[i]))));

        runs = TE_STYLE_RUNS(te_style);
        for(i = 0; i <= n_runs; i++)
            iprintf((o_fp, "runs[%d]: startChar %d, styleIndex %d\n", i,
                     toHost(STYLE_RUN_START_CHAR(&runs[i])),
                     toHost(STYLE_RUN_STYLE_INDEX(&runs[i]))));

        style_table = TE_STYLE_STYLE_TABLE(te_style);
        for(i = 0; i < n_styles; i++)
        {
            STElement *style;

            style = ST_ELT(style_table, i);
            iprintf((o_fp, "style[%d] stCount %d, stHieght %d, stAscent %d\n",
                     i, toHost(ST_ELT_COUNT(style)),
                     toHost(ST_ELT_HEIGHT(style)), toHost(ST_ELT_ASCENT(style))));
        }

        indent -= 2;
        iprintf((o_fp, "}\n"));
    }
    indent -= 2;
    iprintf((o_fp, "}\n"));
}

void dump_scrap(StScrpHandle scrap)
{
}

class MapSaveGuard
{
    GUEST<INTEGER> saveMap;

public:
    MapSaveGuard(GUEST<INTEGER> map)
        : saveMap(LM(CurMap))
    {
        LM(CurMap) = map;
    }
    ~MapSaveGuard()
    {
        LM(CurMap) = saveMap;
    }
};

static INTEGER
CountResourcesRN(LONGINT type, INTEGER rn)
{
    MapSaveGuard guard(rn);
    return Count1Resources(type);
}

static Handle
GetIndResourceRN(LONGINT type, INTEGER i, INTEGER rn)
{
    MapSaveGuard guard(rn);
    return Get1IndResource(type, i);
}

static void
AddResourceRN(Handle h, LONGINT type, INTEGER id, Str255 name, INTEGER rn)
{
    MapSaveGuard guard(rn);
    AddResource(h, type, id, name);
}

static OSErr
copy_resources(INTEGER new_rn, INTEGER old_rn, LONGINT type)
{
    INTEGER num_res;
    Boolean save_res_load;
    INTEGER i;

    save_res_load = LM(ResLoad);
    LM(ResLoad) = true;
    num_res = CountResourcesRN(type, old_rn);
    for(i = 1; i <= num_res; ++i)
    {
        Handle h;
        GUEST<INTEGER> id;
        Str255 name;
        GUEST<LONGINT> ignored;

        h = GetIndResourceRN(type, i, old_rn);
        GetResInfo(h, &id, &ignored, name);
        DetachResource(h);
        AddResourceRN(h, type, id, name, new_rn);
    }
    LM(ResLoad) = save_res_load;
    return noErr;
}

/* NOTE: dump_code_resources detachs the current (CODE '0'), so you will
         not be able to continue after calling this function */

/* EXAMPLE: dump_code_resources ("/:tmp:filename"); */

OSErr dump_code_resources(const char *filename)
{
    Str255 pfilename;
    OSErr retval;

    str255_from_c_string(pfilename, filename);
    CreateResFile(pfilename);
    retval = ResError();
    if(retval == noErr)
    {
        Handle h;
        INTEGER old_rn, new_rn;

        h = GetResource("CODE"_4, 0);
        retval = ResError();
        if(retval == noErr)
        {
            old_rn = HomeResFile(h);
            DetachResource(h);
            retval = ResError();
            if(retval == noErr)
            {
                new_rn = OpenRFPerm(pfilename, 0, fsRdWrPerm);
                retval = ResError();
                if(retval == noErr)
                    retval = copy_resources(new_rn, old_rn, "CODE"_4);
                if(new_rn)
                {
                    CloseResFile(new_rn);
                    if(retval == noErr)
                        retval = ResError();
                }
            }
        }
    }
    return retval;
}

static const char *
just_name(unsigned char just)
{
    const char *names[] = {
        "none", "left", "center", "right", "full", "INVALID"
    };
    const char *retval;

    retval = just < std::size(names) ? names[just] : names[std::size(names) - 1];
    return retval;
}

static const char *
flop_name(unsigned char flop)
{
    const char *names[] = {
        "none", "horizontal", "vertical", "INVALID"
    };
    const char *retval;

    retval = flop < std::size(names) ? names[flop] : names[std::size(names) - 1];
    return retval;
}

void
dump_textbegin(TTxtPicHdl h)
{
    iprintf((o_fp, "just = %d(%s)\n", TEXTPIC_JUST(h),
             just_name(TEXTPIC_JUST(h))));
    iprintf((o_fp, "flop = %d(%s)\n", TEXTPIC_FLOP(h),
             flop_name(TEXTPIC_FLOP(h))));
    iprintf((o_fp, "angle = %d\n", toHost(TEXTPIC_ANGLE(h))));
    iprintf((o_fp, "line = %d\n", TEXTPIC_LINE(h)));
    iprintf((o_fp, "comment = %d\n", TEXTPIC_COMMENT(h)));
    if(GetHandleSize((Handle)h) >= 10)
        iprintf((o_fp, "angle_fixed = 0x%08x\n", toHost(TEXTPIC_ANGLE_FIXED(h))));
}

void
dump_textcenter(TCenterRecHdl h)
{
    iprintf((o_fp, "y = 0x%08x\n", toHost(TEXTCENTER_Y(h))));
    iprintf((o_fp, "x = 0x%08x\n", toHost(TEXTCENTER(h))));
}

void
dump_zone_stats(void)
{
    iprintf((o_fp, "applzone free = %d\n", toHost(ZONE_ZCB_FREE(LM(ApplZone)))));
    iprintf((o_fp, " syszone free = %d\n", toHost(ZONE_ZCB_FREE(LM(SysZone)))));
}

#endif /* !NDEBUG */
