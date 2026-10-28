# Icon Services: behavioural specification

**Settled since** (2026-10-28) by IconTest (tests/guest/iconservices) on
Apple's Icon Services in Mac OS 9.0.4 on the G3: an unknown creator/type
gets a valid (generic) icon, no error; system icons are shared (others,
the Finder, hold them too); getting a ref again or registering a
creator/type again is the same ref and one more owner; the first
registration has one owner; UnregisterIconRef invalidates the ref at once
(owners 0); a composite holds one owner of each part; a plain ref asked
IsIconRefComposite answers noErr and two nils; GetIconFamilyData of a
missing element is paramErr; ReleaseIconRef(nil) is invalidIconRefErr;
a folder's icon from its FSSpec isn't the generic folder ref. What
Executor does: the comment at the top of src/executor/romlib/iconservices.cpp.

A specification of Apple's Icon Services (Mac OS 8.5 to 9; `IconRef`,
`IconServicesLib`, the 68k trap `_IconServicesDispatch` $AA75), written from
Apple's published documentation only, for a from-scratch implementation in
Executor. Clean room: no Apple source code or disassembly was used. Sources
(section 5): the header comments of `Icons.h` (Universal Interfaces 3.4,
"Technology: Mac OS 8.5"), `Icons.r`, `MacErrors.h`, `Finder.h`,
`Folders.h`, `DriverGestalt.h`, `Gestalt.h` from the same CIncludes; the
Carbon *Icon Services and Utilities Reference* (2003 and 2007 editions);
*Obtaining and Using Icons With Icon Services* (1999/2003); Technical Note
TN1142 "Mac OS 8.5"; *Inside Macintosh: More Macintosh Toolbox* (Icon
Utilities) for the shared plotting rules; and, marked as such, one
third-party article (MacTech 15.05, 1999) where it reports observed
behaviour.

The documentation that went with 8.5, *Inside Macintosh: Icon Services*
(preliminary draft, 6/17/98, `developer.apple.com/macos/pdf/IconSvcs.pdf`),
is not in the Wayback Machine (404). The Carbon reference is its successor
and is what is quoted here. It describes CarbonLib and Mac OS X too; only
what applies to the classic `IconServicesLib` 8.5/9.0 is kept, and anything
the Carbon text says only about Mac OS X is labelled.

Quotes are exact and short. Anything the documentation leaves open is
marked **UNSPECIFIED**: settle it with a test program on Mac OS 9.0.4 (the
G3 machine, `tests-9.0.4`), as CollectionTest did for the Collection
Manager.

Abbreviations for where a quote comes from: **[H]** `Icons.h` comment,
**[R03]** Carbon reference, February 2003 edition, **[R07]** Carbon
reference, 2007-04-06 edition (and its deprecation appendix), **[G]** the
*Obtaining and Using Icons* guide, **[TN]** TN1142, **[IM]** *More
Macintosh Toolbox*, **[MT]** MacTech article (third-party; observed
behaviour, not a specification).

---

## 0. Availability and calling conventions

- Gestalt `gestaltIconUtilitiesAttr` = `'icon'`; bit
  `gestaltIconUtilitiesHasIconServices` = 4 "true if IconServices is
  present"; also bit 1 `gestaltIconUtilitiesHas48PixelIcons`, bit 2
  `gestaltIconUtilitiesHas32BitIcons`, bit 3
  `gestaltIconUtilitiesHas8BitDeepMasks`, bit 0
  `gestaltIconUtilitiesPresent` (`Gestalt.h`).
- PowerPC: the routines are exports of `IconServicesLib` ("in
  IconServicesLib 8.5 and later"; `GetIconRefVariant`,
  `RegisterIconRefFromIconFile`, `ReadIconFile`, `WriteIconFile`: "in
  IconServicesLib 9.0 and later") [H].
- 68k: `TWOWORDINLINE(0x70nn, 0xAA75)`, i.e. `moveq #nn,D0` then trap $AA75,
  Pascal calling convention, result `OSErr` (or `Boolean`/`IconRef`) on the
  stack [H]. Selectors, from the header:

| Sel | Routine | Sel | Routine |
|---|---|---|---|
| $02 | GetIconRefFromFile | $1C | RegisterIconRefFromIconFamily |
| $08 | UnregisterIconRef | $1E | RemoveIconRefOverride |
| $09 | UpdateIconRef | $1F | SetCustomIconsEnabled |
| $0B | GetIconRefOwners | $20 | GetCustomIconsEnabled |
| $0E | PlotIconRef | $21 | GetIconRef |
| $0F | PtInIconRef | $22 | GetIconRefFromFolder |
| $10 | RectInIconRef | $23 | RegisterIconRefFromResource |
| $11 | IconRefToRgn | $24 | IconRefToIconFamily |
| $12 | GetIconSizesFromIconRef | $25 | IconFamilyToIconSuite |
| $14 | CompositeIconRef | $26 | IconSuiteToIconFamily |
| $15 | IconServicesInit | $27 | AcquireIconRef |
| $16 | IconServicesTerminate | $28 | ReleaseIconRef |
| $18 | FlushIconRefsByVolume | $29 | FlushIconRefs |
| $1A | IsIconRefComposite | $2A | OverrideIconRefFromResource |
|     |  | $2B | OverrideIconRef |
| $30 | SetIconFamilyData | $33 | IsIconRefMaskEmpty |
| $31 | GetIconFamilyData | $34 | GetIconRefVariant |
| $32 | IsValidIconRef | $35 | RegisterIconRefFromIconFile |
|     |  | $36 | ReadIconFile |
|     |  | $37 | WriteIconFile |

  What $AA75 does with a selector not in the table: **UNSPECIFIED**.
- `IconServicesInit(CFragInitBlockPtr)` ($15) / `IconServicesTerminate()`
  ($16): "Call this routine once per classic 68K application
  initialization. This routine does not need to be called at boot time."
  [H]. [R03] (Unsupported Functions): "It is not necessary to call this
  function on any PowerPC version of the Mac OS, because the initialization
  and cleanup of Icon Services is done automatically by the operating
  system." What a 68k client that never calls `IconServicesInit` gets
  (works anyway, or errors), and whether `IconServicesTerminate` releases
  the IconRefs that client still owns: **UNSPECIFIED**.
- TN1142: "Icon Utilities is now implemented in PowerPC code. (Icon
  Services is also native.)"
- The Mac OS 8.6/9 Finder also imports `IconServicesLib` routines that no
  public header declares (`GetIconRefByName`, `SetIconDevice`; see
  MacOS8-9.md). They are outside this specification.

---

## 1. Concepts

### 1.1 The IconRef

- `typedef struct OpaqueIconRef* IconRef;` "IconRefs are 32-bit values
  identifying cached icon data. IconRef 0 is invalid." [H]
- "IconServices is an efficient mechanism to share icon data amongst
  multiple clients. It avoids duplication of data; it provides efficient
  caching, releasing memory when the icon data is no longer needed; it can
  provide the appropriate icon for any filesystem object; it can provide
  commonly used icons (caution, note, help...); it is Appearance-savvy: the
  icons are switched when appropriate." [H]
- "Two files of same type and creator with no custom icon will have the
  same IconRef. Files with custom icons will have their own IconRef." [H]
  So an IconRef is identified by (creator, type) for type-based icons and by
  file for custom icons; asking twice for the same thing returns the same
  IconRef value with its owner count raised.
- TN1142: "Icon Services maintains a system-wide icon cache shared by all
  applications. Icon data returned is common to all applications, so it is
  possible for two different applications to obtain an icon reference that
  refers to the same icon data." The cache and IconRefs are therefore
  system-wide (system heap or equivalent), not per application.
- Appearance: "Icon Services is Appearance Manager-aware, and will return
  information consistent with the active appearance as appropriate." [TN]
  [MT] (observed): "the cache is automatically updated when the user
  switches to a different theme or scheme." How the theme's icons replace
  the System's (which resources, which file): **UNSPECIFIED**.

### 1.2 Reference counting (owners)

- "IconRefs are reference counted. When there are no more "owners" of an
  IconRef, the memory used by the icon bitmap is disposed of." [H]
- "A valid IconRef always has at least one owner." (`GetIconRefOwners`) [H]
- Every routine that hands out an IconRef "increments the reference count
  of the returned IconRef. Call ReleaseIconRef() when you're done with it."
  [H] (`GetIconRef`, `GetIconRefFromFile`, `GetIconRefFromFolder`,
  `RegisterIconRefFromIconFamily`, `RegisterIconRefFromResource`,
  `RegisterIconRefFromIconFile`). `CompositeIconRef`: whether the
  composite comes with one owner for the caller is not stated; by the same
  rule it should. **UNSPECIFIED**, test it.
- `AcquireIconRef` adds an owner; `ReleaseIconRef` removes one.
- At zero, two wordings: [H] "When the reference count reaches 0, all
  memory allocated for the icon is disposed. Any subsequent use of the
  IconRef is invalid." [R03] "When an IconRef's reference count reaches 0,
  all memory allocated for the IconRef is marked as disposable. Any
  subsequent attempt to use the IconRef returns a result code of -2580
  (invalidIconRefErr)." [TN]: "icons are removed from the cache only if the
  reference count falls to zero."
  Taken together: at zero the IconRef stops being valid (calls taking it
  answer `invalidIconRefErr`, `IsValidIconRef` false) and its storage may
  be reclaimed. Whether the same 32-bit value can come back later for a
  different icon, and whether a registered icon whose count reaches zero
  stays registered (see 1.3): **UNSPECIFIED**.
- What `ReleaseIconRef` on an already invalid IconRef returns
  (`invalidIconRefErr` presumably): **UNSPECIFIED**.
- The owner count is a `UInt16` (`GetIconRefOwners`). Behaviour at 65535:
  **UNSPECIFIED**.
- Other managers own IconRefs too: `gestaltMenuMgrRetainsIconRefBit` (3)
  "SetMenuItemIconHandle, when passed an IconRef, calls AcquireIconRef"
  (`Gestalt.h`, a later system than 8.5).

### 1.3 The registry

- `RegisterIconRefFromIconFamily`: "This routine adds a new entry to the
  IconRef registry. Other clients will be able to access it using the
  (creator, iconType) pair specified here. Lower-case creators are
  reserved for the system." [H] Also "Note that all lowercase creators are
  reserved by Apple." [H]
- Three ways to register: from an `IconFamilyHandle` (data copied, "cannot
  be purged" [H]); from a resource file + ID (`'icns'` first, then the
  classic family; purgeable, re-read on demand); from a `.icns` file
  (9.0). [G]: "The preferred way of registering icons is to use the
  function RegisterIconRefFromIconFile. This will make the icon data
  purgeable if you need to free up memory later."
- Registering a (creator, type) twice: documented only for
  `RegisterIconRefFromIconFile` [H]: "If the creator is kSystemIconsCreator
  and the iconType is 0, a new IconRef is always returned. Otherwise, if
  the creator and type have already been registered, the previously
  registered IconRef is returned." For `RegisterIconRefFromIconFamily` and
  `RegisterIconRefFromResource` the same rule is plausible but
  **UNSPECIFIED** (does the second call return the first IconRef with one
  more owner and ignore the new data, or replace the data?). Also
  **UNSPECIFIED**: whether `noErr` is returned in the "already registered"
  case.
