/*****************************************************************************
 *                                                                           *
 *                                 StdCLib.h                                 *
 *                                 =========                                 *
 *                                                                           *
 * Needs:                                                                    *
 *     MacTypes.h    OSErr, Boolean                                          *
 *     FileMgr.h     FSSpec                                                  *
 *                                                                           *
 *                                                                           *
 *****************************************************************************/


#pragma once
#include "base/mactype.h"
#include <cassert>
#include <iosfwd>
#include <base/lowglobals.h>
#include "ExMacTypes.h"
#include "FileMgr.h"

#define MODULE_NAME StdCLib
#include <base/api-module.h>


namespace Executor {

void *data_errno(); DATA_EXPORT(StdCLib, "StdCLib", errno, data_errno);


void *data_MacOSErr(); DATA_EXPORT(StdCLib, "StdCLib", MacOSErr, data_MacOSErr);


void *data_p_CType(); DATA_EXPORT(StdCLib, "StdCLib", __p_CType, data_p_CType);


void *data_IntEnv(); DATA_EXPORT(StdCLib, "StdCLib", _IntEnv, data_IntEnv);


void *data_C_phase(); DATA_EXPORT(StdCLib, "StdCLib", __C_phase, data_C_phase);


void *data_target_for_exit(); DATA_EXPORT(StdCLib, "StdCLib", __target_for_exit, data_target_for_exit);


void *data_exit_status(); DATA_EXPORT(StdCLib, "StdCLib", _exit_status, data_exit_status);


void *data_NubAt3(); DATA_EXPORT(StdCLib, "StdCLib", __NubAt3, data_NubAt3);


void *data_StandAlone(); DATA_EXPORT(StdCLib, "StdCLib", StandAlone, data_StandAlone);


void *data_NoMoreDebugStr(); DATA_EXPORT(StdCLib, "StdCLib", NoMoreDebugStr, data_NoMoreDebugStr);


void *data_DBL_EPSILON(); DATA_EXPORT(StdCLib, "StdCLib", _DBL_EPSILON, data_DBL_EPSILON);


void *data_DBL_MAX(); DATA_EXPORT(StdCLib, "StdCLib", _DBL_MAX, data_DBL_MAX);


void *data_DBL_MIN(); DATA_EXPORT(StdCLib, "StdCLib", _DBL_MIN, data_DBL_MIN);


void *data_FLT_EPSILON(); DATA_EXPORT(StdCLib, "StdCLib", _FLT_EPSILON, data_FLT_EPSILON);


void *data_FLT_MAX(); DATA_EXPORT(StdCLib, "StdCLib", _FLT_MAX, data_FLT_MAX);


void *data_FLT_MIN(); DATA_EXPORT(StdCLib, "StdCLib", _FLT_MIN, data_FLT_MIN);


void *data_LDBL_EPSILON(); DATA_EXPORT(StdCLib, "StdCLib", _LDBL_EPSILON, data_LDBL_EPSILON);


void *data_LDBL_MAX(); DATA_EXPORT(StdCLib, "StdCLib", _LDBL_MAX, data_LDBL_MAX);


void *data_LDBL_MIN(); DATA_EXPORT(StdCLib, "StdCLib", _LDBL_MIN, data_LDBL_MIN);


void *data_StdDevs(); DATA_EXPORT(StdCLib, "StdCLib", _StdDevs, data_StdDevs);


void *data_SigEnv(); DATA_EXPORT(StdCLib, "StdCLib", __SigEnv, data_SigEnv);


void *data_loc(); DATA_EXPORT(StdCLib, "StdCLib", __loc, data_loc);


void *data_yd(); DATA_EXPORT(StdCLib, "StdCLib", __yd, data_yd);


void *data_CategoryLoc(); DATA_EXPORT(StdCLib, "StdCLib", _CategoryLoc, data_CategoryLoc);


void *data_PublicTimeInfo(); DATA_EXPORT(StdCLib, "StdCLib", _PublicTimeInfo, data_PublicTimeInfo);


void *data_MoneyData(); DATA_EXPORT(StdCLib, "StdCLib", MoneyData, data_MoneyData);


void *data_NumericData(); DATA_EXPORT(StdCLib, "StdCLib", NumericData, data_NumericData);


void *data_iob(); DATA_EXPORT(StdCLib, "StdCLib", _iob, data_iob);


void *data_lastbuf(); DATA_EXPORT(StdCLib, "StdCLib", _lastbuf, data_lastbuf);


char* c_memcpy(char *d, const char *s, uint32_t n);LIBRARY_FUNCTION(StdCLib, memcpy, c_memcpy);


char* c_memcpy(char *d, const char *s, uint32_t n);LIBRARY_FUNCTION(StdCLib, _memcpy, c_memcpy);


char* c_memmove(char *d, const char *s, uint32_t n);LIBRARY_FUNCTION(StdCLib, memmove, c_memmove);


char* c_memmove(char *d, const char *s, uint32_t n);LIBRARY_FUNCTION(StdCLib, _rmemcpy, c_memmove);


char* c_memset(char *d, int32_t c, uint32_t n);LIBRARY_FUNCTION(StdCLib, memset, c_memset);


int32_t c_memcmp(const uint8_t *a, const uint8_t *b, uint32_t n);LIBRARY_FUNCTION(StdCLib, memcmp, c_memcmp);


char* c_memchr(char *s, int32_t c, uint32_t n);LIBRARY_FUNCTION(StdCLib, memchr, c_memchr);


char* c_memchr(char *s, int32_t c, uint32_t n);LIBRARY_FUNCTION(StdCLib, _memchr, c_memchr);


char* c_memccpy(char *d, const char *s, int32_t c, uint32_t n);LIBRARY_FUNCTION(StdCLib, memccpy, c_memccpy);


uint32_t c_strlen(const char *s);LIBRARY_FUNCTION(StdCLib, strlen, c_strlen);


char* c_strcpy(char *d, const char *s);LIBRARY_FUNCTION(StdCLib, strcpy, c_strcpy);


char* c_strncpy(char *d, const char *s, uint32_t n);LIBRARY_FUNCTION(StdCLib, strncpy, c_strncpy);


char* c_strcat(char *d, const char *s);LIBRARY_FUNCTION(StdCLib, strcat, c_strcat);


char* c_strncat(char *d, const char *s, uint32_t n);LIBRARY_FUNCTION(StdCLib, strncat, c_strncat);


int32_t c_strcmp(const uint8_t *a, const uint8_t *b);LIBRARY_FUNCTION(StdCLib, strcmp, c_strcmp);


int32_t c_strncmp(const uint8_t *a, const uint8_t *b, uint32_t n);LIBRARY_FUNCTION(StdCLib, strncmp, c_strncmp);


char* c_strchr(char *s, int32_t c);LIBRARY_FUNCTION(StdCLib, strchr, c_strchr);


char* c_strrchr(char *s, int32_t c);LIBRARY_FUNCTION(StdCLib, strrchr, c_strrchr);


uint32_t c_strspn(const char *s, const char *set);LIBRARY_FUNCTION(StdCLib, strspn, c_strspn);


uint32_t c_strcspn(const char *s, const char *set);LIBRARY_FUNCTION(StdCLib, strcspn, c_strcspn);


char* c_strpbrk(char *s, const char *set);LIBRARY_FUNCTION(StdCLib, strpbrk, c_strpbrk);


char* c_strstr(char *s, const char *t);LIBRARY_FUNCTION(StdCLib, strstr, c_strstr);


char* c_strtok(char *s, const char *sep);LIBRARY_FUNCTION(StdCLib, strtok, c_strtok);


int32_t c_strcoll(const uint8_t *a, const uint8_t *b);LIBRARY_FUNCTION(StdCLib, strcoll, c_strcoll);


uint32_t c_strxfrm(char *d, const char *s, uint32_t n);LIBRARY_FUNCTION(StdCLib, strxfrm, c_strxfrm);


char* c_strerror(int32_t e);LIBRARY_FUNCTION(StdCLib, strerror, c_strerror);


int32_t c_isalnum(int32_t c);LIBRARY_FUNCTION(StdCLib, isalnum, c_isalnum);


int32_t c_isalpha(int32_t c);LIBRARY_FUNCTION(StdCLib, isalpha, c_isalpha);


int32_t c_iscntrl(int32_t c);LIBRARY_FUNCTION(StdCLib, iscntrl, c_iscntrl);


int32_t c_isdigit(int32_t c);LIBRARY_FUNCTION(StdCLib, isdigit, c_isdigit);


int32_t c_isgraph(int32_t c);LIBRARY_FUNCTION(StdCLib, isgraph, c_isgraph);


int32_t c_islower(int32_t c);LIBRARY_FUNCTION(StdCLib, islower, c_islower);


int32_t c_isprint(int32_t c);LIBRARY_FUNCTION(StdCLib, isprint, c_isprint);


int32_t c_ispunct(int32_t c);LIBRARY_FUNCTION(StdCLib, ispunct, c_ispunct);


int32_t c_isspace(int32_t c);LIBRARY_FUNCTION(StdCLib, isspace, c_isspace);


int32_t c_isupper(int32_t c);LIBRARY_FUNCTION(StdCLib, isupper, c_isupper);


int32_t c_isxdigit(int32_t c);LIBRARY_FUNCTION(StdCLib, isxdigit, c_isxdigit);


int32_t c_isascii(int32_t c);LIBRARY_FUNCTION(StdCLib, isascii, c_isascii);


int32_t c_toascii(int32_t c);LIBRARY_FUNCTION(StdCLib, toascii, c_toascii);


int32_t c_tolower(int32_t c);LIBRARY_FUNCTION(StdCLib, tolower, c_tolower);


int32_t c_toupper(int32_t c);LIBRARY_FUNCTION(StdCLib, toupper, c_toupper);


int32_t c_atoi(const char *s);LIBRARY_FUNCTION(StdCLib, atoi, c_atoi);


int32_t c_atol(const char *s);LIBRARY_FUNCTION(StdCLib, atol, c_atol);


double c_atof(const char *s);LIBRARY_FUNCTION(StdCLib, atof, c_atof);


int32_t c_strtol(const char *s, uint32_t endp, int32_t base);LIBRARY_FUNCTION(StdCLib, strtol, c_strtol);


uint32_t c_strtoul(const char *s, uint32_t endp, int32_t base);LIBRARY_FUNCTION(StdCLib, strtoul, c_strtoul);


double c_strtod(const char *s, uint32_t endp);LIBRARY_FUNCTION(StdCLib, strtod, c_strtod);


int32_t c_abs(int32_t x);LIBRARY_FUNCTION(StdCLib, abs, c_abs);


int32_t c_labs(int32_t x);LIBRARY_FUNCTION(StdCLib, labs, c_labs);


LIBRARY_RAW(StdCLib, div);


LIBRARY_RAW(StdCLib, ldiv);


int32_t c_rand();LIBRARY_FUNCTION(StdCLib, rand, c_rand);


void c_srand(uint32_t seed);LIBRARY_FUNCTION(StdCLib, srand, c_srand);


char* c_ecvt(double x, int32_t n, uint32_t decpt, uint32_t sign);LIBRARY_FUNCTION(StdCLib, ecvt, c_ecvt);


char* c_fcvt(double x, int32_t n, uint32_t decpt, uint32_t sign);LIBRARY_FUNCTION(StdCLib, fcvt, c_fcvt);


int32_t c_mblen(const char *s, uint32_t n);LIBRARY_FUNCTION(StdCLib, mblen, c_mblen);


int32_t c_mbtowc(uint32_t pwc, const char *s, uint32_t n);LIBRARY_FUNCTION(StdCLib, mbtowc, c_mbtowc);


int32_t c_wctomb(char *s, int32_t wc);LIBRARY_FUNCTION(StdCLib, wctomb, c_wctomb);


uint32_t c_mbstowcs(uint32_t pwcs, const char *s, uint32_t n);LIBRARY_FUNCTION(StdCLib, mbstowcs, c_mbstowcs);


uint32_t c_wcstombs(char *s, uint32_t pwcs, uint32_t n);LIBRARY_FUNCTION(StdCLib, wcstombs, c_wcstombs);


char* c_malloc(uint32_t size);LIBRARY_FUNCTION(StdCLib, malloc, c_malloc);


char* c_calloc(uint32_t n, uint32_t size);LIBRARY_FUNCTION(StdCLib, calloc, c_calloc);


void c_free(char *p);LIBRARY_FUNCTION(StdCLib, free, c_free);


char* c_realloc(char *p, uint32_t size);LIBRARY_FUNCTION(StdCLib, realloc, c_realloc);


void c_qsort(char *base, uint32_t n, uint32_t size, uint32_t compar);LIBRARY_FUNCTION(StdCLib, qsort, c_qsort);


char* c_bsearch(const char *key, char *base, uint32_t n, uint32_t size, uint32_t compar);LIBRARY_FUNCTION(StdCLib, bsearch, c_bsearch);


int32_t c_atexit(uint32_t func);LIBRARY_FUNCTION(StdCLib, atexit, c_atexit);


void c_exit(int32_t status);LIBRARY_FUNCTION(StdCLib, exit, c_exit);


void c__exit(int32_t status);LIBRARY_FUNCTION(StdCLib, _exit, c__exit);


void c_abort();LIBRARY_FUNCTION(StdCLib, abort, c_abort);


void c_abort();LIBRARY_FUNCTION(StdCLib, __abort, c_abort);


char* c_getenv(const char *arg1);LIBRARY_FUNCTION(StdCLib, getenv, c_getenv);


int32_t c_system(const char *s);LIBRARY_FUNCTION(StdCLib, system, c_system);


void c_breakpoint();LIBRARY_FUNCTION(StdCLib, _BreakPoint, c_breakpoint);


void c_assertprint(const char *file, int32_t line, const char *expr);LIBRARY_FUNCTION(StdCLib, __assertprint, c_assertprint);


uint32_t c_signal(int32_t sig, uint32_t func);LIBRARY_FUNCTION(StdCLib, signal, c_signal);


int32_t c_raise(int32_t sig);LIBRARY_FUNCTION(StdCLib, raise, c_raise);


LIBRARY_RAW(StdCLib, __setjmp);


LIBRARY_RAW(StdCLib, longjmp);


uint32_t c_time(uint32_t timer);LIBRARY_FUNCTION(StdCLib, time, c_time);


uint32_t c_clock();LIBRARY_FUNCTION(StdCLib, clock, c_clock);


double c_difftime(uint32_t t1, uint32_t t0);LIBRARY_FUNCTION(StdCLib, difftime, c_difftime);


uint32_t c_mktime(uint32_t p);LIBRARY_FUNCTION(StdCLib, mktime, c_mktime);


char* c_localtime(uint32_t timer);LIBRARY_FUNCTION(StdCLib, localtime, c_localtime);


char* c_gmtime(uint32_t timer);LIBRARY_FUNCTION(StdCLib, gmtime, c_gmtime);


char* c_asctime(uint32_t p);LIBRARY_FUNCTION(StdCLib, asctime, c_asctime);


char* c_ctime(uint32_t timer);LIBRARY_FUNCTION(StdCLib, ctime, c_ctime);


uint32_t c_strftime(char *s, uint32_t max, const char *format, uint32_t p);LIBRARY_FUNCTION(StdCLib, strftime, c_strftime);


char* c_setlocale(int32_t category, const char *locale);LIBRARY_FUNCTION(StdCLib, setlocale, c_setlocale);


char* c_localeconv();LIBRARY_FUNCTION(StdCLib, localeconv, c_localeconv);


int16_t c_PLstrcmp(const uint8_t *a, const uint8_t *b);LIBRARY_FUNCTION(StdCLib, PLstrcmp, c_PLstrcmp);


int16_t c_PLstrncmp(const uint8_t *a, const uint8_t *b, int16_t num);LIBRARY_FUNCTION(StdCLib, PLstrncmp, c_PLstrncmp);


uint8_t* c_PLstrcpy(uint8_t *d, const uint8_t *s);LIBRARY_FUNCTION(StdCLib, PLstrcpy, c_PLstrcpy);


uint8_t* c_PLstrncpy(uint8_t *d, const uint8_t *s, int16_t num);LIBRARY_FUNCTION(StdCLib, PLstrncpy, c_PLstrncpy);


uint8_t* c_PLstrcat(uint8_t *d, const uint8_t *s);LIBRARY_FUNCTION(StdCLib, PLstrcat, c_PLstrcat);


uint8_t* c_PLstrncat(uint8_t *d, const uint8_t *s, int16_t num);LIBRARY_FUNCTION(StdCLib, PLstrncat, c_PLstrncat);


uint8_t* c_PLstrchr(uint8_t *s, int16_t ch);LIBRARY_FUNCTION(StdCLib, PLstrchr, c_PLstrchr);


uint8_t* c_PLstrrchr(uint8_t *s, int16_t ch);LIBRARY_FUNCTION(StdCLib, PLstrrchr, c_PLstrrchr);


uint8_t* c_PLstrpbrk(uint8_t *s, const uint8_t *set);LIBRARY_FUNCTION(StdCLib, PLstrpbrk, c_PLstrpbrk);


int16_t c_PLstrspn(const uint8_t *s, const uint8_t *set);LIBRARY_FUNCTION(StdCLib, PLstrspn, c_PLstrspn);


uint8_t* c_PLstrstr(uint8_t *s, const uint8_t *t);LIBRARY_FUNCTION(StdCLib, PLstrstr, c_PLstrstr);


int16_t c_PLstrlen(const uint8_t *s);LIBRARY_FUNCTION(StdCLib, PLstrlen, c_PLstrlen);


int16_t c_PLpos(const uint8_t *sub, const uint8_t *str);LIBRARY_FUNCTION(StdCLib, PLpos, c_PLpos);


OSErr c_MakeResolvedFSSpec(int16_t vol, int32_t dir, uint8_t *path, FSSpec *spec, Boolean *isFolder, Boolean *hadAlias, Boolean *leafIsAlias);LIBRARY_FUNCTION(StdCLib, MakeResolvedFSSpec, c_MakeResolvedFSSpec);


OSErr c_ResolveFolderAliases(int16_t vol, int32_t dir, uint8_t *path, Boolean resolveLeaf, FSSpec *spec, Boolean *isFolder, Boolean *hadAlias, Boolean *leafIsAlias);LIBRARY_FUNCTION(StdCLib, ResolveFolderAliases, c_ResolveFolderAliases);


OSErr c_ResolvePath(const char *raw, char *resolved, Boolean *isFolder, Boolean *hadAlias);LIBRARY_FUNCTION(StdCLib, ResolvePath, c_ResolvePath);


OSErr c_ResolvePath(const char *raw, char *resolved, Boolean *isFolder, Boolean *hadAlias);LIBRARY_FUNCTION(StdCLib, IEResolvePath, c_ResolvePath);


OSErr c_MakeResolvedPath(int16_t vol, int32_t dir, uint8_t *path, Boolean resolveLeaf, char *buffer, Boolean *isFolder, Boolean *hadAlias, Boolean *leafIsAlias);LIBRARY_FUNCTION(StdCLib, MakeResolvedPath, c_MakeResolvedPath);


Boolean c_TrapAvailable(int16_t trapNum);LIBRARY_FUNCTION(StdCLib, TrapAvailable, c_TrapAvailable);


int32_t c_getpid();LIBRARY_FUNCTION(StdCLib, getpid, c_getpid);


int32_t c_filbuf(uint32_t f);LIBRARY_FUNCTION(StdCLib, _filbuf, c_filbuf);


int32_t c_flsbuf(uint32_t c, uint32_t f);LIBRARY_FUNCTION(StdCLib, _flsbuf, c_flsbuf);


int32_t c_xflsbuf(uint32_t f);LIBRARY_FUNCTION(StdCLib, _xflsbuf, c_xflsbuf);


int32_t c_wrtchk(uint32_t f);LIBRARY_FUNCTION(StdCLib, _wrtchk, c_wrtchk);


void c_bufsync(uint32_t arg1);LIBRARY_FUNCTION(StdCLib, _bufsync, c_bufsync);


void c_coExit();LIBRARY_FUNCTION(StdCLib, _coExit, c_coExit);


int32_t c_fgetc(uint32_t f);LIBRARY_FUNCTION(StdCLib, fgetc, c_fgetc);


int32_t c_fgetc(uint32_t f);LIBRARY_FUNCTION(StdCLib, getc, c_fgetc);


int32_t c_fputc(int32_t c, uint32_t f);LIBRARY_FUNCTION(StdCLib, fputc, c_fputc);


int32_t c_fputc(int32_t c, uint32_t f);LIBRARY_FUNCTION(StdCLib, putc, c_fputc);


int32_t c_getchar();LIBRARY_FUNCTION(StdCLib, getchar, c_getchar);


int32_t c_putchar(int32_t c);LIBRARY_FUNCTION(StdCLib, putchar, c_putchar);


int32_t c_ungetc(int32_t c, uint32_t f);LIBRARY_FUNCTION(StdCLib, ungetc, c_ungetc);


char* c_fgets(char *s, int32_t n, uint32_t f);LIBRARY_FUNCTION(StdCLib, fgets, c_fgets);


char* c_gets(char *s);LIBRARY_FUNCTION(StdCLib, gets, c_gets);


int32_t c_fputs(const char *s, uint32_t f);LIBRARY_FUNCTION(StdCLib, fputs, c_fputs);


int32_t c_puts(const char *s);LIBRARY_FUNCTION(StdCLib, puts, c_puts);


int32_t c_getw(uint32_t f);LIBRARY_FUNCTION(StdCLib, getw, c_getw);


int32_t c_putw(int32_t w, uint32_t f);LIBRARY_FUNCTION(StdCLib, putw, c_putw);


uint32_t c_fread(uint32_t dst, uint32_t sz, uint32_t n, uint32_t f);LIBRARY_FUNCTION(StdCLib, fread, c_fread);


uint32_t c_fwrite(uint32_t src, uint32_t sz, uint32_t n, uint32_t f);LIBRARY_FUNCTION(StdCLib, fwrite, c_fwrite);


uint32_t c_fopen(const char *path, const char *mode);LIBRARY_FUNCTION(StdCLib, fopen, c_fopen);


uint32_t c_freopen(const char *path, const char *mode, uint32_t f);LIBRARY_FUNCTION(StdCLib, freopen, c_freopen);


uint32_t c_fdopen(int32_t fd, const char *mode);LIBRARY_FUNCTION(StdCLib, fdopen, c_fdopen);


int32_t c_fclose(uint32_t f);LIBRARY_FUNCTION(StdCLib, fclose, c_fclose);


int32_t c_fflush(uint32_t f);LIBRARY_FUNCTION(StdCLib, fflush, c_fflush);


int32_t c_fseek(uint32_t f, int32_t offset, int32_t whence);LIBRARY_FUNCTION(StdCLib, fseek, c_fseek);


int32_t c_ftell(uint32_t f);LIBRARY_FUNCTION(StdCLib, ftell, c_ftell);


void c_rewind(uint32_t f);LIBRARY_FUNCTION(StdCLib, rewind, c_rewind);


int32_t c_fgetpos(uint32_t f, uint32_t pos);LIBRARY_FUNCTION(StdCLib, fgetpos, c_fgetpos);


int32_t c_fsetpos(uint32_t f, uint32_t pos);LIBRARY_FUNCTION(StdCLib, fsetpos, c_fsetpos);


void c_clearerr(uint32_t f);LIBRARY_FUNCTION(StdCLib, clearerr, c_clearerr);


int32_t c_feof(uint32_t f);LIBRARY_FUNCTION(StdCLib, feof, c_feof);


int32_t c_ferror(uint32_t f);LIBRARY_FUNCTION(StdCLib, ferror, c_ferror);


int32_t c_setvbuf(uint32_t f, uint32_t buf, int32_t mode, uint32_t sz);LIBRARY_FUNCTION(StdCLib, setvbuf, c_setvbuf);


void c_setbuf(uint32_t f, uint32_t buf);LIBRARY_FUNCTION(StdCLib, setbuf, c_setbuf);


int32_t c_remove(const char *path);LIBRARY_FUNCTION(StdCLib, remove, c_remove);


int32_t c_remove(const char *path);LIBRARY_FUNCTION(StdCLib, unlink, c_remove);


int32_t c_rename(const char *from, const char *to);LIBRARY_FUNCTION(StdCLib, rename, c_rename);


char* c_tmpnam(char *s);LIBRARY_FUNCTION(StdCLib, tmpnam, c_tmpnam);


uint32_t c_tmpfile();LIBRARY_FUNCTION(StdCLib, tmpfile, c_tmpfile);


char* c_mktemp(char *tmpl);LIBRARY_FUNCTION(StdCLib, mktemp, c_mktemp);


void c_fsetfileinfo(const char *path, uint32_t creator, uint32_t type);LIBRARY_FUNCTION(StdCLib, fsetfileinfo, c_fsetfileinfo);


void c_perror(const char *s);LIBRARY_FUNCTION(StdCLib, perror, c_perror);


int32_t c_open(const char *path, int32_t oflag);LIBRARY_FUNCTION(StdCLib, open, c_open);


int32_t c_creat(const char *path);LIBRARY_FUNCTION(StdCLib, creat, c_creat);


int32_t c_close(int32_t fd);LIBRARY_FUNCTION(StdCLib, close, c_close);


int32_t c_read(int32_t fd, uint32_t buf, uint32_t n);LIBRARY_FUNCTION(StdCLib, read, c_read);


int32_t c_write(int32_t fd, uint32_t buf, uint32_t n);LIBRARY_FUNCTION(StdCLib, write, c_write);


int32_t c_lseek(int32_t fd, int32_t off, int32_t whence);LIBRARY_FUNCTION(StdCLib, lseek, c_lseek);


int32_t c_dup(int32_t fd);LIBRARY_FUNCTION(StdCLib, dup, c_dup);


int32_t c_fcntl(int32_t fd, uint32_t cmd, int32_t arg);LIBRARY_FUNCTION(StdCLib, fcntl, c_fcntl);


int32_t c_ioctl(int32_t fd, uint32_t cmd, uint32_t arg);LIBRARY_FUNCTION(StdCLib, ioctl, c_ioctl);


int32_t c_faccess(const char *path, uint32_t cmd, uint32_t arg);LIBRARY_FUNCTION(StdCLib, faccess, c_faccess);


int32_t c_faccess(const char *path, uint32_t cmd, uint32_t arg);LIBRARY_FUNCTION(StdCLib, _faccess, c_faccess);


int32_t c_access(const char *path, int32_t arg2);LIBRARY_FUNCTION(StdCLib, access, c_access);


LIBRARY_RAW(StdCLib, printf);


LIBRARY_RAW(StdCLib, fprintf);


LIBRARY_RAW(StdCLib, sprintf);


LIBRARY_RAW(StdCLib, vprintf);


LIBRARY_RAW(StdCLib, vfprintf);


LIBRARY_RAW(StdCLib, vsprintf);


LIBRARY_RAW(StdCLib, scanf);


LIBRARY_RAW(StdCLib, fscanf);


LIBRARY_RAW(StdCLib, sscanf);


} /* namespace Executor */