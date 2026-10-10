/* Copyright 1991 by Abacus Research and
 * Development, Inc.  All rights reserved.
 */

/* Forward declarations in ScriptMgr.h (DO NOT DELETE THIS LINE) */

#include <base/common.h>
#include <QuickDraw.h>
#include <IntlUtil.h>
#include <ScriptMgr.h>
#include <MemoryMgr.h>
#include <ToolboxUtil.h>
#include <OSUtil.h>
#include <ResourceMgr.h>
#include <FontMgr.h>
#include <ToolboxEvent.h>

#include <rsys/hook.h>
#include <quickdraw/quick.h>
#include <quickdraw/cquick.h>
#include <rsys/osutil.h>
#include <osevent/osevent.h>
#include <print/print.h>
#include <sane/floatconv.h>
#include <util/string.h>
#include <mman/mman.h>

#include <ctype.h>
#include <string>
#include <vector>

using namespace Executor;

/* MacPhoenix: the Script Manager's variables (Inside Macintosh: Text
 * 6-54..6-58), one Roman script. The settable ones keep what they are set
 * to; AppleScript, turning a date into text, sets smIntlForce around its
 * call and gave up (error -1) when that failed. Starting values are
 * Roman's (status: guess for smVersion and smCharPortion). */
namespace
{
enum
{
    smKeySwapV = 28, smGenFlagsV = 30, smOverrideV = 32, smCharPortionV = 34,
    smDoubleByteV = 36, smRegionCodeV = 40, smKeyDisableStateV = 42,
    kScriptVars = 44,
};

LONGINT script_vars[kScriptVars / 2] = {};
bool script_vars_ready = false;

LONGINT &script_var(INTEGER verb)
{
    if(!script_vars_ready)
    {
        script_vars_ready = true;
        script_vars[smVersion / 2] = 0x0750;
        script_vars[smEnabled / 2] = 1;
        script_vars[smCharPortionV / 2] = 0x0333;
        script_vars[smRegionCodeV / 2] = 0; /* verUS */
    }
    return script_vars[verb / 2];
}

/* What SetScriptManagerVariable may change; the rest describe the
   installed scripts. */
bool script_var_settable(INTEGER verb)
{
    switch(verb)
    {
        case smFontForce: case smIntlForce: case smForced: case smDefault:
        case smSysScript: case smAppScript: case smKeyScript: case smSysRef:
        case smKeyCache: case smKeySwapV: case smGenFlagsV: case smOverrideV:
        case smCharPortionV: case smRegionCodeV: case smKeyDisableStateV:
            return true;
        default:
            return false;
    }
}
}

LONGINT Executor::C_GetScriptManagerVariable(INTEGER verb)
{
    if(verb == smKCHRCache)
        return US_TO_SYN68K(ROMlib_kchr_ptr());
    if(verb < 0 || verb >= kScriptVars || (verb & 1))
    {
        warning_unexpected("unhandled selector `%d'", verb);
        return 0;
    }
    return script_var(verb);
}

OSErr Executor::C_SetScriptManagerVariable(INTEGER verb, LONGINT param)
{
    if(verb < 0 || verb >= kScriptVars || (verb & 1) || !script_var_settable(verb))
        return smVerbNotFound;
    script_var(verb) = param;
    return noErr;
}

/* MacPhoenix: Roman, the only script installed. The resource IDs come
   from its 'itlb' (ItlbRecord: itlbNumber, itlbDate, itlbSort, itlbFlags,
   itlbToken, itlbEncoding, itlbLang, itlbNumRep.b, itlbDateRep.b,
   itlbKeys, itlbIcon); the rest are Roman's values from Inside Macintosh:
   Text (status: guess where marked). Verbs past smScriptName use System
   7's Script.h names. */
LONGINT Executor::C_GetScriptVariable(INTEGER script, INTEGER verb)
{
    if(script != smRoman)
        return 0;
    auto itlb = [](int offset, bool byte = false) -> LONGINT {
        Handle h = GetResource("itlb"_4, smRoman);
        if(!h)
            return 0;
        return byte ? *(uint8_t *)(*h + offset) : (LONGINT) * (GUEST<INTEGER> *)(*h + offset);
    };
    switch(verb)
    {
        case smScriptVersion:
            return 0x0710; /* status: guess */
        case smScriptEnabled:
            return 0xFF; /* the script record's scriptValid byte */
        case smScriptRight: /* left to right */
        case smScriptJust:
        case smScriptRedraw:
        case smScriptMunged:
            return 0;
        case smScriptSysFond:
            return LM(SysFontFam);
        case smScriptAppFond:
            return LM(ApFontID);
        case smScriptNumber: /* the 'itlb' itself */
            return smRoman;
        case smScriptDate:
            return itlb(2);
        case smScriptSort:
            return itlb(4);
        case 22: /* smScriptFlags */
            return itlb(6);
        case 24: /* smScriptToken */
            return itlb(8);
        case 26: /* smScriptEncoding */
            return itlb(10);
        case 28: /* smScriptLang */
            return itlb(12);
        case 30: /* smScriptNumDate: numRep high byte, dateRep low */
            return itlb(14, true) << 8 | itlb(15, true);
        case smScriptKeys:
            return itlb(16);
        case smScriptIcon:
            return itlb(18);
        /* font and size: family in the high word */
        case 72: /* smScriptMonoFondSize: Monaco 9 */
            return 4L << 16 | 9;
        case 74: /* smScriptPrefFondSize: Geneva 12, status: guess */
            return 3L << 16 | 12;
        case 76: /* smScriptSmallFondSize: Geneva 9 */
            return 3L << 16 | 9;
        case 78: /* smScriptSysFondSize: Chicago 12 */
            return (LONGINT)LM(SysFontFam) << 16 | 12;
        case 80: /* smScriptAppFondSize: Geneva 12 */
            return (LONGINT)LM(ApFontID) << 16 | 12;
        case 82: /* smScriptHelpFondSize: Geneva 9 */
            return 3L << 16 | 9;
        case 84: /* smScriptValidStyles: all of them */
            return 0x7F;
        case 86: /* smScriptAliasStyle */
            return 0;
        default:
            warning_unexpected("unhandled selector `%d'", verb);
            return 0;
    }
}

