/* aehdlr68k: the Apple Event Manager's private handler-table calls (Pack8
   $092E $0830 $0831 $0632 $0833 $042F), 68k only, on the Quadra and
   on Executor (aehdlr68k.sh). HyperCard 2.4.1 makes its own tables with
   $092E and calls $0830 on them; Executor took $0830 for a lookup, upstream
   Executor's PACK 8 reading for an install that refuses a key already
   there. One table, one key, the calls in turn, what each answers and what
   a lookup finds after it. Values are 8 bytes, as the handler tables'. */
#include "probe.h"

typedef struct
{
    OSType a, b;
} Key;

typedef struct
{
    long a, b;
} Val;

/* Pascal order (upstream's C signatures reversed): the table, the imports
   vector (0: the manager's own), then the call's own arguments */
pascal OSErr aeAlloc(long capacity, long sizes, long imports, char sysZone, Handle *table)
    = { 0x303C, 0x092E, 0xA816 };
pascal OSErr ae0830(Handle table, long imports, Key *key, Val *value) = { 0x303C, 0x0830, 0xA816 };
pascal OSErr aeInstall(Handle table, long imports, Key *key, Val *value) = { 0x303C, 0x0831, 0xA816 };
pascal OSErr aeDelete(Handle table, long imports, Key *key) = { 0x303C, 0x0632, 0xA816 };
pascal OSErr aeLookup(Handle table, long imports, Key *key, Val *value) = { 0x303C, 0x0833, 0xA816 };
pascal OSErr ae042F(Handle *table, long imports) = { 0x303C, 0x042F, 0xA816 };

static Handle table;

static void lookup(const char *name, Key *k)
{
    Val v;
    OSErr err;
    char key[48];
    v.a = v.b = 0x5A5A5A5A;
    err = aeLookup(table, 0, k, &v);
    sprintf(key, "%s.lookup", name);
    out(key, "%d %08lx %08lx", err, v.a, v.b);
}

int main(void)
{
    Key k1, k2;
    Val v1, v2, v3, buf;
    OSErr err;
    probe_open("aehdlr");

    if(SECTION("table"))
    {
        k1.a = 'aevt', k1.b = 'oapp';
        k2.a = 'pnam', k2.b = '????';
        v1.a = 0x11111111, v1.b = 0x12121212;
        v2.a = 0x22222222, v2.b = 0x23232323;
        v3.a = 0x33333333, v3.b = 0x34343434;

        table = NULL;
        err = aeAlloc(8, 0x00080008, 0, 0, &table);
        out("alloc", "%d table %s", err, table ? "set" : "nil");
        if(err != noErr || !table)
        {
            probe_close();
            return 0;
        }

        lookup("empty", &k1);

        /* $0830 on a key the table doesn't have: does the value buffer
           change (a lookup's output), and is the key there afterwards? */
        buf = v1;
        err = ae0830(table, 0, &k1, &buf);
        out("0830.absent", "%d buffer %s", err, buf.a == v1.a && buf.b == v1.b ? "kept" : "changed");
        lookup("after0830", &k1);

        /* again with another value: refused, replaced or kept? */
        buf = v2;
        err = ae0830(table, 0, &k1, &buf);
        out("0830.present", "%d buffer %08lx %08lx", err, buf.a, buf.b);
        lookup("after0830again", &k1);

        /* $0831, install: what it does to a key that's there */
        buf = v3;
        err = aeInstall(table, 0, &k1, &buf);
        out("0831.present", "%d", err);
        lookup("after0831", &k1);

        /* a second key, through $0831 on its own */
        buf = v2;
        out("0831.absent", "%d", aeInstall(table, 0, &k2, &buf));
        lookup("k2", &k2);

        /* $0834 isn't called: with upstream's reading (the key at an index:
           table, imports, index, key) it stopped the Quadra's 7.6.1 */

        out("delete", "%d", aeDelete(table, 0, &k1));
        lookup("afterdelete", &k1);
        out("delete.absent", "%d", aeDelete(table, 0, &k1));

        err = ae042F(&table, 0);
        out("042F", "%d table %s", err, table ? "set" : "nil");
    }

    probe_close();
    return 0;
}
