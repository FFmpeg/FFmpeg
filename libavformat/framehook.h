#ifndef _FRAMEHOOK_H
#define _FRAMEHOOK_H

/*
 * Prototypes for interface to .so that implement a video processing hook 
 */

#include "avcodec.h"

/* Function must be called 'Configure' */
typedef int (FrameHookConfigure)(void **ctxp, int argc, char *argv[]);
typedef FrameHookConfigure *FrameHookConfigureFn;
extern FrameHookConfigure Configure;

/* Function must be called 'Process' */
typedef void (FrameHookProcess)(void *ctx, struct AVPicture *pict, enum PixelFormat pix_fmt, int width, int height, int64_t pts);
typedef FrameHookProcess *FrameHookProcessFn;
extern FrameHookProcess Process;

/* Function must be called 'Release' */
typedef void (FrameHookRelease)(void *ctx);
typedef FrameHookRelease *FrameHookReleaseFn;
extern FrameHookRelease Release;

extern int frame_hook_add(int argc, char *argv[]);
extern void frame_hook_process(struct AVPicture *pict, enum PixelFormat pix_fmt, int width, int height);
extern void frame_hook_release(void);

#endif
