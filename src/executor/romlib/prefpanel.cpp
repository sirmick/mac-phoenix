#include <rsys/prefpanel.h>
#include <prefs/options.h>
#include <prefs/prefs.h>
#include <sound/sounddriver.h>
#include <vdriver/refresh.h>
#include <util/string.h>
#include <quickdraw/cquick.h>
#include <commandline/option_arguments.h>
#include <time/vbl.h>

#include <DialogMgr.h>
#include <SysErr.h>
#include <OSUtil.h>
#include <SegmentLdr.h>
#include <ControlMgr.h>
#include <CQuickDraw.h>
#include <BinaryDecimal.h>

#include <boost/lexical_cast.hpp>

using namespace Executor;


typedef enum { SETSTATE,
               CLEARSTATE,
               FLIPSTATE } modstate_t;

void modstate(DialogPtr dp, INTEGER tomod, modstate_t mod)
{
    GUEST<INTEGER> type;
    INTEGER newvalue;
    GUEST<Handle> ch_s;
    ControlHandle ch;
    Rect r;

    GetDialogItem(dp, tomod, &type, &ch_s, &r);
    ch = (ControlHandle)ch_s;
    if(type & ctrlItem)
    {
        switch(mod)
        {
            case SETSTATE:
                newvalue = 1;
                break;
            case CLEARSTATE:
                newvalue = 0;
                break;
            case FLIPSTATE:
                newvalue = GetControlValue(ch) ? 0 : 1;
                break;
#if !defined(LETGCCWAIL)
            default:
                newvalue = 0;
                break;
#endif /* !defined(LETGCCWAIL) */
        }
        if(type & itemDisable)
        {
            SetControlValue(ch, 0);
            HiliteControl(ch, 255);
        }
        else
            SetControlValue(ch, newvalue);
    }
}

INTEGER getvalue(DialogPtr dp, INTEGER toget)
{
    GUEST<INTEGER> type;
    GUEST<ControlHandle> ch;
    Rect r;

    GetDialogItem(dp, toget, &type, (GUEST<Handle> *)&ch, &r);
    return (type & ctrlItem) ? GetControlValue(ch) : 0;
}

typedef struct depth
{
    int item;
    int bpp;
} depth_t;

static depth_t depths_list[] = {
    { PREF_COLORS_2, 1 },
    { PREF_COLORS_4, 2 },
    { PREF_COLORS_16, 4 },
    { PREF_COLORS_256, 8 },
    { PREF_COLORS_THOUSANDS, 16 },
    { PREF_COLORS_MILLIONS, 32 },
};

static int current_depth_item;

/* NOTE: Illustrator patches out the Palette Manager, which uses a dispatch
   table.  Our current stubify.h doesn't deal gracefully with the case of
   patched out traps that use dispatch tables, so we use C_SetDepth and
   C_HasDepth below. */

static void
set_depth(DialogPtr dp, int16_t item_to_set)
{
    int i;

    modstate(dp, current_depth_item, CLEARSTATE);

    for(i = 0; i < (int)std::size(depths_list); i++)
    {
        depth_t *depth = &depths_list[i];

        if(item_to_set == depth->item)
            C_SetDepth(LM(MainDevice), depth->bpp, 0, 0);
    }

    current_depth_item = item_to_set;
    modstate(dp, current_depth_item, SETSTATE);
}

void setoneofthree(DialogPtr dp, INTEGER toset, INTEGER item1, INTEGER item2,
                   INTEGER item3)
{
    if(toset != item1)
        modstate(dp, item1, CLEARSTATE);
    if(toset != item2)
        modstate(dp, item2, CLEARSTATE);
    if(toset != item3)
        modstate(dp, item3, CLEARSTATE);
    modstate(dp, toset, SETSTATE);
}

void setedittext(DialogPtr dp, INTEGER itemno, StringPtr str)
{
    GUEST<INTEGER> type;
    Rect r;
    GUEST<Handle> h;

    GetDialogItem(dp, itemno, &type, &h, &r);
    SetDialogItemText(h, str);
}

void setedittextnum(DialogPtr dp, INTEGER itemno, INTEGER value)
{
    Str255 str;

    NumToString(value, str);
    setedittext(dp, itemno, str);
}

