#include "gtest/gtest.h"

#include "compat.h"

#ifdef EXECUTOR
#include <ToolboxEvent.h>
#include <OSEvent.h>
#include <ResourceMgr.h>
#else
#include <Events.h>
#include <Resources.h>
#endif

TEST(Events, FlushThenGet)
{
    FlushEvents(everyEvent, 0);

    EventRecord e;
    bool gotEvent = GetOSEvent(everyEvent, &e);
    long ticks = TickCount();

    EXPECT_FALSE(gotEvent);
    EXPECT_EQ(nullEvent, e.what);
    EXPECT_LE(e.when, ticks);
    EXPECT_GT(e.when + 5, ticks);
}


TEST(Events, PostKey)
{
    FlushEvents(everyEvent, 0);

    PostEvent(keyDown, 0 | 'a');

    EventRecord e;
    bool gotEvent = GetOSEvent(everyEvent, &e);
    long ticks = TickCount();

    EXPECT_TRUE(gotEvent);
    EXPECT_EQ(keyDown, e.what);
    EXPECT_LE(e.when, ticks);
    EXPECT_GT(e.when + 5, ticks);
    EXPECT_EQ(0 | 'a', e.message);
    EXPECT_EQ(btnState, e.modifiers & 0xFF80);

    EXPECT_EQ(0, (ptr_from_longint<uint8_t*>(0x174))[0]);
    PostEvent(keyUp, 0 | 'a');


    gotEvent = GetOSEvent(everyEvent, &e);
    ticks = TickCount();

    EXPECT_FALSE(gotEvent);
    EXPECT_EQ(nullEvent, e.what);
    EXPECT_LE(e.when, ticks);
    EXPECT_GT(e.when + 5, ticks);
}



TEST(Events, PostKeyMod)
{
    FlushEvents(everyEvent, 0);

        // set command key in KeyMap
    (ptr_from_longint<uint8_t*>(0x174))[6] |= 0x80;

    PostEvent(keyDown, 0 | 'a');

        // clear command key in KeyMap
    (ptr_from_longint<uint8_t*>(0x174))[6] &= ~0x80;

    EventRecord e;
    bool gotEvent = GetOSEvent(everyEvent, &e);
    long ticks = TickCount();

    EXPECT_TRUE(gotEvent);
    EXPECT_EQ(keyDown, e.what);
    EXPECT_LE(e.when, ticks);
    EXPECT_GT(e.when + 5, ticks);
    EXPECT_EQ(0 | 'a', e.message);
    EXPECT_EQ(btnState | cmdKey, e.modifiers & 0xFF80);

    EXPECT_EQ(0, (ptr_from_longint<uint8_t*>(0x174))[0]);

    gotEvent = GetOSEvent(everyEvent, &e);
    ticks = TickCount();

    EXPECT_FALSE(gotEvent);
    EXPECT_EQ(nullEvent, e.what);
    EXPECT_LE(e.when, ticks);
    EXPECT_GT(e.when + 5, ticks);
}

TEST(Events, Button)
{
    EXPECT_FALSE(Button());
    auto saveButton = *ptr_from_longint<uint8_t*>(0x172);
    EXPECT_NE(0, saveButton);

    *ptr_from_longint<uint8_t*>(0x172) = 0;
    EXPECT_TRUE(Button());

    *ptr_from_longint<uint8_t*>(0x172) = saveButton;
    EXPECT_FALSE(Button());
}


TEST(Events, ButtonMod)
{
    FlushEvents(everyEvent, 0);

    EXPECT_FALSE(Button());
    auto saveButton = *ptr_from_longint<uint8_t*>(0x172);
    EXPECT_NE(0, saveButton);

    *ptr_from_longint<uint8_t*>(0x172) = 0;
    EXPECT_TRUE(Button());

    PostEvent(keyDown, 0 | 'a');

    EventRecord e;
    bool gotEvent = GetOSEvent(everyEvent, &e);
    long ticks = TickCount();

    EXPECT_TRUE(gotEvent);
    EXPECT_EQ(keyDown, e.what);
    EXPECT_LE(e.when, ticks);
    EXPECT_GT(e.when + 5, ticks);
    EXPECT_EQ(0 | 'a', e.message);
    EXPECT_EQ(0, e.modifiers & 0xFF80);

    *ptr_from_longint<uint8_t*>(0x172) = saveButton;
    EXPECT_FALSE(Button());
}


TEST(Events, PostNullChar)
{
    FlushEvents(everyEvent, 0);

    PostEvent(keyDown, 0);

    EventRecord e;
    bool gotEvent = GetOSEvent(everyEvent, &e);

    EXPECT_TRUE(gotEvent);
    EXPECT_EQ(keyDown, e.what);
    EXPECT_EQ(0, e.message);
}

TEST(Events, KeyTranslate)
{
    Handle h = GetResource('KCHR', 0);
    
    uint32_t state = 0;
    uint32_t result = 0;

    // translate the 'a' key
    result = KeyTranslate(*h, 0, PTR(state));
    EXPECT_EQ('a', result & 0x00FF00FF);
    EXPECT_EQ(0, state);

    // result for keyup is the same
    result = KeyTranslate(*h, 0x80, PTR(state));
    EXPECT_EQ('a', result & 0x00FF00FF);
    EXPECT_EQ(0, state);

    // translate the 'u' key
    result = KeyTranslate(*h, 0x20, PTR(state));
    EXPECT_EQ('u', result & 0x00FF00FF);
    EXPECT_EQ(0, state);

    // result for keyup is the same
    result = KeyTranslate(*h, 0x80 | 0x20, PTR(state));
    EXPECT_EQ('u', result & 0x00FF00FF);
    EXPECT_EQ(0, state);

    // add the shift key to get 'U'
    result = KeyTranslate(*h, shiftKey | 0x20, PTR(state));
    EXPECT_EQ('U', result & 0x00FF00FF);
    EXPECT_EQ(0, state);

    // same for keyup
    result = KeyTranslate(*h, shiftKey | 0x80 | 0x20, PTR(state));
    EXPECT_EQ('U', result & 0x00FF00FF);
    EXPECT_EQ(0, state);

    // translate option-u; that's a dead key. No output, state changed.
    result = KeyTranslate(*h, optionKey | 0x20, PTR(state));
    EXPECT_EQ(0, result & 0x00FF00FF);
    EXPECT_NE(0, state);

    // next 'u' keypress gives umlaut, resets state
    result = KeyTranslate(*h, 0x20, PTR(state));
    EXPECT_EQ(0x9f, result & 0x00FF00FF);   // ü (umlaut u)
    EXPECT_EQ(0, state);

    // on keyup: no dead keys, we get just the dots
    result = KeyTranslate(*h, optionKey | 0x80 | 0x20, PTR(state));
    EXPECT_EQ(0xac, result & 0x00FF00FF);   // umlaut dots
    EXPECT_EQ(0, state);

    // start again with option-u
    result = KeyTranslate(*h, optionKey | 0x20, PTR(state));
    EXPECT_EQ(0, result & 0x00FF00FF);
    EXPECT_NE(0, state);

    // translating the corresponding keyup leaves the state unchanged
    auto unchangedState = state;
    result = KeyTranslate(*h, optionKey | 0x80 | 0x20, PTR(state));
    EXPECT_EQ(0xac, result & 0x00FF00FF);   // umlaut dots
    EXPECT_EQ(unchangedState, state);

    // ... so we can still get the umlaut
    result = KeyTranslate(*h, 0x20, PTR(state));
    EXPECT_EQ(0x9f, result & 0x00FF00FF);   // ü (umlaut u)
    EXPECT_EQ(0, state);

}
