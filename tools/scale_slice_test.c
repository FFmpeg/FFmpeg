/*
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
#include <stdint.h>
#include <stdlib.h>

#include "decode_simple.h"

#include "libavutil/common.h"
#include "libavutil/pixdesc.h"
#include "libavutil/error.h"
#include "libavutil/lfg.h"
#include "libavutil/random_seed.h"
#include "libavutil/video_enc_params.h"

#include "libavformat/avformat.h"

#include "libavcodec/avcodec.h"

#include "libswscale/swscale.h"

typedef struct PrivData {
    unsigned int random_seed;
    AVLFG        lfg;

    struct SwsContext *scaler;

    int v_shift_dst, h_shift_dst;
    int v_shift_src, h_shift_src;

    AVFrame *frame_ref;
    AVFrame *frame_dst;
} PrivData;

static int process_frame(DecodeContext *dc, AVFrame *frame)
{
    PrivData *pd = dc->opaque;
    int slice_start = 0;
    int ret;

    if (!frame)
        return 0;

    if (!pd->scaler) {
        pd->scaler = sws_getContext(frame->width, frame->height, frame->format,
                                    pd->frame_ref->width, pd->frame_ref->height,
                                    pd->frame_ref->format, 0, NULL, NULL, NULL);
        if (!pd->scaler)
            return AVERROR(ENOMEM);

        av_pix_fmt_get_chroma_sub_sample(frame->format, &pd->h_shift_src, &pd->v_shift_src);
    }

    /* scale the whole input frame as reference */
    ret = sws_scale(pd->scaler, (const uint8_t **)frame->data, frame->linesize, 0, frame->height,
                    pd->frame_ref->data, pd->frame_ref->linesize);
    if (ret < 0)
        return ret;

    /* scale slices with randomly generated heights */
    while (slice_start < frame->height) {
        int slice_height;
        const uint8_t *src[4];

        slice_height = av_lfg_get(&pd->lfg) % (frame->height - slice_start);
        slice_height = FFALIGN(FFMAX(1, slice_height), 1 << pd->v_shift_src);

        for (int j = 0; j < FF_ARRAY_ELEMS(src) && frame->data[j]; j++) {
            int shift = (j == 1 || j == 2) ? pd->v_shift_src : 0;
            src[j] = frame->data[j] + frame->linesize[j] * (slice_start >> shift);
        }

        ret = sws_scale(pd->scaler, src, frame->linesize, slice_start, slice_height,
                        pd->frame_dst->data, pd->frame_dst->linesize);
        if (ret < 0)
            return ret;

        slice_start += slice_height;
    }

    /* compare the two results */
    for (int i = 0; i < 4 && pd->frame_ref->data[i]; i++) {
        int shift = (i == 1 || i == 2) ? pd->v_shift_dst : 0;

        if (memcmp(pd->frame_ref->data[i], pd->frame_dst->data[i],
                   pd->frame_ref->linesize[i] * (pd->frame_ref->height >> shift))) {
            fprintf(stderr, "mismatch frame %d seed %u\n",
                    dc->decoder->frame_number - 1, pd->random_seed);
            return AVERROR(EINVAL);
        }
    }

    return 0;
}

int main(int argc, char **argv)
{
    PrivData      pd;
    DecodeContext dc;

    int width, height;
    enum AVPixelFormat pix_fmt;
    const char *filename;
    int ret = 0;

    if (argc <= 4) {
        fprintf(stderr,
                "Usage: %s <input file> <dst width> <dst height> <dst pixfmt> [<random seed>] \n",
                argv[0]);
        return 0;
    }

    memset(&pd, 0, sizeof(pd));

    filename     = argv[1];
    width        = strtol(argv[2], NULL, 0);
    height       = strtol(argv[3], NULL, 0);
    pix_fmt      = av_get_pix_fmt(argv[4]);

    /* init RNG for generating slice sizes */
    if (argc >= 6)
        pd.random_seed = strtoul(argv[5], NULL, 0);
    else
        pd.random_seed = av_get_random_seed();

    av_lfg_init(&pd.lfg, pd.random_seed);

    av_pix_fmt_get_chroma_sub_sample(pix_fmt, &pd.h_shift_dst, &pd.v_shift_dst);

    /* allocate the frames for scaler output */
    for (int i = 0; i < 2; i++) {
        AVFrame *frame = av_frame_alloc();
        if (!frame) {
            fprintf(stderr, "Error allocating frames\n");
            return AVERROR(ENOMEM);
        }

        frame->width  = width;
        frame->height = height;
        frame->format = pix_fmt;

        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            fprintf(stderr, "Error allocating frame data\n");
            return ret;
        }

        /* make sure the padding is zeroed */
        for (int j = 0; j < 4 && frame->data[j]; j++) {
            int shift = (j == 1 || j == 2) ? pd.v_shift_dst : 0;
            memset(frame->data[j], 0,
                   frame->linesize[j] * (height >> shift));
        }
        if (i) pd.frame_ref = frame;
        else   pd.frame_dst = frame;
    }

    ret = ds_open(&dc, filename, 0);
    if (ret < 0) {
        fprintf(stderr, "Error opening the file\n");
        return ret;
    }

    dc.process_frame = process_frame;
    dc.opaque        = &pd;

    ret = ds_run(&dc);

    av_frame_free(&pd.frame_dst);
    av_frame_free(&pd.frame_ref);
    sws_freeContext(pd.scaler);
    ds_free(&dc);
    return ret;
}
