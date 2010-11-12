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
 * @file
 * Nellymoser encoder
 * by Bartlomiej Wolowiec
 *
 * Generic codec information: libavcodec/nellymoserdec.c
 *
 * Some information also from: http://samples.mplayerhq.hu/A-codecs/Nelly_Moser/ASAO/ASAO.zip
 *                             (Copyright Joseph Artsimovich and UAB "DKD")
 *
 * for more information about nellymoser format, visit:
 * http://wiki.multimedia.cx/index.php?title=Nellymoser
 */

#include "nellymoser.h"
#include "avcodec.h"
#include "dsputil.h"
#include "fft.h"

#define BITSTREAM_WRITER_LE
#include "put_bits.h"

#define POW_TABLE_SIZE (1<<11)
#define POW_TABLE_OFFSET 3
#define OPT_SIZE ((1<<15) + 3000)

typedef struct NellyMoserEncodeContext {
    AVCodecContext  *avctx;
    int             last_frame;
    int             bufsel;
    int             have_saved;
    DSPContext      dsp;
    FFTContext      mdct_ctx;
    DECLARE_ALIGNED(16, float, mdct_out)[NELLY_SAMPLES];
    DECLARE_ALIGNED(16, float, in_buff)[NELLY_SAMPLES];
    DECLARE_ALIGNED(16, float, buf)[2][3 * NELLY_BUF_LEN];     ///< sample buffer
    float           (*opt )[NELLY_BANDS];
    uint8_t         (*path)[NELLY_BANDS];
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

static void apply_mdct(NellyMoserEncodeContext *s)
{
    memcpy(s->in_buff, s->buf[s->bufsel], NELLY_BUF_LEN * sizeof(float));
    s->dsp.vector_fmul(s->in_buff, ff_sine_128, NELLY_BUF_LEN);
    s->dsp.vector_fmul_reverse(s->in_buff + NELLY_BUF_LEN, s->buf[s->bufsel] + NELLY_BUF_LEN, ff_sine_128,
                               NELLY_BUF_LEN);
    ff_mdct_calc(&s->mdct_ctx, s->mdct_out, s->in_buff);

    s->dsp.vector_fmul(s->buf[s->bufsel] + NELLY_BUF_LEN, ff_sine_128, NELLY_BUF_LEN);
    s->dsp.vector_fmul_reverse(s->buf[s->bufsel] + 2 * NELLY_BUF_LEN, s->buf[1 - s->bufsel], ff_sine_128,
                               NELLY_BUF_LEN);
    ff_mdct_calc(&s->mdct_ctx, s->mdct_out + NELLY_BUF_LEN, s->buf[s->bufsel] + NELLY_BUF_LEN);
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    NellyMoserEncodeContext *s = avctx->priv_data;
    int i;

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Nellymoser supports only 1 channel\n");
        return -1;
    }

    if (avctx->sample_rate != 8000 && avctx->sample_rate != 16000 &&
        avctx->sample_rate != 11025 &&
        avctx->sample_rate != 22050 && avctx->sample_rate != 44100 &&
        avctx->strict_std_compliance >= FF_COMPLIANCE_NORMAL) {
        av_log(avctx, AV_LOG_ERROR, "Nellymoser works only with 8000, 16000, 11025, 22050 and 44100 sample rate\n");
        return -1;
    }

    avctx->frame_size = NELLY_SAMPLES;
    s->avctx = avctx;
    ff_mdct_init(&s->mdct_ctx, 8, 0, 1.0);
    dsputil_init(&s->dsp, avctx);

    /* Generate overlap window */
    ff_sine_window_init(ff_sine_128, 128);
    for (i = 0; i < POW_TABLE_SIZE; i++)
        pow_table[i] = -pow(2, -i / 2048.0 - 3.0 + POW_TABLE_OFFSET);

    if (s->avctx->trellis) {
        s->opt  = av_malloc(NELLY_BANDS * OPT_SIZE * sizeof(float  ));
        s->path = av_malloc(NELLY_BANDS * OPT_SIZE * sizeof(uint8_t));
    }

    return 0;
}

