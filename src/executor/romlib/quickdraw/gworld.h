#if !defined(_GWORLD_H_)
#define _GWORLD_H_
namespace Executor
{
/* MacPhoenix: a GWorld's pmVersion, as 8.6 sets it (tests/guest/difftest
   sys, gworld): its pixels unlocked, baseAddr is their handle; locked, a
   pointer. QuickDraw draws through a port whose pixmap says handle (a
   program's own copy of the pixmap: Mars Rising). */
enum
{
    gw_pm_version_locked = 1,
    gw_pm_version_handle = 2,
};

typedef struct gw_info
{
    GWorldPtr gw;
    PixMapHandle gw_pixmap;
    GDHandle gw_gd;
    PixMapHandle gw_gd_pixmap;

    GWorldFlags flags;

    int pixel_lock_count;
    bool gd_allocated_p;

    struct gw_info *next;
} gw_info_t;

extern gw_info_t *lookup_gw_info_by_gw(GWorldPtr);
extern gw_info_t *lookup_gw_info_by_gw_pixmap(PixMapHandle);
extern gw_info_t *lookup_gw_info_by_gw_pixmap_baseaddr(void *);

extern gw_info_t *lookup_gw_info_by_gw_gd_pixmap(PixMapHandle);

/* MacPhoenix: a pixmap whose pmVersion says its baseAddr is a handle (a
   copy of a GWorld's or of its device's pixmap: a window opened while the
   GWorld's device is current, Mars Rising) is drawn through it, as
   QuickDraw does. Lock: the handle locked, baseAddr its pointer meanwhile;
   returns the handle, or nil when baseAddr isn't one. Unlock puts it back. */
extern Handle ROMlib_lock_handle_base(PixMap *pm, SignedByte *state);
extern void ROMlib_unlock_handle_base(PixMap *pm, Handle h, SignedByte state);
}
#endif /* !_GWORLD_H_ */
