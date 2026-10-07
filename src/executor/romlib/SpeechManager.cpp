//
//  SpeechManager.cpp
//  CocoaExecutor
//
//  Created by C.W. Betts on 8/4/14.
//  Copyright (c) 2014 C.W. Betts. All rights reserved.
//

#include <base/common.h>
#include <SpeechManager.h>
#include <error/error.h>

#ifdef __APPLE__
#include <SpeechManager-MacBridge.h>
#endif

using namespace Executor;

NumVersion Executor::C_SpeechManagerVersion()
{
#ifdef __APPLE__
    return MacBridge::SpeechManagerVersion();
#else
    warning_unimplemented("");
    return 0;
#endif
}

int16_t Executor::C_SpeechBusy()
{
#ifdef __APPLE__
    return MacBridge::SpeechBusy();
#else
    warning_unimplemented("");
    return 0;
#endif
}

int16_t Executor::C_SpeechBusySystemWide()
{
#ifdef __APPLE__
    return MacBridge::SpeechBusySystemWide();
#else
    warning_unimplemented("");
    return 0;
#endif
}

OSErr Executor::C_CountVoices(GUEST<int16_t> *numVoices)
{
#ifdef __APPLE__
    return MacBridge::CountVoices(numVoices);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_DisposeSpeechChannel(SpeechChannel chan)
{
#ifdef __APPLE__
    return MacBridge::DisposeSpeechChannel(chan);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_SpeakString(ConstStringPtr textToBeSpoken)
{
#ifdef __APPLE__
    return MacBridge::SpeakString(textToBeSpoken);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_StopSpeech(SpeechChannel chan)
{
#ifdef __APPLE__
    return MacBridge::StopSpeech(chan);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_ContinueSpeech(SpeechChannel chan)
{
#ifdef __APPLE__
    return MacBridge::ContinueSpeech(chan);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_GetIndVoice(int16_t index, VoiceSpec *voice)
{
#ifdef __APPLE__
    return MacBridge::GetIndVoice(index, voice);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_NewSpeechChannel(VoiceSpec *voice, GUEST<SpeechChannel> *chan)
{
#ifdef __APPLE__
    return MacBridge::NewSpeechChannel(voice, chan);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_StopSpeechAt(SpeechChannel chan, int32_t whereToStop)
{
#ifdef __APPLE__
    return MacBridge::StopSpeechAt(chan, whereToStop);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_PauseSpeechAt(SpeechChannel chan, int32_t whereToPause)
{
#ifdef __APPLE__
    return MacBridge::PauseSpeechAt(chan, whereToPause);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_SetSpeechRate(SpeechChannel chan, Fixed rate)
{
#ifdef __APPLE__
    return MacBridge::SetSpeechRate(chan, rate);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_GetSpeechRate(SpeechChannel chan, GUEST<Fixed> *rate)
{
#ifdef __APPLE__
    return MacBridge::GetSpeechRate(chan, rate);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_SetSpeechPitch(SpeechChannel chan, Fixed pitch)
{
#ifdef __APPLE__
    return MacBridge::SetSpeechPitch(chan, pitch);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_GetSpeechPitch(SpeechChannel chan, GUEST<Fixed> *pitch)
{
#ifdef __APPLE__
    return MacBridge::GetSpeechPitch(chan, pitch);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_UseDictionary(SpeechChannel chan, Handle dictionary)
{
#ifdef __APPLE__
    return MacBridge::UseDictionary(chan, dictionary);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_MakeVoiceSpec(OSType creator, OSType id, VoiceSpec *voice)
{
#ifdef __APPLE__
    return MacBridge::MakeVoiceSpec(creator, id, voice);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_GetVoiceDescription(const VoiceSpec *voice,
                                      VoiceDescription *info,
                                      LONGINT infoLength)
{
#ifdef __APPLE__
    return MacBridge::GetVoiceDescription(voice, info, infoLength);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_GetVoiceInfo(const VoiceSpec *voice, OSType selector,
                               void *voiceInfo)
{
#ifdef __APPLE__
    return MacBridge::GetVoiceInfo(voice, selector, voiceInfo);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_SpeakText(SpeechChannel chan, const void *textBuf,
                            ULONGINT textBytes)
{
#ifdef __APPLE__
    return MacBridge::SpeakText(chan, textBuf, textBytes);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_SetSpeechInfo(SpeechChannel chan, OSType selector,
                                const void *speechInfo)
{
#ifdef __APPLE__
    return MacBridge::SetSpeechInfo(chan, selector, speechInfo);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_GetSpeechInfo(SpeechChannel chan, OSType selector,
                                void *speechInfo)
{
#ifdef __APPLE__
    return MacBridge::GetSpeechInfo(chan, selector, speechInfo);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_SpeakBuffer(SpeechChannel chan, const void *textBuf,
                              ULONGINT textBytes, int32_t controlFlags)
{
#ifdef __APPLE__
    return MacBridge::SpeakBuffer(chan, textBuf, textBytes, controlFlags);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}

OSErr Executor::C_TextToPhonemes(SpeechChannel chan, const void *textBuf,
                                 ULONGINT textBytes, Handle phonemeBuf,
                                 GUEST<LONGINT> *phonemeBytes)
{
#ifdef __APPLE__
    return MacBridge::TextToPhonemes(chan, textBuf, textBytes, phonemeBuf, phonemeBytes);
#else
    warning_unimplemented("");
    return paramErr;
#endif
}