void setedittextcstr(DialogPtr dp, INTEGER itemno, const std::string& cstr)
{
    int len;
    Str255 str;

    len = cstr.size();
    if(len > 255)
        len = 255;
    str[0] = len;
    memcpy(str + 1, cstr.data(), len);
    setedittext(dp, itemno, str);
}

INTEGER getedittext(DialogPtr dp, INTEGER itemno)
{
    Str255 str;
    GUEST<Handle> h_s;
    Handle h;
    GUEST<INTEGER> type;
    Rect r;
    GUEST<LONGINT> l;

    GetDialogItem(dp, itemno, &type, &h_s, &r);
    h = h_s;
    GetDialogItemText(h, str);
    StringToNum(str, &l);
    return (INTEGER)l;
}

void setupprefvalues(DialogPtr dp)
{
    INTEGER toset;

    setedittextnum(dp, PREFREFRESHITEM, ROMlib_refresh);
    setedittextcstr(dp, PREF_COMMENTS, ROMlib_Comments);

    setoneofthree(dp, PREFNORMALITEM, PREFNORMALITEM, PREFINBETWEENITEM,
                  PREFANIMATIONITEM);
    modstate(dp, PREFNOCLOCKITEM, ROMlib_clock != 2 ? SETSTATE : CLEARSTATE);
    modstate(dp, PREFNO32BITWARNINGSITEM,
             ROMlib_nowarn32 ? SETSTATE : CLEARSTATE);
    modstate(dp, PREFFLUSHCACHEITEM,
             ROMlib_flushoften ? SETSTATE : CLEARSTATE);
    switch(ROMlib_PretendSound)
    {
#if !defined(LETGCCWAIL)
        default:
#endif /* !defined(LETGCCWAIL) */
        case soundoff:
            toset = PREFSOUNDOFFITEM;
            break;
        case soundpretend:
            toset = PREFSOUNDPRETENDITEM;
            break;
        case soundon:
            toset = PREFSOUNDONITEM;
            break;
    }
    setoneofthree(dp, toset, PREFSOUNDOFFITEM, PREFSOUNDPRETENDITEM,
                  PREFSOUNDONITEM);
    modstate(dp, PREFNEWLINEMAPPINGITEM,
             ROMlib_newlinetocr ? SETSTATE : CLEARSTATE);
    modstate(dp, PREFDIRECTDISKITEM,
             ROMlib_directdiskaccess ? SETSTATE : CLEARSTATE);
    modstate(dp, PREFFONTSUBSTITUTIONITEM,
             ROMlib_fontsubstitution ? SETSTATE : CLEARSTATE);

    modstate(dp, PREF_PRETEND_HELP,
             ROMlib_pretend_help ? SETSTATE : CLEARSTATE);
    modstate(dp, PREF_PRETEND_EDITION,
             ROMlib_pretend_edition ? SETSTATE : CLEARSTATE);
    modstate(dp, PREF_PRETEND_SCRIPT,
             ROMlib_pretend_script ? SETSTATE : CLEARSTATE);
    modstate(dp, PREF_PRETEND_ALIAS,
             ROMlib_pretend_alias ? SETSTATE : CLEARSTATE);
    {
        int bpp, i;

        bpp = PIXMAP_PIXEL_SIZE(GD_PMAP(LM(MainDevice)));
        for(i = 0; i < (int)std::size(depths_list); i++)
        {
            depth_t *depth = &depths_list[i];

            if(depth->bpp == bpp)
            {
                modstate(dp, depth->item, SETSTATE);
                current_depth_item = depth->item;
            }
            else
                modstate(dp, depth->item, CLEARSTATE);
        }
    }
    setedittextcstr(dp, PREF_SYSTEM, C_STRING_FROM_SYSTEM_VERSION());
}

void update_string_from_edit_text(std::string& cstr, DialogPtr dp, INTEGER itemno)
{
    Str255 str;
    GUEST<Handle> h_s;
    Handle h;
    GUEST<INTEGER> type;
    Rect r;

    GetDialogItem(dp, itemno, &type, &h_s, &r);
    h = h_s;
    GetDialogItemText(h, str);
    cstr = std::string(&str[1], &str[str[0] + 1]);
}

