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

static void blocks(AVFrame *frame, int blocksize, int mul)
{
    for(int y=0; y<frame->height; y++) {
        for(int x=0; x<frame->width; x++) {
            frame->data[0][x + y*frame->linesize[0]] = x/blocksize*mul + y/blocksize*mul;
        }
    }
    for(int y=0; y<(frame->height+1)/2; y++) {
        for(int x=0; x<(frame->width+1)/2; x++) {
            frame->data[1][x + y*frame->linesize[1]] = x/blocksize*mul + y/blocksize*mul;
            frame->data[2][x + y*frame->linesize[2]] = x/blocksize * (y/blocksize)*mul;
        }
    }
}

static int64_t test(int width, int height, const char * filter_string, int blocksize, int flags, int pict_type, int quality) {
    AVFrame *in  = av_frame_alloc();
    AVFrame *out = av_frame_alloc();
    pp_context *context = pp_get_context(width, height, flags);
    pp_mode *mode = pp_get_mode_by_name_and_quality(filter_string, quality);
    int64_t ret;
#define  QP_STRIDE (352/16)
    int8_t qp[QP_STRIDE * 352/16];

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

    blocks(in, blocksize, 11);

    for(int i= 0; i<sizeof(qp); i++)
        qp[i] = i % 31;

    pp_postprocess( (cuint8[]){in->data[0], in->data[1], in->data[2]}, in->linesize,
                   out->data, out->linesize,
                   width, height, qp, QP_STRIDE,
                   mode, context, pict_type);

    ret = ff_chksum(out);
end:
    av_frame_free(&in);
    av_frame_free(&out);
    pp_free_context(context);
    pp_free_mode(mode);

    return ret;
}

int main(int argc, char **argv) {
    const char *teststrings[] = {
        "be,de",
        "be,h1,v1",
        "be,ha,va",
        "be,al,de",
        "be,vi,de",
        "be,vi,ha,va",
    };

    for (int w=16; w< 352; w=w*3-16) {
        for (int h=16; h< 352; h=h*5-16) {
            for (int b=1; b<17; b*=2) {
                for (int c=0; c<6; c++) {
                    for (int q=0; q<17; q = 2*q+1) {
                        int64_t ret = test(w, h, teststrings[c], b, PP_FORMAT_420, 0, q);
                        printf("blocktest %dx%d %s b:%d q:%d result %"PRIX64"\n", w, h, teststrings[c], b, q, ret);
                    }
                }
            }
        }
    }

    return 0;
}