OSErr Executor::C_SetScriptVariable(INTEGER script, INTEGER verb, LONGINT param)
{
    warning_unimplemented("");
    return smVerbNotFound;
}

INTEGER Executor::C_FontToScript(INTEGER fontnum)
{
    warning_unimplemented("");
    return 0;
}

/*
 * butchered Transliterate provided for Excel 3.0
 */

static char upper(char);
static char lower(char);

#define LOWERTOUPPEROFFSET 'A' - 'a'
static char upper(char ch)
{
    if(ch >= 'a' && ch <= 'z')
#if 1
        return ch + LOWERTOUPPEROFFSET;
#else /* 0 */
        return ch & ~0x20;
#endif /* 0 */
    else
        return ch;
}

#define UPPERTOLOWEROFFSET ('a' - 'A')
static char lower(char ch)
{
    if(ch >= 'A' && ch <= 'Z')
#if 1
        return ch + UPPERTOLOWEROFFSET;
#else /* 0 */
        return ch | 0x20;
#endif /* 0 */
    else
        return ch;
}

INTEGER Executor::C_Transliterate(Handle srch, Handle dsth, INTEGER target,
                                  LONGINT srcmask)
{
    char *sp, *dp, *ep;

    sp = (char *)*srch;
    dp = (char *)*dsth;
    ep = sp + GetHandleSize(srch);
    if(target & smTransLower)
    {
        if(target & smTransUpper)
            /*-->*/ return -1;
        while(sp < ep)
            *dp++ = lower(*sp++);
    }
    else if(target & smTransUpper)
    {
        while(sp < ep)
            *dp++ = upper(*sp++);
    }
    else
    {
        while(sp < ep)
            *dp++ = *sp++;
    }
    return 0;
}

/*
 * NOTE: These are all recent additions, made just before 1.2.2 was frozen.
 *	 They haven't been tested much, if at all.  In addition, much of
 *	 the code below just tries to return something "reasonable", although
 *	 not necessarily correct!
 */

INTEGER Executor::C_FontScript()
{
    warning_unimplemented("");
    return smRoman;
}

INTEGER Executor::C_IntlScript()
{
    warning_unimplemented("");
    return smRoman;
}

/* MacPhoenix. A key event with Command down whose character is test,
   either as typed or as the key gives it with Command released (so
   Command-Shift-period still matches '.'). */
Boolean Executor::C_IsCmdChar(const EventRecord *event, INTEGER test)
{
    if((event->what != keyDown && event->what != autoKey)
       || !(event->modifiers & cmdKey))
        return false;
    if((event->message & charCodeMask) == (uint8_t)test)
        return true;
    GUEST<uint32_t> state = 0;
    uint16_t code = ((event->message & keyCodeMask) >> 8)
        | (event->modifiers & 0xFF00 & ~cmdKey);
    uint32_t chars = KeyTranslate(ROMlib_kchr_ptr(), code, &state);
    return (chars & 0xFF) == (uint8_t)test || ((chars >> 16) & 0xFF) == (uint8_t)test;
}

void Executor::C_KeyScript(INTEGER scriptcode)
{
    warning_unimplemented("");
}

INTEGER Executor::C_CharType(Ptr textbufp, INTEGER offset)
{
    INTEGER retval;
    unsigned char c;
    ROMlib_hook(script_notsupported);

    retval = 0;
    c = textbufp[offset];
    if(!isalpha(c))
    {
        retval |= smCharPunct;
        if(isspace(c))
            retval |= smPunctBlank;
        else if(isdigit(c))
            retval |= smPunctNumber;
        else if(ispunct(c))
            retval |= smPunctSymbol;
    }
    else
    {
        retval |= smCharAscii;
        if(islower(c))
            retval |= smCharLower;
        else
            retval |= smCharUpper;
    }
    retval |= smCharLeft;
    retval |= smChar1byte;

    return retval;
}

void Executor::C_MeasureJust(Ptr textbufp, int16_t length, int16_t slop,
                             Ptr charlocs)
{
    if(slop)
        warning_unimplemented("slop = %d", slop);
    MeasureText(length, textbufp, charlocs);
}

