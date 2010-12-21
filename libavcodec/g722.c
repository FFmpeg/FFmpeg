/*
 * G.722 ADPCM audio encoder/decoder
 *
 * Copyright (c) CMU 1993 Computer Science, Speech Group
 *                        Chengxiang Lu and Alex Hauptmann
 * Copyright (c) 2005 Steve Underwood <steveu at coppice.org>
 * Copyright (c) 2009 Kenan Gillet
 * Copyright (c) 2010 Martin Storsjo
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
 *
 * G.722 ADPCM audio codec
 *
 * This G.722 decoder is a bit-exact implementation of the ITU G.722
 * specification for all three specified bitrates - 64000bps, 56000bps
 * and 48000bps. It passes the ITU tests.
 *
 * @note For the 56000bps and 48000bps bitrates, the lowest 1 or 2 bits
 *       respectively of each byte are ignored.
 */

#include "avcodec.h"
#include "mathops.h"
#include "get_bits.h"

#define PREV_SAMPLES_BUF_SIZE 1024

#define FREEZE_INTERVAL 128

typedef struct {
    int16_t prev_samples[PREV_SAMPLES_BUF_SIZE]; ///< memory of past decoded samples
    int     prev_samples_pos;        ///< the number of values in prev_samples

    /**
     * The band[0] and band[1] correspond respectively to the lower band and higher band.
     */
    struct G722Band {
        int16_t s_predictor;         ///< predictor output value
        int32_t s_zero;              ///< previous output signal from zero predictor
        int8_t  part_reconst_mem[2]; ///< signs of previous partially reconstructed signals
        int16_t prev_qtzd_reconst;   ///< previous quantized reconstructed signal (internal value, using low_inv_quant4)
        int16_t pole_mem[2];         ///< second-order pole section coefficient buffer
        int32_t diff_mem[6];         ///< quantizer difference signal memory
        int16_t zero_mem[6];         ///< Seventh-order zero section coefficient buffer
        int16_t log_factor;          ///< delayed 2-logarithmic quantizer factor
        int16_t scale_factor;        ///< delayed quantizer scale factor
    } band[2];

    struct TrellisNode {
        struct G722Band state;
        uint32_t ssd;
        int path;
    } *node_buf[2], **nodep_buf[2];

    struct TrellisPath {
        int value;
        int prev;
    } *paths[2];
} G722Context;


static const int8_t sign_lookup[2] = { -1, 1 };

static const int16_t inv_log2_table[32] = {
    2048, 2093, 2139, 2186, 2233, 2282, 2332, 2383,
    2435, 2489, 2543, 2599, 2656, 2714, 2774, 2834,
    2896, 2960, 3025, 3091, 3158, 3228, 3298, 3371,
    3444, 3520, 3597, 3676, 3756, 3838, 3922, 4008
};
static const int16_t high_log_factor_step[2] = { 798, -214 };
static const int16_t high_inv_quant[4] = { -926, -202, 926, 202 };
/**
 * low_log_factor_step[index] == wl[rl42[index]]
 */
static const int16_t low_log_factor_step[16] = {
     -60, 3042, 1198, 538, 334, 172,  58, -30,
    3042, 1198,  538, 334, 172,  58, -30, -60
};
static const int16_t low_inv_quant4[16] = {
       0, -2557, -1612, -1121,  -786,  -530,  -323,  -150,
    2557,  1612,  1121,   786,   530,   323,   150,     0
};
static const int16_t low_inv_quant6[64] = {
     -17,   -17,   -17,   -17, -3101, -2738, -2376, -2088,
   -1873, -1689, -1535, -1399, -1279, -1170, -1072,  -982,
    -899,  -822,  -750,  -682,  -618,  -558,  -501,  -447,
    -396,  -347,  -300,  -254,  -211,  -170,  -130,   -91,
    3101,  2738,  2376,  2088,  1873,  1689,  1535,  1399,
    1279,  1170,  1072,   982,   899,   822,   750,   682,
     618,   558,   501,   447,   396,   347,   300,   254,
     211,   170,   130,    91,    54,    17,   -54,   -17
};

/**
 * quadrature mirror filter (QMF) coefficients
 *
 * ITU-T G.722 Table 11
 */
static const int16_t qmf_coeffs[12] = {
    3, -11, 12, 32, -210, 951, 3876, -805, 362, -156, 53, -11,
};


/**
 * adaptive predictor
 *
 * @param cur_diff the dequantized and scaled delta calculated from the
 *                 current codeword
 */
