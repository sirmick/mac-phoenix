//
//  SpeechManager-MacBridge.cpp
//  CocoaExecutor
//
//  Created by C.W. Betts on 8/4/14.
//  Copyright (c) 2014 C.W. Betts. All rights reserved.
//

#include <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#define BOOL BOOL_EXEC
#undef YES
#undef NO
#define YES YES_EXEC
#define NO NO_EXEC
#undef SIGDIGLEN
#include <base/common.h>
#include <MemoryMgr.h>
#include <SpeechManager-MacBridge.h>
#undef BOOL
#undef YES
#undef NO
#define YES 1
#define NO 0

#include <vector>
#include <map>

#define VoiceCreatorIDKey @"VoiceSynthesizerNumericID"
#define VoiceIDKey @"VoiceNumericID"
#define VoiceNameKey @"VoiceName"

// OSType of voices that started out as MacInTalk 3 voices
#define kMacInTalk3VoiceCreator ((Executor::OSType)'mtk3')
// OSType of voices that started out as MacInTalk Pro voices
#define kMacInTalkProVoiceCreator ((Executor::OSType)'gala')

using namespace Executor;

static NSSpeechSynthesizer *internalSynthesizer;
static NSArray *speechVoices;
static std::map<Executor::LONGINT, NSSpeechSynthesizer *> synthesizerMap;
static dispatch_once_t initSpeech = 0;

static dispatch_block_t initSpeechBlock= ^{
  synthesizerMap = std::map<Executor::LONGINT, NSSpeechSynthesizer *>();
  NSMutableArray *tmpVoices = [[NSMutableArray alloc] init];
  @autoreleasepool {
    // Contains a dictionary of voices where the creator and ID aren't included in Cocoa.
    NSDictionary<NSString*,NSDictionary<NSString*,id>*> *oldVoicesDict = @{
      @"com.apple.speech.synthesis.voice.Agnes": @{
        VoiceCreatorIDKey: @(kMacInTalkProVoiceCreator),
        VoiceIDKey: @(300),
      },
      @"com.apple.speech.synthesis.voice.Albert": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(41),
      },
      @"com.apple.speech.synthesis.voice.BadNews": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(36),
      },
      @"com.apple.speech.synthesis.voice.Bahh": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(40),
      },
      @"com.apple.speech.synthesis.voice.Bells": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(26),
      },
      @"com.apple.speech.synthesis.voice.Boing": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(16),
      },
      @"com.apple.speech.synthesis.voice.Bruce": @{
        VoiceCreatorIDKey: @(kMacInTalkProVoiceCreator),
        VoiceIDKey: @(100),
      },
      @"com.apple.speech.synthesis.voice.Bubbles": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(50),
      },
      @"com.apple.speech.synthesis.voice.Cellos": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(35),
      },
      @"com.apple.speech.synthesis.voice.Deranged": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(38),
      },
      @"com.apple.speech.synthesis.voice.Fred": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(1),
      },
      @"com.apple.speech.synthesis.voice.GoodNews": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(39),
      },
      @"com.apple.speech.synthesis.voice.Hysterical": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(30),
      },
      @"com.apple.speech.synthesis.voice.Junior": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(4),
      },
      @"com.apple.speech.synthesis.voice.Kathy": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(2),
      },
      @"com.apple.speech.synthesis.voice.Organ": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(31),
      },
      @"com.apple.speech.synthesis.voice.Princess": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(3),
      },
      @"com.apple.speech.synthesis.voice.Ralph": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(5),
      },
      @"com.apple.speech.synthesis.voice.Trinoids": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(9),
      },
      @"com.apple.speech.synthesis.voice.Victoria": @{
        VoiceCreatorIDKey: @(kMacInTalkProVoiceCreator),
        VoiceIDKey: @(200),
      },
      @"com.apple.speech.synthesis.voice.Whisper": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(6),
      },
      @"com.apple.speech.synthesis.voice.Zarvox": @{
        VoiceCreatorIDKey: @(kMacInTalk3VoiceCreator),
        VoiceIDKey: @(8),
      }
    };
    for (NSString *aVoice in [NSSpeechSynthesizer availableVoices]) {
      NSDictionary *voiceDict = [NSSpeechSynthesizer attributesForVoice:aVoice];
      NSNumber *synthesizerID = voiceDict[@"VoiceSynthesizerNumericID"];
      NSNumber *voiceID = voiceDict[@"VoiceNumericID"];
      
      if ((!synthesizerID || !voiceID) && oldVoicesDict[aVoice]) {
        synthesizerID = oldVoicesDict[aVoice][VoiceCreatorIDKey];
        voiceID = oldVoicesDict[aVoice][VoiceIDKey];
      }
      
      if (!synthesizerID || !voiceID) {
        continue;
      }
      
      [tmpVoices addObject:@{VoiceNameKey: aVoice,
                             VoiceCreatorIDKey: synthesizerID,
                             VoiceIDKey: voiceID}];
    }
  }
  
  speechVoices = [tmpVoices copy];
  [tmpVoices release];
  internalSynthesizer = [[NSSpeechSynthesizer alloc] init];
};