static av_cold int encode_end(AVCodecContext *avctx)
{
    NellyMoserEncodeContext *s = avctx->priv_data;

    ff_mdct_end(&s->mdct_ctx);

    if (s->avctx->trellis) {
        av_free(s->opt);
        av_free(s->path);
    }

    return 0;
}

#define find_best(val, table, LUT, LUT_add, LUT_size) \
    best_idx = \
        LUT[av_clip ((lrintf(val) >> 8) + LUT_add, 0, LUT_size - 1)]; \
    if (fabs(val - table[best_idx]) > fabs(val - table[best_idx + 1])) \
        best_idx++;

static void get_exponent_greedy(NellyMoserEncodeContext *s, float *cand, int *idx_table)
{
    int band, best_idx, power_idx = 0;
    float power_candidate;

    //base exponent
    find_best(cand[0], ff_nelly_init_table, sf_lut, -20, 96);
    idx_table[0] = best_idx;
    power_idx = ff_nelly_init_table[best_idx];

    for (band = 1; band < NELLY_BANDS; band++) {
        power_candidate = cand[band] - power_idx;
        find_best(power_candidate, ff_nelly_delta_table, sf_delta_lut, 37, 78);
        idx_table[band] = best_idx;
        power_idx += ff_nelly_delta_table[best_idx];
    }
}

static inline float distance(float x, float y, int band)
{
    //return pow(fabs(x-y), 2.0);
    float tmp = x - y;
    return tmp * tmp;
}

static void get_exponent_dynamic(NellyMoserEncodeContext *s, float *cand, int *idx_table)
{
    int i, j, band, best_idx;
    float power_candidate, best_val;

    float  (*opt )[NELLY_BANDS] = s->opt ;
    uint8_t(*path)[NELLY_BANDS] = s->path;

    for (i = 0; i < NELLY_BANDS * OPT_SIZE; i++) {
        opt[0][i] = INFINITY;
    }

    for (i = 0; i < 64; i++) {
        opt[0][ff_nelly_init_table[i]] = distance(cand[0], ff_nelly_init_table[i], 0);
        path[0][ff_nelly_init_table[i]] = i;
    }

    for (band = 1; band < NELLY_BANDS; band++) {
        int q, c = 0;
        float tmp;
        int idx_min, idx_max, idx;
        power_candidate = cand[band];
        for (q = 1000; !c && q < OPT_SIZE; q <<= 2) {
            idx_min = FFMAX(0, cand[band] - q);
            idx_max = FFMIN(OPT_SIZE, cand[band - 1] + q);
            for (i = FFMAX(0, cand[band - 1] - q); i < FFMIN(OPT_SIZE, cand[band - 1] + q); i++) {
                if ( isinf(opt[band - 1][i]) )
                    continue;
                for (j = 0; j < 32; j++) {
                    idx = i + ff_nelly_delta_table[j];
                    if (idx > idx_max)
                        break;
                    if (idx >= idx_min) {
                        tmp = opt[band - 1][i] + distance(idx, power_candidate, band);
                        if (opt[band][idx] > tmp) {
                            opt[band][idx] = tmp;
                            path[band][idx] = j;
                            c = 1;
                        }
                    }
                }
            }
        }
        assert(c); //FIXME
    }

    best_val = INFINITY;
    best_idx = -1;
    band = NELLY_BANDS - 1;
    for (i = 0; i < OPT_SIZE; i++) {
        if (best_val > opt[band][i]) {
            best_val = opt[band][i];
            best_idx = i;
        }
    }
    for (band = NELLY_BANDS - 1; band >= 0; band--) {
        idx_table[band] = path[band][best_idx];
        if (band) {
            best_idx -= ff_nelly_delta_table[path[band][best_idx]];
        }
    }
}

/**
 * Encode NELLY_SAMPLES samples. It assumes, that samples contains 3 * NELLY_BUF_LEN values
 *  @param s               encoder context
 *  @param output          output buffer
 *  @param output_size     size of output buffer
 */
