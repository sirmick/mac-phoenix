/* MacPhoenix: run the System Folder's extensions at startup.
 *
 * 7.5.5's loader (System 'lmgr' 0) goes through Extensions, Control Panels
 * and the System Folder itself in name order, and for every file of type
 * INIT, cdev, RDEV or fext opens its resource fork, grows the System heap by the
 * file's 'sysz' request, and calls each 'INIT' resource with the file as
 * the current resource file; then it closes the file (an INIT that wants
 * to stay keeps its code with DetachResource).
 *
 * Here only the files on the allow list run: res/extension-policy.txt
 * (embedded), replaced by <data_dir>/extension-policy.txt when that file
 * exists. tools/macdecode/inits.yaml says which extensions need what.
 */
#include <base/common.h>
#include <FileMgr.h>
#include <AliasMgr.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <OSUtil.h>
#include <Package.h>
#include <file/file.h>
#include <mman/mman.h>
#include <rsys/extensions.h>
#include <rsys/helppkg.h>
#include <SegmentLdr.h>
#include <rsys/component.h>
#include <rsys/version.h>
#include <util/macstrings.h>

#include <cmrc/cmrc.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

CMRC_DECLARE(resources);

using namespace Executor;

namespace
{
std::set<std::string> load_allow_list()
{
    std::set<std::string> allow;
    std::string override_path = (fs::path(ROMlib_system_folder_path()).parent_path()
                                 / "extension-policy.txt").string();
    std::string text;
    std::ifstream user(override_path);
    if(user)
    {
        std::stringstream ss;
        ss << user.rdbuf();
        text = ss.str();
        fprintf(stderr, "[Executor] extension policy: %s\n", override_path.c_str());
    }
    else
    {
        auto efs = cmrc::resources::get_filesystem();
        auto f = efs.open("extension-policy.txt");
        text.assign(f.begin(), f.end());
    }
    std::istringstream in(text);
    std::string line;
    while(std::getline(in, line))
    {
        auto hash = line.find('#');
        if(hash != std::string::npos)
            line.resize(hash);
        while(!line.empty() && isspace((unsigned char)line.back()))
            line.pop_back();
        size_t start = 0;
        while(start < line.size() && isspace((unsigned char)line[start]))
            start++;
        if(start < line.size())
            allow.insert(line.substr(start));
    }
    return allow;
}

struct Item
{
    Str63 name;
    OSType type;
};

/* The files of one folder, in the order the loader visits them. */
std::vector<Item> folder_files(INTEGER vref, LONGINT dirid)
{
    std::vector<Item> out;
    for(INTEGER i = 1;; i++)
    {
        CInfoPBRec pb = {};
        Str63 name;
        pb.hFileInfo.ioNamePtr = name;
        pb.hFileInfo.ioVRefNum = vref;
        pb.hFileInfo.ioDirID = dirid;
        pb.hFileInfo.ioFDirIndex = i;
        if(PBGetCatInfo(&pb, false) != noErr)
            break;
        if(pb.hFileInfo.ioFlAttrib & 0x10) /* a directory */
            continue;
        Item it;
        memcpy(it.name, name, name[0] + 1);  /* a host copy, for sorting */
        it.type = pb.hFileInfo.ioFlFndrInfo.fdType;
        out.push_back(it);
    }
    /* HFS keeps a directory sorted (case- and diacritical-insensitively);
       a host directory doesn't. */
    std::sort(out.begin(), out.end(), [](const Item &a, const Item &b) {
        return RelString(a.name, b.name, false, false) < 0;
    });
    return out;
}

/* A Mac Roman name as UTF-8, as the allow list is written. */
std::string to_string(ConstStringPtr p)
{
    std::string out;
    for(char32_t c : toUnicode(mac_string_view(p)))
    {
        if(c < 0x80)
            out += (char)c;
        else if(c < 0x800)
            out += (char)(0xC0 | c >> 6), out += (char)(0x80 | (c & 0x3F));
        else
            out += (char)(0xE0 | c >> 12), out += (char)(0x80 | (c >> 6 & 0x3F)),
                out += (char)(0x80 | (c & 0x3F));
    }
    return out;
}

/* Call one INIT: its code, as the loader does (JSR with A0 at the code). */
void call_init(Handle h)
{
    HLock(h);
    LONGINT d[8], a[7];
    for(int i = 0; i < 8; i++)
        d[i] = EM_DREG(i);
    for(int i = 0; i < 7; i++)
        a[i] = EM_AREG(i);
    EM_A0 = US_TO_SYN68K(*h);
    execute68K(US_TO_SYN68K(*h));
    for(int i = 0; i < 8; i++)
        EM_DREG(i) = d[i];
    for(int i = 0; i < 7; i++)
        EM_AREG(i) = a[i];
}

void run_file(INTEGER vref, LONGINT dirid, const Item &it)
{
    /* In the System heap, as the loader runs them: an INIT's code and
       what it allocates stay after its file closes, and NewGestalt takes
       selector functions from the System heap only. */
    TheZoneGuard guard(LM(SysZone));

    /* Guest code sees the name: on this (low) stack, not the host heap. */
    Str63 name;
    memcpy(name, it.name, it.name[0] + 1);
    INTEGER rn = HOpenResFile(vref, dirid, name, fsRdPerm);
    if(rn == -1)
    {
        fprintf(stderr, "[Executor] extension %s: can't open (%d)\n",
                to_string(it.name).c_str(), (int)LM(ResErr));
        return;
    }
    INTEGER saved = CurResFile();
    UseResFile(rn);

    if(Handle sysz = Get1Resource("sysz"_4, 0))
    {
        int32_t want = *(GUEST<int32_t> *)*sysz;
        int32_t have = FreeMemSys();
        if(have < want)
            fprintf(stderr, "[Executor] extension %s: asks %d bytes of System heap, %d free\n",
                    to_string(it.name).c_str(), want, have);
    }

    /* 'INIT' resources by ID. */
    std::vector<INTEGER> ids;
    INTEGER n = Count1Resources("INIT"_4);
    for(INTEGER i = 1; i <= n; i++)
        if(Handle h = Get1IndResource("INIT"_4, i))
        {
            GUEST<INTEGER> id;
            GUEST<ResType> type;
            Str255 rname;
            GetResInfo(h, &id, &type, rname);
            ids.push_back(id);
        }
    std::sort(ids.begin(), ids.end());
    for(INTEGER id : ids)
    {
        UseResFile(rn);
        Handle h = Get1Resource("INIT"_4, id);
        if(!h || !*h)
            continue;
        fprintf(stderr, "[Executor] extension %s: INIT %d\n", to_string(it.name).c_str(), id);
        call_init(h);
    }

    UseResFile(saved);
    CloseResFile(rn);
}
}

