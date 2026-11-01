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
    bool temp_mem_p; /* pixels in temporary memory (useTempMem) */
    /* MacPhoenix: the pixels' handle, for when the pixmap holds their
       address (locked): RecoverHandle looks in the current zone only, and
       temporary memory isn't it */
    Handle pixels_h;

    struct gw_info *next;
} gw_info_t;

extern gw_info_t *lookup_gw_info_by_gw(GWorldPtr);
extern gw_info_t *lookup_gw_info_by_gw_pixmap(PixMapHandle);
extern gw_info_t *lookup_gw_info_by_gw_pixmap_baseaddr(void *);

extern gw_info_t *lookup_gw_info_by_gw_gd_pixmap(PixMapHandle);

/* MacPhoenix: a pixmap whose pmVersion says its baseAddr is a handle (a
   GWorld's own with its pixels unlocked, or a copy of it or of its
   device's pixmap: a window opened while the GWorld's device is current,
   Mars Rising) is drawn through it, as QuickDraw does. Lock: the pixels
   locked (LockPixels when they are a GWorld's, so that its own state
   stays right), baseAddr their pointer meanwhile; false when baseAddr
   isn't such a handle. pmh: the pixmap's handle, or nil when pm isn't in
   one (CopyBits' bitmap argument). Unlock puts it back. */
struct handle_base_lock
{
    PixMapHandle pmh;
    PixMap *pm;
    Handle h;
    SignedByte state;
    PixMapHandle gw_pixmap;
};
extern bool ROMlib_lock_handle_base(PixMapHandle pmh, PixMap *pm, handle_base_lock *l);
extern void ROMlib_unlock_handle_base(handle_base_lock *l);
}
#endif /* !_GWORLD_H_ */
