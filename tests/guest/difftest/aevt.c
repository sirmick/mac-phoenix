/* aevt: Apple events, differential (tools/difftest/run.py aevt): the
   attributes AECreateAppleEvent puts in an event (return and transaction
   IDs, source, interaction level, original address) and the ones the
   Apple Event Manager adds to an event it hands a handler (the 'oapp' the
   Process Manager sends at launch); AESend to kCurrentProcess (handled
   before AESend returns, the reply); the Object Support Library's
   accessor table and callbacks. BBEdit Lite 6.1 needs all three: its
   '****' handler gives up on an event without 'esrc', it makes its
   untitled window by sending itself 'core' 'crel', and that handler looks
   its accessors up. aevt.r: a SIZE that asks for high-level events.
   Process serial numbers and generated return IDs differ between runs:
   printed as self/other and generated/not. */
#include "probe.h"
#include <AppleEvents.h>
#include <Processes.h>
#include <Events.h>
#include <Aliases.h>
#include <string.h>

static const OSType attrs[] = {
    'evcl', 'evid', 'rtid', 'tran', 'addr', 'esrc', 'inte', 'miss', 'optk', 'from', 'timo', 'subj' };

static char hres[48][96];
static char hkey[48][48];
static int hn;

static void hout(const char *key, const char *fmt, ...)
{
    va_list ap;
    if(hn >= 48)
        return;
    strncpy(hkey[hn], key, sizeof hkey[0] - 1);
    va_start(ap, fmt);
    vsprintf(hres[hn], fmt, ap);
    va_end(ap);
    hn++;
}

static void flush_h(void)
{
    int i;
    for(i = 0; i < hn; i++)
        out(hkey[i], "%s", hres[i]);
    hn = 0;
}

static int is_self(const unsigned char *p, long n)
{
    ProcessSerialNumber self;
    Boolean same = false;
    if(n != sizeof self)
        return 0;
    GetCurrentProcess(&self);
    SameProcess(&self, (const ProcessSerialNumber *)p, &same);
    return same;
}

/* one line per attribute: error, type, size, bytes (PSNs as self/other,
   a generated return ID as generated, a TargetID by its port name) */
static void dump_attrs(const char *prefix, const AppleEvent *evt, short asked_rtid)
{
    int i, j;
    for(i = 0; i < (int)(sizeof attrs / sizeof attrs[0]); i++)
    {
        unsigned char buf[64];
        DescType type;
        Size size;
        char key[48], val[96], *p;
        OSErr err;
        memset(buf, SENTINEL, sizeof buf);
        type = 0xA5A5A5A5;
        size = 0xA5A5A5A5;
        err = AEGetAttributePtr(evt, attrs[i], typeWildCard, &type, buf, sizeof buf, &size);
        sprintf(key, "%s.%.4s", prefix, (char *)&attrs[i]);
        if(err)
        {
            hout(key, "err %d", err);
            continue;
        }
        p = val + sprintf(val, "%.4s %ld ", (char *)&type, (long)size);
        if(type == typeProcessSerialNumber)
            sprintf(p, "%s", is_self(buf, size) ? "self" : "other");
        else if(attrs[i] == 'rtid' && asked_rtid == kAutoGenerateReturnID)
            sprintf(p, "%s", size == 4 && *(long *)buf != kAutoGenerateReturnID && *(long *)buf != 0
                       ? "generated" : "not generated");
        else if(type == typeTargetID)
            sprintf(p, "port %.*s", buf[6] < 32 ? buf[6] : 32, (char *)buf + 7);  /* name.name */
        else if(type == typeApplSignature)
            sprintf(p, "%.4s", (char *)buf);
        else
            for(j = 0; j < size && j < 16; j++)
                p += sprintf(p, "%02x", buf[j]);
        hout(key, "%s", val);
    }
}

static int got_events;

static pascal OSErr handler(const AppleEvent *evt, AppleEvent *reply, long refcon)
{
    char prefix[32];
    DescType type;
    Size size;
    OSType cls = 0, id = 0;
    (void)reply;
    (void)refcon;
    AEGetAttributePtr(evt, keyEventClassAttr, typeType, &type, &cls, 4, &size);
    AEGetAttributePtr(evt, keyEventIDAttr, typeType, &type, &id, 4, &size);
    sprintf(prefix, "recv%d", got_events++);
    hout(prefix, "%.4s %.4s", (char *)&cls, (char *)&id);
    dump_attrs(prefix, evt, 0);
    {
        long n = -1;
        char key[48];
        sprintf(key, "%s.params", prefix);
        AECountItems(evt, &n);
        hout(key, "%ld", n);
        sprintf(key, "%s.reply", prefix);
        hout(key, "%.4s", reply ? (char *)&reply->descriptorType : "none");
    }
    return noErr;
}

/* send: handlers for events the probe sends itself; refcon 2's answers an
   error */
static int in_send;

static pascal OSErr send_handler(const AppleEvent *evt, AppleEvent *reply, long refcon)
{
    char key[48];
    long n = -1;
    sprintf(key, "h%ld", refcon);
    hout(key, "%s", in_send ? "called in AESend" : "called later");
    dump_attrs(key, evt, kAutoGenerateReturnID);
    sprintf(key, "h%ld.reply", refcon);
    if(reply->descriptorType == typeAppleEvent)
    {
        long v = 42;
        AECountItems(reply, &n);
        hout(key, "aevt params %ld", n);
        sprintf(key, "h%ld.reply.put", refcon);
        hout(key, "%d", AEPutParamPtr(reply, keyDirectObject, typeLongInteger, &v, 4));
    }
    else
        hout(key, "%.4s", (char *)&reply->descriptorType);
    return refcon == 2 ? -1234 : noErr;
}