- `UnregisterIconRef(creator, type)`: "Removes the specified icon from the
  icon cache (if there are no users of it). If some clients are using this
  iconRef, then the IconRef will be removed when the last user calls
  ReleaseIconRef." [H] "You should not unregister an icon that you have
  not registered." [R03] After unregistering, does `GetIconRef(creator,
  type)` fail at once (while owners remain) or still return the old
  IconRef? **UNSPECIFIED**. Unregistering a pair never registered: result
  **UNSPECIFIED** (`noSuchIconErr`?).
- Registered icons shadow the desktop database: [MT] (observed) "The
  registered icons will override the desktop database entries." and
  "the type and creator are the primary indexing method for the icon
  cache." Not stated by Apple; consistent with the `GetIconRef` text
  (1.5). Test it.
- The registered count's relation to the registration itself: the
  registering call gives the caller one owner. Whether the registry holds
  an owner of its own (so the icon survives the caller's
  `ReleaseIconRef`) is **UNSPECIFIED**; the [MT] sample registers,
  plots, releases, then unregisters, which works either way. This is the
  most important open question for the Finder (which registers its own
  icons with `RegisterIconRefFromIconFamily`, MacOS8-9.md) and must be
  tested: register, release, then `GetIconRef` the same pair.

### 1.4 System icons

- "Use the special creator kSystemIconsCreator to get "standard" icons
  that are not associated with a file, such as the help icon." [H]
  `kSystemIconsCreator` = `'macs'` (0x6D616373).
- "The system registers a set of icon such as the help icon with the
  creator code kSystemIconsCreator." [H] Example [H] and [G]:
  `GetIconRef(kOnSystemDisk, kSystemIconsCreator, kHelpIcon, &iconRef)`.
  `kOnSystemDisk` = -32768 (`Folders.h`).
- The full list of types (`Icons.h`, UI 3.4) is in section 1.4.1. The
  2007 reference adds later ones that are not in UI 3.4 and are not part
  of 8.5–9: `kGenericURLIcon` 'gurl', `kUnknownFSObjectIcon` 'unfs',
  `kPublicFolderIcon` 'pubf', `kAlertCautionBadgeIcon` 'cbdg',
  `kGenericWindowIcon` 'gwin', `kQuestionMarkIcon` 'ques',
  `kDeleteAliasIcon` 'dali', `kEjectMediaIcon` 'ejec', `kBurningIcon`
  'burn', `kRightContainerArrowIcon` 'rcar', `kToolbar*Icon` 'tcus'
  'tdel' 'tfav' 'thom'. UI 3.4 (2001) postdates 9.0, so some of its types
  (e.g. `'idsk'`, `'udsk'`, `'ilns'`) may be later than 8.5–9.0.4. Which
  subset Mac OS 8.5, 8.6, 9.0.4 actually register: **UNSPECIFIED**;
  enumerate on 9.0.4 with `GetIconRef` per type.
- Note `kSystemFolderIcon` is `'macs'` too, i.e. (`'macs'`, `'macs'`).
- Many of the system types equal the file type of the object they
  represent (`'APPL'`, `'APPC'`, `'APPD'`, `'INIT'`, `'FFIL'`, `'ffil'`,
  `'tfil'`, `'sfnt'`, `'sfil'`, `'kfil'`, `'ifil'`, `'zsys'`, `'thng'`,
  `'shlb'`, `'pref'`, `'qery'`, `'edtf'`, `'CLIP'`, `'clpp'`...). This is
  what lets `GetIconRef(vRefNum, creator, 'APPL')` fall back to the generic
  application icon (1.5). That fallback is inferred, not stated:
  **UNSPECIFIED** precisely (see 1.5).
- Classic resource IDs (`Icons.h`, "Avoid using icon resources if
  possible. Use IconServices instead."). These are the System file's
  icon families the classic Finder used; whether Icon Services draws its
  `'macs'` icons from these IDs, from `'icns'` resources of the same IDs,
  or from elsewhere (the Appearance theme): **UNSPECIFIED** (dump the 9.0.4
  System file's `'icns'` IDs and compare against `IconRefToIconFamily` of
  each system type).

| Constant | ID | Has |
|---|---|---|
| `kGenericDocumentIconResource` | -4000 | suite + SICN |
| `kGenericStationeryIconResource` | -3985 | suite + SICN |
| `kGenericEditionFileIconResource` | -3989 | suite + SICN |
| `kGenericApplicationIconResource` | -3996 | suite + SICN |
| `kGenericDeskAccessoryIconResource` | -3991 | suite + SICN |
| `kGenericFolderIconResource` | -3999 | suite + SICN |
| `kPrivateFolderIconResource` | -3994 | suite + SICN |
| `kFloppyIconResource` | -3998 | suite + SICN |
| `kTrashIconResource` | -3993 | suite + SICN |
| `kGenericRAMDiskIconResource` | -3988 | suite + SICN |
| `kGenericCDROMIconResource` | -3987 | suite + SICN |
| `kDesktopIconResource` | -3992 | SICN only |
| `kOpenFolderIconResource` | -3997 | SICN only |
| `kGenericHardDiskIconResource` | -3995 | SICN only |
| `kGenericFileServerIconResource` | -3972 | SICN only |
| `kGenericSuitcaseIconResource` | -3970 | SICN only |
| `kGenericMoverObjectIconResource` | -3969 | SICN only |
| `kGenericPreferencesIconResource` | -3971 | suite only |
| `kGenericQueryDocumentIconResource` | -16506 | suite only |
| `kGenericExtensionIconResource` | -16415 | suite only |
| `kSystemFolderIconResource` | -3983 | suite only |
| `kHelpIconResource` | -20271 | suite only |
| `kAppleMenuFolderIconResource` | -3982 | suite only |
| `kStartupFolderIconResource` | -3981 | (not said) |
| `kOwnedFolderIconResource` | -3980 | |
| `kDropFolderIconResource` | -3979 | |
| `kSharedFolderIconResource` | -3978 | |
| `kMountedFolderIconResource` | -3977 | |
| `kControlPanelFolderIconResource` | -3976 | |
| `kPrintMonitorFolderIconResource` | -3975 | |
| `kPreferencesFolderIconResource` | -3974 | |
| `kExtensionsFolderIconResource` | -3973 | |
| `kFontsFolderIconResource` | -3968 | |
| `kFullTrashIconResource` | -3984 | |

("suite" = an icon family, `'ICN#'`/`'icl4'`/`'icl8'`/`'ics#'`/... of
that ID; "SICN" = a `'SICN'` resource of that ID; the header's groups:
"both icon suites and SICNs", "SICNs only", "icon suites only"; the last
group has no comment.) Other IDs: `kCustomIconResource` = -16455 "Custom
icon family resource ID" (`Finder.h`); the alert icons are `kStopIcon` 0,
`kNoteIcon` 1, `kCautionIcon` 2 (`Dialogs.h`), not in `Icons.h`.

#### 1.4.1 The system icon types (`Icons.h`, UI 3.4)

Hex is the big-endian OSType; `ƒ` is MacRoman $C4.

**Generic Finder icons**

| Constant | Type | Hex |
|---|---|---|
| `kClipboardIcon` | `'CLIP'` | 0x434C4950 |
| `kClippingUnknownTypeIcon` | `'clpu'` | 0x636C7075 |
| `kClippingPictureTypeIcon` | `'clpp'` | 0x636C7070 |
| `kClippingTextTypeIcon` | `'clpt'` | 0x636C7074 |
| `kClippingSoundTypeIcon` | `'clps'` | 0x636C7073 |
| `kDesktopIcon` | `'desk'` | 0x6465736B |
| `kFinderIcon` | `'FNDR'` | 0x464E4452 |
| `kFontSuitcaseIcon` | `'FFIL'` | 0x4646494C |
| `kFullTrashIcon` | `'ftrh'` | 0x66747268 |
| `kGenericApplicationIcon` | `'APPL'` | 0x4150504C |
| `kGenericCDROMIcon` | `'cddr'` | 0x63646472 |
| `kGenericControlPanelIcon` | `'APPC'` | 0x41505043 |
| `kGenericControlStripModuleIcon` | `'sdev'` | 0x73646576 |
| `kGenericComponentIcon` | `'thng'` | 0x74686E67 |
| `kGenericDeskAccessoryIcon` | `'APPD'` | 0x41505044 |
| `kGenericDocumentIcon` | `'docu'` | 0x646F6375 |
| `kGenericEditionFileIcon` | `'edtf'` | 0x65647466 |
| `kGenericExtensionIcon` | `'INIT'` | 0x494E4954 |
| `kGenericFileServerIcon` | `'srvr'` | 0x73727672 |
| `kGenericFontIcon` | `'ffil'` | 0x6666696C |
| `kGenericFontScalerIcon` | `'sclr'` | 0x73636C72 |
| `kGenericFloppyIcon` | `'flpy'` | 0x666C7079 |
| `kGenericHardDiskIcon` | `'hdsk'` | 0x6864736B |
| `kGenericIDiskIcon` | `'idsk'` | 0x6964736B |
| `kGenericRemovableMediaIcon` | `'rmov'` | 0x726D6F76 |
| `kGenericMoverObjectIcon` | `'movr'` | 0x6D6F7672 |
| `kGenericPCCardIcon` | `'pcmc'` | 0x70636D63 |
| `kGenericPreferencesIcon` | `'pref'` | 0x70726566 |
| `kGenericQueryDocumentIcon` | `'qery'` | 0x71657279 |
| `kGenericRAMDiskIcon` | `'ramd'` | 0x72616D64 |
| `kGenericSharedLibaryIcon` | `'shlb'` | 0x73686C62 |
| `kGenericStationeryIcon` | `'sdoc'` | 0x73646F63 |
| `kGenericSuitcaseIcon` | `'suit'` | 0x73756974 |
| `kGenericWORMIcon` | `'worm'` | 0x776F726D |
| `kInternationResourcesIcon` | `'ifil'` | 0x6966696C |
| `kKeyboardLayoutIcon` | `'kfil'` | 0x6B66696C |
| `kSoundFileIcon` | `'sfil'` | 0x7366696C |
| `kSystemSuitcaseIcon` | `'zsys'` | 0x7A737973 |
| `kTrashIcon` | `'trsh'` | 0x74727368 |
| `kTrueTypeFontIcon` | `'tfil'` | 0x7466696C |
| `kTrueTypeFlatFontIcon` | `'sfnt'` | 0x73666E74 |
| `kTrueTypeMultiFlatFontIcon` | `'ttcf'` | 0x74746366 |
| `kUserIDiskIcon` | `'udsk'` | 0x7564736B |

**Internet locations**

| Constant | Type | Hex |
|---|---|---|
| `kInternetLocationHTTPIcon` | `'ilht'` | 0x696C6874 |
| `kInternetLocationFTPIcon` | `'ilft'` | 0x696C6674 |
| `kInternetLocationAppleShareIcon` | `'ilaf'` | 0x696C6166 |
| `kInternetLocationAppleTalkZoneIcon` | `'ilat'` | 0x696C6174 |
| `kInternetLocationFileIcon` | `'ilfi'` | 0x696C6669 |
| `kInternetLocationMailIcon` | `'ilma'` | 0x696C6D61 |
| `kInternetLocationNewsIcon` | `'ilnw'` | 0x696C6E77 |
| `kInternetLocationNSLNeighborhoodIcon` | `'ilns'` | 0x696C6E73 |
| `kInternetLocationGenericIcon` | `'ilge'` | 0x696C6765 |

**Folders**

| Constant | Type | Hex |
|---|---|---|
| `kGenericFolderIcon` | `'fldr'` | 0x666C6472 |
| `kDropFolderIcon` | `'dbox'` | 0x64626F78 |
| `kMountedFolderIcon` | `'mntd'` | 0x6D6E7464 |
| `kOpenFolderIcon` | `'ofld'` | 0x6F666C64 |
| `kOwnedFolderIcon` | `'ownd'` | 0x6F776E64 |
| `kPrivateFolderIcon` | `'prvf'` | 0x70727666 |
| `kSharedFolderIcon` | `'shfl'` | 0x7368666C |

**Sharing Privileges icons**

| Constant | Type | Hex |
|---|---|---|
| `kSharingPrivsNotApplicableIcon` | `'shna'` | 0x73686E61 |
| `kSharingPrivsReadOnlyIcon` | `'shro'` | 0x7368726F |
| `kSharingPrivsReadWriteIcon` | `'shrw'` | 0x73687277 |
| `kSharingPrivsUnknownIcon` | `'shuk'` | 0x7368756B |
| `kSharingPrivsWritableIcon` | `'writ'` | 0x77726974 |

**Users and Groups icons**

| Constant | Type | Hex |
|---|---|---|
| `kUserFolderIcon` | `'ufld'` | 0x75666C64 |
| `kWorkgroupFolderIcon` | `'wfld'` | 0x77666C64 |
| `kGuestUserIcon` | `'gusr'` | 0x67757372 |
| `kUserIcon` | `'user'` | 0x75736572 |
| `kOwnerIcon` | `'susr'` | 0x73757372 |
| `kGroupIcon` | `'grup'` | 0x67727570 |

**Special folders**

| Constant | Type | Hex |
|---|---|---|
| `kAppleExtrasFolderIcon` | `'aexƒ'` | 0x616578C4 |
| `kAppleMenuFolderIcon` | `'amnu'` | 0x616D6E75 |
| `kApplicationsFolderIcon` | `'apps'` | 0x61707073 |
| `kApplicationSupportFolderIcon` | `'asup'` | 0x61737570 |
| `kAssistantsFolderIcon` | `'astƒ'` | 0x617374C4 |
| `kContextualMenuItemsFolderIcon` | `'cmnu'` | 0x636D6E75 |
| `kControlPanelDisabledFolderIcon` | `'ctrD'` | 0x63747244 |
| `kControlPanelFolderIcon` | `'ctrl'` | 0x6374726C |
| `kControlStripModulesFolderIcon` | `'sdvƒ'` | 0x736476C4 |
| `kDocumentsFolderIcon` | `'docs'` | 0x646F6373 |
| `kExtensionsDisabledFolderIcon` | `'extD'` | 0x65787444 |
| `kExtensionsFolderIcon` | `'extn'` | 0x6578746E |
| `kFavoritesFolderIcon` | `'favs'` | 0x66617673 |
| `kFontsFolderIcon` | `'font'` | 0x666F6E74 |
| `kHelpFolderIcon` | `'ƒhlp'` | 0xC4686C70 |
| `kInternetFolderIcon` | `'intƒ'` | 0x696E74C4 |
| `kInternetPlugInFolderIcon` | `'ƒnet'` | 0xC46E6574 |
| `kLocalesFolderIcon` | `'ƒloc'` | 0xC46C6F63 |
| `kMacOSReadMeFolderIcon` | `'morƒ'` | 0x6D6F72C4 |
| `kPreferencesFolderIcon` | `'prfƒ'` | 0x707266C4 |
| `kPrinterDescriptionFolderIcon` | `'ppdf'` | 0x70706466 |
| `kPrinterDriverFolderIcon` | `'ƒprd'` | 0xC4707264 |
| `kPrintMonitorFolderIcon` | `'prnt'` | 0x70726E74 |
| `kRecentApplicationsFolderIcon` | `'rapp'` | 0x72617070 |
| `kRecentDocumentsFolderIcon` | `'rdoc'` | 0x72646F63 |
| `kRecentServersFolderIcon` | `'rsrv'` | 0x72737276 |
| `kScriptingAdditionsFolderIcon` | `'ƒscr'` | 0xC4736372 |
| `kSharedLibrariesFolderIcon` | `'ƒlib'` | 0xC46C6962 |
| `kScriptsFolderIcon` | `'scrƒ'` | 0x736372C4 |
| `kShutdownItemsDisabledFolderIcon` | `'shdD'` | 0x73686444 |
| `kShutdownItemsFolderIcon` | `'shdf'` | 0x73686466 |
| `kSpeakableItemsFolder` | `'spki'` | 0x73706B69 |
| `kStartupItemsDisabledFolderIcon` | `'strD'` | 0x73747244 |
| `kStartupItemsFolderIcon` | `'strt'` | 0x73747274 |
| `kSystemExtensionDisabledFolderIcon` | `'macD'` | 0x6D616344 |
| `kSystemFolderIcon` | `'macs'` | 0x6D616373 |
| `kTextEncodingsFolderIcon` | `'ƒtex'` | 0xC4746578 |
| `kAppearanceFolderIcon` | `'appr'` | 0x61707072 |
| `kUtilitiesFolderIcon` | `'utiƒ'` | 0x757469C4 |
| `kVoicesFolderIcon` | `'fvoc'` | 0x66766F63 |
| `kColorSyncFolderIcon` | `'prof'` | 0x70726F66 |
| `kInternetSearchSitesFolderIcon` | `'issf'` | 0x69737366 |
| `kUsersFolderIcon` | `'usrƒ'` | 0x757372C4 |

**Badges**

| Constant | Type | Hex |
|---|---|---|
| `kAppleScriptBadgeIcon` | `'scrp'` | 0x73637270 |
| `kLockedBadgeIcon` | `'lbdg'` | 0x6C626467 |
| `kMountedBadgeIcon` | `'mbdg'` | 0x6D626467 |
| `kSharedBadgeIcon` | `'sbdg'` | 0x73626467 |
| `kAliasBadgeIcon` | `'abdg'` | 0x61626467 |

**Alert icons**

| Constant | Type | Hex |
|---|---|---|
| `kAlertNoteIcon` | `'note'` | 0x6E6F7465 |
| `kAlertCautionIcon` | `'caut'` | 0x63617574 |
| `kAlertStopIcon` | `'stop'` | 0x73746F70 |

**Networking icons**

| Constant | Type | Hex |
|---|---|---|
| `kAppleTalkIcon` | `'atlk'` | 0x61746C6B |
| `kAppleTalkZoneIcon` | `'atzn'` | 0x61747A6E |
| `kAFPServerIcon` | `'afps'` | 0x61667073 |
| `kFTPServerIcon` | `'ftps'` | 0x66747073 |
| `kHTTPServerIcon` | `'htps'` | 0x68747073 |
| `kGenericNetworkIcon` | `'gnet'` | 0x676E6574 |
| `kIPFileServerIcon` | `'isrv'` | 0x69737276 |

**Other icons**

| Constant | Type | Hex |
|---|---|---|
| `kAppleLogoIcon` | `'capl'` | 0x6361706C |
| `kAppleMenuIcon` | `'sapl'` | 0x7361706C |
| `kBackwardArrowIcon` | `'baro'` | 0x6261726F |
| `kFavoriteItemsIcon` | `'favr'` | 0x66617672 |
| `kForwardArrowIcon` | `'faro'` | 0x6661726F |
| `kGridIcon` | `'grid'` | 0x67726964 |
| `kHelpIcon` | `'help'` | 0x68656C70 |
| `kKeepArrangedIcon` | `'arng'` | 0x61726E67 |
| `kLockedIcon` | `'lock'` | 0x6C6F636B |
| `kNoFilesIcon` | `'nfil'` | 0x6E66696C |
| `kNoFolderIcon` | `'nfld'` | 0x6E666C64 |
| `kNoWriteIcon` | `'nwrt'` | 0x6E777274 |
| `kProtectedApplicationFolderIcon` | `'papp'` | 0x70617070 |
| `kProtectedSystemFolderIcon` | `'psys'` | 0x70737973 |
| `kRecentItemsIcon` | `'rcnt'` | 0x72636E74 |
| `kShortcutIcon` | `'shrt'` | 0x73687274 |
| `kSortAscendingIcon` | `'asnd'` | 0x61736E64 |
| `kSortDescendingIcon` | `'dsnd'` | 0x64736E64 |
| `kUnlockedIcon` | `'ulck'` | 0x756C636B |
| `kConnectToIcon` | `'cnct'` | 0x636E6374 |

### 1.5 How GetIconRef finds an icon (creator, type)

The documentation says only:

- "This routine returns an icon ref for an icon in the desktop database
  or for a registered icon." [H]
- "The vRefNum is used as a hint on where to look for the icon first. Use
  kOnSystemDisk if you don't know what to pass." [H] [R03]: "The volume
  where Icon Services should start to search for the desired icon."
- "Icon Services defines constants for commonly-used system icons. You can
  pass one of these constants in the iconType parameter if you specify
  kSystemIconCreator in the creator parameter." [R03]
- [G]: "If you need an icon from the desktop database or a registered icon
  (an icon that has been previously identified to Icon Services), the
  simplest and fastest way to obtain an IconRef is to use the function
  GetIconRef."
- Desktop database icons are at most 8-bit with 1-bit masks: [MT]
  (observed) "The new 32-bit icon format is not supported in the desktop
  database." (`BNDL` unchanged in 8.5.)

So the sources are (a) the registry, keyed by (creator, type), with the
system's icons under `'macs'`, and (b) the desktop database of the volume
`vRefNum` first, then (by "first") other volumes. **UNSPECIFIED**, to test
on 9.0.4:

1. Order: registry before desktop database? ([MT] says registered icons
   "override the desktop database entries".)
2. Which volumes are searched after `vRefNum`, in which order, and whether
   `kOnSystemDisk` means the startup volume only or "all, startup first".
   Natural reading: the hinted volume's desktop database (`PBDTGetIcon`
   per size/depth, `kLarge1BitMask`... types, through `PBDTGetPath`), then
   every other mounted volume's.
3. Fallback when neither has the pair. The header and reference do not
   say that `GetIconRef` returns a generic icon. Two candidates: (i)
   `noSuchIconErr` (-2581, "The requested icon could not be found"); (ii)
   a generic icon chosen by type (`'APPL'` -> `kGenericApplicationIcon`,
   `'APPC'` -> control panel, `'INIT'` -> extension, other types ->
   `kGenericDocumentIcon` 'docu'; i.e. retry with (`'macs'`, type), then
   (`'macs'`, `'docu'`)). For the later Mac OS X call
   `GetIconRefFromTypeInfo` [R07] says "If none of the input parameters is
   specified or if no match is found, this function returns the generic
   document icon" and that with only creator and type it "is equivalent to
   calling GetIconRef (kOnSystemDisk, inCreator, inType)", which suggests
   (ii) for `GetIconRef` as well, but that is Mac OS X text. Test: an
   unknown creator with type 'TEXT', 'APPL', 'xxxx', and creator 'macs' with
   an unknown type.
4. Whether the returned IconRef for a desktop-database icon is shared by
   all volumes or per volume (`FlushIconRefsByVolume` speaks of "the icons
   related to the indicated volume", so IconRefs carry a volume).

### 1.6 How GetIconRefFromFile decides (files, folders, volumes)

- "This routine returns an icon ref for the specified file, folder or
  volume." [H]
- "Use this routine if you have no information about the file system
  object. If you have already done a GetCatInfo on the file and want to
  save some I/O, call GetIconRefFromFolder() if you know it's a folder
  with no custom icon or call GetIconRef() if it's a file with no custom
  icon." [H]
- "The label information is provided separately, since two files with the
  same icon but a different label would share the same iconRef. The label
  can be used in PlotIconRef() for example." [H]
- Custom icons: "For custom icons, icns IconFamily resources of the custom
  icon resource ID are fetched first before the classic custom icons
  (individual 'ics#, ICN#, etc) are fetched. If the fetch of the icns
  resource succeeds then the icns is looked at exclusively for the icon
  data. For custom icons, new icon features such as 32-bit deep icons are
  only fetched from the icns resource." [H] The custom icon resource ID is
  `kCustomIconResource` (-16455) (`Finder.h`); the file's custom-icon flag is
  `kHasCustomIcon` = 0x0400 in `fdFlags`/`frFlags` (`Finder.h`). For a
  folder the resources are in the folder's invisible file `Icon\r` (TN1142:
  "Starting with Finder 8.5, the invisible "Icon<cr>" file placed in
  directories may contain resource-based information used by the Finder
  other than an icon suite."; [G] "a folder's invisible icon file").
- Volumes: TN1142 and [H]: "DriverGestalt is called with code
  kdgMediaIconSuite by IconServices after calling FSM to determine a driver
  icon for a particular device. The result of the kdgMediaIconSuite call to
  DriverGestalt should be a pointer an an IconFamily." `kdgMediaIconSuite`
  = `'mics'`, `kdgPhysDriveIconSuite` = `'dics'` (`DriverGestalt.h`,
  TN1142 Table 6: "A pointer to a IconFamily ('icns') data structure for
  representing the disk driver's media (formerly in csCode 21)"). So for a
  volume: a custom icon (`Icon\r` in the root, flag on the root
  directory), else the File System Manager's icon for the volume, else the
  driver's (Driver Gestalt `'mics'`, before that the classic csCode 21 1-bit
  icon), else a generic by kind (`kGenericHardDiskIcon` 'hdsk',
  `kGenericFloppyIcon` 'flpy', `kGenericCDROMIcon` 'cddr',
  `kGenericRAMDiskIcon` 'ramd', `kGenericFileServerIcon` 'srvr',
  `kGenericRemovableMediaIcon` 'rmov'). The exact order and how the kind is
  determined: **UNSPECIFIED**.
- Custom icons enabled per volume (1.9): a custom icon is used only if
  custom icons are enabled on the file's volume (implied by
  `SetCustomIconsEnabled`; **UNSPECIFIED** whether the flag also stops the
  volume's own custom icon).
- Files without a custom icon: the file's creator and type, as
  `GetIconRef(vRefNum, creator, type)` (the header's advice). Special
  cases the header does not spell out, all **UNSPECIFIED** and to test:
  - Aliases (`kIsAlias` 0x8000): the documentation says nothing about
    resolving them. Badges include `kAliasBadgeIcon` 'abdg'; plausible
    behaviour is the icon of the alias file's own type/creator (the
    Finder sets an alias's type/creator to the original's, folder aliases
    use special types like `'fdrp'`), composited or not with the alias
    badge. Test: alias to an application, document, folder, volume.
  - Stationery (`kIsStationery` 0x0800): `kGenericStationeryIcon`
    'sdoc' as fallback?
  - Files whose custom-icon flag is set but which have no custom icon
    resources (TN1142: the 8.5 Finder "now resets the custom icon bit and
    uses the default icon for these files"): fall back to type/creator.
  - Badges ([G], Finder.h `CustomBadgeResource` 'badg' -16455, with
    `kExtendedFlagHasCustomBadge` 0x0100 and `kExtendedFlagsAreInvalid`
    0x8000 clear in the extended Finder flags): "The overrideType and
    overrideCreator fields let you designate the badge as a replacement
    for the standard icon for this file or folder." and "The type and
    creator codes specified in a'badg' resource must be registered with
    Icon Services before you can use the badge." Whether
    `GetIconRefFromFile` itself applies `customBadge*` (returning a
    composite) and `override*`: **UNSPECIFIED**; [G] presents badges as
    Icon Services behaviour, so probably yes.
- Folders: as `GetIconRefFromFolder` (1.7), unless the folder has a custom
  icon.
- `theLabel` out: the Finder label of the object, "the file or folder's
  label" [R03], from the `kColor` bits (0x000E) of the Finder flags. In
  which form: 0–7 (the flags' bits >> 1) or as a transform
  (`kTransformLabel1` = 0x0100 ... 0x0700)? The header's "The label can
  be used in PlotIconRef() for example" and the [MT] sample
  (`IconTransformType theTransform = theLabel;`, passed straight to
  `PlotIconRef`) suggest the transform form, i.e. label number << 8.
  **UNSPECIFIED**, test it. For volumes the label is presumably 0.
- Nonexistent `FSSpec`: `fnfErr`/`nsvErr` (File Manager's) presumably;
  **UNSPECIFIED**.

### 1.7 Folders and special folders

- `GetIconRefFromFolder(vRefNum, parentFolderID, folderID, attributes,
  accessPrivileges, &iconRef)`: "This routine returns an icon ref for a
  folder with no custom icon." "Attributes should be
  CInfoPBRec.dirInfo.ioFlAttrib for this folder. Access privileges should
  be CInfoPBRec.dirInfo.ioACUser for this folder." [H]
- The documentation does not say how the folder icon is chosen; the inputs
  imply:
  - `folderID`/`parentFolderID`/`vRefNum`: special folders (System Folder,
    Apple Menu Items, Control Panels, Extensions, Fonts, Preferences, ...),
    i.e. the Folder Manager's folders, get the special folder icons
    (`kSystemFolderIcon` 'macs', `kAppleMenuFolderIcon` 'amnu',
    `kControlPanelFolderIcon` 'ctrl', `kExtensionsFolderIcon` 'extn',
    `kFontsFolderIcon` 'font', `kPreferencesFolderIcon` 'prfƒ', ... 1.4.1).
    The Folder Manager's `FolderDesc` has `badgeSignature`/`badgeType`
    (`Folders.h`, `AddFolderDescriptor`), which suggests a special folder's
    icon is the generic folder with the (`badgeSignature`, `badgeType`)
    icon on top, or that type. **UNSPECIFIED** how Icon Services uses
    them. The System Folder is recognised by being the blessed folder; the
    others by `FindFolder` on that volume? **UNSPECIFIED**.
  - `attributes` (`ioFlAttrib`; directory bits from `Files.h`):
    `kioFlAttribLockedMask` 0x01 "Set if file or directory is locked",
    `kioFlAttribInSharedMask` 0x04 "Set if the directory is within a shared
    area of the directory hierarchy", `kioFlAttribMountedMask` 0x08 "Set if
    the directory is a share point that is mounted by some user",
    `kioFlAttribSharePointMask` 0x20 "Set if the directory is a share
    point". These match `kSharedFolderIcon` 'shfl' (share point),
    `kMountedFolderIcon` 'mntd' (mounted share point), and the badges
    `kSharedBadgeIcon` 'sbdg', `kMountedBadgeIcon` 'mbdg',
    `kLockedBadgeIcon` 'lbdg'. Which bits are honoured and whether the
    result is a dedicated folder icon or a badged composite:
    **UNSPECIFIED**.
  - `accessPrivileges` (`ioACUser`, `Files.h`): `kioACUserNoSeeFolderMask`
    0x01, `kioACUserNoSeeFilesMask` 0x02, `kioACUserNoMakeChangesMask`
    0x04, `kioACUserNotOwnerMask` 0x80. These match `kPrivateFolderIcon`
    'prvf' (no See Folder/See Files), `kDropFolderIcon` 'dbox' (Make
    Changes only: a drop box), `kOwnedFolderIcon` 'ownd' (owner). The exact
    mapping is **UNSPECIFIED**; on a local unshared HFS volume `ioACUser`
    is 0 (all privileges, owner) and the result is presumably still
    `kGenericFolderIcon` 'fldr', so the owned/private/drop icons apply only
    inside shared areas or on server volumes.
- Open folder: `kOpenFolderIcon` 'ofld' and, in 9.0, `GetIconRefVariant`
  with `kOpenIconVariant` 'open'.
- The Trash: `kTrashIcon` 'trsh' / `kFullTrashIcon` 'ftrh'. Whether
  `GetIconRefFromFile`/`GetIconRefFromFolder` on the Trash folder returns
  them (empty vs full): **UNSPECIFIED**; the Finder may ask with
  `GetIconRef('macs', 'trsh')` itself.
- The desktop: `kDesktopIcon` 'desk'.

### 1.8 Overrides

- `OverrideIconRef(old, new)`: "This routines replaces the bitmaps of the
  specified IconRef with the ones from the new IconRef." [H]
- `OverrideIconRefFromResource(iconRef, file, id)`: "replaces the bitmaps
  of the specified IconRef with the ones in the specified resource file."
  [H]; "This value must be non-zero. You should provide a resource of type
  'icns' if possible. If an 'icns' resource is not available, Icon
  Services uses standard icon suite resources, such as 'ICN#', instead."
  [R03]
- `RemoveIconRefOverride(iconRef)`: "This routine remove an override if
  one was applied to the icon and reverts back to the original bitmap
  data." [H]
- [G]: "You may wish to redraw icons on a temporary basis without going to
  the trouble of obtaining a new IconRef. One example of this is when a
  user has started to download a file and you want to use partial file
  icons to represent the various stages of the download process."
- The override is on the IconRef, which is shared: every client holding
  that IconRef sees the new bitmaps (by 1.1). **UNSPECIFIED**: whether the
  override keeps an owner on `newIconRef` (or copies its data), whether
  overriding twice stacks or replaces, whether `RemoveIconRefOverride`
  without an override is `noErr`, and whether an override survives the
  IconRef's count going to zero and back.

### 1.9 Composites

- `CompositeIconRef(background, foreground, &composite)`: "Superimposes an
  IconRef on top of another one" [H]; "This function provides an
  alternative to badging when you need to indicate a change of state."
  [R03]
- `IsIconRefComposite(composite, &background, &foreground)`: "Indicates if
  a given icon ref is a composite of two other icon refs (and which ones)
  If it isn't a composite, backgroundIconRef and foreGroundIconRef will be
  0." [H]
- **UNSPECIFIED**: whether the composite holds owners on its two parts;
  whether `IsIconRefComposite` adds owners to the refs it returns; whether
  compositing the same pair twice returns the same IconRef; how the
  foreground is drawn (its mask over the background, same size; for each
  plotted size, the foreground's same-size member); what
  `IconRefToIconFamily` of a composite gives (flattened bitmaps?); whether
  hit-testing uses the union of both masks.

### 1.10 Labels, transforms, usage flags

- `IconTransformType` values (`Icons.h`): `kTransformNone` 0,
  `kTransformDisabled` 1, `kTransformOffline` 2, `kTransformOpen` 3,
  `kTransformLabel1`..`7` = 0x0100..0x0700, `kTransformSelected` 0x4000,
  and the `Selected|` combinations. As with `PlotIconSuite` [IM]/[R03]: "If
  you don't specify a label constant in this parameter, the function
  displays the icon using the default label for that icon suite." An
  IconRef has no label of its own ([H]: the label is "provided
  separately"), so with no label bits the icon is drawn unlabelled.
- `IconServicesUsageFlags`: only `kIconServicesNormalUsageFlag` = 0 is
  defined. "Reserved for future use. Pass the kIconServicesNormalUsageFlag
  constant in this parameter." [R03] Behaviour for other values:
  **UNSPECIFIED** (ignore them).
- Alignment (`IconAlignmentType`, `kAlignNone` 0 ... `kAlignBottomRight`
  0x0F) as for the Icon Utilities.

### 1.11 Custom icons enabled per volume

- `SetCustomIconsEnabled(vRefNum, enable)`: "Enable or disable custom
  icons on the specified volume." [H] "If you use the SetCustomIconsEnabled
  function to enable or disable custom icons, the setting remains in
  effect only as long as the specified volume remains mounted during the
  current session." [R03]
- `GetCustomIconsEnabled(vRefNum, &enabled)`: "Return true if custom icons
  are enabled on the specified volume, false otherwise." [H]
- Default for a newly mounted volume: **UNSPECIFIED** (presumably true).
  Whether disabling flushes IconRefs already handed out for custom icons on
  that volume: **UNSPECIFIED**. Bad `vRefNum`: `nsvErr`? **UNSPECIFIED**.

### 1.12 Deep icons, sizes and depths

- Concepts [G]: "The 'icns' resource provides for 32-bit-deep icon data."
  "Icons provided through the 'icns' resource feature deep masks, meaning
  an icon mask can have 256 different levels of transparency." "The
  'icns' resource adds "huge" icons, which are 48 pixels by 48 pixels, as
  well as providing the sizes contained in other icon resources."
- TN1142: "The new 'icns' resource contains data for 4 icon sizes (mini,
  small, large, huge) at 4 color depths (1-bit, 4-bit, 8-bit, and 32-bit)
  and two kind of masks (1-bit masks and 8-bit masks). Deep (8 bit) masks
  allow for the specification of transparent drawing effects. Each pixel
  in a deep mask indicates the level of transparency desired for drawing a
  particular pixel. Transparency levels range from 0 (transparent) through
  255 (opaque)."
- "Prior to Mac OS 8.5, Icon Utilities allowed only 34 arbitrary colors to
  highlight properly (they were known as the Apple icon colors). This
  limitation has been removed." [TN]; [G] "8-bit icon data is no longer
  limited to the 34 colors of the classic icon color palette. All 256
  colors from the System palette are available."
- Hit-testing and regions use the 1-bit mask: "Icon Services uses the
  icon's black-and-white mask for hit-testing, even if you provide a deep
  mask." and "Icon Services uses the icon's black-and-white mask to
  determine the region data, even if you provide a deep mask." [R03]
- [G] guidelines (for the data, so also what to expect): "You must provide
  at least one of the following icon types: 'ICN#', 'ics#', or 'icl#'."
  "If you provide a deep mask, all of the non-transparent pixels in the
  deep mask should correspond to a black pixel in the black-and-white
  mask." "If you don't provide 4-bit icon data, Icon Services will use
  black-and-white icon data instead."
- `gestaltIconUtilitiesHas48PixelIcons`, `...Has32BitIcons`,
  `...Has8BitDeepMasks` (`Gestalt.h`) report support. "Deep icons
  enabled" as a user setting is not in the documentation; the only
  per-volume switch is custom icons (1.11).
- Size choice when plotting: the Icon Utilities rule [IM]/[R03]
  (`PlotIconSuite`): "If the width or height of a destination rectangle is
  greater than or equal to 32 pixels, the function uses the 32-by-32 pixel
  icon with the appropriate bit depth for the display device. If the
  destination rectangle is less than 32 by 32 pixels and greater than 16
  pixels wide or 12 pixels high, the function uses the 16-by-16 pixel icon
  ... If the destination rectangle's height is less than or equal to 12
  pixels or its width is less than or equal to 16 pixels, the function uses
  the 12-by-16 pixel icon" and "If the destination rectangle is not one of
  these standard sizes, the function expands or shrinks the icon to fit."
  `PlotIconRef` "mostly takes the same parameters as PlotIconSuite" [H].
  Where the 48x48 huge icon enters (rects >= 48?) and whether a missing size
  is scaled from another size: **UNSPECIFIED**. Depth choice: "the current
  bit depth of the display device" [IM]; whether 32-bit data is used at
  16-bit depth: **UNSPECIFIED**.

---

## 2. The IconFamily ('icns') format

### 2.1 Layout

From `Icons.h` (structures; big-endian, `mac68k` alignment):

```
struct IconFamilyElement {
  OSType  elementType;      /* 'ICN#', 'icl8', etc...*/
  Size    elementSize;      /* Size of this element*/
  unsigned char elementData[1];
};
struct IconFamilyResource {
  OSType  resourceType;     /* Always 'icns'*/
  Size    resourceSize;     /* Total size of this resource*/
  IconFamilyElement elements[1];
};
```

and `Icons.r` (the Rez template), which settles what the sizes count:

```
type 'icns' {
iconFamilyBegin:
    literal longint = 'icns';
    unsigned longint = (iconFamilyEnd - iconFamilyBegin) >> 3;
    array elementArray {
        literal longint;
        ...
        longint = (elementEnd[...] - elementBegin[...] >> 3) + 4;
        hex string;
elementEnd:
```

Byte by byte:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `'icns'` ($69636E73) |
| 4 | 4 | total size of the family **including this 8-byte header** (Rez: end minus begin, begin before the `'icns'`) |
| 8 | ... | elements, back to back, until offset = total size |

Each element:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | element type (OSType) |
| 4 | 4 | element size **including its 8-byte header** (Rez: from after the type to the end, plus 4 for the type) |
| 8 | size-8 | data |

No padding or alignment between elements is described. The element order
is not specified (**UNSPECIFIED**: whether readers accept any order, and in
which order `IconSuiteToIconFamily`/`SetIconFamilyData` write them; test by
dumping `IconRefToIconFamily` output). An `IconFamilyHandle` holds exactly
this structure, the same bytes as an `'icns'` resource ([MT]: "The handle
contains the same data as an 'icns' in a resource file."), and a `.icns`
file (9.0, `ReadIconFile`/`WriteIconFile`) is presumably the same bytes in
its data fork (**UNSPECIFIED**: data fork vs resource, file type 'icns'?).
The `Size` fields are signed 32-bit.

### 2.2 Element types

From `Icons.h`. "The following icon types can be used as a resource type
or as an icon element type inside a 'icns' icon family" and "The following
icon types can only be used as an icon element inside a 'icns' icon
family". Pixel sizes: mini 16 wide x 12 high, small 16x16, large 32x32
([IM]), huge 48x48 ([G]), thumbnail 128x128 ([R07]). Byte counts below are
uncompressed and derived (width x height x depth / 8; the `#` types are
a 1-bit icon followed by a 1-bit mask, as the classic `'ICN#'` [IM]); only
the thumbnail figures are stated by Apple.

| Type | Constant | Use | Pixels | Bytes |
|---|---|---|---|---|
| `'ICN#'` | `kLarge1BitMask` | resource or element | 32x32, 1-bit icon + 1-bit mask | 256 |
| `'icl4'` | `kLarge4BitData` | resource or element | 32x32, 4-bit | 512 |
| `'icl8'` | `kLarge8BitData` | resource or element | 32x32, 8-bit | 1024 |
| `'il32'` | `kLarge32BitData` | resource or element | 32x32, 32-bit | 4096 |
| `'l8mk'` | `kLarge8BitMask` | resource or element | 32x32, 8-bit mask | 1024 |
| `'ics#'` | `kSmall1BitMask` | resource or element | 16x16, icon + mask | 64 |
| `'ics4'` | `kSmall4BitData` | resource or element | 16x16, 4-bit | 128 |
| `'ics8'` | `kSmall8BitData` | resource or element | 16x16, 8-bit | 256 |
| `'is32'` | `kSmall32BitData` | resource or element | 16x16, 32-bit | 1024 |
| `'s8mk'` | `kSmall8BitMask` | resource or element | 16x16, 8-bit mask | 256 |
| `'icm#'` | `kMini1BitMask` | resource or element | 16x12, icon + mask | 48 |
| `'icm4'` | `kMini4BitData` | resource or element | 16x12, 4-bit | 96 |
| `'icm8'` | `kMini8BitData` | resource or element | 16x12, 8-bit | 192 |
| `'ich#'` | `kHuge1BitMask` | element only | 48x48, icon + mask | 576 |
| `'ich4'` | `kHuge4BitData` | element only | 48x48, 4-bit | 1152 |
| `'ich8'` | `kHuge8BitData` | element only | 48x48, 8-bit | 2304 |
| `'ih32'` | `kHuge32BitData` | element only | 48x48, 32-bit | 9216 |
| `'h8mk'` | `kHuge8BitMask` | element only | 48x48, 8-bit mask | 2304 |
| `'it32'` | `kThumbnail32BitData` | element only | 128x128, 32-bit | 65536 ("to obtain 65,536 bytes of raw bitmap data" [R07]) |
| `'t8mk'` | `kThumbnail8BitMask` | element only | 128x128, 8-bit mask | 16384 ("the data is in the same format except that it is one byte per pixel" [R07]) |

Also allowed as an element, per `Icons.h` ("These can be used as an
element of an 'icns' icon family or as a parameter to
GetIconRefVariant"): the variants `'tile'` (`kTileIconVariant`), `'over'`
(`kRolloverIconVariant`), `'drop'` (`kDropIconVariant`), `'open'`
(`kOpenIconVariant`), `'odrp'` (`kOpenDropIconVariant`). Their element
data is not described; presumably a nested `'icns'` family.
**UNSPECIFIED**. `'PICT'` is accepted by `SetIconFamilyData` and
`GetIconFamilyData` ("The type can be one of the icon type, or 'PICT'"
[H]); whether it is stored as an element or rendered into the bitmaps
([MT]: "The SetIconFamilyData call can be used to create an icon using a
PICT"): **UNSPECIFIED**.

Note the header's own comment uses `kHuge1BitMask = 'ich#'` while the
`'icns'` text says 4 sizes x 4 depths: so the huge family has 1/4/8/32-bit
data, but the thumbnail only 32-bit + 8-bit mask, and the mini only
1/4/8-bit (no `'im32'`).

Pixel formats (**partly UNSPECIFIED**): 1/4/8-bit as the classic icon
resources (rows of whole bytes, most significant pixel first; 4- and 8-bit
indexes into the standard system color tables, `clut` 4 and 8 [IM]);
8-bit masks one byte per pixel, 0 transparent to 255 opaque [TN]; 32-bit
"four bytes per pixel, RGB data" [R07] (byte order within the pixel,
presumably an unused/alpha byte then R, G, B as a QuickDraw 32-bit
pixel: **UNSPECIFIED**).

Compression: "The data will be compressed if needed." (SetIconFamilyData)
and "The data will be returned uncompressed." (GetIconFamilyData) [H].
[MT] (observed): "Appearance can't handle 'il32' resources that are
compressed. Unfortunately, this is the default way of storing the data in
the 'icns' resource". So the 32-bit elements (`is32`, `il32`, `ih32`,
probably not `it32`) are normally stored compressed, and a reader must
accept both forms (an element of exactly width x height x 4 bytes being
raw). Apple's documentation does not describe the compression scheme:
**UNSPECIFIED**; settle it from data, i.e. by feeding known pixels to
`SetIconFamilyData` on 9.0.4 and reading the element bytes back, not from
Apple code.

### 2.3 Selectors (`IconSelectorValue`)

`kSelectorLarge1Bit` 0x00000001, `kSelectorLarge4Bit` 0x02,
`kSelectorLarge8Bit` 0x04, `kSelectorLarge32Bit` 0x08,
`kSelectorLarge8BitMask` 0x10, `kSelectorSmall1Bit` 0x0100, `...4Bit`
0x0200, `...8Bit` 0x0400, `...32Bit` 0x0800, `...8BitMask` 0x1000,
`kSelectorMini1Bit` 0x00010000, `...4Bit` 0x00020000, `...8Bit`
0x00040000, `kSelectorHuge1Bit` 0x01000000, `...4Bit` 0x02000000,
`...8Bit` 0x04000000, `...32Bit` 0x08000000, `...8BitMask` 0x10000000;
groups `kSelectorAllLargeData` 0x000000FF, `AllSmallData` 0x0000FF00,
`AllMiniData` 0x00FF0000, `AllHugeData` 0xFF000000, `All1BitData`,
`All4BitData`, `All8BitData`, `All32BitData` (large|small|huge),
`kSelectorAllAvailableData` 0xFFFFFFFF. The thumbnail has no selector bit:
whether `IconRefToIconFamily(..., kSelectorAllAvailableData, ...)`
includes `it32`/`t8mk`: **UNSPECIFIED**. A `kSelector*1Bit` bit stands for
the `#` element (icon and mask together).

---

## 3. Routines

Common to all: "A result code. See "Icon Services and Utilities Result
Codes."" [R03]. No routine's page lists specific error codes; the result
code table (section 4) is the only list. So for each routine the errors
below are: the ones the text implies, then **UNSPECIFIED** for the rest.
Presumably: an IconRef parameter that is 0 or not valid gives
`invalidIconRefErr` (-2580) ([R03] at `GetIconRefOwners`/`ReleaseIconRef`:
"Any subsequent attempt to use the IconRef returns a result code of -2580
(invalidIconRefErr)"); out-of-memory gives `memFullErr` (-108); File and
Resource Manager errors (`fnfErr`, `resNotFound`, ...) pass through. Nil
output pointers: **UNSPECIFIED** (`paramErr` -50?).

### 3.1 IconRefToIconFamily ($24)

`OSErr IconRefToIconFamily(IconRef theIconRef, IconSelectorValue
whichIcons, IconFamilyHandle *iconFamily)`

- in `theIconRef`: "An IconRef to use as a source for icon data."
- in `whichIcons`: "The depths and sizes of icons in the iconFamily data
  structure."
- out `*iconFamily`: "On return, a pointer to a handle to the data
  structure which contains icon data as specified in the IconRef and
  whichIcons parameters. Icon Services returns NULL if no appropriate icon
  data is found." [R03]
- [H]: "This routines returns a new IconFamily that contains the data
  corresponding to the specified IconRef." New handle, caller disposes
  (`DisposeHandle`). [MT]: "The iconFamily will contain all versions of the
  icon available in the iconRef."
- Errors: `invalidIconRefErr`; when NULL is returned, the error
  (`noIconDataAvailableErr` -2582? `noErr`?) **UNSPECIFIED**. Handle
  allocated in which heap (current zone presumably): **UNSPECIFIED**.
  Elements stored compressed or not: **UNSPECIFIED**. Owner count
  unchanged.

### 3.2 IconFamilyToIconSuite ($25)

`OSErr IconFamilyToIconSuite(IconFamilyHandle iconFamily,
IconSelectorValue whichIcons, IconSuiteRef *iconSuite)`

- [H]: "This routine transfers the data from an icon family handle into an
  icon suite."
- in `iconFamily`, in `whichIcons` "The depths and sizes of icons to
  extract from the IconFamily data structure."; out `*iconSuite`: "On
  return, a pointer to the structure which contains icon data as
  specified in the iconFamily and whichIcons parameters. Icon Services
  returns NULL if no appropriate icon data is found." [R03]
- The suite is new (as `NewIconSuite`) with one handle per element;
  elements of the new types appear in it: [MT] (observed) "you'll probably
  get the following resource types in the icon suite: il32 ... l8mk ...
  is32 ... s8mk" and "Many of the functions in Icon Utilities will ignore
  these new resources. However, you will get them as resource types in your
  callback for a ForEachIconDo() call." 32-bit data in the suite
  uncompressed? **UNSPECIFIED** ([MT] reports Appearance failing on
  compressed `il32` resources, not on suites). The suite's handles are
  copies, not resources; the family is not consumed. Label of the new
  suite: none, presumably.
- Errors: **UNSPECIFIED** beyond the general ones (an `iconFamily` that is
  not `'icns'`: `paramErr`?).

### 3.3 IconSuiteToIconFamily ($26)

`OSErr IconSuiteToIconFamily(IconSuiteRef iconSuite, IconSelectorValue
whichIcons, IconFamilyHandle *iconFamily)`

- [H]: "This routine transfers the data in an icon suite into an icon
  family." [R03]: out "a pointer to a handle to the structure which
  contains icon data as specified in the iconSuite and whichIcons
  parameters. Icon Services returns NULL if no appropriate icon data is
  found."
- Suite entries that are purged resource handles: loaded first
  (presumably). Icon caches (`IconCacheRef` with a getter) as input:
  **UNSPECIFIED**. Element order, compression of 32-bit data:
  **UNSPECIFIED**.

### 3.4 SetIconFamilyData ($30)

`OSErr SetIconFamilyData(IconFamilyHandle iconFamily, OSType iconType,
Handle h)`

- [H]: "Change the data of an icon family. The data is copied. The type
  can be one of the icon type, or 'PICT'. The data will be compressed if
  needed."
- in/out `iconFamily` "A handle to an iconFamily data structure to be used
  as the target." (resized; element replaced if present, else added;
  header size updated). in `h`: [R07] "A handle to the icon data you
  provide. For a thumbnail icon, the handle contains raw image data in the
  form of 128x128, four bytes per pixel, RGB data. For a thumbnail mask,
  the data is in the same format except that it is one byte per pixel."
- **UNSPECIFIED**: what a handle of the wrong size does (`paramErr`?);
  whether an empty `h` removes the element; what `'PICT'` produces
  (stored as a `'PICT'` element, or rendered into every size, [MT]: "you
  can save the handle as 'icns' with a custom icon resource id (-16455)"
  after `SetIconFamilyData(iconFamily, 'PICT', pictHandle)`); where a new
  element goes in the order.

### 3.5 GetIconFamilyData ($31)

`OSErr GetIconFamilyData(IconFamilyHandle iconFamily, OSType iconType,
Handle h)`

- [H]: "Return a copy of the data in the icon family. The type can be one
  of the icon type, or 'PICT' The data will be returned uncompressed. The
  handle (h) will be resized as appropriate. If no data of the requested
  type is present, the handle size will be set to 0."
- in `h`: an existing handle the caller allocated (not a `Handle *`).
  ([R03] says "Icon Services sets the handle to 0"; the header's "the
  handle size will be set to 0" is the precise one: `SetHandleSize(h, 0)`.)
- Missing type: `noErr` with size 0, or an error? **UNSPECIFIED**.
  `'PICT'` from a family with no PICT element: rendered from the bitmaps,
  or size 0? **UNSPECIFIED**.

### 3.6 GetIconRefOwners ($0B)

`OSErr GetIconRefOwners(IconRef theIconRef, UInt16 *owners)`

- [H]: "This routine returns the reference count for the IconRef, or
  number of owners. A valid IconRef always has at least one owner."
- out `*owners`: "the current reference count" [R03].
- Errors: `invalidIconRefErr` (by 1.2).

### 3.7 AcquireIconRef ($27)

`OSErr AcquireIconRef(IconRef theIconRef)`: "This routine increments the
reference count for the IconRef" [H]. [G]: "Use the function
AcquireIconRef to increment the reference count for an IconRef. This
allows you to keep the icon data in the cache without going through the
trouble of obtaining additional IconRefs." Errors: `invalidIconRefErr`.

### 3.8 ReleaseIconRef ($28)

`OSErr ReleaseIconRef(IconRef theIconRef)`: "This routine decrements the
reference count for the IconRef. When the reference count reaches 0, all
memory allocated for the icon is disposed. Any subsequent use of the
IconRef is invalid." [H] See 1.2 for the two wordings and the open points.
Errors: `invalidIconRefErr`.

### 3.9 GetIconRefFromFile ($02)

`OSErr GetIconRefFromFile(const FSSpec *theFile, IconRef *theIconRef,
SInt16 *theLabel)`

- in `theFile`: "A pointer to the FSSpec structure specifying the file,
  folder or volume for the IconRef." [R03] (A volume: `parID` = 1
  (fsRtParID) and the volume name, presumably; or `parID` = 2
  (fsRtDirID) and an empty name? **UNSPECIFIED** which forms are
  accepted.)
- out `*theIconRef` (one owner added), out `*theLabel`: "On return, a
  pointer to the file or folder's label." [R03]
- Behaviour: 1.6. Errors: File Manager errors for a missing object
  (**UNSPECIFIED** which); `noSuchIconErr` never expected (a generic icon
  always exists) but **UNSPECIFIED**.

### 3.10 GetIconRef ($21)

`OSErr GetIconRef(SInt16 vRefNum, OSType creator, OSType iconType, IconRef
*theIconRef)`

- in `vRefNum`: "The volume where Icon Services should start to search for
  the desired icon. Pass the kOnSystemDisk constant if you are not sure
  which value to specify in this parameter." in `creator`, `iconType`: "The
  creator code of the desired icon." / "The type code of the desired
  icon." out `*theIconRef` (one owner added). [R03]
- Behaviour and open points: 1.5. Errors: `noSuchIconErr` (-2581) is the
  natural "not found" result. **UNSPECIFIED** whether it ever happens (see
  1.5 point 3).

### 3.11 GetIconRefFromFolder ($22)

`OSErr GetIconRefFromFolder(SInt16 vRefNum, SInt32 parentFolderID, SInt32
folderID, SInt8 attributes, SInt8 accessPrivileges, IconRef *theIconRef)`

- in: "The volume where the folder is located.", "The ID of the desired
  folder's parent folder.", "The ID of the desired folder.", attributes
  "You can obtain this data from the CInfoPBRec.dirInfo.ioFlAttrib field of
  the folder's catalog information record.", accessPrivileges "You can
  obtain this data from the CInfoPBRec.dirInfo.ioACUser field". out
  `*theIconRef` (one owner added). [R03]
- "This routine returns an icon ref for a folder with no custom icon." [H]
  So it never looks for `Icon\r` (presumably). Behaviour: 1.7. Errors:
  **UNSPECIFIED** (does it check that the folder exists?).

### 3.12 RegisterIconRefFromIconFamily ($1C)

`OSErr RegisterIconRefFromIconFamily(OSType creator, OSType iconType,
IconFamilyHandle iconFamily, IconRef *theIconRef)`

- [H]: "This routine adds a new entry to the IconRef registry. Other
  clients will be able to access it using the (creator, iconType) pair
  specified here. Lower-case creators are reserved for the system.
  Consider using RegisterIconRefFromResource() if possible, since the data
  registered using RegisterIconRefFromFamily() cannot be purged. The
  iconFamily data is copied and the caller is reponsible for disposing of
  it. This routine increments the reference count of the IconRef."
- out `*theIconRef`: "On return, a pointer to the desired icon data."
  [R03]
- Lower-case creators are a convention, not (as far as documented)
  refused: whether a lower-case creator returns an error: **UNSPECIFIED**
  (presumably accepted; the system itself registers 'macs').
- Registering an existing pair: 1.3 (**UNSPECIFIED**).

### 3.13 RegisterIconRefFromResource ($23)

`OSErr RegisterIconRefFromResource(OSType creator, OSType iconType, const
FSSpec *resourceFile, SInt16 resourceID, IconRef *theIconRef)`

- [H]: "Registers an IconRef from a resouce file. Lower-case creators are
  reserved for the system. The icon data to be fetched is either classic
  icon data or an icon family. The 'icns' icon family is searched for
  before the classic icon data."
- in `resourceID`: "The resource ID of the icon data to be registered.
  This value must be non-zero. You should provide a resource of type
  'icns' if possible. If an 'icns' resource is not available, Icon
  Services uses standard icon suite resources, such as 'ICN#', instead."
  [R03] (So resource ID 0 is an error: which one, **UNSPECIFIED**,
  `paramErr`?)
- Data is fetched from the file on demand and purgeable ([G], `FlushIconRefs`
  "if the data can be reacquired, for example if the data is provided from
  a resource" [H]); so the registry stores the FSSpec and ID, opening the
  file when it needs the bitmaps. **UNSPECIFIED**: whether the file is
  read at registration (errors for a missing file or resource then, or
  only at plot time), whether the file is left open. Note [MT]:
  "CIconPane will only search the application's resource fork (via the
  appSpec). PlotIconID, on the other hand, will search through the
  resource chain", i.e. only that file, not the resource chain.

### 3.14 UnregisterIconRef ($08)

`OSErr UnregisterIconRef(OSType creator, OSType iconType)`: [H]: "Removes
the specified icon from the icon cache (if there are no users of it). If
some clients are using this iconRef, then the IconRef will be removed when
the last user calls ReleaseIconRef." [R03]: "The specified icon data is
not unregistered until all its users have called the function
ReleaseIconRef. You should not unregister an icon that you have not
registered." Open points: 1.3.

### 3.15 UpdateIconRef ($09)

`OSErr UpdateIconRef(IconRef theIconRef)`: [H]: "Call this routine to
force an update of the data for iconRef. For example after changing an
icon in the desktop database or changing the custom icon of a file. Note
that after _adding_ a custom icon to file or folder, you need to call
GetIconRefFromFile() to get a new IconRef specific to this file. This
routine does nothing if the IconRef is a registered icon." [R03]: "Do not
call the UpdateIconRef function if you have not already obtained an
IconRef for a particular icon; call the function GetIconRefFromFile
instead." Behaviour: drop the cached bitmaps so they are re-read from the
source (desktop database, custom icon resources) next time; IconRef value
and owners unchanged. "does nothing" for registered icons: result `noErr`
presumably. Errors: `invalidIconRefErr`.

### 3.16 OverrideIconRefFromResource ($2A)

`OSErr OverrideIconRefFromResource(IconRef theIconRef, const FSSpec
*resourceFile, SInt16 resourceID)`: see 1.8. in `resourceID`: "This value
must be non-zero." [R03]. Errors: `invalidIconRefErr`; missing resource
(`resNotFound`? `noIconDataAvailableErr`?) **UNSPECIFIED**.

### 3.17 OverrideIconRef ($2B)

`OSErr OverrideIconRef(IconRef oldIconRef, IconRef newIconRef)`: see 1.8.
[R03] calls both parameters "A pointer to a value of type IconRef", but
the prototype passes IconRefs by value (the header is authoritative).
Errors: `invalidIconRefErr` for either.

### 3.18 RemoveIconRefOverride ($1E)

`OSErr RemoveIconRefOverride(IconRef theIconRef)`: see 1.8. Errors:
`invalidIconRefErr`; no override present: **UNSPECIFIED** (`noErr`
presumably).

### 3.19 CompositeIconRef ($14)

`OSErr CompositeIconRef(IconRef backgroundIconRef, IconRef
foregroundIconRef, IconRef *compositeIconRef)`: in "A value to use as the
background for the composite IconRef." / "A value to use as the foregound
for the composite IconRef."; out "On completion, this points to an IconRef
that is a composite of the specified background and foreground IconRefs."
[R03]. See 1.9.

### 3.20 IsIconRefComposite ($1A)

`OSErr IsIconRefComposite(IconRef compositeIconRef, IconRef
*backgroundIconRef, IconRef *foregroundIconRef)`: outs "the IconRef value
that forms the background/foreground of the IconRef specified in the
compositeIconRef parameter. If the IconRef specified in the
compositeIconRef parameter is not a composite, the return value is 0."
[R03]. Not a composite: `noErr` with both 0 (by the header text). Errors:
`invalidIconRefErr`.

### 3.21 IsValidIconRef ($32)

`Boolean IsValidIconRef(IconRef theIconRef)`: "Return true if the iconRef
passed in is a valid icon ref" [H]. Must not crash on garbage values (0,
released IconRefs, random pointers): the whole point of the call; so the
implementation should look IconRefs up in a table rather than dereference
them. (That an arbitrary value is safe to pass is not stated:
**UNSPECIFIED**, but required for a robust implementation.)

### 3.22 PlotIconRef ($0E)

`OSErr PlotIconRef(const Rect *theRect, IconAlignmentType align,
IconTransformType transform, IconServicesUsageFlags flags, IconRef
theIconRef)`

- [H]: "This routine plots the IconRef. It mostly takes the same
  parameters as PlotIconSuite. Pass kIconServicesNormalUsageFlag as a
  default value for IconServicesUsageFlags." [R03]: theRect "A pointer to
  the rectangle where the icon is to be drawn.", align "how Icon Services
  should align the icon within the rectangle.", transform "how Icon
  Services should modify the appearance of the icon." "This function is
  similar to the Icon Utilities function PlotIconSuite."
- Draws into the current port, local coordinates, size/depth chosen as in
  1.12, deep mask blended (TN1142: transparency 0–255), label from the
  transform's label bits (1.10).
- [G] "Note: The introduction of deep masks means that you cannot simply
  draw over an icon and assume the previous icon will be erased. Call the
  function IconRefToRgn to determine the area occupied by the current
  icon, erase that area, then draw the new icon."
- Errors: `invalidIconRefErr`; `noIconDataAvailableErr` when no bitmaps
  can be found (e.g. resource file gone): **UNSPECIFIED**. How
  `kTransformSelected`, `Disabled`, `Offline`, `Open` look with deep icons
  (the classic Icon Utilities darken/dim rules in [IM]):
  **UNSPECIFIED** for 32-bit data.

### 3.23 PtInIconRef ($0F)

`Boolean PtInIconRef(const Point *testPt, const Rect *iconRect,
IconAlignmentType align, IconServicesUsageFlags flags, IconRef
theIconRef)`: "This routine indicates if testPt would hit the icon
designated by iconRef. It mostly takes the same parameters as
PtInIconSuite." [H]. Note `testPt` is a pointer (PtInIconSuite takes a
Point by value). "testPt: A pointer to the location, specified in local
coordinates of the current graphics port", "Use the same Rect value as
when the icon was last drawn." Result: "true if the point specified in the
testPt parameter falls within the appropriate icon mask, false otherwise."
"Icon Services uses the icon's black-and-white mask for hit-testing, even
if you provide a deep mask." [R03] Invalid IconRef: false (presumably).

