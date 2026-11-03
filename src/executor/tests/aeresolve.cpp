/* AEResolve (AE_hdlr.cpp): an object specifier resolved from the outside
   in through the installed accessors, as PowerPC MacPerl 5.2's Run Front
   Window needs ('cspt' 1 of 'docu' 1: the script to run). Its 'from' can
   be a specifier with the record header (made here) or without it, as a
   record is kept inside an aggregate (MacPerl's). And AESend with
   kAEDontExecute doesn't run the event (MacPerl sends its typing as
   'setd' so). */
#include "gtest/gtest.h"

#include <base/common.h>
#include <base/cpu.h>
#include <AppleEvents.h>
#include <MemoryMgr.h>
#include <ProcessMgr.h>
#include <appleevent/apple_events.h>

#include <string>
#include <vector>

using namespace Executor;

namespace
{
/* the Apple Event Manager's tables, as at startup and an application's
   launch (the tests' environment does neither) */
void ae_up()
{
    if(!ROMlib_ae_private())
        AE_init();
    if(!ROMlib_ae_private()->appl_zone_tables)
        AE_reinit();
}

struct Call
{
    DescType want, container_type, container_class, form;
    int32_t seld, container;
};
std::vector<Call> calls;

int32_t long_of(AEDesc *d)
{
    if(!d || DESC_TYPE(d) != typeLongInteger || !DESC_DATA(d))
        return 0;
    return *(GUEST<int32_t> *)*DESC_DATA(d);
}

/* pascal OSErr accessor(desiredClass, containerToken*, containerClass,
   keyForm, keyData*, theToken*, refcon): the token a long, 100 times
   the container's plus the index */
syn68k_addr_t accessor(syn68k_addr_t, void *)
{
    syn68k_addr_t ret = POPADDR();
    (void)POPUL();                                      /* refcon */
    AEDesc *token = (AEDesc *)SYN68K_TO_US(POPUL());
    AEDesc *seld = (AEDesc *)SYN68K_TO_US(POPUL());
    DescType form = POPUL();
    DescType container_class = POPUL();
    AEDesc *container = (AEDesc *)SYN68K_TO_US(POPUL());
    DescType want = POPUL();
    Call c = { want, DESC_TYPE(container), container_class, form, long_of(seld), long_of(container) };
    calls.push_back(c);
    GUEST<int32_t> v = c.container * 100 + c.seld;
    WRITEUW(EM_A7, C_AECreateDesc(typeLongInteger, &v, sizeof v, token));
    return ret;
}

int handled;
/* pascal OSErr handler(evt*, reply*, refcon) */
syn68k_addr_t handler(syn68k_addr_t, void *)
{
    syn68k_addr_t ret = POPADDR();
    EM_A7 += 12;
    handled++;
    WRITEUW(EM_A7, noErr);
    return ret;
}

/* a specifier: want, 'indx' n, in from (a typeNull from: the application) */
void spec(DescType want, int32_t n, AEDesc *from, AEDesc *out)
{
    ASSERT_EQ(noErr, C_AECreateList(nullptr, 0, true, out));
    GUEST<DescType> w = want, f = formAbsolutePosition;
    GUEST<int32_t> i = n;
    EXPECT_EQ(noErr, AEPutParamPtr(out, keyAEDesiredClass, typeType, &w, sizeof w));
    EXPECT_EQ(noErr, AEPutParamPtr(out, keyAEKeyForm, typeEnumerated, &f, sizeof f));
    EXPECT_EQ(noErr, AEPutParamPtr(out, keyAEKeyData, typeLongInteger, &i, sizeof i));
    EXPECT_EQ(noErr, AEPutParamDesc(out, keyAEContainer, from));
    out->descriptorType = typeObjectSpecifier;
}

class AEResolveTest : public testing::Test
{
protected:
    ProcPtr fn = nullptr;
    void SetUp() override
    {
        ae_up();
        calls.clear();
        fn = (ProcPtr)SYN68K_TO_US(callback_install(accessor, nullptr));
        ASSERT_EQ(noErr, AEInstallObjectAccessor(typeWildCard, typeWildCard, fn, 0, false));
    }
    void TearDown() override
    {
        AERemoveObjectAccessor(typeWildCard, typeWildCard, fn, false);
    }
};
} // namespace

