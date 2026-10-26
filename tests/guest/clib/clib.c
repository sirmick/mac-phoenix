/* clib.c — CLibTest, a PowerPC application on StdCLib and MathLib (MrC,
   PPCLink; tools/mpw/mpw build tests/guest/clib). Executor's own StdCLib and
   MathLib (docs/executor/PPCLibs.md) against Apple's on a Power Mac: ctest
   clib_executor and clib_rom_ppc (Mac OS 9.0.4, the reference). Each check
   compares what a routine gives with what Apple's library gives; doubles are
   compared as their bits. Results go to Host:clib_results.txt through the
   File Manager (stdio is under test), in the guest suite's form: PASS/FAIL
   lines, "---" at the end. */
#include <Types.h>
#include <Memory.h>
#include <Quickdraw.h>
#include <Fonts.h>
#include <Windows.h>
#include <Menus.h>
#include <TextEdit.h>
#include <Dialogs.h>
#include <Files.h>
#include <Events.h>
#include <OSUtils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <time.h>
#include <locale.h>
#include <float.h>
#include <limits.h>
#include <fp.h>
#include <fenv.h>
#include <PLStringFuncs.h>

QDGlobals qd;

static short out_ref;
static int failures;

static void put(const char *s)
{
    long n = strlen(s);
    FSWrite(out_ref, &n, s);
}

/* The report's own formatting, independent of printf. */
static void hex32(char *out, unsigned long v)
{
    static const char digits[] = "0123456789ABCDEF";
    int i;
    for(i = 0; i < 8; i++)
        out[i] = digits[(v >> (28 - 4 * i)) & 15];
    out[8] = 0;
}

static void dec(char *out, long v)
{
    char tmp[16];
    int n = 0, i = 0;
    unsigned long u = v < 0 ? -(unsigned long)v : (unsigned long)v;
    do
    {
        tmp[n++] = (char)('0' + u % 10);
        u /= 10;
    } while(u);
    if(v < 0)
        out[i++] = '-';
    while(n)
        out[i++] = tmp[--n];
    out[i] = 0;
}

static void bits(char *out, double d)
{
    unsigned long w[2];
    memmove(w, &d, 8);
    hex32(out, w[0]);
    hex32(out + 8, w[1]);
}

/* "PASS label got" or "FAIL label got (want expected)" */
static void check(const char *label, const char *got, const char *want)
{
    int ok = strcmp(got, want) == 0;
    put(ok ? "PASS " : "FAIL ");
    put(label);
    put(" [");
    put(got);
    put("]");
    if(!ok)
    {
        put(" (want [");
        put(want);
        put("])");
        failures++;
    }
    put("\n");
}

/* Apple's answer or the exact one: where Apple's library is off by an ulp or
   approximates past 17 digits and Executor's is correctly rounded. */
static void check2(const char *label, const char *got, const char *apple, const char *exact)
{
    check(label, got, strcmp(got, exact) == 0 ? exact : apple);
}

static void info(const char *label, const char *got)
{
    put("INFO ");
    put(label);
    put(" [");
    put(got);
    put("]\n");
}

static void checkl(const char *label, long got, long want)
{
    char g[16], w[16];
    dec(g, got);
    dec(w, want);
    check(label, g, w);
}

static void checkd(const char *label, double got, const char *wantBits)
{
    char g[20];
    bits(g, got);
    check(label, g, wantBits);
}

static void checkd2(const char *label, double got, const char *apple, const char *exact)
{
    char g[20];
    bits(g, got);
    check2(label, g, apple, exact);
}

/* A negative zero MrC can't fold away. */
static double negzero(void)
{
    unsigned long w[2];
    double d;
    w[0] = 0x80000000UL;
    w[1] = 0;
    memmove(&d, w, 8);
    return d;
}

/* ---- string.h ---- */