namespace
{
/* Code that runs at startup: the loader's INITs, Finder's extensions
   (fext) and background applications (appe). A cdev counts when it has
   INIT resources; a plain control panel stays where it is. */
bool runs_at_startup(INTEGER vref, LONGINT dirid, const Item &it)
{
    if(it.type == "INIT"_4 || it.type == "RDEV"_4 || it.type == "fext"_4 || it.type == "appe"_4)
        return true;
    if(it.type != "cdev"_4)
        return false;
    Str63 name;
    memcpy(name, it.name, it.name[0] + 1);
    INTEGER saved = CurResFile();
    INTEGER rn = HOpenResFile(vref, dirid, name, fsRdPerm);
    if(rn == -1)
        return false;
    UseResFile(rn);
    bool inits = Count1Resources("INIT"_4) > 0;
    UseResFile(saved);
    CloseResFile(rn);
    return inits;
}

/* A sub-folder of the System Folder, made if asked; 0 if none. */
LONGINT sub_folder(INTEGER vref, LONGINT sysdir, const char *cname, bool create)
{
    Str63 name;
    name[0] = strlen(cname);
    memcpy(name + 1, cname, name[0]);
    CInfoPBRec pb = {};
    pb.hFileInfo.ioNamePtr = name;
    pb.hFileInfo.ioVRefNum = vref;
    pb.hFileInfo.ioDirID = sysdir;
    if(PBGetCatInfo(&pb, false) == noErr && (pb.hFileInfo.ioFlAttrib & 0x10))
        return pb.dirInfo.ioDrDirID;
    if(!create)
        return 0;
    HParamBlockRec hpb = {};
    hpb.fileParam.ioNamePtr = name;
    hpb.fileParam.ioVRefNum = vref;
    hpb.fileParam.ioDirID = sysdir;
    if(PBDirCreate(&hpb, false) != noErr)
        return 0;
    return hpb.fileParam.ioDirID;
}

void move(INTEGER vref, LONGINT from, const Item &it, LONGINT to, const char *where)
{
    Str63 name;
    memcpy(name, it.name, it.name[0] + 1);
    CMovePBRec pb = {};
    pb.ioNamePtr = name;
    pb.ioVRefNum = vref;
    pb.ioDirID = from;
    pb.ioNewName = nullptr;
    pb.ioNewDirID = to;
    OSErr err = PBCatMove(&pb, false);
    fprintf(stderr, "[Executor] extension %s: %s%s\n", to_string(it.name).c_str(), where,
            err == noErr ? "" : " (failed)");
}
}

