#ifndef _OS_SUPPORT_H
#define _OS_SUPPORT_H

/*
 * miscellaneous OS support macros
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
#  include <stdlib.h>
#  define usleep(t)    _sleep2((t) / 1000)
#endif

#endif /* _OS_SUPPORT_H */
