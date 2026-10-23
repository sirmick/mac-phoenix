/* A Bison parser, made by GNU Bison 3.8.2.  */

/* Bison implementation for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2021 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* C LALR(1) parser skeleton written by Richard Stallman, by
   simplifying the original so-called "semantic" parser.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

/* All symbols defined below should begin with yy or YY, to avoid
   infringing on user name space.  This should be done even for local
   variables, as they might otherwise be expanded by user macros.
   There are some unavoidable exceptions within include files to
   define necessary library symbols; they are noted "INFRINGES ON
   USER NAME SPACE" below.  */

/* Identify Bison output, and Bison version.  */
#define YYBISON 30802

/* Bison version string.  */
#define YYBISON_VERSION "3.8.2"

/* Skeleton name.  */
#define YYSKELETON_NAME "yacc.c"

/* Pure parsers.  */
#define YYPURE 0

/* Push parsers.  */
#define YYPUSH 0

/* Pull parsers.  */
#define YYPULL 1




/* First part of user prologue.  */

#include <base/common.h>
#include <prefs/options.h>
#include <prefs/prefs.h>
#include <rsys/version.h>
#include <prefs/crc.h>
#include <prefs/parse.h>
#include <rsys/gestalt.h>
#include <rsys/launch.h>
#include <print/print.h>

/* Copyright 1992 - 2000 by Abacus Research and Development, Inc.
 * All rights reserved.
 */

using namespace Executor;

static long yylex( void );
static void freestring( char *string );
static char validblock = 1;
static unsigned short crcval;

FILE *configfile;
int32_t ROMlib_options = 0;
int ROMlib_desired_bpp = 0;

void yyerror(const char *str);


# ifndef YY_CAST
#  ifdef __cplusplus
#   define YY_CAST(Type, Val) static_cast<Type> (Val)
#   define YY_REINTERPRET_CAST(Type, Val) reinterpret_cast<Type> (Val)
#  else
#   define YY_CAST(Type, Val) ((Type) (Val))
#   define YY_REINTERPRET_CAST(Type, Val) ((Type) (Val))
#  endif
# endif
# ifndef YY_NULLPTR
#  if defined __cplusplus
#   if 201103L <= __cplusplus
#    define YY_NULLPTR nullptr
#   else
#    define YY_NULLPTR 0
#   endif
#  else
#   define YY_NULLPTR ((void*)0)
#  endif
# endif


/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    STRINGCONSTANT = 258,          /* STRINGCONSTANT  */
    HEXCONSTANT = 259,             /* HEXCONSTANT  */
    INTEGERCONSTANT = 260,         /* INTEGERCONSTANT  */
    CHAR4CONSTANT = 261,           /* CHAR4CONSTANT  */
    OPTIONSCONSTANT = 262,         /* OPTIONSCONSTANT  */
    WINDOWNAME = 263,              /* WINDOWNAME  */
    DONGLEFAMILY = 264,            /* DONGLEFAMILY  */
    SYSTEMVERSION = 265,           /* SYSTEMVERSION  */
    COMMENTS = 266,                /* COMMENTS  */
    RDELAY = 267,                  /* RDELAY  */
    SCREENSIZE = 268,              /* SCREENSIZE  */
    MACSIZE = 269,                 /* MACSIZE  */
    SCREENLOCATION = 270,          /* SCREENLOCATION  */
    REFRESHNUMBER = 271,           /* REFRESHNUMBER  */
    CRC = 272,                     /* CRC  */
    OPTIONS = 273,                 /* OPTIONS  */
    CACHEPROBLEMS = 274,           /* CACHEPROBLEMS  */
    BITSNUMBER = 275,              /* BITSNUMBER  */
    APPLECHAR = 276,               /* APPLECHAR  */
    PRINTERNAME = 277,             /* PRINTERNAME  */
    PRINTERTYPELABEL = 278,        /* PRINTERTYPELABEL  */
    WIN32TOKEN = 279,              /* WIN32TOKEN  */
    MACCDROM = 280,                /* MACCDROM  */
    RIGHT_BUTTON_MODIFIER = 281,   /* RIGHT_BUTTON_MODIFIER  */
    GESTALT = 282,                 /* GESTALT  */
    APP_VERSION = 283,             /* APP_VERSION  */
    PRVERSION = 284                /* PRVERSION  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
 long number; char *cptr; std::string *strptr; std::pair<int,int> *pptr; int *iptr;
	  unsigned long rtype;


};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;


int yyparse (void);



/* Symbol kind.  */
enum yysymbol_kind_t
{
  YYSYMBOL_YYEMPTY = -2,
  YYSYMBOL_YYEOF = 0,                      /* "end of file"  */
  YYSYMBOL_YYerror = 1,                    /* error  */
  YYSYMBOL_YYUNDEF = 2,                    /* "invalid token"  */
  YYSYMBOL_STRINGCONSTANT = 3,             /* STRINGCONSTANT  */
  YYSYMBOL_HEXCONSTANT = 4,                /* HEXCONSTANT  */
  YYSYMBOL_INTEGERCONSTANT = 5,            /* INTEGERCONSTANT  */
  YYSYMBOL_CHAR4CONSTANT = 6,              /* CHAR4CONSTANT  */
  YYSYMBOL_OPTIONSCONSTANT = 7,            /* OPTIONSCONSTANT  */
  YYSYMBOL_WINDOWNAME = 8,                 /* WINDOWNAME  */
  YYSYMBOL_DONGLEFAMILY = 9,               /* DONGLEFAMILY  */
  YYSYMBOL_SYSTEMVERSION = 10,             /* SYSTEMVERSION  */
  YYSYMBOL_COMMENTS = 11,                  /* COMMENTS  */
  YYSYMBOL_RDELAY = 12,                    /* RDELAY  */
  YYSYMBOL_SCREENSIZE = 13,                /* SCREENSIZE  */
  YYSYMBOL_MACSIZE = 14,                   /* MACSIZE  */
  YYSYMBOL_SCREENLOCATION = 15,            /* SCREENLOCATION  */
  YYSYMBOL_REFRESHNUMBER = 16,             /* REFRESHNUMBER  */
  YYSYMBOL_CRC = 17,                       /* CRC  */
  YYSYMBOL_OPTIONS = 18,                   /* OPTIONS  */
  YYSYMBOL_CACHEPROBLEMS = 19,             /* CACHEPROBLEMS  */
  YYSYMBOL_BITSNUMBER = 20,                /* BITSNUMBER  */
  YYSYMBOL_APPLECHAR = 21,                 /* APPLECHAR  */
  YYSYMBOL_PRINTERNAME = 22,               /* PRINTERNAME  */
  YYSYMBOL_PRINTERTYPELABEL = 23,          /* PRINTERTYPELABEL  */
  YYSYMBOL_WIN32TOKEN = 24,                /* WIN32TOKEN  */
  YYSYMBOL_MACCDROM = 25,                  /* MACCDROM  */
  YYSYMBOL_RIGHT_BUTTON_MODIFIER = 26,     /* RIGHT_BUTTON_MODIFIER  */
  YYSYMBOL_GESTALT = 27,                   /* GESTALT  */
  YYSYMBOL_APP_VERSION = 28,               /* APP_VERSION  */
  YYSYMBOL_PRVERSION = 29,                 /* PRVERSION  */
  YYSYMBOL_30_ = 30,                       /* '{'  */
  YYSYMBOL_31_ = 31,                       /* '}'  */
  YYSYMBOL_32_ = 32,                       /* ';'  */
  YYSYMBOL_33_ = 33,                       /* '('  */
  YYSYMBOL_34_ = 34,                       /* ','  */
  YYSYMBOL_35_ = 35,                       /* ')'  */
  YYSYMBOL_36_ = 36,                       /* ':'  */
  YYSYMBOL_37_ = 37,                       /* '='  */
  YYSYMBOL_38_ = 38,                       /* '.'  */
  YYSYMBOL_39_ = 39,                       /* '-'  */
  YYSYMBOL_YYACCEPT = 40,                  /* $accept  */
  YYSYMBOL_configuration = 41,             /* configuration  */
  YYSYMBOL_assignmentsandblocks = 42,      /* assignmentsandblocks  */
  YYSYMBOL_assignment = 43,                /* assignment  */
  YYSYMBOL_block = 44,                     /* block  */
  YYSYMBOL_assignments = 45,               /* assignments  */
  YYSYMBOL_blockbegin = 46,                /* blockbegin  */
  YYSYMBOL_stringassignment = 47,          /* stringassignment  */
  YYSYMBOL_sysversassignment = 48,         /* sysversassignment  */
  YYSYMBOL_prversassignment = 49,          /* prversassignment  */
  YYSYMBOL_numerassignment = 50,           /* numerassignment  */
  YYSYMBOL_stringvar = 51,                 /* stringvar  */
  YYSYMBOL_numervar = 52,                  /* numervar  */
  YYSYMBOL_optionvalues = 53,              /* optionvalues  */
  YYSYMBOL_optionsvar = 54,                /* optionsvar  */
  YYSYMBOL_optionsassignment = 55,         /* optionsassignment  */
  YYSYMBOL_arrayassignment = 56,           /* arrayassignment  */
  YYSYMBOL_pairassignment = 57,            /* pairassignment  */
  YYSYMBOL_gestaltassignment = 58,         /* gestaltassignment  */
  YYSYMBOL_number = 59,                    /* number  */
  YYSYMBOL_gestalt_entry = 60,             /* gestalt_entry  */
  YYSYMBOL_pairvar = 61,                   /* pairvar  */
  YYSYMBOL_unboundedassignment = 62,       /* unboundedassignment  */
  YYSYMBOL_unboundedvar = 63,              /* unboundedvar  */
  YYSYMBOL_numberlist = 64                 /* numberlist  */
};
typedef enum yysymbol_kind_t yysymbol_kind_t;




