/*
 * Null Video Hook 
 * Copyright (c) 2002 Philip Gladstone
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdio.h>

#include "framehook.h"

typedef struct {
    int dummy;
} ContextInfo;

void Release(void *ctx)
{
    ContextInfo *ci;
    ci = (ContextInfo *) ctx;

    if (ctx)
        av_free(ctx);
}

int Configure(void **ctxp, int argc, char *argv[])
{
    fprintf(stderr, "Called with argc=%d\n", argc);

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
        if (img_convert(&picture1, PIX_FMT_RGB24, 
                        picture, pix_fmt, width, height) < 0) {
            av_free(buf);
            return;
        }
        pict = &picture1;
    }

    /* Insert filter code here */

    if (pix_fmt != PIX_FMT_RGB24) {
        if (img_convert(picture, pix_fmt, 
                        &picture1, PIX_FMT_RGB24, width, height) < 0) {
        }
    }

    av_free(buf);
}

