#ifndef _FRAMEHOOK_H
#define _FRAMEHOOK_H

/*
 * Prototypes for interface to .so that implement a video processing hook 
 */

#include "avcodec.h"

/* Function must be called 'Configure' */
typedef int (*FrameHookConfigureFn)(void **ctxp, int argc, char *argv[]);

/* Function must be called 'Process' */
typedef void (*FrameHookProcessFn)(void *ctx, struct AVPicture *pict, enum PixelFormat pix_fmt, int width, int height, INT64 pts);

extern int frame_hook_add(int argc, char *argv[]);
extern void frame_hook_process(struct AVPicture *pict, enum PixelFormat pix_fmt, int width, int height);

#endif
