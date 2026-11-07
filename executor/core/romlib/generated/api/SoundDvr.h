/*****************************************************************************
 *                                                                           *
 *                                 SoundDvr.h                                *
 *                                 ==========                                *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    Byte, INTEGER, Fixed, LONGINT, Ptr, Boolean             *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"

#define MODULE_NAME SoundDvr
#include <base/api-module.h>


namespace Executor {

enum {swMode = -1,
ftMode = 1,
ffMode = 0,
};

typedef Byte FreeWave[30001];

struct FFSynthRec{GUEST_STRUCT;GUEST<INTEGER> mode;GUEST<Fixed> fcount;GUEST<FreeWave> waveBytes;};static_assert(sizeof(FFSynthRec) == 30008);void describeStruct(const FFSynthRec&, std::ostream&);

typedef FFSynthRec *FFSynthPtr;

struct Tone{GUEST_STRUCT;GUEST<INTEGER> tcount;GUEST<INTEGER> amplitude;GUEST<INTEGER> tduration;};static_assert(sizeof(Tone) == 6);void describeStruct(const Tone&, std::ostream&);

typedef Tone Tones[5001];

struct SWSynthRec{GUEST_STRUCT;GUEST<INTEGER> mode;GUEST<Tones> triplets;};static_assert(sizeof(SWSynthRec) == 30008);void describeStruct(const SWSynthRec&, std::ostream&);

typedef SWSynthRec *SWSynthPtr;

typedef Byte Wave[256];

typedef GUEST<Wave> *WavePtr;

struct FTSoundRec{GUEST_STRUCT;GUEST<INTEGER> fduration;GUEST<Fixed> sound1Rate;GUEST<LONGINT> sound1Phase;GUEST<Fixed> sound2Rate;GUEST<LONGINT> sound2Phase;GUEST<Fixed> sound3Rate;GUEST<LONGINT> sound3Phase;GUEST<Fixed> sound4Rate;GUEST<LONGINT> sound4Phase;GUEST<WavePtr> sound1Wave;GUEST<WavePtr> sound2Wave;GUEST<WavePtr> sound3Wave;GUEST<WavePtr> sound4Wave;};static_assert(sizeof(FTSoundRec) == 50);void describeStruct(const FTSoundRec&, std::ostream&);

typedef FTSoundRec *FTSndRecPtr;

struct FTSynthRec{GUEST_STRUCT;GUEST<INTEGER> mode;GUEST<FTSndRecPtr> sndRec;};static_assert(sizeof(FTSynthRec) == 6);void describeStruct(const FTSynthRec&, std::ostream&);

typedef FTSynthRec *FTsynthPtr;

// SoundDvr IMII-232 (true-b);
const LowMemGlobal<Byte > SdVolume{ 0x260 };
inline Byte  LMGetSdVolume() { return LM(SdVolume); }
inline void LMSetSdVolume(Byte val) { LM(SdVolume) = val; }
NOTRAP_FUNCTION2(LMGetSdVolume);
NOTRAP_FUNCTION2(LMSetSdVolume);


// SoundDvr IMII-227 (false);
const LowMemGlobal<FTSndRecPtr > SoundPtr{ 0x262 };
inline FTSndRecPtr  LMGetSoundPtr() { return LM(SoundPtr); }
inline void LMSetSoundPtr(FTSndRecPtr val) { LM(SoundPtr) = val; }
NOTRAP_FUNCTION2(LMGetSoundPtr);
NOTRAP_FUNCTION2(LMSetSoundPtr);


// SoundDvr IMIII-21 (true-b);
const LowMemGlobal<Ptr > SoundBase{ 0x266 };
inline Ptr  LMGetSoundBase() { return LM(SoundBase); }
inline void LMSetSoundBase(Ptr val) { LM(SoundBase) = val; }
NOTRAP_FUNCTION2(LMGetSoundBase);
NOTRAP_FUNCTION2(LMSetSoundBase);


// SoundDvr IMII-234 (false);
const LowMemGlobal<Byte > SoundLevel{ 0x27F };
inline Byte  LMGetSoundLevel() { return LM(SoundLevel); }
inline void LMSetSoundLevel(Byte val) { LM(SoundLevel) = val; }
NOTRAP_FUNCTION2(LMGetSoundLevel);
NOTRAP_FUNCTION2(LMSetSoundLevel);


// SoundDvr IMII-226 (true-b);
const LowMemGlobal<INTEGER > CurPitch{ 0x280 };
inline INTEGER  LMGetCurPitch() { return LM(CurPitch); }
inline void LMSetCurPitch(INTEGER val) { LM(CurPitch) = val; }
NOTRAP_FUNCTION2(LMGetCurPitch);
NOTRAP_FUNCTION2(LMSetCurPitch);


void StartSound(Ptr srec, LONGINT nb, ProcPtr comp);

void StopSound();

Boolean SoundDone();

void GetSoundVol(GUEST<INTEGER> *volp);

void SetSoundVol(INTEGER vol);

} /* namespace Executor */