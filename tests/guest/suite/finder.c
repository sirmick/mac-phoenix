/* finder.c — the Finder, driven by AppleScript. Each step asks the Finder
 * to do something through its scripting dictionary (Finder Scripting
 * Extension + AppleScript) and then checks the result independently, with
 * the File Manager: the Finder must not only say it did it, the files must
 * show it. Works in a scratch folder "FinderTest" at the root of the startup
 * disk, removed first and at the end. */

#include "suite.h"

#include <Files.h>
#include <stdio.h>
#include <string.h>

static char root[300];                          /* "<startup disk>:FinderTest" */
static const char folder[] = "folder \"FinderTest\" of startup disk";
static char data[600];

/* Tells the Finder cmd; 1 and its result in out, or 0 and the error. */
static int finder(const char *cmd, char *out, int size)
{
    char src[700];
    int n;

    sprintf(src, "try\ntell application \"Finder\" to %s\non error m number n\n"
                 "return \"ERR \" & n & \": \" & m\nend try", cmd);
    if(!as_run(src, out, size))
        return 0;
    if(strncmp(out, "\"ERR ", 5) == 0)
    {
        n = strlen(out);
        memmove(out, out + 5, n - 5 + 1);
        if(n > 6 && out[n - 6] == '"')
            out[n - 6] = 0;
        return 0;
    }
    return 1;
}

/* 0: not there; 1: a file; 2: a folder. */
static int kind_of(const char *path)
{
    CInfoPBRec pb;
    Str255 name;
    size_t n = strlen(path);

    name[0] = (unsigned char)n;
    memcpy(name + 1, path, n);
    memset(&pb, 0, sizeof pb);
    pb.hFileInfo.ioNamePtr = name;
    if(PBGetCatInfoSync(&pb) != noErr)
        return 0;
    return pb.hFileInfo.ioFlAttrib & ioDirMask ? 2 : 1;
}

static void path(char *out, const char *rel)
{
    sprintf(out, "%s%s%s", root, *rel ? ":" : "", rel);
}

static int write_file(const char *p, const char *text)
{
    FILE *f = fopen(p, "w");

    if(!f)
        return 0;
    fputs(text, f);
    fclose(f);
    return 1;
}

static int same_contents(const char *p, const char *text)
{
    static char buf[sizeof data];
    FILE *f = fopen(p, "r");
    size_t n;

    if(!f)
        return 0;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = 0;
    return strcmp(buf, text) == 0;
}

/* A Finder command for test `name`: 1 if the Finder did it, else the
 * failure reported. */
static int finder_did(const char *name, const char *cmd)
{
    char r[256], detail[300];

    if(finder(cmd, r, sizeof r))
        return 1;
    sprintf(detail, "finder: %s", r);
    report_fail(name, detail);
    return 0;
}

/* The command, then the check on the disk (cond, evaluated after it). */
#define CHECK(name, cmd, cond, what)                                    \
    do                                                                  \
    {                                                                   \
        if(finder_did(name, cmd))                                       \
        {                                                               \
            if(cond)                                                    \
                report_pass(name);                                      \
            else                                                        \
                report_fail(name, what);                                \
        }                                                               \
    } while(0)

