#if !defined(_MACROS_H_)
#define _MACROS_H_

#if !defined(ALLOCABEGIN)
#define ALLOCABEGIN /* nothing */
#define ALLOCA(n) alloca(n)
#define ALLOCAEND /* nothing */
#endif

#define str255assign(d, s) \
    (memcpy(d, s, uint8_t((s)[0]) + 1))

#endif /* !_MACROS_H_ */