### 3.24 RectInIconRef ($10)

`Boolean RectInIconRef(const Rect *testRect, const Rect *iconRect,
IconAlignmentType align, IconServicesUsageFlags flags, IconRef
theIconRef)`: "This routine indicates if testRect would intersect the icon
designated by iconRef. It mostly takes the same parameters as
RectInIconSuite." [H]; "true if the rectangle specified in the testRect
parameter intersects the appropriate icon mask, false otherwise." [R03];
1-bit mask, as 3.23.

### 3.25 IconRefToRgn ($11)

`OSErr IconRefToRgn(RgnHandle theRgn, const Rect *iconRect,
IconAlignmentType align, IconServicesUsageFlags flags, IconRef
theIconRef)`: "This routine returns a region for the icon. It mostly
takes the same parameters as IconSuiteToRgn." [H]; "You must call the
QuickDraw function NewRegion to allocate memory for the region handle
before calling the IconRefToRgn function." "Icon Services uses the
rectangle and alignment values to automatically select the icon used to
generate the region data." "Icon Services uses the icon's black-and-white
mask to determine the region data, even if you provide a deep mask."
[R03] Errors: `invalidIconRefErr`; `noMaskFoundErr` (-1000, the Icon
Utilities' error) when no mask exists: **UNSPECIFIED**.

### 3.26 GetIconSizesFromIconRef ($12)

`OSErr GetIconSizesFromIconRef(IconSelectorValue iconSelectorInput,
IconSelectorValue *iconSelectorOutputPtr, IconServicesUsageFlags flags,
IconRef theIconRef)`: [H]: "This routine returns an IconSelectorValue
indicating the depths and sizes of icon data which are actually
available. It takes an IconSelectorValue indicating which sizes/depths the
caller is interested and returns an IconSelectorValue indicating which
sizes/depths exist. Caution: This is potentially an extremely expensive
call as IconServices may be forced to attempt fetching the data for the
IconRef's sizes/depths which may result in hitting the local disk or even
the network". Output is a subset of the input (presumably `input & have`).

### 3.27 FlushIconRefs ($29)

`OSErr FlushIconRefs(OSType creator, OSType iconType)`: [H]: "Making this
call will dispose of all the data for the specified icons if the data can
be reacquired, for example if the data is provided from a resource. '****'
is a wildcard for all types or all creators." [R03]: "Reclaims memory used
by the specified icon if the memory is purgeable." IconRefs stay valid;
only bitmaps are dropped. Data registered from an icon family "cannot be
purged" (3.12), so it is kept. [MT]: "If the icon is registered with a
type and creator, FlushIconRefs() should be called." (after changing it),
which suggests the next use re-reads the source. No matching icons:
`noErr` presumably.

### 3.28 FlushIconRefsByVolume ($18)

`OSErr FlushIconRefsByVolume(SInt16 vRefNum)`: [H]: "This routine disposes
of the data for the icons related to the indicated volume if this data can
be reacquired, for example if the data is provided from a resource." [R03]:
"Calling this function locks the bitmap data of all IconRefs with non-zero
reference counts (that is, all IconRefs that are in use) on the volume.
The Finder normally maintains a number of IconRefs with non-zero reference
counts, so you should use the function FlushIconRefs instead of the
FlushIconRefsByVolume function whenever feasible." [G]: "Using the the
FlushIconRefsByVolume function makes the icon data of all currently used
IconRefs non-purgeable. This means any subsequent efforts to flush icon
data will be much less effective." So: in-use IconRefs of that volume are
reloaded (from their file/desktop database) and their data then held
locked/non-purgeable; unused ones are freed. Exactly what "locks" means
here (reload now, or mark non-purgeable): **UNSPECIFIED**. [MT]: "You
should also call FlushIconRefsByVolume() if you change the icon of a file
or folder. This allows Icon Services to update the cache on the specified
volume".

### 3.29 SetCustomIconsEnabled ($1F)

`OSErr SetCustomIconsEnabled(SInt16 vRefNum, Boolean enableCustomIcons)`:
1.11.

### 3.30 GetCustomIconsEnabled ($20)

`OSErr GetCustomIconsEnabled(SInt16 vRefNum, Boolean *customIconsEnabled)`:
"On return, customIconsEnabled points to the value true if custom icons are
enabled on the volume specified or false if custom icons are disabled on
the volume specified." [R03]. 1.11.

### 3.31 IsIconRefMaskEmpty ($33)

`Boolean IsIconRefMaskEmpty(IconRef iconRef)`: "Returns true if the mask
for this icon is blank" [H]. Which mask (each size's 1-bit mask, all must
be blank? the largest?): **UNSPECIFIED**. Invalid IconRef: **UNSPECIFIED**
(false? true?).

