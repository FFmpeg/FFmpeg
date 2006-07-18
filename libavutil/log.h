#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

/**
 * Used by av_log
 */
typedef struct AVCLASS AVClass;
struct AVCLASS {
    const char* class_name;
    const char* (*item_name)(void*); /* actually passing a pointer to an AVCodecContext
                                        or AVFormatContext, which begin with an AVClass.
                                        Needed because av_log is in libavcodec and has no visibility
                                        of AVIn/OutputFormat */
    struct AVOption *option;
};

/* av_log API */

#define AV_LOG_QUIET -1
#define AV_LOG_ERROR 0
#define AV_LOG_INFO 1
#define AV_LOG_DEBUG 2

#ifdef __GNUC__
extern void av_log(void*, int level, const char *fmt, ...) __attribute__ ((__format__ (__printf__, 3, 4)));
#else
extern void av_log(void*, int level, const char *fmt, ...);
#endif

extern void av_vlog(void*, int level, const char *fmt, va_list);
extern int av_log_get_level(void);
extern void av_log_set_level(int);
extern void av_log_set_callback(void (*)(void*, int, const char*, va_list));

#endif /* LOG_H */
