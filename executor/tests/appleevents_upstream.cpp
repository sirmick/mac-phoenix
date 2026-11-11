/* Upstream Executor's Apple Event Manager tests (autc04/executor
   tests/appleevents.cpp, 1af5b3e2: its manager rewritten from a spec of
   Inside Macintosh and PACK 8), run against ours. Where they and 8.6/9.0.4
   disagree, the expectation is the Mac's, measured by the difftest aevt
   probe's "spec" section (tools/difftest/run.py aevt), and marked
   "8.6:". The private handler-table test and the dispatch test use
   upstream's own internals and are left out. */
#include "gtest/gtest.h"

#include "compat.h"

#ifdef EXECUTOR
#include <AppleEvents.h>
#include <MemoryMgr.h>
#include <ProcessMgr.h>
#include <appleevent/apple_events.h>
#include <prefs/prefs.h>

using namespace Executor;

namespace
{
class AppleEventsTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        static bool initialized = false;

        if(!initialized)
        {
            AE_init();
            AE_reinit();
            initialized = true;
        }
    }
};

} /* namespace */

TEST_F(AppleEventsTest, CreateDisposeDesc)
{
    AEDesc desc;

    ASSERT_EQ(noErr, AECreateDesc(typeChar, "hello", 5, &desc));
    EXPECT_EQ(typeChar, desc.descriptorType);
    ASSERT_NE(nullptr, desc.dataHandle);
    EXPECT_EQ(5, GetHandleSize(desc.dataHandle));

    ASSERT_EQ(noErr, AEDisposeDesc(&desc));
    EXPECT_EQ(typeNull, desc.descriptorType);
    EXPECT_EQ(nullptr, desc.dataHandle);

    ASSERT_EQ(noErr, AECreateDesc(typeNull, nullptr, 0, &desc));
    EXPECT_EQ(typeNull, desc.descriptorType);
    EXPECT_EQ(nullptr, desc.dataHandle);
}

TEST_F(AppleEventsTest, ListPutGetCoercesAndCopiesCoercedData)
{
    AEDescList list;
    GUEST<AEKeyword> keyword;
    GUEST<DescType> type;
    GUEST<Size> size;
    GUEST<int32_t> count;
    GUEST<int32_t> value = 0;

    ASSERT_EQ(noErr, AECreateList(nullptr, 0, false, &list));
    ASSERT_EQ(noErr, AEPutPtr(&list, 0, typeChar, "42", 2));

    ASSERT_EQ(noErr, AECountItems(&list, &count));
    EXPECT_EQ(1, count);

    /* Regression: AEGetNthPtr used to copy the original data even though it
     * reported the coerced type. */
    ASSERT_EQ(noErr, AEGetNthPtr(&list, 1, typeSInt32, &keyword, &type,
                                 &value, sizeof value, &size));
    EXPECT_EQ(typeSInt32, type);
    EXPECT_EQ(4, size);
    EXPECT_EQ(42, value);

    AEDisposeDesc(&list);
}

TEST_F(AppleEventsTest, DeleteItem)
{
    AEDescList list;
    GUEST<int32_t> count;
    GUEST<int32_t> value = 0;
    GUEST<DescType> type;
    GUEST<Size> size;

    ASSERT_EQ(noErr, AECreateList(nullptr, 0, false, &list));
    ASSERT_EQ(noErr, AEPutPtr(&list, 0, typeSInt32, &value, 4));
    value = 7;
    ASSERT_EQ(noErr, AEPutPtr(&list, 0, typeSInt32, &value, 4));

    {
        GUEST<int32_t> first_value = -1;
        GUEST<int32_t> second_value = -1;

        ASSERT_EQ(noErr, AEGetNthPtr(&list, 1, typeSInt32, nullptr, &type,
                                     &first_value, sizeof first_value, &size));
        ASSERT_EQ(noErr, AEGetNthPtr(&list, 2, typeSInt32, nullptr, &type,
                                     &second_value, sizeof second_value, &size));
        EXPECT_EQ(0, first_value);
        EXPECT_EQ(7, second_value);
    }

    ASSERT_EQ(noErr, AEDeleteItem(&list, 1));
    ASSERT_EQ(noErr, AECountItems(&list, &count));
    EXPECT_EQ(1, count);

    ASSERT_EQ(noErr, AEGetNthPtr(&list, 1, typeSInt32, nullptr, &type,
                                 &value, sizeof value, &size));
    EXPECT_EQ(7, value);

    EXPECT_EQ(errAEDescNotFound, AEDeleteItem(&list, 2));

    AEDisposeDesc(&list);
}