static void do_adaptive_prediction(struct G722Band *band, const int cur_diff)
{
    int sg[2], limit, i, cur_qtzd_reconst;

    const int cur_part_reconst = band->s_zero + cur_diff < 0;

    sg[0] = sign_lookup[cur_part_reconst != band->part_reconst_mem[0]];
    sg[1] = sign_lookup[cur_part_reconst == band->part_reconst_mem[1]];
    band->part_reconst_mem[1] = band->part_reconst_mem[0];
    band->part_reconst_mem[0] = cur_part_reconst;

    band->pole_mem[1] = av_clip((sg[0] * av_clip(band->pole_mem[0], -8191, 8191) >> 5) +
                                (sg[1] << 7) + (band->pole_mem[1] * 127 >> 7), -12288, 12288);

    limit = 15360 - band->pole_mem[1];
    band->pole_mem[0] = av_clip(-192 * sg[0] + (band->pole_mem[0] * 255 >> 8), -limit, limit);


    if (cur_diff) {
        for (i = 0; i < 6; i++)
            band->zero_mem[i] = ((band->zero_mem[i]*255) >> 8) +
                                ((band->diff_mem[i]^cur_diff) < 0 ? -128 : 128);
    } else
        for (i = 0; i < 6; i++)
            band->zero_mem[i] = (band->zero_mem[i]*255) >> 8;

    for (i = 5; i > 0; i--)
        band->diff_mem[i] = band->diff_mem[i-1];
    band->diff_mem[0] = av_clip_int16(cur_diff << 1);

    band->s_zero = 0;
    for (i = 5; i >= 0; i--)
        band->s_zero += (band->zero_mem[i]*band->diff_mem[i]) >> 15;


    cur_qtzd_reconst = av_clip_int16((band->s_predictor + cur_diff) << 1);
    band->s_predictor = av_clip_int16(band->s_zero +
                                      (band->pole_mem[0] * cur_qtzd_reconst >> 15) +
                                      (band->pole_mem[1] * band->prev_qtzd_reconst >> 15));
    band->prev_qtzd_reconst = cur_qtzd_reconst;
}

static int inline linear_scale_factor(const int log_factor)
{
    const int wd1 = inv_log2_table[(log_factor >> 6) & 31];
    const int shift = log_factor >> 11;
    return shift < 0 ? wd1 >> -shift : wd1 << shift;
}

static void update_low_predictor(struct G722Band *band, const int ilow)
{
    do_adaptive_prediction(band,
                           band->scale_factor * low_inv_quant4[ilow] >> 10);

    // quantizer adaptation
    band->log_factor   = av_clip((band->log_factor * 127 >> 7) +
                                 low_log_factor_step[ilow], 0, 18432);
    band->scale_factor = linear_scale_factor(band->log_factor - (8 << 11));
}

static void update_high_predictor(struct G722Band *band, const int dhigh,
                                  const int ihigh)
{
    do_adaptive_prediction(band, dhigh);

    // quantizer adaptation
    band->log_factor   = av_clip((band->log_factor * 127 >> 7) +
                                 high_log_factor_step[ihigh&1], 0, 22528);
    band->scale_factor = linear_scale_factor(band->log_factor - (10 << 11));
}

static void apply_qmf(const int16_t *prev_samples, int *xout1, int *xout2)
{
    int i;

    *xout1 = 0;
    *xout2 = 0;
    for (i = 0; i < 12; i++) {
        MAC16(*xout2, prev_samples[2*i  ], qmf_coeffs[i   ]);
        MAC16(*xout1, prev_samples[2*i+1], qmf_coeffs[11-i]);
    }
}

static av_cold int g722_init(AVCodecContext * avctx)
{
    G722Context *c = avctx->priv_data;

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono tracks are allowed.\n");
        return AVERROR_INVALIDDATA;
    }
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    switch (avctx->bits_per_coded_sample) {
    case 8:
    case 7:
    case 6:
        break;
    default:
        av_log(avctx, AV_LOG_WARNING, "Unsupported bits_per_coded_sample [%d], "
                                      "assuming 8\n",
                                      avctx->bits_per_coded_sample);
    case 0:
        avctx->bits_per_coded_sample = 8;
        break;
    }

    c->band[0].scale_factor = 8;
    c->band[1].scale_factor = 2;
    c->prev_samples_pos = 22;

    if (avctx->lowres)
        avctx->sample_rate /= 2;

    if (avctx->trellis) {
        int frontier = 1 << avctx->trellis;
        int max_paths = frontier * FREEZE_INTERVAL;
        int i;
        for (i = 0; i < 2; i++) {
            c->paths[i] = av_mallocz(max_paths * sizeof(**c->paths));
            c->node_buf[i] = av_mallocz(2 * frontier * sizeof(**c->node_buf));
            c->nodep_buf[i] = av_mallocz(2 * frontier * sizeof(**c->nodep_buf));
        }
    }

    return 0;
}

