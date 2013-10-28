/*
 * Copyright (c) 2001-2003 The ffmpeg Project
 *
 * first version by Francois Revol (revol@free.fr)
 * fringe ADPCM codecs (e.g., DK3, DK4, Westwood)
 *   by Mike Melanson (melanson@pcisys.net)
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

#include "avcodec.h"
#include "put_bits.h"
#include "bytestream.h"
#include "adpcm.h"
#include "adpcm_data.h"
#include "internal.h"

/**
 * @file
 * ADPCM encoders
 * See ADPCM decoder reference documents for codec information.
 */

typedef struct TrellisPath {
    int nibble;
    int prev;
} TrellisPath;

typedef struct TrellisNode {
    uint32_t ssd;
    int path;
    int sample1;
    int sample2;
    int step;
} TrellisNode;

typedef struct ADPCMEncodeContext {
    ADPCMChannelStatus status[6];
    TrellisPath *paths;
    TrellisNode *node_buf;
    TrellisNode **nodep_buf;
    uint8_t *trellis_hash;
} ADPCMEncodeContext;

#define FREEZE_INTERVAL 128

static av_cold int adpcm_encode_close(AVCodecContext *avctx);

static av_cold int adpcm_encode_init(AVCodecContext *avctx)
{
    ADPCMEncodeContext *s = avctx->priv_data;
    uint8_t *extradata;
    int i;
    int ret = AVERROR(ENOMEM);

    if (avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "only stereo or mono is supported\n");
        return AVERROR(EINVAL);
    }

    if (avctx->trellis && (unsigned)avctx->trellis > 16U) {
        av_log(avctx, AV_LOG_ERROR, "invalid trellis size\n");
        return AVERROR(EINVAL);
    }

    if (avctx->trellis) {
        int frontier  = 1 << avctx->trellis;
        int max_paths =  frontier * FREEZE_INTERVAL;
        FF_ALLOC_OR_GOTO(avctx, s->paths,
                         max_paths * sizeof(*s->paths), error);
        FF_ALLOC_OR_GOTO(avctx, s->node_buf,
                         2 * frontier * sizeof(*s->node_buf),  error);
        FF_ALLOC_OR_GOTO(avctx, s->nodep_buf,
                         2 * frontier * sizeof(*s->nodep_buf), error);
        FF_ALLOC_OR_GOTO(avctx, s->trellis_hash,
                         65536 * sizeof(*s->trellis_hash), error);
    }

    avctx->bits_per_coded_sample = av_get_bits_per_sample(avctx->codec->id);

    switch (avctx->codec->id) {
    case AV_CODEC_ID_ADPCM_IMA_WAV:
        /* each 16 bits sample gives one nibble
           and we have 4 bytes per channel overhead */
        avctx->frame_size = (BLKSIZE - 4 * avctx->channels) * 8 /
                            (4 * avctx->channels) + 1;
        /* seems frame_size isn't taken into account...
           have to buffer the samples :-( */
        avctx->block_align = BLKSIZE;
        avctx->bits_per_coded_sample = 4;
        break;
    case AV_CODEC_ID_ADPCM_IMA_QT:
        avctx->frame_size  = 64;
        avctx->block_align = 34 * avctx->channels;
        break;
    case AV_CODEC_ID_ADPCM_MS:
        /* each 16 bits sample gives one nibble
           and we have 7 bytes per channel overhead */
        avctx->frame_size = (BLKSIZE - 7 * avctx->channels) * 2 / avctx->channels + 2;
        avctx->bits_per_coded_sample = 4;
        avctx->block_align    = BLKSIZE;
        if (!(avctx->extradata = av_malloc(32 + FF_INPUT_BUFFER_PADDING_SIZE)))
            goto error;
        avctx->extradata_size = 32;
        extradata = avctx->extradata;
        bytestream_put_le16(&extradata, avctx->frame_size);
        bytestream_put_le16(&extradata, 7); /* wNumCoef */
        for (i = 0; i < 7; i++) {
            bytestream_put_le16(&extradata, ff_adpcm_AdaptCoeff1[i] * 4);
            bytestream_put_le16(&extradata, ff_adpcm_AdaptCoeff2[i] * 4);
        }
        break;
    case AV_CODEC_ID_ADPCM_YAMAHA:
        avctx->frame_size  = BLKSIZE * 2 / avctx->channels;
        avctx->block_align = BLKSIZE;
        break;
    case AV_CODEC_ID_ADPCM_SWF:
        if (avctx->sample_rate != 11025 &&
            avctx->sample_rate != 22050 &&
            avctx->sample_rate != 44100) {
            av_log(avctx, AV_LOG_ERROR, "Sample rate must be 11025, "
                   "22050 or 44100\n");
            ret = AVERROR(EINVAL);
            goto error;
        }
        avctx->frame_size = 512 * (avctx->sample_rate / 11025);
        break;
    default:
        ret = AVERROR(EINVAL);
        goto error;
    }

    return 0;
