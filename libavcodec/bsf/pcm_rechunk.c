/*
 * Copyright (c) 2020 Marton Balint
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

#include "bsf.h"
#include "bsf_internal.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"

typedef struct PCMContext {
    const AVClass *class;

    int nb_out_samples;
    int pad;
    AVRational frame_rate;

    AVPacket *in_pkt;
    AVPacket *out_pkt;
    int sample_size;
    int64_t n;
} PCMContext;

static int init(AVBSFContext *ctx)
{
    PCMContext *s = ctx->priv_data;
    AVRational sr = av_make_q(ctx->par_in->sample_rate, 1);
    int64_t min_samples;

    if (ctx->par_in->ch_layout.nb_channels <= 0 || ctx->par_in->sample_rate <= 0)
        return AVERROR(EINVAL);

    ctx->time_base_out = av_inv_q(sr);
    s->sample_size = ctx->par_in->ch_layout.nb_channels *
                     av_get_bits_per_sample(ctx->par_in->codec_id) / 8;

    if (s->frame_rate.num) {
        min_samples = av_rescale_q_rnd(1, sr, s->frame_rate, AV_ROUND_DOWN);
    } else {
        min_samples = s->nb_out_samples;
    }
    if (min_samples <= 0 || min_samples > INT_MAX / s->sample_size - 1)
        return AVERROR(EINVAL);

    s->in_pkt  = av_packet_alloc();
    s->out_pkt = av_packet_alloc();
    if (!s->in_pkt || !s->out_pkt)
        return AVERROR(ENOMEM);

    return 0;
}

static void uninit(AVBSFContext *ctx)
{
    PCMContext *s = ctx->priv_data;
    av_packet_free(&s->in_pkt);
    av_packet_free(&s->out_pkt);
}

static void flush(AVBSFContext *ctx)
{
    PCMContext *s = ctx->priv_data;
    av_packet_unref(s->in_pkt);
    av_packet_unref(s->out_pkt);
    s->n = 0;
}

static int send_packet(PCMContext *s, int nb_samples, AVPacket *pkt)
{
    pkt->duration = nb_samples;
    s->n++;
    return 0;
}

static void drain_packet(AVPacket *pkt, int drain_data, int drain_samples)
{
    pkt->size -= drain_data;
    pkt->data += drain_data;
    if (pkt->dts != AV_NOPTS_VALUE)
        pkt->dts += drain_samples;
    if (pkt->pts != AV_NOPTS_VALUE)
        pkt->pts += drain_samples;
}

static int get_next_nb_samples(AVBSFContext *ctx)
{
    PCMContext *s = ctx->priv_data;
    if (s->frame_rate.num) {
        AVRational sr = av_make_q(ctx->par_in->sample_rate, 1);
        return av_rescale_q(s->n + 1, sr, s->frame_rate) - av_rescale_q(s->n, sr, s->frame_rate);
    } else {
        return s->nb_out_samples;
    }
}

static void set_silence(AVCodecParameters *par, uint8_t *buf, size_t size)
{
    int c = 0;
    switch (par->codec_id) {
    case AV_CODEC_ID_PCM_ALAW:  c = 0xd5; break;
    case AV_CODEC_ID_PCM_MULAW:
    case AV_CODEC_ID_PCM_VIDC:  c = 0xff; break;
    case AV_CODEC_ID_PCM_U8:    c = 0x80; break;
    }
    memset(buf, c, size);
}

static int rechunk_filter(AVBSFContext *ctx, AVPacket *pkt)
{
    PCMContext *s = ctx->priv_data;
    int nb_samples = get_next_nb_samples(ctx);
    int data_size = nb_samples * s->sample_size;
    int ret;

    do {
        if (s->in_pkt->size) {
            if (s->out_pkt->size || s->in_pkt->size < data_size) {
                int drain = FFMIN(s->in_pkt->size, data_size - s->out_pkt->size);
                if (!s->out_pkt->size) {
                    ret = av_new_packet(s->out_pkt, data_size);
                    if (ret < 0)
                        return ret;
                    ret = av_packet_copy_props(s->out_pkt, s->in_pkt);
                    if (ret < 0) {
                        av_packet_unref(s->out_pkt);
                        return ret;
                    }
                    s->out_pkt->size = 0;
                }
                memcpy(s->out_pkt->data + s->out_pkt->size, s->in_pkt->data, drain);
                s->out_pkt->size += drain;
                drain_packet(s->in_pkt, drain, drain / s->sample_size);
                if (!s->in_pkt->size)
                    av_packet_unref(s->in_pkt);
                if (s->out_pkt->size == data_size) {
                    av_packet_move_ref(pkt, s->out_pkt);
                    return send_packet(s, nb_samples, pkt);
                }
                av_assert0(!s->in_pkt->size);
            } else if (s->in_pkt->size > data_size) {
                ret = av_packet_ref(pkt, s->in_pkt);
                if (ret < 0)
                    return ret;
                pkt->size = data_size;
                drain_packet(s->in_pkt, data_size, nb_samples);
                return send_packet(s, nb_samples, pkt);
            } else {
                av_assert0(s->in_pkt->size == data_size);
                av_packet_move_ref(pkt, s->in_pkt);
                return send_packet(s, nb_samples, pkt);
            }
        } else
            av_packet_unref(s->in_pkt);

        ret = ff_bsf_get_packet_ref(ctx, s->in_pkt);
        if (ret == AVERROR_EOF && s->out_pkt->size) {
            if (s->pad) {
                set_silence(ctx->par_in, s->out_pkt->data + s->out_pkt->size, data_size - s->out_pkt->size);
                s->out_pkt->size = data_size;
            } else {
                nb_samples = s->out_pkt->size / s->sample_size;
            }
            av_packet_move_ref(pkt, s->out_pkt);
            return send_packet(s, nb_samples, pkt);
        }
        if (ret >= 0)
            av_packet_rescale_ts(s->in_pkt, ctx->time_base_in, ctx->time_base_out);
    } while (ret >= 0);

    return ret;
}

#define OFFSET(x) offsetof(PCMContext, x)
#define FLAGS (AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_BSF_PARAM)
static const AVOption options[] = {
    { "nb_out_samples", "set the number of per-packet output samples", OFFSET(nb_out_samples),   AV_OPT_TYPE_INT, {.i64=1024}, 1, INT_MAX, FLAGS },
    { "n",              "set the number of per-packet output samples", OFFSET(nb_out_samples),   AV_OPT_TYPE_INT, {.i64=1024}, 1, INT_MAX, FLAGS },
    { "pad",            "pad last packet with zeros",                  OFFSET(pad),             AV_OPT_TYPE_BOOL, {.i64=1} ,   0,       1, FLAGS },
    { "p",              "pad last packet with zeros",                  OFFSET(pad),             AV_OPT_TYPE_BOOL, {.i64=1} ,   0,       1, FLAGS },
    { "frame_rate",     "set number of packets per second",            OFFSET(frame_rate),  AV_OPT_TYPE_RATIONAL, {.dbl=0},    0, INT_MAX, FLAGS },
    { "r",              "set number of packets per second",            OFFSET(frame_rate),  AV_OPT_TYPE_RATIONAL, {.dbl=0},    0, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass pcm_rechunk_class = {
    .class_name = "pcm_rechunk_bsf",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID codec_ids[] = {
    AV_CODEC_ID_PCM_ALAW,
    AV_CODEC_ID_PCM_F16LE,
    AV_CODEC_ID_PCM_F24LE,
    AV_CODEC_ID_PCM_F32BE,
    AV_CODEC_ID_PCM_F32LE,
    AV_CODEC_ID_PCM_F64BE,
    AV_CODEC_ID_PCM_F64LE,
    AV_CODEC_ID_PCM_MULAW,
    AV_CODEC_ID_PCM_S16BE,
    AV_CODEC_ID_PCM_S16LE,
    AV_CODEC_ID_PCM_S24BE,
    AV_CODEC_ID_PCM_S24DAUD,
    AV_CODEC_ID_PCM_S24LE,
    AV_CODEC_ID_PCM_S32BE,
    AV_CODEC_ID_PCM_S32LE,
    AV_CODEC_ID_PCM_S64BE,
    AV_CODEC_ID_PCM_S64LE,
    AV_CODEC_ID_PCM_S8,
    AV_CODEC_ID_PCM_SGA,
    AV_CODEC_ID_PCM_U8,
    AV_CODEC_ID_PCM_VIDC,
    AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_pcm_rechunk_bsf = {
    .p.name         = "pcm_rechunk",
    .p.codec_ids    = codec_ids,
    .p.priv_class   = &pcm_rechunk_class,
    .priv_data_size = sizeof(PCMContext),
    .filter         = rechunk_filter,
    .init           = init,
    .flush          = flush,
    .close          = uninit,
};