static av_cold int g722_close(AVCodecContext *avctx)
{
    G722Context *c = avctx->priv_data;
    int i;
    for (i = 0; i < 2; i++) {
        av_freep(&c->paths[i]);
        av_freep(&c->node_buf[i]);
        av_freep(&c->nodep_buf[i]);
    }
    return 0;
}

#if CONFIG_ADPCM_G722_DECODER
static const int16_t low_inv_quant5[32] = {
     -35,   -35, -2919, -2195, -1765, -1458, -1219, -1023,
    -858,  -714,  -587,  -473,  -370,  -276,  -190,  -110,
    2919,  2195,  1765,  1458,  1219,  1023,   858,   714,
     587,   473,   370,   276,   190,   110,    35,   -35
};

static const int16_t *low_inv_quants[3] = { low_inv_quant6, low_inv_quant5,
                                 low_inv_quant4 };

static int g722_decode_frame(AVCodecContext *avctx, void *data,
                             int *data_size, AVPacket *avpkt)
{
    G722Context *c = avctx->priv_data;
    int16_t *out_buf = data;
    int j, out_len = 0;
    const int skip = 8 - avctx->bits_per_coded_sample;
    const int16_t *quantizer_table = low_inv_quants[skip];
    GetBitContext gb;

    init_get_bits(&gb, avpkt->data, avpkt->size * 8);

    for (j = 0; j < avpkt->size; j++) {
        int ilow, ihigh, rlow;

        ihigh = get_bits(&gb, 2);
        ilow = get_bits(&gb, 6 - skip);
        skip_bits(&gb, skip);

        rlow = av_clip((c->band[0].scale_factor * quantizer_table[ilow] >> 10)
                      + c->band[0].s_predictor, -16384, 16383);

        update_low_predictor(&c->band[0], ilow >> (2 - skip));

        if (!avctx->lowres) {
            const int dhigh = c->band[1].scale_factor *
                              high_inv_quant[ihigh] >> 10;
            const int rhigh = av_clip(dhigh + c->band[1].s_predictor,
                                      -16384, 16383);
            int xout1, xout2;

            update_high_predictor(&c->band[1], dhigh, ihigh);

            c->prev_samples[c->prev_samples_pos++] = rlow + rhigh;
            c->prev_samples[c->prev_samples_pos++] = rlow - rhigh;
            apply_qmf(c->prev_samples + c->prev_samples_pos - 24,
                      &xout1, &xout2);
            out_buf[out_len++] = av_clip_int16(xout1 >> 12);
            out_buf[out_len++] = av_clip_int16(xout2 >> 12);
            if (c->prev_samples_pos >= PREV_SAMPLES_BUF_SIZE) {
                memmove(c->prev_samples,
                        c->prev_samples + c->prev_samples_pos - 22,
                        22 * sizeof(c->prev_samples[0]));
                c->prev_samples_pos = 22;
            }
        } else
            out_buf[out_len++] = rlow;
    }
    *data_size = out_len << 1;
    return avpkt->size;
}

AVCodec adpcm_g722_decoder = {
    .name           = "g722",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_ADPCM_G722,
    .priv_data_size = sizeof(G722Context),
    .init           = g722_init,
    .decode         = g722_decode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("G.722 ADPCM"),
    .max_lowres     = 1,
};
#endif

#if CONFIG_ADPCM_G722_ENCODER
static const int16_t low_quant[33] = {
      35,   72,  110,  150,  190,  233,  276,  323,
     370,  422,  473,  530,  587,  650,  714,  786,
     858,  940, 1023, 1121, 1219, 1339, 1458, 1612,
    1765, 1980, 2195, 2557, 2919
};

static inline void filter_samples(G722Context *c, const int16_t *samples,
                                  int *xlow, int *xhigh)
{
    int xout1, xout2;
    c->prev_samples[c->prev_samples_pos++] = samples[0];
    c->prev_samples[c->prev_samples_pos++] = samples[1];
    apply_qmf(c->prev_samples + c->prev_samples_pos - 24, &xout1, &xout2);
    *xlow  = xout1 + xout2 >> 13;
    *xhigh = xout1 - xout2 >> 13;
    if (c->prev_samples_pos >= PREV_SAMPLES_BUF_SIZE) {
        memmove(c->prev_samples,
                c->prev_samples + c->prev_samples_pos - 22,
                22 * sizeof(c->prev_samples[0]));
        c->prev_samples_pos = 22;
    }
}