static void test_string(void)
{
    char buf[64], *tok;
    char hay[] = "a,b,,c";
    char ov[16];

    checkl("strlen", strlen("hello"), 5);
    strcpy(buf, "abc");
    strcat(buf, "def");
    check("strcat", buf, "abcdef");
    memset(buf, 'x', 8);
    strncpy(buf, "ab", 5);
    checkl("strncpy_pad", buf[2] == 0 && buf[4] == 0 && buf[5] == 'x', 1);
    strcpy(buf, "abc");
    strncat(buf, "defgh", 2);
    check("strncat", buf, "abcde");
    checkl("strcmp_lt", strcmp("abc", "abd") < 0, 1);
    checkl("strcmp_hi", strcmp("\x80", "a") > 0, 1);
    checkl("strncmp", strncmp("abcx", "abcy", 3), 0);
    check("strchr", strchr("hello", 'l'), "llo");
    check("strrchr", strrchr("hello", 'l'), "lo");
    checkl("strchr_nul", strchr("hello", 0) - (char *)0 != 0, 1);
    check("strstr", strstr("needle in haystack", "hay"), "haystack");
    checkl("strspn", strspn("aabbcd", "ab"), 4);
    checkl("strcspn", strcspn("hello world", " "), 5);
    check("strpbrk", strpbrk("hello", "ol"), "llo");
    tok = strtok(hay, ",");
    check("strtok1", tok, "a");
    tok = strtok(NULL, ",");
    check("strtok2", tok, "b");
    tok = strtok(NULL, ",");
    check("strtok3", tok, "c");
    checkl("strtok4", strtok(NULL, ",") == NULL, 1);
    strcpy(ov, "abcdefgh");
    memmove(ov + 2, ov, 6);
    check("memmove", ov, "ababcdef");
    checkl("memcmp", memcmp("abc", "abd", 3) < 0, 1);
    check("memchr", (char *)memchr("hello", 'l', 5), "llo");
    check("strerror2", strerror(2), "No such file or directory");
    check("strerror34", strerror(34), "Math result not representable");
    check("strerror0", strerror(0), "Error 0 - No error");
    check("strerror1", strerror(1), "Permission denied");
    check("strerror11", strerror(11), "Resource temporarily unavailable, try again later");
    check("strerror16", strerror(16), "Device or resource busy");
    check("strerror30", strerror(30), "Read-only file system");
    check("strerror33", strerror(33), "Math arg out of domain of func");
    check("strerror35", strerror(35), "Unknown error");
    check("strerror_neg", strerror(-1), "Unknown error");
    checkl("strcoll", strcoll("a", "b") < 0, 1);
    checkl("strxfrm", strxfrm(buf, "xyz", sizeof buf), 3);
}

/* ---- ctype.h ---- */

static void test_ctype(void)
{
    char out[300];
    int c, n = 0;
    checkl("isalpha", isalpha('a') != 0, 1);
    checkl("isdigit", isdigit('7') != 0, 1);
    checkl("isspace_cr", isspace(13) != 0, 1);
    checkl("isspace_lf", isspace(10) != 0, 1);
    checkl("ispunct", ispunct('!') != 0, 1);
    checkl("toupper", toupper('q'), 'Q');
    checkl("tolower", tolower('Q'), 'q');
    checkl("toupper_digit", toupper('5'), '5');
    /* The table's upper half: which MacRoman characters are anything. */
    for(c = 128; c < 256; c++)
        if(isalpha(c) || ispunct(c) || isspace(c) || iscntrl(c) || isprint(c))
            out[n++] = 'x';
        else
            out[n++] = '.';
    out[n] = 0;
    check("ctype_high", out, "................................................................................................................................");
    n = 0;
    for(c = 0; c < 128; c++)
    {
        int k = (isupper(c) ? 1 : 0) | (islower(c) ? 2 : 0) | (isdigit(c) ? 4 : 0) |
                (isspace(c) ? 8 : 0) | (ispunct(c) ? 16 : 0) | (iscntrl(c) ? 32 : 0) |
                (isxdigit(c) ? 64 : 0);
        out[n++] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz!#$%&()*+,-./:;<=>?@[]^_{|}~"[k];
    }
    out[n] = 0;
    check("ctype_low", out, "WWWWWWWWWeeeeeWWWWWWWWWWWWWWWWWW8GGGGGGGGGGGGGGG))))))))))GGGGGGG%%%%%%11111111111111111111GGGGGG&&&&&&22222222222222222222GGGGW");
}

/* ---- stdlib.h ---- */

static int cmp_int(const void *a, const void *b)
{
    return *(const int *)a - *(const int *)b;
}