/* As Extensions Manager does: what isn't allowed waits in "<folder>
   (Disabled)", where neither the loader nor the Finder looks; what is
   allowed comes back. */
static void sort_out_disabled(const std::set<std::string> &allow)
{
    GUEST<INTEGER> vref_g;
    GUEST<LONGINT> sysdir_g;
    if(FindFolder(-32768 /* kOnSystemDisk */, "macs"_4, false, &vref_g, &sysdir_g) != noErr)
        return;
    INTEGER vref = vref_g;
    LONGINT sysdir = sysdir_g;
    struct Pair
    {
        const char *active, *disabled;
    };
    for(Pair p : { Pair { "Extensions", "Extensions (Disabled)" },
                   Pair { "Control Panels", "Control Panels (Disabled)" },
                   Pair { nullptr, "System Extensions (Disabled)" } })
    {
        LONGINT active = p.active ? sub_folder(vref, sysdir, p.active, false) : sysdir;
        if(!active)
            continue;
        LONGINT disabled = sub_folder(vref, sysdir, p.disabled, false);
        for(const Item &it : folder_files(vref, active))
            if(!allow.count(to_string(it.name)) && runs_at_startup(vref, active, it))
            {
                if(!disabled)
                    disabled = sub_folder(vref, sysdir, p.disabled, true);
                if(disabled)
                    move(vref, active, it, disabled, "disabled");
            }
        if(disabled)
            for(const Item &it : folder_files(vref, disabled))
                if(allow.count(to_string(it.name)))
                    move(vref, disabled, it, active, "enabled");
    }
}

/* The components of the files of type 'thng' in Extensions, which the
   System registers itself after the extensions have run (7.5.5: 'gpch'
   667). Registering runs no code unless a component asks for its
   register message. */
static void register_component_files()
{
    GUEST<INTEGER> vref;
    GUEST<LONGINT> dirid;
    if(FindFolder(-32768 /* kOnSystemDisk */, "extn"_4, false, &vref, &dirid) != noErr)
        return;
    for(const Item &it : folder_files(vref, dirid))
    {
        if(it.type != "thng"_4)
            continue;
        Str63 name;
        memcpy(name, it.name, it.name[0] + 1);
        INTEGER rn = HOpenResFile(vref, dirid, name, fsRdPerm);
        if(rn == -1)
            continue;
        int32_t n = ROMlib_register_components(rn, true);
        fprintf(stderr, "[Executor] components: %s registered %d\n", to_string(it.name).c_str(), n);
        CloseResFile(rn);
    }
}

