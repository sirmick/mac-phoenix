# QuickTime calls in applications' code (static)

tools/qt/qt_static.py IMAGE PATH: PowerPC imports from QuickTimeLib per PEF fragment, and the 68k trap glue in CODE resources ($AAAA/$AAA3 with the selector loaded just before; component-call glue by (paramSize, what), which may also be non-QuickTime components). Counts are call sites, not calls.

```
=== Games:Ares 1.2.0 ƒ:Ares
PowerPC fragment X-Ares PPC Final: InterfaceLib (317), NavigationLib (5), NetSprocketLib (23), OTInetClientLib (1), ObjectSupportLib (1), QuickTimeLib (19), SpeechLib (1)
  imports from QuickTimeLib: CloseMovieFile, DisposeMovie, EnterMovies, ExitMovies, GetMovieBox, GetMovieColorTable, GetMovieRate, GetMoviesError, IsMovieDone, MoviesTask, NewMovieFromFile, OpenMovieFile, PrerollMovie, SetMovieBox, SetMovieColorTable, SetMovieGWorld, SetMovieVolume, StartMovie, StopMovie
68k trap glue (44 sites):
  Component  what=1 p=0 (SGInitialize [QuickTimeComponents])              x1
  Component  what=10 p=0 (SCRequestImageSettings [QuickTimeComponents])   x1
  Component  what=16 p=24                                                 x1
  Component  what=17 p=20 (ImageCodecNewMemory [ImageCodec])              x1
  Component  what=5 p=2 (MCGetIndMovie [Movies] | InstrumentCloseComponentResFile [QuickTimeMusic]) x1
  Component  what=6 p=16 (XMLParseAddAttribute [QuickTimeComponents])     x1
  QuickTime  CloseMovieFile                                               x2
  QuickTime  DisposeMovie                                                 x2
  QuickTime  EnterMovies                                                  x1
  QuickTime  ExitMovies                                                   x1
  QuickTime  GetMovieBox                                                  x3
  QuickTime  GetMovieColorTable                                           x1
  QuickTime  GetMovieRate                                                 x2
  QuickTime  GetMoviesError                                               x6
  QuickTime  IsMovieDone                                                  x2
  QuickTime  MoviesTask                                                   x2
  QuickTime  NewMovieFromFile                                             x2
  QuickTime  OpenMovieFile                                                x2
  QuickTime  PrerollMovie                                                 x2
  QuickTime  SetMovieBox                                                  x2
  QuickTime  SetMovieColorTable                                           x1
  QuickTime  SetMovieGWorld                                               x2
  QuickTime  SetMovieVolume                                               x2
  QuickTime  StartMovie                                                   x2
  QuickTime  StopMovie                                                    x1
```

