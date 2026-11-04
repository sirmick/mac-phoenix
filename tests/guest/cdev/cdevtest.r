/* cdevtest.r — CdevTest's resources; Rez -d V=N picks the variant
 * (build.mpw). Every variant: a button "Hit" (item 1) and a static text,
 * one 'nrct' rectangle. The variants:
 *   1 CdevMac      'mach' $0000FFFF (macDev), a 'CURS' -4064 (cursorDev)
 *   2 CdevAll      'mach' $FFFF0000 (all systems), no 'CURS'
 *   3 CdevNoRun    $0000FFFF, macDev answers 0
 *   4 CdevInitRes  initDev answers cdevResErr
 *   5 CdevNulMem   the first nulDev answers cdevMemErr
 *   6 CdevHitGen   the first hitDev answers cdevGenErr
 *   7 CdevMachII   'mach' $3FFF0000 (Macintosh II systems)
 *   8 CdevMachADB  'mach' $7FFF0400 (ADB)
 *   9 CdevMach0    'mach' $00000000
 */
#include "Types.r"

#if V == 1
#define NAME "CdevMac"
#define MACH $"0000 FFFF"
#define CFG ""
#elif V == 2
#define NAME "CdevAll"
#define MACH $"FFFF 0000"
#define CFG ""
#elif V == 3
#define NAME "CdevNoRun"
#define MACH $"0000 FFFF"
#define CFG "8Z"
#elif V == 4
#define NAME "CdevInitRes"
#define MACH $"FFFF 0000"
#define CFG "0R"
#elif V == 5
#define NAME "CdevNulMem"
#define MACH $"FFFF 0000"
#define CFG "3M"
#elif V == 6
#define NAME "CdevHitGen"
#define MACH $"FFFF 0000"
#define CFG "1G"
#elif V == 7
#define NAME "CdevMachII"
#define MACH $"3FFF 0000"
#define CFG ""
#elif V == 8
#define NAME "CdevMachADB"
#define MACH $"7FFF 0400"
#define CFG ""
#else
#define NAME "CdevMach0"
#define MACH $"0000 0000"
#define CFG ""
#endif

resource 'STR ' (-4064, purgeable) { CFG };
resource 'STR ' (-4065, purgeable) { NAME };

/* soft mask, hard mask */
data 'mach' (-4064, purgeable) { MACH };

/* one rectangle: top, left, bottom, right; the origin must be (-1,87) */
data 'nrct' (-4064, purgeable) { $"0001" $"FFFF 0057 0064 0142" };

resource 'DITL' (-4064, purgeable) {
    {
        {40, 100, 60, 180}, Button { enabled, "Hit" },
        {10, 100, 26, 300}, StaticText { disabled, NAME }
    }
};

#if V == 1
resource 'CURS' (-4064, purgeable) {
    $"0000 0000 0000 0000 0000 0000 0000 0000"
    $"0000 0000 0000 0000 0000 0000 0000 0000",
    $"0000 0000 0000 0000 0000 0000 0000 0000"
    $"0000 0000 0000 0000 0000 0000 0000 0000",
    {0, 0}
};
#endif
