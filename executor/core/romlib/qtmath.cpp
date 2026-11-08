/* MacPhoenix: QuickTime's fixed-point, wide and matrix routines (trap
 * $ABC2; ImageCompression.yaml).
 *
 * Inside Macintosh: QuickTime, "Image Compression Manager" (matrix
 * functions) and the fixed-point routines of Universal Interfaces'
 * ImageCompression.h and FixMath.h. On a Mac QuickTime's INIT installs
 * them; Apple's codecs and handlers call them, so Executor has them
 * whether or not Apple's QuickTime is on the disk. Where the documents
 * leave rounding, signs and edge cases open, the answers are a 7.6.1
 * Quadra's with QuickTime 2.5 (tests/guest/qtmath, run_qtmath.sh).
 *
 * A wide is {hi, lo}: a signed 64-bit integer. A matrix maps the row
 * vector (x, y, 1): its first two columns are Fixed (16.16), the third
 * Fract (2.30). */
#include <base/common.h>
#include <ImageCompression.h>
#include <QuickDraw.h>

#include <cmath>
#include <cstring>

using namespace Executor;

namespace
{
using i128 = __int128;

int64_t get(const wide *w)
{
    return (int64_t)(((uint64_t)(uint32_t)(int32_t)w->hi << 32) | (uint32_t)w->lo);
}

void put(wide *w, int64_t v)
{
    w->hi = (int32_t)(v >> 32);
    w->lo = (uint32_t)v;
}

/* a / b rounded to the nearest, halves away from zero */
i128 div_round(i128 a, i128 b)
{
    bool neg = (a < 0) != (b < 0);
    i128 ua = a < 0 ? -a : a, ub = b < 0 ? -b : b;
    i128 q = (ua + ub / 2) / ub;
    return neg ? -q : q;
}

int32_t clamp32(i128 v)
{
    if(v > INT32_MAX)
        return INT32_MAX;
    if(v < INT32_MIN)
        return INT32_MIN;
    return (int32_t)v;
}

/* the answer for a division by zero: the largest value of the sign */
int32_t overflow_of(i128 numerator)
{
    return numerator < 0 ? INT32_MIN : INT32_MAX;
}

/* ---- matrices, as doubles: 3x3, the third column a Fract ---- */

struct M
{
    double m[3][3];
};

M load(const MatrixRecord *mr)
{
    M r;
    for(int i = 0; i < 3; i++)
    {
        r.m[i][0] = (int32_t)mr->matrix[i][0] / 65536.0;
        r.m[i][1] = (int32_t)mr->matrix[i][1] / 65536.0;
        r.m[i][2] = (int32_t)mr->matrix[i][2] / 1073741824.0;
    }
    return r;
}

Fixed to_fixed(double v)
{
    return clamp32((i128)std::llround(v * 65536.0));
}

void store(MatrixRecord *mr, const M &r)
{
    for(int i = 0; i < 3; i++)
    {
        mr->matrix[i][0] = to_fixed(r.m[i][0]);
        mr->matrix[i][1] = to_fixed(r.m[i][1]);
        mr->matrix[i][2] = clamp32((i128)std::llround(r.m[i][2] * 1073741824.0));
    }
}

M mul(const M &a, const M &b)
{
    M r;
    for(int i = 0; i < 3; i++)
        for(int j = 0; j < 3; j++)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}

M identity()
{
    M r = {};
    r.m[0][0] = r.m[1][1] = r.m[2][2] = 1;
    return r;
}

M translation(double dx, double dy)
{
    M r = identity();
    r.m[2][0] = dx;
    r.m[2][1] = dy;
    return r;
}

/* m followed by t about (x, y) */
void about(MatrixRecord *mr, const M &t, Fixed x, Fixed y)
{
    double ax = (int32_t)x / 65536.0, ay = (int32_t)y / 65536.0;
    store(mr, mul(mul(mul(load(mr), translation(-ax, -ay)), t), translation(ax, ay)));
}

bool affine(const MatrixRecord *mr)
{
    return mr->matrix[0][2] == 0 && mr->matrix[1][2] == 0 && mr->matrix[2][2] == 0x40000000;
}

/* (x, y) through the matrix, in Fixed */
bool transform(const MatrixRecord *mr, Fixed x, Fixed y, Fixed *ox, Fixed *oy)
{
    if(affine(mr))
    {
        /* exact: products in 32.32, rounded once */
        i128 px = (i128)(int32_t)x * (int32_t)mr->matrix[0][0] + (i128)(int32_t)y * (int32_t)mr->matrix[1][0];
        i128 py = (i128)(int32_t)x * (int32_t)mr->matrix[0][1] + (i128)(int32_t)y * (int32_t)mr->matrix[1][1];
        *ox = clamp32(div_round(px, 65536) + (int32_t)mr->matrix[2][0]);
        *oy = clamp32(div_round(py, 65536) + (int32_t)mr->matrix[2][1]);
        return true;
    }
    M m = load(mr);
    double fx = (int32_t)x / 65536.0, fy = (int32_t)y / 65536.0;
    double w = fx * m.m[0][2] + fy * m.m[1][2] + m.m[2][2];
    if(w == 0)
        return false;
    *ox = to_fixed((fx * m.m[0][0] + fy * m.m[1][0] + m.m[2][0]) / w);
    *oy = to_fixed((fx * m.m[0][1] + fy * m.m[1][1] + m.m[2][1]) / w);
    return true;
}

/* rectangles stay rectangles: no rotation (a quarter turn neither, on a
   Mac), skew or perspective */
bool keeps_rects(const MatrixRecord *mr)
{
    return affine(mr) && mr->matrix[0][1] == 0 && mr->matrix[1][0] == 0;
}

/* the four corners through the matrix: top left, top right, bottom
   right, bottom left; their bounds into fr */
bool transform_rect(const MatrixRecord *mr, Fixed l, Fixed t, Fixed r, Fixed b, FixedRect *fr,
                    FixedPoint *fpp)
{
    const Fixed xs[4] = { l, r, r, l }, ys[4] = { t, t, b, b };
    Fixed px[4], py[4];
    /* a Mac's QuickTime 2.5 maps no rectangle through a perspective
       matrix: the corners as they are, the rectangle left alone */
    bool persp = !affine(mr);
    for(int i = 0; i < 4; i++)
        if(persp)
            px[i] = xs[i], py[i] = ys[i];
        else
            transform(mr, xs[i], ys[i], &px[i], &py[i]);
    if(fpp)
        for(int i = 0; i < 4; i++)
        {
            fpp[i].x = px[i];
            fpp[i].y = py[i];
        }
    if(persp)
        return false;
    Fixed minx = px[0], maxx = px[0], miny = py[0], maxy = py[0];
    for(int i = 1; i < 4; i++)
    {
        minx = std::min(minx, px[i]);
        maxx = std::max(maxx, px[i]);
        miny = std::min(miny, py[i]);
        maxy = std::max(maxy, py[i]);
    }
    fr->left = minx;
    fr->top = miny;
    fr->right = maxx;
    fr->bottom = maxy;
    return keeps_rects(mr);
}

/* rounded to the nearest, halves away from zero */
INTEGER fixed_to_int(Fixed f)
{
    int32_t v = f;
    return (INTEGER)(v < 0 ? -((-(int64_t)v + 0x8000) >> 16) : (v + 0x8000) >> 16);
}
}

