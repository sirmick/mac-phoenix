/* QTMath: QuickTime's fixed-point, wide and matrix routines (trap $ABC2,
   Universal Interfaces 3.3.2's ImageCompression.h and FixMath.h) on
   chosen values, every answer printed in hex: a ROM machine's run with
   QuickTime is the reference Executor's own $ABC2 is compared with
   (tests/guest/qtmath/run_qtmath.sh). One "PASS name value" line per
   call, to Host:<suite.cfg's name> (default qtmath_results.txt), then
   "---".

     tools/mpw/mpw build tests/guest/qtmath -o tests/guest/qtmath */
#include <Types.h>
#include <Memory.h>
#include <Gestalt.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <FixMath.h>
#include <ImageCompression.h>
#include <stdio.h>
#include <string.h>

QDGlobals qd;
static FILE *out;
static char line[512];

/* MPW SC evaluates arguments right to left: every value is computed
   before the line is printed. */
static void put(const char *name, const char *value)
{
    fprintf(out, "PASS %s %s\n", name, value);
    fflush(out);
}

/* A line not compared: Apple's sine, cosine, exponent, logarithm and
   power are approximations (to about 2^-12), Executor's exact. */
static void note(const char *name, const char *value)
{
    fprintf(out, "# %s %s\n", name, value);
    fflush(out);
}

static void wide_str(const wide *w, char *s)
{
    sprintf(s, "%08lX%08lX", (unsigned long)w->hi, (unsigned long)w->lo);
}

static void set_wide(wide *w, long hi, unsigned long lo)
{
    w->hi = hi;
    w->lo = lo;
}

static void matrix_str(const MatrixRecord *m, char *s)
{
    int i, j;
    s[0] = 0;
    for(i = 0; i < 3; i++)
        for(j = 0; j < 3; j++)
            sprintf(s + strlen(s), "%s%08lX", i || j ? " " : "", (unsigned long)m->matrix[i][j]);
}

static void put_matrix(const char *name, const MatrixRecord *m)
{
    matrix_str(m, line);
    put(name, line);
}

static const Fixed fixeds[] = {
    0x00010000, 0x00018000, 0xFFFE8000, 0x00000001, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000,
    0x00030000, 0x00000003, 0x0000AAAB, 0x00640000, 0xFF9C0000, 0
};
#define NFIXED (sizeof fixeds / sizeof fixeds[0])