void test_finder(void)
{
    char r[256], cmd[400], p1[320], p2[320], detail[300];
    int i, n;

    if(!finder("get name of startup disk", r, sizeof r) || r[0] != '"')
    {
        report_fail("finder_scriptable", r);
        return;
    }
    report_pass("finder_scriptable");
    r[strlen(r) - 1] = 0;
    sprintf(root, "%s:FinderTest", r + 1);

    /* Start clean (a crashed earlier run may have left the folder). */
    if(kind_of(root))
    {
        sprintf(cmd, "delete %s", folder);
        finder(cmd, r, sizeof r);
    }
    finder("empty trash", r, sizeof r);

    /* Folders */
    CHECK("make_folder", "make new folder at startup disk with properties {name:\"FinderTest\"}",
          kind_of(root) == 2, "FinderTest missing");
    sprintf(cmd, "make new folder at %s with properties {name:\"Sub\"}", folder);
    path(p1, "Sub");
    CHECK("make_subfolder", cmd, kind_of(p1) == 2, "Sub missing");
    sprintf(cmd, "set name of folder \"Sub\" of %s to \"Renamed\"", folder);
    path(p2, "Renamed");
    CHECK("rename_folder", cmd, kind_of(p2) == 2 && !kind_of(p1), "rename not on disk");

    /* Files (made here, handled by the Finder) */
    data[0] = 0;
    for(i = 0; i < 20; i++)
        strcat(data, "Finder test data 0123456789\n");
    path(p1, "a.txt");
    if(write_file(p1, data))
        report_pass("create_file");
    else
        report_fail("create_file", "fopen");

    sprintf(cmd, "count items of %s", folder);
    if(finder(cmd, r, sizeof r) && sscanf(r, "%d", &n) == 1 && n == 2)
        report_pass("count_items");
    else
    {
        sprintf(detail, "got '%s' want 2", r);
        report_fail("count_items", detail);
    }

    sprintf(cmd, "get kind of folder \"Renamed\" of %s", folder);
    if(finder(cmd, r, sizeof r) && strstr(r, "folder"))
        report_pass("kind_folder");
    else
    {
        sprintf(detail, "got '%s'", r);
        report_fail("kind_folder", detail);
    }

    sprintf(cmd, "set name of file \"a.txt\" of %s to \"b.txt\"", folder);
    path(p2, "b.txt");
    CHECK("rename_file", cmd, kind_of(p2) == 1 && !kind_of(p1), "b.txt not on disk");

    sprintf(cmd, "duplicate file \"b.txt\" of %s", folder);
    path(p1, "b.txt copy");
    CHECK("duplicate_file", cmd, kind_of(p1) == 1, "'b.txt copy' missing");
    if(same_contents(p1, data))
        report_pass("duplicate_contents");
    else
        report_fail("duplicate_contents", "copy differs");

    /* "Dragging" an item into a folder: move. */
    sprintf(cmd, "move file \"b.txt copy\" of %s to folder \"Renamed\" of %s", folder, folder);
    path(p2, "Renamed:b.txt copy");
    CHECK("move_file", cmd, kind_of(p2) == 1 && !kind_of(p1), "not moved");

    sprintf(cmd, "exists file \"b.txt copy\" of folder \"Renamed\" of %s", folder);
    if(finder(cmd, r, sizeof r) && strcmp(r, "true") == 0)
        report_pass("exists_after_move");
    else
    {
        sprintf(detail, "got '%s'", r);
        report_fail("exists_after_move", detail);
    }

    /* Option-drag: a copy into another folder. */
    sprintf(cmd, "duplicate file \"b.txt\" of %s to folder \"Renamed\" of %s with replacing", folder, folder);
    path(p1, "Renamed:b.txt");
    path(p2, "b.txt");
    CHECK("duplicate_to_folder", cmd, kind_of(p1) == 1 && kind_of(p2) == 1, "not copied");

    /* Across volumes: copy to the Host shared folder. */
    HDelete(0, 0, (ConstStr255Param)"\pHost:FinderTest b.txt");
    sprintf(cmd, "set name of file \"b.txt\" of folder \"Renamed\" of %s to \"FinderTest b.txt\"", folder);
    path(p1, "Renamed:FinderTest b.txt");
    CHECK("rename_for_host", cmd, kind_of(p1) == 1, "rename failed");
    sprintf(cmd, "duplicate file \"FinderTest b.txt\" of folder \"Renamed\" of %s to disk \"Host\"", folder);
    CHECK("copy_to_other_volume", cmd, kind_of("Host:FinderTest b.txt") == 1, "not on Host:");
    if(same_contents("Host:FinderTest b.txt", data))
        report_pass("other_volume_contents");
    else
        report_fail("other_volume_contents", "copy differs");

    /* Windows */
    sprintf(cmd, "open %s", folder);
    CHECK("open_folder", cmd, 1, "");
    if(finder("get name of window 1", r, sizeof r) && strstr(r, "FinderTest"))
        report_pass("window_name");
    else
    {
        sprintf(detail, "got '%s'", r);
        report_fail("window_name", detail);
    }
    finder("set position of window 1 to {40, 60}", r, sizeof r);
    if(finder("get position of window 1", r, sizeof r) && strcmp(r, "{40, 60}") == 0)
        report_pass("window_position");
    else
    {
        sprintf(detail, "got '%s'", r);
        report_fail("window_position", detail);
    }
    if(finder("close window 1", r, sizeof r))
        report_pass("close_window");
    else
        report_fail("close_window", r);

    /* Trash */
    sprintf(cmd, "delete file \"b.txt\" of %s", folder);
    path(p1, "b.txt");
    CHECK("delete_to_trash", cmd, !kind_of(p1), "still in folder");
    if(finder("count items of trash", r, sizeof r) && sscanf(r, "%d", &n) == 1 && n >= 1)
        report_pass("trash_has_item");
    else
    {
        sprintf(detail, "got '%s'", r);
        report_fail("trash_has_item", detail);
    }
    i = finder("empty trash", detail, sizeof detail);
    if(i && finder("count items of trash", r, sizeof r) && sscanf(r, "%d", &n) == 1 && n == 0)
        report_pass("empty_trash");
    else
    {
        sprintf(cmd, "count after: '%s' (%s)", r, detail);
        report_fail("empty_trash", cmd);
    }

    /* Clean up; emptying a Trash that holds a folder with folders and files. */
    sprintf(cmd, "delete %s", folder);
    CHECK("delete_folder", cmd, !kind_of(root), "FinderTest still there");
    i = finder("empty trash", detail, sizeof detail);
    if(i && finder("count items of trash", r, sizeof r) && sscanf(r, "%d", &n) == 1 && n == 0)
        report_pass("empty_trash_folder");
    else
    {
        sprintf(cmd, "count after: '%s' (%s)", r, detail);
        report_fail("empty_trash_folder", cmd);
    }
    HDelete(0, 0, (ConstStr255Param)"\pHost:FinderTest b.txt");
}
