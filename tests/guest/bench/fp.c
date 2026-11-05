/* fp.c — floating point (double): a Whetstone-like loop, an FFT, a matrix
 * multiply. Built once for PowerPC and twice for 68k: with SANE (fp.c) and
 * with 68881 code (FP881 defined: the tests' names end in _881; build.mpw). */

#include "bench.h"

#include <Memory.h>
#include <math.h>

#ifdef FP881
#pragma segment Fp881
#define NAME(n) n "_881"
#define FN(f) f##_881
#else
#pragma segment Fp
#if !BENCH_PPC
#define NAME(n) n "_sane"
#else
#define NAME(n) n
#endif
#define FN(f) f
#endif

/* ---- whet: Whetstone's modules (arrays, conditionals, trigonometry,
 * calls, sqrt/exp/log), 10 passes ---- */

static double e1[4];

static void p3(double x, double y, double *z)
{
    double t = 0.499975;

    x = t * (x + y);
    y = t * (x + y);
    *z = (x + y) / 2.0;
}

static void pa(double *e)
{
    double t = 0.499975, t2 = 2.0;
    int j;

    for(j = 0; j < 6; j++)
    {
        e[0] = (e[0] + e[1] + e[2] - e[3]) * t;
        e[1] = (e[0] + e[1] - e[2] + e[3]) * t;
        e[2] = (e[0] - e[1] + e[2] + e[3]) * t;
        e[3] = (-e[0] + e[1] + e[2] + e[3]) / t2;
    }
}

static long FN(whet)(void)
{
    double x, y, z, t = 0.499975, t1 = 0.50025;
    long i, pass;
    int j;

    for(pass = 0; pass < 10; pass++)
    {
        e1[0] = 1.0, e1[1] = e1[2] = e1[3] = -1.0;
        for(i = 0; i < 120; i++)
        {
            e1[0] = (e1[0] + e1[1] + e1[2] - e1[3]) * t;
            e1[1] = (e1[0] + e1[1] - e1[2] + e1[3]) * t;
            e1[2] = (e1[0] - e1[1] + e1[2] + e1[3]) * t;
            e1[3] = (-e1[0] + e1[1] + e1[2] + e1[3]) * t;
        }
        for(i = 0; i < 140; i++)
            pa(e1);
        j = 1;
        for(i = 0; i < 345; i++)
        {
            j = j == 1 ? 2 : 3;
            j = j > 2 ? 0 : 1;
            j = j < 1 ? 1 : 0;
        }
        x = y = 0.5;
        for(i = 0; i < 22; i++)
        {
            x = t * atan(t1 * sin(x) * cos(x) / (cos(x + y) + cos(x - y) - 1.0));
            y = t * atan(t1 * sin(y) * cos(y) / (cos(x + y) + cos(x - y) - 1.0));
        }
        x = y = z = 1.0;
        for(i = 0; i < 899; i++)
            p3(x, y, &z);
        x = 0.75;
        for(i = 0; i < 93; i++)
            x = sqrt(exp(log(x) / t1));
        e1[0] += x + z + j;
    }
    return (long)(e1[0] * 1000.0);
}

/* ---- fft: 1024-point complex, radix 2, forward and back, 4 times ---- */

enum { kFftN = 1024 };
static double *fft_re, *fft_im, *fft_cos, *fft_sin;

static long FN(fft_setup)(void)
{
    long i;

    fft_re = (double *)NewPtr(4 * kFftN * sizeof(double));
    if(!fft_re)
    {
        bench_skip("no memory");
        return 1;
    }
    fft_im = fft_re + kFftN;
    fft_cos = fft_im + kFftN;
    fft_sin = fft_cos + kFftN;
    for(i = 0; i < kFftN / 2; i++)
    {
        fft_cos[i] = cos(2.0 * 3.14159265358979 * i / kFftN);
        fft_sin[i] = sin(2.0 * 3.14159265358979 * i / kFftN);
    }
    return 0;
}

static long FN(fft_done)(void)
{
    DisposePtr((Ptr)fft_re);
    return 0;
}