/* What InitAllPacks leaves: AppPacks holding the System file's PACK 0-7.
   The packages themselves run in C++ (the Pack traps never call these),
   but code checks the handles: Date & Time's clock draws nothing unless
   AppPacks[6] is a resource that starts with the 'PACK' header.  Every
   launch clears AppPacks (InitPerProcessLowMem), so it runs again then. */
void Executor::ROMlib_install_app_packs()
{
    if(!ROMlib_apple_system_file || !LM(SysMapHndl))
        return;
    /* The packages the Quadra ROM holds (4, 5, 7: SANE and Binary-Decimal)
       are not in the System file; a real boot's slots point into ROM. A
       stand-in with Apple's header (BRA.S; 'PACK'; id; version) whose code
       is the package's trap stands for those: Date & Time's panel checks
       AppPacks[7] before every NumToString and shows nothing without it. */
    static Handle standins[8];
    INTEGER saved = CurResFile();
    UseResFile(LM(SysMap));
    for(int i = 0; i < (int)std::size(LM(AppPacks)); i++)
    {
        if(LM(AppPacks)[i])
            continue;
        Handle h = Get1Resource("PACK"_4, i);
        if(!h && (i == 4 || i == 5 || i == 7))
        {
            if(!standins[i])
            {
                TheZoneGuard guard(LM(SysZone));
                standins[i] = NewHandleSys(16);
                if(standins[i])
                {
                    uint8_t *p = (uint8_t *)*standins[i];
                    const uint8_t header[12] = { 0x60, 0x0A, 0, 0, 'P', 'A', 'C', 'K',
                                                 0, (uint8_t)i, 0, 1 };
                    memcpy(p, header, 12);
                    uint16_t trap = 0xA9E7 + i; /* _Pack0 .. _Pack7 */
                    p[12] = trap >> 8;
                    p[13] = trap & 0xFF;
                    p[14] = 0x4E; /* RTS */
                    p[15] = 0x75;
                }
            }
            h = standins[i];
        }
        LM(AppPacks)[i] = h;
    }
    UseResFile(saved);
}

void Executor::ROMlib_load_extensions()
{
    ROMlib_install_app_packs();
    ROMlib_install_help_package(); /* before INITs that patch or ask for it */
    /* CurApName as a 7.5.5 boot has it before the Finder launches: $FF
       bytes. Apple's linked-patch loaders (the Speech Manager's INIT, PC
       Exchange, Macintosh Easy Open, the System's own 'lodr') read the
       long at $918, CurApName+8, as a hook to call unless negative; the
       host program's name there sent the Speech Manager into "enix". */
    memset(LM(CurApName), 0xFF, 32);
    LM(CurApName)[0] = 0;

    /* The System file's own components first, as its boot code does. */
    int32_t n = ROMlib_register_components(LM(SysMap), true);
    fprintf(stderr, "[Executor] components: System registered %d\n", n);

    std::set<std::string> allow = load_allow_list();
    sort_out_disabled(allow);
    if(allow.empty())
    {
        register_component_files();
        return;
    }

    struct Where
    {
        OSType folder;
        const char *label;
    };
    for(Where w : { Where { "extn"_4, "Extensions" }, Where { "ctrl"_4, "Control Panels" },
                    Where { "macs"_4, "System Folder" } })
    {
        GUEST<INTEGER> vref;
        GUEST<LONGINT> dirid;
        if(FindFolder(-32768 /* kOnSystemDisk */, w.folder, false, &vref, &dirid) != noErr)
            continue;
        for(const Item &it : folder_files(vref, dirid))
        {
            /* fext too: Finder Scripting Extension's INIT installs the
               Finder's Apple event handlers (seen on a real 7.5.5 boot);
               Finder loads the rest of a Finder extension itself. */
            if(it.type != "INIT"_4 && it.type != "cdev"_4 && it.type != "RDEV"_4
               && it.type != "fext"_4)
                continue;
            if(allow.count(to_string(it.name)))
                run_file(vref, dirid, it);
        }
    }
    register_component_files();
}