static void send_case(int n, OSType id, AESendMode mode)
{
    /* kCurrentProcess: dispatched at once (the process's own serial
       number goes through the queue: on 8.6 AESend then waits out its
       timeout, -1712) */
    ProcessSerialNumber self = { 0, kCurrentProcess };
    AEAddressDesc target;
    AppleEvent evt, reply;
    char key[32];
    OSErr err;
    AECreateDesc(typeProcessSerialNumber, &self, sizeof self, &target);
    AECreateAppleEvent('Test', id, &target, kAutoGenerateReturnID, kAnyTransactionID, &evt);
    memset(&reply, SENTINEL, sizeof reply);
    in_send = 1;
    err = AESend(&evt, &reply, mode, kAENormalPriority, kAEDefaultTimeout, NULL, NULL);
    in_send = 0;
    flush_h();
    sprintf(key, "send%d", n);
    out(key, "%d", err);
    sprintf(key, "send%d.reply", n);
    if(reply.descriptorType == typeAppleEvent)
    {
        long v = 0, errn = 0, cnt = -1;
        DescType type;
        Size size;
        AECountItems(&reply, &cnt);
        out(key, "aevt params %ld", cnt);
        sprintf(key, "send%d.reply.----", n);
        err = AEGetParamPtr(&reply, keyDirectObject, typeLongInteger, &type, &v, 4, &size);
        out(key, err ? "err %ld" : "%ld", err ? (long)err : v);
        sprintf(key, "send%d.reply.errn", n);
        err = AEGetParamPtr(&reply, keyErrorNumber, typeLongInteger, &type, &errn, 4, &size);
        out(key, err ? "err %ld" : "%ld", err ? (long)err : errn);
        sprintf(key, "send%d.reply", n);
        dump_attrs(key, &reply, kAutoGenerateReturnID);
        flush_h();
        {
            long a = 0, b = 1;
            AEGetAttributePtr(&evt, keyReturnIDAttr, typeLongInteger, &type, &a, 4, &size);
            AEGetAttributePtr(&reply, keyReturnIDAttr, typeLongInteger, &type, &b, 4, &size);
            sprintf(key, "send%d.reply.rtid.same", n);
            out(key, "%s", a == b ? "yes" : "no");
        }
        AEDisposeDesc(&reply);
    }
    else
        out(key, "%.4s", (char *)&reply.descriptorType);
    AEDisposeDesc(&evt);
    AEDisposeDesc(&target);
}

/* osl: the Object Support Library's accessor table and callbacks */
static pascal OSErr osl_accessor(DescType desiredClass, const AEDesc *container,
                                 DescType containerClass, DescType form,
                                 const AEDesc *selectionData, AEDesc *value, long refcon)
{
    static int calls;
    char key[32];
    (void)container;
    (void)selectionData;
    sprintf(key, "accessor%d", ++calls);
    hout(key, "%.4s in %.4s form %.4s refcon %ld", (char *)&desiredClass,
         (char *)&containerClass, (char *)&form, refcon);
    return AECreateDesc(typeLongInteger, &refcon, sizeof refcon, value);
}

static int disposed;

static pascal OSErr osl_dispose(AEDesc *token)
{
    disposed++;
    hout("dispose", "%.4s", (char *)&token->descriptorType);
    return AEDisposeDesc(token);
}

static void osl_call(const char *key, DescType cls, DescType container)
{
    AEDesc null_token, data, token;
    long k = 1;
    char kk[48];
    OSErr err;
    AECreateDesc(typeNull, NULL, 0, &null_token);
    AECreateDesc(typeLongInteger, &k, sizeof k, &data);
    memset(&token, SENTINEL, sizeof token);
    err = AECallObjectAccessor(cls, &null_token, container, 'indx', &data, &token);
    flush_h();
    out(key, "%d", err);
    sprintf(kk, "%s.token", key);
    if(err == noErr && token.descriptorType == typeLongInteger)
    {
        out(kk, "long %ld", **(long **)token.dataHandle);
        AEDisposeDesc(&token);
    }
    else
        out(kk, "%.4s", (char *)&token.descriptorType);
    AEDisposeDesc(&data);
}

/* AEObjects.h's, which the Multiversal headers don't have yet */
#define kAEDesiredClassKey 'want'
#define kAEContainerKey 'from'
#define kAEKeyFormKey 'form'
#define kAEKeyDataKey 'seld'
#define kObjectSpecifierType 'obj '
#define kAbsolutePositionForm 'indx'
#define kNameForm 'name'
#define kNoSuchObjectErr (-1728)

/* resolve: AEResolve walking a specifier's containers to the accessors */
static int resolve_calls;
static const char *resolve_key = "";

static pascal OSErr resolve_accessor(DescType desiredClass, const AEDesc *container,
                                     DescType containerClass, DescType form,
                                     const AEDesc *selectionData, AEDesc *value, long refcon)
{
    char key[48], sel[40];
    long token;
    sprintf(key, "%s.accessor%d", resolve_key, ++resolve_calls);
    if(selectionData->descriptorType == typeLongInteger)
        sprintf(sel, "long %ld", **(long **)selectionData->dataHandle);
    else if(selectionData->descriptorType == typeChar)
        sprintf(sel, "TEXT %.*s", (int)GetHandleSize(selectionData->dataHandle),
                *selectionData->dataHandle);
    else
        sprintf(sel, "%.4s", (char *)&selectionData->descriptorType);
    hout(key, "%.4s in %.4s (token %.4s %ld) form %.4s seld %s refcon %ld", (char *)&desiredClass,
         (char *)&containerClass, (char *)&container->descriptorType,
         container->descriptorType == typeLongInteger ? **(long **)container->dataHandle : 0L,
         (char *)&form, sel, refcon);
    if(refcon == 99)
        return kNoSuchObjectErr;
    token = refcon * 100 + (selectionData->descriptorType == typeLongInteger
                            ? **(long **)selectionData->dataHandle : 0);
    return AECreateDesc(typeLongInteger, &token, sizeof token, value);
}

static pascal OSErr resolve_dispose(AEDesc *token)
{
    char key[48];
    sprintf(key, "%s.dispose%d", resolve_key, ++resolve_calls);
    hout(key, "%.4s %ld", (char *)&token->descriptorType,
         token->descriptorType == typeLongInteger ? **(long **)token->dataHandle : 0L);
    return AEDisposeDesc(token);
}

/* an object specifier made as CreateObjSpecifier makes one: a record of
   want, from, form, seld coerced to 'obj ' */
