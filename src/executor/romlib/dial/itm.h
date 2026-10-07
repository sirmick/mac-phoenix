#if !defined(_ITM_H_)
#define _ITM_H_

/*
 * Copyright 1986, 1989, 1990 by Abacus Research and Development, Inc.
 * All rights reserved.
 *

 */

namespace Executor
{
struct itmstr
{
    GUEST_STRUCT;
    GUEST<Handle> itmhand;
    GUEST<Rect> itmr;
    GUEST<unsigned char> itmtype;
    GUEST<unsigned char> itmlen;
};

typedef itmstr *itmp;

typedef GUEST<itmp> *itmh;

#define DIALOG_RES_HAS_POSITION_P(dlogh) \
    ((20                                 \
      + (((*dlogh)->dlglen + 2) & ~1)  \
      + 2)                               \
     == GetHandleSize((Handle)(dlogh)))
#define DIALOG_RES_POSITION(dlogh)                 \
    (*(GUEST<int16_t> *)((char *)&(*dlogh)->dlglen \
                         + (((*dlogh)->dlglen + 2) & ~1)))

#define ALERT_RES_HAS_POSITION_P(alerth) \
    ((sizeof(altstr) + 2) == GetHandleSize((Handle)(alerth)))

#define ALERT_RES_POSITION(alerth) \
    (*(GUEST<int16_t> *)((char *)&(*alerth)->altstag + 2))

#define noAutoCenter (0)
#define alertPositionParentWindow (0xB00A)
#define alertPositionMainScreen (0x300A)
#define alertPositionParentWindowScreen (0x700A)
#define dialogPositionParentWindow (0xA80A)
#define dialogPositionMainScreen (0x280A)
#define dialogPositionParentWindowScreen (0x680A)

extern void dialog_compute_rect(Rect *dialog_rect, Rect *dst_rect,
                                int position);

struct altstr
{
    GUEST_STRUCT;
    GUEST<Rect> altr;
    GUEST<INTEGER> altiid;
    GUEST<INTEGER> altstag;
};

typedef altstr *altp;

typedef GUEST<altp> *alth;

struct dlogstr
{
    GUEST_STRUCT;
    GUEST<Rect> dlgr;
    GUEST<INTEGER> dlgprocid;
    GUEST<char> dlgvis;
    GUEST<char> dlgfil1;
    GUEST<char> dlggaflag;
    GUEST<char> dlgfil2;
    GUEST<LONGINT> dlgrc;
    GUEST<INTEGER> dlgditl;
    GUEST<char> dlglen;
};
typedef dlogstr *dlogp;

typedef GUEST<dlogp> *dlogh;

typedef struct item_style_info
{
    GUEST_STRUCT;
    GUEST<int16_t> font;
    GUEST<Style> face;
    GUEST<unsigned char> filler;
    GUEST<int16_t> size;
    GUEST<RGBColor> foreground;
    GUEST<RGBColor> background;
    GUEST<int16_t> mode;
} item_style_info_t;

typedef struct item_color_info
{
    GUEST_STRUCT;
    GUEST<int16_t> data;
    GUEST<int16_t> offset;
} item_color_info_t;

extern itmp ROMlib_dpnotoip(DialogPeek dp, INTEGER itemno, SignedByte *flags);
extern void ROMlib_dpntoteh(DialogPeek dp, INTEGER no);

extern void ROMlib_drawiptext(DialogPtr dp, itmp ip, int item_no);
extern void dialog_create_item(DialogPeek dp, itmp dst, itmp src,
                               int item_no, Point base_pt);
extern bool get_item_style_info(DialogPtr dp, int item_no,
                                uint16_t *flags_return,
                                item_style_info_t *style_info);

extern void dialog_draw_item(DialogPtr dp, itmp itemp, int itemno);

#define ITEM_RECT(itemp) \
    ((itemp)->itmr)

#define ITEM_H(itemp) \
    ((itemp)->itmhand)


#define ITEM_TYPE(itemp) \
    ((itemp)->itmtype)

#define ITEM_LEN(itemp) \
    ((sizeof *(itemp) + (itemp)->itmlen + 1) & ~1)
#define ITEM_DATA(itemp) \
    ((GUEST<INTEGER> *)((itemp) + 1))

inline void BUMPIP(itmp& ip)
{
    ip = (itmp)((char *)(ip) + ITEM_LEN(ip));
}


extern void Beep(INTEGER n);

static_assert(sizeof(itmstr) == 14);
static_assert(sizeof(altstr) == 12);
static_assert(sizeof(dlogstr) == 22);
static_assert(sizeof(item_style_info_t) == 20);
static_assert(sizeof(item_color_info_t) == 4);
}
#endif /* _ITM_H_ */
