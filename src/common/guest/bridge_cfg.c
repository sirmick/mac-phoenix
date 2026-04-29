/*
 *  bridge_cfg.c — see bridge_cfg.h.
 *
 *  Cfg format (key=value, one per line, CR or LF terminated):
 *    bridge_dir=Host:MacPhoenix:12345
 *
 *  Only `bridge_dir` is recognised today; future keys can be added
 *  without breaking older guest builds (unknown keys are skipped).
 */
#include "bridge_cfg.h"
#include "bridge_paths.h"

#include <Files.h>
#include <Folders.h>
#include <Memory.h>

#include <stdint.h>
#include <string.h>

/* Cached prefix, e.g. "Host:MacPhoenix:12345". Plain C string for easy
 * concat; we wrap into a Pascal string in br_cfg_path. */
static char    s_prefix[120];
static int     s_loaded = 0;

/* Built-in fallback when MacPhoenix.cfg is unreadable. Matches the
 * pre-pid layout of older mac-phoenix releases. */
static const char kFallbackPrefix[] = BR_VOLUME ":" BR_PARENT;

static void copy_prefix(const char *src)
{
    size_t n = strlen(src);
    if (n >= sizeof(s_prefix)) n = sizeof(s_prefix) - 1;
    memcpy(s_prefix, src, n);
    s_prefix[n] = '\0';
}

/* Strip trailing whitespace + CR/LF from a 0-terminated string in
 * place. Cfg writers use \r so MacPerl <FILE> + Toolbox both see
 * one record; we tolerate both. */
static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0) {
        char c = s[n - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            s[--n] = '\0';
        else
            break;
    }
}

void br_cfg_load(void)
{
    if (s_loaded) return;
    s_loaded = 1;
    copy_prefix(kFallbackPrefix);

    /* FindFolder finds the Preferences folder on the BOOT disk
     * regardless of which volume we were launched from. Critical
     * for MacBrowser which usually runs from the floppy. */
    short  vRefNum = 0;
    long   dirID   = 0;
    OSErr  err = FindFolder(kOnSystemDisk, kPreferencesFolderType,
                            kDontCreateFolder, &vRefNum, &dirID);
    if (err != noErr) return;

    Str255 leaf;
    /* Pascal-string copy of BR_CFG_LEAF. */
    {
        const char *s = BR_CFG_LEAF;
        size_t n = strlen(s);
        if (n > 255) n = 255;
        leaf[0] = (unsigned char)n;
        memcpy(leaf + 1, s, n);
    }

    FSSpec spec;
    err = FSMakeFSSpec(vRefNum, dirID, leaf, &spec);
    if (err != noErr) return;

    short ref = 0;
    err = FSpOpenDF(&spec, fsRdPerm, &ref);
    if (err != noErr) return;

    /* Read up to 1 KB of cfg text. Bigger files than that are
     * malformed; truncate and parse what we got. */
    enum { kMaxCfg = 1024 };
    char buf[kMaxCfg + 1];
    long count = kMaxCfg;
    err = FSRead(ref, &count, buf);
    FSClose(ref);
    if (err != noErr && err != eofErr) return;
    if (count < 0) count = 0;
    if (count > kMaxCfg) count = kMaxCfg;
    buf[count] = '\0';

    /* Parse line by line. Recognise `bridge_dir=...`; ignore unknown
     * keys (forward-compat). */
    char *p = buf;
    while (*p) {
        char *eol = p;
        while (*eol && *eol != '\r' && *eol != '\n') eol++;
        char saved = *eol;
        *eol = '\0';

        char *eq = strchr(p, '=');
        if (eq) {
            *eq = '\0';
            char *key = p;
            char *val = eq + 1;
            rtrim(val);
            if (strcmp(key, "bridge_dir") == 0 && val[0]) {
                copy_prefix(val);
            }
        }
        if (saved == '\0') break;
        p = eol + 1;
    }
}

void br_cfg_path(unsigned char *out, const char *leaf)
{
    if (!s_loaded) br_cfg_load();
    size_t pn = strlen(s_prefix);
    size_t ln = strlen(leaf);
    /* Pascal length byte caps at 255. Total = prefix + ":" + leaf. */
    size_t total = pn + 1 + ln;
    if (total > 255) total = 255;
    out[0] = (unsigned char)total;
    size_t off = 1;
    if (pn > 0) {
        size_t take = pn;
        if (take > total) take = total;
        memcpy(out + off, s_prefix, take);
        off += take;
    }
    if (off < total + 1u) {
        out[off++] = ':';
    }
    if (off < total + 1u) {
        size_t take = ln;
        if (off + take > total + 1u) take = (total + 1u) - off;
        memcpy(out + off, leaf, take);
    }
}