TEST_F(AppleEventsTest, ListReplaceShiftsTail)
{
    AEDescList list;
    GUEST<int32_t> value = 0;
    GUEST<int32_t> second = 0;
    GUEST<DescType> type;
    GUEST<Size> size;

    ASSERT_EQ(noErr, AECreateList(nullptr, 0, false, &list));
    ASSERT_EQ(noErr, AEPutPtr(&list, 0, typeChar, "a", 1));
    value = 42;
    ASSERT_EQ(noErr, AEPutPtr(&list, 0, typeSInt32, &value, 4));

    /* Replace the first item with a longer one; the second must survive. */
    ASSERT_EQ(noErr, AEPutPtr(&list, 1, typeChar, "long", 4));
    ASSERT_EQ(noErr, AEGetNthPtr(&list, 2, typeSInt32, nullptr, &type,
                                 &second, sizeof second, &size));
    EXPECT_EQ(42, second);

    /* 8.6: index -1 is errAEIllegalIndex, the list as it was (aevt
       spec.list.put_minus1) */
    value = 99;
    EXPECT_EQ(errAEIllegalIndex, AEPutPtr(&list, -1, typeSInt32, &value, 4));
    ASSERT_EQ(noErr, AEGetNthPtr(&list, 2, typeSInt32, nullptr, &type,
                                 &second, sizeof second, &size));
    EXPECT_EQ(42, second);

    AEDisposeDesc(&list);
}

TEST_F(AppleEventsTest, CreateAppleEventAttributes)
{
    AEDesc target;
    AppleEvent evt;
    GUEST<DescType> type;
    GUEST<Size> size;
    GUEST<uint32_t> event_class;
    GUEST<uint32_t> event_id;
    GUEST<int16_t> return_id;
    GUEST<int32_t> transaction_id;

    target.descriptorType = typeNull;
    target.dataHandle = nullptr;

    ASSERT_EQ(noErr, AECreateAppleEvent("TEST"_4, "EVNT"_4, &target,
                                        kAutoGenerateReturnID, 123, &evt));

    ASSERT_EQ(noErr, AEGetAttributePtr(&evt, keyEventClassAttr, typeType,
                                       &type, &event_class, sizeof event_class,
                                       &size));
    EXPECT_EQ("TEST"_4, event_class);

    ASSERT_EQ(noErr, AEGetAttributePtr(&evt, keyEventIDAttr, typeType,
                                       &type, &event_id, sizeof event_id,
                                       &size));
    EXPECT_EQ("EVNT"_4, event_id);

    /* 8.6: a return ID asked for as a short doesn't coerce, and the type
       given back is typeNull (aevt spec.event.returnid) */
    EXPECT_EQ(errAECoercionFail, AEGetAttributePtr(&evt, keyReturnIDAttr, typeSInt16,
                                                   &type, &return_id, sizeof return_id,
                                                   &size));
    EXPECT_EQ(typeNull, (DescType)type);

    ASSERT_EQ(noErr, AEGetAttributePtr(&evt, keyTransactionIDAttr, typeSInt32,
                                       &type, &transaction_id,
                                       sizeof transaction_id, &size));
    EXPECT_EQ(123, transaction_id);

    AEDisposeDesc(&evt);
}

TEST_F(AppleEventsTest, CreateListAcceptsFactoringArguments)
{
    AEDescList list;
    uint8_t prefix[8] = { 0 };

    ASSERT_EQ(noErr, AECreateList((Ptr)prefix, sizeof prefix, false, &list));
    AEDisposeDesc(&list);
}

