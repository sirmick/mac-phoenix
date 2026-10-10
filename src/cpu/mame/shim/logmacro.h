/*
 * logmacro.h - MacPhoenix's stand-in for MAME's logmacro.h. Included once
 * per translation unit after everything else, as upstream requires.
 */
#ifndef VERBOSE
#define VERBOSE 0
#endif

#ifndef LOG_OUTPUT_FUNC
#define LOG_OUTPUT_FUNC [this](auto &&...args) { this->logerror(std::forward<decltype(args)>(args)...); }
#endif

#ifndef LOG_GENERAL
#define LOG_GENERAL (1U << 0)
#endif

#define LOGMASKED(mask, ...) \
    do { if(VERBOSE & (mask)) (LOG_OUTPUT_FUNC)(__VA_ARGS__); } while(false)

#define LOG(...) LOGMASKED(LOG_GENERAL, __VA_ARGS__)