void Executor::C_MeasureJustified(Ptr text, int32_t length, Fixed slop,
                              Ptr charLocs, JustStyleCode run_pos, Point numer,
                              Point denom)
{
    GUEST<Point> numerx, denomx;

    warning_unimplemented("slop = %d, run_pos = %d", slop, run_pos);

    numerx.v = numer.v;
    numerx.h = numer.h;
    denomx.v = denom.v;
    denomx.h = denom.h;

    xStdTxMeas(length, (uint8_t *)text, &numerx, &denomx,
               nullptr, (GUEST<int16_t> *)charLocs);
}

Boolean Executor::C_ParseTable(CharByteTable table)
{
    memset(table, 0, sizeof(CharByteTable));
    return true;
}

Boolean Executor::C_FillParseTable(CharByteTable table, ScriptCode script)
{
    /* ### should we even look at `script' */
    memset(table, 0, sizeof(CharByteTable));
    return true;
}

INTEGER Executor::C_CharacterByteType(Ptr textBuf, INTEGER textOffset,
                                      ScriptCode script)
{
    warning_unimplemented("");
    /* Single-byte character */
    return 0;
}

INTEGER Executor::C_CharacterType(Ptr textbufp, INTEGER offset,
                                  ScriptCode script)
{
    warning_unimplemented("");
    return CharType(textbufp, offset);
}

INTEGER Executor::C_TransliterateText(Handle srch, Handle dsth, INTEGER target,
                                      LONGINT srcmask, ScriptCode script)
{
    warning_unimplemented("");
    return Transliterate(srch, dsth, target, srcmask);
}

INTEGER Executor::C_Pixel2Char(Ptr textbufp, INTEGER len, INTEGER slop,
                               INTEGER pixwidth, Boolean *leftsidep)
{
    Point num, den;
    INTEGER retval;

    warning_unimplemented("poorly implemented");

    num.h = 1;
    num.v = 1;
    den.h = 1;
    den.v = 1;
    retval = C_PixelToChar(textbufp, len, slop << 16, pixwidth << 16, leftsidep,
                           0, 0, num, den);
    return retval;
}

INTEGER Executor::C_Char2Pixel(Ptr textbufp, INTEGER len, INTEGER slop,
                               INTEGER offset, SignedByte dir)
{
    INTEGER retval;
    Point num, den;

    warning_unimplemented("poorly implemented");

    num.h = 1;
    num.v = 1;
    den.h = 1;
    den.v = 1;

    retval = C_CharToPixel(textbufp, len, slop << 16, offset, dir, 0,
                           num, den);
    return retval;
}

void Executor::C_FindWord(Ptr textbufp, INTEGER length, INTEGER offset,
                          Boolean leftside, Ptr breaks, GUEST<INTEGER> *offsets)
{
    INTEGER start, stop;
    bool chasing_spaces_p;
    ROMlib_hook(script_notsupported);

    if(!leftside)
        --offset;
    if(offset < 0)
        offset = 0;

    chasing_spaces_p = isspace(textbufp[offset]);
    for(start = offset;
        start > 0 && !isspace(textbufp[start - 1]) == !chasing_spaces_p;
        --start)
        ;

    for(stop = offset;
        stop < length && !isspace(textbufp[stop]) == !chasing_spaces_p;
        ++stop)
        ;

    offsets[0] = start;
    offsets[1] = stop;
    offsets[2] = 0; /* Testing on Brute shows we should zero this memory */
    offsets[3] = 0;
    offsets[4] = 0;
    offsets[5] = 0;
    warning_unimplemented("poorly implemented");
}

void Executor::C_HiliteText(Ptr textbufp, INTEGER firstoffset,
                            INTEGER secondoffset, GUEST<INTEGER> *offsets)
{
    ROMlib_hook(script_notsupported);
    offsets[0] = firstoffset;
    offsets[1] = secondoffset;
    offsets[2] = 0;
    offsets[3] = 0;
    offsets[4] = 0;
    offsets[5] = 0;
}

static int16_t
count_spaces(Ptr textbufp, int16_t length)
{
    int16_t retval;

    retval = 0;
    while(length-- > 0)
        if(*textbufp++ == ' ')
            ++retval;

    return retval;
}

void Executor::C_DrawJust(Ptr textbufp, int16_t length, int16_t slop)
{
    GUEST<Fixed> save_sp_extra_x;
    int n_spaces;

    save_sp_extra_x = PORT_SP_EXTRA(qdGlobals().thePort);
    n_spaces = count_spaces(textbufp, length);
    if(n_spaces)
    {
        Fixed extra;

        extra = save_sp_extra_x + FixRatio(slop, n_spaces);
        PORT_SP_EXTRA(qdGlobals().thePort) = extra;
    }
    DrawText(textbufp, 0, length);
    PORT_SP_EXTRA(qdGlobals().thePort) = save_sp_extra_x;
}

/* ── StringToDate / StringToTime (Inside Macintosh: Text 5-72..5-80) ────
 * MacPhoenix. Dates as people and AppleScript write them: month and weekday
 * names from 'itl1' (English if it has none; a unique prefix of three or
 * more letters will do), numbers in 'itl0's date order, any of / - . , and
 * spaces between. Times as h:mm[:ss] with an optional AM/PM ('itl0's
 * strings or am/pm). The results go into the LongDateRec; the status bits
 * are Apple's (fatal ones have the high bit). */