/* ---- wides ---- */

void Executor::C_CompAdd(wide *src, wide *dst)
{
    put(dst, (int64_t)((uint64_t)get(dst) + (uint64_t)get(src)));
}

void Executor::C_CompSub(wide *src, wide *dst)
{
    put(dst, (int64_t)((uint64_t)get(dst) - (uint64_t)get(src)));
}

void Executor::C_CompNeg(wide *dst)
{
    put(dst, (int64_t)(0 - (uint64_t)get(dst)));
}

/* shift > 0: right, rounded to the nearest (halves up); < 0: left */
void Executor::C_CompShift(wide *src, INTEGER shift)
{
    i128 v = get(src);
    if(shift > 0)
        v = shift >= 100 ? 0 : (v + ((i128)1 << (shift - 1))) >> shift;
    else if(shift < 0)
        v = shift <= -64 ? 0 : (i128)(int64_t)((uint64_t)(int64_t)v << -shift);
    put(src, (int64_t)v);
}

void Executor::C_CompMul(LONGINT src1, LONGINT src2, wide *dst)
{
    put(dst, (int64_t)src1 * src2);
}

/* truncated, the remainder with the numerator's sign; a quotient that
   doesn't fit is the largest of its sign, the remainder $80000000 */
LONGINT Executor::C_CompDiv(wide *numerator, LONGINT denominator, GUEST<LONGINT> *remainder)
{
    i128 n = get(numerator);
    if(denominator == 0)
    {
        if(remainder)
            *remainder = INT32_MIN;
        return overflow_of(n);
    }
    i128 q = n / denominator;
    if(q > INT32_MAX || q < INT32_MIN)
    {
        if(remainder)
            *remainder = INT32_MIN;
        return clamp32(q);
    }
    if(remainder)
        *remainder = (int32_t)(n - q * denominator);
    return (int32_t)q;
}