```
=== Games PPC:EV Nova 1.0.10 ƒ:EV Nova
PowerPC fragment EV Nova.carbon: Apple;Carbon;Multimedia (24), Apple;Carbon;Networking (47), CarbonLib (583), RezLib (9)
  imports from Apple;Carbon;Multimedia: BeginFullScreen, CloseMovieFile, DisposeMovie, EndFullScreen, EnterMovies, ExitMovies, GetMovieBox, GetMovieDuration, GetMovieTimeBase, GetTimeBaseFlags, GoToBeginningOfMovie, IsMovieDone, LoadMovieIntoRam, MoviesTask, NewMovieFromFile, OpenMovieFile, SetMovieBox, SetMovieGWorld, SetMoviePlayHints, SetMovieRate, SetMovieVolume, SetTimeBaseFlags, StartMovie, StopMovie
PowerPC fragment EV Nova.ppc: DisplayLib (4), ICAp;InternetConfigLib (9), InterfaceLib (470), MathLib (8), OTATalkClientLib (2), OTATalkSharedLib (1), OTClientLib (35), OTClientUtilLib (4), OTInetClientLib (5), QuickTimeLib (22), SoundLib (2), SpeechLib (4)
  imports from QuickTimeLib: CloseMovieFile, DisposeMovie, EnterMovies, ExitMovies, GetMovieBox, GetMovieDuration, GetMovieTimeBase, GetTimeBaseFlags, GoToBeginningOfMovie, IsMovieDone, LoadMovieIntoRam, MoviesTask, NewMovieFromFile, OpenMovieFile, SetMovieBox, SetMovieGWorld, SetMoviePlayHints, SetMovieRate, SetMovieVolume, SetTimeBaseFlags, StartMovie, StopMovie
PowerPC fragment FATCarbon 1.5.1: CFMPriv_CarbonCore (2), CarbonCore.vlib (1), CodeFragmentMgr (1)
PowerPC fragment RezLib: Apple;Carbon;Multimedia (2), CarbonLib (82)
  imports from Apple;Carbon;Multimedia: BeginFullScreen, EndFullScreen
68k trap glue (65 sites):
  Component  what=0 p=4 (ImageCodecGetCodecInfo [ImageCodec])             x1
  Component  what=1 p=0 (SGInitialize [QuickTimeComponents])              x1
  Component  what=10 p=0 (SCRequestImageSettings [QuickTimeComponents])   x1
  Component  what=17 p=20 (ImageCodecNewMemory [ImageCodec])              x1
  Component  what=18 p=8                                                  x1
  Component  what=19 p=16                                                 x1
  Component  what=2 p=6 (ClockNewCallBack [QuickTimeComponents] | VDGetActiveSrcRect [QuickTimeComponents]) x2
  Component  what=36 p=12 (MCPositionController [Movies] | ImageCodecValidateParameters [ImageCodec]) x1
  Component  what=5 p=2 (MCGetIndMovie [Movies] | InstrumentCloseComponentResFile [QuickTimeMusic]) x1
  QuickTime  ? (selector not loaded just before)                          x5
  QuickTime  CloseMovieFile                                               x3
  QuickTime  DisposeMovie                                                 x4
  QuickTime  EnterMovies                                                  x1
  QuickTime  ExitMovies                                                   x2
  QuickTime  GetMovieBox                                                  x1
  QuickTime  GetMovieDuration                                             x1
  QuickTime  GetMovieTimeBase                                             x1
  QuickTime  GetTimeBaseFlags                                             x1
  QuickTime  GoToBeginningOfMovie                                         x2
  QuickTime  IsMovieDone                                                  x3
  QuickTime  LoadMovieIntoRam                                             x1
  QuickTime  MoviesTask                                                   x3
  QuickTime  NewMovieFromFile                                             x3
  QuickTime  OpenMovieFile                                                x6
  QuickTime  SetMovieBox                                                  x3
  QuickTime  SetMovieGWorld                                               x2
  QuickTime  SetMoviePlayHints                                            x1
  QuickTime  SetMovieRate                                                 x1
  QuickTime  SetMovieVolume                                               x3
  QuickTime  SetTimeBaseFlags                                             x1
  QuickTime  StartMovie                                                   x4
  QuickTime  StopMovie                                                    x3
```

```
=== Games:Cythera 1.0.4 ƒ:Cythera
PowerPC fragment ControlStripLib: InterfaceLib (1)
PowerPC fragment Cythera: AppearanceLib (15), ContextualMenu (3), ControlStripLib (2), InputSprocketLib (10), InterfaceLib (498), MathLib (1), NavigationLib (9), QuickTimeLib (17), SoundLib (1), ThreadsLib (7)
  imports from QuickTimeLib: AddFilePreview, EnterMovies, MakeFilePreview, MakeThumbnailFromPixMap, NADisposeNoteChannel, NANewNoteChannel, NAPlayNote, NAStuffToneDescription, StandardGetFilePreview, TuneGetStatus, TunePreroll, TuneQueue, TuneSetHeader, TuneSetTimeScale, TuneSetVolume, TuneStop, TuneUnroll
68k trap glue (29 sites):
  Component  what=10 p=28 (TuneQueue [QuickTimeMusic])                    x3
  Component  what=12 p=4 (MCSetControllerPort [Movies] | MovieImportSetDuration [QuickTimeComponents] | VDSetBrightness [QuickTimeComponents]) x2
  Component  what=13 p=12 (QTVideoOutputGetGWorldParameters [QuickTimeComponents] | NAPlayNote [QuickTimeMusic]) x2
  Component  what=13 p=4 (SCCompressPictureFile [QuickTimeComponents] | MovieImportGetAuxiliaryDataType [QuickTimeComponents] | VDGetBrightness [QuickTimeComponents]) x2
  Component  what=16 p=4 (MCGetControllerBoundsRect [Movies] | VDSetSharpness [QuickTimeComponents] | XMLParseSetStartElementHandler [QuickTimeComponents]) x6
  Component  what=18 p=0 (MCGetControllerBoundsRgn [Movies] | SGIdle [QuickTimeComponents] | TunePreroll [QuickTimeMusic]) x2
  Component  what=19 p=0 (SGStop [QuickTimeComponents] | TuneUnroll [QuickTimeMusic]) x2
  Component  what=27 p=8 (GraphicsExportSetResolution [ImageCompression] | MusicGetMIDIProc [QuickTimeMusic] | NAStuffToneDescription [QuickTimeMusic]) x1
  Component  what=4 p=4 (DataCodecBeginInterruptSafe [QuickTimeComponents] | VDGetDigitizerRect [QuickTimeComponents] | QTVideoOutputGetClientName [QuickTimeComponents]) x2
  Component  what=4 p=8 (MovieImportSetSampleDescription [QuickTimeComponents] | PreviewEvent [QuickTimeComponents] | XMLParseGetDetailedParseError [QuickTimeComponents]) x1
  Component  what=5 p=4 (ClockCancelCallBack [QuickTimeComponents] | MovieImportSetMediaFile [QuickTimeComponents] | GraphicsImportSetDataHandle [ImageCompression]) x1
  Component  what=6 p=4 (ClockRateChanged [QuickTimeComponents] | VDGetMaskPixMap [QuickTimeComponents] | GraphicsImportGetDataHandle [ImageCompression]) x1
  ICM        AddFilePreview                                               x1
  ICM        MakeFilePreview                                              x1
  ICM        MakeThumbnailFromPixMap                                      x1
  ICM        StandardGetFilePreview                                       x1
```