namespace
{
enum
{
    longDateFound = 1,
    leftOverChars = 2,
    dateTimeNotFound = 0x8400,
    dateTimeInvalid = 0x8800,
};

struct DateToken
{
    enum Kind { Number, Word, Colon } kind;
    int value;          /* Number */
    std::string word;   /* Word, lower case */
    int start, end;     /* offsets in the text */
};

std::vector<DateToken> date_tokens(const char *text, int length)
{
    std::vector<DateToken> tokens;
    for(int i = 0; i < length;)
    {
        unsigned char c = text[i];
        if(isdigit(c))
        {
            DateToken t{ DateToken::Number, 0, {}, i, i };
            while(i < length && isdigit((unsigned char)text[i]))
                t.value = t.value * 10 + (text[i++] - '0');
            t.end = i;
            tokens.push_back(t);
        }
        else if(isalpha(c))
        {
            DateToken t{ DateToken::Word, 0, {}, i, i };
            while(i < length && (isalpha((unsigned char)text[i]) || text[i] == '.'))
            {
                if(text[i] != '.')
                    t.word += (char)tolower((unsigned char)text[i]);
                ++i;
            }
            t.end = i;
            tokens.push_back(t);
        }
        else if(c == ':')
        {
            tokens.push_back({ DateToken::Colon, 0, {}, i, i + 1 });
            ++i;
        }
        else
            ++i; /* separators: / - . , space */
    }
    return tokens;
}

std::string lower_pstring(ConstStringPtr p)
{
    std::string s((const char *)p + 1, p[0]);
    for(char &c : s)
        c = (char)tolower((unsigned char)c);
    return s;
}

/* 1..12 for a month name or its abbreviation, 0 if the word isn't one. */
int month_named(const std::string &w)
{
    static const char *const english[12] = {
        "january", "february", "march", "april", "may", "june", "july",
        "august", "september", "october", "november", "december"
    };
    if(w.size() < 3)
        return 0;
    Handle h = GetIntlResource(1);
    for(int m = 0; m < 12; m++)
    {
        std::string name = h ? lower_pstring(((Intl1Ptr)*h)->months[m]) : english[m];
        if(name.compare(0, w.size(), w) == 0 || std::string(english[m]).compare(0, w.size(), w) == 0)
            return m + 1;
    }
    return 0;
}

bool weekday_named(const std::string &w)
{
    static const char *const english[7] = {
        "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday"
    };
    if(w.size() < 3)
        return false;
    Handle h = GetIntlResource(1);
    for(int d = 0; d < 7; d++)
    {
        std::string name = h ? lower_pstring(((Intl1Ptr)*h)->days[d]) : english[d];
        if(name.compare(0, w.size(), w) == 0 || std::string(english[d]).compare(0, w.size(), w) == 0)
            return true;
    }
    return false;
}

int full_year(int year, int digits)
{
    if(digits > 2)
        return year;
    /* Two digits: the century that puts it nearest today. */
    GUEST<ULONGINT> now;
    GetDateTime(&now);
    DateTimeRec today;
    SecondsToDate(now, &today);
    int century = today.year / 100 * 100;
    int y = century + year;
    if(y > today.year + 50)
        y -= 100;
    else if(y < today.year - 50)
        y += 100;
    return y;
}
}

String2DateStatus Executor::C_StringToDate(
    Ptr textp, int32_t length, DateCachePtr cache,
    GUEST<int32_t> *length_used_ret, LongDatePtr date_time)
{
    const char *text = (const char *)textp;
    std::vector<DateToken> tokens = date_tokens(text, length);
    int month = 0, used = 0;
    std::vector<const DateToken *> numbers;
    for(const DateToken &t : tokens)
    {
        if(t.kind == DateToken::Colon)
            break; /* a time follows */
        if(t.kind == DateToken::Number)
        {
            /* A number right before a colon is the time's hour. */
            if(&t + 1 < tokens.data() + tokens.size() && (&t + 1)->kind == DateToken::Colon)
                break;
            if(numbers.size() == 3)
                break;
            numbers.push_back(&t);
            used = t.end;
        }
        else if(int m = month_named(t.word))
        {
            if(month)
                break;
            month = m;
            used = t.end;
        }
        else if(weekday_named(t.word))
            used = t.end;
        else
            break; /* am, pm or something else: not the date */
    }

    int day = 0, year = 0, year_digits = 4;
    bool have_year = false;
    auto digits = [](const DateToken *t) { return t->end - t->start; };
    if(month)
    {
        /* Named month: the day is the number that can be one. */
        for(const DateToken *t : numbers)
            if(!day && t->value >= 1 && t->value <= 31 && digits(t) <= 2 && numbers.size() > 1)
                day = t->value;
            else if(!have_year)
                year = t->value, year_digits = digits(t), have_year = true;
        if(numbers.size() == 1)
            day = numbers[0]->value, have_year = false;
    }
    else if(numbers.size() >= 2)
    {
        int order = mdy;
        if(Handle h = GetIntlResource(0))
            order = ((Intl0Ptr)*h)->dateOrder;
        const DateToken *a = numbers[0], *b = numbers[1];
        const DateToken *c = numbers.size() > 2 ? numbers[2] : nullptr;
        const DateToken *y = nullptr;
        switch(order)
        {
            case dmy: day = a->value; month = b->value; y = c; break;
            case ymd:
                if(c) { y = a; month = b->value; day = c->value; }
                else { month = a->value; day = b->value; }
                break;
            default: month = a->value; day = b->value; y = c; break;
        }
        if(y)
            year = y->value, year_digits = digits(y), have_year = true;
    }
    else
    {
        *length_used_ret = 0;
        return (String2DateStatus)dateTimeNotFound;
    }

    if(!have_year)
    {
        GUEST<ULONGINT> now;
        GetDateTime(&now);
        DateTimeRec today;
        SecondsToDate(now, &today);
        year = today.year;
    }
    else
        year = full_year(year, year_digits);

    *length_used_ret = used;
    if(month < 1 || month > 12 || day < 1 || day > 31)
        return (String2DateStatus)dateTimeInvalid;
    date_time->year = year;
    date_time->month = month;
    date_time->day = day;
    int status = longDateFound;
    for(int i = used; i < length; i++)
        if(!isspace((unsigned char)text[i]) && text[i] != ',')
        {
            status |= leftOverChars;
            break;
        }
    return (String2DateStatus)status;
}