static void make_spec(DescType cls, const AEDesc *from, DescType form, const AEDesc *seld, AEDesc *spec)
{
    AEDesc rec;
    AECreateList(NULL, 0, true, &rec);
    AEPutParamPtr(&rec, kAEDesiredClassKey, typeType, &cls, sizeof cls);
    AEPutParamDesc(&rec, kAEContainerKey, from);
    AEPutParamPtr(&rec, kAEKeyFormKey, typeEnumerated, &form, sizeof form);
    AEPutParamDesc(&rec, kAEKeyDataKey, seld);
    AECoerceDesc(&rec, kObjectSpecifierType, spec);
    AEDisposeDesc(&rec);
}

static void resolve_case(const char *key, const AEDesc *spec)
{
    AEDesc token;
    char k[48];
    OSErr err;
    token.descriptorType = 'XXXX';
    token.dataHandle = NULL;
    resolve_calls = 0;
    resolve_key = key;
    err = AEResolve(spec, 0, &token);
    flush_h();
    out(key, "%d", err);
    sprintf(k, "%s.token", key);
    if(err == noErr && token.descriptorType == typeLongInteger)
        out(k, "long %ld", **(long **)token.dataHandle);
    else
        out(k, "%.4s", (char *)&token.descriptorType);
    if(err == noErr)
        AEDisposeDesc(&token);
}

static void osl_get(const char *key, DescType cls, DescType container, ProcPtr expect)
{
    ProcPtr p = (ProcPtr)0xA5A5A5A5;
    long refcon = 0xA5A5A5A5;
    OSErr err = AEGetObjectAccessor(cls, container, &p, &refcon, false);
    if(err)
        out(key, "err %d", err);
    else
        out(key, "%s refcon %ld", p == expect ? "ours" : "other", refcon);
}