/* rounded down */
void Executor::C_CompFixMul(wide *compSrc, Fixed fixSrc, wide *compDst)
{
    put(compDst, (int64_t)(((i128)get(compSrc) * (int32_t)fixSrc) >> 16));
}

namespace
{
/* co * mul / divisor: the quotient's magnitude, truncated or rounded,
   and the remainder's (a Quadra's are never negative) */
void mul_div(wide *co, LONGINT mul, LONGINT divisor, bool round, GUEST<LONGINT> *remainder)
{
    i128 p = (i128)get(co) * mul;
    if(!divisor)
    {
        put(co, p < 0 ? INT64_MIN : INT64_MAX);
        if(remainder)
            *remainder = 0;
        return;
    }
    i128 up = p < 0 ? -p : p, ud = divisor < 0 ? -(i128)divisor : divisor;
    i128 q = round ? (up + ud / 2) / ud : up / ud;
    if(remainder)
        *remainder = (int32_t)(up % ud);
    if(divisor == INT32_MIN)
    {
        /* a Quadra's: the low long with the numerator's sign, the high
           one the other way */
        put(co, (int64_t)(((uint64_t)(p < 0 ? 0 : 0xFFFFFFFFu) << 32)
                          | (uint32_t)(p < 0 ? -(int64_t)q : (int64_t)q)));
        return;
    }
    put(co, (int64_t)(((p < 0) != (divisor < 0)) ? -q : q));
}
}

/* rounded to the nearest */
void Executor::C_CompMulDiv(wide *co, LONGINT mul, LONGINT divisor)
{
    mul_div(co, mul, divisor, true, nullptr);
}

/* truncated; the remainder's magnitude. A multiplier equal to the divisor
   leaves both as they were (a Quadra's). */
void Executor::C_CompMulDivTrunc(wide *co, LONGINT mul, LONGINT divisor, GUEST<LONGINT> *remainder)
{
    if(mul == divisor)
        return;
    mul_div(co, mul, divisor, false, remainder);
}

LONGINT Executor::C_CompCompare(const wide *a, const wide *minusb)
{
    int64_t x = get(a), y = get(minusb);
    return x > y ? 1 : x < y ? -1 : 0;
}

ULONGINT Executor::C_CompSquareRoot(const wide *src)
{
    uint64_t v = (uint64_t)get(src);
    uint64_t r = (uint64_t)std::sqrt((long double)v);
    while(r * r > v)
        r--;
    while((r + 1) * (r + 1) <= v && r + 1 <= UINT32_MAX)
        r++;
    return (ULONGINT)r;
}

/* ---- fixed ---- */