static void fft1(double *re, double *im, int inverse)
{
    long i, j, k, m, len, half, step;
    double tr, ti, wr, wi;

    for(i = 1, j = 0; i < kFftN; i++)
    {
        for(m = kFftN >> 1; j & m; m >>= 1)
            j ^= m;
        j |= m;
        if(i < j)
        {
            tr = re[i], re[i] = re[j], re[j] = tr;
            ti = im[i], im[i] = im[j], im[j] = ti;
        }
    }
    for(len = 2; len <= kFftN; len <<= 1)
    {
        half = len >> 1;
        step = kFftN / len;
        for(i = 0; i < kFftN; i += len)
            for(k = 0; k < half; k++)
            {
                wr = fft_cos[k * step];
                wi = inverse ? fft_sin[k * step] : -fft_sin[k * step];
                tr = re[i + k + half] * wr - im[i + k + half] * wi;
                ti = re[i + k + half] * wi + im[i + k + half] * wr;
                re[i + k + half] = re[i + k] - tr;
                im[i + k + half] = im[i + k] - ti;
                re[i + k] += tr;
                im[i + k] += ti;
            }
    }
    if(inverse)
        for(i = 0; i < kFftN; i++)
            re[i] /= kFftN, im[i] /= kFftN;
}

static long FN(fft)(void)
{
    long i;
    int pass;

    for(i = 0; i < kFftN; i++)
        fft_re[i] = (double)(i % 17) - 8.0, fft_im[i] = 0.0;
    for(pass = 0; pass < 4; pass++)
    {
        fft1(fft_re, fft_im, 0);
        fft1(fft_re, fft_im, 1);
    }
    return (long)(fft_re[5] * 1000.0);
}

/* ---- matmul: two 48x48 matrices ---- */

enum { kMatN = 48 };
static double *mat_a, *mat_b, *mat_c;

static long FN(mat_setup)(void)
{
    long i;

    mat_a = (double *)NewPtr(3 * kMatN * kMatN * sizeof(double));
    if(!mat_a)
    {
        bench_skip("no memory");
        return 1;
    }
    mat_b = mat_a + kMatN * kMatN;
    mat_c = mat_b + kMatN * kMatN;
    for(i = 0; i < kMatN * kMatN; i++)
    {
        mat_a[i] = (double)(i % 13) / 7.0;
        mat_b[i] = (double)(i % 11) / 5.0;
    }
    return 0;
}

static long FN(mat_done)(void)
{
    DisposePtr((Ptr)mat_a);
    return 0;
}

static long FN(matmul)(void)
{
    int i, j, k;
    double s;

    for(i = 0; i < kMatN; i++)
        for(j = 0; j < kMatN; j++)
        {
            s = 0.0;
            for(k = 0; k < kMatN; k++)
                s += mat_a[i * kMatN + k] * mat_b[k * kMatN + j];
            mat_c[i * kMatN + j] = s;
        }
    return (long)mat_c[kMatN + 1];
}

#ifdef FP881
static long need_fpu(void)
{
    if(!have_fpu())
    {
        bench_skip("no FPU");
        return 1;
    }
    return 0;
}

static long fft_setup_881_fpu(void)
{
    return need_fpu() || fft_setup_881();
}

static long mat_setup_881_fpu(void)
{
    return need_fpu() || mat_setup_881();
}

void add_fp_tests_881(void)
{
    bench_add(NAME("whet"), "10_passes", whet_881, need_fpu, NULL);
    bench_add(NAME("fft"), "1024x8", fft_881, fft_setup_881_fpu, fft_done_881);
    bench_add(NAME("matmul"), "48x48", matmul_881, mat_setup_881_fpu, mat_done_881);
}
#else
void add_fp_tests(void)
{
    bench_add(NAME("whet"), "10_passes", whet, NULL, NULL);
    bench_add(NAME("fft"), "1024x8", fft, fft_setup, fft_done);
    bench_add(NAME("matmul"), "48x48", matmul, mat_setup, mat_done);
}
#endif
