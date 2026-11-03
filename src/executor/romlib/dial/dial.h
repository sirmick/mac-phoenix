#if !defined(__rsys_dial_h__)
#define __rsys_dial_h__

#include <DialogMgr.h>

#define MODULE_NAME dial_dial
#include <base/api-module.h>

namespace Executor
{
struct icon_item_template_t
{
    GUEST_STRUCT;
    GUEST<int16_t> count;
    GUEST<Handle> h;
    GUEST<Rect> r;
    GUEST<uint8_t> type;
    GUEST<uint8_t> len;
    GUEST<int16_t> res_id;
};

extern Boolean C_ROMlib_myfilt(DialogPtr dlg, EventRecord *evt,
                                      GUEST<INTEGER> *ith);
EXTERN_PASCAL_FUNCTION_PTR(ROMlib_myfilt);

/* The cancel item SetDialogCancelItem set (0: none); forgotten as the
   dialog closes. */
extern INTEGER ROMlib_dialog_cancel_item(DialogPtr dialog);
extern void ROMlib_dialog_forget(DialogPtr dialog);
/* MacPhoenix: what dialMgr8.cpp keeps for a dialog */
extern void ROMlib_dialog8_forget(DialogPtr dialog);
/* the controls standing for items that are no controls in a dialog with
   the control hierarchy (dialMgr8.cpp) */
extern void ROMlib_dialog_item_controls(DialogPtr dp);
extern void ROMlib_dialog_item_controls_shorten(DialogPtr dp, int count);
extern ControlHandle ROMlib_dialog_item_control(DialogPtr dp, int item);
extern void ROMlib_dialog_item_control_sync(DialogPtr dp, int item, const Rect *r);
extern int32_t C_ROMlib_cdef_dialog_item(int16_t var, ControlHandle c, int16_t mess,
                                         int32_t param);
EXTERN_PASCAL_FUNCTION_PTR(ROMlib_cdef_dialog_item);

static_assert(sizeof(icon_item_template_t) == 18);
}
#endif /* !defined (__rsys_dial_h__) */
