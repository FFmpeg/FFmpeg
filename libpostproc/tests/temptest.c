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
#include "test_utils.h"

typedef const uint8_t *cuint8;

static void stuff(AVFrame *frame, unsigned *state, int mul)
{
    for(int y=0; y<frame->height; y++) {
        for(int x=0; x<frame->width; x++) {
            *state= *state*1664525+1013904223;
            frame->data[0][x + y*frame->linesize[0]] = x*x + (y-x)*mul + ((((x+y)&0xFF)* (int64_t)(*state))>>32);
        }
    }
    for(int y=0; y<(frame->height+1)/2; y++) {
        for(int x=0; x<(frame->width+1)/2; x++) {
            *state= *state*1664525+1013904223;
            frame->data[1][x + y*frame->linesize[1]] = x + y + ((mul*(int64_t)(*state))>>32);
            frame->data[2][x + y*frame->linesize[2]] = mul*x - ((y*x*(int64_t)(*state))>>32);
        }
    }
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

    unsigned state = mul;
    for(int f=0; f<10; f++) {
        stuff(in, &state, mul);

        pp_postprocess( (cuint8[]){in->data[0], in->data[1], in->data[2]}, in->linesize,
                    out->data, out->linesize,
                    width, height, NULL, 0,
                    mode, context, pict_type);

        ret += ff_chksum(out);
        ret *= 1664525U;
    }
end:
    av_frame_free(&in);
    av_frame_free(&out);
    pp_free_context(context);
    pp_free_mode(mode);

    return ret;
}

int main(int argc, char **argv) {

    for(int a=0; a<600000; a= 17*a+1) {
        for(int b=a; b<600000; b= 17*b+1) {
            for(int c=b; c<600000; c= 17*c+1) {
                for (int m=0; m<128; m = 3*m+1) {
                    char buf[100];
                    snprintf(buf, sizeof(buf), "be,tn:%d:%d:%d", a, b, c);
                    int64_t ret = test(352, 288, buf, m, PP_FORMAT_420, 0, 11);
                    printf("temptest %d %d %d %d result %"PRIX64"\n", a,b,c,m, ret);
                }
            }
        }
    }

    return 0;
}