static void test_stdlib(void)
{
    char *end, buf[64];
    int v[6] = { 5, 3, 9, 1, 7, 3 };
    int key = 7, *found;
    div_t d;
    ldiv_t ld;
    char *p;

    checkl("atoi", atoi("  -42xyz"), -42);
    checkl("strtol_hex", strtol("0x1F", &end, 0), 31);
    checkl("strtol_oct", strtol("017", &end, 0), 15);
    checkl("strtol_end", strtol("123abc", &end, 10), 123);
    check("strtol_endp", end, "abc");
    errno = 0;
    checkl("strtol_ovf", strtol("99999999999", &end, 10), LONG_MAX);
    checkl("strtol_errno", errno, ERANGE);
    checkl("strtol_none", strtol("xyz", &end, 10), 0);
    check("strtol_none_end", end, "xyz");
    checkl("strtoul_neg", (long)strtoul("-1", &end, 10), -1);
    checkl("strtol_b36", strtol("zz", &end, 36), 1295);
    checkd("strtod", strtod("1.5e3", &end), "4097700000000000");
    checkd("atof", atof("  -0.25"), "BFD0000000000000");
    checkd("strtod_hex", strtod("0x10", &end), "0000000000000000");
    check("strtod_hex_end", end, "x10");
    checkl("abs", abs(-5), 5);
    d = div(7, -2);
    checkl("div_q", d.quot, -3);
    checkl("div_r", d.rem, 1);
    ld = ldiv(-7L, 2L);
    checkl("ldiv_q", ld.quot, -3);
    checkl("ldiv_r", ld.rem, -1);
    srand(1);
    checkl("rand1", rand(), 16838);
    checkl("rand2", rand(), 5758);
    checkl("rand3", rand(), 10113);
    qsort(v, 6, sizeof v[0], cmp_int);
    checkl("qsort", v[0] * 100000 + v[1] * 10000 + v[2] * 1000 + v[3] * 100 + v[4] * 10 + v[5], 133579);
    found = (int *)bsearch(&key, v, 6, sizeof v[0], cmp_int);
    checkl("bsearch", found ? found - v : -1, 4);
    p = (char *)malloc(10);
    strcpy(p, "123456789");
    p = (char *)realloc(p, 1000);
    check("realloc", p, "123456789");
    free(p);
    p = (char *)calloc(4, 4);
    checkl("calloc", p[0] | p[15], 0);
    free(p);
    checkl("getenv", getenv("PATH") == NULL, 1);
    checkl("system_null", system(NULL), 0);
    sprintf(buf, "%d", (int)MB_CUR_MAX);
    check("mb_cur_max", buf, "1");
}

/* ---- printf ---- */

static void test_printf(void)
{
    char buf[200];
    int n, k;
    double zero = 0.0;

    sprintf(buf, "[%d|%5d|%-5d|%05d|%+d|% d]", 42, 42, 42, 42, 42, 42);
    check("pf_d", buf, "[42|   42|42   |00042|+42| 42]");
    sprintf(buf, "[%x|%X|%#x|%o|%#o|%u]", 255, 255, 255, 8, 8, -1);
    check("pf_x", buf, "[ff|FF|0xff|10|010|4294967295]");
    sprintf(buf, "[%c|%s|%.2s|%10.3s|%-4s|]", 'A', "str", "string", "abcdef", "ab");
    check("pf_s", buf, "[A|str|st|       abc|ab  |]");
    sprintf(buf, "[%e|%E|%f|%.0f|%g|%G|%.3g]", 1234.5, 0.000123, 3.14159, 2.5, 100000.0, 1e-5, 3.14159);
    check("pf_f", buf, "[1.234500e+03|1.230000E-04|3.141590|2|100000|1E-05|3.14]");
    sprintf(buf, "[%g|%g]", 1e100, 1e-100);
    check2("pf_big", buf, "[1e+100|0]", "[1e+100|1e-100]");
    sprintf(buf, "%f", 1e20);
    check2("pf_1e20", buf, "99999999999999987584.860160", "100000000000000000000.000000");
    {
        volatile double z = 0.0;
        double nn = z / z;
        char nb[20];
        bits(nb, nn);
        check("nan_0_0", nb, "FFF8000000000000");
        sprintf(buf, "[%f|%e|%g|%5.1f|%-6e|]", 1.0 / z, -1.0 / z, nn, 1.0 / z, nan("5"));
        check("pf_inf", buf, "[INF|-INF|-NAN(000)|  INF|NAN(005)|]");
        sprintf(buf, "[%g|%f|%e]", -nn, nan("255"), -nan("3"));
        check("pf_nan2", buf, "[NAN(000)|NAN(255)|-NAN(003)]");
    }
    sprintf(buf, "[%f|%g|%e]", negzero(), negzero(), negzero());
    check("pf_negzero", buf, "[-0.000000|-0|-0.000000e+00]");
    sprintf(buf, "[%hd|%ld|%*d|%-*d|%.*f]", (short)70000, 123456789L, 6, 7, 3, 8, 2, 3.14159);
    check("pf_mod", buf, "[4464|123456789|     7|8  |3.14]");
    sprintf(buf, "[%Lf]", (long double)1.5);
    check("pf_L", buf, "[1.500000]");
    sprintf(buf, "%p", (void *)0x1234);
    check("pf_p", buf, "0x1234");
    sprintf(buf, "[%s]", (char *)NULL);
    check("pf_null", buf, "[]");
    sprintf(buf, "%p|%#o|%#X", (void *)0, 0, 0);
    check("pf_zero_alt", buf, "0x0|0|0X0");
    n = sprintf(buf, "abc%ndef", &k);
    checkl("pf_n", k, 3);
    checkl("pf_ret", n, 6);
    sprintf(buf, "%5.1f%%", 99.44);
    check("pf_pct", buf, " 99.4%");
    sprintf(buf, "%.20f", 0.1);
    check2("pf_prec20", buf, "0.10000000000000000000", "0.10000000000000000555");
    sprintf(buf, "%#.0f|%#g|%#x", 3.0, 1.5, 0);
    check("pf_alt", buf, "3.|1.50000|0x0");
    sprintf(buf, "%.15g|%.17g|%.3e|%10.4f|%-10.2e|", 0.1, 0.1, 123456.789, -3.14159, 0.000123);
    check2("pf_digits", buf, "0.1|0.1|1.235e+05|   -3.1416|1.23e-04  |",
           "0.1|0.10000000000000001|1.235e+05|   -3.1416|1.23e-04  |");
}

