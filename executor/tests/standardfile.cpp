#include "gtest/gtest.h"

#include "compat.h"

/* Standard File measured on the Quadra (7.5.5, 7.6.1). The 68k suite
   only: natively nothing answers the dialog. */
#ifndef EXECUTOR
#include <Dialogs.h>
#include <StandardFile.h>
#include <string>

namespace
{
struct Seen
{
    DialogPtr dp = nullptr;
    int hooks = 0;
    std::string refcons;   /* where: h hook, f filter, F file filter */
    bool all_stdf = true;
};

void note(Seen *seen, char where, DialogPtr dp)
{
    long rc = GetWRefCon((WindowPtr)dp);
    if(seen->refcons.size() < 40)
        seen->refcons += where;
    if(rc != sfMainDialogRefCon)
        seen->all_stdf = false;
}

pascal short hook(short item, DialogPtr dp, void *data)
{
    Seen *seen = (Seen *)data;
    if(!seen->dp)
        seen->dp = dp;
    note(seen, 'h', dp);
    if(++seen->hooks >= 4)
        return sfItemCancelButton;
    return item == sfHookFirstCall ? sfHookRebuildList : sfHookNullEvent;
}

pascal Boolean filter(DialogPtr dp, EventRecord *, short *, void *data)
{
    note((Seen *)data, 'f', dp);
    return false;
}

pascal Boolean file_filter(CInfoPBPtr, void *data)
{
    Seen *seen = (Seen *)data;
    if(seen->dp)
        note(seen, 'F', seen->dp);
    return false;
}
}

/* The main dialog's refCon is sfMainDialogRefCon ('stdf') all along: in
   its hooks and filters, and in the file filter, which runs outside
   them. */
TEST(StandardFile, MainDialogRefConAllAlong)
{
    Seen seen;
    StandardFileReply reply;
    Point where = { -1, -1 };
    CustomGetFile(NewFileFilterYDProc(file_filter), -1, nullptr, &reply, 0, where,
                  NewDlgHookYDProc(hook), NewModalFilterYDProc(filter),
                  nullptr, nullptr, &seen);
    EXPECT_TRUE(seen.all_stdf) << seen.refcons;
    EXPECT_NE(std::string::npos, seen.refcons.find('F')) << seen.refcons;
}
#endif