TEST_F(AEResolveTest, OutsideIn)
{
    static AEDesc app, win, par, token;
    app = { typeNull, nullptr };
    spec("cwin"_4, 2, &app, &win);
    spec("cpar"_4, 1, &win, &par);
    ASSERT_EQ(noErr, AEResolve(&par, kAEIDoMinimum, &token));
    EXPECT_EQ(201, long_of(&token));
    ASSERT_EQ(2u, calls.size());
    EXPECT_EQ("cwin"_4, calls[0].want);
    EXPECT_EQ((DescType)typeNull, calls[0].container_type);
    EXPECT_EQ((DescType)typeNull, calls[0].container_class);
    EXPECT_EQ((DescType)formAbsolutePosition, calls[0].form);
    EXPECT_EQ(2, calls[0].seld);
    EXPECT_EQ("cpar"_4, calls[1].want);
    EXPECT_EQ((DescType)typeLongInteger, calls[1].container_type);
    EXPECT_EQ("cwin"_4, calls[1].container_class);
    EXPECT_EQ(1, calls[1].seld);
    AEDisposeDesc(&token);
    AEDisposeDesc(&par);
    AEDisposeDesc(&win);
}

/* 'from' kept as a record is inside an aggregate: from the count on */
TEST_F(AEResolveTest, ContainerWithoutRecordHeader)
{
    static AEDesc app, win, par, token;
    app = { typeNull, nullptr };
    spec("docu"_4, 1, &app, &win);
    Handle h = DESC_DATA(&win);
    ASSERT_GT(GetHandleSize(h), 0x18);
    ASSERT_EQ((uint32_t)typeAERecord, (uint32_t) * (GUEST<uint32_t> *)(*h + 12));
    std::string nested(*h + 16, *h + GetHandleSize(h));
    spec("cspt"_4, 1, &app, &par);
    par.descriptorType = typeAERecord;
    EXPECT_EQ(noErr, AEPutParamPtr(&par, keyAEContainer, typeObjectSpecifier,
                                   nested.data(), nested.size()));
    par.descriptorType = typeObjectSpecifier;
    ASSERT_EQ(noErr, AEResolve(&par, kAEIDoMinimum, &token));
    EXPECT_EQ(101, long_of(&token));
    ASSERT_EQ(2u, calls.size());
    EXPECT_EQ("docu"_4, calls[0].want);
    EXPECT_EQ("cspt"_4, calls[1].want);
    AEDisposeDesc(&token);
    AEDisposeDesc(&par);
    AEDisposeDesc(&win);
}

TEST_F(AEResolveTest, NotASpecifier)
{
    static AEDesc d, token;
    GUEST<int32_t> v = 1;
    C_AECreateDesc(typeLongInteger, &v, sizeof v, &d);
    EXPECT_EQ(errAENotAnObjSpec, AEResolve(&d, kAEIDoMinimum, &token));
    EXPECT_EQ(0u, calls.size());
    AEDisposeDesc(&d);
}

TEST(AESend, DontExecuteIsNotRun)
{
    static AEDesc target, evt, reply;
    ae_up();
    ProcessSerialNumber self = { 0, kCurrentProcess };
    ProcPtr fn = (ProcPtr)SYN68K_TO_US(callback_install(handler, nullptr));
    ASSERT_EQ(noErr, AEInstallEventHandler("M1ts"_4, "test"_4, (AEEventHandlerUPP)fn, 0, false));
    C_AECreateDesc(typeProcessSerialNumber, &self, sizeof self, &target);
    ASSERT_EQ(noErr, AECreateAppleEvent("M1ts"_4, "test"_4, &target, kAutoGenerateReturnID,
                                        kAnyTransactionID, &evt));
    handled = 0;
    EXPECT_EQ(noErr, AESend(&evt, &reply, kAENoReply | kAEDontExecute, kAENormalPriority,
                            kAEDefaultTimeout, nullptr, nullptr));
    EXPECT_EQ(0, handled);
    EXPECT_EQ(noErr, AESend(&evt, &reply, kAENoReply, kAENormalPriority, kAEDefaultTimeout,
                            nullptr, nullptr));
    EXPECT_EQ(1, handled);
    AERemoveEventHandler("M1ts"_4, "test"_4, (AEEventHandlerUPP)fn, false);
    AEDisposeDesc(&evt);
    AEDisposeDesc(&target);
}
