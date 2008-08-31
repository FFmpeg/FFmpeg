/*
 * Nellymoser encoder
 * This code is developed as part of Google Summer of Code 2008 Program.
 *
 * Copyright (c) 2008 Bartlomiej Wolowiec
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

/**
 * @file nellymoserenc.c
 * Nellymoser encoder
 * by Bartlomiej Wolowiec
 *
 * Generic codec information: libavcodec/nellymoserdec.c
 *
 * Some information also from: http://www1.mplayerhq.hu/ASAO/ASAO.zip
 *                             (Copyright Joseph Artsimovich and UAB "DKD")
 *
 * for more information about nellymoser format, visit:
 * http://wiki.multimedia.cx/index.php?title=Nellymoser
 */

#include "nellymoser.h"
#include "avcodec.h"
#include "dsputil.h"

#define BITSTREAM_WRITER_LE
#include "bitstream.h"

#define POW_TABLE_SIZE (1<<11)
#define POW_TABLE_OFFSET 3

typedef struct NellyMoserEncodeContext {
    AVCodecContext  *avctx;
    int             last_frame;
    DSPContext      dsp;
    MDCTContext     mdct_ctx;
} NellyMoserEncodeContext;

static float pow_table[POW_TABLE_SIZE];     ///< -pow(2, -i / 2048.0 - 3.0);

static const uint8_t sf_lut[96] = {
     0,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  4,  4,
     5,  5,  5,  6,  7,  7,  8,  8,  9, 10, 11, 11, 12, 13, 13, 14,
    15, 15, 16, 17, 17, 18, 19, 19, 20, 21, 22, 22, 23, 24, 25, 26,
    27, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 37, 38, 39, 40,
    41, 41, 42, 43, 44, 45, 45, 46, 47, 48, 49, 50, 51, 52, 52, 53,
    54, 55, 55, 56, 57, 57, 58, 59, 59, 60, 60, 60, 61, 61, 61, 62,
};

static const uint8_t sf_delta_lut[78] = {
     0,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  3,  3,  3,  4,  4,
     4,  5,  5,  5,  6,  6,  7,  7,  8,  8,  9, 10, 10, 11, 11, 12,
    13, 13, 14, 15, 16, 17, 17, 18, 19, 19, 20, 21, 21, 22, 22, 23,
    23, 24, 24, 25, 25, 25, 26, 26, 26, 26, 27, 27, 27, 27, 27, 28,
    28, 28, 28, 28, 28, 29, 29, 29, 29, 29, 29, 29, 29, 30,
};

static const uint8_t quant_lut[230] = {
     0,

     0,  1,  2,

     0,  1,  2,  3,  4,  5,  6,

     0,  1,  1,  2,  2,  3,  3,  4,  5,  6,  7,  8,  9, 10, 11, 11,
    12, 13, 13, 13, 14,

     0,  1,  1,  2,  2,  2,  3,  3,  4,  4,  5,  5,  6,  6,  7,  8,
     8,  9, 10, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
    22, 23, 23, 24, 24, 25, 25, 26, 26, 27, 27, 28, 28, 29, 29, 29,
    30,

     0,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  3,  3,  3,  3,
     4,  4,  4,  5,  5,  5,  6,  6,  7,  7,  7,  8,  8,  9,  9,  9,
    10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 13, 14, 14, 14, 15, 15,
    15, 15, 16, 16, 16, 17, 17, 17, 18, 18, 18, 19, 19, 20, 20, 20,
    21, 21, 22, 22, 23, 23, 24, 25, 26, 26, 27, 28, 29, 30, 31, 32,
    33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 42, 43, 44, 44, 45, 45,
    46, 47, 47, 48, 48, 49, 49, 50, 50, 50, 51, 51, 51, 52, 52, 52,
    53, 53, 53, 54, 54, 54, 55, 55, 55, 56, 56, 56, 57, 57, 57, 57,
    58, 58, 58, 58, 59, 59, 59, 59, 60, 60, 60, 60, 60, 61, 61, 61,
    61, 61, 61, 61, 62,
};

static const float quant_lut_mul[7] = { 0.0,  0.0,  2.0,  2.0,  5.0, 12.0,  36.6 };
static const float quant_lut_add[7] = { 0.0,  0.0,  2.0,  7.0, 21.0, 56.0, 157.0 };
static const uint8_t quant_lut_offset[8] = { 0, 0, 1, 4, 11, 32, 81, 230 };

static av_cold int encode_init(AVCodecContext *avctx)
{
    NellyMoserEncodeContext *s = avctx->priv_data;
    int i;

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Nellymoser supports only 1 channel\n");
        return -1;
    }

    if (avctx->sample_rate != 8000 && avctx->sample_rate != 11025 &&
        avctx->sample_rate != 22050 && avctx->sample_rate != 44100 &&
        avctx->strict_std_compliance >= FF_COMPLIANCE_NORMAL) {
        av_log(avctx, AV_LOG_ERROR, "Nellymoser works only with 8000, 11025, 22050 and 44100 sample rate\n");
        return -1;
    }

    avctx->frame_size = NELLY_SAMPLES;
    s->avctx = avctx;
    ff_mdct_init(&s->mdct_ctx, 8, 0);
    dsputil_init(&s->dsp, avctx);

    /* Generate overlap window */
    ff_sine_window_init(ff_sine_128, 128);
    for (i = 0; i < POW_TABLE_SIZE; i++)
        pow_table[i] = -pow(2, -i / 2048.0 - 3.0 + POW_TABLE_OFFSET);

    return 0;
}

static av_cold int encode_end(AVCodecContext *avctx)
{
    NellyMoserEncodeContext *s = avctx->priv_data;

    ff_mdct_end(&s->mdct_ctx);
    return 0;
}

#define find_best(val, table, LUT, LUT_add, LUT_size) \
    best_idx = \
        LUT[av_clip ((lrintf(val) >> 8) + LUT_add, 0, LUT_size - 1)]; \
    if (fabs(val - table[best_idx]) > fabs(val - table[best_idx + 1])) \
        best_idx++;

AVCodec nellymoser_encoder = {
    .name = "nellymoser",
    .type = CODEC_TYPE_AUDIO,
    .id = CODEC_ID_NELLYMOSER,
    .priv_data_size = sizeof(NellyMoserEncodeContext),
    .init = encode_init,
    .encode = encode_frame,
    .close = encode_end,
    .capabilities = CODEC_CAP_SMALL_LAST_FRAME | CODEC_CAP_DELAY,
    .long_name = NULL_IF_CONFIG_SMALL("Nellymoser Asao Codec"),
};