void readprefvalues(DialogPtr dp)
{
    set_refresh_rate(getedittext(dp, PREFREFRESHITEM));
    update_string_from_edit_text(ROMlib_Comments, dp, PREF_COMMENTS);

    ROMlib_clockonoff(!getvalue(dp, PREFNOCLOCKITEM));

    ROMlib_nowarn32 = getvalue(dp, PREFNO32BITWARNINGSITEM);
    ROMlib_flushoften = getvalue(dp, PREFFLUSHCACHEITEM);

    if(getvalue(dp, PREFSOUNDONITEM))
        ROMlib_PretendSound = soundon;
    else if(getvalue(dp, PREFSOUNDPRETENDITEM))
        ROMlib_PretendSound = soundpretend;
    else
        ROMlib_PretendSound = soundoff;

    ROMlib_newlinetocr = getvalue(dp, PREFNEWLINEMAPPINGITEM);
    ROMlib_directdiskaccess = getvalue(dp, PREFDIRECTDISKITEM);
    ROMlib_fontsubstitution = getvalue(dp, PREFFONTSUBSTITUTIONITEM);

    ROMlib_pretend_help = getvalue(dp, PREF_PRETEND_HELP);
    ROMlib_pretend_edition = getvalue(dp, PREF_PRETEND_EDITION);
    ROMlib_pretend_script = getvalue(dp, PREF_PRETEND_SCRIPT);
    ROMlib_pretend_alias = getvalue(dp, PREF_PRETEND_ALIAS);

    try
    {
        std::string system_string;
        update_string_from_edit_text(system_string, dp, PREF_SYSTEM);
        auto vn = boost::lexical_cast<VersionNumber>(system_string);
        system_version = CREATE_SYSTEM_VERSION(vn.major, vn.minor, vn.patch);
    }
    catch(boost::bad_lexical_cast&)
    {
    }
}


typedef enum { disable,
               enable } enableness_t;

static void mod_item_enableness(DialogPtr dp, INTEGER item,
                                enableness_t enableness_wanted)
{
    INTEGER type;
    GUEST<INTEGER> type_s;
    Handle h;
    GUEST<Handle> tmpH;
    ControlHandle ch;
    Rect r;

    GetDialogItem(dp, item, &type_s, &tmpH, &r);
    type = type_s;
    h = tmpH;
    if(((type & itemDisable) && enableness_wanted == enable)
       || (!(type & itemDisable) && enableness_wanted == disable))
    {
        type ^= itemDisable;
        SetDialogItem(dp, item, type, h, &r);
    }
    ch = (ControlHandle)h;
    if((*ch)->contrlHilite == 255 && enableness_wanted == enable)
        HiliteControl(ch, 0);
    else if((*ch)->contrlHilite != 255 && enableness_wanted == disable)
        HiliteControl(ch, 255);
}

static void
set_sound_on_string(DialogPtr dp)
{
    Str255 sound_string;
    GUEST<INTEGER> junk1;
    GUEST<Handle> h;
    Rect junk2;

    str255_from_c_string(sound_string,
                         (sound_driver->sound_silent()
                              ? "On (silent)"
                              : "On"));
    GetDialogItem(dp, PREFSOUNDONITEM, &junk1, &h, &junk2);
    SetControlTitle((ControlHandle)h, sound_string);
}

/*
 * Don't count on the proper items to be enabled/disabled, do it
 * explicitly.  This is to avoid version skew problems associated with
 * our System file.
 */

static void enable_disable_pref_items(DialogPtr dp)
{
    static INTEGER to_enable[] = /* put only controls in this list */
        {
          PREFOKITEM,
          PREFCANCELITEM,
          PREFSAVEITEM,
          PREFNORMALITEM,
          PREFANIMATIONITEM,
          PREFSOUNDOFFITEM,
          PREFSOUNDPRETENDITEM,
          PREFNEWLINEMAPPINGITEM, /* Perhaps this should be disabled under DOS */
          PREFFLUSHCACHEITEM,
          PREFDIRECTDISKITEM,
          PREFNO32BITWARNINGSITEM,
          PREFFONTSUBSTITUTIONITEM,
          PREFCACHEHEURISTICSITEM,
          PREF_PRETEND_HELP,
          PREF_PRETEND_EDITION,
          PREF_PRETEND_SCRIPT,
          PREF_PRETEND_ALIAS,
          PREFREFRESHITEM,
        };
    static INTEGER to_disable[] = /* put only controls in this list */
        {
          PREFNOCLOCKITEM,
          PREFPASSPOSTSCRIPTITEM,
          PREF_COLORS_THOUSANDS,
          PREF_COLORS_MILLIONS,
          PREF_GRAY_SCALE,
          PREFSOUNDONITEM,
        };
    int i;

    for(i = 0; i < (int)std::size(to_enable); ++i)
        mod_item_enableness(dp, to_enable[i], enable);
    for(i = 0; i < (int)std::size(to_disable); ++i)
        mod_item_enableness(dp, to_disable[i], disable);

    if(sound_driver->sound_works())
        mod_item_enableness(dp, PREFSOUNDONITEM, enable);
    set_sound_on_string(dp);

    for(i = 0; i < (int)std::size(depths_list); i++)
    {
        depth_t *depth = &depths_list[i];

        if(C_HasDepth(LM(MainDevice), depth->bpp, 0, 0))
            mod_item_enableness(dp, depth->item, enable);
        else
            mod_item_enableness(dp, depth->item, disable);
    }
}