```
=== Macintosh HD:Apple Extras:MoviePlayer:MoviePlayer
68k trap glue (363 sites):
  Component  what=1 p=12 (TweenerInitialize [QuickTimeComponents] | PreviewShowData [QuickTimeComponents] | InstrumentGetInst [QuickTimeMusic]) x1
  Component  what=12 p=4 (MCSetControllerPort [Movies] | MovieImportSetDuration [QuickTimeComponents] | VDSetBrightness [QuickTimeComponents]) x2
  Component  what=13 p=0 (MCGetControllerPort [Movies] | DataHTask [QuickTimeComponents]) x1
  Component  what=1306 p=8                                                x1
  Component  what=1309 p=12                                               x1
  Component  what=16 p=4 (MCGetControllerBoundsRect [Movies] | VDSetSharpness [QuickTimeComponents] | XMLParseSetStartElementHandler [QuickTimeComponents]) x3
  Component  what=2 p=12 (MCSetMovie [Movies] | XMLParseFile [QuickTimeComponents] | MusicGetPart [QuickTimeMusic]) x1
  Component  what=2 p=4 (TweenerDoTween [QuickTimeComponents] | QTVideoOutputGetCurrentClientName [QuickTimeComponents] | GraphicsExportCanTranscode [ImageCompression]) x1
  Component  what=20 p=4 (MCMovieChanged [Movies] | MovieImportGetSettingsAsAtomContainer [QuickTimeComponents] | DataHGetVolumeList [QuickTimeComponents]) x10
  Component  what=24 p=4 (MCDraw [Movies] | SCSetCompressFlags [QuickTimeComponents] | DataHGetFreeSpace [QuickTimeComponents]) x2
  Component  what=25 p=6 (MCActivate [Movies] | DataHCreateFile [QuickTimeComponents]) x1
  Component  what=26 p=0 (MCIdle [Movies] | NALoseDefaultMIDIInput [QuickTimeMusic]) x1
  Component  what=29 p=2 (MCEnableEditing [Movies])                       x1
  Component  what=3 p=6 (SCPositionDialog [QuickTimeComponents])          x1
  Component  what=31 p=0 (MCCopy [Movies] | MusicGetMasterTune [QuickTimeMusic]) x1
  Component  what=32 p=0 (MCCut [Movies])                                 x1
  Component  what=33 p=4 (MCPaste [Movies] | GraphicsImportGetMIMETypeList [ImageCompression] | GraphicsExportSetColorSyncProfile [ImageCompression]) x1
  Component  what=34 p=0 (MCClear [Movies])                               x1
  Component  what=35 p=0 (MCUndo [Movies])                                x1
  Component  what=36 p=12 (MCPositionController [Movies] | ImageCodecValidateParameters [ImageCodec]) x1
  Component  what=37 p=4 (MCGetControllerInfo [Movies] | DataHGetFileName [QuickTimeComponents] | VDGetClipState [QuickTimeComponents]) x10
  Component  what=4 p=18                                                  x1
  Component  what=43 p=8 (MCSetUpEditMenu [Movies] | MusicGetInstrumentKnobDescription [QuickTimeMusic] | NASetInstrumentNumberInterruptSafe [QuickTimeMusic]) x1
  Component  what=45 p=8 (MCSetActionFilterWithRefCon [Movies] | SGGetMode [QuickTimeComponents] | MusicGetKnobDescription [QuickTimeMusic]) x2
  Component  what=5 p=8 (SCSetTestImagePictFile [QuickTimeComponents] | SGGetGWorld [QuickTimeComponents] | GraphicsExportDoUseCompressor [ImageCompression]) x1
  Component  what=6 p=0 (MCRemoveAllMovies [Movies] | MCRemoveMovie [Movies] | DataHCloseForWrite [QuickTimeComponents]) x1
  Component  what=6 p=6                                                   x1
  Component  what=7 p=4 (MCIsPlayerEvent [Movies] | ClockTimeChanged [QuickTimeComponents] | SCGetBestDeviceRect [QuickTimeComponents]) x2
  Component  what=8 p=8 (MovieImportSetProgressProc [QuickTimeComponents] | GraphicsImportGetDataOffsetAndSize [ImageCompression] | TuneGetIndexedNoteChannel [QuickTimeMusic]) x1
  Component  what=9 p=6 (MCDoAction [Movies])                             x40
  ICM        AlignScreenRect                                              x1
  ICM        CustomGetFilePreview                                         x2
  ICM        DragAlignedWindow                                            x1
  ICM        DrawTrimmedPicture                                           x1
  ICM        GDGetScale                                                   x1
  ICM        GDSetScale                                                   x2
  ICM        GetBestDeviceRect                                            x1
  ICM        MakeFilePreview                                              x2
  ICM        SFGetFilePreview                                             x1
  QuickTime  #273                                                         x2
  QuickTime  #274                                                         x2
  QuickTime  AddMovieResource                                             x1
  QuickTime  AddUserData                                                  x5
  QuickTime  BeginFullScreen                                              x2
  QuickTime  ClearMovieChanged                                            x1
  QuickTime  ClearMoviesStickyError                                       x2
  QuickTime  CloseMovieFile                                               x4
  QuickTime  ConvertMovieToFile                                           x1
  QuickTime  CountUserDataType                                            x5
  QuickTime  CreateMovieFile                                              x1
  QuickTime  DeleteMovieFile                                              x2
  QuickTime  DisposeMovie                                                 x9
  QuickTime  DisposeMovieController                                       x3
  QuickTime  DisposeMovieEditState                                        x2
  QuickTime  DisposeMovieTrack                                            x3
  QuickTime  EndFullScreen                                                x1
  QuickTime  EnterMovies                                                  x1
  QuickTime  FlattenMovie                                                 x1
  QuickTime  GetMediaHandler                                              x2
  QuickTime  GetMediaHandlerDescription                                   x2
  QuickTime  GetMediaLanguage                                             x1
  QuickTime  GetMovieActiveSegment                                        x1
  QuickTime  GetMovieBox                                                  x11
  QuickTime  GetMovieDataSize                                             x1
  QuickTime  GetMovieDuration                                             x7
  QuickTime  GetMovieGWorld                                               x1
  QuickTime  GetMovieIndTrack                                             x14
  QuickTime  GetMovieIndTrackType                                         x3
  QuickTime  GetMovieMatrix                                               x3
  QuickTime  GetMovieNextInterestingTime                                  x1
  QuickTime  GetMoviePict                                                 x1
  QuickTime  GetMoviePosterPict                                           x1
  QuickTime  GetMoviePosterTime                                           x1
  QuickTime  GetMoviePreferredRate                                        x1
  QuickTime  GetMoviePreferredVolume                                      x1
  QuickTime  GetMovieSelection                                            x1
  QuickTime  GetMovieTime                                                 x7
  QuickTime  GetMovieTimeBase                                             x1
  QuickTime  GetMovieTimeScale                                            x2
  QuickTime  GetMovieTrack                                                x1
  QuickTime  GetMovieTrackCount                                           x8
  QuickTime  GetMovieUserData                                             x2
  QuickTime  GetMovieVolume                                               x2
  QuickTime  GetMoviesError                                               x20
  QuickTime  GetMoviesStickyError                                         x2
  QuickTime  GetTrackAlternate                                            x2
  QuickTime  GetTrackEnabled                                              x6
  QuickTime  GetTrackID                                                   x1
  QuickTime  GetTrackMedia                                                x4
  QuickTime  GetTrackMovie                                                x2
  QuickTime  GetTrackReference                                            x1
  QuickTime  GetTrackUserData                                             x1
  QuickTime  GetUserData                                                  x6
  QuickTime  GoToBeginningOfMovie                                         x1
  QuickTime  GoToEndOfMovie                                               x1
  QuickTime  HasMovieChanged                                              x2
  QuickTime  InsertMovieSegment                                           x1
  QuickTime  IsMovieDone                                                  x1
  QuickTime  MovieSearchText                                              x1
  QuickTime  MoviesTask                                                   x8
  QuickTime  NewMovie                                                     x1
  QuickTime  NewMovieController                                           x1
  QuickTime  NewMovieEditState                                            x2
  QuickTime  NewMovieFromFile                                             x2
  QuickTime  NewMovieFromHandle                                           x1
  QuickTime  OpenMovieFile                                                x4
  QuickTime  PrerollMovie                                                 x1
  QuickTime  PutMovieIntoHandle                                           x2
  QuickTime  PutMovieOnScrap                                              x2
  QuickTime  QTFindChildByID                                              x6
  QuickTime  QTGetAtomDataPtr                                             x2
  QuickTime  QTInsertChild                                                x3
  QuickTime  QTSetAtomData                                                x2
  QuickTime  RemoveUserData                                               x5
  QuickTime  SetMovieActive                                               x1
  QuickTime  SetMovieActiveSegment                                        x2
  QuickTime  SetMovieBox                                                  x11
  QuickTime  SetMovieGWorld                                               x4
  QuickTime  SetMovieLanguage                                             x1
  QuickTime  SetMovieMatrix                                               x3
  QuickTime  SetMoviePosterTime                                           x1
  QuickTime  SetMoviePreferredVolume                                      x1
  QuickTime  SetMovieProgressProc                                         x3
  QuickTime  SetMovieRate                                                 x2
  QuickTime  SetMovieTime                                                 x1
  QuickTime  SetMovieTimeValue                                            x2
  QuickTime  SetMovieVolume                                               x1
  QuickTime  SetTrackEnabled                                              x3
  QuickTime  ShowMovieInformation                                         x1
  QuickTime  UpdateMovieResource                                          x1
  QuickTime  UseMovieEditState                                            x2
```