static void test_wide(void)
{
    static const long longs[] = { 1, -1, 3, -3, 0x7FFFFFFF, 0x80000000, 0x12345, -0x12345, 1000, 7 };
    static const short shifts[] = { 0, 1, 4, 31, 32, 33, 63, -1, -4, -32 };
    wide a, b, c;
    long q, r;
    unsigned long u;
    int i, j;
    char s1[20], s2[20], name[40];

    for(i = 0; i < 10; i++)
        for(j = 0; j < 10; j += 3)
        {
            CompMul(longs[i], longs[j], &a);
            wide_str(&a, s1);
            sprintf(name, "CompMul.%d.%d", i, j);
            put(name, s1);
        }
    set_wide(&a, 0x00000001, 0x80000000);
    set_wide(&b, 0xFFFFFFFF, 0xC0000000);
    c = a;
    CompAdd(&b, &c);
    wide_str(&c, s1);
    put("CompAdd", s1);
    c = a;
    CompSub(&b, &c);
    wide_str(&c, s1);
    put("CompSub", s1);
    c = b;
    CompNeg(&c);
    wide_str(&c, s1);
    put("CompNeg", s1);
    for(i = 0; i < 10; i++)
    {
        set_wide(&c, 0x00000123, 0x456789AB);
        CompShift(&c, shifts[i]);
        wide_str(&c, s1);
        set_wide(&a, 0xFFFFFEDC, 0xBA987655);
        CompShift(&a, shifts[i]);
        wide_str(&a, s2);
        sprintf(line, "%s %s", s1, s2);
        sprintf(name, "CompShift.%d", shifts[i]);
        put(name, line);
    }
    for(i = 0; i < 10; i++)
    {
        set_wide(&a, 0x00000002, 0x00000007);
        r = 0x55555555;
        q = CompDiv(&a, longs[i], &r);
        set_wide(&b, 0xFFFFFFFD, 0xFFFFFFF9);
        u = 0x55555555;
        sprintf(line, "%08lX %08lX", q, r);
        q = CompDiv(&b, longs[i], (long *)&u);
        sprintf(line + strlen(line), " %08lX %08lX", q, u);
        sprintf(name, "CompDiv.%d", i);
        put(name, line);
    }
    for(i = 0; i < NFIXED; i += 2)
    {
        set_wide(&a, 0x00000003, 0x00000005);
        CompFixMul(&a, fixeds[i], &c);
        wide_str(&c, s1);
        set_wide(&a, 0xFFFFFFFC, 0xFFFFFFFB);
        CompFixMul(&a, fixeds[i], &b);
        wide_str(&b, s2);
        sprintf(line, "%s %s", s1, s2);
        sprintf(name, "CompFixMul.%d", i);
        put(name, line);
    }
    for(i = 0; i < 10; i++)
    {
        set_wide(&a, 0x00000001, 0x00000005);
        CompMulDiv(&a, 7, longs[i]);
        wide_str(&a, s1);
        set_wide(&b, 0xFFFFFFFE, 0xFFFFFFFB);
        CompMulDiv(&b, 7, longs[i]);
        wide_str(&b, s2);
        sprintf(line, "%s %s", s1, s2);
        sprintf(name, "CompMulDiv.%d", i);
        put(name, line);
        set_wide(&a, 0x00000001, 0x00000005);
        r = 0x55555555;
        CompMulDivTrunc(&a, 7, longs[i], &r);
        wide_str(&a, s1);
        set_wide(&b, 0xFFFFFFFE, 0xFFFFFFFB);
        q = 0x55555555;
        CompMulDivTrunc(&b, 7, longs[i], &q);
        wide_str(&b, s2);
        sprintf(line, "%s %08lX %s %08lX", s1, r, s2, q);
        sprintf(name, "CompMulDivTrunc.%d", i);
        put(name, line);
    }
    {   /* rounding: 3/2, -3/2, 5/4, 7/4, -7/4 */
        static const long co[] = { 1, 1, 5, 7, -7 }, mul[] = { 3, -3, 1, 1, 1 }, dv[] = { 2, 2, 4, 4, 4 };
        for(i = 0; i < 5; i++)
        {
            set_wide(&a, co[i] < 0 ? -1 : 0, co[i]);
            CompMulDiv(&a, mul[i], dv[i]);
            wide_str(&a, s1);
            sprintf(name, "CompMulDiv.round%d", i);
            put(name, s1);
        }
    }
    set_wide(&a, 1, 0);
    set_wide(&b, 0, 0xFFFFFFFF);
    sprintf(line, "%08lX %08lX %08lX", CompCompare(&a, &b), CompCompare(&b, &a), CompCompare(&a, &a));
    put("CompCompare", line);
    set_wide(&a, -1, 0);
    sprintf(line, "%08lX %08lX", CompCompare(&a, &b), CompCompare(&b, &a));
    put("CompCompare.neg", line);
    /* (CompSquareRoot, $11, is QuickTime 3.0's: on 2.5 the call never
       returns) */
}