/* ---- scanf ---- */

static void test_scanf(void)
{
    int a = 0, b = 0, n = 0, cnt;
    char s[32], t[32], c3[4];
    float f = 0;
    double d = 0;
    short h = 0;

    cnt = sscanf("  12 abc 3.5", "%d %s %f", &a, s, &f);
    checkl("sf_cnt", cnt, 3);
    checkl("sf_d", a, 12);
    check("sf_s", s, "abc");
    checkl("sf_f", (long)(f * 10), 35);
    cnt = sscanf("ff 0x1f 017", "%x %i %i", &a, &b, &n);
    checkl("sf_x", a * 10000 + b * 100 + n, 2553115);
    cnt = sscanf("hello123world", "%[a-z]%n%d", s, &n, &a);
    checkl("sf_set", cnt, 2);
    check("sf_set_s", s, "hello");
    checkl("sf_n", n, 5);
    checkl("sf_set_d", a, 123);
    cnt = sscanf("abcdefgh", "%3c%5s", c3, t);
    c3[3] = 0;
    check("sf_c", c3, "abc");
    check("sf_width", t, "defgh");
    checkl("sf_empty", sscanf("", "%d", &a), EOF);
    checkl("sf_mismatch", sscanf("x", "%d", &a), 0);
    cnt = sscanf("1e5 -7", "%lf %hd", &d, &h);
    checkd("sf_lf", d, "40F86A0000000000");
    checkl("sf_hd", h, -7);
    cnt = sscanf("25%", "%d%%", &a);
    checkl("sf_pct", cnt, 1);
    cnt = sscanf("a:b", "%[^:]:%s", s, t);
    check("sf_neg", s, "a");
}

/* ---- stdio files ---- */

static void test_files(void)
{
    FILE *f;
    char line[80];
    int c;
    long pos;
    FInfo fi;
    char type[5], creator[5];

    remove("clibtest.tmp");
    remove("clibtest2.tmp");
    f = fopen("clibtest.tmp", "w");
    checkl("fopen_w", f != NULL, 1);
    if(!f)
        return;
    checkl("fprintf_ret", fprintf(f, "line %d\n", 1), 7);
    fputs("second\n", f);
    putc('x', f);
    fputc('y', f);
    fwrite("\nend", 1, 4, f);
    checkl("ftell_w", ftell(f), 20);
    checkl("fclose", fclose(f), 0);

    GetFInfo("\pclibtest.tmp", 0, &fi);
    memmove(type, &fi.fdType, 4);
    memmove(creator, &fi.fdCreator, 4);
    type[4] = creator[4] = 0;
    check("finfo_type", type, "TEXT");
    check("finfo_creator", creator, "ttxt");

    f = fopen("clibtest.tmp", "r");
    checkl("fopen_r", f != NULL, 1);
    if(!f)
        return;
    fgets(line, sizeof line, f);
    check("fgets", line, "line 1\n");
    c = getc(f);
    checkl("getc", c, 's');
    ungetc('S', f);
    fgets(line, sizeof line, f);
    check("ungetc", line, "Second\n");
    checkl("ftell_r", ftell(f), 14);
    fseek(f, -3, SEEK_END);
    fgets(line, sizeof line, f);
    check("fseek_end", line, "end");
    checkl("feof_after_fgets", feof(f) != 0, 1);
    checkl("getc_eof", getc(f), EOF);
    checkl("feof", feof(f) != 0, 1);
    rewind(f);
    checkl("rewind", fread(line, 1, 4, f), 4);
    line[4] = 0;
    check("fread", line, "line");
    fseek(f, 0, SEEK_SET);
    {
        int num = 0;
        checkl("fscanf", fscanf(f, "line %d", &num), 1);
        checkl("fscanf_v", num, 1);
    }
    fclose(f);

    f = fopen("clibtest.tmp", "a");
    fputs("+app", f);
    fclose(f);
    f = fopen("clibtest.tmp", "r+");
    fseek(f, 0, SEEK_END);
    pos = ftell(f);
    checkl("append", pos, 24);
    fseek(f, 0, SEEK_SET);
    fputs("LINE", f);
    fseek(f, 0, SEEK_SET);
    fgets(line, sizeof line, f);
    check("update", line, "LINE 1\n");
    fclose(f);

    checkl("rename", rename("clibtest.tmp", "clibtest2.tmp"), 0);
    errno = 0;
    checkl("fopen_missing", fopen("clibtest.tmp", "r") == NULL, 1);
    checkl("errno_missing", errno, ENOENT);
    checkl("remove", remove("clibtest2.tmp"), 0);
    checkl("remove_again", remove("clibtest2.tmp"), -1);
    {
        char name[L_tmpnam];
        tmpnam(name);
        checkl("tmpnam", strncmp(name, "tmp.", 4), 0);
    }
    checkl("stdout_fileno", stdout->_file, 1);
    checkl("stderr_flag", stderr->_flag, _IOWRT | _IOLBF);
    checkl("stdout_flag", stdout->_flag & ~_IOMYBUF, _IOWRT);
    checkl("stdin_flag", stdin->_flag & ~_IOMYBUF, _IOREAD);
}