namespace
{
/* src * mul / divisor as a Quadra's QuickTime 2.5 answers it: the
   magnitudes divided, rounded by adding half the divisor taken as a
   signed long (so a divisor of $80000000 rounds down by a half); a
   quotient that doesn't fit is $80000000 with the high long of the
   product's magnitude in it, and a divisor of 0 is $80000000 | mul. */
Fixed fix_mul_div(Fixed src, Fixed mul, Fixed divisor, bool is_signed)
{
    if(!divisor)
        return (Fixed)(0x80000000u | (uint32_t)mul);
    i128 p = is_signed ? (i128)(int32_t)src * (int32_t)mul : (i128)(uint32_t)src * (uint32_t)mul;
    bool neg = is_signed && ((p < 0) != ((int32_t)divisor < 0));
    uint64_t up = (uint64_t)(p < 0 ? -p : p);
    uint32_t ud = is_signed && (int32_t)divisor < 0 ? -(uint32_t)divisor : (uint32_t)divisor;
    int64_t half = (int32_t)ud >> 1;
    uint64_t q;
    bool overflow;
    if(is_signed)
    {
        i128 sum = (i128)up + half;
        q = sum < 0 ? 0 : (uint64_t)(sum / ud);
        overflow = q > INT32_MAX;
    }
    else
    {
        /* measured: a divisor above $80000000 truncates; $80000000 takes
           the signed half off, wrapping */
        if(ud > 0x80000000u)
            half = 0;
        q = (up + (uint64_t)half) / ud;
        overflow = q > UINT32_MAX;
    }
    if(overflow)
    {
        /* (dividing by -1: the high long of the product's complement) */
        uint64_t hi = (int32_t)divisor == -1 && p < 0 ? ~(uint64_t)p : up;
        return (Fixed)(0x80000000u | (uint32_t)(hi >> 32));
    }
    return (Fixed)(uint32_t)(neg ? -q : q);
}
}

Fixed Executor::C_FixMulDiv(Fixed src, Fixed mul, Fixed divisor)
{
    return fix_mul_div(src, mul, divisor, true);
}

Fixed Executor::C_UnsignedFixMulDiv(Fixed src, Fixed mul, Fixed divisor)
{
    return fix_mul_div(src, mul, divisor, false);
}

/* degree: in degrees */
Fract Executor::C_FracSinCos(Fixed degree, GUEST<Fract> *cosOut)
{
    double rad = (int32_t)degree / 65536.0 * M_PI / 180.0;
    if(cosOut)
        *cosOut = clamp32((i128)std::llround(std::cos(rad) * 1073741824.0));
    return clamp32((i128)std::llround(std::sin(rad) * 1073741824.0));
}

/* The transcendental ones: a Mac's are approximations (to about 2^-12);
   these are exact, saturated as a Mac's are. */
namespace
{
Fixed saturated(double v)
{
    v *= 65536.0;
    if(!(v < 2147483647.0))
        return INT32_MAX;
    if(v < -2147483648.0)
        return INT32_MIN;
    return (Fixed)std::llround(v);
}
}

Fixed Executor::C_FixExp2(Fixed src)
{
    return saturated(std::exp2((int32_t)src / 65536.0));
}

/* rounded down; 0 and below: $80000000 */
Fixed Executor::C_FixLog2(Fixed src)
{
    if((int32_t)src <= 0)
        return INT32_MIN;
    return (Fixed)std::floor(std::log2((int32_t)src / 65536.0) * 65536.0);
}

/* a base of 0 or below: 0 */
Fixed Executor::C_FixPow(Fixed base, Fixed exp)
{
    if((int32_t)base <= 0)
        return 0;
    return saturated(std::pow((int32_t)base / 65536.0, (int32_t)exp / 65536.0));
}

/* ---- matrices ---- */

INTEGER Executor::C_GetMatrixType(const MatrixRecord *m)
{
    if(!affine(m))
        return perspectiveMatrixType;
    bool translate = m->matrix[2][0] != 0 || m->matrix[2][1] != 0;
    if(m->matrix[0][1] != 0 || m->matrix[1][0] != 0)
        return translate ? linearTranslateMatrixType : linearMatrixType;
    if(m->matrix[0][0] != 0x10000 || m->matrix[1][1] != 0x10000)
        return translate ? scaleTranslateMatrixType : scaleMatrixType;
    return translate ? translateMatrixType : identityMatrixType;
}

