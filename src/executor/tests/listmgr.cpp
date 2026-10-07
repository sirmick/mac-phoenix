#include "gtest/gtest.h"

#include "compat.h"
#ifdef EXECUTOR
#include <ListMgr.h>
#include <WindowMgr.h>
#include <ControlMgr.h>
#else
#include <Lists.h>
#include <Windows.h>
#endif

struct ListMgr : public testing::Test
{
    WindowPtr window;
    ListHandle list = nullptr;
    Rect view = {0,0,200,400};
    Rect bounds = {0,0,7,3};

    ListMgr()
    {
        Rect r = {50,50,250,450};
        window = NewWindow(nullptr, &r, (StringPtr)"", false, 0, (WindowPtr)-1, false, 0);
        SetPort(window);
    }

    ~ListMgr()
    {
        if(list)
            LDispose(list);
        DisposeWindow(window);
    }
};

TEST_F(ListMgr, LNew)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, false, false, false, false);

    EXPECT_EQ(0, (*list)->rView.top);
    EXPECT_EQ(0, (*list)->rView.left);
    EXPECT_EQ(200, (*list)->rView.bottom);
    EXPECT_EQ(400, (*list)->rView.right);

    EXPECT_EQ(window, (*list)->port);
    EXPECT_EQ(16, (*list)->cellSize.v);
    EXPECT_EQ(42, (*list)->cellSize.h);
    EXPECT_EQ(nullptr, (*list)->vScroll);
    EXPECT_EQ(nullptr, (*list)->hScroll);
    EXPECT_EQ(0, (*list)->selFlags);
    EXPECT_EQ(true, (*list)->lActive);
    EXPECT_EQ(0, (*list)->lReserved);

    EXPECT_EQ(nullptr, (*list)->userHandle);
    EXPECT_EQ(0, (*list)->dataBounds.top);
    EXPECT_EQ(0, (*list)->dataBounds.left);
    EXPECT_EQ(7, (*list)->dataBounds.bottom);
    EXPECT_EQ(3, (*list)->dataBounds.right);

    Handle cells = (Handle)(*list)->cells;

    EXPECT_NE(nullptr, cells);
    EXPECT_EQ(0, GetHandleSize(cells));
}

TEST_F(ListMgr, flagsDefault)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, false, false, false, false);
    EXPECT_EQ(0x1C, (*list)->listFlags);
}

TEST_F(ListMgr, flagsDraw)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, true, false, false, false);
    EXPECT_EQ(0x14, (*list)->listFlags);
}
TEST_F(ListMgr, flagsGrow)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, false, true, false, false);
    EXPECT_EQ(0x3C, (*list)->listFlags);
}
TEST_F(ListMgr, flagsScrollH)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, false, false, true, false);
    EXPECT_EQ(0x1D, (*list)->listFlags);
}
TEST_F(ListMgr, flagsScrollV)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, false, false, false, true);
    EXPECT_EQ(0x1E, (*list)->listFlags);
}


TEST_F(ListMgr, visible)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, false, false, false, false);
    EXPECT_EQ(0, (*list)->visible.top);
    EXPECT_EQ(0, (*list)->visible.left);
    EXPECT_EQ(13, (*list)->visible.bottom);
    EXPECT_EQ(10, (*list)->visible.right);
}

TEST_F(ListMgr, scrollLayoutH)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, false, false, true, false);
    ASSERT_NE(nullptr, (*list)->hScroll);

    ControlHandle scroll = (*list)->hScroll;

    EXPECT_EQ(200, (*scroll)->contrlRect.top);
    EXPECT_EQ(-1, (*scroll)->contrlRect.left);
    EXPECT_EQ(216, (*scroll)->contrlRect.bottom);
    EXPECT_EQ(401, (*scroll)->contrlRect.right);
}

TEST_F(ListMgr, scrollLayoutV)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, false, false, false, true);
    ASSERT_NE(nullptr, (*list)->vScroll);

    ControlHandle scroll = (*list)->vScroll;

    EXPECT_EQ(-1, (*scroll)->contrlRect.top);
    EXPECT_EQ(400, (*scroll)->contrlRect.left);
    EXPECT_EQ(201, (*scroll)->contrlRect.bottom);
    EXPECT_EQ(416, (*scroll)->contrlRect.right);
}

TEST_F(ListMgr, scrollLayoutVGrow)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, false, true, false, true);
    ASSERT_NE(nullptr, (*list)->vScroll);

    ControlHandle scroll = (*list)->vScroll;

    EXPECT_EQ(-1, (*scroll)->contrlRect.top);
    EXPECT_EQ(400, (*scroll)->contrlRect.left);
    // Okay, the grow flag does *not* do what I thought it does.
    // The vertical scroll bar is *not* adjusted to make room for a grow box.
    //     EXPECT_EQ(201-16, (*scroll)->contrlRect.bottom);
    EXPECT_EQ(201, (*scroll)->contrlRect.bottom);
    EXPECT_EQ(416, (*scroll)->contrlRect.right);
}

TEST_F(ListMgr, scrollRangeV)
{
    bounds = {0,0,70,40};
    list = LNew(&view,&bounds, {16,42}, 0, window, true, false, false, true);
    ASSERT_NE(nullptr, (*list)->vScroll);

    ControlHandle scroll = (*list)->vScroll;

    // the mac only sets up contrlMin/contrlMax when the scroll bars are active
    // Executor sets them up right away, but this shouldn't be a problem,
    // so don't assert
    //EXPECT_EQ(0, (*scroll)->contrlMin);
    //EXPECT_EQ(0, (*scroll)->contrlMax);

    LActivate(true, list);

    EXPECT_EQ(0, (*scroll)->contrlMin);
    EXPECT_EQ(58, (*scroll)->contrlMax);

    LAddRow(0,1,list);

        // LAddRow does not update scroll bar min/max
    EXPECT_EQ(0, (*scroll)->contrlMin);
    EXPECT_EQ(58, (*scroll)->contrlMax);

    // No idea how to get the List Manager to update min/max
    //EXPECT_EQ(0, (*scroll)->contrlMin);
    //EXPECT_EQ(59, (*scroll)->contrlMax);
}




TEST_F(ListMgr, maxIndex)
{
    list = LNew(&view,&bounds, {16,42}, 0, window, false, false, false, false);

    EXPECT_EQ(42, (*list)->maxIndex);

    EXPECT_EQ(0x56 + (7*3+1)*2, GetHandleSize((Handle)list));

    for(int i = 0; i < (7*3+1); i++)
        EXPECT_EQ(0, (*list)->cellArray[i]) << " i = " << i;
}

TEST_F(ListMgr, maxIndex2)
{
    bounds = {0,0,70,40};
    list = LNew(&view,&bounds, {16,42}, 0, window, false, false, false, false);

    EXPECT_EQ(5600, (*list)->maxIndex);
    EXPECT_EQ(0x56 + (70*40+1)*2, GetHandleSize((Handle)list));
}