/* ---- setjmp, signal ---- */

static jmp_buf env;
static int depth_reached;
static int caught;

static void deep(int n)
{
    depth_reached = n;
    if(n == 5)
        longjmp(env, 0);
    deep(n + 1);
}

static void on_sigint(int sig)
{
    caught = sig;
}

static void test_control(void)
{
    volatile int local = 1;
    int r;
    r = setjmp(env);
    if(r == 0)
    {
        local = 2;
        deep(0);
    }
    checkl("longjmp_zero", r, 1);
    checkl("longjmp_depth", depth_reached, 5);
    checkl("longjmp_local", local, 2);
    r = setjmp(env);
    if(r == 0)
        longjmp(env, 77);
    checkl("longjmp_val", r, 77);

    checkl("signal_old", signal(SIGINT, on_sigint) == SIG_DFL, 1);
    checkl("raise", raise(SIGINT), 0);
    checkl("raise_caught", caught, SIGINT);
    checkl("signal_reset", signal(SIGINT, SIG_IGN) == SIG_DFL, 1);
    checkl("raise_ign", raise(SIGINT), 0);
    {
        char h[12];
        hex32(h, (unsigned long)signal(12345, on_sigint));
        check("signal_bits", h, "00000001");
        checkl("signal_bits_set", signal(SIGABRT, SIG_DFL) == on_sigint, 1);
        signal(12345, SIG_DFL);
        hex32(h, (unsigned long)signal(64, on_sigint));
        check("signal_none", h, "00000001");
    }
}

/* ---- time.h, locale.h ---- */

static void test_time(void)
{
    struct tm t, *lt;
    time_t secs;
    char buf[100];

    memset(&t, 0, sizeof t);
    t.tm_year = 96;
    t.tm_mon = 1;
    t.tm_mday = 29;
    t.tm_hour = 13;
    t.tm_min = 5;
    t.tm_sec = 9;
    t.tm_isdst = -1;
    secs = mktime(&t);
    checkl("mktime", (long)secs, (long)2908443909UL);
    checkl("mktime_wday", t.tm_wday, 4);
    checkl("mktime_yday", t.tm_yday, 59);
    checkl("mktime_isdst", t.tm_isdst, 0);
    lt = localtime(&secs);
    checkl("localtime", lt->tm_year * 10000 + lt->tm_mon * 100 + lt->tm_mday, 960129);
    check("asctime", asctime(&t), "Thu Feb 29 13:05:09 1996\n");
    strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S %a %A %b %B %j %w %y %p %I %U %W", &t);
    check("strftime", buf, "1996-02-29 13:05:09 Thu Thursday Feb February 060 4 96  PM 01 08 09");
    strftime(buf, sizeof buf, "[%c] [%x] [%X] [%Z]", &t);
    check("strftime_cxX", buf, "[01:05:09 PM Thursday, February 29, 1996] [Thursday, February 29, 1996] [01:05:09 PM] []");
    {
        struct tm u;
        memset(&u, 0, sizeof u);
        u.tm_year = 101;
        u.tm_mon = 2;
        u.tm_mday = 5;
        u.tm_hour = 8;
        u.tm_min = 4;
        u.tm_sec = 3;
        mktime(&u);
        strftime(buf, sizeof buf, "[%c] [%x] [%X] [%p] [%d] [%I] [%j]", &u);
        check("strftime_small", buf, "[08:04:03 AM Monday, March 5, 2001] [Monday, March 5, 2001] [08:04:03 AM] [ AM] [05] [08] [064]");
        u.tm_hour = 0;
        strftime(buf, sizeof buf, "[%X] [%p] [%I]", &u);
        check("strftime_midnight", buf, "[12:04:03 AM] [ AM] [12]");
        u.tm_hour = 12;
        strftime(buf, sizeof buf, "[%X] [%p] [%I]", &u);
        check("strftime_noon", buf, "[12:04:03 PM] [ PM] [12]");
        check("asctime_small", asctime(&u), "Mon Mar  5 12:04:03 2001\n");
        checkl("strftime_short", strftime(buf, 5, "%Y-%m", &u), 0);
    }
    t.tm_mon = 13;
    t.tm_mday = 0;
    secs = mktime(&t);
    checkl("mktime_norm", t.tm_year * 10000 + t.tm_mon * 100 + t.tm_mday, 970031);
    checkl("difftime", (long)difftime(100, 40), 60);
    checkl("time", time(NULL) > 2900000000UL, 1);
    checkl("clock", clock() >= 0, 1);

    check("setlocale", setlocale(LC_ALL, NULL), "#0#0#1#1#1");
    check("setlocale_numeric", setlocale(LC_NUMERIC, NULL), "#0#0#1#1#1");
    check("setlocale_ctype", setlocale(LC_CTYPE, NULL), "#0#0#1#1#1");
    {
        char *r = setlocale(LC_ALL, "C");
        check("setlocale_C", r ? r : "NULL", "#0#0#1#1#1");
        r = setlocale(LC_ALL, "");
        check("setlocale_empty", r ? r : "NULL", "#0#0#2#2#2");
        r = setlocale(LC_CTYPE, "C");
        check("setlocale_ctype_C", r ? r : "NULL", "#0#0#2#2#2");
        r = setlocale(LC_NUMERIC, "C");
        info("setlocale_numeric_C", r ? r : "NULL");
        setlocale(LC_ALL, "C");
    }
    checkl("setlocale_bad", setlocale(LC_ALL, "fr") == NULL, 1);
    check("decimal_point", localeconv()->decimal_point, ".");
    checkl("frac_digits", localeconv()->frac_digits, CHAR_MAX);
}

