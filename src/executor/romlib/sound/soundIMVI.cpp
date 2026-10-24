/* Copyright 1990, 1992, 1995, 1996 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in SoundMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <MemoryMgr.h>
#include <ResourceMgr.h>
#include <SoundDvr.h>
#include <SoundMgr.h>
#include <TimeMgr.h>
#include <FileMgr.h>
#include <sound/sounddriver.h>
#include <prefs/prefs.h>
#include <rsys/component.h>
#include <base/traps.h>
#include <cmath>
#include <sound/soundopts.h>
#include <base/functions.impl.h>
#include <base/traps.impl.h>

using namespace Executor;

static OSErr
start_playing(SndChannelPtr chanp, SndDoubleBufferHeaderPtr paramp,
              int which_buf);

void Executor::C_SndGetSysBeepState(GUEST<INTEGER> *statep)
{
    /* #warning SndGetSysBeepState not implemented */
    warning_sound_log("");
}

OSErr Executor::C_SndSetSysBeepState(INTEGER state)
{
    /* #warning SndSetSysBeepState not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SndManagerStatus(INTEGER length, SMStatusPtr statusp)
{
    /* #warning SndManagerStatus not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

NumVersion Executor::C_SndSoundManagerVersion()
{
    NumVersion ret;

    warning_sound_log("");
    switch(ROMlib_PretendSound)
    {
        case soundoff:
            ret = 0;
            break;
        case soundpretend:
        case soundon:
            ret = 0x03030303; /* FIXME; need to get this right */
            break;
        default:
            gui_abort();
            ret = 0;
    }

    return ret;
}

