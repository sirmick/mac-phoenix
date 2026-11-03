/* StrProbe: GetIndString and GetIndPattern given an index outside the
   list (0, past the count, -1), an empty list and a missing one (the
   resources in StrProbe.r). The buffers are filled with $AA first, so the
   INFO lines show what a call wrote: the first bytes and how many changed.
   Measured on the Quadra (7.6.1) and the G3 (8.6), the same on both: the
   empty string (its length byte only) for any index outside the list,
   the pattern left alone, ResError noErr throughout. Results in Host:str_results.txt (tests/test_guest_suite.sh). */
#include <Types.h>
#include <Resources.h>
#include <TextUtils.h>
#include <ToolUtils.h>
#include <string.h>
#include <stdio.h>

static int failures;

static int changed(const unsigned char *p, int n)
{
    int i, c = 0;
    for(i = 0; i < n; i++)
        c += p[i] != 0xAA;
    return c;
}

static void str(const char *name, short id, short index, int want_len, int want_changed)
{
    Str255 s;
    int c, err;
    memset(s, 0xAA, sizeof s);
    GetIndString(s, id, index);
    err = ResError();
    c = changed(s, sizeof s);
    printf("INFO %s: GetIndString(%d, %d) len %d bytes %02x %02x %02x %02x, %d changed, ResError %d\n",
           name, id, index, s[0], s[0], s[1], s[2], s[3], c, err);
    if(s[0] == want_len && c == want_changed && err == noErr)
        printf("PASS %s\n", name);
    else
    {
        printf("FAIL %s: length %d, %d bytes changed, ResError %d (want %d, %d, 0)\n", name, s[0], c, err,
               want_len, want_changed);
        failures++;
    }
}

static void pat(const char *name, short id, short index, int want_changed)
{
    Pattern p;
    int c, err;
    memset(&p, 0xAA, sizeof p);
    GetIndPattern((void *)&p, id, index);  /* Pattern * in Apple's headers, Byte * in Executor's */
    err = ResError();
    c = changed((unsigned char *)&p, sizeof p);
    printf("INFO %s: GetIndPattern(%d, %d) %02x %02x .. %02x, %d changed, ResError %d\n",
           name, id, index, ((unsigned char *)&p)[0], ((unsigned char *)&p)[1],
           ((unsigned char *)&p)[7], c, err);
    if(c == want_changed && err == noErr)
        printf("PASS %s\n", name);
    else
    {
        printf("FAIL %s: %d bytes changed, ResError %d (want %d, 0)\n", name, c, err, want_changed);
        failures++;
    }
}

int main(void)
{
    freopen("Host:str_results.txt", "w", stdout);
    str("str_in_range", 128, 2, 3, 4);
    str("str_past_count", 128, 3, 0, 1);
    str("str_index_0", 128, 0, 0, 1);
    str("str_index_neg", 128, -1, 0, 1);
    str("str_empty_list", 129, 1, 0, 1);
    str("str_missing_list", 200, 1, 0, 1);
    pat("pat_in_range", 128, 1, 8);
    pat("pat_past_count", 128, 2, 0);
    pat("pat_index_0", 128, 0, 0);
    pat("pat_missing_list", 200, 1, 0);
    printf("%s\n---\n", failures ? "SOME FAILED" : "ALL PASSED");
    return 0;
}
