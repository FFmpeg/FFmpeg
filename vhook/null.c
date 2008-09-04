/*
 * Null Video Hook
 * Copyright (c) 2002 Philip Gladstone
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
#include <stdio.h>

#include "libavformat/framehook.h"
#include "libswscale/swscale.h"

static int sws_flags = SWS_BICUBIC;

typedef struct {
    int dummy;

    // This vhook first converts frame to RGB ...
    struct SwsContext *toRGB_convert_ctx;

    // ... and later converts back frame from RGB to initial format
    struct SwsContext *fromRGB_convert_ctx;

} ContextInfo;

void Release(void *ctx)
{
    ContextInfo *ci;
    ci = (ContextInfo *) ctx;

    if (ctx) {
        sws_freeContext(ci->toRGB_convert_ctx);
        sws_freeContext(ci->fromRGB_convert_ctx);
        av_free(ctx);
    }
}

int Configure(void **ctxp, int argc, char *argv[])
{
    av_log(NULL, AV_LOG_DEBUG, "Called with argc=%d\n", argc);

    *ctxp = av_mallocz(sizeof(ContextInfo));
    return 0;
}

void Process(void *ctx, AVPicture *picture, enum PixelFormat pix_fmt, int width, int height, int64_t pts)
{
    ContextInfo *ci = (ContextInfo *) ctx;
    char *buf = 0;
    AVPicture picture1;
    AVPicture *pict = picture;

    (void) ci;

    if (pix_fmt != PIX_FMT_RGB24) {
        int size;

        size = avpicture_get_size(PIX_FMT_RGB24, width, height);
        buf = av_malloc(size);

        avpicture_fill(&picture1, buf, PIX_FMT_RGB24, width, height);

        // if we already got a SWS context, let's realloc if is not re-useable
        ci->toRGB_convert_ctx = sws_getCachedContext(ci->toRGB_convert_ctx,
                                    width, height, pix_fmt,
                                    width, height, PIX_FMT_RGB24,
                                    sws_flags, NULL, NULL, NULL);
        if (ci->toRGB_convert_ctx == NULL) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot initialize the toRGB conversion context\n");
            return;
        }
// img_convert parameters are          2 first destination, then 4 source
// sws_scale   parameters are context, 4 first source,      then 2 destination
        sws_scale(ci->toRGB_convert_ctx,
            picture->data, picture->linesize, 0, height,
            picture1.data, picture1.linesize);

        pict = &picture1;
    }

    /* Insert filter code here */

    if (pix_fmt != PIX_FMT_RGB24) {
        ci->fromRGB_convert_ctx = sws_getCachedContext(ci->fromRGB_convert_ctx,
                                        width, height, PIX_FMT_RGB24,
                                        width, height, pix_fmt,
                                        sws_flags, NULL, NULL, NULL);
        if (ci->fromRGB_convert_ctx == NULL) {
            av_log(NULL, AV_LOG_ERROR,
                   "Cannot initialize the fromRGB conversion context\n");
            return;
        }
// img_convert parameters are          2 first destination, then 4 source
// sws_scale   parameters are context, 4 first source,      then 2 destination
        sws_scale(ci->fromRGB_convert_ctx,
            picture1.data, picture1.linesize, 0, height,
            picture->data, picture->linesize);
    }

    av_free(buf);
}

