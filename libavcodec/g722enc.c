/*
 * Copyright (c) CMU 1993 Computer Science, Speech Group
 *                        Chengxiang Lu and Alex Hauptmann
 * Copyright (c) 2005 Steve Underwood <steveu at coppice.org>
 * Copyright (c) 2009 Kenan Gillet
 * Copyright (c) 2010 Martin Storsjo
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

/**
 * @file
 * G.722 ADPCM audio encoder
 */

#include "avcodec.h"
#include "internal.h"
#include "g722.h"
#include "libavutil/common.h"

#define FREEZE_INTERVAL 128

/* This is an arbitrary value. Allowing insanely large values leads to strange
   problems, so we limit it to a reasonable value */
#define MAX_FRAME_SIZE 32768

/* We clip the value of avctx->trellis to prevent data type overflows and
   undefined behavior. Using larger values is insanely slow anyway. */
#define MIN_TRELLIS 0
#define MAX_TRELLIS 16

static av_cold int g722_encode_close(AVCodecContext *avctx)
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

static av_cold int g722_encode_init(AVCodecContext * avctx)
{
    G722Context *c = avctx->priv_data;
    int ret;

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono tracks are allowed.\n");
        return AVERROR_INVALIDDATA;
    }

    c->band[0].scale_factor = 8;
    c->band[1].scale_factor = 2;
    c->prev_samples_pos = 22;

    if (avctx->trellis) {
        int frontier = 1 << avctx->trellis;
        int max_paths = frontier * FREEZE_INTERVAL;
        int i;
        for (i = 0; i < 2; i++) {
            c->paths[i] = av_mallocz(max_paths * sizeof(**c->paths));
            c->node_buf[i] = av_mallocz(2 * frontier * sizeof(**c->node_buf));
            c->nodep_buf[i] = av_mallocz(2 * frontier * sizeof(**c->nodep_buf));
            if (!c->paths[i] || !c->node_buf[i] || !c->nodep_buf[i]) {
                ret = AVERROR(ENOMEM);
                goto error;
            }
        }
    }

    if (avctx->frame_size) {
        /* validate frame size */
        if (avctx->frame_size & 1 || avctx->frame_size > MAX_FRAME_SIZE) {
            int new_frame_size;

            if (avctx->frame_size == 1)
                new_frame_size = 2;
            else if (avctx->frame_size > MAX_FRAME_SIZE)
                new_frame_size = MAX_FRAME_SIZE;
            else
                new_frame_size = avctx->frame_size - 1;

            av_log(avctx, AV_LOG_WARNING, "Requested frame size is not "
                   "allowed. Using %d instead of %d\n", new_frame_size,
                   avctx->frame_size);
            avctx->frame_size = new_frame_size;
        }
    } else {
        /* This is arbitrary. We use 320 because it's 20ms @ 16kHz, which is
           a common packet size for VoIP applications */
        avctx->frame_size = 320;
    }
    avctx->initial_padding = 22;

    if (avctx->trellis) {
        /* validate trellis */
        if (avctx->trellis < MIN_TRELLIS || avctx->trellis > MAX_TRELLIS) {
            int new_trellis = av_clip(avctx->trellis, MIN_TRELLIS, MAX_TRELLIS);
            av_log(avctx, AV_LOG_WARNING, "Requested trellis value is not "
                   "allowed. Using %d instead of %d\n", new_trellis,
                   avctx->trellis);
            avctx->trellis = new_trellis;
        }
    }

    ff_g722dsp_init(&c->dsp);

    return 0;
error:
    g722_encode_close(avctx);
    return ret;
}

static const int16_t low_quant[33] = {
      35,   72,  110,  150,  190,  233,  276,  323,
     370,  422,  473,  530,  587,  650,  714,  786,
     858,  940, 1023, 1121, 1219, 1339, 1458, 1612,
    1765, 1980, 2195, 2557, 2919
};