TEST_F(AppleEventsTest, DefaultCoercion)
{
    AEDesc desc;
    GUEST<int32_t> value = 0;

    ASSERT_EQ(noErr, AECoercePtr(typeChar, (Ptr)"123", 3, typeSInt32,
                                 &desc));
    ASSERT_EQ(typeSInt32, desc.descriptorType);
    ASSERT_NE(nullptr, desc.dataHandle);
    ASSERT_EQ(4, GetHandleSize(desc.dataHandle));
    memcpy(&value, *desc.dataHandle, sizeof value);
    EXPECT_EQ(123, value);
    AEDisposeDesc(&desc);
}

TEST_F(AppleEventsTest, HandlerInstallReplaceAndRemove)
{
    /* Handlers are guest code pointers.  Use addresses inside the emulated
     * address space; the manager converts host/guest pointers when it stores
     * them. */
    AEEventHandlerUPP first((void *)SYN68K_TO_US(0x1000));
    AEEventHandlerUPP second((void *)SYN68K_TO_US(0x1004));
    GUEST<AEEventHandlerUPP> got;
    GUEST<int32_t> refcon;

    ASSERT_EQ(noErr, AEInstallEventHandler("CLAS"_4, "EVNT"_4, first, 1,
                                           false));
    ASSERT_EQ(noErr, AEGetEventHandler("CLAS"_4, "EVNT"_4, &got, &refcon,
                                       false));
    EXPECT_EQ(first.ptr, AEEventHandlerUPP(got).ptr);
    EXPECT_EQ(1, refcon);

    /* Installing the same key replaces the entry. */
    ASSERT_EQ(noErr, AEInstallEventHandler("CLAS"_4, "EVNT"_4, second, 2,
                                           false));
    ASSERT_EQ(noErr, AEGetEventHandler("CLAS"_4, "EVNT"_4, &got, &refcon,
                                       false));
    EXPECT_EQ(second.ptr, AEEventHandlerUPP(got).ptr);
    EXPECT_EQ(2, refcon);

    /* 8.6: naming another handler than the installed one is noErr and
       removes nothing (aevt spec.hdlr.remove_other2) */
    EXPECT_EQ(noErr, AERemoveEventHandler("CLAS"_4, "EVNT"_4, first, false));
    ASSERT_EQ(noErr, AEGetEventHandler("CLAS"_4, "EVNT"_4, &got, &refcon, false));
    EXPECT_EQ(2, refcon);

    ASSERT_EQ(noErr, AERemoveEventHandler("CLAS"_4, "EVNT"_4, second, false));
    /* 8.6: one that isn't there is errAEHandlerNotFound (spec.hdlr.remove_absent) */
    EXPECT_EQ(errAEHandlerNotFound, AERemoveEventHandler("CLAS"_4, "EVNT"_4, second, false));
    EXPECT_EQ(errAEHandlerNotFound,
              AEGetEventHandler("CLAS"_4, "EVNT"_4, &got, &refcon, false));
}

#if 0 /* private/internal calls: see notes */
TEST_F(AppleEventsTest, DispatchPrefersMoreSpecificWildcardEntry)
{
    AEEventHandlerUPP exact((void *)SYN68K_TO_US(0x1000));
    AEEventHandlerUPP class_wild((void *)SYN68K_TO_US(0x1004));
    GUEST<AEEventHandlerUPP> got;
    GUEST<int32_t> refcon;

    ASSERT_EQ(noErr, AEInstallEventHandler("CLAS"_4, "EVNT"_4, exact, 1,
                                           false));
    ASSERT_EQ(noErr, AEInstallEventHandler("CLAS"_4, typeWildCard, class_wild,
                                           2, false));

    ASSERT_TRUE(ae_lookup_event_handler("CLAS"_4, "EVNT"_4, false,
                                        &got, &refcon));
    EXPECT_EQ(exact.ptr, AEEventHandlerUPP(got).ptr);

    ASSERT_TRUE(ae_lookup_event_handler("CLAS"_4, "OTHR"_4, false,
                                        &got, &refcon));
    EXPECT_EQ(class_wild.ptr, AEEventHandlerUPP(got).ptr);

    AERemoveEventHandler("CLAS"_4, "EVNT"_4, exact, false);
    AERemoveEventHandler("CLAS"_4, typeWildCard, class_wild, false);
}