static void test_fixed(void)
{
    int i, j;
    Fixed f;
    Fract c;
    char name[40];

    for(i = 0; i < NFIXED; i++)
    {
        line[0] = 0;
        for(j = 0; j < NFIXED; j += 2)
        {
            f = FixMulDiv(fixeds[i], 0x00030000, fixeds[j]);
            sprintf(line + strlen(line), "%s%08lX", j ? " " : "", f);
        }
        sprintf(name, "FixMulDiv.%d", i);
        put(name, line);
        line[0] = 0;
        for(j = 0; j < NFIXED; j += 2)
        {
            f = UnsignedFixMulDiv(fixeds[i], 0x00030000, fixeds[j]);
            sprintf(line + strlen(line), "%s%08lX", j ? " " : "", f);
        }
        sprintf(name, "UnsignedFixMulDiv.%d", i);
        put(name, line);
    }
    {
        static const Fixed degs[] = { 0, 0x001E0000, 0x002D0000, 0x005A0000, 0x00B40000, 0xFFD30000,
                                      0x01680000, 0x00010000, 0x00008000 };
        for(i = 0; i < 9; i++)
        {
            c = 0x55555555;
            f = FracSinCos(degs[i], &c);
            sprintf(line, "%08lX %08lX", f, c);
            sprintf(name, "FracSinCos.%d", i);
            note(name, line);
        }
    }
    for(i = 0; i < NFIXED; i++)
    {
        f = FixExp2(fixeds[i]);
        sprintf(line, "%08lX", f);
        f = FixLog2(fixeds[i]);
        sprintf(line + strlen(line), " %08lX", f);
        f = FixPow(fixeds[i], 0x00020000);
        sprintf(line + strlen(line), " %08lX", f);
        f = FixPow(0x00020000, fixeds[i]);
        sprintf(line + strlen(line), " %08lX", f);
        sprintf(name, "FixExpLogPow.%d", i);
        note(name, line);
    }
}

static void points(const char *name, MatrixRecord *m)
{
    FixedPoint fp[3];
    Point p[3];
    FixedRect fr;
    Rect r;
    FixedPoint corners[4];
    Boolean b;
    OSErr e;
    char n[64];
    int i;

    fp[0].x = 0x00030000; fp[0].y = 0x00050000;
    fp[1].x = 0xFFF88000; fp[1].y = 0x00004000;
    fp[2].x = 0x00000001; fp[2].y = 0x7FFF0000;
    e = TransformFixedPoints(m, fp, 3);
    sprintf(line, "%d", e);
    for(i = 0; i < 3; i++)
        sprintf(line + strlen(line), " %08lX,%08lX", fp[i].x, fp[i].y);
    sprintf(n, "%s.TransformFixedPoints", name);
    put(n, line);

    SetPt(&p[0], 3, 5);
    SetPt(&p[1], -8, 1);
    SetPt(&p[2], 100, -7);
    e = TransformPoints(m, p, 3);
    sprintf(line, "%d", e);
    for(i = 0; i < 3; i++)
        sprintf(line + strlen(line), " %d,%d", p[i].h, p[i].v);
    sprintf(n, "%s.TransformPoints", name);
    put(n, line);

    fr.left = 0x00010000; fr.top = 0x00020000; fr.right = 0x000A8000; fr.bottom = 0x00140000;
    memset(corners, 0x55, sizeof corners);
    b = TransformFixedRect(m, &fr, corners);
    sprintf(line, "%d %08lX %08lX %08lX %08lX", b, fr.left, fr.top, fr.right, fr.bottom);
    for(i = 0; i < 4; i++)
        sprintf(line + strlen(line), " %08lX,%08lX", corners[i].x, corners[i].y);
    sprintf(n, "%s.TransformFixedRect", name);
    put(n, line);

    SetRect(&r, 1, 2, 11, 21);
    b = TransformRect(m, &r, NULL);
    sprintf(line, "%d %d %d %d %d", b, r.left, r.top, r.right, r.bottom);
    sprintf(n, "%s.TransformRect", name);
    put(n, line);

    sprintf(n, "%s.GetMatrixType", name);
    sprintf(line, "%d", GetMatrixType(m));
    put(n, line);
    {
        MatrixRecord inv;
        memset(&inv, 0x55, sizeof inv);
        b = InverseMatrix(m, &inv);
        sprintf(n, "%s.InverseMatrix", name);
        sprintf(line, "%d ", b);
        matrix_str(&inv, line + strlen(line));
        put(n, line);
    }
}