#define BeginSpeech() dispatch_once(&initSpeech, initSpeechBlock)

Executor::NumVersion MacBridge::SpeechManagerVersion (void)
{
  ::NumVersion theVers = ::SpeechManagerVersion();
  return theVers.majorRev << 24 | theVers.minorAndBugRev << 16 | theVers.stage << 8 | theVers.nonRelRev;
}

int16_t MacBridge::SpeechBusy (void)
{
  BeginSpeech();
  @autoreleasepool {
    return internalSynthesizer.speaking;
  }
}

int16_t MacBridge::SpeechBusySystemWide(void)
{
  @autoreleasepool {
    return [NSSpeechSynthesizer isAnyApplicationSpeaking];
  }
}

Executor::OSErr MacBridge::CountVoices (GUEST<int16_t> *numVoices)
{
  @autoreleasepool {
    if (!numVoices) {
      return ::paramErr;
    }
    
    BeginSpeech();
    SInt16 voiceCount = [NSSpeechSynthesizer availableVoices].count;
    
    *numVoices = voiceCount;
    return Executor::noErr;
  }
}

Executor::OSErr MacBridge::DisposeSpeechChannel(Executor::SpeechChannel chan)
{
  //BeginSpeech();
  Executor::LONGINT ourDat = chan->data[0];
  NSSpeechSynthesizer *synth = synthesizerMap[ourDat];
  Executor::DisposePtr((Executor::Ptr)chan);
  [synth release];
  synthesizerMap.erase(ourDat);

  return Executor::noErr;
}

Executor::OSErr MacBridge::SpeakString (Executor::ConstStr255Param textToBeSpoken)
{
  BeginSpeech();
  @autoreleasepool {
    NSString *ourStr = CFBridgingRelease(CFStringCreateWithPascalString(kCFAllocatorDefault, textToBeSpoken, kCFStringEncodingMacRoman));
    BOOL isBegin = [internalSynthesizer startSpeakingString:ourStr];
    Executor::OSErr toRet = isBegin ? Executor::noErr : -1;
    
    return toRet;
  }
}

Executor::OSErr MacBridge::StopSpeech (Executor::SpeechChannel chan)
{
  BeginSpeech();
  NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
  @autoreleasepool {
    [synth stopSpeaking];
  }
  
  return Executor::noErr;
}

Executor::OSErr MacBridge::ContinueSpeech (Executor::SpeechChannel chan)
{
  BeginSpeech();
  @autoreleasepool {
    NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
    [synth continueSpeaking];

    return Executor::noErr;
  }
}

static inline void MacVoiceSpecToExecutorVoiceSpec(Executor::VoiceSpec &ExecutorVoice, ::VoiceSpec &MacVoice)
{
  ExecutorVoice.creator = MacVoice.creator;
  ExecutorVoice.id = MacVoice.id;
}

Executor::OSErr MacBridge::GetIndVoice (int16_t index, Executor::VoiceSpec *voice)
{
  @autoreleasepool {
    BeginSpeech();
    if (!voice) {
      return Executor::paramErr;
    }
    NSArray *voices = [NSSpeechSynthesizer availableVoices];
    if (index >= voices.count) {
      return (Executor::OSErr)(-244);
    }
    NSDictionary *voiceDict = [NSSpeechSynthesizer attributesForVoice:voices[index]];
    
    NSNumber *synthesizerID = voiceDict[@"VoiceSynthesizerNumericID"];
    NSNumber *voiceID = voiceDict[@"VoiceNumericID"];
    
    voice->creator = [synthesizerID unsignedIntValue];
    voice->id = [voiceID unsignedIntValue];
    
  }
  
  return Executor::noErr;
}