#ifdef short
# undef short
#endif

/* On compilers that do not define __PTRDIFF_MAX__ etc., make sure
   <limits.h> and (if available) <stdint.h> are included
   so that the code can choose integer types of a good width.  */

#ifndef __PTRDIFF_MAX__
# include <limits.h> /* INFRINGES ON USER NAME SPACE */
# if defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stdint.h> /* INFRINGES ON USER NAME SPACE */
#  define YY_STDINT_H
# endif
#endif

/* Narrow types that promote to a signed type and that can represent a
   signed or unsigned integer of at least N bits.  In tables they can
   save space and decrease cache pressure.  Promoting to a signed type
   helps avoid bugs in integer arithmetic.  */

#ifdef __INT_LEAST8_MAX__
typedef __INT_LEAST8_TYPE__ yytype_int8;
#elif defined YY_STDINT_H
typedef int_least8_t yytype_int8;
#else
typedef signed char yytype_int8;
#endif

#ifdef __INT_LEAST16_MAX__
typedef __INT_LEAST16_TYPE__ yytype_int16;
#elif defined YY_STDINT_H
typedef int_least16_t yytype_int16;
#else
typedef short yytype_int16;
#endif

/* Work around bug in HP-UX 11.23, which defines these macros
   incorrectly for preprocessor constants.  This workaround can likely
   be removed in 2023, as HPE has promised support for HP-UX 11.23
   (aka HP-UX 11i v2) only through the end of 2022; see Table 2 of
   <https://h20195.www2.hpe.com/V2/getpdf.aspx/4AA4-7673ENW.pdf>.  */
#ifdef __hpux
# undef UINT_LEAST8_MAX
# undef UINT_LEAST16_MAX
# define UINT_LEAST8_MAX 255
# define UINT_LEAST16_MAX 65535
#endif

#if defined __UINT_LEAST8_MAX__ && __UINT_LEAST8_MAX__ <= __INT_MAX__
typedef __UINT_LEAST8_TYPE__ yytype_uint8;
#elif (!defined __UINT_LEAST8_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST8_MAX <= INT_MAX)
typedef uint_least8_t yytype_uint8;
#elif !defined __UINT_LEAST8_MAX__ && UCHAR_MAX <= INT_MAX
typedef unsigned char yytype_uint8;
#else
typedef short yytype_uint8;
#endif

#if defined __UINT_LEAST16_MAX__ && __UINT_LEAST16_MAX__ <= __INT_MAX__
typedef __UINT_LEAST16_TYPE__ yytype_uint16;
#elif (!defined __UINT_LEAST16_MAX__ && defined YY_STDINT_H \
       && UINT_LEAST16_MAX <= INT_MAX)
typedef uint_least16_t yytype_uint16;
#elif !defined __UINT_LEAST16_MAX__ && USHRT_MAX <= INT_MAX
typedef unsigned short yytype_uint16;
#else
typedef int yytype_uint16;
#endif

#ifndef YYPTRDIFF_T
# if defined __PTRDIFF_TYPE__ && defined __PTRDIFF_MAX__
#  define YYPTRDIFF_T __PTRDIFF_TYPE__
#  define YYPTRDIFF_MAXIMUM __PTRDIFF_MAX__
# elif defined PTRDIFF_MAX
#  ifndef ptrdiff_t
#   include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  endif
#  define YYPTRDIFF_T ptrdiff_t
#  define YYPTRDIFF_MAXIMUM PTRDIFF_MAX
# else
#  define YYPTRDIFF_T long
#  define YYPTRDIFF_MAXIMUM LONG_MAX
# endif
#endif

#ifndef YYSIZE_T
# ifdef __SIZE_TYPE__
#  define YYSIZE_T __SIZE_TYPE__
# elif defined size_t
#  define YYSIZE_T size_t
# elif defined __STDC_VERSION__ && 199901 <= __STDC_VERSION__
#  include <stddef.h> /* INFRINGES ON USER NAME SPACE */
#  define YYSIZE_T size_t
# else
#  define YYSIZE_T unsigned
# endif
#endif

#define YYSIZE_MAXIMUM                                  \
  YY_CAST (YYPTRDIFF_T,                                 \
           (YYPTRDIFF_MAXIMUM < YY_CAST (YYSIZE_T, -1)  \
            ? YYPTRDIFF_MAXIMUM                         \
            : YY_CAST (YYSIZE_T, -1)))

#define YYSIZEOF(X) YY_CAST (YYPTRDIFF_T, sizeof (X))


/* Stored state numbers (used for stacks). */
typedef yytype_uint8 yy_state_t;

/* State numbers in computations.  */
typedef int yy_state_fast_t;

#ifndef YY_
# if defined YYENABLE_NLS && YYENABLE_NLS
#  if ENABLE_NLS
#   include <libintl.h> /* INFRINGES ON USER NAME SPACE */
#   define YY_(Msgid) dgettext ("bison-runtime", Msgid)
#  endif
# endif
# ifndef YY_
#  define YY_(Msgid) Msgid
# endif
#endif


#ifndef YY_ATTRIBUTE_PURE
# if defined __GNUC__ && 2 < __GNUC__ + (96 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_PURE __attribute__ ((__pure__))
# else
#  define YY_ATTRIBUTE_PURE
# endif
#endif

#ifndef YY_ATTRIBUTE_UNUSED
# if defined __GNUC__ && 2 < __GNUC__ + (7 <= __GNUC_MINOR__)
#  define YY_ATTRIBUTE_UNUSED __attribute__ ((__unused__))
# else
#  define YY_ATTRIBUTE_UNUSED
# endif
#endif

/* Suppress unused-variable warnings by "using" E.  */
#if ! defined lint || defined __GNUC__
# define YY_USE(E) ((void) (E))
#else
# define YY_USE(E) /* empty */
#endif

/* Suppress an incorrect diagnostic about yylval being uninitialized.  */
#if defined __GNUC__ && ! defined __ICC && 406 <= __GNUC__ * 100 + __GNUC_MINOR__
# if __GNUC__ * 100 + __GNUC_MINOR__ < 407
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")
# else
#  define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN                           \
    _Pragma ("GCC diagnostic push")                                     \
    _Pragma ("GCC diagnostic ignored \"-Wuninitialized\"")              \
    _Pragma ("GCC diagnostic ignored \"-Wmaybe-uninitialized\"")
# endif
# define YY_IGNORE_MAYBE_UNINITIALIZED_END      \
    _Pragma ("GCC diagnostic pop")
#else
# define YY_INITIAL_VALUE(Value) Value
#endif
#ifndef YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
# define YY_IGNORE_MAYBE_UNINITIALIZED_END
#endif
#ifndef YY_INITIAL_VALUE
# define YY_INITIAL_VALUE(Value) /* Nothing. */
#endif

#if defined __cplusplus && defined __GNUC__ && ! defined __ICC && 6 <= __GNUC__
# define YY_IGNORE_USELESS_CAST_BEGIN                          \
    _Pragma ("GCC diagnostic push")                            \
    _Pragma ("GCC diagnostic ignored \"-Wuseless-cast\"")
# define YY_IGNORE_USELESS_CAST_END            \
    _Pragma ("GCC diagnostic pop")
#endif
#ifndef YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_BEGIN
# define YY_IGNORE_USELESS_CAST_END
#endif


#define YY_ASSERT(E) ((void) (0 && (E)))

#if !defined yyoverflow

/* The parser invokes alloca or malloc; define the necessary symbols.  */

# ifdef YYSTACK_USE_ALLOCA
#  if YYSTACK_USE_ALLOCA
#   ifdef __GNUC__
#    define YYSTACK_ALLOC __builtin_alloca
#   elif defined __BUILTIN_VA_ARG_INCR
#    include <alloca.h> /* INFRINGES ON USER NAME SPACE */
#   elif defined _AIX
#    define YYSTACK_ALLOC __alloca
#   elif defined _MSC_VER
#    include <malloc.h> /* INFRINGES ON USER NAME SPACE */
#    define alloca _alloca
#   else
#    define YYSTACK_ALLOC alloca
#    if ! defined _ALLOCA_H && ! defined EXIT_SUCCESS
#     include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
      /* Use EXIT_SUCCESS as a witness for stdlib.h.  */
#     ifndef EXIT_SUCCESS
#      define EXIT_SUCCESS 0
#     endif
#    endif
#   endif
#  endif
# endif

# ifdef YYSTACK_ALLOC
   /* Pacify GCC's 'empty if-body' warning.  */
