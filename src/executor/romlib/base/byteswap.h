#if !defined(_BYTESWAP_H_)
#define _BYTESWAP_H_

#include <stdint.h>
#include <type_traits>


#if !defined(BIGENDIAN) & !defined(LITTLEENDIAN)

#if defined(powerpc) || defined(__ppc__)
#define BIGENDIAN
#elif defined(i386) \
     || defined(__x86_64) || defined(__x86_64__) \
     || defined (_M_X64) || defined(_M_IX86) \
     || defined(__arm__)
#define LITTLEENDIAN
#else
#error "Unknown CPU type"
#endif

#endif

#if defined(_MSC_VER) && defined(LITTLEENDIAN)
#include <stdlib.h>     /* for _byteswap_* */
#endif

namespace Executor
{
#ifdef _MSC_VER
inline uint16_t swap16(uint16_t v) { return _byteswap_ushort(v); }
inline uint32_t swap32(uint32_t v) { return _byteswap_ulong(v); }
inline uint64_t swap64(uint64_t v) { return _byteswap_uint64(v); }
#else
inline uint16_t swap16(uint16_t v) { return __builtin_bswap16(v); }
inline uint32_t swap32(uint32_t v) { return __builtin_bswap32(v); }
inline uint64_t swap64(uint64_t v) { return __builtin_bswap64(v); }
#endif

#if defined(BIGENDIAN)

#define CW_RAW(rhs) (rhs)
#define CL_RAW(rhs) (rhs)
#define CWC_RAW(rhs) (rhs)
#define CLC_RAW(rhs) (rhs)

#else /* !defined (BIGENDIAN) */

template<class TT>
inline TT CW_RAW(TT n)
{
    return swap16((uint16_t)n);
}
template<class TT>
inline TT CL_RAW(TT n)
{
    return swap32((uint32_t)n);
}

#define CWC_RAW(n) ((std::conditional_t<std::is_signed_v<decltype(n)>, int16_t, uint16_t>)(                       \
    (((((unsigned short)n) << 8) & 0xFF00) \
                   | ((((unsigned short)n) >> 8) & 0x00FF))))
#define CLC_RAW(n) ((std::conditional_t<std::is_signed_v<decltype(n)>, int32_t, uint32_t>)(                       \
    ((((unsigned int)((n) | 0) & 0x000000FF) << 24) \
          | (((unsigned int)(n)&0x0000FF00) << 8)        \
          | (((unsigned int)(n)&0x00FF0000) >> 8)        \
          | (((unsigned int)(n)&0xFF000000)              \
             >> 24))))

#endif /* !defined (BIGENDIAN) */



}

#endif /* !_BYTESWAP_H_ */