### 3.32 GetIconRefVariant ($34, 9.0)

`IconRef GetIconRefVariant(IconRef inIconRef, OSType inVariant,
IconTransformType *outTransform)`: [H]: "Icon variants allows different
images to be used with different icon state. For example, the 'open'
variant for a folder could be represented with an open folder. Given an
icon ref and a desired variant, this routine returns an icon ref (which
may be the same as the input icon ref) and a transformation which should
be used with PlotIconRef() to render the icon appropriately. The returned
icon ref should be used to do hit-testing." Variants: `'tile'`, `'over'`,
`'drop'`, `'open'`, `'odrp'`. [R03]: "Icon variants give you a simple way
to indicate a temporary change of state by changing an icon's appearance.
For example, if you specify the kDropIconVariant value when the user drags
over a valid drop target, the GetIconVariant function provides the
appropriate data for you to plot the variant". Natural behaviour: if the
family has a variant element, an IconRef for it and `kTransformNone`;
otherwise the input IconRef and a transform (`'open'` ->
`kTransformOpen`, others `kTransformNone`?). **UNSPECIFIED**: the transform
for each variant without data; whether the returned IconRef carries a new
owner (it is returned as a function result, not via an `OSErr` call, so
probably not; must test with `GetIconRefOwners`); result for an invalid
IconRef (0?).