NumVersion Executor::C_MACEVersion()
{
    /* #warning MACEVersion not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? 0x2000000 : 0;
}

/* MacPhoenix: the Sound Input Manager is there (Gestalt 'snd ' bit 3: Apple's
   SimpleSound wants it) but the machine has no sound input device (bit 5
   clear), so the calls that would find or open one answer as a Mac without
   input hardware. */
enum
{
    siNoSoundInHardware = -220,
    siBadSoundInDevice = -221,
};

NumVersion Executor::C_SPBVersion()
{
    warning_sound_log("");
    return ROMlib_PretendSound == soundoff ? 0 : 0x1000000;
}

OSErr Executor::C_SndStartFilePlay(SndChannelPtr chanp, INTEGER refnum,
                                   INTEGER resnum, LONGINT buffersize,
                                   Ptr bufferp,
                                   AudioSelectionPtr theselectionp,
                                   ProcPtr completionp, Boolean async)
{
    /* #warning SndStartFilePlay not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SndPauseFilePlay(SndChannelPtr chanp)
{
    /* #warning SndPauseFilePlay not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SndStopFilePlay(SndChannelPtr chanp, Boolean async)
{
    /* #warning SndStopFilePlay not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

static struct
{
    SndDoubleBufferHeaderPtr headp;
    SndChannelPtr chanp;
    int current_buffer;
    TMTask task;
    Boolean busy;
} call_back_info;

/*
 * clear_pending_sounds may be called before we ever initialize sound, so
 * we have to check to make sure the sound_clear_pending function has
 * been initialized.
 */

void Executor::clear_pending_sounds(void)
{
    call_back_info.headp = nullptr;
    call_back_info.busy = false;
    sound_driver->sound_clear_pending();
    allchans = nullptr;
}


static void C_sound_timer_handler()
{
    SndDoubleBufferPtr dbp;
    SndDoubleBackUPP pp;
    int current_buffer;

    if(call_back_info.headp)
    {
        current_buffer = call_back_info.current_buffer;
        pp = call_back_info.headp->dbhDoubleBack;
        dbp = call_back_info.headp->dbhBufferPtr[current_buffer];
        call_back_info.busy = false;
        start_playing(call_back_info.chanp, call_back_info.headp,
                      current_buffer ^ 1);
        dbp->dbFlags &= ~dbBufferReady;
        pp(call_back_info.chanp, dbp);
    }
}
PASCAL_FUNCTION_PTR(sound_timer_handler);

/*
 * NOTE: we're not really playing anything here, although we could.
 */

static OSErr
start_playing(SndChannelPtr chanp, SndDoubleBufferHeaderPtr paramp,
              int which_buf)
{
    SndDoubleBackUPP pp;
    static bool task_inserted = false;

    pp = paramp->dbhDoubleBack;
    if(pp)
    {
        SndDoubleBufferPtr dbp;

        dbp = paramp->dbhBufferPtr[which_buf];
        if(!(dbp->dbFlags & dbLastBuffer))
        {
            LONGINT duration_in_mills;

            if(call_back_info.busy)
                warning_unexpected("busy");
            call_back_info.headp = paramp;
            call_back_info.chanp = chanp;
            call_back_info.current_buffer = which_buf;
            call_back_info.busy = true;
            if(!task_inserted)
            {
                call_back_info.task.tmAddr
                    = (ProcPtr)&sound_timer_handler;
                InsTime((QElemPtr)&call_back_info.task);
                task_inserted = true;
            }
            duration_in_mills = (((long long)1000 * (1 << 16)
                                  * dbp->dbNumFrames)
                                 / paramp->dbhSampleRate);
            PrimeTime((QElemPtr)&call_back_info.task, duration_in_mills);
        }
        else
        {
            RmvTime((QElemPtr)&call_back_info.task);
            task_inserted = false;
        }
    }
    return noErr;
}


OSErr Executor::C_SndPlayDoubleBuffer(SndChannelPtr chanp,
                                      SndDoubleBufferHeaderPtr paramp)
{
    OSErr retval;
    /* #warning SndPlayDoubleBuffer not implemented */
    switch(ROMlib_PretendSound)
    {
        case soundoff:
            retval = notEnoughHardware;
            break;

        case soundpretend:
            retval = start_playing(chanp, paramp, 0); /* always start with buffer 0 */
            break;

        case soundon:
            if(!paramp)
                warning_sound_log("paramp = nullptr");
            else
                warning_sound_log("nc %d sz %d c %d p %d",
                                  paramp->dbhNumChannels,
                                  paramp->dbhSampleSize,
                                  paramp->dbhCompressionID,
                                  paramp->dbhPacketSize);
            SND_CHAN_DBHP(chanp) = paramp;
            SND_CHAN_CURRENT_DB(chanp) = 0;
            /*
      SND_CHAN_CURRENT_START (chanp) = SND_PROMOTE (SND_CHAN_TIME (chanp));
      */
            SND_CHAN_TIME(chanp) = 0;
            SND_CHAN_CURRENT_START(chanp) = 0;
            chanp->flags |= CHAN_DBINPROG_FLAG;
            sound_driver->sound_go();
            retval = noErr;
            break;

        default:
            gui_abort();
            retval = noErr; /* quiet gcc if necessary */
            break;
    }
    return retval;
}

void Executor::C_Comp3to1(Ptr inp, Ptr outp, LONGINT cnt, Ptr instatep,
                          Ptr outstatep, LONGINT numchannels,
                          LONGINT whichchannel)
{
    /* #warning Comp3to1 not implemented */
    warning_sound_log("");
}

void Executor::C_Comp6to1(Ptr inp, Ptr outp, LONGINT cnt, Ptr instatep,
                          Ptr outstatep, LONGINT numchannels,
                          LONGINT whichchannel)
{
    /* #warning Comp6to1 not implemented */
    warning_sound_log("");
}

void Executor::C_Exp1to3(Ptr inp, Ptr outp, LONGINT cnt, Ptr instatep,
                         Ptr outstatep, LONGINT numchannels,
                         LONGINT whichchannel)
{
    /* #warning Exp1to3 not implemented */
    warning_sound_log("");
}

void Executor::C_Exp1to6(Ptr inp, Ptr outp, LONGINT cnt, Ptr instatep,
                         Ptr outstatep, LONGINT numchannels,
                         LONGINT whichchannel)
{
    /* #warning Exp1to6 not implemented */
    warning_sound_log("");
}

OSErr Executor::C_SndRecord(ProcPtr filterp, Point corner, OSType quality,
                            GUEST<Handle> *sndhandlep)
{
    warning_sound_log("");
    return siNoSoundInHardware;
}

OSErr Executor::C_SndRecordToFile(ProcPtr filterp, Point corner,
                                  OSType quality, INTEGER refnum)
{
    warning_sound_log("");
    return siNoSoundInHardware;
}

OSErr Executor::C_SPBOpenDevice(ConstStringPtr name, INTEGER permission,
                                GUEST<LONGINT> *inrefnump)
{
    warning_sound_log("");
    return siBadSoundInDevice;
}

OSErr Executor::C_SPBCloseDevice(LONGINT inrefnum)
{
    /* #warning SPBCloseDevice not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBRecord(SPBPtr inparamp, Boolean async)
{
    /* #warning SPBRecord not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBRecordToFile(INTEGER refnum, SPBPtr inparamp,
                                  Boolean async)
{
    /* #warning SPBRecordToFile not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBPauseRecording(LONGINT refnum)
{
    /* #warning SPBPauseRecording not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBResumeRecording(LONGINT refnum)
{
    /* #warning SPBResumeRecording not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBStopRecording(LONGINT refnum)
{
    /* #warning PPBStopRecording not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBGetRecordingStatus(LONGINT refnum,
                                        GUEST<INTEGER> *recordingstatus,
                                        GUEST<INTEGER> *meterlevel,
                                        GUEST<LONGINT> *totalsampstorecord,
                                        GUEST<LONGINT> *numsampsrecorded,
                                        GUEST<LONGINT> *totalmsecstorecord,
                                        GUEST<LONGINT> *numbermsecsrecorded)
{
    /* #warning SPBGetRecordingStatus not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBGetDeviceInfo(LONGINT refnum, OSType info, Ptr infop)
{
    /* #warning SPBGetDeviceInfo not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBSetDeviceInfo(LONGINT refnum, OSType info, Ptr infop)
{
    /* #warning SPBSetDeviceInfo not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

/* MacPhoenix: SetupSndHeader (Inside Macintosh: Sound 2-118) writes a
   format 1 'snd ' header, one bufferCmd to the sound header after it, into
   sndhandle and says how long it is; the caller puts its samples there.
   It was a stub that left *headerlenp alone, and Warcraft II wrote 8K of
   samples at whatever its stack held (1840) into an 8448-byte handle. A
   handle too small for the header gets only the length. */
namespace
{
struct BigEndianWriter
{
    uint8_t *p;
    void u8(uint8_t v) { *p++ = v; }
    void u16(uint16_t v) { u8(v >> 8); u8(v); }
    void u32(uint32_t v) { u16(v >> 16); u16(v); }
    void x80(double v)          /* IEEE 754 80-bit extended */
    {
        if(v <= 0)
        {
            for(int i = 0; i < 10; i++)
                u8(0);
            return;
        }
        int e;
        double m = std::frexp(v, &e);                /* v = m * 2^e, 0.5 <= m < 1 */
        uint64_t mant = (uint64_t)std::ldexp(m, 64);
        u16(e - 1 + 16383);
        u32(mant >> 32);
        u32((uint32_t)mant);
    }
};
}

OSErr Executor::C_SetupSndHeader(Handle sndhandle, INTEGER numchannels,
                                 Fixed rate, INTEGER size, OSType compresion,
                                 INTEGER basefreq, LONGINT numbytes,
                                 GUEST<INTEGER> *headerlenp)
{
    bool compressed = compresion != "NONE"_4 && compresion != 0;
    bool standard = !compressed && numchannels == 1 && size == 8;
    int header_len = 20 + (standard ? 22 : 64);

    *headerlenp = header_len;
    if(!sndhandle || GetHandleSize(sndhandle) < header_len)
        return noErr;

    BigEndianWriter w { (uint8_t *)*sndhandle };
    w.u16(1);                                   /* format 1 */
    w.u16(1);                                   /* one synthesizer: */
    w.u16(sampledSynth);
    w.u32(numchannels == 2 ? 0xC0 : 0x80);      /* initStereo / initMono */
    w.u16(1);                                   /* one command: */
    w.u16(0x8000 | bufferCmd);                  /* dataOffsetFlag | bufferCmd */
    w.u16(0);
    w.u32(20);                                  /* the sound header, next */

    double hz = (uint32_t)rate / 65536.0;
    int channels = std::max<int>(numchannels, 1);
    w.u32(0);                                   /* samplePtr: the samples follow */
    if(standard)
    {
        w.u32(numbytes);
        w.u32(rate);
        w.u32(0);                               /* loopStart */
        w.u32(0);                               /* loopEnd */
        w.u8(stdSH);
        w.u8(basefreq);
        return noErr;
    }

    uint32_t frames;
    INTEGER compression_id = 0;                 /* notCompressed */
    if(compresion == "MAC3"_4)
        frames = numbytes * 3 / channels, compression_id = 3;
    else if(compresion == "MAC6"_4)
        frames = numbytes * 6 / channels, compression_id = 4;
    else if(compresion == "ima4"_4)
        frames = numbytes / 34 * 64 / channels, compression_id = -1;
    else
        frames = numbytes / channels / std::max(size / 8, 1), compression_id = compressed ? -1 : 0;

    w.u32(numchannels);
    w.u32(rate);
    w.u32(0);                                   /* loopStart */
    w.u32(0);                                   /* loopEnd */
    w.u8(compressed ? cmpSH : extSH);
    w.u8(basefreq);
    w.u32(frames);
    w.x80(hz);                                  /* AIFFSampleRate */
    w.u32(0);                                   /* markerChunk */
    if(compressed)
    {
        w.u32(compresion);                      /* format */
        w.u32(0);                               /* futureUse2 */
        w.u32(0);                               /* stateVars */
        w.u32(0);                               /* leftOverSamples */
        w.u16(compression_id);
        w.u16(0);                               /* packetSize */
        w.u16(0);                               /* snthID */
        w.u16(size);
    }
    else
    {
        w.u32(0);                               /* instrumentChunks */
        w.u32(0);                               /* AESRecording */
        w.u16(size);
        w.u16(0);                               /* futureUse1 */
        w.u32(0);
        w.u32(0);
        w.u32(0);                               /* futureUse2..4 */
    }
    return noErr;
}

OSErr Executor::C_SetupAIFFHeader(INTEGER refnum, INTEGER numchannels,
                                  Fixed samplerate, INTEGER samplesize,
                                  OSType compression, LONGINT numbytes,
                                  LONGINT numframes)
{
    /* #warning SetupAIFFHeader not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBSignInDevice(INTEGER refnum, ConstStringPtr name)
{
    /* #warning SPBSignInDevice not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBSignOutDevice(INTEGER refnum)
{
    /* #warning SPBSignOutDevice not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBGetIndexedDevice(INTEGER count, ConstStringPtr name,
                                      GUEST<Handle> *deviceiconhandlep)
{
    warning_sound_log("");
    return siBadSoundInDevice;
}

OSErr Executor::C_SPBMillisecondsToBytes(LONGINT refnum, GUEST<LONGINT> *millip)
{
    /* #warning SPBMillisecondsToBytes not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

OSErr Executor::C_SPBBytesToMilliseconds(
    LONGINT refnum, GUEST<LONGINT> *bytecountp)
{
    /* #warning SPBBytesToMilliseconds not implemented */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

void Executor::C_FinaleUnknown1()
{
    /* Finale calls this */
    warning_sound_log("");
}

OSErr Executor::C_FinaleUnknown2(ResType arg1, LONGINT arg2, Ptr arg3, Ptr arg4)
{
    /* Finale calls this */
    warning_sound_log("");
    return ROMlib_PretendSound == soundpretend ? noErr : notEnoughHardware;
}

/* various self-running demos (made by Macromedia Director, I think)
   seem to call this */

LONGINT Executor::C_DirectorUnknown3()
{
    warning_sound_log("");
    return 0;
}

INTEGER Executor::C_DirectorUnknown4(ResType arg1, INTEGER arg2, Ptr arg3,
                                     Ptr arg4)
{
    warning_sound_log("");
    return paramErr;
}

/* Sound Manager 3.0 */

/* MacPhoenix: volumes, right << 16 | left, each 0 to $100. The output's is
   applied to what is sent out (phoenix_sound.cpp); the alert volume is
   kept for those that ask (the Sound control panel's alert slider). */
uint32_t Executor::ROMlib_sound_output_volume = 0x01000100;
bool Executor::ROMlib_sound_output_muted = false;
static uint32_t alert_volume = 0x01000100;

OSErr Executor::C_GetSysBeepVolume(GUEST<LONGINT> *levelp)
{
    *levelp = alert_volume;
    return noErr;
}

OSErr Executor::C_SetSysBeepVolume(LONGINT level)
{
    alert_volume = level;
    return noErr;
}

OSErr Executor::C_GetDefaultOutputVolume(GUEST<LONGINT> *levelp)
{
    *levelp = ROMlib_sound_output_volume;
    return noErr;
}

OSErr Executor::C_SetDefaultOutputVolume(LONGINT level)
{
    ROMlib_sound_output_volume = level;
    return noErr;
}

/* MacPhoenix: the output device, as a sound output component ('sdev') of
   Executor's own: what the Sound control panel's Volumes and Sound Out
   pages ask the device (SoundComponentGetInfo/SetInfo, Sound.h). Executor
   mixes 8-bit mono at 22254.5 Hz; what it sends out is 48 kHz 16-bit
   stereo, the format the device reports. */
namespace
{
enum
{
    kComponentOpenSelect = -1, kComponentCloseSelect = -2, kComponentCanDoSelect = -3,
    kComponentVersionSelect = -4, kComponentRegisterSelect = -5,
    kSoundComponentGetInfoSelect = 0x0103, kSoundComponentSetInfoSelect = 0x0104,
    badComponentSelector = (int32_t)0x80008002, siUnknownInfoType = -231,
};

int32_t sound_output_component(uint32_t params, uint32_t storage)
{
    int16_t what = (int16_t)READUW(params + 2);
    switch(what)
    {
        case kComponentOpenSelect:
        case kComponentCloseSelect:
        case kComponentRegisterSelect:
            return noErr;
        case kComponentVersionSelect:
            return 0x00010000;
        case kComponentCanDoSelect:
        {
            int16_t which = (int16_t)READUW(params + 4);
            return which == kComponentOpenSelect || which == kComponentCloseSelect
                || which == kComponentCanDoSelect || which == kComponentVersionSelect
                || which == kSoundComponentGetInfoSelect || which == kSoundComponentSetInfoSelect;
        }
        case kSoundComponentGetInfoSelect:
        case kSoundComponentSetInfoSelect:
        {
            /* (ci, sourceID, OSType selector, infoPtr): the last argument
               first in the block */
            uint32_t info = READUL(params + 4);
            uint32_t selector = READUL(params + 8);
            bool set = what == kSoundComponentSetInfoSelect;
            switch(selector)
            {
                case "hvol"_4: /* siHardwareVolume */
                case "svol"_4: /* siSpeakerVolume */
                    if(set)
                        ROMlib_sound_output_volume = info; /* the value itself */
                    else
                        WRITEUL(info, ROMlib_sound_output_volume);
                    return noErr;
                case "hmut"_4: /* siHardwareMute */
                case "smut"_4: /* siSpeakerMute */
                    if(set)
                        ROMlib_sound_output_muted = info != 0;
                    else
                        WRITEUW(info, ROMlib_sound_output_muted);
                    return noErr;
                case "hstp"_4: /* siHardwareVolumeSteps */
                    if(set)
                        break;
                    WRITEUW(info, 8);
                    return noErr;
                /* the format sent out: 48 kHz 16-bit stereo, the only one */
                case "ssiz"_4: /* siSampleSize */
                case "chan"_4: /* siNumberChannels */
                {
                    uint16_t v = selector == "ssiz"_4 ? 16 : 2;
                    if(set)
                        return (info & 0xFFFF) == v ? noErr : siUnknownInfoType;
                    WRITEUW(info, v);
                    return noErr;
                }
                case "srat"_4: /* siSampleRate, UnsignedFixed */
                    if(set)
                        return info == 0xBB800000 ? noErr : siUnknownInfoType;
                    WRITEUL(info, 0xBB800000);
                    return noErr;
                /* the choices, SoundInfoList {count, handle of values}, the
                   caller's to dispose: one of each */
                case "ssav"_4: /* siSampleSizeAvailable */
                case "chav"_4: /* siChannelAvailable */
                case "srav"_4: /* siSampleRateAvailable */
                {
                    if(set)
                        break;
                    bool rate = selector == "srav"_4;
                    Handle h = NewHandle(rate ? 4 : 2);
                    if(!h)
                        return MemError();
                    if(rate)
                        *(GUEST<uint32_t> *)*h = 0xBB800000;
                    else
                        *(GUEST<uint16_t> *)*h = selector == "ssav"_4 ? 16 : 2;
                    WRITEUW(info, 1);
                    WRITEUL(info + 2, US_TO_SYN68K(h));
                    return noErr;
                }
            }
            char sel[5] = { (char)(selector >> 24), (char)(selector >> 16), (char)(selector >> 8),
                            (char)selector, 0 };
            traps::log_unimplemented(std::string("sound output component ") + (set ? "SetInfo" : "GetInfo")
                                     + " '" + sel + "': answered siUnknownInfoType");
            return siUnknownInfoType;
        }
    }
    traps::log_unimplemented("sound output component selector " + std::to_string(what)
                             + ": answered badComponentSelector");
    return badComponentSelector;
}
}

static uint32_t sound_output_component_id;

void Executor::ROMlib_install_sound_output_component()
{
    sound_output_component_id = ROMlib_register_native_component(
        "sdev"_4, "xctr"_4, "appl"_4, "Built-in", sound_output_component);
}

OSErr Executor::C_GetSoundOutputDevice(GUEST<uint32_t> *outputDevice)
{
    if(!sound_output_component_id)
        return paramErr;
    *outputDevice = sound_output_component_id;
    return noErr;
}

OSErr Executor::C_GetSoundHeaderOffset(Handle sndHandle, GUEST<LONGINT> *offset)
{
    OSErr retval;
    int num_commands;
    SndCommand *cmds;
    int i;

    warning_sound_log("");

    num_commands = ROMlib_get_snd_cmds(sndHandle, &cmds);

    retval = badFormat;
    for(i = 0; i < num_commands; ++i)
    {
        if(cmds[i].cmd == (bufferCmd | 0x8000) || cmds[i].cmd == (soundCmd | 0x8000))
        {
            *offset = cmds[i].param2;
            retval = noErr;
            break;
        }
    }

    return retval;
}

UnsignedFixed Executor::C_UnsignedFixedMulDiv(
    UnsignedFixed value, UnsignedFixed multiplier, UnsignedFixed divisor)
{
    UnsignedFixed retval;

    warning_sound_log("");
    warning_unimplemented("");
    retval = 0;
    return retval;
}

OSErr Executor::C_GetCompressionInfo(INTEGER compressionID, OSType format,
                                     INTEGER numChannels, INTEGER sampleSize,
                                     CompressionInfoPtr cp)
{
    OSErr retval;

    warning_sound_log("");
    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::C_SetSoundPreference(OSType theType, ConstStringPtr name,
                                     Handle settings)
{
    OSErr retval;

    warning_sound_log("");
    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::C_GetSoundPreference(OSType theType, ConstStringPtr name,
                                     Handle settings)
{
    OSErr retval;

    warning_sound_log("");
    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

/* Sound Manager 3.1 */

OSErr Executor::C_SndGetInfo(SndChannelPtr chan, OSType selector, void *infoPtr)
{
    OSErr retval;

    warning_sound_log("");
    warning_unimplemented("");
    retval = paramErr;
    return retval;
}

OSErr Executor::C_SndSetInfo(SndChannelPtr chan, OSType selector, void *infoPtr)
{
    OSErr retval;

    warning_sound_log("");
    warning_unimplemented("");
    retval = paramErr;
    return retval;
}
