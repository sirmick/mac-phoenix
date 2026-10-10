#include "gtest/gtest.h"

#include <base/mactype.h>
#include <IntlUtil.h>
#include <OSUtil.h>
#include <ScriptMgr.h>

#include <cstring>
#include <string>

using namespace Executor;

/* Dates and times to and from text, as AppleScript uses them: a date
   literal goes through StringToDate and StringToTime, and a date shown as
   text through IULDateString and IULTimeString. */
class DateTime : public testing::Test
{
protected:
    DateCacheRecord cache;
    LongDateRec rec;

    DateTime() { InitDateCache(&cache); }

    int date(const char *text, GUEST<int32_t> *used = nullptr)
    {
        memset(&rec, 0, sizeof rec);
        GUEST<int32_t> u;
        int status = StringToDate((Ptr)text, strlen(text), &cache, used ? used : &u, &rec);
        return status;
    }

    int time(const char *text)
    {
        GUEST<LONGINT> used;
        return StringToTime((Ptr)text, strlen(text), (Ptr)&cache, &used, (GUEST<Ptr> *)&rec);
    }

    static std::string str(ConstStringPtr p) { return std::string((const char *)p + 1, p[0]); }

    /* A LongDateTime for a local date and time. */
    struct Long { GUEST<uint32_t> hi, lo; };
    Long at(int y, int mo, int d, int h, int mi, int s)
    {
        DateTimeRec dtr = {};
        dtr.year = y; dtr.month = mo; dtr.day = d;
        dtr.hour = h; dtr.minute = mi; dtr.second = s;
        GUEST<ULONGINT> secs;
        DateToSeconds(&dtr, &secs);
        return { 0, secs };
    }
};

TEST_F(DateTime, LongDate)
{
    int status = date("Friday, October 9, 2026");
    EXPECT_EQ(0, status & 0x8000);
    EXPECT_TRUE(status & 1); // longDateFound
    EXPECT_EQ(2026, rec.year);
    EXPECT_EQ(10, rec.month);
    EXPECT_EQ(9, rec.day);
}

TEST_F(DateTime, MonthFirstOrDayFirst)
{
    date("Oct 9, 2026");
    EXPECT_EQ(10, rec.month);
    EXPECT_EQ(9, rec.day);
    EXPECT_EQ(2026, rec.year);
    date("9 October 2026");
    EXPECT_EQ(10, rec.month);
    EXPECT_EQ(9, rec.day);
    EXPECT_EQ(2026, rec.year);
}

TEST_F(DateTime, Numeric)
{
    date("10/9/2026");
    EXPECT_EQ(10, rec.month);
    EXPECT_EQ(9, rec.day);
    EXPECT_EQ(2026, rec.year);
    date("1/1/00");
    EXPECT_EQ(2000, rec.year); // two digits: the nearest century
}

TEST_F(DateTime, DateThenTime)
{
    const char *text = "Friday, October 9, 2026 4:11:52 PM";
    GUEST<int32_t> used;
    int status = date(text, &used);
    EXPECT_EQ(0, status & 0x8000);
    EXPECT_TRUE(status & 2); // leftOverChars: the time
    EXPECT_EQ(2026, rec.year);
    EXPECT_LT(used, (int32_t)strlen(text));
    EXPECT_EQ(0, time(text) & 0x8000);
    EXPECT_EQ(16, rec.hour);
    EXPECT_EQ(11, rec.minute);
    EXPECT_EQ(52, rec.second);
}

TEST_F(DateTime, TwelveOClock)
{
    time("12:00:00 AM");
    EXPECT_EQ(0, rec.hour);
    time("12:30 PM");
    EXPECT_EQ(12, rec.hour);
    EXPECT_EQ(30, rec.minute);
    time("16:05");
    EXPECT_EQ(16, rec.hour);
}

TEST_F(DateTime, NotADate)
{
    EXPECT_EQ(0x8400, date("hello") & 0xFFFF); // dateTimeNotFound
    EXPECT_EQ(0x8400, time("hello") & 0xFFFF);
}

TEST_F(DateTime, LongDateAndTimeStrings)
{
    Long t = at(2026, 10, 9, 16, 11, 52);
    Str255 s;
    IULDateString((GUEST<LongDateTime> *)&t, longDate, s, nullptr);
    EXPECT_EQ("Friday, October 9, 2026", str(s));
    IULTimeString((GUEST<LongDateTime> *)&t, true, s, nullptr);
    EXPECT_EQ("4:11:52 PM", str(s));
}

TEST_F(DateTime, LongSecondsToDateNoonIsPM)
{
    Long t = at(2026, 10, 9, 12, 0, 0);
    LongDateRec r;
    LongSecondsToDate((GUEST<ULONGINT> *)&t, &r);
    EXPECT_EQ(12, r.hour);
    EXPECT_EQ(1, r.pm);
}