### 3.33 RegisterIconRefFromIconFile ($35, 9.0)

`OSErr RegisterIconRefFromIconFile(OSType creator, OSType iconType, const
FSSpec *iconFile, IconRef *theIconRef)`: [H]: "This routine adds a new
entry to the IconRef registry. Other clients will be able to access it
using the (creator, iconType) pair specified here. Lower-case creators are
reserved for the system. If the creator is kSystemIconsCreator and the
iconType is 0, a new IconRef is always returned. Otherwise, if the creator
and type have already been registered, the previously registered IconRef
is returned. This routine increments the reference count of the IconRef."
in `iconFile`: "A pointer to the file system specification structure for
the file to use as the icon data source." [R03] Data purgeable ([G]). So
(`'macs'`, 0) is an anonymous registration: a fresh IconRef each call, not
findable by `GetIconRef`. Errors: file errors, a file that is not an
icon family (**UNSPECIFIED** which code).

### 3.34 ReadIconFile ($36, 9.0)

`OSErr ReadIconFile(const FSSpec *iconFile, IconFamilyHandle
*iconFamily)`: [H]: "Read the specified icon file into the icon family
handle. The caller is responsible for disposing the iconFamily". [R03]:
"A handle to an iconFamily data structure to be used as the target data
structure. Icon Services resizes the handle as needed." The two texts
differ on whether `*iconFamily` must be an existing handle (resized) or is
created (caller disposes). **UNSPECIFIED**; accept both: if
`*iconFamily` is a valid handle resize it, else allocate one (test which
Apple does with nil on input).

