#if !defined(_RSYS_NORETURN_H_)
#define _RSYS_NORETURN_H_

/* Macros to declare a function as not returning.  Put _NORET_1_
 * before the function declaration (but after the extern), and
 *_NORET_2_ after the end.
 */

#ifdef __GNUC__
#define _NORET_1_
#define _NORET_2_ __attribute__((noreturn))
#elif defined(_MSC_VER)
#define _NORET_1_ __declspec(noreturn)
#define _NORET_2_
#else
#define _NORET_1_
#define _NORET_2_
#endif

#endif /* !_RSYS_NORETURN_H_ */