#  define YYSTACK_FREE(Ptr) do { /* empty */; } while (0)
#  ifndef YYSTACK_ALLOC_MAXIMUM
    /* The OS might guarantee only one guard page at the bottom of the stack,
       and a page size can be as small as 4096 bytes.  So we cannot safely
       invoke alloca (N) if N exceeds 4096.  Use a slightly smaller number
       to allow for a few compiler-allocated temporary stack slots.  */
#   define YYSTACK_ALLOC_MAXIMUM 4032 /* reasonable circa 2006 */
#  endif
# else
#  define YYSTACK_ALLOC YYMALLOC
#  define YYSTACK_FREE YYFREE
#  ifndef YYSTACK_ALLOC_MAXIMUM
#   define YYSTACK_ALLOC_MAXIMUM YYSIZE_MAXIMUM
#  endif
#  if (defined __cplusplus && ! defined EXIT_SUCCESS \
       && ! ((defined YYMALLOC || defined malloc) \
             && (defined YYFREE || defined free)))
#   include <stdlib.h> /* INFRINGES ON USER NAME SPACE */
#   ifndef EXIT_SUCCESS
#    define EXIT_SUCCESS 0
#   endif
#  endif
#  ifndef YYMALLOC
#   define YYMALLOC malloc
#   if ! defined malloc && ! defined EXIT_SUCCESS
void *malloc (YYSIZE_T); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
#  ifndef YYFREE
#   define YYFREE free
#   if ! defined free && ! defined EXIT_SUCCESS
void free (void *); /* INFRINGES ON USER NAME SPACE */
#   endif
#  endif
# endif
#endif /* !defined yyoverflow */

#if (! defined yyoverflow \
     && (! defined __cplusplus \
         || (defined YYSTYPE_IS_TRIVIAL && YYSTYPE_IS_TRIVIAL)))

/* A type that is properly aligned for any stack member.  */
union yyalloc
{
  yy_state_t yyss_alloc;
  YYSTYPE yyvs_alloc;
};

/* The size of the maximum gap between one aligned stack and the next.  */
# define YYSTACK_GAP_MAXIMUM (YYSIZEOF (union yyalloc) - 1)

/* The size of an array large to enough to hold all stacks, each with
   N elements.  */
# define YYSTACK_BYTES(N) \
     ((N) * (YYSIZEOF (yy_state_t) + YYSIZEOF (YYSTYPE)) \
      + YYSTACK_GAP_MAXIMUM)

# define YYCOPY_NEEDED 1

/* Relocate STACK from its old location to the new one.  The
   local variables YYSIZE and YYSTACKSIZE give the old and new number of
   elements in the stack, and YYPTR gives the new location of the
   stack.  Advance YYPTR to a properly aligned location for the next
   stack.  */
# define YYSTACK_RELOCATE(Stack_alloc, Stack)                           \
    do                                                                  \
      {                                                                 \
        YYPTRDIFF_T yynewbytes;                                         \
        YYCOPY (&yyptr->Stack_alloc, Stack, yysize);                    \
        Stack = &yyptr->Stack_alloc;                                    \
        yynewbytes = yystacksize * YYSIZEOF (*Stack) + YYSTACK_GAP_MAXIMUM; \
        yyptr += yynewbytes / YYSIZEOF (*yyptr);                        \
      }                                                                 \
    while (0)

#endif

#if defined YYCOPY_NEEDED && YYCOPY_NEEDED
/* Copy COUNT objects from SRC to DST.  The source and destination do
   not overlap.  */
# ifndef YYCOPY
#  if defined __GNUC__ && 1 < __GNUC__
#   define YYCOPY(Dst, Src, Count) \
      __builtin_memcpy (Dst, Src, YY_CAST (YYSIZE_T, (Count)) * sizeof (*(Src)))
#  else
#   define YYCOPY(Dst, Src, Count)              \
      do                                        \
        {                                       \
          YYPTRDIFF_T yyi;                      \
          for (yyi = 0; yyi < (Count); yyi++)   \
            (Dst)[yyi] = (Src)[yyi];            \
        }                                       \
      while (0)
#  endif
# endif
#endif /* !YYCOPY_NEEDED */

/* YYFINAL -- State number of the termination state.  */
#define YYFINAL  48
/* YYLAST -- Last index in YYTABLE.  */
#define YYLAST   152

/* YYNTOKENS -- Number of terminals.  */
#define YYNTOKENS  40
/* YYNNTS -- Number of nonterminals.  */
#define YYNNTS  25
/* YYNRULES -- Number of rules.  */
#define YYNRULES  62
/* YYNSTATES -- Number of states.  */
#define YYNSTATES  132

/* YYMAXUTOK -- Last valid token kind.  */
#define YYMAXUTOK   284


/* YYTRANSLATE(TOKEN-NUM) -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex, with out-of-bounds checking.  */
#define YYTRANSLATE(YYX)                                \
  (0 <= (YYX) && (YYX) <= YYMAXUTOK                     \
   ? YY_CAST (yysymbol_kind_t, yytranslate[YYX])        \
   : YYSYMBOL_YYUNDEF)

/* YYTRANSLATE[TOKEN-NUM] -- Symbol number corresponding to TOKEN-NUM
   as returned by yylex.  */
static const yytype_int8 yytranslate[] =
{
       0,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
      33,    35,     2,     2,    34,    39,    38,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,    36,    32,
       2,    37,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,    30,     2,    31,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     2,     2,     2,     2,
       2,     2,     2,     2,     2,     2,     1,     2,     3,     4,
       5,     6,     7,     8,     9,    10,    11,    12,    13,    14,
      15,    16,    17,    18,    19,    20,    21,    22,    23,    24,
      25,    26,    27,    28,    29
};

#if YYDEBUG
/* YYRLINE[YYN] -- Source line where rule number YYN was defined.  */
static const yytype_int16 yyrline[] =
{
       0,    76,    76,    77,    80,    81,    82,    83,    86,    87,
      88,    89,    90,    91,    94,   100,   101,   104,   110,   117,
     125,   132,   139,   148,   154,   162,   169,   171,   173,   175,
     177,   179,   181,   187,   189,   191,   193,   195,   199,   201,
     205,   215,   219,   224,   231,   232,   233,   236,   246,   252,
     254,   256,   260,   262,   266,   268,   270,   274,   275,   276,
     279,   284,   288
};
#endif

/** Accessing symbol of state STATE.  */
#define YY_ACCESSING_SYMBOL(State) YY_CAST (yysymbol_kind_t, yystos[State])

#if YYDEBUG || 0
/* The user-facing name of the symbol whose (internal) number is
   YYSYMBOL.  No bounds checking.  */
static const char *yysymbol_name (yysymbol_kind_t yysymbol) YY_ATTRIBUTE_UNUSED;

/* YYTNAME[SYMBOL-NUM] -- String name of the symbol SYMBOL-NUM.
   First, the terminals, then, starting at YYNTOKENS, nonterminals.  */
static const char *const yytname[] =
{
  "\"end of file\"", "error", "\"invalid token\"", "STRINGCONSTANT",
  "HEXCONSTANT", "INTEGERCONSTANT", "CHAR4CONSTANT", "OPTIONSCONSTANT",
  "WINDOWNAME", "DONGLEFAMILY", "SYSTEMVERSION", "COMMENTS", "RDELAY",
  "SCREENSIZE", "MACSIZE", "SCREENLOCATION", "REFRESHNUMBER", "CRC",
  "OPTIONS", "CACHEPROBLEMS", "BITSNUMBER", "APPLECHAR", "PRINTERNAME",
  "PRINTERTYPELABEL", "WIN32TOKEN", "MACCDROM", "RIGHT_BUTTON_MODIFIER",
  "GESTALT", "APP_VERSION", "PRVERSION", "'{'", "'}'", "';'", "'('", "','",
  "')'", "':'", "'='", "'.'", "'-'", "$accept", "configuration",
  "assignmentsandblocks", "assignment", "block", "assignments",
  "blockbegin", "stringassignment", "sysversassignment",
  "prversassignment", "numerassignment", "stringvar", "numervar",
  "optionvalues", "optionsvar", "optionsassignment", "arrayassignment",
  "pairassignment", "gestaltassignment", "number", "gestalt_entry",
  "pairvar", "unboundedassignment", "unboundedvar", "numberlist", YY_NULLPTR
};

static const char *
yysymbol_name (yysymbol_kind_t yysymbol)
{
  return yytname[yysymbol];
}
#endif

#define YYPACT_NINF (-48)

#define yypact_value_is_default(Yyn) \
  ((Yyn) == YYPACT_NINF)

#define YYTABLE_NINF (-1)

#define yytable_value_is_error(Yyn) \
  0

/* YYPACT[STATE-NUM] -- Index in YYTABLE of the portion describing
   STATE-NUM.  */