static inline int encode_high(const struct G722Band *state, int xhigh)
{
    int diff = av_clip_int16(xhigh - state->s_predictor);
    int pred = 141 * state->scale_factor >> 8;
           /* = diff >= 0 ? (diff < pred) + 2 : diff >= -pred */
    return ((diff ^ (diff >> (sizeof(diff)*8-1))) < pred) + 2*(diff >= 0);
}

static inline int encode_low(const struct G722Band* state, int xlow)
{
    int diff  = av_clip_int16(xlow - state->s_predictor);
           /* = diff >= 0 ? diff : -(diff + 1) */
    int limit = diff ^ (diff >> (sizeof(diff)*8-1));
    int i = 0;
    limit = limit + 1 << 10;
    if (limit > low_quant[8] * state->scale_factor)
        i = 9;
    while (i < 29 && limit > low_quant[i] * state->scale_factor)
        i++;
    return (diff < 0 ? (i < 2 ? 63 : 33) : 61) - i;
}

static int g722_encode_trellis(AVCodecContext *avctx,
                               uint8_t *dst, int buf_size, void *data)
{
    G722Context *c = avctx->priv_data;
    const int16_t *samples = data;
    int i, j, k;
    int frontier = 1 << avctx->trellis;
    struct TrellisNode **nodes[2];
    struct TrellisNode **nodes_next[2];
    int pathn[2] = {0, 0}, froze = -1;
    struct TrellisPath *p[2];

    for (i = 0; i < 2; i++) {
        nodes[i] = c->nodep_buf[i];
        nodes_next[i] = c->nodep_buf[i] + frontier;
        memset(c->nodep_buf[i], 0, 2 * frontier * sizeof(*c->nodep_buf));
        nodes[i][0] = c->node_buf[i] + frontier;
        nodes[i][0]->ssd = 0;
        nodes[i][0]->path = 0;
        nodes[i][0]->state = c->band[i];
    }

    for (i = 0; i < buf_size >> 1; i++) {
        int xlow, xhigh;
        struct TrellisNode *next[2];
        int heap_pos[2] = {0, 0};

        for (j = 0; j < 2; j++) {
            next[j] = c->node_buf[j] + frontier*(i & 1);
            memset(nodes_next[j], 0, frontier * sizeof(**nodes_next));
        }

        filter_samples(c, &samples[2*i], &xlow, &xhigh);

        for (j = 0; j < frontier && nodes[0][j]; j++) {
            /* Only k >> 2 affects the future adaptive state, therefore testing
             * small steps that don't change k >> 2 is useless, the orignal
             * value from encode_low is better than them. Since we step k
             * in steps of 4, make sure range is a multiple of 4, so that
             * we don't miss the original value from encode_low. */
            int range = j < frontier/2 ? 4 : 0;
            struct TrellisNode *cur_node = nodes[0][j];

            int ilow = encode_low(&cur_node->state, xlow);

            for (k = ilow - range; k <= ilow + range && k <= 63; k += 4) {
                int decoded, dec_diff, pos;
                uint32_t ssd;
                struct TrellisNode* node;

                if (k < 0)
                    continue;

                decoded = av_clip((cur_node->state.scale_factor *
                                  low_inv_quant6[k] >> 10)
                                + cur_node->state.s_predictor, -16384, 16383);
                dec_diff = xlow - decoded;

#define STORE_NODE(index, UPDATE, VALUE)\
                ssd = cur_node->ssd + dec_diff*dec_diff;\
                /* Check for wraparound. Using 64 bit ssd counters would \
                 * be simpler, but is slower on x86 32 bit. */\
                if (ssd < cur_node->ssd)\
                    continue;\
                if (heap_pos[index] < frontier) {\
                    pos = heap_pos[index]++;\
                    assert(pathn[index] < FREEZE_INTERVAL * frontier);\
                    node = nodes_next[index][pos] = next[index]++;\
                    node->path = pathn[index]++;\
                } else {\
                    /* Try to replace one of the leaf nodes with the new \
                     * one, but not always testing the same leaf position */\
                    pos = (frontier>>1) + (heap_pos[index] & ((frontier>>1) - 1));\
                    if (ssd >= nodes_next[index][pos]->ssd)\
                        continue;\
                    heap_pos[index]++;\
                    node = nodes_next[index][pos];\
                }\
                node->ssd = ssd;\
                node->state = cur_node->state;\
                UPDATE;\
                c->paths[index][node->path].value = VALUE;\
                c->paths[index][node->path].prev = cur_node->path;\
                /* Sift the newly inserted node up in the heap to restore \
                 * the heap property */\
                while (pos > 0) {\
                    int parent = (pos - 1) >> 1;\
                    if (nodes_next[index][parent]->ssd <= ssd)\
                        break;\
                    FFSWAP(struct TrellisNode*, nodes_next[index][parent],\
                                                nodes_next[index][pos]);\
                    pos = parent;\
                }
                STORE_NODE(0, update_low_predictor(&node->state, k >> 2), k);
            }
        }

        for (j = 0; j < frontier && nodes[1][j]; j++) {
            int ihigh;
            struct TrellisNode *cur_node = nodes[1][j];

            /* We don't try to get any initial guess for ihigh via
             * encode_high - since there's only 4 possible values, test
             * them all. Testing all of these gives a much, much larger
             * gain than testing a larger range around ilow. */
            for (ihigh = 0; ihigh < 4; ihigh++) {
                int dhigh, decoded, dec_diff, pos;
                uint32_t ssd;
                struct TrellisNode* node;

                dhigh = cur_node->state.scale_factor *
                        high_inv_quant[ihigh] >> 10;
                decoded = av_clip(dhigh + cur_node->state.s_predictor,
                                  -16384, 16383);
                dec_diff = xhigh - decoded;

                STORE_NODE(1, update_high_predictor(&node->state, dhigh, ihigh), ihigh);
            }
        }

        for (j = 0; j < 2; j++) {
            FFSWAP(struct TrellisNode**, nodes[j], nodes_next[j]);

            if (nodes[j][0]->ssd > (1 << 16)) {
                for (k = 1; k < frontier && nodes[j][k]; k++)
                    nodes[j][k]->ssd -= nodes[j][0]->ssd;
                nodes[j][0]->ssd = 0;
            }
        }

        if (i == froze + FREEZE_INTERVAL) {
            p[0] = &c->paths[0][nodes[0][0]->path];
            p[1] = &c->paths[1][nodes[1][0]->path];
            for (j = i; j > froze; j--) {
                dst[j] = p[1]->value << 6 | p[0]->value;
                p[0] = &c->paths[0][p[0]->prev];
                p[1] = &c->paths[1][p[1]->prev];
            }
            froze = i;
            pathn[0] = pathn[1] = 0;
            memset(nodes[0] + 1, 0, (frontier - 1)*sizeof(**nodes));
            memset(nodes[1] + 1, 0, (frontier - 1)*sizeof(**nodes));
        }
    }

    p[0] = &c->paths[0][nodes[0][0]->path];
    p[1] = &c->paths[1][nodes[1][0]->path];
    for (j = i; j > froze; j--) {
        dst[j] = p[1]->value << 6 | p[0]->value;
        p[0] = &c->paths[0][p[0]->prev];
        p[1] = &c->paths[1][p[1]->prev];
    }
    c->band[0] = nodes[0][0]->state;
    c->band[1] = nodes[1][0]->state;

    return i;
}

static int g722_encode_frame(AVCodecContext *avctx,
                             uint8_t *dst, int buf_size, void *data)
{
    G722Context *c = avctx->priv_data;
    const int16_t *samples = data;
    int i;

    if (avctx->trellis)
        return g722_encode_trellis(avctx, dst, buf_size, data);

    for (i = 0; i < buf_size >> 1; i++) {
        int xlow, xhigh, ihigh, ilow;
        filter_samples(c, &samples[2*i], &xlow, &xhigh);
        ihigh = encode_high(&c->band[1], xhigh);
        ilow  = encode_low(&c->band[0], xlow);
        update_high_predictor(&c->band[1], c->band[1].scale_factor *
                              high_inv_quant[ihigh] >> 10, ihigh);
        update_low_predictor(&c->band[0], ilow >> 2);
        *dst++ = ihigh << 6 | ilow;
    }
    return i;
}

AVCodec adpcm_g722_encoder = {
    .name           = "g722",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_ADPCM_G722,
    .priv_data_size = sizeof(G722Context),
    .init           = g722_init,
    .close          = g722_close,
    .encode         = g722_encode_frame,
    .long_name      = NULL_IF_CONFIG_SMALL("G.722 ADPCM"),
    .sample_fmts    = (enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
};
#endif