int main(void)
{
    probe_open("aevt");
    if(SECTION("received"))
    {
        /* the launch's 'oapp', through a '****' '****' handler */
        EventRecord ev;
        long start = TickCount();
        AEEventHandlerUPP upp = NewAEEventHandlerUPP(handler);
        out("install", "%d", AEInstallEventHandler(typeWildCard, typeWildCard, upp, 0, false));
        while(!got_events && TickCount() - start < 600)
            if(WaitNextEvent(everyEvent, &ev, 5, NULL) && ev.what == kHighLevelEvent)
                out("process", "%d", AEProcessAppleEvent(&ev));
        flush_h();
        out("events", "%d", got_events);
        AERemoveEventHandler(typeWildCard, typeWildCard, upp, false);
    }
    if(SECTION("created"))
    {
        /* AECreateAppleEvent with given IDs, then with the automatic ones */
        ProcessSerialNumber self;
        AEAddressDesc target;
        AppleEvent evt;
        GetCurrentProcess(&self);
        AECreateDesc(typeProcessSerialNumber, &self, sizeof self, &target);
        out("create", "%d", AECreateAppleEvent('Test', 'Evnt', &target, 7, 0x1234, &evt));
        dump_attrs("given", &evt, 7);
        flush_h();
        AEDisposeDesc(&evt);
        out("create.auto", "%d", AECreateAppleEvent('Test', 'Evnt', &target, kAutoGenerateReturnID,
                                                    kAnyTransactionID, &evt));
        dump_attrs("auto", &evt, kAutoGenerateReturnID);
        flush_h();
        {
            /* two automatic return IDs: different? */
            AppleEvent evt2;
            long a = 0, b = 0;
            DescType type;
            Size size;
            AECreateAppleEvent('Test', 'Evnt', &target, kAutoGenerateReturnID, kAnyTransactionID, &evt2);
            AEGetAttributePtr(&evt, keyReturnIDAttr, typeLongInteger, &type, &a, 4, &size);
            AEGetAttributePtr(&evt2, keyReturnIDAttr, typeLongInteger, &type, &b, 4, &size);
            out("auto.distinct", "%s", a != b ? "yes" : "no");
            AEDisposeDesc(&evt2);
        }
        {
            /* as descriptors: missing (BBEdit Lite 6.1 asks for 'optk' as a
               list on a new event), coerced, as it is */
            static const struct { OSType key, type; const char *name; } d[] = {
                { 'optk', 'list', "desc.optk" }, { 'esrc', 'long', "desc.esrc" },
                { 'evid', typeWildCard, "desc.evid" }, { 'rtid', 'shor', "desc.rtid" } };
            int i;
            for(i = 0; i < 4; i++)
            {
                AEDesc dd;
                OSErr e;
                memset(&dd, 0xFF, sizeof dd);
                e = AEGetAttributeDesc(&evt, d[i].key, d[i].type, &dd);
                if(e)
                    out(d[i].name, "err %d type %.4s handle %s", e, (char *)&dd.descriptorType,
                        dd.dataHandle ? (dd.dataHandle == (Handle)-1 ? "unchanged" : "set") : "nil");
                else
                {
                    out(d[i].name, "%.4s %ld", (char *)&dd.descriptorType,
                        dd.dataHandle ? (long)GetHandleSize(dd.dataHandle) : -1L);
                    AEDisposeDesc(&dd);
                }
            }
        }
        {
            /* an attribute put on an event: read back */
            short v = 2;
            out("put", "%d", AEPutAttributePtr(&evt, keyEventSourceAttr, typeShortInteger, &v, 2));
            dump_attrs("put", &evt, kAutoGenerateReturnID);
            flush_h();
        }
        AEDisposeDesc(&evt);
        AEDisposeDesc(&target);
        {
            /* addressed to another application, by signature: the
               original address and the source */
            OSType finder = 'MACS';
            AECreateDesc(typeApplSignature, &finder, sizeof finder, &target);
            out("create.other", "%d", AECreateAppleEvent('Test', 'Evnt', &target, 9, 0, &evt));
            dump_attrs("other", &evt, 9);
            flush_h();
            AEDisposeDesc(&evt);
            AEDisposeDesc(&target);
        }
    }
    if(SECTION("send"))
    {
        /* AESend to the probe itself: waiting for the reply (BBEdit Lite
           6.1 makes its untitled window so: 'core' 'crel'), a handler
           answering an error, no handler, and no reply */
        AEEventHandlerUPP upp = NewAEEventHandlerUPP(send_handler);
        AEInstallEventHandler('Test', 'Snd1', upp, 1, false);
        AEInstallEventHandler('Test', 'Snd2', upp, 2, false);
        AEInstallEventHandler('Test', 'Snd3', upp, 3, false);
        send_case(1, 'Snd1', kAEWaitReply | kAECanInteract);
        send_case(2, 'Snd2', kAEWaitReply | kAECanInteract);
        send_case(3, 'None', kAEWaitReply | kAECanInteract);
        send_case(4, 'Snd3', kAENoReply | kAECanInteract);
    }
    if(SECTION("osl"))
    {
        /* routine descriptors: Mixed Mode takes a bare pointer for 68k
           code (uppOSLAccessorProcInfo, uppOSLDisposeTokenProcInfo) */
        ProcPtr acc = (ProcPtr)NewRoutineDescriptor((ProcPtr)osl_accessor, 0x000FFFE0, kPowerPCISA);
        ProcPtr dispose = (ProcPtr)NewRoutineDescriptor((ProcPtr)osl_dispose, 0x000000E0, kPowerPCISA);
        AEDesc token;
        long v = 5;
        /* AEObjectInit first, as the library asks */
        OSErr (*init)(void) = (OSErr (*)(void))probe_bind("ObjectSupportLib", "AEObjectInit");
        if(init)
            out("init", "%d", init());
        osl_get("get.none", 'Cls1', 'Cnt1', acc);
        osl_call("call.none", 'Cls1', 'Cnt1');
        out("install", "%d", AEInstallObjectAccessor('Cls1', 'Cnt1', acc, 11, false));
        osl_get("get", 'Cls1', 'Cnt1', acc);
        osl_get("get.other", 'Cls1', 'Cnt2', acc);
        osl_call("call", 'Cls1', 'Cnt1');
        out("install.cls3", "%d", AEInstallObjectAccessor('Cls3', typeWildCard, acc, 33, false));
        out("install.cnt4", "%d", AEInstallObjectAccessor(typeWildCard, 'Cnt4', acc, 44, false));
        osl_call("call.cls3", 'Cls3', 'Cnt9');
        osl_call("call.cnt4", 'Cls9', 'Cnt4');
        osl_call("call.both", 'Cls3', 'Cnt4');
        osl_get("get.wild", 'Cls3', 'Cnt9', acc);
        out("install.wild", "%d", AEInstallObjectAccessor(typeWildCard, typeWildCard, acc, 22, false));
        osl_call("call.wild", 'Cls2', 'Cnt2');
        out("remove.wrong", "%d", AERemoveObjectAccessor('Cls1', 'Cnt1', dispose, false));
        out("remove", "%d", AERemoveObjectAccessor('Cls1', 'Cnt1', acc, false));
        osl_get("get.removed", 'Cls1', 'Cnt1', acc);
        out("remove.again", "%d", AERemoveObjectAccessor('Cls1', 'Cnt1', acc, false));
        AECreateDesc(typeLongInteger, &v, sizeof v, &token);
        out("dispose.plain", "%d", AEDisposeToken(&token));
        out("dispose.plain.token", "%.4s", (char *)&token.descriptorType);
        out("callbacks", "%d", AESetObjectCallbacks(NULL, NULL, dispose, NULL, NULL, NULL, NULL));
        AECreateDesc(typeLongInteger, &v, sizeof v, &token);
        out("dispose.cb", "%d", AEDisposeToken(&token));
        flush_h();
        out("dispose.cb.calls", "%d", disposed);
        out("dispose.cb.token", "%.4s", (char *)&token.descriptorType);
        AESetObjectCallbacks(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        AERemoveObjectAccessor('Cls3', typeWildCard, acc, false);
        AERemoveObjectAccessor(typeWildCard, 'Cnt4', acc, false);
        AERemoveObjectAccessor(typeWildCard, typeWildCard, acc, false);
    }
    if(SECTION("resolve"))
    {
        ProcPtr acc = (ProcPtr)NewRoutineDescriptor((ProcPtr)resolve_accessor, 0x000FFFE0, kPowerPCISA);
        ProcPtr dispose = (ProcPtr)NewRoutineDescriptor((ProcPtr)resolve_dispose, 0x000000E0, kPowerPCISA);
        AEDesc null_desc, one, two, name, win, doc, bad, nowhere, plain;
        long v1 = 1, v2 = 2;
        OSErr (*init)(void) = (OSErr (*)(void))probe_bind("ObjectSupportLib", "AEObjectInit");
        if(init)
            init();
        AEInstallObjectAccessor('cwin', typeNull, acc, 1, false);
        AEInstallObjectAccessor('docu', typeWildCard, acc, 2, false);
        AEInstallObjectAccessor('fail', typeWildCard, acc, 99, false);
        AESetObjectCallbacks(NULL, NULL, dispose, NULL, NULL, NULL, NULL);
        AECreateDesc(typeNull, NULL, 0, &null_desc);
        AECreateDesc(typeLongInteger, &v1, sizeof v1, &one);
        AECreateDesc(typeLongInteger, &v2, sizeof v2, &two);
        AECreateDesc(typeChar, "x", 1, &name);
        resolve_case("null", &null_desc);
        make_spec('cwin', &null_desc, kAbsolutePositionForm, &one, &win);
        resolve_case("window", &win);
        make_spec('docu', &win, kNameForm, &name, &doc);
        resolve_case("document", &doc);
        make_spec('fail', &win, kAbsolutePositionForm, &two, &bad);
        resolve_case("failing", &bad);
        make_spec('cwin', &bad, kAbsolutePositionForm, &one, &nowhere);
        resolve_case("in.failing", &nowhere);
        AEDisposeDesc(&nowhere);
        make_spec('none', &null_desc, kAbsolutePositionForm, &one, &nowhere);
        resolve_case("no.accessor", &nowhere);
        resolve_case("not.spec", &one);
        AECreateList(NULL, 0, true, &plain);
        resolve_case("empty.record", &plain);
        AEDisposeDesc(&plain);
        AESetObjectCallbacks(NULL, NULL, NULL, NULL, NULL, NULL, NULL);
        AERemoveObjectAccessor('cwin', typeNull, acc, false);
        AERemoveObjectAccessor('docu', typeWildCard, acc, false);
        AERemoveObjectAccessor('fail', typeWildCard, acc, false);
        AEDisposeDesc(&nowhere);
        AEDisposeDesc(&bad);
        AEDisposeDesc(&doc);
        AEDisposeDesc(&win);
        AEDisposeDesc(&name);
        AEDisposeDesc(&two);
        AEDisposeDesc(&one);
    }
    if(SECTION("coerce"))
    {
        /* The Apple Event Manager's own coercions between an alias and an
           FSSpec: the Finder's 'odoc' gives its documents as aliases
           (NewAliasMinimal) and applications ask for 'fss ' (SimpleText
           and BBEdit Lite 6.1 opened nothing on Executor). The probe's own
           file. */
        ProcessSerialNumber self;
        ProcessInfoRec info;
        FSSpec me, got;
        AliasHandle alias = NULL;
        AEDesc ad, fd, list;
        AEKeyword kw;
        DescType type;
        Size size;
        OSErr err;
        GetCurrentProcess(&self);
        memset(&info, 0, sizeof info);
        info.processInfoLength = sizeof info;
        info.processAppSpec = &me;
        GetProcessInformation(&self, &info);
        out("alias.new", "%d", NewAliasMinimal(&me, &alias));
        HLock((Handle)alias);
        AECreateDesc(typeAlias, *alias, GetHandleSize((Handle)alias), &ad);
        HUnlock((Handle)alias);
        err = AECoerceDesc(&ad, typeFSS, &fd);
        out("alis.fss", "%d", err);
        if(err == noErr)
        {
            out("alis.fss.type", "%.4s", (char *)&fd.descriptorType);
            memcpy(&got, *fd.dataHandle, sizeof got);
            out("alis.fss.same", "%s", got.vRefNum == me.vRefNum && got.parID == me.parID
                && EqualString(got.name, me.name, false, true) ? "yes" : "no");
            AEDisposeDesc(&fd);
        }
        /* from a list, as an 'odoc' handler reads it */
        AECreateList(NULL, 0, false, &list);
        AEPutDesc(&list, 0, &ad);
        memset(&got, 0, sizeof got);
        err = AEGetNthPtr(&list, 1, typeFSS, &kw, &type, &got, sizeof got, &size);
        out("list.fss", "%d", err);
        if(err == noErr)
            out("list.fss.result", "%.4s %ld %s", (char *)&type, (long)size,
                EqualString(got.name, me.name, false, true) ? "same file" : "other");
        AEDisposeDesc(&list);
        AEDisposeDesc(&ad);
        /* the other way */
        AECreateDesc(typeFSS, &me, sizeof me, &fd);
        err = AECoerceDesc(&fd, typeAlias, &ad);
        out("fss.alis", "%d", err);
        if(err == noErr)
        {
            out("fss.alis.type", "%.4s", (char *)&ad.descriptorType);
            AEDisposeDesc(&ad);
        }
        /* A list coerced whole (Navigation Services' reply: BBEdit Lite 6.1
           coerces its selection, a list of one FSSpec, to 'fss '): one item,
           two, none; an item that itself needs coercing */
        {
            static const struct { const char *key; int items; DescType item, to; } lists[] = {
                { "one.fss", 1, typeFSS, typeFSS },     { "one.alis", 1, typeFSS, typeAlias },
                { "two.fss", 2, typeFSS, typeFSS },     { "none.fss", 0, typeFSS, typeFSS },
                { "one.long.shor", 1, typeLongInteger, typeShortInteger },
                { "one.long.list", 1, typeLongInteger, typeAEList },
            };
            for(int i = 0; i < (int)(sizeof lists / sizeof lists[0]); i++)
            {
                AEDesc l, item, result;
                long n = 7;
                char k[48];
                AECreateList(NULL, 0, false, &l);
                if(lists[i].item == typeFSS)
                    AECreateDesc(typeFSS, &me, sizeof me, &item);
                else
                    AECreateDesc(typeLongInteger, &n, sizeof n, &item);
                for(int j = 0; j < lists[i].items; j++)
                    AEPutDesc(&l, 0, &item);
                AEDisposeDesc(&item);
                result.descriptorType = typeNull;
                result.dataHandle = NULL;
                err = AECoerceDesc(&l, lists[i].to, &result);
                sprintf(k, "list.%s", lists[i].key);
                out(k, "%d type %.4s size %ld", err, (char *)&result.descriptorType,
                    result.dataHandle ? (long)GetHandleSize(result.dataHandle) : -1L);
                AEDisposeDesc(&result);
                AEDisposeDesc(&l);
            }
        }
        /* records under other types (an object specifier is a record
           coerced to 'obj '; the Object Support Library coerces it back to
           read its keys: BBEdit Lite 6.1's 'save' handler) */
        {
            AEDesc rec, obj, back, other, num;
            long n = 7, v = 0;
            char k[48];
            DescType type;
            Size size;
            AECreateList(NULL, 0, true, &rec);
            AEPutParamPtr(&rec, 'want', typeLongInteger, &n, sizeof n);
            obj.descriptorType = back.descriptorType = other.descriptorType = typeNull;
            obj.dataHandle = back.dataHandle = other.dataHandle = NULL;
            err = AECoerceDesc(&rec, 'obj ', &obj);
            out("reco.obj", "%d type %.4s", err, (char *)&obj.descriptorType);
            if(err == noErr)
            {
                err = AECoerceDesc(&obj, typeAERecord, &back);
                out("obj.reco", "%d type %.4s", err, (char *)&back.descriptorType);
                if(err == noErr)
                {
                    err = AEGetParamPtr(&back, 'want', typeLongInteger, &type, &v, sizeof v, &size);
                    out("obj.reco.want", "%d %ld", err, v);
                    AEDisposeDesc(&back);
                }
                v = 0;
                err = AEGetParamPtr(&obj, 'want', typeLongInteger, &type, &v, sizeof v, &size);
                out("obj.want", "%d %ld", err, v);
                err = AECoerceDesc(&obj, 'abcd', &other);
                out("obj.abcd", "%d type %.4s", err, (char *)&other.descriptorType);
                if(err == noErr)
                    AEDisposeDesc(&other);
                AEDisposeDesc(&obj);
            }
            err = AECoerceDesc(&rec, 'abcd', &other);
            out("reco.abcd", "%d type %.4s", err, (char *)&other.descriptorType);
            if(err == noErr)
            {
                err = AECoerceDesc(&other, typeAERecord, &back);
                out("abcd.reco", "%d type %.4s", err, (char *)&back.descriptorType);
                if(err == noErr)
                    AEDisposeDesc(&back);
                AEDisposeDesc(&other);
            }
            err = AECoerceDesc(&rec, typeAEList, &other);
            out("reco.list", "%d type %.4s", err, (char *)&other.descriptorType);
            if(err == noErr)
                AEDisposeDesc(&other);
            /* a plain descriptor asked for as a list (MacPerl's 'dosc'
               handler takes its script so) */
            {
                AEDesc text, l;
                long cnt = -1;
                AECreateDesc(typeChar, "abc", 3, &text);
                l.descriptorType = typeNull;
                l.dataHandle = NULL;
                err = AECoerceDesc(&text, typeAEList, &l);
                if(err == noErr)
                    AECountItems(&l, &cnt);
                out("TEXT.list", "%d type %.4s items %ld", err, (char *)&l.descriptorType, cnt);
                if(err == noErr)
                {
                    DescType t;
                    Size sz;
                    AEKeyword kw;
                    char buf[8] = {0};
                    err = AEGetNthPtr(&l, 1, typeChar, &kw, &t, buf, sizeof buf, &sz);
                    out("TEXT.list.1", "%d %.4s %ld %.3s", err, (char *)&t, (long)sz, buf);
                    AEDisposeDesc(&l);
                }
                AEDisposeDesc(&text);
            }
            AECreateDesc(typeLongInteger, &n, sizeof n, &num);
            back.descriptorType = typeNull;
            err = AECoerceDesc(&num, typeAERecord, &back);
            out("long.reco", "%d type %.4s", err, (char *)&back.descriptorType);
            if(err == noErr)
                AEDisposeDesc(&back);
            AEDisposeDesc(&num);
            AEDisposeDesc(&rec);
            (void)k;
        }
        /* the null descriptor (an object specifier's container for the
           application; BBEdit Lite 6.1's Save sends itself one) */
        {
            static const DescType tos[] = { 'obj ', typeNull, typeChar, typeLongInteger,
                                            typeAEList, typeAERecord, typeWildCard };
            for(int i = 0; i < (int)(sizeof tos / sizeof tos[0]); i++)
            {
                AEDesc nd, result;
                char k[48];
                nd.descriptorType = typeNull;
                nd.dataHandle = NULL;
                result.descriptorType = 'XXXX';
                result.dataHandle = NULL;
                err = AECoerceDesc(&nd, tos[i], &result);
                sprintf(k, "null.%.4s", (char *)&tos[i]);
                out(k, "%d type %.4s size %ld", err, (char *)&result.descriptorType,
                    result.dataHandle ? (long)GetHandleSize(result.dataHandle) : -1L);
                if(err == noErr)
                    AEDisposeDesc(&result);
            }
        }
        AEDisposeDesc(&fd);
        DisposeHandle((Handle)alias);
        /* A volume (BridgeAgent's LAUNCH "Macintosh HD:" sends the Finder an
           'odoc' with NewAlias of it): its FSSpec, the record's kind, parent
           and ID, its full path, and back to an FSSpec */
        {
            FSSpec vol;
            unsigned char *r;
            short at, n;
            alias = NULL;
            err = FSMakeFSSpec(me.vRefNum, fsRtDirID, "\p", &vol);
            out("vol.spec", "%d parent %ld same volume %s", err, (long)vol.parID,
                vol.vRefNum == me.vRefNum ? "yes" : "no");
            out("vol.alias.minimal", "%d", NewAliasMinimal(&vol, &alias));
            if(alias)
                DisposeHandle((Handle)alias);
            alias = NULL;
            err = NewAlias(NULL, &vol, &alias);
            out("vol.alias.new", "%d", err);
            if(err == noErr && alias)
            {
                HLock((Handle)alias);
                r = (unsigned char *)*alias;
                out("vol.alias.kind", "%d", *(short *)(r + 8));
                out("vol.alias.parent", "%ld", *(long *)(r + 46));
                out("vol.alias.id", "%ld", *(long *)(r + 114));
                out("vol.alias.name.same", "%s",
                    EqualString(r + 50, vol.name, false, true) ? "yes" : "no");
                /* the tagged data from 150: tag 2, the full path, with
                   the volume's name as "VOL" */
                for(at = 150; at + 4 <= GetHandleSize((Handle)alias); )
                {
                    short tag = *(short *)(r + at);
                    n = *(short *)(r + at + 2);
                    if(tag == -1)
                        break;
                    if(tag == 2)
                        out("vol.alias.path", "VOL%.*s", (int)(n - vol.name[0]),
                            (char *)r + at + 4 + vol.name[0]);
                    at += 4 + ((n + 1) & ~1);
                }
                AECreateDesc(typeAlias, *alias, GetHandleSize((Handle)alias), &ad);
                HUnlock((Handle)alias);
                memset(&got, 0, sizeof got);
                err = AECoerceDesc(&ad, typeFSS, &fd);
                out("vol.alis.fss", "%d", err);
                if(err == noErr)
                {
                    memcpy(&got, *fd.dataHandle, sizeof got);
                    out("vol.alis.fss.same", "%s", got.vRefNum == vol.vRefNum
                        && got.parID == vol.parID && EqualString(got.name, vol.name, false, true)
                        ? "yes" : "no");
                    AEDisposeDesc(&fd);
                }
                AEDisposeDesc(&ad);
                DisposeHandle((Handle)alias);
            }
        }
    }
    if(SECTION("layout"))
    {
        /* A new event's handle: its size and the header's parameter offset
           and counts, and a parameter appended at the end by hand, counted
           in the header, as BBEdit Lite 6.1 builds its 'crel' (AEGizmos'
           way): does the Apple Event Manager then find it? */
        ProcessSerialNumber self;
        AEAddressDesc target;
        AppleEvent evt;
        Handle h;
        Size size;
        long *hdr;
        DescType type;
        OSType cls = 0;
        static const long param[4] = { 'kocl', typeType, 4, 'TxtW' };
        GetCurrentProcess(&self);
        AECreateDesc(typeProcessSerialNumber, &self, sizeof self, &target);
        AECreateAppleEvent('Test', 'Lay1', &target, kAutoGenerateReturnID, kAnyTransactionID, &evt);
        h = evt.dataHandle;
        size = GetHandleSize(h);
        hdr = (long *)*h;
        out("new", "size %ld params at %ld attributes %ld parameters %ld", (long)size, hdr[2],
            hdr[3], hdr[4]);
        out("new.end", "%.4s", *h + size - 4);
        {
            /* the header's bytes up to the target: where the class and ID are */
            char hex[3 * 64 + 1];
            int i, n = size < 64 ? (int)size : 64;
            for(i = 0; i < n; i++)
                sprintf(hex + 3 * i, "%02x ", (unsigned char)(*h)[i]);
            hex[3 * n - 1] = 0;
            out("new.bytes", "%s", hex);
        }
        SetHandleSize(h, size + 16);
        BlockMoveData(param, *h + size, 16);
        ((long *)*h)[4] += 1;
        out("appended", "%d", AEGetParamPtr(&evt, 'kocl', typeType, &type, &cls, 4, &size));
        out("appended.value", "%.4s", (char *)&cls);
        /* BBEdit Lite 6.1 also clears the long at 44 when it builds its
           'crel' (twice, AEGizmos' stream code): the event keeps its class */
        ((long *)*h)[11] = 0;
        cls = 0;
        out("cleared44", "%d", AEGetAttributePtr(&evt, keyEventClassAttr, typeType, &type, &cls, 4, &size));
        out("cleared44.class", "%.4s", (char *)&cls);
        cls = 0;
        AEGetAttributePtr(&evt, keyEventIDAttr, typeType, &type, &cls, 4, &size);
        out("cleared44.id", "%.4s", (char *)&cls);
        AEDisposeDesc(&evt);
        AECreateAppleEvent('Test', 'Lay2', &target, kAutoGenerateReturnID, kAnyTransactionID, &evt);
        cls = 'TxtW';
        AEPutParamPtr(&evt, 'kocl', typeType, &cls, 4);
        h = evt.dataHandle;
        hdr = (long *)*h;
        out("put", "size %ld params at %ld parameters %ld", (long)GetHandleSize(h), hdr[2], hdr[4]);
        out("put.at", "%.4s", *h + hdr[2]);
        AEDisposeDesc(&evt);
        AEDisposeDesc(&target);
    }

    /* The checks of upstream Executor's tests/appleevents.cpp (its Apple
       Event Manager written from a spec, autc04/executor 1af5b3e2), made
       on 8.6: what the descriptor, list, attribute, coercion, handler,
       array and AEManagerInfo calls answer, the values they give. */
    if(SECTION("spec"))
    {
        AEDesc d;
        AEDescList list;
        AEKeyword kw;
        DescType type;
        Size size;
        long count, v32;
        short v16;
        OSErr err;

        /* AEDisposeDesc empties the descriptor; a typeNull one has no handle */
        err = AECreateDesc(typeChar, "hello", 5, &d);
        out("desc.create", "%d size %ld", err, d.dataHandle ? (long)GetHandleSize(d.dataHandle) : -1L);
        err = AEDisposeDesc(&d);
        out("desc.dispose", "%d type %.4s handle %s", err, (char *)&d.descriptorType,
            d.dataHandle ? "set" : "nil");
        memset(&d, SENTINEL, sizeof d);
        err = AECreateDesc(typeNull, NULL, 0, &d);
        out("desc.null", "%d type %.4s handle %s", err, (char *)&d.descriptorType,
            d.dataHandle ? "set" : "nil");
        AEDisposeDesc(&d);

        /* a list item put as text, got coerced to a long */
        AECreateList(NULL, 0, false, &list);
        err = AEPutPtr(&list, 0, typeChar, "42", 2);
        count = -1;
        AECountItems(&list, &count);
        out("list.put0", "%d count %ld", err, count);
        v32 = -1;
        type = 0;
        size = -1;
        err = AEGetNthPtr(&list, 1, typeLongInteger, &kw, &type, &v32, 4, &size);
        out("list.get_coerced", "%d type %.4s size %ld value %ld", err, (char *)&type, (long)size, v32);
        AEDisposeDesc(&list);

        /* AEDeleteItem, then an index past the end */
        AECreateList(NULL, 0, false, &list);
        v32 = 0;
        AEPutPtr(&list, 0, typeLongInteger, &v32, 4);
        v32 = 7;
        AEPutPtr(&list, 0, typeLongInteger, &v32, 4);
        err = AEDeleteItem(&list, 1);
        count = -1;
        AECountItems(&list, &count);
        v32 = -1;
        AEGetNthPtr(&list, 1, typeLongInteger, &kw, &type, &v32, 4, &size);
        out("list.delete1", "%d count %ld first %ld", err, count, v32);
        out("list.delete_past", "%d", AEDeleteItem(&list, 2));
        AEDisposeDesc(&list);

        /* a longer item replacing the first keeps the second; -1 is the last */
        AECreateList(NULL, 0, false, &list);
        AEPutPtr(&list, 0, typeChar, "a", 1);
        v32 = 42;
        AEPutPtr(&list, 0, typeLongInteger, &v32, 4);
        err = AEPutPtr(&list, 1, typeChar, "long", 4);
        v32 = -1;
        AEGetNthPtr(&list, 2, typeLongInteger, &kw, &type, &v32, 4, &size);
        out("list.replace1", "%d second %ld", err, v32);
        v32 = 99;
        err = AEPutPtr(&list, -1, typeLongInteger, &v32, 4);
        count = -1;
        AECountItems(&list, &count);
        v32 = -1;
        AEGetNthPtr(&list, 2, typeLongInteger, &kw, &type, &v32, 4, &size);
        out("list.put_minus1", "%d count %ld second %ld", err, count, v32);
        AEDisposeDesc(&list);

        /* AECreateList with factoring arguments */
        {
            char prefix[8];
            memset(prefix, 0, sizeof prefix);
            err = AECreateList(prefix, sizeof prefix, false, &list);
            out("list.factored", "%d type %.4s", err, (char *)&list.descriptorType);
            if(err == noErr)
                AEDisposeDesc(&list);
        }

        /* a new event's return and transaction IDs, as short and long */
        {
            AEAddressDesc target;
            AppleEvent evt;
            long tid = 0;
            target.descriptorType = typeNull;
            target.dataHandle = NULL;
            err = AECreateAppleEvent('TEST', 'EVNT', &target, kAutoGenerateReturnID, 123, &evt);
            out("event.create", "%d", err);
            v16 = 0;
            err = AEGetAttributePtr(&evt, keyReturnIDAttr, typeShortInteger, &type, &v16, 2, &size);
            out("event.returnid", "%d type %.4s %s", err, (char *)&type,
                v16 == 0 ? "zero" : v16 == kAutoGenerateReturnID ? "auto" : "generated");
            err = AEGetAttributePtr(&evt, keyTransactionIDAttr, typeLongInteger, &type, &tid, 4, &size);
            out("event.transactionid", "%d type %.4s %ld", err, (char *)&type, tid);
            AEDisposeDesc(&evt);
        }

        /* the default text to number coercion */
        memset(&d, SENTINEL, sizeof d);
        err = AECoercePtr(typeChar, (Ptr) "123", 3, typeLongInteger, &d);
        v32 = -1;
        if(err == noErr && d.dataHandle)
            BlockMoveData(*d.dataHandle, &v32, 4);
        out("coerce.text_long", "%d type %.4s value %ld", err, (char *)&d.descriptorType, v32);
        if(err == noErr)
            AEDisposeDesc(&d);

        /* installing a handler again for the same event; removing it with
           another handler */
        {
            AEEventHandlerUPP a = NewAEEventHandlerUPP(send_handler), b = NewAEEventHandlerUPP(handler), got = NULL;
            long refcon = 0;
            err = AEInstallEventHandler('CLAS', 'EVNT', a, 1, false);
            out("hdlr.install1", "%d", err);
            err = AEInstallEventHandler('CLAS', 'EVNT', b, 2, false);
            AEGetEventHandler('CLAS', 'EVNT', &got, &refcon, false);
            out("hdlr.install2", "%d got %s refcon %ld", err, got == b ? "second" : got == a ? "first" : "other",
                refcon);
            out("hdlr.remove_other", "%d", AERemoveEventHandler('CLAS', 'EVNT', a, false));
            out("hdlr.remove_it", "%d", AERemoveEventHandler('CLAS', 'EVNT', b, false));
            got = NULL;
            out("hdlr.after", "%d", AEGetEventHandler('CLAS', 'EVNT', &got, &refcon, false));
            /* which of the two removed it: one never installed, and one
               removed naming another handler, then looked up */
            out("hdlr.remove_absent", "%d", AERemoveEventHandler('NONE', 'EVNT', a, false));
            AEInstallEventHandler('CLAS', 'EVN2', b, 5, false);
            err = AERemoveEventHandler('CLAS', 'EVN2', a, false);
            refcon = 0;
            out("hdlr.remove_other2", "%d then get %d refcon %ld", err,
                AEGetEventHandler('CLAS', 'EVN2', &got, &refcon, false), refcon);
            AERemoveEventHandler('CLAS', 'EVN2', b, false);
            /* a wildcard ID and an exact one: which AEGetEventHandler gives */
            AEInstallEventHandler('CLAS', typeWildCard, a, 3, false);
            AEInstallEventHandler('CLAS', 'EXAC', b, 4, false);
            refcon = 0;
            err = AEGetEventHandler('CLAS', 'EXAC', &got, &refcon, false);
            out("hdlr.get_exact", "%d refcon %ld", err, refcon);
            refcon = 0;
            err = AEGetEventHandler('CLAS', 'OTHR', &got, &refcon, false);
            out("hdlr.get_other", "%d refcon %ld", err, refcon);
            refcon = 0;
            err = AEGetEventHandler('CLAS', typeWildCard, &got, &refcon, false);
            out("hdlr.get_wild", "%d refcon %ld", err, refcon);
            AERemoveEventHandler('CLAS', typeWildCard, a, false);
            AERemoveEventHandler('CLAS', 'EXAC', b, false);
        }

        /* AEPutArray and AEGetArray, a data array of shorts */
        {
            short in[3] = { 1, 2, 3 }, outv[3] = { 0, 0, 0 };
            DescType itype = 0;
            Size isize = 0;
            long icount = 0;
            AECreateList(NULL, 0, false, &list);
            err = AEPutArray(&list, kAEDataArray, (AEArrayDataPointer)in, typeShortInteger, 2, 3);
            out("array.put", "%d", err);
            err = AEGetArray(&list, kAEDataArray, (AEArrayDataPointer)outv, sizeof outv, &itype, &isize, &icount);
            out("array.get", "%d type %.4s size %ld count %ld values %d %d %d", err, (char *)&itype, (long)isize,
                icount, outv[0], outv[1], outv[2]);
            AEDisposeDesc(&list);
        }

        /* AEManagerInfo */
        {
            long r = -1;
            err = AEManagerInfo(keyAEVersion, &r);
            out("info.version", "%d %08lx", err, r);
            r = -1;
            err = AEManagerInfo(keyAERecorderCount, &r);
            out("info.recorders", "%d %ld", err, r);
        }
    }
    probe_close();
    return 0;
}
