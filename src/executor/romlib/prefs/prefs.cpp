
#include <prefs/prefs.h>
#include <prefs/options.h>
#include <prefs/parse.h>
#include <rsys/paths.h>
#include <rsys/appearance.h>
#include <sound/sounddriver.h>
#include <quickdraw/cquick.h>
#include <vdriver/vdriver.h>

#define CONFIGEXTENSION ".ecf"

static std::string ROMlib_configfilename;

void Executor::ParseConfigFile(std::string appname, OSType type)
{
    ROMlib_WindowName.clear();
    ROMlib_Comments.clear();
    ROMlib_desired_bpp = 0;

    ROMlib_configfilename = ROMlib_ConfigurationFolder + "/" + appname + CONFIGEXTENSION;
    configfile = fopen(ROMlib_configfilename.c_str(), "r");
    if(!configfile && type != 0)
    {
        char buf[16];
        sprintf(buf, "%08x", type);
        ROMlib_configfilename = ROMlib_ConfigurationFolder + "/" + buf + CONFIGEXTENSION;
        configfile = fopen(ROMlib_configfilename.c_str(), "r");
    }
    if(configfile)
    {
        yyparse();

#if 0	
	if (ROMlib_options & ROMLIB_NOCLOCK_BIT)
	    ROMlib_noclock = true;
#endif
        if(ROMlib_options & ROMLIB_REFRESH_BIT)
            ROMlib_refresh = 10;
        if(ROMlib_options & ROMLIB_SOUNDOFF_BIT)
            ROMlib_PretendSound = soundoff;
        if(ROMlib_options & ROMLIB_PRETENDSOUND_BIT)
            ROMlib_PretendSound = soundpretend;
        if(ROMlib_options & ROMLIB_SOUNDON_BIT)
            ROMlib_PretendSound = sound_driver->sound_works() ? soundon : soundpretend;
        if(ROMlib_options & ROMLIB_NEWLINETOCR_BIT)
            ROMlib_newlinetocr = true;
        else
            ROMlib_newlinetocr = false;
        if(ROMlib_options & ROMLIB_DIRECTDISKACCESS_BIT)
            ROMlib_directdiskaccess = true;
        else
            ROMlib_directdiskaccess = false;
        if(ROMlib_options & ROMLIB_NOWARN32_BIT)
            ROMlib_nowarn32 = true;
        else
            ROMlib_nowarn32 = false;
        if(ROMlib_options & ROMLIB_FLUSHOFTEN_BIT)
            ROMlib_flushoften = true;
        else
            ROMlib_flushoften = false;

        if(ROMlib_options & ROMLIB_PRETEND_HELP_BIT)
            ROMlib_pretend_help = true;
        else
            ROMlib_pretend_help = false;

        if(ROMlib_options & ROMLIB_PRETEND_ALIAS_BIT)
            ROMlib_pretend_alias = true;
        else
            ROMlib_pretend_alias = false;

        if(ROMlib_options & ROMLIB_PRETEND_EDITION_BIT)
            ROMlib_pretend_edition = true;
        else
            ROMlib_pretend_edition = false;

        if(ROMlib_options & ROMLIB_PRETEND_SCRIPT_BIT)
            ROMlib_pretend_script = true;
        else
            ROMlib_pretend_script = false;

        if(ROMlib_desired_bpp)
            SetDepth(LM(MainDevice), ROMlib_desired_bpp, 0, 0);
        fclose(configfile);
    }

    if(!ROMlib_WindowName.empty())
        vdriver->setTitle(ROMlib_WindowName);
    else
        vdriver->setTitle(appname);
}



/*
 * Get rid of any characters that cause trouble inside a quoted
 * string.  Look at yylex() in parse.y.
 */

static void
clean(std::string& str)
{
    for(char& c : str)
        if(c == '"')
            c = '\'';
}

bool Executor::SaveConfigFile()
{
    const char *savefilename = ROMlib_configfilename.c_str();
    FILE *fp = fopen(savefilename, "w");

    if (!fp)
        return false;

    {
        const char *lastslash;

        lastslash = strrchr(savefilename, '/');
        lastslash = lastslash ? lastslash + 1 : savefilename;
        fprintf(fp, "// This Configuration file (%s) was built by "
                    "Executor\n",
                lastslash);
    }
    if(!ROMlib_Comments.empty())
    {
        clean(ROMlib_Comments);
        fprintf(fp, "Comments = \"%s\";\n", ROMlib_Comments.c_str());
    }
    if(!ROMlib_WindowName.empty())
    {
        clean(ROMlib_WindowName);
        fprintf(fp, "WindowName = \"%s\";\n", ROMlib_WindowName.c_str());
    }
    fprintf(fp, "BitsPerPixel = %d;\n",
            toHost(PIXMAP_PIXEL_SIZE(GD_PMAP(LM(MainDevice)))));

    fprintf(fp, "SystemVersion = %s;\n", C_STRING_FROM_SYSTEM_VERSION());
    fprintf(fp, "RefreshNumber = %d;\n", ROMlib_refresh);
    fprintf(fp, "Delay = %d;\n", ROMlib_delay);
    fprintf(fp, "Options = {");
    switch(ROMlib_PretendSound)
    {
        case soundoff:
            fprintf(fp, "SoundOff");
            break;
        case soundpretend:
            fprintf(fp, "PretendSound");
            break;
        case soundon:
            fprintf(fp, "SoundOn");
            break;
    }
    if(ROMlib_newlinetocr)
        fprintf(fp, ", NewLineToCR");
    if(ROMlib_directdiskaccess)
        fprintf(fp, ", DirectDiskAccess");

    if(ROMlib_nowarn32)
        fprintf(fp, ", NoWarn32");
    if(ROMlib_flushoften)
        fprintf(fp, ", FlushOften");

    if(ROMlib_pretend_help)
        fprintf(fp, ", PretendHelp");
    if(ROMlib_pretend_edition)
        fprintf(fp, ", PretendEdition");
    if(ROMlib_pretend_script)
        fprintf(fp, ", PretendScript");
    if(ROMlib_pretend_alias)
        fprintf(fp, ", PretendAlias");

    fprintf(fp, "};\n");
    fclose(fp);
    
    
    return true;
}