Executor::OSErr MacBridge::NewSpeechChannel (Executor::VoiceSpec *voice, Executor::GUEST<Executor::SpeechChannel> *chan)
{
  static Executor::LONGINT speechChanData = 0;
  @autoreleasepool {
    BeginSpeech();
    NSString *voiceID = nil;
    ::OSType voiceIDFCC = voice->id;
    ::OSType voiceCreatorFCC = voice->creator;
    
    for (NSDictionary *aVoice in speechVoices) {
      
      //NSDictionary *voiceDict = [NSSpeechSynthesizer attributesForVoice:aVoice];
      NSNumber *synthesizerID = aVoice[VoiceCreatorIDKey];
      NSNumber *voiceID = aVoice[VoiceIDKey];
      
      if ([synthesizerID unsignedIntValue] == voiceCreatorFCC && [voiceID unsignedIntValue] == voiceIDFCC) {
        voiceID = aVoice[VoiceNameKey];
      }
    }
    
    if (voiceID == nil) {
      return (Executor::OSErr)(-244);
    }
    
    Executor::SpeechChannelRecord aChan;
    aChan.data[0] = ++speechChanData;
    
    *chan = (Executor::SpeechChannel)Executor::NewPtr(sizeof(Executor::SpeechChannelRecord));
    
    **chan = aChan;
    NSSpeechSynthesizer *NSsynth = [[NSSpeechSynthesizer alloc] initWithVoice:voiceID];
    synthesizerMap[aChan.data[0]] = NSsynth;
    
    return Executor::noErr;
  }
}

Executor::OSErr MacBridge::StopSpeechAt (Executor::SpeechChannel chan, int32_t whereToStop)
{
  NSSpeechBoundary boundary;
  NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
  switch (whereToStop) {
      //kImmediate
    case 0:
      boundary = NSSpeechImmediateBoundary;
      break;
      
      //kEndOfWord
    case 1:
      boundary = NSSpeechWordBoundary;
      break;
      
    case 2:
      boundary = NSSpeechSentenceBoundary;
      break;
      
    default:
      return Executor::paramErr;
      break;
  }
  
  @autoreleasepool {
    [synth stopSpeakingAtBoundary:boundary];
  }
  return Executor::noErr;
}

Executor::OSErr MacBridge::PauseSpeechAt (Executor::SpeechChannel chan, int32_t whereToPause)
{
  NSSpeechBoundary boundary;
  NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
  switch (whereToPause) {
      //kImmediate
    case 0:
      boundary = NSSpeechImmediateBoundary;
      break;
      
      //kEndOfWord
    case 1:
      boundary = NSSpeechWordBoundary;
      break;
      
    case 2:
      boundary = NSSpeechSentenceBoundary;
      break;
      
    default:
      return Executor::paramErr;
      break;
  }
  
  @autoreleasepool {
    [synth pauseSpeakingAtBoundary:boundary];
  }
  
  return Executor::noErr;
}

Executor::OSErr MacBridge::SetSpeechRate(Executor::SpeechChannel chan, Executor::Fixed rate)
{
  @autoreleasepool {
    NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
    
    
    synth.rate = FixedToFloat(rate);
  }
  
  return Executor::noErr;
}

Executor::OSErr MacBridge::GetSpeechRate (Executor::SpeechChannel chan, Executor::GUEST<Executor::Fixed> *rate)
{
  if (!rate) {
    return Executor::paramErr;
  }
  @autoreleasepool {
    NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
    
    ::Fixed ourRate = FloatToFixed(synth.rate);
    
    *rate = ourRate;
  }
  
  return Executor::noErr;
}

Executor::OSErr MacBridge::SetSpeechPitch (Executor::SpeechChannel chan, Executor::Fixed pitch)
{
  Executor::OSErr wasSuccess = Executor::noErr;
  @autoreleasepool {
    NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
    
    float ourFloatPitch = FixedToFloat(pitch);
    
    wasSuccess = [synth setObject:@(ourFloatPitch) forProperty:NSSpeechPitchModProperty error:nullptr] ? Executor::noErr : -1;
  }
  
  return wasSuccess;
}

Executor::OSErr MacBridge::GetSpeechPitch (Executor::SpeechChannel chan, Executor::GUEST<Executor::Fixed> *pitch)
{
  if (!pitch) {
    return Executor::paramErr;
  }
  ::OSErr toRet = Executor::noErr;
  @autoreleasepool {
    NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
    
    NSNumber *ourNum = [synth objectForProperty:NSSpeechPitchModProperty error:nil];
    if (ourNum == nullptr) {
      toRet = -1;
    } else {
      Executor::Fixed ourPitch = FloatToFixed(ourNum.floatValue);
      *pitch = ourPitch;
    }
  }
  
  return toRet;
}

#undef NewHandle