static void test_matrix(void)
{
    MatrixRecord m, a;
    Rect r1, r2;
    char s[16];

    SetIdentityMatrix(&m);
    put_matrix("SetIdentityMatrix", &m);
    points("identity", &m);
    TranslateMatrix(&m, 0x00058000, 0xFFFD0000);
    put_matrix("TranslateMatrix", &m);
    points("translate", &m);
    ScaleMatrix(&m, 0x00020000, 0x00008000, 0x00010000, 0x00030000);
    put_matrix("ScaleMatrix", &m);
    points("scale", &m);
    SetIdentityMatrix(&m);
    ScaleMatrix(&m, 0xFFFF0000, 0x00030000, 0, 0);
    put_matrix("ScaleMatrix.flip", &m);
    points("flip", &m);
    SetIdentityMatrix(&m);
    RotateMatrix(&m, 0x005A0000, 0x000A0000, 0x00140000);
    put_matrix("RotateMatrix.90", &m);
    points("rotate90", &m);
    SetIdentityMatrix(&m);
    RotateMatrix(&m, 0x001E0000, 0, 0);
    put_matrix("RotateMatrix.30", &m);
    points("rotate30", &m);
    SetIdentityMatrix(&m);
    SkewMatrix(&m, 0x00008000, 0x00004000, 0x00020000, 0x00010000);
    put_matrix("SkewMatrix", &m);
    points("skew", &m);
    SetRect(&r1, 0, 0, 160, 120);
    SetRect(&r2, 10, 20, 90, 260);
    RectMatrix(&m, &r1, &r2);
    put_matrix("RectMatrix", &m);
    points("rect", &m);
    SetIdentityMatrix(&a);
    TranslateMatrix(&a, 0x00030000, 0x00040000);
    CopyMatrix(&a, &m);
    MapMatrix(&m, &r1, &r2);
    put_matrix("MapMatrix", &m);
    SetIdentityMatrix(&m);
    ScaleMatrix(&m, 0x00020000, 0x00020000, 0, 0);
    CopyMatrix(&m, &a);
    TranslateMatrix(&a, 0x00010000, 0x00010000);
    ConcatMatrix(&a, &m);
    put_matrix("ConcatMatrix", &m);
    sprintf(s, "%d %d", EqualMatrix(&a, &a), EqualMatrix(&a, &m));
    put("EqualMatrix", s);
    /* perspective */
    SetIdentityMatrix(&m);
    m.matrix[0][2] = 0x00400000;   /* 1/256 (Fract) */
    m.matrix[1][2] = 0x00100000;
    put_matrix("perspective", &m);
    points("perspective", &m);
    /* singular */
    memset(&m, 0, sizeof m);
    m.matrix[2][2] = 0x40000000;
    points("singular", &m);
}

int main(void)
{
    char results_name[64] = "qtmath_results.txt";
    char path[80];
    FILE *cfg;
    long v;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(NULL);
    InitCursor();

    if((cfg = fopen("Host:suite.cfg", "r")) != NULL)
    {
        if(fgets(results_name, sizeof results_name, cfg))
            results_name[strcspn(results_name, "\r\n")] = 0;
        fclose(cfg);
    }
    sprintf(path, "Host:%s", results_name);
    if((out = fopen(path, "w")) == NULL)
        return 1;
    /* $ABC2 is there with QuickTime on a Mac, and always on Executor */
    if(Gestalt('qtim', &v) != noErr)
        fprintf(out, "# no QuickTime: $ABC2 may be missing\n");
    test_wide();
    test_fixed();
    test_matrix();
    fprintf(out, "---\n");
    fclose(out);
    return 0;
}
