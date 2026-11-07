/*
 * session.cpp - the command line, the System Folder a session runs on, and
 * the disks' System Folder additions (session.h).
 *
 * A disk's System Folder additions (what its applications' installers put
 * in the System Folder: tools/web/make_disk.py --system-from) are copied
 * into Executor's System (hfs_sideload.h: system_additions; files it has
 * newer stay), for the disks the front end names (--additions,
 * mount-additions: the tab's are those whose record lists the System
 * running, main.js additionsFor): for the disks there at the start before
 * Executor starts, so fonts are in use at once; for one put in later,
 * before it is mounted. Hooks::additions(path, {copied, kept, restart,
 * live}) tells the front end; restart: what is in use only after a
 * restart. They stay when the disk goes, as an installer's would
 * (docs/executor/Disks.md).
 */
#include "internal.h"
#include "hfs_sideload.h"

#include <hfs/hfs.h>
#include <quickdraw/quick.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace executor_host::session {

using namespace Executor;

namespace {

Setup setup_;
Hooks hooks_;

/* The disks there at the start whose additions wait for Executor's own
   System Folder (made as it starts): at the first event call. */
std::vector<std::string> additions_pending;

}

const Setup &setup() { return setup_; }
const Hooks &hooks() { return hooks_; }

/* [--size WxH] [--disk IMAGE]... [--additions IMAGE]... [--system IMAGE]
 * [--ppc] [--ram MB] [--depth BPP] [APP]: the screen size, disk images to
 * mount (read-only), the image whose System Folder to run on (Apple's
 * System and Finder: copied into Executor's volume, and the Finder
 * started), fat applications' PowerPC code run (as --executor-ppc), the
 * memory (default: Setup's, 32 MB, 128 on Apple's System) and the depth the
 * screen starts in (as --depth; default: as the System's Macs), and an
 * application to start instead of Executor's Browser or the Finder. The
 * tab's settings (executor/web/settings.js) say which. */
Options parse_args(int argc, char **argv)
{
    Options o;
    Config &c = o.config;
    int bpp = 0;
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if(a == "--size" && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &c.width, &c.height);
        else if(a == "--scale" && i + 1 < argc)
            c.scale = atof(argv[++i]);
        else if(a == "--disk" && i + 1 < argc)
            c.disks.push_back(argv[++i]);
        else if(a == "--cdrom" && i + 1 < argc)  /* locked, as a Mac's CDs */
            c.cdroms.push_back(argv[++i]);
        else if(a == "--system" && i + 1 < argc)
            o.system_image = argv[++i];
        else if(a == "--additions" && i + 1 < argc)  /* a --disk's System Folder additions */
            o.additions.push_back(argv[++i]);
        else if(a == "--ppc")
            c.prefer_ppc = true;  /* fat applications' PowerPC code too */
        else if(a == "--ram" && i + 1 < argc)
            o.ram_mb = atoi(argv[++i]);
        else if(a == "--depth" && i + 1 < argc)
            bpp = atoi(argv[++i]);
        else if(a == "--logtraps")  /* the tab's ?logtraps= (main.js) */
            c.logtraps = true;
        else if(a == "--logtraps-filter" && i + 1 < argc)
            c.logtraps_filter = argv[++i];
        else if(a == "--logtraps-nesting" && i + 1 < argc)
            c.logtraps_nesting = atoi(argv[++i]);
        else if(a == "--debug" && i + 1 < argc)
            c.debug_options = argv[++i];
        else if(a == "--ppc-break" && i + 1 < argc)  /* ?ppcbreak= */
            c.ppc_breaks.push_back(argv[++i]);
        else
            c.app = a;
    }
    /* The depth the System's Macs start in, as natively: 8.x 32 bits, 7.x
       and Executor's own System 8 (at 8 bits 8.6's desktop pattern maps
       to one colour of the standard palette, flat lavender). */
    c.bpp = bpp;
    return o;
}

void add_to_system(const std::string &path, bool live)
{
    Additions done;
    std::string err;
    if(!system_additions(path, setup_.data_dir, &done, &err) || (!done.copied && !done.kept))
        return;
    fprintf(stderr, "%s %s: %d files added to the System Folder, %d there already\n",
            setup_.log_prefix.c_str(), path.c_str(), done.copied, done.kept);
    std::string json = "{\"copied\":" + std::to_string(done.copied) + ",\"kept\":" + std::to_string(done.kept)
        + ",\"live\":" + (live ? "true" : "false") + ",\"restart\":[";
    for(size_t i = 0; i < done.restart.size(); i++)
        json += (i ? "," : "") + json_string(done.restart[i]);
    if(hooks_.additions)
        hooks_.additions(path, json + "]}");
}

void prepare(Options &o, const Setup &setup, const Hooks &hooks)
{
    setup_ = setup;
    hooks_ = hooks;
    Config &c = o.config;
    c.ram_mb = setup.ram_mb;
    c.data_dir = setup.data_dir;
    /* The System's image stays a fixed disk (others may be removable). */
    ROMlib_fixed_image = o.system_image;
    if(!o.system_image.empty())
    {
        bool finder = false;
        std::string err;
        const char *log = setup.log_prefix.c_str();
        /* A System Folder already there is the one the front end restored
           (the tab: its saved volume, worker.js): the user's, preferences
           and all. */
        std::string sys = c.data_dir + "/System Folder";
        if(std::filesystem::exists(sys + "/System"))
        {
            finder = std::filesystem::exists(sys + "/Finder");
            fprintf(stderr, "%s the saved System Folder%s\n", log, finder ? ", starting the Finder" : "");
            if(finder && c.app.empty())
                c.app = sys + "/Finder";
            c.ram_mb = setup.apple_ram_mb;
        }
        else if(sideload_system(o.system_image, c.data_dir, &finder, &err))
        {
            fprintf(stderr, "%s System Folder from %s%s\n", log, o.system_image.c_str(),
                    finder ? ", starting the Finder" : "");
            if(finder && c.app.empty())
                c.app = c.data_dir + "/System Folder/Finder";
            c.ram_mb = setup.apple_ram_mb;
        }
        else
            fprintf(stderr, "%s no System Folder from %s: %s\n", log, o.system_image.c_str(), err.c_str());
    }
    if(o.ram_mb > 0)
        c.ram_mb = o.ram_mb;
    /* The disks' System Folder additions asked for */
    for(const std::string &disk : o.additions)
        if(std::filesystem::exists(c.data_dir + "/System Folder/System"))
            add_to_system(disk, false);
        else
            additions_pending.push_back(disk);
}

void event_call_begin()
{
    ROMlib_text_drawn = text_drawn;
    for(const std::string &path : additions_pending)
        add_to_system(path, true);
    additions_pending.clear();
}

void event_call_end()
{
    watch_images();
}

}  // namespace executor_host::session