```
=== Macintosh HD:Applications:QuickTime:MoviePlayer
68k trap glue (419 sites):
  Component  what=1 p=12 (TweenerInitialize [QuickTimeComponents] | PreviewShowData [QuickTimeComponents] | InstrumentGetInst [QuickTimeMusic]) x1
  Component  what=10 p=4 (ClockGetRate [QuickTimeComponents] | DataHSetDataRef [QuickTimeComponents] | QTVideoOutputSaveState [QuickTimeComponents]) x1
  Component  what=11 p=12 (MovieImportDoUserDialog [QuickTimeComponents] | VDGetInputGammaValue [QuickTimeComponents] | ImageCodecRequestSettings [ImageCodec]) x1
  Component  what=11 p=4 (DataHGetDataRef [QuickTimeComponents] | QTVideoOutputRestoreState [QuickTimeComponents] | GraphicsImportGetClip [ImageCompression]) x1
  Component  what=12 p=4 (MCSetControllerPort [Movies] | MovieImportSetDuration [QuickTimeComponents] | VDSetBrightness [QuickTimeComponents]) x3
  Component  what=13 p=0 (MCGetControllerPort [Movies] | DataHTask [QuickTimeComponents]) x1
  Component  what=13 p=4 (SCCompressPictureFile [QuickTimeComponents] | MovieImportGetAuxiliaryDataType [QuickTimeComponents] | VDGetBrightness [QuickTimeComponents]) x2
  Component  what=1306 p=8                                                x1
  Component  what=1309 p=12                                               x1
  Component  what=14 p=2 (MCSetVisible [Movies])                          x2
  Component  what=16 p=4 (MCGetControllerBoundsRect [Movies] | VDSetSharpness [QuickTimeComponents] | XMLParseSetStartElementHandler [QuickTimeComponents]) x2
  Component  what=2 p=12 (MCSetMovie [Movies] | XMLParseFile [QuickTimeComponents] | MusicGetPart [QuickTimeMusic]) x1
  Component  what=2 p=32 (MovieImportFile [QuickTimeComponents])          x1
  Component  what=2 p=4 (TweenerDoTween [QuickTimeComponents] | QTVideoOutputGetCurrentClientName [QuickTimeComponents] | GraphicsExportCanTranscode [ImageCompression]) x2
  Component  what=20 p=4 (MCMovieChanged [Movies] | MovieImportGetSettingsAsAtomContainer [QuickTimeComponents] | DataHGetVolumeList [QuickTimeComponents]) x10
  Component  what=24 p=4 (MCDraw [Movies] | SCSetCompressFlags [QuickTimeComponents] | DataHGetFreeSpace [QuickTimeComponents]) x3
  Component  what=25 p=6 (MCActivate [Movies] | DataHCreateFile [QuickTimeComponents]) x1
  Component  what=256 p=2 (GraphicsImageImportSetSequenceEnabled [QuickTimeComponents]) x1
  Component  what=26 p=0 (MCIdle [Movies] | NALoseDefaultMIDIInput [QuickTimeMusic]) x1
  Component  what=29 p=2 (MCEnableEditing [Movies])                       x1
  Component  what=3 p=6 (SCPositionDialog [QuickTimeComponents])          x1
  Component  what=31 p=0 (MCCopy [Movies] | MusicGetMasterTune [QuickTimeMusic]) x1
  Component  what=32 p=0 (MCCut [Movies])                                 x1
  Component  what=33 p=4 (MCPaste [Movies] | GraphicsImportGetMIMETypeList [ImageCompression] | GraphicsExportSetColorSyncProfile [ImageCompression]) x1
  Component  what=34 p=0 (MCClear [Movies])                               x1
  Component  what=35 p=0 (MCUndo [Movies])                                x1
  Component  what=36 p=12 (MCPositionController [Movies] | ImageCodecValidateParameters [ImageCodec]) x1
  Component  what=37 p=4 (MCGetControllerInfo [Movies] | DataHGetFileName [QuickTimeComponents] | VDGetClipState [QuickTimeComponents]) x10
  Component  what=4 p=18                                                  x2
  Component  what=43 p=8 (MCSetUpEditMenu [Movies] | MusicGetInstrumentKnobDescription [QuickTimeMusic] | NASetInstrumentNumberInterruptSafe [QuickTimeMusic]) x1
  Component  what=45 p=8 (MCSetActionFilterWithRefCon [Movies] | SGGetMode [QuickTimeComponents] | MusicGetKnobDescription [QuickTimeMusic]) x2
  Component  what=5 p=8 (SCSetTestImagePictFile [QuickTimeComponents] | SGGetGWorld [QuickTimeComponents] | GraphicsExportDoUseCompressor [ImageCompression]) x1
  Component  what=6 p=0 (MCRemoveAllMovies [Movies] | MCRemoveMovie [Movies] | DataHCloseForWrite [QuickTimeComponents]) x1
  Component  what=6 p=6                                                   x1
  Component  what=7 p=4 (MCIsPlayerEvent [Movies] | ClockTimeChanged [QuickTimeComponents] | SCGetBestDeviceRect [QuickTimeComponents]) x2
  Component  what=8 p=8 (MovieImportSetProgressProc [QuickTimeComponents] | GraphicsImportGetDataOffsetAndSize [ImageCompression] | TuneGetIndexedNoteChannel [QuickTimeMusic]) x2
  Component  what=9 p=4 (VDUseThisCLUT [QuickTimeComponents] | QTVideoOutputCustomConfigureDisplay [QuickTimeComponents] | GraphicsExportGetMIMETypeList [ImageCompression]) x1
  Component  what=9 p=6 (MCDoAction [Movies])                             x43
  ICM        AlignScreenRect                                              x1
  ICM        CustomGetFilePreview                                         x2
  ICM        DragAlignedWindow                                            x1
  ICM        DrawTrimmedPicture                                           x1
  ICM        GDGetScale                                                   x1
  ICM        GDSetScale                                                   x2
  ICM        GetBestDeviceRect                                            x1
  ICM        MakeFilePreview                                              x2
  ICM        QTNewGWorld                                                  x2
  ICM        SFGetFilePreview                                             x1
  ICM        StdPix                                                       x1
  QuickTime  #273                                                         x2
  QuickTime  #274                                                         x2
  QuickTime  #2E2                                                         x1
  QuickTime  AddMovieResource                                             x1
  QuickTime  AddUserData                                                  x6
  QuickTime  BeginFullScreen                                              x2
  QuickTime  CheckQuickTimeRegistration                                   x2
  QuickTime  ClearMovieChanged                                            x1
  QuickTime  ClearMoviesStickyError                                       x2
  QuickTime  CloseMovieFile                                               x4
  QuickTime  ConvertMovieToFile                                           x1
  QuickTime  CopyMovieSettings                                            x1
  QuickTime  CountUserDataType                                            x5
  QuickTime  CreateMovieFile                                              x1
  QuickTime  DeleteMovieFile                                              x2
  QuickTime  DisposeMovie                                                 x11
  QuickTime  DisposeMovieController                                       x5
  QuickTime  DisposeMovieEditState                                        x2
  QuickTime  DisposeMovieTrack                                            x3
  QuickTime  EndFullScreen                                                x1
  QuickTime  EnterMovies                                                  x1
  QuickTime  FlattenMovieData                                             x1
  QuickTime  GetMediaHandler                                              x2
  QuickTime  GetMediaHandlerDescription                                   x3
  QuickTime  GetMediaLanguage                                             x1
  QuickTime  GetMovieActiveSegment                                        x1
  QuickTime  GetMovieBox                                                  x10
  QuickTime  GetMovieDataSize                                             x1
  QuickTime  GetMovieDuration                                             x7
  QuickTime  GetMovieGWorld                                               x1
  QuickTime  GetMovieIndTrack                                             x14
  QuickTime  GetMovieIndTrackType                                         x3
  QuickTime  GetMovieMatrix                                               x2
  QuickTime  GetMovieNaturalBoundsRect                                    x8
  QuickTime  GetMovieNextInterestingTime                                  x1
  QuickTime  GetMoviePict                                                 x2
  QuickTime  GetMoviePosterPict                                           x1
  QuickTime  GetMoviePosterTime                                           x1
  QuickTime  GetMoviePreferredRate                                        x1
  QuickTime  GetMoviePreferredVolume                                      x1
  QuickTime  GetMovieRate                                                 x1
  QuickTime  GetMovieSelection                                            x1
  QuickTime  GetMovieStatus                                               x2
  QuickTime  GetMovieTime                                                 x8
  QuickTime  GetMovieTimeBase                                             x1
  QuickTime  GetMovieTimeScale                                            x3
  QuickTime  GetMovieTrack                                                x1
  QuickTime  GetMovieTrackCount                                           x8
  QuickTime  GetMovieUserData                                             x4
  QuickTime  GetMovieVolume                                               x2
  QuickTime  GetMoviesError                                               x17
  QuickTime  GetMoviesStickyError                                         x2
  QuickTime  GetQuickTimePreference                                       x1
  QuickTime  GetTrackAlternate                                            x2
  QuickTime  GetTrackEnabled                                              x6
  QuickTime  GetTrackID                                                   x1
  QuickTime  GetTrackMedia                                                x5
  QuickTime  GetTrackMovie                                                x2
  QuickTime  GetTrackReference                                            x1
  QuickTime  GetTrackUserData                                             x1
  QuickTime  GetUserData                                                  x6
  QuickTime  GoToBeginningOfMovie                                         x1
  QuickTime  GoToEndOfMovie                                               x1
  QuickTime  HasMovieChanged                                              x2
  QuickTime  InsertMovieSegment                                           x1
  QuickTime  IsMovieDone                                                  x1
  QuickTime  MovieSearchText                                              x1
  QuickTime  MoviesTask                                                   x9
  QuickTime  NewMovie                                                     x2
  QuickTime  NewMovieController                                           x1
  QuickTime  NewMovieEditState                                            x2
  QuickTime  NewMovieFromDataRef                                          x1
  QuickTime  NewMovieFromFile                                             x2
  QuickTime  NewMovieFromHandle                                           x1
  QuickTime  OpenMovieFile                                                x4
  QuickTime  PrerollMovie                                                 x1
  QuickTime  PutMovieIntoHandle                                           x2
  QuickTime  PutMovieOnScrap                                              x2
  QuickTime  QTDisposeAtomContainer                                       x1
  QuickTime  QTFindChildByID                                              x9
  QuickTime  QTGetAtomDataPtr                                             x4
  QuickTime  QTInsertChild                                                x3
  QuickTime  QTSetAtomData                                                x2
  QuickTime  RemoveUserData                                               x6
  QuickTime  SetMovieActive                                               x1
  QuickTime  SetMovieActiveSegment                                        x2
  QuickTime  SetMovieBox                                                  x11
  QuickTime  SetMovieGWorld                                               x4
  QuickTime  SetMovieLanguage                                             x1
  QuickTime  SetMovieMatrix                                               x3
  QuickTime  SetMoviePlayHints                                            x1
  QuickTime  SetMoviePosterTime                                           x1
  QuickTime  SetMoviePreferredVolume                                      x1
  QuickTime  SetMovieProgressProc                                         x3
  QuickTime  SetMovieRate                                                 x2
  QuickTime  SetMovieTimeScale                                            x1
  QuickTime  SetMovieTimeValue                                            x2
  QuickTime  SetMovieVolume                                               x3
  QuickTime  SetTrackEnabled                                              x3
  QuickTime  ShowMovieInformation                                         x1
  QuickTime  UpdateMovieResource                                          x1
  QuickTime  UseMovieEditState                                            x2
```