error:
    adpcm_encode_close(avctx);
    return ret;
}

static av_cold int adpcm_encode_close(AVCodecContext *avctx)
{
    ADPCMEncodeContext *s = avctx->priv_data;
    av_freep(&s->paths);
    av_freep(&s->node_buf);
    av_freep(&s->nodep_buf);
    av_freep(&s->trellis_hash);

    return 0;
}


static inline uint8_t adpcm_ima_compress_sample(ADPCMChannelStatus *c,
                                                int16_t sample)
{
    int delta  = sample - c->prev_sample;
    int nibble = FFMIN(7, abs(delta) * 4 /
                       ff_adpcm_step_table[c->step_index]) + (delta < 0) * 8;
    c->prev_sample += ((ff_adpcm_step_table[c->step_index] *
                        ff_adpcm_yamaha_difflookup[nibble]) / 8);
    c->prev_sample = av_clip_int16(c->prev_sample);
    c->step_index  = av_clip(c->step_index + ff_adpcm_index_table[nibble], 0, 88);
    return nibble;
}

static inline uint8_t adpcm_ima_qt_compress_sample(ADPCMChannelStatus *c,
                                                   int16_t sample)
{
    int delta  = sample - c->prev_sample;
    int diff, step = ff_adpcm_step_table[c->step_index];
    int nibble = 8*(delta < 0);

    delta= abs(delta);
    diff = delta + (step >> 3);

    if (delta >= step) {
        nibble |= 4;
        delta  -= step;
    }
    step >>= 1;
    if (delta >= step) {
        nibble |= 2;
        delta  -= step;
    }
    step >>= 1;
    if (delta >= step) {
        nibble |= 1;
        delta  -= step;
    }
    diff -= delta;

    if (nibble & 8)
        c->prev_sample -= diff;
    else
        c->prev_sample += diff;

    c->prev_sample = av_clip_int16(c->prev_sample);
    c->step_index  = av_clip(c->step_index + ff_adpcm_index_table[nibble], 0, 88);

    return nibble;
}

static inline uint8_t adpcm_ms_compress_sample(ADPCMChannelStatus *c,
                                               int16_t sample)
{
    int predictor, nibble, bias;

    predictor = (((c->sample1) * (c->coeff1)) +
                (( c->sample2) * (c->coeff2))) / 64;

    nibble = sample - predictor;
    if (nibble >= 0)
        bias =  c->idelta / 2;
    else
        bias = -c->idelta / 2;

    nibble = (nibble + bias) / c->idelta;
    nibble = av_clip(nibble, -8, 7) & 0x0F;

    predictor += ((nibble & 0x08) ? (nibble - 0x10) : nibble) * c->idelta;

    c->sample2 = c->sample1;
    c->sample1 = av_clip_int16(predictor);

    c->idelta = (ff_adpcm_AdaptationTable[nibble] * c->idelta) >> 8;
    if (c->idelta < 16)
        c->idelta = 16;

    return nibble;
}

