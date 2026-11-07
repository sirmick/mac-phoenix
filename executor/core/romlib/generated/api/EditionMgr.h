/*****************************************************************************
 *                                                                           *
 *                                EditionMgr.h                               *
 *                                ============                               *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    Handle, SignedByte, ScriptCode, Str31, OSType,          *
 *                   Boolean, ResType, Ptr, OSErr, INTEGER, Size             *
 *     AliasMgr.h    AliasHandle                                             *
 *     FileMgr.h     FSSpec, FSSpecPtr                                       *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "AliasMgr.h"
#include "FileMgr.h"

#define MODULE_NAME EditionMgr
#include <base/api-module.h>


namespace Executor {

typedef int32_t TimeStamp;

typedef Handle EditionRefNum;

typedef int16_t UpdateMode;

typedef SignedByte SectionType;

typedef char FormatType[4];

typedef ProcPtr ExpDialogHookUPP;

typedef ProcPtr ExpModalFilterUPP;

typedef ProcPtr FormatIOUPP;

typedef ProcPtr EditionOpenerUPP;

struct SectionRecord{GUEST_STRUCT;SignedByte version;SectionType kind;GUEST<UpdateMode> mode;GUEST<TimeStamp> mdDate;GUEST<int32_t> sectionID;GUEST<int32_t> refCon;GUEST<AliasHandle> alias;GUEST<int32_t> subPart;GUEST<Handle> nextSection; // ### Section
GUEST<Handle> controlBlock;GUEST<EditionRefNum> refNum;};static_assert(sizeof(SectionRecord) == 36);void describeStruct(const SectionRecord&, std::ostream&);

typedef SectionRecord *SectionPtr;

typedef GUEST<SectionPtr> *SectionHandle;

struct EditionContainerSpec{GUEST_STRUCT;FSSpec theFile;GUEST<ScriptCode> theFileScript;GUEST<int32_t> thePart;GUEST<Str31> thePartName;GUEST<ScriptCode> thePartScript;};void describeStruct(const EditionContainerSpec&, std::ostream&);

struct EditionContainerSpec;static_assert(sizeof(EditionContainerSpec) == 110);

typedef EditionContainerSpec *EditionContainerSpecPtr;

struct EditionInfoRecord{GUEST_STRUCT;GUEST<TimeStamp> crDate;GUEST<TimeStamp> mdDate;GUEST<OSType> fdCreator;GUEST<OSType> fdType;EditionContainerSpec container;};void describeStruct(const EditionInfoRecord&, std::ostream&);

struct EditionInfoRecord;static_assert(sizeof(EditionInfoRecord) == 126);

typedef EditionInfoRecord *EditionInfoPtr;

struct NewPublisherReply{GUEST_STRUCT;Boolean canceled;Boolean replacing;Boolean usePart;uint8_t _filler;GUEST<Handle> preview;GUEST<FormatType> previewFormat;EditionContainerSpec container;};void describeStruct(const NewPublisherReply&, std::ostream&);

struct NewPublisherReply;static_assert(sizeof(NewPublisherReply) == 122);

typedef NewPublisherReply *NewPublisherReplyPtr;

struct NewSubscriberReply{GUEST_STRUCT;Boolean canceled;SignedByte formatsMask;EditionContainerSpec container;};void describeStruct(const NewSubscriberReply&, std::ostream&);

struct NewSubscriberReply;static_assert(sizeof(NewSubscriberReply) == 112);

typedef NewSubscriberReply *NewSubscriberReplyPtr;

struct SectionOptionsReply{GUEST_STRUCT;Boolean canceled;Boolean changed;GUEST<SectionHandle> sectionH;GUEST<ResType> action;};void describeStruct(const SectionOptionsReply&, std::ostream&);

struct SectionOptionsReply;static_assert(sizeof(SectionOptionsReply) == 10);

typedef SectionOptionsReply *SectionOptionsReplyPtr;

typedef uint8_t EditionOpenerVerb;

enum {eoOpen = 0,
eoClose = 1,
eoOpenNew = 2,
eoCloseNew = 3,
eoCanSubscribe = 4,
};

struct EditionOpenerParamBlock{GUEST_STRUCT;EditionInfoRecord info;GUEST<SectionHandle> sectionH;GUEST<FSSpecPtr> document;GUEST<OSType> fdCreator;GUEST<int32_t> ioRefNum;GUEST<FormatIOUPP> ioProc;Boolean success;SignedByte formatsMask;};void describeStruct(const EditionOpenerParamBlock&, std::ostream&);

struct EditionOpenerParamBlock;static_assert(sizeof(EditionOpenerParamBlock) == 148);

typedef EditionOpenerParamBlock *EditionOpenerParamBlockPtr;

typedef uint8_t FormatIOVerb;

enum {ioHasFormat = 0,
ioReadFormat = 1,
ioNewFormat = 2,
ioWtriteFormat = 3,
};

struct FormatIOParamBlock{GUEST_STRUCT;GUEST<int32_t> ioRefNum;GUEST<FormatType> format;GUEST<int32_t> formatIndex;GUEST<int32_t> offset;GUEST<Ptr> buffPtr;GUEST<int32_t> buffLen;};void describeStruct(const FormatIOParamBlock&, std::ostream&);

struct FormatIOParamBlock;static_assert(sizeof(FormatIOParamBlock) == 24);

typedef FormatIOParamBlock *FormatIOParamBlockPtr;

enum {flLckedErr = -45,
fBusyErr = -47,
userCanceledErr = -128,
editionMgrInitErr = -450,
badSectionErr = -451,
notRegisteredSectionErr = -452,
badSubPartErr = -454,
multiplePubliserWrn = -460,
containerNotFoundWrn = -461,
notThePublisherWrn = -463,
};

DISPATCHER_TRAP(Pack11, 0xA82D, D0W);


OSErr C_InitEditionPackVersion(INTEGER unused);PASCAL_SUBTRAP(InitEditionPackVersion, 0xA82D, 0x0100, Pack11);


OSErr C_NewSection(EditionContainerSpecPtr container, FSSpecPtr section_doc, SectionType kind, int32_t section_id, UpdateMode initial_mode, GUEST<SectionHandle> *section_out);PASCAL_SUBTRAP(NewSection, 0xA82D, 0x0A02, Pack11);


OSErr C_RegisterSection(FSSpecPtr section_doc, SectionHandle section, Boolean *alias_was_updated_p_out);PASCAL_SUBTRAP(RegisterSection, 0xA82D, 0x0604, Pack11);


OSErr C_UnRegisterSection(SectionHandle section);PASCAL_SUBTRAP(UnRegisterSection, 0xA82D, 0x0206, Pack11);


OSErr C_IsRegisteredSection(SectionHandle section);PASCAL_SUBTRAP(IsRegisteredSection, 0xA82D, 0x0208, Pack11);


OSErr C_AssociateSection(SectionHandle section, FSSpecPtr new_section_doc);PASCAL_SUBTRAP(AssociateSection, 0xA82D, 0x040C, Pack11);


OSErr C_CreateEditionContainerFile(FSSpecPtr edition_file, OSType creator, ScriptCode edition_file_name_script);PASCAL_SUBTRAP(CreateEditionContainerFile, 0xA82D, 0x050E, Pack11);


OSErr C_DeleteEditionContainerFile(FSSpecPtr edition_file);PASCAL_SUBTRAP(DeleteEditionContainerFile, 0xA82D, 0x0210, Pack11);


OSErr C_SetEditionFormatMark(EditionRefNum edition, FormatType format, int32_t mark);PASCAL_SUBTRAP(SetEditionFormatMark, 0xA82D, 0x0620, Pack11);


OSErr C_GetEditionFormatMark(EditionRefNum edition, FormatType format, GUEST<int32_t> *currentMark);PASCAL_SUBTRAP(GetEditionFormatMark, 0xA82D, 0x061E, Pack11);


OSErr C_OpenEdition(SectionHandle subscriber_section, GUEST<EditionRefNum> *ref_num);PASCAL_SUBTRAP(OpenEdition, 0xA82D, 0x0412, Pack11);


OSErr C_EditionHasFormat(EditionRefNum edition, FormatType format, GUEST<Size> *format_size);PASCAL_SUBTRAP(EditionHasFormat, 0xA82D, 0x0618, Pack11);


OSErr C_ReadEdition(EditionRefNum edition, FormatType format, Ptr buffer, Size buffer_size);PASCAL_SUBTRAP(ReadEdition, 0xA82D, 0x081A, Pack11);


OSErr C_OpenNewEdition(SectionHandle publisher_section, OSType creator, FSSpecPtr publisher_section_doc, GUEST<EditionRefNum> *ref_num);PASCAL_SUBTRAP(OpenNewEdition, 0xA82D, 0x0814, Pack11);


OSErr C_WriteEdition(EditionRefNum edition, FormatType format, Ptr buffer, Size buffer_size);PASCAL_SUBTRAP(WriteEdition, 0xA82D, 0x081C, Pack11);


OSErr C_CloseEdition(EditionRefNum edition, Boolean success_p);PASCAL_SUBTRAP(CloseEdition, 0xA82D, 0x0316, Pack11);


OSErr C_GetLastEditionContainerUsed(EditionContainerSpecPtr container);PASCAL_SUBTRAP(GetLastEditionContainerUsed, 0xA82D, 0x0226, Pack11);


OSErr C_NewSubscriberDialog(NewSubscriberReplyPtr reply);PASCAL_SUBTRAP(NewSubscriberDialog, 0xA82D, 0x0232, Pack11);


OSErr C_NewPublisherDialog(NewSubscriberReplyPtr reply);PASCAL_SUBTRAP(NewPublisherDialog, 0xA82D, 0x0236, Pack11);


OSErr C_SectionOptionsDialog(SectionOptionsReply *reply);PASCAL_SUBTRAP(SectionOptionsDialog, 0xA82D, 0x023A, Pack11);


OSErr C_NewSubscriberExpDialog(NewSubscriberReplyPtr reply, Point where, int16_t expnasion_ditl_res_id, ExpDialogHookUPP dialog_hook, ExpModalFilterUPP filter_hook, Ptr data);PASCAL_SUBTRAP(NewSubscriberExpDialog, 0xA82D, 0x0B34, Pack11);


OSErr C_NewPublisherExpDialog(NewPublisherReplyPtr reply, Point where, int16_t expnasion_ditl_res_id, ExpDialogHookUPP dialog_hook, ExpModalFilterUPP filter_hook, Ptr data);PASCAL_SUBTRAP(NewPublisherExpDialog, 0xA82D, 0x0B38, Pack11);


OSErr C_SectionOptionsExpDialog(SectionOptionsReply *reply, Point where, int16_t expnasion_ditl_res_id, ExpDialogHookUPP dialog_hook, ExpModalFilterUPP filter_hook, Ptr data);PASCAL_SUBTRAP(SectionOptionsExpDialog, 0xA82D, 0x0B3C, Pack11);


OSErr C_GetEditionInfo(SectionHandle section, EditionInfoPtr edition_info);PASCAL_SUBTRAP(GetEditionInfo, 0xA82D, 0x0422, Pack11);


OSErr C_GoToPublisherSection(EditionContainerSpecPtr container);PASCAL_SUBTRAP(GoToPublisherSection, 0xA82D, 0x0224, Pack11);


OSErr C_GetStandardFormats(EditionContainerSpecPtr container, GUEST<FormatType> *preview_format, Handle preview, Handle publisher_alias, Handle formats);PASCAL_SUBTRAP(GetStandardFormats, 0xA82D, 0x0A28, Pack11);


OSErr C_GetEditionOpenerProc(GUEST<EditionOpenerUPP> *opener);PASCAL_SUBTRAP(GetEditionOpenerProc, 0xA82D, 0x022A, Pack11);


OSErr C_SetEditionOpenerProc(EditionOpenerUPP opener);PASCAL_SUBTRAP(SetEditionOpenerProc, 0xA82D, 0x022C, Pack11);


OSErr C_CallEditionOpenerProc(EditionOpenerVerb selector, EditionOpenerParamBlock *param_block, EditionOpenerUPP opener);PASCAL_SUBTRAP(CallEditionOpenerProc, 0xA82D, 0x052E, Pack11);


OSErr C_CallFormatIOProc(FormatIOVerb selector, FormatIOParamBlock *param_block, FormatIOUPP proc);PASCAL_SUBTRAP(CallFormatIOProc, 0xA82D, 0x0530, Pack11);


} /* namespace Executor */