String2DateStatus Executor::C_StringToTime(
    Ptr textp, LONGINT len, Ptr cachep, GUEST<LONGINT> *lenusedp,
    GUEST<Ptr> *datetimep)
{
    const char *text = (const char *)textp;
    LongDatePtr date_time = (LongDatePtr)datetimep; /* a LongDateRec, as for StringToDate */
    std::vector<DateToken> tokens = date_tokens(text, len);
    for(size_t i = 0; i + 2 < tokens.size(); i++)
    {
        if(tokens[i].kind != DateToken::Number || tokens[i + 1].kind != DateToken::Colon
           || tokens[i + 2].kind != DateToken::Number)
            continue;
        int hour = tokens[i].value, minute = tokens[i + 2].value, second = 0;
        size_t next = i + 3;
        int used = tokens[i + 2].end;
        if(next + 1 < tokens.size() && tokens[next].kind == DateToken::Colon
           && tokens[next + 1].kind == DateToken::Number)
        {
            second = tokens[next + 1].value;
            used = tokens[next + 1].end;
            next += 2;
        }
        if(next < tokens.size() && tokens[next].kind == DateToken::Word)
        {
            std::string w = tokens[next].word;
            std::string morn = "am", eve = "pm";
            if(Handle h = GetIntlResource(0))
            {
                Intl0Ptr i0 = (Intl0Ptr)*h;
                std::string m((const char *)&i0->mornStr, 4), e((const char *)&i0->eveStr, 4);
                morn.clear();
                eve.clear();
                for(char c : m)
                    if(c && c != ' ')
                        morn += (char)tolower((unsigned char)c);
                for(char c : e)
                    if(c && c != ' ')
                        eve += (char)tolower((unsigned char)c);
                if(morn.empty())
                    morn = "am";
                if(eve.empty())
                    eve = "pm";
            }
            if(w == morn || w == "am")
            {
                if(hour == 12)
                    hour = 0;
                used = tokens[next].end;
            }
            else if(w == eve || w == "pm")
            {
                if(hour < 12)
                    hour += 12;
                used = tokens[next].end;
            }
        }
        *lenusedp = used;
        if(hour > 23 || minute > 59 || second > 59)
            return (String2DateStatus)dateTimeInvalid;
        date_time->hour = hour;
        date_time->minute = minute;
        date_time->second = second;
        int status = longDateFound;
        for(int k = used; k < len; k++)
            if(!isspace((unsigned char)text[k]))
            {
                status |= leftOverChars;
                break;
            }
        return (String2DateStatus)status;
    }
    *lenusedp = 0;
    return (String2DateStatus)dateTimeNotFound;
}

StyledLineBreakCode Executor::C_StyledLineBreak(
    Ptr textp, int32_t length, int32_t text_start, int32_t text_end,
    int32_t flags, GUEST<Fixed> *text_width_fp, GUEST<int32_t> *text_offset)
{
    char *text = (char *)textp;
    /* the index into `text' that began the last word, which is where we
     want to break if the current word extends past the end of the
     current line */
    int last_word_break = -1;
    char current_char;
    int current_index;
    int text_width;
    int width = 0;

    /* ### are we losing information here? */
    text_width = Fix2Long(*text_width_fp);

    for(current_index = text_start, current_char = text[current_index];
        current_index < text_end;
        current_index++, current_char = text[current_index])
    {
        /* ### do we do this? */
        if(current_char == '\r')
        {
            *text_offset = current_index + 1;
            return smBreakWord;
        }

        if(current_index > text_start
           && current_char != ' '
           && text[current_index - 1] == ' ')
        {
            last_word_break = current_index - 1;
        }

        width += CharWidth(current_char);

        if(width > text_width)
        {
            /* we got our char */
            if(last_word_break == -1)
            {
                /* d'oh, we are on the first word */
                if(*text_offset)
                {
                    /* beginning of the line, break here */
                    *text_offset = current_index - 1;
                    return smBreakChar;
                }
                *text_offset = current_index - 1;
                return smBreakWord;
            }
            else
            {
                *text_offset = last_word_break;
                return smBreakWord;
            }
        }
    }
    /* if we got here, that means the run did not extend past the end of
     the current line */
    *text_width_fp = Long2Fix(text_width - width);
    *text_offset = current_index;
    return smBreakOverflow;
}

