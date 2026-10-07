/* Copyright 1989, 1990 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in PrintMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <PrintMgr.h>
#include <ResourceMgr.h>

#include <print/nextprint.h>
#include <print/print.h>
#include <prefs/prefs.h>

using namespace Executor;

static void
set_wDev(THPrint hPrint)
{
    (*hPrint)->prStl.wDev = 0x307;
}

void
Executor::ROMlib_set_default_resolution(THPrint hPrint, INTEGER vres, INTEGER hres)
{
    printer_init();
    update_printing_globals();

    (*hPrint)->prInfo.iVRes = vres;
    (*hPrint)->prInfo.iHRes = hres;
    (*hPrint)->prInfo.rPage.top = 0;
    (*hPrint)->prInfo.rPage.left = 0;
    (*hPrint)->prInfo.rPage.bottom = (ROMlib_paper_y - 72) * vres / 72;
    (*hPrint)->prInfo.rPage.right = (ROMlib_paper_x - 72) * hres / 72;

    (*hPrint)->rPaper.top = (INTEGER)(-0.5 * vres);
    (*hPrint)->rPaper.bottom = (INTEGER)((ROMlib_paper_y - 36) * vres / 72);
    (*hPrint)->rPaper.left = (INTEGER)(-0.5 * hres);
    (*hPrint)->rPaper.right = (INTEGER)((ROMlib_paper_x - 36) * hres / 72);

    ROMlib_resolution_x = hres;
    ROMlib_resolution_y = vres;
}

void Executor::C_PrintDefault(THPrint hPrint)
{
    /* TODO:  fill this information in from the currently open
       printer resource file.  I don't know where it's kept so
       I've filled in the values by hand to be what I suspect the
       LaserWriter we're using wants */

    memset(*hPrint, 0, sizeof(TPrint));
    (*hPrint)->iPrVersion = ROMlib_PrDrvrVers;

    ROMlib_set_default_resolution(hPrint, 72, 72);

    (*hPrint)->prInfo.iDev = 0;

    set_wDev(hPrint);
    (*hPrint)->prStl.iPageV = 1320; /* These were switched a while back */
    (*hPrint)->prStl.iPageH = 1020; /* but I think it was a mistake */
    (*hPrint)->prStl.bPort = 0;
    (*hPrint)->prStl.feed = 2;

    (*hPrint)->prInfoPT.iDev = 0;
    (*hPrint)->prInfoPT.iVRes = 72;
    (*hPrint)->prInfoPT.iHRes = 72;
    (*hPrint)->prInfoPT.rPage = (*hPrint)->prInfo.rPage;

    (*hPrint)->prXInfo.iRowBytes = ((*hPrint)->prXInfo.iBandH + 7) / 8;
    /* TODO: what about the rest of prXInfo? (is zero for now) */

    (*hPrint)->prJob.iFstPage = 1;
    (*hPrint)->prJob.iLstPage = 9999;
    (*hPrint)->prJob.iCopies = 1;
    (*hPrint)->prJob.bJDocLoop = 2; /* used to be 1, but then File Maker
					  Pro 2.1 would call PrOpenDoc
					  and PrCloseDoc once for each page */
    (*hPrint)->prJob.fFromUsr = 1;
}

Boolean Executor::C_PrValidate(THPrint hPrint) /* IMII-158 */
{
    /* TODO: figure out what are problem areas for us and adjust
	     accordingly */

    set_wDev(hPrint);

    if(!(*hPrint)->prInfo.iVRes || !(*hPrint)->prInfo.iHRes)
        PrintDefault(hPrint);

    {
        int first, last;

        first = (*hPrint)->prJob.iFstPage;
        last = (*hPrint)->prJob.iLstPage;

        if(first < 1 || first > last)
        {
            (*hPrint)->prJob.iFstPage = 1;
            (*hPrint)->prJob.iLstPage = 9999;
        }
    }
    {
        int copies;

        copies = (*hPrint)->prJob.iCopies;

        if(copies < 1 || copies > 99)
            (*hPrint)->prJob.iCopies = 1;
    }

    (*hPrint)->prJob.bJDocLoop = 2;
    return false;
}

Boolean Executor::C_PrStlDialog(THPrint hPrint)
{
    Boolean retval;

    retval = C_PrDlgMain(hPrint, (ProcPtr)&PrStlInit);
    return retval;
}

Boolean Executor::C_PrJobDialog(THPrint hPrint)
{
    ROMlib_acknowledge_job_dialog(hPrint);
    return C_PrDlgMain(hPrint, (ProcPtr)&PrJobInit);
}

void Executor::C_PrJobMerge(THPrint hPrintSrc, THPrint hPrintDst) /* TODO */
{
    warning_unimplemented("");
}
