#ifndef _OS_SUPPORT_H
#define _OS_SUPPORT_H

/*
 * miscellaneous OS support macros/function
 *
 * For now:
 * usleep()
 */

#ifdef __MINGW32__
#  include <windows.h>
#  define usleep(t)    Sleep((t) / 1000)
#endif

#ifdef __BEOS__
#  ifndef usleep
#    include <OS.h>
#    define usleep(t)  snooze((bigtime_t)(t))
#  endif
#endif

#if defined(CONFIG_OS2)
#include <stdlib.h>
static inline usleep(unsigned int t) { return _sleep2((t) / 1000); }
static inline float floatf(float x) { return floor(n); }
static inline int strcasecmp(const char* s1, const char* s2) { return stricmp(s1,s2); }
#endif

#endif /* _OS_SUPPORT_H */