void Executor::C_SetIdentityMatrix(MatrixRecord *matrix)
{
    memset(matrix, 0, sizeof *matrix);
    matrix->matrix[0][0] = 0x10000;
    matrix->matrix[1][1] = 0x10000;
    matrix->matrix[2][2] = 0x40000000;
}

void Executor::C_TranslateMatrix(MatrixRecord *m, Fixed deltaH, Fixed deltaV)
{
    if(affine(m))
    {
        m->matrix[2][0] = (int32_t)m->matrix[2][0] + (int32_t)deltaH;
        m->matrix[2][1] = (int32_t)m->matrix[2][1] + (int32_t)deltaV;
        return;
    }
    store(m, mul(load(m), translation((int32_t)deltaH / 65536.0, (int32_t)deltaV / 65536.0)));
}

void Executor::C_ScaleMatrix(MatrixRecord *m, Fixed scaleX, Fixed scaleY, Fixed aboutX, Fixed aboutY)
{
    M s = identity();
    s.m[0][0] = (int32_t)scaleX / 65536.0;
    s.m[1][1] = (int32_t)scaleY / 65536.0;
    about(m, s, aboutX, aboutY);
}

void Executor::C_RotateMatrix(MatrixRecord *m, Fixed degrees, Fixed aboutX, Fixed aboutY)
{
    GUEST<Fract> c;
    Fract s = C_FracSinCos(degrees, &c);
    M r = identity();
    r.m[0][0] = (int32_t)c / 1073741824.0;
    r.m[0][1] = s / 1073741824.0;
    r.m[1][0] = -r.m[0][1];
    r.m[1][1] = r.m[0][0];
    about(m, r, aboutX, aboutY);
}

void Executor::C_SkewMatrix(MatrixRecord *m, Fixed skewX, Fixed skewY, Fixed aboutX, Fixed aboutY)
{
    M k = identity();
    k.m[1][0] = (int32_t)skewX / 65536.0;
    k.m[0][1] = (int32_t)skewY / 65536.0;
    about(m, k, aboutX, aboutY);
}

/* b = b, then a (a Mac's order) */
void Executor::C_ConcatMatrix(const MatrixRecord *a, MatrixRecord *b)
{
    store(b, mul(load(b), load(a)));
}

Boolean Executor::C_InverseMatrix(const MatrixRecord *m, MatrixRecord *im)
{
    /* a Mac's 2.5: a perspective matrix answers true and leaves im alone;
       scaling and translation are inverted per axis, saturated (a zero
       scale gives $7FFFFFFF) */
    if(!affine(m))
        return true;
    if(m->matrix[0][1] == 0 && m->matrix[1][0] == 0)
    {
        Fixed sx = m->matrix[0][0], sy = m->matrix[1][1];
        Fixed ix = sx ? fix_mul_div(0x10000, 0x10000, sx, true) : INT32_MAX;
        Fixed iy = sy ? fix_mul_div(0x10000, 0x10000, sy, true) : INT32_MAX;
        C_SetIdentityMatrix(im);
        im->matrix[0][0] = ix;
        im->matrix[1][1] = iy;
        im->matrix[2][0] = clamp32(-div_round((i128)(int32_t)m->matrix[2][0] * (int32_t)ix, 65536));
        im->matrix[2][1] = clamp32(-div_round((i128)(int32_t)m->matrix[2][1] * (int32_t)iy, 65536));
        return true;
    }
    M a = load(m);
    const auto &x = a.m;
    double det = x[0][0] * (x[1][1] * x[2][2] - x[1][2] * x[2][1])
        - x[0][1] * (x[1][0] * x[2][2] - x[1][2] * x[2][0])
        + x[0][2] * (x[1][0] * x[2][1] - x[1][1] * x[2][0]);
    if(det == 0)
        return false;
    M r;
    r.m[0][0] = (x[1][1] * x[2][2] - x[1][2] * x[2][1]) / det;
    r.m[0][1] = (x[0][2] * x[2][1] - x[0][1] * x[2][2]) / det;
    r.m[0][2] = (x[0][1] * x[1][2] - x[0][2] * x[1][1]) / det;
    r.m[1][0] = (x[1][2] * x[2][0] - x[1][0] * x[2][2]) / det;
    r.m[1][1] = (x[0][0] * x[2][2] - x[0][2] * x[2][0]) / det;
    r.m[1][2] = (x[0][2] * x[1][0] - x[0][0] * x[1][2]) / det;
    r.m[2][0] = (x[1][0] * x[2][1] - x[1][1] * x[2][0]) / det;
    r.m[2][1] = (x[0][1] * x[2][0] - x[0][0] * x[2][1]) / det;
    r.m[2][2] = (x[0][0] * x[1][1] - x[0][1] * x[1][0]) / det;
    /* scaled so that the corner is 1, as a matrix is kept */
    if(r.m[2][2] != 0)
    {
        double k = 1 / r.m[2][2];
        for(auto &row : r.m)
            for(double &v : row)
                v *= k;
    }
    store(im, r);
    return true;
}