static void ROMlib_circledefault(DialogPtr dp)
{
    GUEST<INTEGER> type;
    GUEST<Handle> h;
    Rect r;
    GrafPtr saveport;

    saveport = qdGlobals().thePort;
    SetPort(dp);
    GetDialogItem(dp, 1, &type, &h, &r);
    PenSize(3, 3);
    InsetRect(&r, -4, -4);
    if(!ROMlib_rect_buttons)
        FrameRoundRect(&r, 16, 16);
    else
        FrameRect(&r);
    PenSize(1, 1);
    SetPort(saveport);
}

void Executor::dopreferences(void)
{
    DialogPtr dp;
    INTEGER ihit;
    GUEST<INTEGER> ihit_s;

    if(LM(WWExist) != EXIST_YES)
        SysBeep(5);
    else
    {
        static Boolean am_already_here = false;

        if(!am_already_here)
        {
            am_already_here = true;

            ParamText(LM(CurApName), 0, 0, 0);

            dp = GetNewDialog(PREFDIALID, (Ptr)0, (WindowPtr)-1);
            enable_disable_pref_items(dp);
            setupprefvalues(dp);
            ROMlib_circledefault(dp);
            do
            {
                ModalDialog(nullptr, &ihit_s);
                ihit = ihit_s;
                switch(ihit)
                {
                    case PREFNORMALITEM:
                    case PREFINBETWEENITEM:
                    case PREFANIMATIONITEM:
                        setoneofthree(dp, ihit, PREFNORMALITEM, PREFINBETWEENITEM,
                                        PREFANIMATIONITEM);
                        break;
                    case PREFSOUNDOFFITEM:
                    case PREFSOUNDPRETENDITEM:
                    case PREFSOUNDONITEM:
                        setoneofthree(dp, ihit, PREFSOUNDOFFITEM,
                                        PREFSOUNDPRETENDITEM, PREFSOUNDONITEM);
                        break;
                    case PREF_COLORS_2:
                    case PREF_COLORS_4:
                    case PREF_COLORS_16:
                    case PREF_COLORS_256:
                    case PREF_COLORS_THOUSANDS:
                    case PREF_COLORS_MILLIONS:
                        set_depth(dp, ihit);
                        break;
                    case PREFNOCLOCKITEM:
                    case PREFPASSPOSTSCRIPTITEM:
                    case PREFNEWLINEMAPPINGITEM:
                    case PREFFLUSHCACHEITEM:
                    case PREFDIRECTDISKITEM:
                    case PREFNO32BITWARNINGSITEM:
                    case PREFFONTSUBSTITUTIONITEM:
                    case PREFCACHEHEURISTICSITEM:
                    case PREF_PRETEND_HELP:
                    case PREF_PRETEND_EDITION:
                    case PREF_PRETEND_SCRIPT:
                    case PREF_PRETEND_ALIAS:
                        modstate(dp, ihit, FLIPSTATE);
                        break;
                }
            } while(ihit != PREFOKITEM && ihit != PREFCANCELITEM && ihit != PREFSAVEITEM);
            if(ihit == PREFOKITEM || ihit == PREFSAVEITEM)
            {
                readprefvalues(dp);
                if(ihit == PREFSAVEITEM)
                    SaveConfigFile();
            }
            DisposeDialog(dp);
            am_already_here = false;
        }
    }
}
