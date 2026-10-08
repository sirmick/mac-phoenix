#if !defined(__rsys_icon_h__)
#define __rsys_icon_h__

#define N_SUITE_ICONS 6
namespace Executor
{
/* One layout for icon suites and icon caches (icon.cpp); a cache sets
   flags bit 0 and fills missing members from cacheProc. Apple's own layout
   is not known, so guest code must not look inside. */
struct suite_layout_t
{
    GUEST_STRUCT;
    GUEST<Handle> icons[N_SUITE_ICONS]; // ICN#, icl4, icl8, ics#, ics4, ics8
    GUEST<INTEGER> label;
    GUEST<INTEGER> flags;
    GUEST<void *> cacheData;
    GUEST<IconGetterUPP> cacheProc;
};
}
#endif /* !defined (__rsys_icon_h__) */
