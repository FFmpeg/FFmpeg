/*
 * Interface to libtwolame for mp2 encoding
 * Copyright (c) 2012 Paul B Mahol
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
 * Interface to libtwolame for mp2 encoding.
 */

#include <twolame.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "encode.h"
#include "internal.h"
#include "mpegaudio.h"

typedef struct TWOLAMEContext {
    AVClass *class;
    int mode;
    int psymodel;
    int energy;
    int error_protection;
    int copyright;
    int original;
    int verbosity;

    twolame_options *glopts;
    int64_t next_pts;
} TWOLAMEContext;

static av_cold int twolame_encode_close(AVCodecContext *avctx)
{
    TWOLAMEContext *s = avctx->priv_data;
    twolame_close(&s->glopts);
    return 0;
}

static av_cold int twolame_encode_init(AVCodecContext *avctx)
{
    TWOLAMEContext *s = avctx->priv_data;
    int ret;

    avctx->frame_size = TWOLAME_SAMPLES_PER_FRAME;
    avctx->initial_padding = 512 - 32 + 1;

    s->glopts = twolame_init();
    if (!s->glopts)
        return AVERROR(ENOMEM);

    twolame_set_verbosity(s->glopts, s->verbosity);
    twolame_set_mode(s->glopts, s->mode);
    twolame_set_psymodel(s->glopts, s->psymodel);
    twolame_set_energy_levels(s->glopts, s->energy);
    twolame_set_error_protection(s->glopts, s->error_protection);
    twolame_set_copyright(s->glopts, s->copyright);
    twolame_set_original(s->glopts, s->original);

    twolame_set_num_channels(s->glopts, avctx->channels);
    twolame_set_in_samplerate(s->glopts, avctx->sample_rate);
    twolame_set_out_samplerate(s->glopts, avctx->sample_rate);

    if (!avctx->bit_rate) {
        if ((s->mode == TWOLAME_AUTO_MODE && avctx->channels == 1) || s->mode == TWOLAME_MONO)
            avctx->bit_rate = avctx->sample_rate < 28000 ? 80000 : 192000;
        else
            avctx->bit_rate = avctx->sample_rate < 28000 ? 160000 : 384000;
    }

    if (avctx->flags & AV_CODEC_FLAG_QSCALE || !avctx->bit_rate) {
        twolame_set_VBR(s->glopts, TRUE);
        twolame_set_VBR_level(s->glopts,
                              avctx->global_quality / (float) FF_QP2LAMBDA);
        av_log(avctx, AV_LOG_WARNING,
               "VBR in MP2 is a hack, use another codec that supports it.\n");
    } else {
        twolame_set_bitrate(s->glopts, avctx->bit_rate / 1000);
    }

    ret = twolame_init_params(s->glopts);
    if (ret) {
        twolame_encode_close(avctx);
        return AVERROR_UNKNOWN;
    }

    return 0;
}