static const yytype_int16 yypact[] =
{
      80,   -48,   -48,   -14,   -48,   -48,   -48,   -48,   -48,   -48,
      -1,   -48,   -48,   -48,   -48,   -48,   -48,   -48,   -48,   -48,
       1,     5,     6,    40,    80,   -48,   -48,    14,   -48,   -48,
     -48,   -48,     9,    10,    12,   -48,   -48,   -48,   -48,    13,
      15,   -48,    16,     3,    35,    11,    44,     3,   -48,   -48,
     -48,   102,    48,     3,    24,    25,    26,    29,   -48,   -48,
       3,   -22,    28,    38,    49,    51,   -20,   -48,    56,    87,
      98,     2,     3,     3,    -2,   -48,   -48,     3,    55,   -48,
     -48,    96,   -48,     3,   101,   -48,   -48,   -48,   -48,   103,
     -12,   100,   104,   105,   -48,   -10,   -18,   106,   132,   107,
     -48,   -48,   108,     4,     3,     3,   -48,   110,     0,   -48,
       3,   109,   -48,   -48,   -48,   -48,   111,   113,   115,   -48,
     116,   -48,   117,   143,   -48,   118,   119,   -48,   -48,   -48,
     -48,   -48
};

/* YYDEFACT[STATE-NUM] -- Default reduction number in state STATE-NUM.
   Performed when YYTABLE does not specify something else to do.  Zero
   means the default is an error.  */
static const yytype_int8 yydefact[] =
{
       2,    26,    28,     0,    27,    33,    54,    56,    55,    34,
       0,    40,    60,    35,    36,    29,    30,    31,    32,    37,
       0,     0,     0,     0,     3,     4,     5,     0,     8,    12,
      13,    11,     0,     0,     0,    10,     9,    44,    46,     0,
       0,    45,     0,     0,     0,     0,     0,     0,     1,     6,
       7,     0,     0,     0,     0,     0,     0,     0,    49,    50,
       0,     0,     0,     0,     0,     0,     0,    15,     0,     0,
       0,     0,     0,     0,     0,    51,    20,     0,     0,    53,
      52,     0,    23,     0,     0,    16,    19,    25,    39,     0,
       0,     0,     0,     0,    62,     0,     0,     0,     0,     0,
      14,    42,     0,     0,     0,     0,    59,     0,     0,    21,
       0,     0,    18,    24,    41,    38,     0,     0,     0,    57,
       0,    61,     0,     0,    43,     0,     0,    58,    22,    17,
      48,    47
};

/* YYPGOTO[NTERM-NUM].  */
static const yytype_int16 yypgoto[] =
{
     -48,   -48,   -48,   -23,   128,   -48,   -48,   -48,   -48,   -48,
     -48,   -48,   -48,   -48,   -48,   -48,   -48,   -48,   -48,   -47,
     -48,   -48,   -48,   -48,   -48
};

/* YYDEFGOTO[NTERM-NUM].  */
static const yytype_int8 yydefgoto[] =
{
       0,    23,    24,    25,    26,    68,    27,    28,    29,    30,
      31,    32,    33,    90,    34,    35,    36,    37,    38,    61,
      39,    40,    41,    42,    95
};

/* YYTABLE[YYPACT[STATE-NUM]] -- What to do in state STATE-NUM.  If
   positive, shift that token.  If negative, reduce the rule whose
   number is the opposite.  If YYTABLE_NINF, syntax error.  */
static const yytype_uint8 yytable[] =
{
      66,    49,    58,    59,    58,    59,    70,    58,    59,    88,
      76,   115,    82,    75,   109,    63,    77,    64,    83,   102,
     110,   107,   103,    43,   108,    91,    92,    94,    67,    93,
      96,   120,    44,    89,    45,   116,    99,    60,    46,    60,
      48,    62,    60,    47,    51,    85,    52,    53,    65,    54,
      55,    69,    56,    57,    71,    72,    73,   117,   118,    74,
      97,   121,    78,   122,     1,     2,     3,     4,     5,     6,
       7,     8,     9,    79,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    80,    22,    81,    84,     1,     2,
       3,     4,     5,     6,     7,     8,     9,    10,    11,    12,
      13,    14,    15,    16,    17,    18,    19,    20,    21,    22,
       1,     2,     3,     4,     5,     6,     7,     8,     9,    86,
      11,    12,    13,    14,    15,    16,    17,    18,    19,    20,
      87,    22,    98,   100,   104,   101,   112,   106,   105,   113,
     114,   111,   119,   124,   125,   123,   126,   129,   127,   128,
     130,   131,    50
};

static const yytype_int8 yycheck[] =
{
      47,    24,     4,     5,     4,     5,    53,     4,     5,     7,
      32,     7,    32,    60,    32,     4,    38,     6,    38,    31,
      38,    31,    34,    37,    34,    72,    73,    74,    51,    31,
      77,    31,    33,    31,    33,    31,    83,    39,    33,    39,
       0,     6,    39,    37,    30,    68,    37,    37,     4,    37,
      37,     3,    37,    37,    30,    30,    30,   104,   105,    30,
       5,   108,    34,   110,     8,     9,    10,    11,    12,    13,
      14,    15,    16,    35,    18,    19,    20,    21,    22,    23,
      24,    25,    26,    27,    35,    29,    35,    31,     8,     9,
      10,    11,    12,    13,    14,    15,    16,    17,    18,    19,
      20,    21,    22,    23,    24,    25,    26,    27,    28,    29,
       8,     9,    10,    11,    12,    13,    14,    15,    16,    32,
      18,    19,    20,    21,    22,    23,    24,    25,    26,    27,
      32,    29,    36,    32,    34,    32,     4,    32,    34,    32,
      32,    35,    32,    32,    31,    36,    31,     4,    32,    32,
      32,    32,    24
};

/* YYSTOS[STATE-NUM] -- The symbol kind of the accessing symbol of
   state STATE-NUM.  */
static const yytype_int8 yystos[] =
{
       0,     8,     9,    10,    11,    12,    13,    14,    15,    16,
      17,    18,    19,    20,    21,    22,    23,    24,    25,    26,
      27,    28,    29,    41,    42,    43,    44,    46,    47,    48,
      49,    50,    51,    52,    54,    55,    56,    57,    58,    60,
      61,    62,    63,    37,    33,    33,    33,    37,     0,    43,
      44,    30,    37,    37,    37,    37,    37,    37,     4,     5,
      39,    59,     6,     4,     6,     4,    59,    43,    45,     3,
      59,    30,    30,    30,    30,    59,    32,    38,    34,    35,
      35,    35,    32,    38,    31,    43,    32,    32,     7,    31,
      53,    59,    59,    31,    59,    64,    59,     5,    36,    59,
      32,    32,    31,    34,    34,    34,    32,    31,    34,    32,
      38,    35,     4,    32,    32,     7,    31,    59,    59,    32,
      31,    59,    59,    36,    32,    31,    31,    32,    32,     4,
      32,    32
};

/* YYR1[RULE-NUM] -- Symbol kind of the left-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr1[] =
{
       0,    40,    41,    41,    42,    42,    42,    42,    43,    43,
      43,    43,    43,    43,    44,    45,    45,    46,    46,    47,
      48,    48,    48,    49,    49,    50,    51,    51,    51,    51,
      51,    51,    51,    52,    52,    52,    52,    52,    53,    53,
      54,    55,    55,    55,    56,    56,    56,    57,    58,    59,
      59,    59,    60,    60,    61,    61,    61,    62,    62,    62,
      63,    64,    64
};

/* YYR2[RULE-NUM] -- Number of symbols on the right-hand side of rule RULE-NUM.  */
static const yytype_int8 yyr2[] =
{
       0,     2,     0,     1,     1,     1,     2,     2,     1,     1,
       1,     1,     1,     1,     5,     1,     2,     8,     6,     4,
       4,     6,     8,     4,     6,     4,     1,     1,     1,     1,
       1,     1,     1,     1,     1,     1,     1,     1,     3,     1,
       1,     6,     5,     7,     1,     1,     1,     8,     8,     1,
       1,     2,     4,     4,     1,     1,     1,     6,     7,     5,
       1,     3,     1
};


enum { YYENOMEM = -2 };

#define yyerrok         (yyerrstatus = 0)
#define yyclearin       (yychar = YYEMPTY)

#define YYACCEPT        goto yyacceptlab
#define YYABORT         goto yyabortlab
#define YYERROR         goto yyerrorlab
#define YYNOMEM         goto yyexhaustedlab


#define YYRECOVERING()  (!!yyerrstatus)

#define YYBACKUP(Token, Value)                                    \
  do                                                              \
    if (yychar == YYEMPTY)                                        \
      {                                                           \
        yychar = (Token);                                         \
        yylval = (Value);                                         \
        YYPOPSTACK (yylen);                                       \
        yystate = *yyssp;                                         \
        goto yybackup;                                            \
      }                                                           \
    else                                                          \
      {                                                           \
        yyerror (YY_("syntax error: cannot back up")); \
        YYERROR;                                                  \
      }                                                           \
  while (0)

/* Backward compatibility with an undocumented macro.
   Use YYerror or YYUNDEF. */
#define YYERRCODE YYUNDEF


/* Enable debugging if requested.  */
#if YYDEBUG

# ifndef YYFPRINTF
#  include <stdio.h> /* INFRINGES ON USER NAME SPACE */
#  define YYFPRINTF fprintf
# endif

