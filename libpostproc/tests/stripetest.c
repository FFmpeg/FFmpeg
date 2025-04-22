/*
 * Copyright (c) 2025 Michael Niedermayer
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

#include "libavutil/frame.h"
#include "libavutil/adler32.h"
#include "libpostproc/postprocess.h"

typedef const uint8_t *cuint8;

static void strips(AVFrame *frame, int mul)
{
    for(int y=0; y<frame->height; y++) {
        for(int x=0; x<frame->width; x++) {
            if (y&1) {
                frame->data[0][x + y*frame->linesize[0]] = x*x + y*mul;
            } else {
                frame->data[0][x + y*frame->linesize[0]] = (y-x)*(y-x);
            }
        }
    }
    for(int y=0; y<(frame->height+1)/2; y++) {
        for(int x=0; x<(frame->width+1)/2; x++) {
            if (y&1) {
                frame->data[1][x + y*frame->linesize[1]] = x + y + mul;
                frame->data[2][x + y*frame->linesize[2]] = mul*x - y*x;
            } else {
                frame->data[1][x + y*frame->linesize[1]] = (x - y)/(mul+1);
                frame->data[2][x + y*frame->linesize[2]] = (y + x)/(mul+1);
            }
        }
    }
}

static int64_t chksum(AVFrame *f)
{
    AVAdler a = 123;

    for(int y=0; y<f->height; y++) {
        a = av_adler32_update(a, &f->data[0][y*f->linesize[0]], f->width);
    }
    for(int y=0; y<(f->height+1)/2; y++) {
        a = av_adler32_update(a, &f->data[1][y*f->linesize[1]], (f->width+1)/2);
        a = av_adler32_update(a, &f->data[2][y*f->linesize[2]], (f->width+1)/2);
    }

    return a;
}

static int64_t test(int width, int height, const char *testname, int mul, int flags, int pict_type, int quality) {
    AVFrame *in  = av_frame_alloc();
    AVFrame *out = av_frame_alloc();
    pp_context *context = pp_get_context(width, height, flags);
    pp_mode *mode = pp_get_mode_by_name_and_quality(testname, quality);
    int64_t ret;

    if (!in || !out || !context || !mode) {
        ret = AVERROR(ENOMEM);
        goto end;
    }

    in-> width = out->width  = width;
    in->height = out->height = height;
    in->format = out->format = AV_PIX_FMT_YUV420P;

    ret = av_frame_get_buffer(in, 0);
    if (ret < 0)
        goto end;

    ret = av_frame_get_buffer(out, 0);
    if (ret < 0)
        goto end;

    strips(in, mul);

    pp_postprocess( (cuint8[]){in->data[0], in->data[1], in->data[2]}, in->linesize,
                   out->data, out->linesize,
                   width, height, NULL, 0,
                   mode, context, pict_type);

    ret = chksum(out);
end:
    av_frame_free(&in);
    av_frame_free(&out);
    pp_free_context(context);
    pp_free_mode(mode);

    return ret;
}

int main(int argc, char **argv) {
    const char *teststrings[] = {
        "be,lb",
        "be,li",
        "be,ci",
        "be,md",
        "be,fd",
        "be,l5",
    };

    for (int w=8; w< 352; w=w*3-1) {
        for (int h=8; h< 352; h=h*5-7) {
            for (int b=0; b<6; b++) {
                for (int m=0; m<17; m = 2*m+1) {
                    int64_t ret = test(352, 288, teststrings[b], m, PP_FORMAT_420, 0, 11);
                    printf("striptest %dx%d T:%s m:%d result %"PRIX64"\n", w, h, teststrings[b], m, ret);
                }
            }
        }
    }

    return 0;
}
