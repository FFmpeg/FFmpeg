/*
 * AMR Audio encoder stub
 * Copyright (c) 2003 the ffmpeg project
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <vo-amrwbenc/enc_if.h>

#include "avcodec.h"

static const char wb_bitrate_unsupported[] =
    "bitrate not supported: use one of 6.6k, 8.85k, 12.65k, 14.25k, 15.85k, "
    "18.25k, 19.85k, 23.05k, or 23.85k\n";

typedef struct AMRWBContext {
    void  *state;
    int    mode;
    int    allow_dtx;
} AMRWBContext;

static int get_wb_bitrate_mode(int bitrate)
{
    /* make the correspondance between bitrate and mode */
    static const int rates[] = {  6600,  8850, 12650, 14250, 15850, 18250,
                                 19850, 23050, 23850 };
    int i;

    for (i = 0; i < 9; i++)
        if (rates[i] == bitrate)
            return i;
    /* no bitrate matching, return an error */
    return -1;
}

static av_cold int amr_wb_encode_init(AVCodecContext *avctx)
{
    AMRWBContext *s = avctx->priv_data;

    if (avctx->sample_rate != 16000) {
        av_log(avctx, AV_LOG_ERROR, "Only 16000Hz sample rate supported\n");
        return AVERROR(ENOSYS);
    }

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return AVERROR(ENOSYS);
    }

    if ((s->mode = get_wb_bitrate_mode(avctx->bit_rate)) < 0) {
        av_log(avctx, AV_LOG_ERROR, wb_bitrate_unsupported);
        return AVERROR(ENOSYS);
    }

    avctx->frame_size  = 320;
    avctx->coded_frame = avcodec_alloc_frame();

    s->state     = E_IF_init();
    s->allow_dtx = 0;

    return 0;
}

static int amr_wb_encode_close(AVCodecContext *avctx)
{
    AMRWBContext *s = avctx->priv_data;

    E_IF_exit(s->state);
    av_freep(&avctx->coded_frame);
    return 0;
}

static int amr_wb_encode_frame(AVCodecContext *avctx,
                               unsigned char *frame/*out*/,
                               int buf_size, void *data/*in*/)
{
    AMRWBContext *s = avctx->priv_data;
    int size;

    if ((s->mode = get_wb_bitrate_mode(avctx->bit_rate)) < 0) {
        av_log(avctx, AV_LOG_ERROR, wb_bitrate_unsupported);
        return AVERROR(ENOSYS);
    }
    size = E_IF_encode(s->state, s->mode, data, frame, s->allow_dtx);
    return size;
}

AVCodec ff_libvo_amrwbenc_encoder = {
    "libvo_amrwbenc",
    CODEC_TYPE_AUDIO,
    CODEC_ID_AMR_WB,
    sizeof(AMRWBContext),
    amr_wb_encode_init,
    amr_wb_encode_frame,
    amr_wb_encode_close,
    NULL,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("libvo-amrwbenc Adaptive Multi-Rate "
                                      "(AMR) Wide-Band"),
};