Executor::OSErr MacBridge::UseDictionary (Executor::SpeechChannel chan, Executor::Handle dictionary)
{
#if 0
  @autoreleasepool {
    ::Size ExecSize = BigEndianValue(Executor::GetHandleSize(dictionary));
    NSData *aData = [NSData dataWithBytes:*dictionary length:ExecSize];
    NSSpeechSynthesizer *aSynth = synthesizerMap[chan];
  }
#endif
  warning_unimplemented ("");
  return Executor::paramErr;
}

Executor::OSErr MacBridge::MakeVoiceSpec (Executor::OSType creator, Executor::OSType identifier, Executor::VoiceSpec *voice)
{
  if (!voice) {
    return Executor::paramErr;
  }
  ::VoiceSpec nativeSpec = {0};
  ::OSErr toRet = ::MakeVoiceSpec(creator, identifier, &nativeSpec);
  MacVoiceSpecToExecutorVoiceSpec(*voice, nativeSpec);
  
  return toRet;
}

Executor::OSErr MacBridge::GetVoiceDescription (
											const Executor::VoiceSpec *voice,
											Executor::VoiceDescription *info,
											Executor::LONGINT infoLength)
{
  if (infoLength != 362) {
    return Executor::paramErr;
  }
  if (voice == nullptr || info == nullptr) {
    return Executor::paramErr;
  }
  
  @autoreleasepool {
    info->length = 362;
    //NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
    NSString *voiceID = nil;
    ::OSType voiceIDFCC = voice->id;
    ::OSType voiceCreatorFCC = voice->creator;
    
    for (NSDictionary *aVoice in speechVoices) {
      
      //NSDictionary *voiceDict = [NSSpeechSynthesizer attributesForVoice:aVoice];
      NSNumber *synthesizerID = aVoice[VoiceCreatorIDKey];
      NSNumber *voiceID = aVoice[VoiceIDKey];
      
      if ([synthesizerID unsignedIntValue] == voiceCreatorFCC && [voiceID unsignedIntValue] == voiceIDFCC) {
        voiceID = aVoice[VoiceNameKey];
      }
    }
    if (voiceID == nil) {
      return (Executor::OSErr)(-244);
    }
    
    NSDictionary *aVoiceInfo = [NSSpeechSynthesizer attributesForVoice:voiceID];
    NSString *name = aVoiceInfo[NSVoiceName];
    CFStringGetPascalString((CFStringRef)name, info->name, 63, kCFStringEncodingMacRoman);
    info->age = [aVoiceInfo[NSVoiceAge] shortValue];
    info->voice = *voice;
    info->version = 1; //TODO: get real version!
    {
      NSString *gender = aVoiceInfo[NSVoiceGender];
      if ([gender isEqualToString:NSVoiceGenderMale]) {
        info->gender = (int16_t)1; //kMale
      } else if ([gender isEqualToString:NSVoiceGenderFemale]) {
        info->gender = (int16_t)2; //kFemale
      } else {
        info->gender = (int16_t)0;
      }
    }
    
    NSString *comment = aVoiceInfo[NSVoiceDemoText];
    CFStringGetPascalString((CFStringRef)comment, info->comment, 255, kCFStringEncodingMacRoman);
    
    LangCode aLang = 0;
    RegionCode aRegion = 0;
    ::OSStatus ourStat = ::LocaleStringToLangAndRegionCodes([aVoiceInfo[NSVoiceLocaleIdentifier] UTF8String], &aLang, &aRegion);
    if (ourStat != ::noErr) {
      ourStat = ::LocaleStringToLangAndRegionCodes("en-US", &aLang, &aRegion);
      if (ourStat != ::noErr) {
        return -244;
      }
    }
    info->language = aLang;
    info->region = aRegion;
    info->script = (int16_t)NSMacOSRomanStringEncoding;
    memset(info->reserved, 0, sizeof(info->reserved));
  }
  
  return Executor::noErr;
}