static inline void filter_samples(G722Context *c, const int16_t *samples,
                                  int *xlow, int *xhigh)
{
    int xout[2];
    c->prev_samples[c->prev_samples_pos++] = samples[0];
    c->prev_samples[c->prev_samples_pos++] = samples[1];
    c->dsp.apply_qmf(c->prev_samples + c->prev_samples_pos - 24, xout);
    *xlow  = xout[0] + xout[1] >> 14;
    *xhigh = xout[0] - xout[1] >> 14;
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

static void g722_encode_trellis(G722Context *c, int trellis,
                                uint8_t *dst, int nb_samples,
                                const int16_t *samples)
{
    int i, j, k;
    int frontier = 1 << trellis;
    struct TrellisNode **nodes[2];
    struct TrellisNode **nodes_next[2];
    int pathn[2] = {0, 0}, froze = -1;
    struct TrellisPath *p[2];

    for (i = 0; i < 2; i++) {
        nodes[i] = c->nodep_buf[i];
        nodes_next[i] = c->nodep_buf[i] + frontier;
        memset(c->nodep_buf[i], 0, 2 * frontier * sizeof(*c->nodep_buf[i]));
        nodes[i][0] = c->node_buf[i] + frontier;
        nodes[i][0]->ssd = 0;
        nodes[i][0]->path = 0;
        nodes[i][0]->state = c->band[i];
    }

    for (i = 0; i < nb_samples >> 1; i++) {
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
             * small steps that don't change k >> 2 is useless, the original
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

                decoded = av_clip_intp2((cur_node->state.scale_factor *
                                  ff_g722_low_inv_quant6[k] >> 10)
                                + cur_node->state.s_predictor, 14);
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
                STORE_NODE(0, ff_g722_update_low_predictor(&node->state, k >> 2), k);
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
                        ff_g722_high_inv_quant[ihigh] >> 10;
                decoded = av_clip_intp2(dhigh + cur_node->state.s_predictor, 14);
                dec_diff = xhigh - decoded;

                STORE_NODE(1, ff_g722_update_high_predictor(&node->state, dhigh, ihigh), ihigh);
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
}

static av_always_inline void encode_byte(G722Context *c, uint8_t *dst,
                                         const int16_t *samples)
{
    int xlow, xhigh, ilow, ihigh;
    filter_samples(c, samples, &xlow, &xhigh);
    ihigh = encode_high(&c->band[1], xhigh);
    ilow  = encode_low (&c->band[0], xlow);
    ff_g722_update_high_predictor(&c->band[1], c->band[1].scale_factor *
                                ff_g722_high_inv_quant[ihigh] >> 10, ihigh);
    ff_g722_update_low_predictor(&c->band[0], ilow >> 2);
    *dst = ihigh << 6 | ilow;
}

static void g722_encode_no_trellis(G722Context *c,
                                   uint8_t *dst, int nb_samples,
                                   const int16_t *samples)
{
    int i;
    for (i = 0; i < nb_samples; i += 2)
        encode_byte(c, dst++, &samples[i]);
}

static int g722_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                             const AVFrame *frame, int *got_packet_ptr)
{
    G722Context *c = avctx->priv_data;
    const int16_t *samples = (const int16_t *)frame->data[0];
    int nb_samples, out_size, ret;

    out_size = (frame->nb_samples + 1) / 2;
    if ((ret = ff_alloc_packet(avpkt, out_size))) {
        av_log(avctx, AV_LOG_ERROR, "Error getting output packet\n");
        return ret;
    }

    nb_samples = frame->nb_samples - (frame->nb_samples & 1);

    if (avctx->trellis)
        g722_encode_trellis(c, avctx->trellis, avpkt->data, nb_samples, samples);
    else
        g722_encode_no_trellis(c, avpkt->data, nb_samples, samples);

    /* handle last frame with odd frame_size */
    if (nb_samples < frame->nb_samples) {
        int16_t last_samples[2] = { samples[nb_samples], samples[nb_samples] };
        encode_byte(c, &avpkt->data[nb_samples >> 1], last_samples);
    }

    if (frame->pts != AV_NOPTS_VALUE)
        avpkt->pts = frame->pts - ff_samples_to_time_base(avctx, avctx->initial_padding);
    *got_packet_ptr = 1;
    return 0;
}

AVCodec ff_adpcm_g722_encoder = {
    .name           = "g722",
    .long_name      = NULL_IF_CONFIG_SMALL("G.722 ADPCM"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_ADPCM_G722,
    .priv_data_size = sizeof(G722Context),
    .init           = g722_encode_init,
    .close          = g722_encode_close,
    .encode2        = g722_encode_frame,
    .capabilities   = AV_CODEC_CAP_SMALL_LAST_FRAME,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
};