#endif

#if 0 /* private/internal calls: see notes */
TEST_F(AppleEventsTest, PrivateHandlerTableSemantics)
{
    GUEST<AE_hdlr_table_h> table = nullptr;
    AE_hdlr_selector_t key;
    AE_hdlr_selector_t key_out;
    AE_hdlr_t hdlr;
    AE_hdlr_t result;

    key.sel0 = 1;
    key.sel1 = 2;
    hdlr.fn = (void *)SYN68K_TO_US(0x1234);
    hdlr.refcon = 7;

    ASSERT_EQ(noErr, _AE_hdlr_table_alloc(&table, 0, nullptr, 0x00080008, 16));

    ASSERT_EQ(noErr, _AE_hdlr_install(&hdlr, &key, nullptr, table));
    ASSERT_EQ(noErr, _AE_hdlr_lookup(&result, &key, nullptr, table));
    EXPECT_EQ((void *)SYN68K_TO_US(0x1234), (void *)result.fn);
    EXPECT_EQ(7, result.refcon);

    EXPECT_EQ(errAEDuplicateKey,
              _AE_hdlr_install_unique(&hdlr, &key, nullptr, table));

    ASSERT_EQ(noErr, _AE_hdlr_get_key(&key_out, 1, nullptr, table));
    EXPECT_EQ(1, key_out.sel0);
    EXPECT_EQ(2, key_out.sel1);
    EXPECT_EQ(errAEIndexOutOfRange,
              _AE_hdlr_get_key(&key_out, 2, nullptr, table));

    ASSERT_EQ(noErr, _AE_hdlr_delete(&key, nullptr, table));
    EXPECT_EQ(errAEKeyNotFound,
              _AE_hdlr_lookup(&result, &key, nullptr, table));
    /* Deleting an absent key still succeeds. */
    EXPECT_EQ(noErr, _AE_hdlr_delete(&key, nullptr, table));

    ASSERT_EQ(noErr, _AE_hdlr_table_free(nullptr, &table));
    EXPECT_EQ(nullptr, (AE_hdlr_table_h)table);
}

#endif

TEST_F(AppleEventsTest, ArrayRoundTrip)
{
    AEDescList list;
    GUEST<int16_t> input[3] = { 1, 2, 3 };
    GUEST<int16_t> output[3] = { 0, 0, 0 };
    GUEST<DescType> item_type;
    GUEST<Size> item_size;
    GUEST<int32_t> item_count;

    ASSERT_EQ(noErr, AECreateList(nullptr, 0, false, &list));
    ASSERT_EQ(noErr, AEPutArray(&list, kAEDataArray,
                                (AEArrayDataPointer)(void *)input,
                                typeSInt16, sizeof(int16_t), 3));

    /* 8.6: a data array doesn't come back out of the list AEPutArray made
       (aevt spec.array.get) */
    EXPECT_EQ(errAEWrongDataType, AEGetArray(&list, kAEDataArray,
                                             (AEArrayDataPointer)(void *)output,
                                             sizeof output, &item_type, &item_size,
                                             &item_count));
    EXPECT_EQ(typeWildCard, (DescType)item_type);
    EXPECT_EQ(-1, (Size)item_size);
    EXPECT_EQ(0, (int32_t)item_count);

    AEDisposeDesc(&list);
}

TEST_F(AppleEventsTest, ManagerInfo)
{
    GUEST<LONGINT> result;

    /* 8.6 and 9.0.4: version 1.0.5, no recorders (aevt spec.info); before
       8.0 paramErr, as 7.5.5's (Disk Copy) */
    uint32_t saved = system_version;
    system_version = 0x0860;
    ASSERT_EQ(noErr, AEManagerInfo(keyAEVersion, &result));
    EXPECT_EQ(0x01058000, result);
    ASSERT_EQ(noErr, AEManagerInfo(keyAERecorderCount, &result));
    EXPECT_EQ(0, result);
    system_version = 0x0755;
    EXPECT_EQ(paramErr, AEManagerInfo(keyAEVersion, &result));
    system_version = saved;
}
#endif