namespace
{
M rect_map(const Rect *from, const Rect *to)
{
    M r = identity();
    double fw = from->right - from->left, fh = from->bottom - from->top;
    r.m[0][0] = fw ? (to->right - to->left) / fw : 1;
    r.m[1][1] = fh ? (to->bottom - to->top) / fh : 1;
    r.m[2][0] = to->left - from->left * r.m[0][0];
    r.m[2][1] = to->top - from->top * r.m[1][1];
    return r;
}
}

void Executor::C_RectMatrix(MatrixRecord *matrix, const Rect *srcRect, const Rect *dstRect)
{
    store(matrix, rect_map(srcRect, dstRect));
}

/* the matrix, then from's coordinates mapped to to's */
void Executor::C_MapMatrix(MatrixRecord *matrix, const Rect *fromRect, const Rect *toRect)
{
    store(matrix, mul(load(matrix), rect_map(fromRect, toRect)));
}

void Executor::C_CopyMatrix(const MatrixRecord *m1, MatrixRecord *m2)
{
    memmove(m2, m1, sizeof *m2);
}

Boolean Executor::C_EqualMatrix(const MatrixRecord *m1, const MatrixRecord *m2)
{
    return memcmp(m1, m2, sizeof *m1) == 0;
}

/* a perspective matrix: -1, the points left alone (a Mac's 2.5) */
OSErr Executor::C_TransformFixedPoints(const MatrixRecord *m, FixedPoint *fpt, LONGINT count)
{
    if(!affine(m))
        return -1;
    for(LONGINT i = 0; i < count; i++)
    {
        Fixed x, y;
        transform(m, fpt[i].x, fpt[i].y, &x, &y);
        fpt[i].x = x;
        fpt[i].y = y;
    }
    return noErr;
}

OSErr Executor::C_TransformPoints(const MatrixRecord *mp, GUEST<Point> *pt1, LONGINT count)
{
    if(!affine(mp))
        return -1;
    for(LONGINT i = 0; i < count; i++)
    {
        Point p = pt1[i];
        Fixed x, y;
        transform(mp, (int32_t)p.h << 16, (int32_t)p.v << 16, &x, &y);
        p.h = fixed_to_int(x);
        p.v = fixed_to_int(y);
        pt1[i] = p;
    }
    return noErr;
}

Boolean Executor::C_TransformFixedRect(const MatrixRecord *m, FixedRect *fr, FixedPoint *fpp)
{
    return transform_rect(m, fr->left, fr->top, fr->right, fr->bottom, fr, fpp);
}

Boolean Executor::C_TransformRect(const MatrixRecord *m, Rect *r, FixedPoint *fpp)
{
    FixedRect fr;
    Boolean ok = transform_rect(m, (int32_t)r->left << 16, (int32_t)r->top << 16, (int32_t)r->right << 16,
                                (int32_t)r->bottom << 16, &fr, fpp);
    if(!affine(m))
        return false; /* left alone */
    r->left = fixed_to_int(fr.left);
    r->top = fixed_to_int(fr.top);
    r->right = fixed_to_int(fr.right);
    r->bottom = fixed_to_int(fr.bottom);
    return ok;
}