```
=== Macintosh HD:SimpleText
68k trap glue (1 sites):
  QuickTime  ? (selector not loaded just before)                          x1
```

```
=== Macintosh HD:Applications:SimpleText
68k trap glue (25 sites):
  Component  what=16 p=4 (MCGetControllerBoundsRect [Movies] | VDSetSharpness [QuickTimeComponents] | XMLParseSetStartElementHandler [QuickTimeComponents]) x1
  Component  what=7 p=4 (MCIsPlayerEvent [Movies] | ClockTimeChanged [QuickTimeComponents] | SCGetBestDeviceRect [QuickTimeComponents]) x1
  Component  what=9 p=6 (MCDoAction [Movies])                             x1
  ICM        AddImageDescriptionExtension                                 x1
  ICM        AlignWindow                                                  x1
  ICM        CustomGetFilePreview                                         x1
  ICM        DecompressImage                                              x1
  ICM        DragAlignedWindow                                            x1
  ICM        DrawPictureFile                                              x1
  QuickTime  CloseMovieFile                                               x1
  QuickTime  DisposeMovie                                                 x2
  QuickTime  DisposeMovieController                                       x1
  QuickTime  EnterMovies                                                  x1
  QuickTime  ExitMovies                                                   x1
  QuickTime  GetMovieBox                                                  x1
  QuickTime  GetMovieIndTrackType                                         x1
  QuickTime  GetMoviesError                                               x1
  QuickTime  IsMovieDone                                                  x1
  QuickTime  MovieSearchText                                              x1
  QuickTime  NewMovieController                                           x1
  QuickTime  NewMovieFromFile                                             x1
  QuickTime  OpenMovieFile                                                x1
  QuickTime  SetMovieBox                                                  x1
  QuickTime  SetMovieGWorld                                               x1
```

