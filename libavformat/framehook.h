/*
 * video processing hooks
 * copyright (c) 2000, 2001 Fabrice Bellard
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef _FRAMEHOOK_H
#define _FRAMEHOOK_H

#warning VHOOK is deprecated please help porting libmpcodecs or a better filter system to ffmpeg instead of wasting your time writing new fiters for this crappy one

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
extern void frame_hook_process(struct AVPicture *pict, enum PixelFormat pix_fmt, int width, int height, int64_t pts);
extern void frame_hook_release(void);

#endif
