/*****************************************************************************
 *                                                                           *
 *                                 SoundMgr.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    INTEGER, LONGINT, Ptr, Fixed, Byte, Boolean, OSErr,     *
 *                   OSType, Handle, ConstStringPtr, ResType                 *
 *     SANE.h        extended80                                              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "SANE.h"

#define MODULE_NAME SoundMgr
#include <base/api-module.h>


namespace Executor {

struct SndCommand{GUEST_STRUCT;GUEST<INTEGER> cmd;GUEST<INTEGER> param1;GUEST<LONGINT> param2;};static_assert(sizeof(SndCommand) == 8);void describeStruct(const SndCommand&, std::ostream&);

enum {stdQLength = 128,
};

enum {stdSH = 0, // standard sound header
cmpSH = 254, // compressed sound header
extSH = 255, // extended sound header
};

struct SndChannel;

using SndCallbackUPP = UPP<void(SndChannel *, SndCommand *)>;


struct SndChannel{GUEST_STRUCT;GUEST<SndChannel*> nextChan;GUEST<Ptr> firstMod;GUEST<SndCallbackUPP> callBack;GUEST<LONGINT> userInfo;GUEST<LONGINT> wait;SndCommand cmdInProg;GUEST<INTEGER> flags;GUEST<INTEGER> qLength;GUEST<INTEGER> qHead;GUEST<INTEGER> qTail;SndCommand queue[stdQLength];};static_assert(sizeof(SndChannel) == 1060);void describeStruct(const SndChannel&, std::ostream&);

typedef SndChannel *SndChannelPtr;

enum {nullCmd,
initCmd,
freeCmd,
quietCmd,
flushCmd,
waitCmd = 10,
pauseCmd,
resumeCmd,
callBackCmd,
syncCmd,
emptyCmd,
tickleCmd = 20,
requestNextCmd,
howOftenCmd,
wakeUpCmd,
availableCmd,
noteCmd = 40,
restCmd,
freqCmd,
ampCmd,
timbreCmd,
waveTableCmd = 60,
phaseCmd,
soundCmd = 80,
bufferCmd,
rateCmd,
midiDataCmd = 100,
};

struct soundbuffer_t{GUEST_STRUCT;GUEST<LONGINT> offset;GUEST<LONGINT> nsamples;GUEST<LONGINT> rate;GUEST<LONGINT> altbegin;GUEST<LONGINT> altend;GUEST<INTEGER> basenote;uint8_t buf[1];};static_assert(sizeof(soundbuffer_t) == 24);void describeStruct(const soundbuffer_t&, std::ostream&);

struct _SoundHeader{GUEST_STRUCT;GUEST<Ptr> samplePtr;GUEST<LONGINT> length;GUEST<Fixed> sampleRate;GUEST<LONGINT> loopStart;GUEST<LONGINT> loopEnd;Byte encode;Byte baseFrequency;Byte sampleArea[1];};void describeStruct(const _SoundHeader&, std::ostream&);

typedef _SoundHeader SoundHeader;

typedef _SoundHeader *SoundHeaderPtr;

struct _ExtSoundHeader{GUEST_STRUCT;GUEST<Ptr> samplePtr;GUEST<LONGINT> numChannels;GUEST<Fixed> sampleRate;GUEST<LONGINT> loopStart;GUEST<LONGINT> loopEnd;Byte encode;Byte baseFrequency;GUEST<LONGINT> numFrames;GUEST<extended80> AIFFSampleRate; // ???  should be Extended80
GUEST<Ptr> MarkerChunk;GUEST<Ptr> instrumentChunks;GUEST<Ptr> AESRecording;GUEST<INTEGER> sampleSize;GUEST<INTEGER> futureUse1;GUEST<LONGINT> futureUse2;GUEST<LONGINT> futureUse3;GUEST<LONGINT> futureUse4;Byte sampleArea[1];};void describeStruct(const _ExtSoundHeader&, std::ostream&);

typedef _ExtSoundHeader ExtSoundHeader;

typedef _ExtSoundHeader *ExtSoundHeaderPtr;

enum {noteSynth = 1,
waveTableSynth = 3,
sampledSynth = 5,
MIDISynthIn = 7,
MIDISynthOut = 9,
};

enum {badChannel = -205,
badFormat = -206,
noHardware = -200,
notEnoughHardware = -201,
queueFull = -203,
resProblem = -204,
};

// SoundDvr MPW (true);
const LowMemGlobal<Byte > SoundActive{ 0x27E };
inline Byte  LMGetSoundActive() { return LM(SoundActive); }
inline void LMSetSoundActive(Byte val) { LM(SoundActive) = val; }
NOTRAP_FUNCTION2(LMGetSoundActive);
NOTRAP_FUNCTION2(LMSetSoundActive);


enum {soundactiveoff = 0,
soundactive5 = 5,
soundactiveinplay = 129,
soundactivenone = 255,
};

struct SndDoubleBuffer{GUEST_STRUCT;GUEST<LONGINT> dbNumFrames;GUEST<LONGINT> dbFlags;GUEST<LONGINT> dbUserInfo[2];Byte dbSoundData[1];};static_assert(sizeof(SndDoubleBuffer) == 18);void describeStruct(const SndDoubleBuffer&, std::ostream&);

typedef SndDoubleBuffer *SndDoubleBufferPtr;

enum {dbBufferReady = 1,
dbLastBuffer = 4,
};

using SndDoubleBackUPP = UPP<void(SndChannelPtr , SndDoubleBufferPtr )>;


struct SndDoubleBufferHeader{GUEST_STRUCT;GUEST<INTEGER> dbhNumChannels;GUEST<INTEGER> dbhSampleSize;GUEST<INTEGER> dbhCompressionID;GUEST<INTEGER> dbhPacketSize;GUEST<Fixed> dbhSampleRate;GUEST<SndDoubleBufferPtr> dbhBufferPtr[2];GUEST<SndDoubleBackUPP> dbhDoubleBack;};static_assert(sizeof(SndDoubleBufferHeader) == 24);void describeStruct(const SndDoubleBufferHeader&, std::ostream&);

typedef SndDoubleBufferHeader *SndDoubleBufferHeaderPtr;

struct _SCSTATUS{GUEST_STRUCT;GUEST<Fixed> scStartTime;GUEST<Fixed> scEndTime;GUEST<Fixed> scCurrentTime;Boolean scChannelBusy;Boolean scChannelDisposed;Boolean scChannelPaused;Boolean scUnused;GUEST<LONGINT> scChannelAttributes;GUEST<LONGINT> scCPULoad;};void describeStruct(const _SCSTATUS&, std::ostream&);

typedef _SCSTATUS SCStatus;

typedef _SCSTATUS *SCStatusPtr;

typedef void *SMStatusPtr;

typedef LONGINT NumVersion;

typedef void *AudioSelectionPtr;

typedef void *SPBPtr;

typedef uint32_t UnsignedFixed;

typedef Ptr CompressionInfoPtr;

DISPATCHER_TRAP(SoundDispatch, 0xA800, D0<0xFFFFFF>);


void C_SndGetSysBeepState(GUEST<INTEGER> *statep);PASCAL_SUBTRAP(SndGetSysBeepState, 0xA800, 0x2180008, SoundDispatch);


OSErr C_SndSetSysBeepState(INTEGER state);PASCAL_SUBTRAP(SndSetSysBeepState, 0xA800, 0x11C0008, SoundDispatch);


OSErr C_SndChannelStatus(SndChannelPtr chanp, INTEGER length, SCStatusPtr statusp);PASCAL_SUBTRAP(SndChannelStatus, 0xA800, 0x5100008, SoundDispatch);


OSErr C_SndManagerStatus(INTEGER length, SMStatusPtr statusp);PASCAL_SUBTRAP(SndManagerStatus, 0xA800, 0x3140008, SoundDispatch);


NumVersion C_SndSoundManagerVersion();PASCAL_SUBTRAP(SndSoundManagerVersion, 0xA800, 0xC0008, SoundDispatch);


NumVersion C_MACEVersion();PASCAL_SUBTRAP(MACEVersion, 0xA800, 0x0010, SoundDispatch);


NumVersion C_SPBVersion();PASCAL_SUBTRAP(SPBVersion, 0xA800, 0x0014, SoundDispatch);


OSErr C_SndStartFilePlay(SndChannelPtr chanp, INTEGER refnum, INTEGER resnum, LONGINT buffersize, Ptr bufferp, AudioSelectionPtr theselectionp, ProcPtr completionp, Boolean async);PASCAL_SUBTRAP(SndStartFilePlay, 0xA800, 0xD000008, SoundDispatch);


OSErr C_SndPauseFilePlay(SndChannelPtr chanp);PASCAL_SUBTRAP(SndPauseFilePlay, 0xA800, 0x2040008, SoundDispatch);


OSErr C_SndStopFilePlay(SndChannelPtr chanp, Boolean async);PASCAL_SUBTRAP(SndStopFilePlay, 0xA800, 0x3080008, SoundDispatch);


OSErr C_SndPlayDoubleBuffer(SndChannelPtr chanp, SndDoubleBufferHeaderPtr paramp);PASCAL_SUBTRAP(SndPlayDoubleBuffer, 0xA800, 0x4200008, SoundDispatch);


void C_Comp3to1(Ptr inp, Ptr outp, LONGINT cnt, Ptr instatep, Ptr outstatep, LONGINT numchannels, LONGINT whichchannel);PASCAL_SUBTRAP(Comp3to1, 0xA800, 0x40010, SoundDispatch);


void C_Comp6to1(Ptr inp, Ptr outp, LONGINT cnt, Ptr instatep, Ptr outstatep, LONGINT numchannels, LONGINT whichchannel);PASCAL_SUBTRAP(Comp6to1, 0xA800, 0xC0010, SoundDispatch);


void C_Exp1to3(Ptr inp, Ptr outp, LONGINT cnt, Ptr instatep, Ptr outstatep, LONGINT numchannels, LONGINT whichchannel);PASCAL_SUBTRAP(Exp1to3, 0xA800, 0x80010, SoundDispatch);


void C_Exp1to6(Ptr inp, Ptr outp, LONGINT cnt, Ptr instatep, Ptr outstatep, LONGINT numchannels, LONGINT whichchannel);PASCAL_SUBTRAP(Exp1to6, 0xA800, 0x100010, SoundDispatch);


OSErr C_SndRecord(ProcPtr filterp, Point corner, OSType quality, GUEST<Handle> *sndhandlep);PASCAL_SUBTRAP(SndRecord, 0xA800, 0x8040014, SoundDispatch);


OSErr C_SndRecordToFile(ProcPtr filterp, Point corner, OSType quality, INTEGER refnum);PASCAL_SUBTRAP(SndRecordToFile, 0xA800, 0x7080014, SoundDispatch);


OSErr C_SPBOpenDevice(ConstStringPtr name, INTEGER permission, GUEST<LONGINT> *inrefnump);PASCAL_SUBTRAP(SPBOpenDevice, 0xA800, 0x5180014, SoundDispatch);


OSErr C_SPBCloseDevice(LONGINT inrefnum);PASCAL_SUBTRAP(SPBCloseDevice, 0xA800, 0x21C0014, SoundDispatch);


OSErr C_SPBRecord(SPBPtr inparamp, Boolean async);PASCAL_SUBTRAP(SPBRecord, 0xA800, 0x3200014, SoundDispatch);


OSErr C_SPBRecordToFile(INTEGER refnum, SPBPtr inparamp, Boolean async);PASCAL_SUBTRAP(SPBRecordToFile, 0xA800, 0x4240014, SoundDispatch);


OSErr C_SPBPauseRecording(LONGINT refnum);PASCAL_SUBTRAP(SPBPauseRecording, 0xA800, 0x2280014, SoundDispatch);


OSErr C_SPBResumeRecording(LONGINT refnum);PASCAL_SUBTRAP(SPBResumeRecording, 0xA800, 0x22C0014, SoundDispatch);


OSErr C_SPBStopRecording(LONGINT refnum);PASCAL_SUBTRAP(SPBStopRecording, 0xA800, 0x2300014, SoundDispatch);


OSErr C_SPBGetRecordingStatus(LONGINT refnum, GUEST<INTEGER> *recordingstatus, GUEST<INTEGER> *meterlevel, GUEST<LONGINT> *totalsampstorecord, GUEST<LONGINT> *numsampsrecorded, GUEST<LONGINT> *totalmsecstorecord, GUEST<LONGINT> *numbermsecsrecorded);PASCAL_SUBTRAP(SPBGetRecordingStatus, 0xA800, 0xE340014, SoundDispatch);


OSErr C_SPBGetDeviceInfo(LONGINT refnum, OSType info, Ptr infop);PASCAL_SUBTRAP(SPBGetDeviceInfo, 0xA800, 0x6380014, SoundDispatch);


OSErr C_SPBSetDeviceInfo(LONGINT refnum, OSType info, Ptr infop);PASCAL_SUBTRAP(SPBSetDeviceInfo, 0xA800, 0x63C0014, SoundDispatch);


OSErr C_SetupSndHeader(Handle sndhandle, INTEGER numchannels, Fixed rate, INTEGER size, OSType compresion, INTEGER basefreq, LONGINT numbytes, GUEST<INTEGER> *headerlenp);PASCAL_SUBTRAP(SetupSndHeader, 0xA800, 0xD480014, SoundDispatch);


OSErr C_SetupAIFFHeader(INTEGER refnum, INTEGER numchannels, Fixed samplerate, INTEGER samplesize, OSType compression, LONGINT numbytes, LONGINT numframes);PASCAL_SUBTRAP(SetupAIFFHeader, 0xA800, 0xB4C0014, SoundDispatch);


OSErr C_SPBSignInDevice(INTEGER refnum, ConstStringPtr name);PASCAL_SUBTRAP(SPBSignInDevice, 0xA800, 0x30C0014, SoundDispatch);


OSErr C_SPBSignOutDevice(INTEGER refnum);PASCAL_SUBTRAP(SPBSignOutDevice, 0xA800, 0x1100014, SoundDispatch);


OSErr C_SPBGetIndexedDevice(INTEGER count, ConstStringPtr name, GUEST<Handle> *deviceiconhandlep);PASCAL_SUBTRAP(SPBGetIndexedDevice, 0xA800, 0x5140014, SoundDispatch);


OSErr C_SPBMillisecondsToBytes(LONGINT refnum, GUEST<LONGINT> *millip);PASCAL_SUBTRAP(SPBMillisecondsToBytes, 0xA800, 0x4400014, SoundDispatch);


OSErr C_SPBBytesToMilliseconds(LONGINT refnum, GUEST<LONGINT> *bytecountp);PASCAL_SUBTRAP(SPBBytesToMilliseconds, 0xA800, 0x4440014, SoundDispatch);


OSErr C_GetSysBeepVolume(GUEST<LONGINT> *levelp);PASCAL_SUBTRAP(GetSysBeepVolume, 0xA800, 0x2240018, SoundDispatch);


OSErr C_SetSysBeepVolume(LONGINT level);PASCAL_SUBTRAP(SetSysBeepVolume, 0xA800, 0x2280018, SoundDispatch);


OSErr C_GetDefaultOutputVolume(GUEST<LONGINT> *levelp);PASCAL_SUBTRAP(GetDefaultOutputVolume, 0xA800, 0x22C0018, SoundDispatch);


OSErr C_SetDefaultOutputVolume(LONGINT level);PASCAL_SUBTRAP(SetDefaultOutputVolume, 0xA800, 0x2300018, SoundDispatch);


OSErr C_GetSoundHeaderOffset(Handle sndHandle, GUEST<LONGINT> *offset);PASCAL_SUBTRAP(GetSoundHeaderOffset, 0xA800, 0x4040018, SoundDispatch);


UnsignedFixed C_UnsignedFixedMulDiv(UnsignedFixed value, UnsignedFixed multiplier, UnsignedFixed divisor);PASCAL_SUBTRAP(UnsignedFixedMulDiv, 0xA800, 0x60C0018, SoundDispatch);


OSErr C_GetCompressionInfo(INTEGER compressionID, OSType format, INTEGER numChannels, INTEGER sampleSize, CompressionInfoPtr cp);PASCAL_SUBTRAP(GetCompressionInfo, 0xA800, 0x7100018, SoundDispatch);


OSErr C_SetSoundPreference(OSType theType, ConstStringPtr name, Handle settings);PASCAL_SUBTRAP(SetSoundPreference, 0xA800, 0x6340018, SoundDispatch);


OSErr C_GetSoundPreference(OSType theType, ConstStringPtr name, Handle settings);PASCAL_SUBTRAP(GetSoundPreference, 0xA800, 0x6380018, SoundDispatch);


OSErr C_SndGetInfo(SndChannelPtr chan, OSType selector, void *infoPtr);PASCAL_SUBTRAP(SndGetInfo, 0xA800, 0x63C0018, SoundDispatch);


OSErr C_SndSetInfo(SndChannelPtr chan, OSType selector, void *infoPtr);PASCAL_SUBTRAP(SndSetInfo, 0xA800, 0x6400018, SoundDispatch);


OSErr C_SndPlay(SndChannelPtr chanp, Handle sndh, Boolean async);PASCAL_TRAP(SndPlay, 0xA805);


OSErr C_SndNewChannel(GUEST<SndChannelPtr> *chanpp, INTEGER synth, LONGINT init, SndCallbackUPP userroutinep);PASCAL_TRAP(SndNewChannel, 0xA807);


OSErr C_SndAddModifier(SndChannelPtr chanp, ProcPtr mod, INTEGER id, LONGINT init);PASCAL_TRAP(SndAddModifier, 0xA802);


OSErr C_SndDoCommand(SndChannelPtr chanp, SndCommand *cmdp, Boolean nowait);PASCAL_TRAP(SndDoCommand, 0xA803);


OSErr C_SndDoImmediate(SndChannelPtr chanp, SndCommand *cmdp);PASCAL_TRAP(SndDoImmediate, 0xA804);


OSErr C_SndControl(INTEGER id, SndCommand *cmdp);PASCAL_TRAP(SndControl, 0xA806);


OSErr C_SndDisposeChannel(SndChannelPtr chanp, Boolean quitnow);PASCAL_TRAP(SndDisposeChannel, 0xA801);


void C_FinaleUnknown1();PASCAL_SUBTRAP(FinaleUnknown1, 0xA800, 0x0000, SoundDispatch);


OSErr C_FinaleUnknown2(ResType , LONGINT , Ptr , Ptr );PASCAL_SUBTRAP(FinaleUnknown2, 0xA800, 0x40004, SoundDispatch);


LONGINT C_DirectorUnknown3();PASCAL_SUBTRAP(DirectorUnknown3, 0xA800, 0x0004, SoundDispatch);


INTEGER C_DirectorUnknown4(ResType , INTEGER , Ptr , Ptr );PASCAL_SUBTRAP(DirectorUnknown4, 0xA800, 0x1C0004, SoundDispatch);


// MacPhoenix (name is a guess): SoundDispatch group 0, selector 4, as
// the Quadra ROM has it ($4043F30). SoundDispatch's low word is a
// dispatch group, an offset into a table of handlers hanging off the
// Sound Manager globals (ExpandMem+$110, +$2A); this puts a handler
// there (group < $24), which then gets every call whose low word is
// the group with D0 swapped (the selector in the low word). Apple's
// Speech Manager installs its 68k handler as group $C
// (sound/snddispatch.cpp emulates the ROM's dispatcher).
OSErr C_SndInstallDispatchGroup(INTEGER group, ProcPtr handler);PASCAL_SUBTRAP(SndInstallDispatchGroup, 0xA800, 0x40000, SoundDispatch);


// MacPhoenix (name is a guess): group 0, selector 8: the group's slot
// goes back to the "no such group" handler.
OSErr C_SndRemoveDispatchGroup(INTEGER group);PASCAL_SUBTRAP(SndRemoveDispatchGroup, 0xA800, 0x80000, SoundDispatch);


} /* namespace Executor */