static inline uint8_t adpcm_yamaha_compress_sample(ADPCMChannelStatus *c,
                                                   int16_t sample)
{
    int nibble, delta;

    if (!c->step) {
        c->predictor = 0;
        c->step      = 127;
    }

    delta = sample - c->predictor;

    nibble = FFMIN(7, abs(delta) * 4 / c->step) + (delta < 0) * 8;

    c->predictor += ((c->step * ff_adpcm_yamaha_difflookup[nibble]) / 8);
    c->predictor = av_clip_int16(c->predictor);
    c->step = (c->step * ff_adpcm_yamaha_indexscale[nibble]) >> 8;
    c->step = av_clip(c->step, 127, 24567);

    return nibble;
}

static void adpcm_compress_trellis(AVCodecContext *avctx,
                                   const int16_t *samples, uint8_t *dst,
                                   ADPCMChannelStatus *c, int n, int stride)
{
    //FIXME 6% faster if frontier is a compile-time constant
    ADPCMEncodeContext *s = avctx->priv_data;
    const int frontier = 1 << avctx->trellis;
    const int version  = avctx->codec->id;
    TrellisPath *paths       = s->paths, *p;
    TrellisNode *node_buf    = s->node_buf;
    TrellisNode **nodep_buf  = s->nodep_buf;
    TrellisNode **nodes      = nodep_buf; // nodes[] is always sorted by .ssd
    TrellisNode **nodes_next = nodep_buf + frontier;
    int pathn = 0, froze = -1, i, j, k, generation = 0;
    uint8_t *hash = s->trellis_hash;
    memset(hash, 0xff, 65536 * sizeof(*hash));

    memset(nodep_buf, 0, 2 * frontier * sizeof(*nodep_buf));
    nodes[0]          = node_buf + frontier;
    nodes[0]->ssd     = 0;
    nodes[0]->path    = 0;
    nodes[0]->step    = c->step_index;
    nodes[0]->sample1 = c->sample1;
    nodes[0]->sample2 = c->sample2;
    if (version == AV_CODEC_ID_ADPCM_IMA_WAV ||
        version == AV_CODEC_ID_ADPCM_IMA_QT  ||
        version == AV_CODEC_ID_ADPCM_SWF)
        nodes[0]->sample1 = c->prev_sample;
    if (version == AV_CODEC_ID_ADPCM_MS)
        nodes[0]->step = c->idelta;
    if (version == AV_CODEC_ID_ADPCM_YAMAHA) {
        if (c->step == 0) {
            nodes[0]->step    = 127;
            nodes[0]->sample1 = 0;
        } else {
            nodes[0]->step    = c->step;
            nodes[0]->sample1 = c->predictor;
        }
    }

    for (i = 0; i < n; i++) {
        TrellisNode *t = node_buf + frontier*(i&1);
        TrellisNode **u;
        int sample   = samples[i * stride];
        int heap_pos = 0;
        memset(nodes_next, 0, frontier * sizeof(TrellisNode*));
        for (j = 0; j < frontier && nodes[j]; j++) {
            // higher j have higher ssd already, so they're likely
            // to yield a suboptimal next sample too
            const int range = (j < frontier / 2) ? 1 : 0;
            const int step  = nodes[j]->step;
            int nidx;
            if (version == AV_CODEC_ID_ADPCM_MS) {
                const int predictor = ((nodes[j]->sample1 * c->coeff1) +
                                       (nodes[j]->sample2 * c->coeff2)) / 64;
                const int div  = (sample - predictor) / step;
                const int nmin = av_clip(div-range, -8, 6);
                const int nmax = av_clip(div+range, -7, 7);
                for (nidx = nmin; nidx <= nmax; nidx++) {
                    const int nibble = nidx & 0xf;
                    int dec_sample   = predictor + nidx * step;
#define STORE_NODE(NAME, STEP_INDEX)\
                    int d;\
                    uint32_t ssd;\
                    int pos;\
                    TrellisNode *u;\
                    uint8_t *h;\
                    dec_sample = av_clip_int16(dec_sample);\
                    d = sample - dec_sample;\
                    ssd = nodes[j]->ssd + d*d;\
                    /* Check for wraparound, skip such samples completely. \
                     * Note, changing ssd to a 64 bit variable would be \
                     * simpler, avoiding this check, but it's slower on \
                     * x86 32 bit at the moment. */\
                    if (ssd < nodes[j]->ssd)\
                        goto next_##NAME;\
                    /* Collapse any two states with the same previous sample value. \
                     * One could also distinguish states by step and by 2nd to last
                     * sample, but the effects of that are negligible.
                     * Since nodes in the previous generation are iterated
                     * through a heap, they're roughly ordered from better to
                     * worse, but not strictly ordered. Therefore, an earlier
                     * node with the same sample value is better in most cases
                     * (and thus the current is skipped), but not strictly
                     * in all cases. Only skipping samples where ssd >=
                     * ssd of the earlier node with the same sample gives
                     * slightly worse quality, though, for some reason. */ \
                    h = &hash[(uint16_t) dec_sample];\
                    if (*h == generation)\
                        goto next_##NAME;\
                    if (heap_pos < frontier) {\
                        pos = heap_pos++;\
                    } else {\
                        /* Try to replace one of the leaf nodes with the new \
                         * one, but try a different slot each time. */\
                        pos = (frontier >> 1) +\
                              (heap_pos & ((frontier >> 1) - 1));\
                        if (ssd > nodes_next[pos]->ssd)\
                            goto next_##NAME;\
                        heap_pos++;\
                    }\
                    *h = generation;\
                    u  = nodes_next[pos];\
                    if (!u) {\
                        av_assert1(pathn < FREEZE_INTERVAL << avctx->trellis);\
                        u = t++;\
                        nodes_next[pos] = u;\
                        u->path = pathn++;\
                    }\
                    u->ssd  = ssd;\
                    u->step = STEP_INDEX;\
                    u->sample2 = nodes[j]->sample1;\
                    u->sample1 = dec_sample;\
                    paths[u->path].nibble = nibble;\
                    paths[u->path].prev   = nodes[j]->path;\
                    /* Sift the newly inserted node up in the heap to \
                     * restore the heap property. */\
                    while (pos > 0) {\
                        int parent = (pos - 1) >> 1;\
                        if (nodes_next[parent]->ssd <= ssd)\
                            break;\
                        FFSWAP(TrellisNode*, nodes_next[parent], nodes_next[pos]);\
                        pos = parent;\
                    }\
                    next_##NAME:;
                    STORE_NODE(ms, FFMAX(16,
                               (ff_adpcm_AdaptationTable[nibble] * step) >> 8));
                }
            } else if (version == AV_CODEC_ID_ADPCM_IMA_WAV ||
                       version == AV_CODEC_ID_ADPCM_IMA_QT  ||
                       version == AV_CODEC_ID_ADPCM_SWF) {
#define LOOP_NODES(NAME, STEP_TABLE, STEP_INDEX)\
                const int predictor = nodes[j]->sample1;\
                const int div = (sample - predictor) * 4 / STEP_TABLE;\
                int nmin = av_clip(div - range, -7, 6);\
                int nmax = av_clip(div + range, -6, 7);\
                if (nmin <= 0)\
                    nmin--; /* distinguish -0 from +0 */\
                if (nmax < 0)\
                    nmax--;\
                for (nidx = nmin; nidx <= nmax; nidx++) {\
                    const int nibble = nidx < 0 ? 7 - nidx : nidx;\
                    int dec_sample = predictor +\
                                    (STEP_TABLE *\
                                     ff_adpcm_yamaha_difflookup[nibble]) / 8;\
                    STORE_NODE(NAME, STEP_INDEX);\
                }
                LOOP_NODES(ima, ff_adpcm_step_table[step],
                           av_clip(step + ff_adpcm_index_table[nibble], 0, 88));
            } else { //AV_CODEC_ID_ADPCM_YAMAHA
                LOOP_NODES(yamaha, step,
                           av_clip((step * ff_adpcm_yamaha_indexscale[nibble]) >> 8,
                                   127, 24567));
#undef LOOP_NODES
#undef STORE_NODE
            }
        }

        u = nodes;
        nodes = nodes_next;
        nodes_next = u;

        generation++;
        if (generation == 255) {
            memset(hash, 0xff, 65536 * sizeof(*hash));
            generation = 0;
        }

        // prevent overflow
        if (nodes[0]->ssd > (1 << 28)) {
            for (j = 1; j < frontier && nodes[j]; j++)
                nodes[j]->ssd -= nodes[0]->ssd;
            nodes[0]->ssd = 0;
        }

        // merge old paths to save memory
        if (i == froze + FREEZE_INTERVAL) {
            p = &paths[nodes[0]->path];
            for (k = i; k > froze; k--) {
                dst[k] = p->nibble;
                p = &paths[p->prev];
            }
            froze = i;
            pathn = 0;
            // other nodes might use paths that don't coincide with the frozen one.
            // checking which nodes do so is too slow, so just kill them all.
            // this also slightly improves quality, but I don't know why.
            memset(nodes + 1, 0, (frontier - 1) * sizeof(TrellisNode*));
        }
    }

    p = &paths[nodes[0]->path];
    for (i = n - 1; i > froze; i--) {
        dst[i] = p->nibble;
        p = &paths[p->prev];
    }

    c->predictor  = nodes[0]->sample1;
    c->sample1    = nodes[0]->sample1;
    c->sample2    = nodes[0]->sample2;
    c->step_index = nodes[0]->step;
    c->step       = nodes[0]->step;
    c->idelta     = nodes[0]->step;
}

