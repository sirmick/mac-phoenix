/*****************************************************************************
 *                                                                           *
 *                              SpeechManager.h                              *
 *                              ===============                              *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    OSType, Boolean, Str63, Str255, LONGINT, Str15,         *
 *                   Str31, Byte, OSErr, ConstStringPtr, Fixed, Handle,      *
 *                   ULONGINT                                                *
 *     FileMgr.h     FSSpec                                                  *
 *     SoundMgr.h    NumVersion                                              *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "FileMgr.h"
#include "SoundMgr.h"

#define MODULE_NAME SpeechManager
#include <base/api-module.h>


namespace Executor {

struct VoiceSpec{GUEST_STRUCT;GUEST<OSType> creator;GUEST<OSType> id;};void describeStruct(const VoiceSpec&, std::ostream&);

typedef VoiceSpec *VoiceSpecPtr;

struct VoiceFileInfo{GUEST_STRUCT;FSSpec fileSpec;GUEST<uint16_t> resID;};void describeStruct(const VoiceFileInfo&, std::ostream&);

struct SpeechStatusInfo{GUEST_STRUCT;Boolean outputBusy;Boolean outputPaused;GUEST<int32_t> inputBytesLeft;GUEST<int16_t> phonemeCode;};void describeStruct(const SpeechStatusInfo&, std::ostream&);

struct VoiceDescription{GUEST_STRUCT;GUEST<int32_t> length;VoiceSpec voice;GUEST<int32_t> version;GUEST<Str63> name;GUEST<Str255> comment;GUEST<int16_t> gender;GUEST<int16_t> age;GUEST<int16_t> script;GUEST<int16_t> language;GUEST<int16_t> region;GUEST<int32_t> reserved[4];};void describeStruct(const VoiceDescription&, std::ostream&);

struct SpeechChannelRecord{GUEST_STRUCT;GUEST<LONGINT> data[1];};void describeStruct(const SpeechChannelRecord&, std::ostream&);

typedef SpeechChannelRecord *SpeechChannel;

struct PhonemeInfo{GUEST_STRUCT;GUEST<int16_t> opcode;GUEST<Str15> phStr;GUEST<Str31> exampleStr;GUEST<int16_t> hiliteStart;GUEST<int16_t> hiliteEnd;};void describeStruct(const PhonemeInfo&, std::ostream&);

struct PhonemeDescriptor{GUEST_STRUCT;GUEST<int16_t> phonemeCount;PhonemeInfo thePhonemes[1];};void describeStruct(const PhonemeDescriptor&, std::ostream&);

struct SpeechXtndData{GUEST_STRUCT;GUEST<OSType> synthCreator;Byte synthData[2];};void describeStruct(const SpeechXtndData&, std::ostream&);

struct DelimiterInfo{GUEST_STRUCT;Byte startDelimiter[2];Byte endDelimiter[2];};void describeStruct(const DelimiterInfo&, std::ostream&);

EXTERN_DISPATCHER_TRAP(SoundDispatch, 0xA800, D0<0xFFFFFF>);
NumVersion C_SpeechManagerVersion();PASCAL_SUBTRAP(SpeechManagerVersion, 0xA800, 0x000C, SoundDispatch);


int16_t C_SpeechBusy();PASCAL_SUBTRAP(SpeechBusy, 0xA800, 0x3C000C, SoundDispatch);


int16_t C_SpeechBusySystemWide();PASCAL_SUBTRAP(SpeechBusySystemWide, 0xA800, 0x40000C, SoundDispatch);


OSErr C_CountVoices(GUEST<int16_t> *numVoices);PASCAL_SUBTRAP(CountVoices, 0xA800, 0x108000C, SoundDispatch);


OSErr C_DisposeSpeechChannel(SpeechChannel chan);PASCAL_SUBTRAP(DisposeSpeechChannel, 0xA800, 0x21C000C, SoundDispatch);


OSErr C_SpeakString(ConstStringPtr textToBeSpoken);PASCAL_SUBTRAP(SpeakString, 0xA800, 0x220000C, SoundDispatch);


OSErr C_StopSpeech(SpeechChannel chan);PASCAL_SUBTRAP(StopSpeech, 0xA800, 0x22C000C, SoundDispatch);


OSErr C_ContinueSpeech(SpeechChannel chan);PASCAL_SUBTRAP(ContinueSpeech, 0xA800, 0x238000C, SoundDispatch);


OSErr C_GetIndVoice(int16_t index, VoiceSpec *voice);PASCAL_SUBTRAP(GetIndVoice, 0xA800, 0x30C000C, SoundDispatch);


OSErr C_NewSpeechChannel(VoiceSpec *voice, GUEST<SpeechChannel> *chan);PASCAL_SUBTRAP(NewSpeechChannel, 0xA800, 0x418000C, SoundDispatch);


OSErr C_StopSpeechAt(SpeechChannel chan, int32_t whereToStop);PASCAL_SUBTRAP(StopSpeechAt, 0xA800, 0x430000C, SoundDispatch);


OSErr C_PauseSpeechAt(SpeechChannel chan, int32_t whereToPause);PASCAL_SUBTRAP(PauseSpeechAt, 0xA800, 0x434000C, SoundDispatch);


OSErr C_SetSpeechRate(SpeechChannel chan, Fixed rate);PASCAL_SUBTRAP(SetSpeechRate, 0xA800, 0x444000C, SoundDispatch);


OSErr C_GetSpeechRate(SpeechChannel chan, GUEST<Fixed> *rate);PASCAL_SUBTRAP(GetSpeechRate, 0xA800, 0x448000C, SoundDispatch);


OSErr C_SetSpeechPitch(SpeechChannel chan, Fixed pitch);PASCAL_SUBTRAP(SetSpeechPitch, 0xA800, 0x44C000C, SoundDispatch);


OSErr C_GetSpeechPitch(SpeechChannel chan, GUEST<Fixed> *pitch);PASCAL_SUBTRAP(GetSpeechPitch, 0xA800, 0x450000C, SoundDispatch);


OSErr C_UseDictionary(SpeechChannel chan, Handle dictionary);PASCAL_SUBTRAP(UseDictionary, 0xA800, 0x460000C, SoundDispatch);


OSErr C_MakeVoiceSpec(OSType creator, OSType id, VoiceSpec *voice);PASCAL_SUBTRAP(MakeVoiceSpec, 0xA800, 0x604000C, SoundDispatch);


OSErr C_GetVoiceDescription(const VoiceSpec *voice, VoiceDescription *info, LONGINT infoLength);PASCAL_SUBTRAP(GetVoiceDescription, 0xA800, 0x610000C, SoundDispatch);


OSErr C_GetVoiceInfo(const VoiceSpec *voice, OSType selector, void *voiceInfo);PASCAL_SUBTRAP(GetVoiceInfo, 0xA800, 0x614000C, SoundDispatch);


OSErr C_SpeakText(SpeechChannel chan, const void *textBuf, ULONGINT textBytes);PASCAL_SUBTRAP(SpeakText, 0xA800, 0x624000C, SoundDispatch);


OSErr C_SetSpeechInfo(SpeechChannel chan, OSType selector, const void *speechInfo);PASCAL_SUBTRAP(SetSpeechInfo, 0xA800, 0x654000C, SoundDispatch);


OSErr C_GetSpeechInfo(SpeechChannel chan, OSType selector, void *speechInfo);PASCAL_SUBTRAP(GetSpeechInfo, 0xA800, 0x658000C, SoundDispatch);


OSErr C_SpeakBuffer(SpeechChannel chan, const void *textBuf, ULONGINT textBytes, int32_t controlFlags);PASCAL_SUBTRAP(SpeakBuffer, 0xA800, 0x828000C, SoundDispatch);


OSErr C_TextToPhonemes(SpeechChannel chan, const void *textBuf, ULONGINT textBytes, Handle phonemeBuf, GUEST<LONGINT> *phonemeBytes);PASCAL_SUBTRAP(TextToPhonemes, 0xA800, 0xA5C000C, SoundDispatch);


} /* namespace Executor */