# define YYDPRINTF(Args)                        \
do {                                            \
  if (yydebug)                                  \
    YYFPRINTF Args;                             \
} while (0)




# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)                    \
do {                                                                      \
  if (yydebug)                                                            \
    {                                                                     \
      YYFPRINTF (stderr, "%s ", Title);                                   \
      yy_symbol_print (stderr,                                            \
                  Kind, Value); \
      YYFPRINTF (stderr, "\n");                                           \
    }                                                                     \
} while (0)


/*-----------------------------------.
| Print this symbol's value on YYO.  |
`-----------------------------------*/

static void
yy_symbol_value_print (FILE *yyo,
                       yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  FILE *yyoutput = yyo;
  YY_USE (yyoutput);
  if (!yyvaluep)
    return;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/*---------------------------.
| Print this symbol on YYO.  |
`---------------------------*/

static void
yy_symbol_print (FILE *yyo,
                 yysymbol_kind_t yykind, YYSTYPE const * const yyvaluep)
{
  YYFPRINTF (yyo, "%s %s (",
             yykind < YYNTOKENS ? "token" : "nterm", yysymbol_name (yykind));

  yy_symbol_value_print (yyo, yykind, yyvaluep);
  YYFPRINTF (yyo, ")");
}

/*------------------------------------------------------------------.
| yy_stack_print -- Print the state stack from its BOTTOM up to its |
| TOP (included).                                                   |
`------------------------------------------------------------------*/

static void
yy_stack_print (yy_state_t *yybottom, yy_state_t *yytop)
{
  YYFPRINTF (stderr, "Stack now");
  for (; yybottom <= yytop; yybottom++)
    {
      int yybot = *yybottom;
      YYFPRINTF (stderr, " %d", yybot);
    }
  YYFPRINTF (stderr, "\n");
}

# define YY_STACK_PRINT(Bottom, Top)                            \
do {                                                            \
  if (yydebug)                                                  \
    yy_stack_print ((Bottom), (Top));                           \
} while (0)


/*------------------------------------------------.
| Report that the YYRULE is going to be reduced.  |
`------------------------------------------------*/

static void
yy_reduce_print (yy_state_t *yyssp, YYSTYPE *yyvsp,
                 int yyrule)
{
  int yylno = yyrline[yyrule];
  int yynrhs = yyr2[yyrule];
  int yyi;
  YYFPRINTF (stderr, "Reducing stack by rule %d (line %d):\n",
             yyrule - 1, yylno);
  /* The symbols being reduced.  */
  for (yyi = 0; yyi < yynrhs; yyi++)
    {
      YYFPRINTF (stderr, "   $%d = ", yyi + 1);
      yy_symbol_print (stderr,
                       YY_ACCESSING_SYMBOL (+yyssp[yyi + 1 - yynrhs]),
                       &yyvsp[(yyi + 1) - (yynrhs)]);
      YYFPRINTF (stderr, "\n");
    }
}

# define YY_REDUCE_PRINT(Rule)          \
do {                                    \
  if (yydebug)                          \
    yy_reduce_print (yyssp, yyvsp, Rule); \
} while (0)

/* Nonzero means print parse trace.  It is left uninitialized so that
   multiple parsers can coexist.  */
int yydebug;
#else /* !YYDEBUG */
# define YYDPRINTF(Args) ((void) 0)
# define YY_SYMBOL_PRINT(Title, Kind, Value, Location)
# define YY_STACK_PRINT(Bottom, Top)
# define YY_REDUCE_PRINT(Rule)
#endif /* !YYDEBUG */


/* YYINITDEPTH -- initial size of the parser's stacks.  */
#ifndef YYINITDEPTH
# define YYINITDEPTH 200
#endif

/* YYMAXDEPTH -- maximum size the stacks can grow to (effective only
   if the built-in stack extension method is used).

   Do not make this value too large; the results are undefined if
   YYSTACK_ALLOC_MAXIMUM < YYSTACK_BYTES (YYMAXDEPTH)
   evaluated with infinite-precision integer arithmetic.  */

#ifndef YYMAXDEPTH
# define YYMAXDEPTH 10000
#endif






/*-----------------------------------------------.
| Release the memory associated to this symbol.  |
`-----------------------------------------------*/

static void
yydestruct (const char *yymsg,
            yysymbol_kind_t yykind, YYSTYPE *yyvaluep)
{
  YY_USE (yyvaluep);
  if (!yymsg)
    yymsg = "Deleting";
  YY_SYMBOL_PRINT (yymsg, yykind, yyvaluep, yylocationp);

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  YY_USE (yykind);
  YY_IGNORE_MAYBE_UNINITIALIZED_END
}


/* Lookahead token kind.  */
int yychar;

/* The semantic value of the lookahead symbol.  */
YYSTYPE yylval;
/* Number of syntax errors so far.  */
int yynerrs;




/*----------.
| yyparse.  |
`----------*/

int
yyparse (void)
{
    yy_state_fast_t yystate = 0;
    /* Number of tokens to shift before error messages enabled.  */
    int yyerrstatus = 0;

    /* Refer to the stacks through separate pointers, to allow yyoverflow
       to reallocate them elsewhere.  */

    /* Their size.  */
    YYPTRDIFF_T yystacksize = YYINITDEPTH;

    /* The state stack: array, bottom, top.  */
    yy_state_t yyssa[YYINITDEPTH];
    yy_state_t *yyss = yyssa;
    yy_state_t *yyssp = yyss;

    /* The semantic value stack: array, bottom, top.  */
    YYSTYPE yyvsa[YYINITDEPTH];
    YYSTYPE *yyvs = yyvsa;
    YYSTYPE *yyvsp = yyvs;

  int yyn;
  /* The return value of yyparse.  */
  int yyresult;
  /* Lookahead symbol kind.  */
  yysymbol_kind_t yytoken = YYSYMBOL_YYEMPTY;
  /* The variables used to return semantic value and location from the
     action routines.  */
  YYSTYPE yyval;



#define YYPOPSTACK(N)   (yyvsp -= (N), yyssp -= (N))

  /* The number of symbols on the RHS of the reduced rule.
     Keep to zero when no symbol should be popped.  */
  int yylen = 0;

  YYDPRINTF ((stderr, "Starting parse\n"));

  yychar = YYEMPTY; /* Cause a token to be read.  */

  goto yysetstate;


/*------------------------------------------------------------.
| yynewstate -- push a new state, which is found in yystate.  |
`------------------------------------------------------------*/
yynewstate:
  /* In all cases, when you get here, the value and location stacks
     have just been pushed.  So pushing a state here evens the stacks.  */
  yyssp++;


/*--------------------------------------------------------------------.
| yysetstate -- set current state (the top of the stack) to yystate.  |
`--------------------------------------------------------------------*/
yysetstate:
  YYDPRINTF ((stderr, "Entering state %d\n", yystate));
  YY_ASSERT (0 <= yystate && yystate < YYNSTATES);
  YY_IGNORE_USELESS_CAST_BEGIN
  *yyssp = YY_CAST (yy_state_t, yystate);
  YY_IGNORE_USELESS_CAST_END
  YY_STACK_PRINT (yyss, yyssp);

  if (yyss + yystacksize - 1 <= yyssp)
#if !defined yyoverflow && !defined YYSTACK_RELOCATE
    YYNOMEM;
#else
    {
      /* Get the current used size of the three stacks, in elements.  */
      YYPTRDIFF_T yysize = yyssp - yyss + 1;

# if defined yyoverflow
      {
        /* Give user a chance to reallocate the stack.  Use copies of
           these so that the &'s don't force the real ones into
           memory.  */
        yy_state_t *yyss1 = yyss;
        YYSTYPE *yyvs1 = yyvs;

        /* Each stack pointer address is followed by the size of the
           data in use in that stack, in bytes.  This used to be a
           conditional around just the two extra args, but that might
           be undefined if yyoverflow is a macro.  */
        yyoverflow (YY_("memory exhausted"),
                    &yyss1, yysize * YYSIZEOF (*yyssp),
                    &yyvs1, yysize * YYSIZEOF (*yyvsp),
                    &yystacksize);
        yyss = yyss1;
        yyvs = yyvs1;
      }
# else /* defined YYSTACK_RELOCATE */
      /* Extend the stack our own way.  */
      if (YYMAXDEPTH <= yystacksize)
        YYNOMEM;
      yystacksize *= 2;
      if (YYMAXDEPTH < yystacksize)
        yystacksize = YYMAXDEPTH;

      {
        yy_state_t *yyss1 = yyss;
        union yyalloc *yyptr =
          YY_CAST (union yyalloc *,
                   YYSTACK_ALLOC (YY_CAST (YYSIZE_T, YYSTACK_BYTES (yystacksize))));
        if (! yyptr)
          YYNOMEM;
        YYSTACK_RELOCATE (yyss_alloc, yyss);
        YYSTACK_RELOCATE (yyvs_alloc, yyvs);
#  undef YYSTACK_RELOCATE
        if (yyss1 != yyssa)
          YYSTACK_FREE (yyss1);
      }
# endif

      yyssp = yyss + yysize - 1;
      yyvsp = yyvs + yysize - 1;

      YY_IGNORE_USELESS_CAST_BEGIN
      YYDPRINTF ((stderr, "Stack size increased to %ld\n",
                  YY_CAST (long, yystacksize)));
      YY_IGNORE_USELESS_CAST_END

      if (yyss + yystacksize - 1 <= yyssp)
        YYABORT;
    }
#endif /* !defined yyoverflow && !defined YYSTACK_RELOCATE */


  if (yystate == YYFINAL)
    YYACCEPT;

  goto yybackup;


/*-----------.
| yybackup.  |
`-----------*/
yybackup:
  /* Do appropriate processing given the current state.  Read a
     lookahead token if we need one and don't already have one.  */

  /* First try to decide what to do without reference to lookahead token.  */
  yyn = yypact[yystate];
  if (yypact_value_is_default (yyn))
    goto yydefault;

  /* Not known => get a lookahead token if don't already have one.  */

  /* YYCHAR is either empty, or end-of-input, or a valid lookahead.  */
  if (yychar == YYEMPTY)
    {
      YYDPRINTF ((stderr, "Reading a token\n"));
      yychar = yylex ();
    }

  if (yychar <= YYEOF)
    {
      yychar = YYEOF;
      yytoken = YYSYMBOL_YYEOF;
      YYDPRINTF ((stderr, "Now at end of input.\n"));
    }
  else if (yychar == YYerror)
    {
      /* The scanner already issued an error message, process directly
         to error recovery.  But do not keep the error token as
         lookahead, it is too special and may lead us to an endless
         loop in error recovery. */
      yychar = YYUNDEF;
      yytoken = YYSYMBOL_YYerror;
      goto yyerrlab1;
    }
  else
    {
      yytoken = YYTRANSLATE (yychar);
      YY_SYMBOL_PRINT ("Next token is", yytoken, &yylval, &yylloc);
    }

  /* If the proper action on seeing token YYTOKEN is to reduce or to
     detect an error, take that action.  */
  yyn += yytoken;
  if (yyn < 0 || YYLAST < yyn || yycheck[yyn] != yytoken)
    goto yydefault;
  yyn = yytable[yyn];
  if (yyn <= 0)
    {
      if (yytable_value_is_error (yyn))
        goto yyerrlab;
      yyn = -yyn;
      goto yyreduce;
    }

  /* Count tokens shifted since error; after three, turn off error
     status.  */
  if (yyerrstatus)
    yyerrstatus--;

  /* Shift the lookahead token.  */
  YY_SYMBOL_PRINT ("Shifting", yytoken, &yylval, &yylloc);
  yystate = yyn;
  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END

  /* Discard the shifted token.  */
  yychar = YYEMPTY;
  goto yynewstate;


/*-----------------------------------------------------------.
| yydefault -- do the default action for the current state.  |
`-----------------------------------------------------------*/
yydefault:
  yyn = yydefact[yystate];
  if (yyn == 0)
    goto yyerrlab;
  goto yyreduce;


/*-----------------------------.
| yyreduce -- do a reduction.  |
`-----------------------------*/
yyreduce:
  /* yyn is the number of a rule to reduce with.  */
  yylen = yyr2[yyn];

  /* If YYLEN is nonzero, implement the default value of the action:
     '$$ = $1'.

     Otherwise, the following line sets YYVAL to garbage.
     This behavior is undocumented and Bison
     users should not rely upon it.  Assigning to YYVAL
     unconditionally makes the parser a bit smaller, and it avoids a
     GCC warning that YYVAL may be used uninitialized.  */
  yyval = yyvsp[1-yylen];


  YY_REDUCE_PRINT (yyn);
  switch (yyn)
    {
  case 14: /* block: blockbegin '{' assignments '}' ';'  */
        {
	    validblock = (yyvsp[-4].number);
	}
    break;

  case 17: /* blockbegin: CRC '(' CHAR4CONSTANT ',' INTEGERCONSTANT ')' ':' HEXCONSTANT  */
                {
		    (yyval.number) = validblock;
		    crcval = getthecrc((yyvsp[-5].rtype), (yyvsp[-3].number));
		    validblock = (yyvsp[0].number) == crcval;
		}
    break;

  case 18: /* blockbegin: APP_VERSION '(' HEXCONSTANT ')' ':' HEXCONSTANT  */
                {
		    (yyval.number) = validblock;
		    validblock = (ROMlib_version_long & (yyvsp[-3].number)) == (yyvsp[0].number);
		}
    break;

  case 19: /* stringassignment: stringvar '=' STRINGCONSTANT ';'  */
                        {
                if(validblock && (yyvsp[-3].strptr))
                    *(yyvsp[-3].strptr) = (yyvsp[-1].cptr);
				freestring((yyvsp[-1].cptr));
			}
    break;

  case 20: /* sysversassignment: SYSTEMVERSION '=' number ';'  */
                        { 
			  if (validblock)
			    ROMlib_set_system_version
			      (CREATE_SYSTEM_VERSION ((yyvsp[-1].number), 0, 0));
			}
    break;

  case 21: /* sysversassignment: SYSTEMVERSION '=' number '.' number ';'  */
                        {
			  if (validblock) 
			    ROMlib_set_system_version
			      (CREATE_SYSTEM_VERSION ((yyvsp[-3].number), (yyvsp[-1].number), 0));
			}
    break;

  case 22: /* sysversassignment: SYSTEMVERSION '=' number '.' number '.' number ';'  */
                        {
			  if (validblock)
			    ROMlib_set_system_version
			      (CREATE_SYSTEM_VERSION ((yyvsp[-5].number), (yyvsp[-3].number), (yyvsp[-1].number)));
			}
    break;

  case 23: /* prversassignment: PRVERSION '=' number ';'  */
                        { 
			  if (validblock)
			    ROMlib_PrDrvrVers = (yyvsp[-1].number) * 10;
			}
    break;

  case 24: /* prversassignment: PRVERSION '=' number '.' number ';'  */
                        {
			  if (validblock)
			    ROMlib_PrDrvrVers = (yyvsp[-3].number) * 10 + (yyvsp[-1].number);
			}
    break;

  case 25: /* numerassignment: numervar '=' number ';'  */
                        {
			    if (validblock && (yyvsp[-3].iptr))
				    *(yyvsp[-3].iptr) = (yyvsp[-1].number);
			}
    break;

  case 26: /* stringvar: WINDOWNAME  */
        { (yyval.strptr) = &ROMlib_WindowName; }
    break;

  case 27: /* stringvar: COMMENTS  */
        { (yyval.strptr) = &ROMlib_Comments; }
    break;

  case 28: /* stringvar: DONGLEFAMILY  */
        { (yyval.strptr) = nullptr; }
    break;

  case 29: /* stringvar: PRINTERNAME  */
        { (yyval.strptr) = nullptr; }
    break;

  case 30: /* stringvar: PRINTERTYPELABEL  */
        { (yyval.strptr) = nullptr; }
    break;

  case 31: /* stringvar: WIN32TOKEN  */
        { (yyval.strptr) = nullptr; }
    break;

  case 32: /* stringvar: MACCDROM  */
        { (yyval.strptr) = nullptr;
          warning_unexpected ("MacCDROM no longer supported "
			      "in configuration files"); }
    break;

  case 33: /* numervar: RDELAY  */
        { (yyval.iptr) = &ROMlib_delay; }
    break;

  case 34: /* numervar: REFRESHNUMBER  */
        { (yyval.iptr) = &ROMlib_refresh; }
    break;

  case 35: /* numervar: BITSNUMBER  */
        { (yyval.iptr) = &ROMlib_desired_bpp; }
    break;

  case 36: /* numervar: APPLECHAR  */
        { (yyval.iptr) = &ROMlib_AppleChar; }
    break;

  case 37: /* numervar: RIGHT_BUTTON_MODIFIER  */
        { (yyval.iptr) = nullptr; }
    break;

  case 38: /* optionvalues: optionvalues ',' OPTIONSCONSTANT  */
        { (yyval.number) = (yyvsp[-2].number) | (yyvsp[0].number); }
    break;

  case 39: /* optionvalues: OPTIONSCONSTANT  */
        { (yyval.number) = (yyvsp[0].number); }
    break;

  case 40: /* optionsvar: OPTIONS  */
        { 
	  if (validblock)
	    {
	      (yyval.iptr) = &ROMlib_options;
	      ROMlib_options = 0;
	    }
	}
    break;

  case 41: /* optionsassignment: optionsvar '=' '{' optionvalues '}' ';'  */
        { if (validblock)
	      *(yyvsp[-5].iptr) = (yyvsp[-2].number);
	}
    break;

  case 42: /* optionsassignment: optionsvar '=' '{' '}' ';'  */
        {
	  if (validblock)
	    *(yyvsp[-4].iptr) = 0;
	}
    break;

  case 43: /* optionsassignment: optionsvar '=' '{' optionvalues ',' '}' ';'  */
        {
	  if (validblock)
	    *(yyvsp[-6].iptr) = (yyvsp[-3].number);
	}
    break;

  case 47: /* pairassignment: pairvar '=' '{' number ',' number '}' ';'  */
        {
	  /*if (validblock)
	    {
	      $1->first = $4;
	      $1->second = $6;
	    }*/
	}
    break;

  case 48: /* gestaltassignment: gestalt_entry '=' '{' number ',' number '}' ';'  */
        { if (validblock)
	    ROMlib_add_to_gestalt_list ((yyvsp[-7].number), (yyvsp[-4].number), (yyvsp[-2].number));
	}
    break;

  case 49: /* number: HEXCONSTANT  */
        { (yyval.number) = (yyvsp[0].number); }
    break;

  case 50: /* number: INTEGERCONSTANT  */
        { (yyval.number) = (yyvsp[0].number); }
    break;

  case 51: /* number: '-' number  */
        { (yyval.number) = - (yyvsp[0].number); }
    break;

  case 52: /* gestalt_entry: GESTALT '(' CHAR4CONSTANT ')'  */
        { (yyval.number) = (yyvsp[-1].rtype); }
    break;

  case 53: /* gestalt_entry: GESTALT '(' HEXCONSTANT ')'  */
        { (yyval.number) = (yyvsp[-1].number); }
    break;

  case 54: /* pairvar: SCREENSIZE  */
        { (yyval.pptr) = nullptr; /*&ROMlib_ScreenSize;*/ }
    break;

  case 55: /* pairvar: SCREENLOCATION  */
        { (yyval.pptr) = nullptr; /*&ROMlib_ScreenLocation;*/ }
    break;

  case 56: /* pairvar: MACSIZE  */
        { (yyval.pptr) = nullptr; /*&ROMlib_MacSize;*/ }
    break;

  case 61: /* numberlist: numberlist ',' number  */
        { if (validblock)
	    /* addtocache($3); */{};
        }
    break;

  case 62: /* numberlist: number  */
        { if (validblock)
	    /* addtocache($1); */{};
	}
    break;



      default: break;
    }
  /* User semantic actions sometimes alter yychar, and that requires
     that yytoken be updated with the new translation.  We take the
     approach of translating immediately before every use of yytoken.
     One alternative is translating here after every semantic action,
     but that translation would be missed if the semantic action invokes
     YYABORT, YYACCEPT, or YYERROR immediately after altering yychar or
     if it invokes YYBACKUP.  In the case of YYABORT or YYACCEPT, an
     incorrect destructor might then be invoked immediately.  In the
     case of YYERROR or YYBACKUP, subsequent parser actions might lead
     to an incorrect destructor call or verbose syntax error message
     before the lookahead is translated.  */
  YY_SYMBOL_PRINT ("-> $$ =", YY_CAST (yysymbol_kind_t, yyr1[yyn]), &yyval, &yyloc);

  YYPOPSTACK (yylen);
  yylen = 0;

  *++yyvsp = yyval;

  /* Now 'shift' the result of the reduction.  Determine what state
     that goes to, based on the state we popped back to and the rule
     number reduced by.  */
  {
    const int yylhs = yyr1[yyn] - YYNTOKENS;
    const int yyi = yypgoto[yylhs] + *yyssp;
    yystate = (0 <= yyi && yyi <= YYLAST && yycheck[yyi] == *yyssp
               ? yytable[yyi]
               : yydefgoto[yylhs]);
  }

  goto yynewstate;


/*--------------------------------------.
| yyerrlab -- here on detecting error.  |
`--------------------------------------*/
yyerrlab:
  /* Make sure we have latest lookahead translation.  See comments at
     user semantic actions for why this is necessary.  */
  yytoken = yychar == YYEMPTY ? YYSYMBOL_YYEMPTY : YYTRANSLATE (yychar);
  /* If not already recovering from an error, report this error.  */
  if (!yyerrstatus)
    {
      ++yynerrs;
      yyerror (YY_("syntax error"));
    }

  if (yyerrstatus == 3)
    {
      /* If just tried and failed to reuse lookahead token after an
         error, discard it.  */

      if (yychar <= YYEOF)
        {
          /* Return failure if at end of input.  */
          if (yychar == YYEOF)
            YYABORT;
        }
      else
        {
          yydestruct ("Error: discarding",
                      yytoken, &yylval);
          yychar = YYEMPTY;
        }
    }

  /* Else will try to reuse lookahead token after shifting the error
     token.  */
  goto yyerrlab1;


/*---------------------------------------------------.
| yyerrorlab -- error raised explicitly by YYERROR.  |
`---------------------------------------------------*/
yyerrorlab:
  /* Pacify compilers when the user code never invokes YYERROR and the
     label yyerrorlab therefore never appears in user code.  */
  if (0)
    YYERROR;
  ++yynerrs;

  /* Do not reclaim the symbols of the rule whose action triggered
     this YYERROR.  */
  YYPOPSTACK (yylen);
  yylen = 0;
  YY_STACK_PRINT (yyss, yyssp);
  yystate = *yyssp;
  goto yyerrlab1;


/*-------------------------------------------------------------.
| yyerrlab1 -- common code for both syntax error and YYERROR.  |
`-------------------------------------------------------------*/
yyerrlab1:
  yyerrstatus = 3;      /* Each real token shifted decrements this.  */

  /* Pop stack until we find a state that shifts the error token.  */
  for (;;)
    {
      yyn = yypact[yystate];
      if (!yypact_value_is_default (yyn))
        {
          yyn += YYSYMBOL_YYerror;
          if (0 <= yyn && yyn <= YYLAST && yycheck[yyn] == YYSYMBOL_YYerror)
            {
              yyn = yytable[yyn];
              if (0 < yyn)
                break;
            }
        }

      /* Pop the current state because it cannot handle the error token.  */
      if (yyssp == yyss)
        YYABORT;


      yydestruct ("Error: popping",
                  YY_ACCESSING_SYMBOL (yystate), yyvsp);
      YYPOPSTACK (1);
      yystate = *yyssp;
      YY_STACK_PRINT (yyss, yyssp);
    }

  YY_IGNORE_MAYBE_UNINITIALIZED_BEGIN
  *++yyvsp = yylval;
  YY_IGNORE_MAYBE_UNINITIALIZED_END


  /* Shift the error token.  */
  YY_SYMBOL_PRINT ("Shifting", YY_ACCESSING_SYMBOL (yyn), yyvsp, yylsp);

  yystate = yyn;
  goto yynewstate;


/*-------------------------------------.
| yyacceptlab -- YYACCEPT comes here.  |
`-------------------------------------*/
yyacceptlab:
  yyresult = 0;
  goto yyreturnlab;


/*-----------------------------------.
| yyabortlab -- YYABORT comes here.  |
`-----------------------------------*/
yyabortlab:
  yyresult = 1;
  goto yyreturnlab;


/*-----------------------------------------------------------.
| yyexhaustedlab -- YYNOMEM (memory exhaustion) comes here.  |
`-----------------------------------------------------------*/
yyexhaustedlab:
  yyerror (YY_("memory exhausted"));
  yyresult = 2;
  goto yyreturnlab;


/*----------------------------------------------------------.
| yyreturnlab -- parsing is finished, clean up and return.  |
`----------------------------------------------------------*/
yyreturnlab:
  if (yychar != YYEMPTY)
    {
      /* Make sure we have latest lookahead translation.  See comments at
         user semantic actions for why this is necessary.  */
      yytoken = YYTRANSLATE (yychar);
      yydestruct ("Cleanup: discarding lookahead",
                  yytoken, &yylval);
    }
  /* Do not reclaim the symbols of the rule whose action triggered
     this YYABORT or YYACCEPT.  */
  YYPOPSTACK (yylen);
  YY_STACK_PRINT (yyss, yyssp);
  while (yyssp != yyss)
    {
      yydestruct ("Cleanup: popping",
                  YY_ACCESSING_SYMBOL (+*yyssp), yyvsp);
      YYPOPSTACK (1);
    }
#ifndef yyoverflow
  if (yyss != yyssa)
    YYSTACK_FREE (yyss);
#endif

  return yyresult;
}


#include	<stdio.h>
#include	<ctype.h>

struct namevaluestr {
    const char *name;
    long value;
    long lval;
} reserved[] = 
{	/* NOTE: these are sorted so we can use a binary search */
  { "Accelerated",	OPTIONSCONSTANT,	0 },
  { "AppleChar",	APPLECHAR,              0 },
  { "BitsPerPixel",	BITSNUMBER,		0 },
  { "BlitAtTrapEnd",	OPTIONSCONSTANT,	0 },
  { "BlitInOSEvent",	OPTIONSCONSTANT,	0 },
  { "BlitOften",	OPTIONSCONSTANT,	0 },
  { "CRC",		CRC,			0 },
  { "CacheProblems",	CACHEPROBLEMS,		0 },
  { "CloseIsQuit",	OPTIONSCONSTANT,	0 },
  { "Comments",		COMMENTS,		0 },
  { "Debug",		OPTIONSCONSTANT,	0 },
  { "Delay",		RDELAY,			0 },
  { "DirectDiskAccess",	OPTIONSCONSTANT,	ROMLIB_DIRECTDISKACCESS_BIT },
  { "DirtyVariant",	OPTIONSCONSTANT,	0 },
  { "DisposHandleHack", OPTIONSCONSTANT,        0 },
  { "DongleFamily",	DONGLEFAMILY,		0 },
  { "FlushOften",	OPTIONSCONSTANT,	ROMLIB_FLUSHOFTEN_BIT },
  { "Gestalt",          GESTALT,                0 },
  { "MacCDROM",         MACCDROM,               0 },
  { "MacSize",		MACSIZE,		0 },
  { "NewLineToCR",	OPTIONSCONSTANT,	ROMLIB_NEWLINETOCR_BIT },
  { "NoClock",		OPTIONSCONSTANT,	ROMLIB_NOCLOCK_BIT },
  { "NoLower",		OPTIONSCONSTANT,	0 },
  { "NoPrefPanel",	OPTIONSCONSTANT,	0 },
  { "NoSuspend",	OPTIONSCONSTANT,	0 },
  { "NoWarn32",		OPTIONSCONSTANT,	ROMLIB_NOWARN32_BIT },
  { "Options",		OPTIONS,		0 },
  { "PassPostscript",	OPTIONSCONSTANT,	0 },
  { "PretendAlias",	OPTIONSCONSTANT,	ROMLIB_PRETEND_ALIAS_BIT },
  { "PretendEdition",	OPTIONSCONSTANT,	ROMLIB_PRETEND_EDITION_BIT },
  { "PretendHelp",	OPTIONSCONSTANT,	ROMLIB_PRETEND_HELP_BIT },
  { "PretendScript",	OPTIONSCONSTANT,	ROMLIB_PRETEND_SCRIPT_BIT },
  { "PretendSound",	OPTIONSCONSTANT,	ROMLIB_PRETENDSOUND_BIT },
  { "PrinterName",      PRINTERNAME,            0, },
  { "PrinterTypeLabel", PRINTERTYPELABEL,       0, },
  { "PrintingHack",	OPTIONSCONSTANT,	0 },
  { "PrVers",		PRVERSION,		0, },
  { "RectScreen",	OPTIONSCONSTANT,	0 },
  { "Refresh",		OPTIONSCONSTANT,	ROMLIB_REFRESH_BIT },
  { "RefreshNumber",	REFRESHNUMBER,		0 },
  { "RightButtonModifier", RIGHT_BUTTON_MODIFIER, 0 },
  { "ScreenLocation",	SCREENLOCATION,		0 },
  { "ScreenSize",	SCREENSIZE,		0 },
  { "SoundOff",		OPTIONSCONSTANT,	ROMLIB_SOUNDOFF_BIT },
  { "SoundOn",		OPTIONSCONSTANT,	ROMLIB_SOUNDON_BIT },
  { "StripAddress",	OPTIONSCONSTANT,	0 },
  { "SystemVersion",	SYSTEMVERSION,          0 },
  { "TextDisableHack",	OPTIONSCONSTANT,	ROMLIB_TEXT_DISABLE_BIT },
  { "Version",		APP_VERSION,		0 },
  { "Win32Token",       WIN32TOKEN,             0 },
  { "WindowName",	WINDOWNAME,		0 },
};

#define	RELOAD	(-2)
#define	STRINGSPACE	1024

long binfind(char *tofind, struct namevaluestr table[], short nentries)
{
  short low, mid, high;
  int cmpret;

    for (low = -1, high = nentries; high - low > 1;) {
	mid = (high + low) / 2;
	cmpret = strcmp(tofind, table[mid].name);
	if (cmpret < 0)
	    high = mid;
	else if (cmpret == 0) {
	    yylval.number = table[mid].lval;
/*-->*/	    return table[mid].value;
	} else /* cmpret > 0 */
	    low = mid;
    }
    return 0;
}

static char charspace[STRINGSPACE];
static char *freechar = charspace, *endchar = charspace + STRINGSPACE;

/*
 * NOTE: call freestring only if you know no strings have been assigned
 *       after the one you are freeing.
 */

static void freestring( char *string )
{
    freechar = string;
}

static long linecount = 1;

static int
cmp (const void *p1, const void *p2)
{
  int retval;
  const char *str1;
  const char *str2;

  str1 = ((struct namevaluestr *)p1)->name;
  str2 = ((struct namevaluestr *)p2)->name;
  retval = strcmp (str1, str2);
  return retval;
}

static long yylex( void ) {
    static short c = RELOAD;
    char *tempp;
    long retval;
    int i;

    if (c == RELOAD)
	c = getc(configfile);

    for (;;) {
	switch (c) {

/* NOTE: if you mess with the '"' code, make sure to adjust clean()
   in toolevent.c.  */

	case '"':
	    yylval.cptr = freechar;
	    while (freechar < endchar && ((c = getc(configfile)) != '"'))
		*freechar++ = c;
	    if (freechar == endchar)
		warning_unexpected ("out of string space parsing configuration file (line %ld)", linecount);
	    else
		*freechar++ = 0;
	    c = getc(configfile);
	    return STRINGCONSTANT;
	    break;

	case '0':
	    if ((c = getc(configfile)) == 'x') {
		yylval.number = 0;
		while (isalnum(c = getc(configfile))) {
		    if (yylval.number & 0xF0000000)
			warning_unexpected ("hex constant too long parsing configuration file (line %ld)", linecount);
		    if (c >= '0' && c <= '9')
			yylval.number = (yylval.number << 4) + c - '0';
		    else if (c >= 'a' && c <= 'f')
			yylval.number = (yylval.number << 4) + c - 'a' + 10;
		    else if (c >= 'A' && c <= 'F')
			yylval.number = (yylval.number << 4) + c - 'A' + 10;
		    else
			warning_unexpected ("bad character in hex constant parsing configuration file (line %ld)", linecount);
		}
		return HEXCONSTANT;
		break;
	    }
	    /* We just ate a 0 that should be part of a decimal integer
	       (we don't support octal).  It is a leading zero, so we can
	       forget about it and fall through */
	    ungetc(c, configfile);
	    c = '0';
	    /* FALL THROUGH */
	case '1': case '2': case '3':
	case '4': case '5': case '6':
	case '7': case '8': case '9':
	    yylval.number = c - '0';
	    while (isdigit(c = getc(configfile))) {
		yylval.number = yylval.number * 10 + c - '0';
		if (yylval.number < 0)
		    warning_unexpected ("integer overflow parsing configuration file (line %ld)", linecount);
	    }
	    return INTEGERCONSTANT;
	    break;
	case ' ':
	case '\r':
	case '\n':
	case '\t':
	case '\f':
	    do
		if (c == '\n')
		    linecount++;
	    while (isspace(c = getc(configfile)));
	    break;
	case '/':
	    if ((c = getc(configfile)) == '/') {
		while ((c = getc(configfile)) != '\n')
		    ;
		linecount++;
		c = getc(configfile);
	    } else
		warning_unexpected ("single '/' parsing configuration file (line %ld)", linecount);
	    break;
	case '\'':
	    yylval.rtype = 0;
	    for (i = 0 ; i < 4 ; i++) {
		c = getc(configfile);
		yylval.rtype = (unsigned long)yylval.rtype * 256 + c;
	    }
	    if ((c = getc(configfile)) != '\'')
		warning_unexpected ("missing \' parsing configuration file (line %ld)", linecount);
	    c = getc(configfile);
	    return CHAR4CONSTANT;
	    break;
	case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
	case 'g': case 'h': case 'i': case 'j': case 'k': case 'l':
	case 'm': case 'n': case 'o': case 'p': case 'q': case 'r':
	case 's': case 't': case 'u': case 'v': case 'w': case 'x':
	case 'y': case 'z':
	case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
	case 'G': case 'H': case 'I': case 'J': case 'K': case 'L':
	case 'M': case 'N': case 'O': case 'P': case 'Q': case 'R':
	case 'S': case 'T': case 'U': case 'V': case 'W': case 'X':
	case 'Y': case 'Z':
	    tempp = freechar;
	    if (tempp < endchar)
		do
		    *tempp++ = c;
		while (tempp < endchar && isalnum(c = getc(configfile)));
	    if (tempp == endchar)
		warning_unexpected ("out of string space while looking up keyword parsing configuration file (line %ld)", linecount);
	    else
		*tempp++ = 0;
	    {
	      static bool sorted = 0;

	      if (!sorted)
		{
		  qsort (reserved, std::size (reserved), sizeof reserved[0], cmp);
		  sorted = 1;
		}
	    }
	    retval = binfind(freechar, reserved, std::size (reserved));
	    if (!retval)
		warning_unexpected ("unknown reserved word parsing configuration file (line %ld)", linecount);
	    return retval;
	    break;
	case '{':
	case '}':
	case '(':
	case ')':
	case '=':
	case ':':
	case ',':
	case ';':
	case '-':
	case '.':
	    retval = c;
	    c = getc(configfile);
	    return retval;
	    break;
	case EOF:
	    c = RELOAD;	/* for the next time around */
	    linecount = 1;
	    validblock = 1;
	    return EOF;
	    break;
	default:
	    warning_unexpected ("unknown character parsing configuration file (line %ld)", linecount);
	}
    }
}

void yyerror(const char *str)
{
    warning_unexpected ("configuration file parse error (line %ld): %s", linecount, str);
}
