/* MacPhoenix: the Text Services Manager's trap ($AA54), as far as Apple's
 * Help Manager needs it, and its document calls for a TSM without input
 * methods (TextServices.yaml). */
#include <base/common.h>
#include <rsys/process.h>
#include <TextServices.h>
#include <WindowMgr.h>
#include <ProcessMgr.h>
#include <MemoryMgr.h>

#include <set>

using namespace Executor;

INTEGER Executor::C_TSMFindWindow(Point pt, GUEST<WindowPtr> *window)
{
    (void)pt;
    *window = nullptr;
    return 0;
}

/* OSDispatch $64 (ProcessMgr.yaml): the same through the Process Manager. */
INTEGER Executor::C_FindInputWindow(Point pt, GUEST<WindowPtr> *window)
{
    return C_TSMFindWindow(pt, window);
}

OSErr Executor::C_TSMInputWindowOp(Ptr record, INTEGER op)
{
    (void)record, (void)op;
    return paramErr;
}

OSErr Executor::C_InitTSMAwareApplication()
{
    return noErr;
}

OSErr Executor::C_CloseTSMAwareApplication()
{
    return noErr;
}

/* A document is a small block of the caller's heap holding its refcon (an
   input method would find it there); kept so DeleteTSMDocument knows its
   own. No input method ever takes an event, a menu or the cursor. */
namespace
{
std::set<uint32_t> tsm_documents;
constexpr OSErr tsmInvalidDocIDErr = -2507;
bool is_document(TSMDocumentID id)
{
    return id && tsm_documents.count(US_TO_SYN68K(id));
}
}

OSErr Executor::C_NewTSMDocument(INTEGER numOfInterface, GUEST<OSType> *supportedInterfaces,
                                 GUEST<TSMDocumentID> *idocID, LONGINT refcon)
{
    if(!idocID || numOfInterface < 1 || !supportedInterfaces)
        return paramErr;
    Ptr doc = NewPtrClear(8);
    if(!doc)
        return memFullErr;
    *(GUEST<LONGINT> *)doc = refcon;
    tsm_documents.insert(US_TO_SYN68K(doc));
    *idocID = doc;
    return noErr;
}

OSErr Executor::C_DeleteTSMDocument(TSMDocumentID idocID)
{
    if(!is_document(idocID))
        return tsmInvalidDocIDErr;
    tsm_documents.erase(US_TO_SYN68K(idocID));
    DisposePtr(idocID);
    return noErr;
}

OSErr Executor::C_ActivateTSMDocument(TSMDocumentID idocID)
{
    return is_document(idocID) ? noErr : tsmInvalidDocIDErr;
}

OSErr Executor::C_DeactivateTSMDocument(TSMDocumentID idocID)
{
    return is_document(idocID) ? noErr : tsmInvalidDocIDErr;
}

OSErr Executor::C_FixTSMDocument(TSMDocumentID idocID)
{
    return is_document(idocID) ? noErr : tsmInvalidDocIDErr;
}

OSErr Executor::C_UseInputWindow(TSMDocumentID idocID, Boolean useWindow)
{
    (void)useWindow;
    return !idocID || is_document(idocID) ? noErr : tsmInvalidDocIDErr;
}

Boolean Executor::C_TSMEvent(EventRecord *event)
{
    (void)event;
    return false;
}

Boolean Executor::C_TSMMenuSelect(LONGINT menuResult)
{
    (void)menuResult;
    return false;
}

Boolean Executor::C_SetTSMCursor(Point mousePos)
{
    (void)mousePos;
    return false;
}