static int adpcm_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                              const AVFrame *frame, int *got_packet_ptr)
{
    int n, i, ch, st, pkt_size, ret;
    const int16_t *samples;
    int16_t **samples_p;
    uint8_t *dst;
    ADPCMEncodeContext *c = avctx->priv_data;
    uint8_t *buf;

    samples = (const int16_t *)frame->data[0];
    samples_p = (int16_t **)frame->extended_data;
    st = avctx->channels == 2;

    if (avctx->codec_id == AV_CODEC_ID_ADPCM_SWF)
        pkt_size = (2 + avctx->channels * (22 + 4 * (frame->nb_samples - 1)) + 7) / 8;
    else
        pkt_size = avctx->block_align;
    if ((ret = ff_alloc_packet2(avctx, avpkt, pkt_size)) < 0)
        return ret;
    dst = avpkt->data;

    switch(avctx->codec->id) {
    case AV_CODEC_ID_ADPCM_IMA_WAV:
    {
        int blocks, j;

        blocks = (frame->nb_samples - 1) / 8;

        for (ch = 0; ch < avctx->channels; ch++) {
            ADPCMChannelStatus *status = &c->status[ch];
            status->prev_sample = samples_p[ch][0];
            /* status->step_index = 0;
               XXX: not sure how to init the state machine */
            bytestream_put_le16(&dst, status->prev_sample);
            *dst++ = status->step_index;
            *dst++ = 0; /* unknown */
        }

        /* stereo: 4 bytes (8 samples) for left, 4 bytes for right */
        if (avctx->trellis > 0) {
            FF_ALLOC_OR_GOTO(avctx, buf, avctx->channels * blocks * 8, error);
            for (ch = 0; ch < avctx->channels; ch++) {
                adpcm_compress_trellis(avctx, &samples_p[ch][1],
                                       buf + ch * blocks * 8, &c->status[ch],
                                       blocks * 8, 1);
            }
            for (i = 0; i < blocks; i++) {
                for (ch = 0; ch < avctx->channels; ch++) {
                    uint8_t *buf1 = buf + ch * blocks * 8 + i * 8;
                    for (j = 0; j < 8; j += 2)
                        *dst++ = buf1[j] | (buf1[j + 1] << 4);
                }
            }
            av_free(buf);
        } else {
            for (i = 0; i < blocks; i++) {
                for (ch = 0; ch < avctx->channels; ch++) {
                    ADPCMChannelStatus *status = &c->status[ch];
                    const int16_t *smp = &samples_p[ch][1 + i * 8];
                    for (j = 0; j < 8; j += 2) {
                        uint8_t v = adpcm_ima_compress_sample(status, smp[j    ]);
                        v        |= adpcm_ima_compress_sample(status, smp[j + 1]) << 4;
                        *dst++ = v;
                    }
                }
            }
        }
        break;
    }
    case AV_CODEC_ID_ADPCM_IMA_QT:
    {
        PutBitContext pb;
        init_put_bits(&pb, dst, pkt_size * 8);

        for (ch = 0; ch < avctx->channels; ch++) {
            ADPCMChannelStatus *status = &c->status[ch];
            put_bits(&pb, 9, (status->prev_sample & 0xFFFF) >> 7);
            put_bits(&pb, 7,  status->step_index);
            if (avctx->trellis > 0) {
                uint8_t buf[64];
                adpcm_compress_trellis(avctx, &samples_p[ch][1], buf, status,
                                       64, 1);
                for (i = 0; i < 64; i++)
                    put_bits(&pb, 4, buf[i ^ 1]);
            } else {
                for (i = 0; i < 64; i += 2) {
                    int t1, t2;
                    t1 = adpcm_ima_qt_compress_sample(status, samples_p[ch][i    ]);
                    t2 = adpcm_ima_qt_compress_sample(status, samples_p[ch][i + 1]);
                    put_bits(&pb, 4, t2);
                    put_bits(&pb, 4, t1);
                }
            }
        }

        flush_put_bits(&pb);
        break;
    }
    case AV_CODEC_ID_ADPCM_SWF:
    {
        PutBitContext pb;
        init_put_bits(&pb, dst, pkt_size * 8);

        n = frame->nb_samples - 1;

        // store AdpcmCodeSize
        put_bits(&pb, 2, 2);    // set 4-bit flash adpcm format

        // init the encoder state
        for (i = 0; i < avctx->channels; i++) {
            // clip step so it fits 6 bits
            c->status[i].step_index = av_clip(c->status[i].step_index, 0, 63);
            put_sbits(&pb, 16, samples[i]);
            put_bits(&pb, 6, c->status[i].step_index);
            c->status[i].prev_sample = samples[i];
        }

        if (avctx->trellis > 0) {
            FF_ALLOC_OR_GOTO(avctx, buf, 2 * n, error);
            adpcm_compress_trellis(avctx, samples + avctx->channels, buf,
                                   &c->status[0], n, avctx->channels);
            if (avctx->channels == 2)
                adpcm_compress_trellis(avctx, samples + avctx->channels + 1,
                                       buf + n, &c->status[1], n,
                                       avctx->channels);
            for (i = 0; i < n; i++) {
                put_bits(&pb, 4, buf[i]);
                if (avctx->channels == 2)
                    put_bits(&pb, 4, buf[n + i]);
            }
            av_free(buf);
        } else {
            for (i = 1; i < frame->nb_samples; i++) {
                put_bits(&pb, 4, adpcm_ima_compress_sample(&c->status[0],
                         samples[avctx->channels * i]));
                if (avctx->channels == 2)
                    put_bits(&pb, 4, adpcm_ima_compress_sample(&c->status[1],
                             samples[2 * i + 1]));
            }
        }
        flush_put_bits(&pb);
        break;
    }
    case AV_CODEC_ID_ADPCM_MS:
        for (i = 0; i < avctx->channels; i++) {
            int predictor = 0;
            *dst++ = predictor;
            c->status[i].coeff1 = ff_adpcm_AdaptCoeff1[predictor];
            c->status[i].coeff2 = ff_adpcm_AdaptCoeff2[predictor];
        }
        for (i = 0; i < avctx->channels; i++) {
            if (c->status[i].idelta < 16)
                c->status[i].idelta = 16;
            bytestream_put_le16(&dst, c->status[i].idelta);
        }
        for (i = 0; i < avctx->channels; i++)
            c->status[i].sample2= *samples++;
        for (i = 0; i < avctx->channels; i++) {
            c->status[i].sample1 = *samples++;
            bytestream_put_le16(&dst, c->status[i].sample1);
        }
        for (i = 0; i < avctx->channels; i++)
            bytestream_put_le16(&dst, c->status[i].sample2);

        if (avctx->trellis > 0) {
            n = avctx->block_align - 7 * avctx->channels;
            FF_ALLOC_OR_GOTO(avctx, buf, 2 * n, error);
            if (avctx->channels == 1) {
                adpcm_compress_trellis(avctx, samples, buf, &c->status[0], n,
                                       avctx->channels);
                for (i = 0; i < n; i += 2)
                    *dst++ = (buf[i] << 4) | buf[i + 1];
            } else {
                adpcm_compress_trellis(avctx, samples,     buf,
                                       &c->status[0], n, avctx->channels);
                adpcm_compress_trellis(avctx, samples + 1, buf + n,
                                       &c->status[1], n, avctx->channels);
                for (i = 0; i < n; i++)
                    *dst++ = (buf[i] << 4) | buf[n + i];
            }
            av_free(buf);
        } else {
            for (i = 7 * avctx->channels; i < avctx->block_align; i++) {
                int nibble;
                nibble  = adpcm_ms_compress_sample(&c->status[ 0], *samples++) << 4;
                nibble |= adpcm_ms_compress_sample(&c->status[st], *samples++);
                *dst++  = nibble;
            }
        }
        break;
    case AV_CODEC_ID_ADPCM_YAMAHA:
        n = frame->nb_samples / 2;
        if (avctx->trellis > 0) {
            FF_ALLOC_OR_GOTO(avctx, buf, 2 * n * 2, error);
            n *= 2;
            if (avctx->channels == 1) {
                adpcm_compress_trellis(avctx, samples, buf, &c->status[0], n,
                                       avctx->channels);
                for (i = 0; i < n; i += 2)
                    *dst++ = buf[i] | (buf[i + 1] << 4);
            } else {
                adpcm_compress_trellis(avctx, samples,     buf,
                                       &c->status[0], n, avctx->channels);
                adpcm_compress_trellis(avctx, samples + 1, buf + n,
                                       &c->status[1], n, avctx->channels);
                for (i = 0; i < n; i++)
                    *dst++ = buf[i] | (buf[n + i] << 4);
            }
            av_free(buf);
        } else
            for (n *= avctx->channels; n > 0; n--) {
                int nibble;
                nibble  = adpcm_yamaha_compress_sample(&c->status[ 0], *samples++);
                nibble |= adpcm_yamaha_compress_sample(&c->status[st], *samples++) << 4;
                *dst++  = nibble;
            }
        break;
    default:
        return AVERROR(EINVAL);
    }

    avpkt->size = pkt_size;
    *got_packet_ptr = 1;
    return 0;
error:
    return AVERROR(ENOMEM);
}

