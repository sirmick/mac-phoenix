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
}
#endif /* !_GWORLD_H_ */