INTEGER Executor::C_ReplaceText(Handle base_text, Handle subst_text, Str15 key)
{
    INTEGER retval;

    warning_unimplemented("not tested much");

    retval = 0;
    HLockGuard guard(subst_text);
    Ptr p;
    INTEGER len;
    LONGINT offset;
    LONGINT l;

    p = (Ptr)*subst_text;
    len = GetHandleSize(subst_text);
    offset = 0;
    while(retval >= 0 && (l = Munger(base_text, offset, (Ptr)key + 1, key[0], nullptr, 1)) >= 0)
    {
        offset = Munger(base_text,
                        l, (Ptr)key + 1, key[0], p, len);
        if(offset < 0)
            retval = offset;
        else
            ++retval;
    }

    return retval;
}

/* StringToExtended is now StringToExtended */
FormatStatus Executor::C_StringToExtended(
    Str255 string, NumFormatStringRec *formatp, NumberParts *partsp,
    Extended80 *xp) /* TTS TODO */
{
    FormatStatus retval;
    double d;
    char buf[256];

    memcpy(buf, string + 1, string[0]);
    buf[string[0]] = 0;
    sscanf(buf, "%lg", &d);
    ieee_to_x80((ieee_t)d, xp);
    warning_unimplemented("");
    retval = noErr;
    return retval;
}

/* ── Number formats (Inside Macintosh: Text 5-39..5-57) ─────────────────
 * MacPhoenix. StringToFormatRec keeps the format string itself in the
 * (opaque to applications) NumFormatStringRec, and ExtendedToString lays a
 * number out by it: up to three parts (positive;negative;zero), digit
 * places 0 (always shown) and # (shown when significant), a decimal point,
 * an exponent (e+ always signs it, e- only when negative; written E+/E-
 * as on 7.5.5), and literal text around the digits. AppleScript writes its
 * reals this way: "###0.0############;..." and "0.0#############e+##0;...".
 * Thousands separators and ^ places aren't laid out yet. */
namespace
{
enum
{
    fFormatOK = 0,
    fEmptyFormatString = 13,
    kFormatRecVersion = 0x4D, /* ours: the raw format string follows */
};

struct NumPart
{
    std::string prefix, suffix;
    int intReq = 0, decReq = 0, decOpt = 0, expReq = 0;
    bool point = false, exp = false, expPlus = false;
};

NumPart parse_num_part(const std::string &f)
{
    NumPart p;
    enum { Before, Int, Dec, Exp, After } state = Before;
    for(size_t i = 0; i < f.size(); i++)
    {
        char c = f[i];
        std::string &lit = state == Before ? p.prefix : p.suffix;
        if(c == '\'')
        {
            size_t close = f.find('\'', i + 1);
            lit += f.substr(i + 1, (close == std::string::npos ? f.size() : close) - i - 1);
            i = close == std::string::npos ? f.size() : close;
            if(state != Before)
                state = After;
        }
        else if(c == '#' || c == '0' || c == '^')
        {
            if(state == Before || state == Int)
            {
                state = Int;
                if(c == '0')
                    p.intReq++;
            }
            else if(state == Dec)
                (c == '0' ? p.decReq : p.decOpt)++;
            else if(state == Exp)
            {
                if(c == '0')
                    p.expReq++;
            }
            else
                lit += c;
        }
        else if(c == '.' && (state == Before || state == Int))
        {
            p.point = true;
            state = Dec;
        }
        else if((c == 'e' || c == 'E') && i + 1 < f.size() && (f[i + 1] == '+' || f[i + 1] == '-')
                && state != Before && state != After)
        {
            p.exp = true;
            p.expPlus = f[i + 1] == '+';
            state = Exp;
            ++i;
        }
        else if(c == ',' && (state == Int || state == Before))
            ; /* thousands separator */
        else
        {
            if(state != Before)
                state = After;
            (state == Before ? p.prefix : p.suffix) += c;
        }
    }
    return p;
}

/* digits with trailing zeros past the required count taken off */
void trim_decimals(std::string &digits, int required)
{
    while((int)digits.size() > required && digits.back() == '0')
        digits.pop_back();
}

std::string format_number(long double v, const NumPart &p)
{
    int maxDec = p.decReq + p.decOpt;
    std::string intDigits, decDigits, expText;
    char buf[128];
    if(p.exp)
    {
        int e = 0;
        if(v != 0)
        {
            snprintf(buf, sizeof buf, "%.*Le", maxDec, v);
            char *ep = strchr(buf, 'e');
            e = atoi(ep + 1);
            *ep = 0;
        }
        else
            snprintf(buf, sizeof buf, "%.*Lf", maxDec, (long double)0);
        std::string m = buf;
        size_t dot = m.find('.');
        intDigits = m.substr(0, dot);
        decDigits = dot == std::string::npos ? "" : m.substr(dot + 1);
        std::string ed = std::to_string(e < 0 ? -e : e);
        while((int)ed.size() < p.expReq)
            ed = "0" + ed;
        expText = std::string("E") + (e < 0 ? "-" : p.expPlus ? "+" : "") + ed;
    }
    else
    {
        snprintf(buf, sizeof buf, "%.*Lf", maxDec, v);
        std::string m = buf;
        size_t dot = m.find('.');
        intDigits = m.substr(0, dot);
        decDigits = dot == std::string::npos ? "" : m.substr(dot + 1);
        if(p.intReq == 0 && intDigits == "0")
            intDigits.clear();
        while((int)intDigits.size() < p.intReq)
            intDigits = "0" + intDigits;
    }
    trim_decimals(decDigits, p.decReq);
    std::string out = p.prefix + intDigits;
    if(p.point && !decDigits.empty())
        out += "." + decDigits;
    return out + expText + p.suffix;
}
}