/* ---- PLStringFuncs.h, float.h ---- */

static void test_misc(void)
{
    Str255 a, b;
    PLstrcpy(a, "\phello");
    checkl("PLstrlen", PLstrlen(a), 5);
    PLstrcat(a, "\p world");
    checkl("PLstrcat", a[0], 11);
    PLstrcpy(b, "\pworld");
    checkl("PLpos", PLpos(b, a), 7);
    checkl("PLpos_none", PLpos(a, b), 0);
    checkl("PLstrcmp", PLstrcmp(a, b) < 0, 1);
    checkl("PLstrncmp", PLstrncmp("\pabcx", "\pabcy", 3), 0);
    checkl("PLstrchr", (char *)PLstrchr(a, 'o') - (char *)a, 5);
    checkd("DBL_EPSILON", DBL_EPSILON, "3CB0000000000000");
    checkd("DBL_MAX", DBL_MAX, "7FEFFFFFFFFFFFFF");
    checkd("DBL_MIN", DBL_MIN, "0010000000000000");
    checkd("FLT_MAX", (double)FLT_MAX, "47EFFFFFE0000000");
}

/* ---- MathLib ---- */

static void test_math(void)
{
    decform df;
    decimal dc;
    char s[100], g[100];
    short ix, vp;
    double x, ip;
    int e;
    unsigned char x80[10];
    fenv_t envp;

    checkd2("sqrt2", sqrt(2.0), "3FF6A09E667F3BCC", "3FF6A09E667F3BCD");
    checkd("pow", pow(2.0, 0.5), "3FF6A09E667F3BCD");
    checkd("sin1", sin(1.0), "3FEAED548F090CEE");
    checkd("cos1", cos(1.0), "3FE14A280FB5068C");
    checkd("exp1", exp(1.0), "4005BF0A8B145769");
    checkd2("log10", log(10.0), "40026BB1BBB55515", "40026BB1BBB55516");
    checkd("atan2", atan2(1.0, -1.0), "4002D97C7F3321D2");
    checkd("hypot", hypot(3.0, 4.0), "4014000000000000");
    checkd("gamma5", gamma(5.0), "4038000000000000");
    /* lgamma: within an ulp or two (Apple's and the host's are each one off). */
    {
        double l = lgamma(10.0), want = 12.801827480081469;
        checkl("lgamma10", l > want * (1 - 1e-15) && l < want * (1 + 1e-15), 1);
    }
    checkd("erf", erf(0.5), "3FE0A7EF5C18EDD2");
    checkd("fmod", fmod(7.5, 2.0), "3FF8000000000000");
    checkd("floor", floor(-2.5), "C008000000000000");
    checkd("ceil", ceil(-2.5), "C000000000000000");
    checkd("rint", rint(2.5), "4000000000000000");
    checkd("round", round(-2.5), "C008000000000000");
    checkd("trunc", trunc(-2.7), "C000000000000000");
    checkd("pi", pi, "400921FB54442D18");
    checkd("inf", __inf(), "7FF0000000000000");
    checkd("nan17", nan("17"), "7FF8022000000000");
    checkd("nan255", nan("255"), "7FF81FE000000000");
    checkd("nan_empty", nan(""), "7FF8000000000000");
    {
        char nb[20];
        bits(nb, nan("300"));
        check("nan300", nb, "7FF81FE000000000");
        bits(nb, nan("abc"));
        check("nan_abc", nb, "7FF8000000000000");
    }
    checkd("copysign", copysign(3.0, negzero()), "C008000000000000");
    checkd("nextafter", nextafterd(1.0, 2.0), "3FF0000000000001");
    x = frexp(48.0, &e);
    checkd("frexp", x, "3FE8000000000000");
    checkl("frexp_e", e, 6);
    checkd("ldexp", ldexp(0.75, 6), "4048000000000000");
    x = modf(-3.25, &ip);
    checkd("modf", x, "BFD0000000000000");
    checkd("modf_i", ip, "C008000000000000");
    checkd("scalb", scalb(1.5, 4), "4038000000000000");
    checkd("logb", logb(8.0), "4008000000000000");
    {
        int qi;
        x = remquo(7.0, 2.0, &qi);
        checkd("remquo", x, "BFF0000000000000");
        checkl("remquo_q", qi, 4);
    }
    checkd("remainder", remainder(5.0, 2.0), "3FF0000000000000");
    checkl("relation_lt", relation(1.0, 2.0), LESSTHAN);
    checkl("relation_un", relation(1.0, nan("1")), UNORDERED);
    checkl("classify_inf", __fpclassifyd(__inf()), FP_INFINITE);
    checkl("classify_sub", __fpclassifyd(DBL_MIN / 4), FP_SUBNORMAL);
    checkl("classify_zero", __fpclassifyd(0.0), FP_ZERO);
    checkl("classify_qnan", __fpclassifyd(nan("1")), FP_QNAN);
    checkl("signbit", __signbitd(negzero()) != 0, 1);
    checkl("isnan", __isnand(nan("")) != 0, 1);
    checkl("rinttol", rinttol(2.5), 2);
    checkl("roundtol", roundtol(2.5), 3);
    checkd("compound", compound(0.1, 2.0), "3FF35C28F5C28F5D");
    checkd2("annuity", annuity(0.1, 2.0), "3FFBC4C2A50658DB", "3FFBC4C2A50658E0");
    x = 1.0;
    randomx(&x);
    randomx(&x);
    checkd("randomx", x, "41B0D63AF1000000");

    /* the floating-point environment */
    feclearexcept(FE_ALL_EXCEPT);
    checkl("fe_clear", fetestexcept(FE_ALL_EXCEPT), 0);
    feraiseexcept(FE_OVERFLOW);
    checkl("fe_raise", fetestexcept(FE_ALL_EXCEPT), FE_OVERFLOW);
    feclearexcept(FE_ALL_EXCEPT);
    {
        volatile double m1 = -1.0;
        char h[12];
        x = sqrt(m1);
        /* Apple's MathLib raises invalid; the reference machine's mame-ppc
           core keeps no FPSCR exception bits, so either is taken. */
        hex32(h, (unsigned long)fetestexcept(FE_INVALID));
        check2("fe_invalid", h, "00000000", "20000000");
        checkd("sqrt_m1", x, "7FF8002000000000");
        checkd("log_m1", log(m1), "7FF8048000000000");
        checkd("acos_2", acos(-2.0 * m1), "7FF8044000000000");
    }
    feclearexcept(FE_ALL_EXCEPT);
    checkl("fe_round", fegetround(), FE_TONEAREST);
    checkl("fe_setround", fesetround(FE_UPWARD) != 0, 1);
    checkl("fe_getround", fegetround(), FE_UPWARD);
    checkd("rint_up", rint(2.1), "4008000000000000");
    {
        volatile double one = 1.0, three = 3.0;
        checkd("div_up", one / three, "3FD5555555555556");
    }
    checkl("fe_setround_bad", fesetround(7), 0);
    fesetround(FE_TONEAREST);
    feholdexcept(&envp);
    feraiseexcept(FE_INEXACT);
    feupdateenv(&envp);
    checkl("fe_update", fetestexcept(FE_INEXACT) != 0, 1);
    feclearexcept(FE_ALL_EXCEPT);
    checkl("fe_dfl", _FE_DFL_ENV, 0);

    /* extended80 */
    x = 1.5;
    dtox80(&x, (extended80 *)x80);
    {
        char h[24];
        hex32(h, ((unsigned long)x80[0] << 24) | ((unsigned long)x80[1] << 16) |
                 ((unsigned long)x80[2] << 8) | x80[3]);
        hex32(h + 8, ((unsigned long)x80[4] << 24) | ((unsigned long)x80[5] << 16) |
                     ((unsigned long)x80[6] << 8) | x80[7]);
        h[16] = 0;
        check("dtox80", h, "3FFFC00000000000");
    }
    checkd("x80tod", x80tod((extended80 *)x80), "3FF8000000000000");

    /* decimal records */
    df.style = FLOATDECIMAL;
    df.digits = 5;
    num2dec(&df, pi, &dc);
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    check("num2dec_sig", s, "31416");
    checkl("num2dec_exp", dc.exp, -4);
    dec2str(&df, &dc, g);
    check("dec2str_float", g, " 3.1416e+0");
    df.style = FIXEDDECIMAL;
    df.digits = 2;
    num2dec(&df, -1234.5678, &dc);
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    check("num2dec_fix", s, "123457");
    checkl("num2dec_fix_exp", dc.exp, -2);
    checkl("num2dec_fix_sgn", dc.sgn, 1);
    dec2str(&df, &dc, g);
    check("dec2str_fixed", g, "-1234.57");
    num2dec(&df, 1234.5678, &dc);
    dec2str(&df, &dc, g);
    check("dec2str_fixed_pos", g, "1234.57");
    num2dec(&df, 0.001, &dc);
    dec2str(&df, &dc, g);
    check("dec2str_fixed_small", g, "0.00");
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    check("num2dec_fixed_small", s, "0");
    df.style = FLOATDECIMAL;
    df.digits = 3;
    num2dec(&df, -0.000123456, &dc);
    dec2str(&df, &dc, g);
    check("dec2str_float_neg", g, "-1.23e-4");
    num2dec(&df, 1e300, &dc);
    dec2str(&df, &dc, g);
    check("dec2str_float_big", g, " 1.00e+300");
    df.style = FIXEDDECIMAL;
    df.digits = 2;
    df.style = FLOATDECIMAL;
    df.digits = 5;
    num2dec(&df, 0.0, &dc);
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    check("num2dec_zero", s, "0");
    num2dec(&df, __inf(), &dc);
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    check("num2dec_inf", s, "I");
    num2dec(&df, nan("17"), &dc);
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    check("num2dec_nan", s, "N4011");
    dec2str(&df, &dc, g);
    check("dec2str_nan", g, " NAN(017)");
    num2dec(&df, __inf(), &dc);
    dec2str(&df, &dc, g);
    check("dec2str_inf", g, " INF");
    num2dec(&df, -__inf(), &dc);
    dec2str(&df, &dc, g);
    check("dec2str_neginf", g, "-INF");
    df.style = FIXEDDECIMAL;
    df.digits = 0;
    num2dec(&df, 1e40, &dc);
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    /* Apple's gives 36 digits of an approximation, exponent 4 (not checked). */
    info("num2dec_fixbig", s);
    ix = 0;
    str2dec("  -12.5e3xyz", &ix, &dc, &vp);
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    check("str2dec_sig", s, "125");
    checkl("str2dec_exp", dc.exp, 2);
    checkl("str2dec_sgn", dc.sgn, 1);
    checkl("str2dec_ix", ix, 9);
    checkl("str2dec_vp", vp, 0);
    checkd("dec2num", dec2num(&dc), "C0C86A0000000000");
    checkl("dec2l", dec2l(&dc), -12500);
    ix = 0;
    str2dec("1.5e", &ix, &dc, &vp);
    checkl("str2dec_prefix_ix", ix, 3);
    checkl("str2dec_prefix_vp", vp, 1);
    ix = 0;
    str2dec("abc", &ix, &dc, &vp);
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    check("str2dec_bad", s, "N0011");
    checkd("dec2num_bad", dec2num(&dc), "7FF8022000000000");
    checkl("str2dec_bad_ix", ix, 0);
    ix = 0;
    str2dec("0.000123", &ix, &dc, &vp);
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    check("str2dec_frac", s, "123");
    checkl("str2dec_frac_exp", dc.exp, -6);
    df.style = FLOATDECIMAL;
    df.digits = 17;
    num2dec(&df, 0.1, &dc);
    memmove(s, dc.sig.text, dc.sig.length);
    s[dc.sig.length] = 0;
    check2("num2dec_17", s, "10000000000000000", "10000000000000001");
    checkl("num2dec_17_exp", dc.exp, -17);
}

int main(void)
{
    OSErr err;
    long ticks;

    InitGraf(&qd.thePort);
    InitFonts();
    InitWindows();
    InitMenus();
    TEInit();
    InitDialogs(0);
    InitCursor();

    HDelete(0, 0, "\pHost:clib_results.txt");
    err = HCreate(0, 0, "\pHost:clib_results.txt", 'ttxt', 'TEXT');
    if(err == noErr)
        err = HOpen(0, 0, "\pHost:clib_results.txt", fsWrPerm, &out_ref);
    if(err != noErr)
        ExitToShell();

    test_string();
    test_ctype();
    test_stdlib();
    test_printf();
    test_scanf();
    test_files();
    test_control();
    test_time();
    test_misc();
    test_math();

    ticks = TickCount();
    Delay(30, &ticks);
    put("---\n");
    FSClose(out_ref);
    /* exit() through StdCLib, as an MPW application ends. */
    exit(failures ? 1 : 0);
    return 0;
}