### 3.35 WriteIconFile ($37, 9.0)

`OSErr WriteIconFile(IconFamilyHandle iconFamily, const FSSpec
*iconFile)`: "Write the iconFamily handle to the specified file" [H];
"Copies data from a given icon family into a file." [R03]. **UNSPECIFIED**:
whether the file must exist or is created (and with which type/creator,
presumably `'icns'`), whether the data fork is truncated to the family's
size.

---

## 4. Error codes

From `MacErrors.h` (UI 3.4) and the result code table [R03]/[R07] ("The
table below shows the most common result codes returned by Icon Services
and Utilities."):

| Name | Value | Text |
|---|---|---|
| `noMaskFoundErr` | -1000 | `/*Icon Utilties Error*/` (MacErrors.h); no description in [R03] |
| `invalidIconRefErr` | -2580 | "The icon ref is not valid" (MacErrors.h); "The IconRef is not valid." [R03] |
| `noSuchIconErr` | -2581 | "The requested icon could not be found" |
| `noIconDataAvailableErr` | -2582 | "The necessary icon data is not available" |

(`MacErrors.h` groups -2580..-2582 under "Icon Services error codes".)
General codes the routines can be expected to pass through, not listed by
the Icon Services documentation: `paramErr` -50, `memFullErr` -108,
`resNotFound` -192, `fnfErr` -43, `nsvErr` -35, `dirNFErr` -120. The
unrelated `afpIconTypeError` -5030 is AppleShare's.

---

## 5. Sources

- `Icons.h`, `Icons.r`, `MacErrors.h`, `Finder.h`, `Folders.h`,
  `DriverGestalt.h`, `Gestalt.h`, `Traps.h`: Universal Interfaces 3.4,
  `/home/mick/opt/retro68/build/toolchain/universal/CIncludes/` and
  `.../RIncludes/` (CR line endings, MacRoman).
- Icon Services and Utilities Reference (Carbon), February 2003,
  per-group pages, via the Wayback Machine:
  `http://developer.apple.com/documentation/Carbon/Reference/IconServices/icon_services/function_group_N.html`
  (N = 8 custom icons, 9 flushing, 14 reference counting, 15 modifying,
  16 obtaining icon data, 17 obtaining IconRefs, 19 reading/converting, 20
  registering, 21 using IconRef data, 24 unsupported (IconServicesInit),
  6 PlotIconSuite), `.../icon_services/ResultCodes.html`
  (web.archive.org/web/20031226121545), `.../icon_services/ConstantsIndex.html`,
  `.../icon_services/toc.html`; captures 2003-08 to 2004-06 (CDX listing:
  `http://web.archive.org/cdx/search/cdx?url=developer.apple.com/documentation/Carbon/Reference/IconServices/*`).
- Icon Services and Utilities Reference, 2007-04-06 edition:
  `http://web.archive.org/web/20090101045738/http://developer.apple.com/documentation/Carbon/Reference/IconServices/Reference/reference.html`,
  and its deprecation appendix
  `http://web.archive.org/web/20080905184150/http://developer.apple.com/documentation/Carbon/Reference/IconServices/DeprecationAppendix/AppendixADeprecatedAPI.html`.
  (Not on today's developer.apple.com archive: `documentation/Carbon/Reference/IconServices/...` is 404.)
- Obtaining and Using Icons With Icon Services (Legacy), 1999-12-01 / 2003-02-01:
  https://developer.apple.com/library/archive/documentation/Carbon/Conceptual/Icon_Service_nd_Utilities/02concepts/concepts.html,
  https://developer.apple.com/library/archive/documentation/Carbon/Conceptual/Icon_Service_nd_Utilities/03tasks/tasks.html
  (TOC: `.../Icon_Service_nd_Utilities/book.json`).
- Technical Note TN1142 "Mac OS 8.5" (Icon Services, Finder, Device
  Manager sections): https://developer.apple.com/library/archive/technotes/tn/tn1142.html
  (TN1142 is the general Mac OS 8.5 note, not an Icon Services note.)
- Apple's Mac OS 8.5 developer page, 1999, linking the preliminary draft
  `pdf/IconSvcs.pdf` (not archived):
  `http://web.archive.org/web/19990210092455/http://developer.apple.com/macos/8.5.html`.
- Inside Macintosh: More Macintosh Toolbox, chapter 5 Icon Utilities
  (1996): https://developer.apple.com/library/archive/documentation/mac/MoreToolbox/MoreToolbox-273.html
  and https://developer.apple.com/library/archive/documentation/mac/pdf/More_Mac_Toolbox/Icon_Utilities.pdf.
- Third-party, observed behaviour only: Craig Hockenberry, "Tune-up your
  Application with Icon Services", MacTech 15.05 (1999):
  http://preserve.mactech.com/articles/mactech/Vol.15/15.05/IconServicesInApps/index.html

---

## 6. Tests to run on Mac OS 9.0.4 (the UNSPECIFIEDs, by priority)

1. Registry ownership: `RegisterIconRefFromIconFamily(c,t)`, owners;
   `ReleaseIconRef`; `IsValidIconRef`; `GetIconRef(c,t)`; owners;
   `UnregisterIconRef(c,t)` while owned, then `GetIconRef(c,t)`; register
   the same pair twice with different data (same IconRef? whose bitmaps?).
2. `GetIconRef` lookup and fallback: (`'macs'`, every 1.4.1 type) on 9.0.4
   (which exist); unknown creator with 'APPL', 'TEXT', 'xxxx'; known
   application creator (desktop database); result codes and
   `IconRefToIconFamily` dumps (sizes present) for each.
3. `GetIconRefFromFile` on: plain document, application, custom-icon file
   (classic only, `'icns'` only), custom-icon flag without resources,
   alias (file, folder, volume), stationery, folder, special folders
   (System, Extensions, Control Panels, Fonts, Apple Menu Items,
   Preferences), the Trash (empty and full), volume root, a floppy/CD
   image; with labels 0..7 (form of `theLabel`); identity of the
   returned IconRefs against `GetIconRef`/`GetIconRefFromFolder`.
4. `GetIconRefFromFolder` with `attributes`/`accessPrivileges` bit sweeps.
5. Owner counts after `CompositeIconRef`, `IsIconRefComposite`,
   `GetIconRefVariant`, `OverrideIconRef` (on both refs).
6. Family format: `IconRefToIconFamily(kSelectorAllAvailableData)` byte
   dumps (order, compressed 32-bit sizes); `SetIconFamilyData` with known
   32-bit pixels then the element bytes (compression), and with `'PICT'`;
   `GetIconFamilyData` for a missing type (error, size).
7. Error codes: every routine with IconRef 0 and a released IconRef.