FormatStatus Executor::C_ExtendedToString(
    Extended80 *xp, NumFormatStringRec *formatp, NumberParts *partsp,
    Str255 string)
{
    long double val = x80_to_ieee(xp);
    char buf[256];

    if(!formatp || formatp->fVersion != kFormatRecVersion)
    {
        /* Not one of ours: as near as printf gets. */
        snprintf(buf, sizeof buf, "%Lg", val);
        str255_from_c_string(string, buf);
        return fFormatOK;
    }

    std::string all((const char *)formatp->data, formatp->fLength);
    std::vector<std::string> parts;
    for(size_t from = 0;;)
    {
        size_t semi = all.find(';', from);
        parts.push_back(all.substr(from, semi == std::string::npos ? std::string::npos : semi - from));
        if(semi == std::string::npos)
            break;
        from = semi + 1;
    }

    std::string out;
    if(val == 0 && parts.size() > 2)
        out = format_number(0, parse_num_part(parts[2]));
    else if(val < 0 && parts.size() > 1)
        out = format_number(-val, parse_num_part(parts[1]));
    else if(val < 0)
        out = "-" + format_number(-val, parse_num_part(parts[0]));
    else
        out = format_number(val, parse_num_part(parts[0]));
    str255_from_c_string(string, out.c_str());
    return fFormatOK;
}

FormatStatus Executor::C_StringToFormatRec(
    ConstStringPtr in_string, NumberParts *partsp,
    NumFormatStringRec *out_string)
{
    int n = std::min<int>(in_string[0], sizeof out_string->data);
    if(n == 0)
        return fEmptyFormatString;
    out_string->fLength = n;
    out_string->fVersion = kFormatRecVersion;
    memcpy(out_string->data, in_string + 1, n);
    return fFormatOK;
}

ToggleResults Executor::C_ToggleDate(LongDateTime *lsecsp, LongDateField field,
                                     DateDelta delta, INTEGER ch,
                                     TogglePB *paramsp) /* TTS TODO */
{
    ToggleResults retval;

    warning_unimplemented("");
    retval = 0;
    return retval;
}

INTEGER Executor::C_TruncString(INTEGER width, Str255 string,
                                TruncCode code) /* TTS TODO */
{
    warning_unimplemented("");

    /* ### claim we didn't have to truncate the string */
    return Truncated;
}

LONGINT Executor::C_VisibleLength(Ptr textp, LONGINT len)
{
    warning_unimplemented("poorly implemented -- what about other white space");

    warning_trace_info("%.*s", (int)len, textp);
    while(len > 0 && textp[len - 1] == ' ')
        --len;
    return len;
}

void Executor::C_LongDateToSeconds(LongDateRec *ldatep, GUEST<ULONGINT> *secs_outp)
{
    long long secs;
    LONGINT high, low;
    INTEGER hour;

    hour = ldatep->hour;
    if(ldatep->pm && hour < 12)
        hour += 12;

    secs = ROMlib_long_long_secs(ldatep->year, ldatep->month,
                                 ldatep->day, hour,
                                 ldatep->minute, ldatep->second);
    high = secs >> 32;
    low = secs;
    secs_outp[0] = high;
    secs_outp[1] = low;
}

void Executor::C_LongSecondsToDate(GUEST<ULONGINT> *secs_inp, LongDateRec *ldatep)

{
    long long secs;
    INTEGER pm;

    secs = ((long long)secs_inp[0] << 32) | secs_inp[1];
    date_to_swapped_fields(secs, &ldatep->year, &ldatep->month, &ldatep->day,
                           &ldatep->hour, &ldatep->minute, &ldatep->second,
                           &ldatep->dayOfWeek, &ldatep->dayOfYear,
                           &ldatep->weekOfYear);

    pm = (ldatep->hour >= 12) ? 1 : 0; /* noon is PM */
    ldatep->pm = pm;
}

/*
 * NOTE: At least some of these need to be implemented if we want
 *	 Resolve to work
 */

#if 0
ParseTable

PortionText

FindScriptRun

IsSpecialFont

RawPrinterValues

NPixel2Char

CharToPixel

DrawJustified

PortionLine

ReplaceText

TruncText

TrunString

NFindWord

ValidDate

StringToExtended

ExtendedToString

FormatRecToString