static int twolame_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                                const AVFrame *frame, int *got_packet_ptr)
{
    TWOLAMEContext *s = avctx->priv_data;
    int ret;

    if ((ret = ff_alloc_packet(avctx, avpkt, MPA_MAX_CODED_FRAME_SIZE)) < 0)
        return ret;

    if (frame) {
        switch (avctx->sample_fmt) {
        case AV_SAMPLE_FMT_FLT:
            ret = twolame_encode_buffer_float32_interleaved(s->glopts,
                                                            (const float *)frame->data[0],
                                                            frame->nb_samples,
                                                            avpkt->data,
                                                            avpkt->size);
            break;
        case AV_SAMPLE_FMT_FLTP:
            ret = twolame_encode_buffer_float32(s->glopts,
                                                (const float *)frame->data[0],
                                                (const float *)frame->data[1],
                                                frame->nb_samples,
                                                avpkt->data, avpkt->size);
            break;
        case AV_SAMPLE_FMT_S16:
            ret = twolame_encode_buffer_interleaved(s->glopts,
                                                    (const short int *)frame->data[0],
                                                    frame->nb_samples,
                                                    avpkt->data, avpkt->size);
            break;
        case AV_SAMPLE_FMT_S16P:
            ret = twolame_encode_buffer(s->glopts,
                                        (const short int *)frame->data[0],
                                        (const short int *)frame->data[1],
                                        frame->nb_samples,
                                        avpkt->data, avpkt->size);
            break;
        default:
            av_log(avctx, AV_LOG_ERROR,
                   "Unsupported sample format %d.\n", avctx->sample_fmt);
            return AVERROR_BUG;
        }
    } else {
        ret = twolame_encode_flush(s->glopts, avpkt->data, avpkt->size);
    }

    if (!ret)     // no bytes written
        return 0;
    if (ret < 0)  // twolame error
        return AVERROR_UNKNOWN;

    if (frame) {
        avpkt->duration = ff_samples_to_time_base(avctx, frame->nb_samples);
        if (frame->pts != AV_NOPTS_VALUE)
            avpkt->pts = frame->pts - ff_samples_to_time_base(avctx, avctx->initial_padding);
    } else {
        avpkt->pts = s->next_pts;
    }
    // this is for setting pts for flushed packet(s).
    if (avpkt->pts != AV_NOPTS_VALUE)
        s->next_pts = avpkt->pts + avpkt->duration;

    av_shrink_packet(avpkt, ret);
    *got_packet_ptr = 1;
    return 0;
}

#define OFFSET(x) offsetof(TWOLAMEContext, x)
#define AE AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "mode", "Mpeg Mode", OFFSET(mode), AV_OPT_TYPE_INT, { .i64 = TWOLAME_AUTO_MODE }, TWOLAME_AUTO_MODE, TWOLAME_MONO, AE, "mode"},
        { "auto", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TWOLAME_AUTO_MODE }, 0, 0, AE, "mode" },
        { "stereo", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TWOLAME_STEREO }, 0, 0, AE, "mode" },
        { "joint_stereo", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TWOLAME_JOINT_STEREO }, 0, 0, AE, "mode" },
        { "dual_channel", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TWOLAME_DUAL_CHANNEL }, 0, 0, AE, "mode" },
        { "mono", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = TWOLAME_MONO }, 0, 0, AE, "mode" },
    { "psymodel", "Psychoacoustic Model", OFFSET(psymodel), AV_OPT_TYPE_INT, { .i64 = 3 }, -1, 4, AE},
    { "energy_levels","enable energy levels", OFFSET(energy), AV_OPT_TYPE_INT, { .i64 = 0 },  0, 1, AE},
    { "error_protection","enable CRC error protection", OFFSET(error_protection), AV_OPT_TYPE_INT, { .i64 = 0 },  0, 1, AE},
    { "copyright", "set MPEG Audio Copyright flag", OFFSET(copyright), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AE},
    { "original", "set MPEG Audio Original flag", OFFSET(original), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AE},
    { "verbosity", "set library optput level (0-10)", OFFSET(verbosity), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 10, AE},
    { NULL },
};

static const AVClass twolame_class = {
    .class_name = "libtwolame encoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault twolame_defaults[] = {
    { "b", "0" },
    { NULL },
};

static const int twolame_samplerates[] = {
    16000, 22050, 24000, 32000, 44100, 48000, 0
};

const AVCodec ff_libtwolame_encoder = {
    .name           = "libtwolame",
    .long_name      = NULL_IF_CONFIG_SMALL("libtwolame MP2 (MPEG audio layer 2)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP2,
    .priv_data_size = sizeof(TWOLAMEContext),
    .init           = twolame_encode_init,
    .encode2        = twolame_encode_frame,
    .close          = twolame_encode_close,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .defaults       = twolame_defaults,
    .priv_class     = &twolame_class,
    .sample_fmts    = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_FLT,
        AV_SAMPLE_FMT_FLTP,
        AV_SAMPLE_FMT_S16,
        AV_SAMPLE_FMT_S16P,
        AV_SAMPLE_FMT_NONE
    },
    .channel_layouts = (const uint64_t[]) {
        AV_CH_LAYOUT_MONO,
        AV_CH_LAYOUT_STEREO,
        0 },
    .supported_samplerates = twolame_samplerates,
    .wrapper_name   = "libtwolame",
};