static void encode_block(NellyMoserEncodeContext *s, unsigned char *output, int output_size)
{
    PutBitContext pb;
    int i, j, band, block, best_idx, power_idx = 0;
    float power_val, coeff, coeff_sum;
    float pows[NELLY_FILL_LEN];
    int bits[NELLY_BUF_LEN], idx_table[NELLY_BANDS];
    float cand[NELLY_BANDS];

    apply_mdct(s);

    init_put_bits(&pb, output, output_size * 8);

    i = 0;
    for (band = 0; band < NELLY_BANDS; band++) {
        coeff_sum = 0;
        for (j = 0; j < ff_nelly_band_sizes_table[band]; i++, j++) {
            coeff_sum += s->mdct_out[i                ] * s->mdct_out[i                ]
                       + s->mdct_out[i + NELLY_BUF_LEN] * s->mdct_out[i + NELLY_BUF_LEN];
        }
        cand[band] =
            log(FFMAX(1.0, coeff_sum / (ff_nelly_band_sizes_table[band] << 7))) * 1024.0 / M_LN2;
    }

    if (s->avctx->trellis) {
        get_exponent_dynamic(s, cand, idx_table);
    } else {
        get_exponent_greedy(s, cand, idx_table);
    }

    i = 0;
    for (band = 0; band < NELLY_BANDS; band++) {
        if (band) {
            power_idx += ff_nelly_delta_table[idx_table[band]];
            put_bits(&pb, 5, idx_table[band]);
        } else {
            power_idx = ff_nelly_init_table[idx_table[0]];
            put_bits(&pb, 6, idx_table[0]);
        }
        power_val = pow_table[power_idx & 0x7FF] / (1 << ((power_idx >> 11) + POW_TABLE_OFFSET));
        for (j = 0; j < ff_nelly_band_sizes_table[band]; i++, j++) {
            s->mdct_out[i] *= power_val;
            s->mdct_out[i + NELLY_BUF_LEN] *= power_val;
            pows[i] = power_idx;
        }
    }

    ff_nelly_get_sample_bits(pows, bits);

    for (block = 0; block < 2; block++) {
        for (i = 0; i < NELLY_FILL_LEN; i++) {
            if (bits[i] > 0) {
                const float *table = ff_nelly_dequantization_table + (1 << bits[i]) - 1;
                coeff = s->mdct_out[block * NELLY_BUF_LEN + i];
                best_idx =
                    quant_lut[av_clip (
                            coeff * quant_lut_mul[bits[i]] + quant_lut_add[bits[i]],
                            quant_lut_offset[bits[i]],
                            quant_lut_offset[bits[i]+1] - 1
                            )];
                if (fabs(coeff - table[best_idx]) > fabs(coeff - table[best_idx + 1]))
                    best_idx++;

                put_bits(&pb, bits[i], best_idx);
            }
        }
        if (!block)
            put_bits(&pb, NELLY_HEADER_BITS + NELLY_DETAIL_BITS - put_bits_count(&pb), 0);
    }

    flush_put_bits(&pb);
}

static int encode_frame(AVCodecContext *avctx, uint8_t *frame, int buf_size, void *data)
{
    NellyMoserEncodeContext *s = avctx->priv_data;
    const int16_t *samples = data;
    int i;

    if (s->last_frame)
        return 0;

    if (data) {
        for (i = 0; i < avctx->frame_size; i++) {
            s->buf[s->bufsel][i] = samples[i];
        }
        for (; i < NELLY_SAMPLES; i++) {
            s->buf[s->bufsel][i] = 0;
        }
        s->bufsel = 1 - s->bufsel;
        if (!s->have_saved) {
            s->have_saved = 1;
            return 0;
        }
    } else {
        memset(s->buf[s->bufsel], 0, sizeof(s->buf[0][0]) * NELLY_BUF_LEN);
        s->bufsel = 1 - s->bufsel;
        s->last_frame = 1;
    }

    if (s->have_saved) {
        encode_block(s, frame, buf_size);
        return NELLY_BLOCK_LEN;
    }
    return 0;
}

AVCodec nellymoser_encoder = {
    .name = "nellymoser",
    .type = AVMEDIA_TYPE_AUDIO,
    .id = CODEC_ID_NELLYMOSER,
    .priv_data_size = sizeof(NellyMoserEncodeContext),
    .init = encode_init,
    .encode = encode_frame,
    .close = encode_end,
    .capabilities = CODEC_CAP_SMALL_LAST_FRAME | CODEC_CAP_DELAY,
    .long_name = NULL_IF_CONFIG_SMALL("Nellymoser Asao"),
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
};