StringToFormatRec

ToggleDate

LongSecondsToDate

LongDateToSeconds

IntlTokenize

GetFormatOrder
#endif

OSErr Executor::C_InitDateCache(DateCachePtr cache) /* TTS TODO */
{
    warning_unimplemented("");
    return noErr;
}

INTEGER Executor::C_CharByte(Ptr textBuf, INTEGER textOffset)
{
    warning_unimplemented("");
    /* Single-byte character */
    return 0;
}

Fixed Executor::C_PortionLine(Ptr textPtr, LONGINT textLen,
                              JustStyleCode styleRunPosition, Point numer,
                              Point denom)
{
    Fixed retval;

    warning_unimplemented("");
    retval = 0x10000;
    return retval;
}

void Executor::C_DrawJustified(Ptr textPtr, LONGINT textLength, Fixed slop,
                               JustStyleCode styleRunPosition, Point numer,
                               Point denom)
{
    GUEST<Point> swapped_numer;
    GUEST<Point> swapped_denom;

    warning_unimplemented("poorly implemented");
    swapped_numer.h = numer.h;
    swapped_numer.v = numer.v;
    swapped_denom.h = denom.h;
    swapped_denom.v = denom.v;
    text_helper(textLength, textPtr, &swapped_numer, &swapped_denom, 0, 0,
                text_helper_draw);
}

ScriptRunStatus Executor::C_FindScriptRun(
    Ptr textPtr, LONGINT textLen, GUEST<LONGINT> *lenUsedp)
{
    warning_unimplemented("");
    *lenUsedp = 1;
    return 0;
}

INTEGER Executor::C_PixelToChar(Ptr textBuf, LONGINT textLen, Fixed slop,
                                Fixed pixelWidth, Boolean *leadingEdgep,
                                GUEST<Fixed> *widthRemainingp,
                                JustStyleCode styleRunPosition, Point numer,
                                Point denom)
{
    GUEST<INTEGER> *locs;
    INTEGER retval, i;
    GUEST<Point> swapped_numer;
    GUEST<Point> swapped_denom;
    INTEGER int_pix_width;

    warning_unimplemented("poorly implemented");

    locs = (GUEST<INTEGER> *)alloca(sizeof(INTEGER) * (textLen + 1));

    swapped_numer.h = numer.h;
    swapped_numer.v = numer.v;
    swapped_denom.h = denom.h;
    swapped_denom.v = denom.v;

    int_pix_width = pixelWidth >> 16;

    text_helper(textLen, textBuf, &swapped_numer, &swapped_denom, 0, locs,
                text_helper_measure);

    /* NOTE: we could distribute slop here, or we could adjust text_helper
     to account for slop (probably better in the long run).  Right now,
     we do neither.  Ick. */

    if(int_pix_width >= locs[textLen])
    {
        retval = textLen;
        *leadingEdgep = false;
        *widthRemainingp = pixelWidth - (locs[textLen] << 16);
    }
    else
    {
        *widthRemainingp = -1;
        for(i = 0; int_pix_width > locs[i]; ++i)
            ;
        if((i > 0) && ((int_pix_width - locs[i - 1]) > (locs[i] - int_pix_width)))
        {
            retval = i - 1;
            *leadingEdgep = false;
        }
        else
        {
            retval = i;
            *leadingEdgep = true;
        }
    }
    return retval;
}

INTEGER Executor::C_CharToPixel(Ptr textBuf, LONGINT textLen, Fixed slop,
                                LONGINT offset, INTEGER direction,
                                JustStyleCode styleRunPosition, Point numer,
                                Point denom)
{
    INTEGER retval;
    GUEST<Point> swapped_numer, swapped_denom;

    warning_unimplemented("poorly implemented");

    swapped_numer.h = numer.h;
    swapped_numer.v = numer.v;
    swapped_denom.h = denom.h;
    swapped_denom.v = denom.v;
    retval = text_helper(offset, textBuf, &swapped_numer, &swapped_denom,
                         0, 0, text_helper_measure);
    retval += (slop / textLen) >> 16;
    return retval;
}

void Executor::C_LowercaseText(Ptr textp, INTEGER len, ScriptCode script)
{
    warning_unimplemented("poorly implemented");
}

void Executor::C_UppercaseText(Ptr textp, INTEGER len, ScriptCode script)
{
    ROMlib_UprString((StringPtr)textp, false, len);
}

void Executor::C_StripDiacritics(Ptr textp, INTEGER len, ScriptCode script)
{
    warning_unimplemented("poorly implemented");
}

void Executor::C_UppercaseStripDiacritics(
    Ptr textp, INTEGER len, ScriptCode script)
{
    ROMlib_UprString((StringPtr)textp, true, len);
}

void Executor::C_TextUtilFunctions(
    short selector,
    Ptr textp,
    INTEGER len,
    ScriptCode script)
{
    switch(selector)
    {
        case 0x0000:
            C_LowercaseText(textp, len, script);
            break;
        case 0x0200:
            C_StripDiacritics(textp, len, script);
            break;
        case 0x0400:
            C_UppercaseText(textp, len, script);
            break;
        case 0x0600:
            C_UppercaseStripDiacritics(textp, len, script);
            break;
    }
}