```
=== Development:Languages ƒ:HyperCard 2.4.1 ƒ:HyperCard
68k trap glue (86 sites):
  Component  what=-4 p=0                                                  x1
  Component  what=0 p=12 (NARegisterMusicDevice [QuickTimeMusic])         x1
  Component  what=0 p=24                                                  x1
  Component  what=0 p=4 (ImageCodecGetCodecInfo [ImageCodec])             x1
  Component  what=1 p=0 (SGInitialize [QuickTimeComponents])              x1
  Component  what=1 p=12 (TweenerInitialize [QuickTimeComponents] | PreviewShowData [QuickTimeComponents] | InstrumentGetInst [QuickTimeMusic]) x2
  Component  what=1 p=4 (ClockGetTime [QuickTimeComponents] | QTVideoOutputGetDisplayModeList [QuickTimeComponents] | GraphicsExportDoExport [ImageCompression]) x1
  Component  what=10 p=0 (SCRequestImageSettings [QuickTimeComponents])   x1
  Component  what=1025 p=8                                                x1
  Component  what=11 p=4 (DataHGetDataRef [QuickTimeComponents] | QTVideoOutputRestoreState [QuickTimeComponents] | GraphicsImportGetClip [ImageCompression]) x1
  Component  what=16 p=24                                                 x1
  Component  what=17 p=20 (ImageCodecNewMemory [ImageCodec])              x1
  Component  what=2 p=12 (MCSetMovie [Movies] | XMLParseFile [QuickTimeComponents] | MusicGetPart [QuickTimeMusic]) x1
  Component  what=2 p=16 (PreviewMakePreview [QuickTimeComponents] | DataHGetData [QuickTimeComponents] | ImageTranscoderConvert [ImageCompression]) x3
  Component  what=2 p=6 (ClockNewCallBack [QuickTimeComponents] | VDGetActiveSrcRect [QuickTimeComponents]) x1
  Component  what=2051 p=16                                               x1
  Component  what=259 p=12 (SpriteMediaHitTestSprites [Movies] | TCTimeCodeToString [QuickTimeComponents] | CurveCountPointsInPath [ImageCodec]) x3
  Component  what=3 p=0 (TweenerReset [QuickTimeComponents] | GraphicsExportDoTranscode [ImageCompression] | NASaveMusicConfiguration [QuickTimeMusic]) x2
  Component  what=3 p=16 (DataHPutData [QuickTimeComponents])             x3
  Component  what=3 p=4 (MCRemoveAMovie [Movies] | ClockDisposeCallBack [QuickTimeComponents] | VDSetDigitizerRect [QuickTimeComponents]) x1
  Component  what=30 p=10                                                 x1
  Component  what=4 p=0 (DataHFlushData [QuickTimeComponents] | ImageTranscoderEndSequence [ImageCompression]) x1
  Component  what=4 p=16 (ClockCallMeWhen [QuickTimeComponents])          x1
  Component  what=4097 p=28                                               x1
  Component  what=4356 p=12                                               x1
  Component  what=5 p=12                                                  x4
  Component  what=5 p=2 (MCGetIndMovie [Movies] | InstrumentCloseComponentResFile [QuickTimeMusic]) x1
  Component  what=5 p=8 (SCSetTestImagePictFile [QuickTimeComponents] | SGGetGWorld [QuickTimeComponents] | GraphicsExportDoUseCompressor [ImageCompression]) x1
  Component  what=513 p=12                                                x3
  Component  what=6 p=16 (XMLParseAddAttribute [QuickTimeComponents])     x1
  Component  what=6 p=4 (ClockRateChanged [QuickTimeComponents] | VDGetMaskPixMap [QuickTimeComponents] | GraphicsImportGetDataHandle [ImageCompression]) x9
  Component  what=770 p=16                                                x2
  Component  what=8 p=12                                                  x2
  Component  what=9 p=8 (ClockStartStopChanged [QuickTimeComponents] | MovieImportSetAuxiliaryData [QuickTimeComponents]) x1
  ICM        AddFilePreview                                               x2
  ICM        CustomGetFilePreview                                         x2
  ICM        MakeThumbnailFromPicture                                     x1
  QuickTime  ? (selector not loaded just before)                          x23
  QuickTime  CheckQuickTimeRegistration                                   x1
```

```
=== Creative:Deneba Canvas 5.0.2 ƒ:Canvas™ 5
68k trap glue (7 sites):
  Component  what=1 p=12 (TweenerInitialize [QuickTimeComponents] | PreviewShowData [QuickTimeComponents] | InstrumentGetInst [QuickTimeMusic]) x1
  ICM        ? (selector not loaded just before)                          x1
  ICM        CustomGetFilePreview                                         x1
  ICM        DecompressImage                                              x2
  QuickTime  ? (selector not loaded just before)                          x2
```