Executor::OSErr MacBridge::GetVoiceInfo (const Executor::VoiceSpec *voice, Executor::OSType selector, void *voiceInfo)
{
#if 0
  @autoreleasepool {
    //NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
    NSString *voiceID = nil;
    ::OSType voiceIDFCC = BigEndianValue(voice->id);
    ::OSType voiceCreatorFCC = BigEndianValue(voice->creator);
    
    for (NSDictionary *aVoice in speechVoices) {
      
      //NSDictionary *voiceDict = [NSSpeechSynthesizer attributesForVoice:aVoice];
      NSNumber *synthesizerID = aVoice[VoiceCreatorIDKey];
      NSNumber *voiceID = aVoice[VoiceIDKey];
      
      if ([synthesizerID unsignedIntValue] == voiceCreatorFCC && [voiceID unsignedIntValue] == voiceIDFCC) {
        voiceID = aVoice[VoiceNameKey];
      }
    }
    if (voiceID == nil) {
      return (Executor::OSErr)(-244);
    }
    
    NSDictionary *aVoiceInfo = [NSSpeechSynthesizer attributesForVoice:voiceID];
  }
#endif
  // TODO: handle different data types
  ::OSErr toRet = ::noErr;
  //::OSErr toRet = ::GetVoiceInfo((const ::VoiceSpec*)voice, BigEndianValue(selector), voiceInfo);
  
  switch (selector) {
    case soVoiceFile:
      //TODO: implement?
      warning_unimplemented ("");
	  toRet = Executor::fnfErr;
      break;
      
    case soVoiceDescription:
      return MacBridge::GetVoiceDescription(voice, (Executor::VoiceDescription*)voiceInfo, 362);
      break;
      
    default:
      break;
  }
  return toRet;
}

Executor::OSErr MacBridge::SpeakText (Executor::SpeechChannel chan, const void *textBuf, Executor::ULONGINT textBytes)
{
  Executor::OSErr theErr = ::noErr;
  @autoreleasepool {
    NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
    
    NSString *ourStr = [[NSString alloc] initWithBytes:textBuf length:textBytes encoding:NSMacOSRomanStringEncoding];
    
    if (![synth startSpeakingString:ourStr]) {
      theErr = -1;
    }
  }
  return theErr;
}

Executor::OSErr MacBridge::SetSpeechInfo (
									  Executor::SpeechChannel chan,
									  Executor::OSType selector,
									  const void *speechInfo
									  )
{
  // TODO: handle different data types
  //::OSErr toRet = ::SetSpeechInfo((::SpeechChannel)chan, BigEndianValue(selector), speechInfo);
  ::OSErr toRet = ::noErr;
  
  warning_unimplemented ("");
  return toRet;
}

Executor::OSErr MacBridge::GetSpeechInfo (
                                          Executor::SpeechChannel chan,
                                          Executor::OSType selector,
                                          void *speechInfo)
{
  // TODO: handle different data types
  //::OSErr toRet = ::GetSpeechInfo((::SpeechChannel)chan, BigEndianValue(selector), speechInfo);
  ::OSErr toRet = ::noErr;

  warning_unimplemented ("");
  return toRet;
}

namespace MacBridge {
  typedef NS_OPTIONS(SInt32, SpeechFlags) {
    kNoEndingProsody = 1,
    kNoSpeechInterrupt = 2,
    kPreflightThenPause = 4
  };
}

Executor::OSErr MacBridge::SpeakBuffer (
									Executor::SpeechChannel chan,
									const void *textBuf,
									Executor::ULONGINT textBytes,
									int32_t controlFlags
									)
{
  ::OSErr toRet = ::noErr;
  @autoreleasepool {
    //TODO: handle flags
    Executor::ULONGINT textSize = textBytes;
    NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
    NSString *aStr = [[NSString alloc] initWithBytes:textBuf length:textSize encoding:NSMacOSRomanStringEncoding];
    if (![synth startSpeakingString:aStr]) {
      toRet = -1;
    }
  }
  
	return toRet;
}

Executor::OSErr MacBridge::TextToPhonemes (Executor::SpeechChannel chan, const void *textBuf, Executor::ULONGINT textBytes, Executor::Handle phonemeBuf, GUEST<Executor::LONGINT> *phonemeBytes)
{
  Executor::ULONGINT ExecSize = Executor::GetHandleSize(phonemeBuf);
  Executor::ULONGINT textSize = textBytes;
  ::OSErr toRet = ::noErr;
  @autoreleasepool {
    NSSpeechSynthesizer *synth = synthesizerMap[chan->data[0]];
    NSString *aStr = [[NSString alloc] initWithBytes:textBuf length:textSize encoding:NSMacOSRomanStringEncoding];
    NSString *phonemes = [synth phonemesFromText:aStr];
    [aStr release];
    Executor::LONGINT lengthInMacRoman = (Executor::LONGINT)[phonemes lengthOfBytesUsingEncoding:NSMacOSRomanStringEncoding];
    
    if (lengthInMacRoman == 0) {
      return -224;
    }
    if (lengthInMacRoman > ExecSize) {
      Executor::SetHandleSize(phonemeBuf, lengthInMacRoman);
    }
    strcpy((char*)*phonemeBuf, [phonemes cStringUsingEncoding:NSMacOSRomanStringEncoding]);
    *phonemeBytes = lengthInMacRoman;
  }
  
	return toRet;
}