static const enum AVSampleFormat sample_fmts[] = {
    AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_NONE
};

static const enum AVSampleFormat sample_fmts_p[] = {
    AV_SAMPLE_FMT_S16P, AV_SAMPLE_FMT_NONE
};

#define ADPCM_ENCODER(id_, name_, sample_fmts_, long_name_) \
AVCodec ff_ ## name_ ## _encoder = {                        \
    .name           = #name_,                               \
    .long_name      = NULL_IF_CONFIG_SMALL(long_name_),     \
    .type           = AVMEDIA_TYPE_AUDIO,                   \
    .id             = id_,                                  \
    .priv_data_size = sizeof(ADPCMEncodeContext),           \
    .init           = adpcm_encode_init,                    \
    .encode2        = adpcm_encode_frame,                   \
    .close          = adpcm_encode_close,                   \
    .sample_fmts    = sample_fmts_,                         \
}

ADPCM_ENCODER(AV_CODEC_ID_ADPCM_IMA_QT,  adpcm_ima_qt,  sample_fmts_p, "ADPCM IMA QuickTime");
ADPCM_ENCODER(AV_CODEC_ID_ADPCM_IMA_WAV, adpcm_ima_wav, sample_fmts_p, "ADPCM IMA WAV");
ADPCM_ENCODER(AV_CODEC_ID_ADPCM_MS,      adpcm_ms,      sample_fmts,   "ADPCM Microsoft");
ADPCM_ENCODER(AV_CODEC_ID_ADPCM_SWF,     adpcm_swf,     sample_fmts,   "ADPCM Shockwave Flash");
ADPCM_ENCODER(AV_CODEC_ID_ADPCM_YAMAHA,  adpcm_yamaha,  sample_fmts,   "ADPCM Yamaha");
