/*
 * ALSA input
 * Copyright (c) 2007 Luca Abeni ( lucabe72 email it )
 * Copyright (c) 2007 Benoit Fouet ( benoit fouet free fr )
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
 * ALSA input
 * @author Luca Abeni ( lucabe72 email it )
 * @author Benoit Fouet ( benoit fouet free fr )
 * @author Nicolas George ( nicolas george normalesup org )
 */

#include <alsa/asoundlib.h>

#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"

#include "libavformat/avformat.h"
#include "libavformat/internal.h"

/* XXX: we make the assumption that the soundcard accepts this format */
/* XXX: find better solution with "preinit" method, needed also in
        other formats */
#define DEFAULT_CODEC_ID AV_NE(AV_CODEC_ID_PCM_S16BE, AV_CODEC_ID_PCM_S16LE)

#define ALSA_BUFFER_SIZE_MAX 32768

typedef struct AlsaData {
    AVClass *class;
    snd_pcm_t *h;
    int frame_size;  ///< preferred size for reads and writes
    int period_size; ///< bytes per sample * channels
    int sample_rate; ///< sample rate set by user
    int channels;    ///< number of channels set by user
    void (*reorder_func)(const void *, void *, int);
    void *reorder_buf;
    int reorder_buf_size; ///< in frames
} AlsaData;

static av_cold snd_pcm_format_t codec_id_to_pcm_format(int codec_id)
{
    switch(codec_id) {
        case AV_CODEC_ID_PCM_F64LE: return SND_PCM_FORMAT_FLOAT64_LE;
        case AV_CODEC_ID_PCM_F64BE: return SND_PCM_FORMAT_FLOAT64_BE;
        case AV_CODEC_ID_PCM_F32LE: return SND_PCM_FORMAT_FLOAT_LE;
        case AV_CODEC_ID_PCM_F32BE: return SND_PCM_FORMAT_FLOAT_BE;
        case AV_CODEC_ID_PCM_S32LE: return SND_PCM_FORMAT_S32_LE;
        case AV_CODEC_ID_PCM_S32BE: return SND_PCM_FORMAT_S32_BE;
        case AV_CODEC_ID_PCM_U32LE: return SND_PCM_FORMAT_U32_LE;
        case AV_CODEC_ID_PCM_U32BE: return SND_PCM_FORMAT_U32_BE;
        case AV_CODEC_ID_PCM_S24LE: return SND_PCM_FORMAT_S24_3LE;
        case AV_CODEC_ID_PCM_S24BE: return SND_PCM_FORMAT_S24_3BE;
        case AV_CODEC_ID_PCM_U24LE: return SND_PCM_FORMAT_U24_3LE;
        case AV_CODEC_ID_PCM_U24BE: return SND_PCM_FORMAT_U24_3BE;
        case AV_CODEC_ID_PCM_S16LE: return SND_PCM_FORMAT_S16_LE;
        case AV_CODEC_ID_PCM_S16BE: return SND_PCM_FORMAT_S16_BE;
        case AV_CODEC_ID_PCM_U16LE: return SND_PCM_FORMAT_U16_LE;
        case AV_CODEC_ID_PCM_U16BE: return SND_PCM_FORMAT_U16_BE;
        case AV_CODEC_ID_PCM_S8:    return SND_PCM_FORMAT_S8;
        case AV_CODEC_ID_PCM_U8:    return SND_PCM_FORMAT_U8;
        case AV_CODEC_ID_PCM_MULAW: return SND_PCM_FORMAT_MU_LAW;
        case AV_CODEC_ID_PCM_ALAW:  return SND_PCM_FORMAT_A_LAW;
        default:                 return SND_PCM_FORMAT_UNKNOWN;
    }
}

#define REORDER_OUT_50(NAME, TYPE) \
static void alsa_reorder_ ## NAME ## _out_50(const void *in_v, void *out_v, int n) \
{ \
    const TYPE *in = in_v; \
    TYPE      *out = out_v; \
\
    while (n-- > 0) { \
        out[0] = in[0]; \
        out[1] = in[1]; \
        out[2] = in[3]; \
        out[3] = in[4]; \
        out[4] = in[2]; \
        in  += 5; \
        out += 5; \
    } \
}

#define REORDER_OUT_51(NAME, TYPE) \
static void alsa_reorder_ ## NAME ## _out_51(const void *in_v, void *out_v, int n) \
{ \
    const TYPE *in = in_v; \
    TYPE      *out = out_v; \
\
    while (n-- > 0) { \
        out[0] = in[0]; \
        out[1] = in[1]; \
        out[2] = in[4]; \
        out[3] = in[5]; \
        out[4] = in[2]; \
        out[5] = in[3]; \
        in  += 6; \
        out += 6; \
    } \
}

#define REORDER_OUT_71(NAME, TYPE) \
static void alsa_reorder_ ## NAME ## _out_71(const void *in_v, void *out_v, int n) \
{ \
    const TYPE *in = in_v; \
    TYPE      *out = out_v; \
\
    while (n-- > 0) { \
        out[0] = in[0]; \
        out[1] = in[1]; \
        out[2] = in[4]; \
        out[3] = in[5]; \
        out[4] = in[2]; \
        out[5] = in[3]; \
        out[6] = in[6]; \
        out[7] = in[7]; \
        in  += 8; \
        out += 8; \
    } \
}

REORDER_OUT_50(int8, int8_t)
REORDER_OUT_51(int8, int8_t)
REORDER_OUT_71(int8, int8_t)
REORDER_OUT_50(int16, int16_t)
REORDER_OUT_51(int16, int16_t)
REORDER_OUT_71(int16, int16_t)
REORDER_OUT_50(int32, int32_t)
REORDER_OUT_51(int32, int32_t)
REORDER_OUT_71(int32, int32_t)
REORDER_OUT_50(f32, float)
REORDER_OUT_51(f32, float)
REORDER_OUT_71(f32, float)

#define FORMAT_I8  0
#define FORMAT_I16 1
#define FORMAT_I32 2
#define FORMAT_F32 3

#define PICK_REORDER(layout)\
switch(format) {\
    case FORMAT_I8:  s->reorder_func = alsa_reorder_int8_out_ ##layout;  break;\
    case FORMAT_I16: s->reorder_func = alsa_reorder_int16_out_ ##layout; break;\
    case FORMAT_I32: s->reorder_func = alsa_reorder_int32_out_ ##layout; break;\
    case FORMAT_F32: s->reorder_func = alsa_reorder_f32_out_ ##layout;   break;\
}

static av_cold int find_reorder_func(AlsaData *s, int codec_id, uint64_t layout, int out)
{
    int format;

    /* reordering input is not currently supported */
    if (!out)
        return AVERROR(ENOSYS);

    /* reordering is not needed for QUAD or 2_2 layout */
    if (layout == AV_CH_LAYOUT_QUAD || layout == AV_CH_LAYOUT_2_2)
        return 0;

    switch (codec_id) {
    case AV_CODEC_ID_PCM_S8:
    case AV_CODEC_ID_PCM_U8:
    case AV_CODEC_ID_PCM_ALAW:
    case AV_CODEC_ID_PCM_MULAW: format = FORMAT_I8;  break;
    case AV_CODEC_ID_PCM_S16LE:
    case AV_CODEC_ID_PCM_S16BE:
    case AV_CODEC_ID_PCM_U16LE:
    case AV_CODEC_ID_PCM_U16BE: format = FORMAT_I16; break;
    case AV_CODEC_ID_PCM_S32LE:
    case AV_CODEC_ID_PCM_S32BE:
    case AV_CODEC_ID_PCM_U32LE:
    case AV_CODEC_ID_PCM_U32BE: format = FORMAT_I32; break;
    case AV_CODEC_ID_PCM_F32LE:
    case AV_CODEC_ID_PCM_F32BE: format = FORMAT_F32; break;
    default:                 return AVERROR(ENOSYS);
    }

    if      (layout == AV_CH_LAYOUT_5POINT0_BACK || layout == AV_CH_LAYOUT_5POINT0)
        PICK_REORDER(50)
    else if (layout == AV_CH_LAYOUT_5POINT1_BACK || layout == AV_CH_LAYOUT_5POINT1)
        PICK_REORDER(51)
    else if (layout == AV_CH_LAYOUT_7POINT1)
        PICK_REORDER(71)

    return s->reorder_func ? 0 : AVERROR(ENOSYS);
}

/**
 * Open an ALSA PCM.
 *
 * @param s media file handle
 * @param mode either SND_PCM_STREAM_CAPTURE or SND_PCM_STREAM_PLAYBACK
 * @param sample_rate in: requested sample rate;
 *                    out: actually selected sample rate
 * @param channels number of channels
 * @param codec_id in: requested AVCodecID or AV_CODEC_ID_NONE;
 *                 out: actually selected AVCodecID, changed only if
 *                 AV_CODEC_ID_NONE was requested
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static av_cold int alsa_open(AVFormatContext *ctx, snd_pcm_stream_t mode,
                             unsigned int *sample_rate,
                             int channels, enum AVCodecID *codec_id)
{
    AlsaData *s = ctx->priv_data;
    const char *audio_device;
    int res, flags = 0;
    snd_pcm_format_t format;
    snd_pcm_t *h;
    snd_pcm_hw_params_t *hw_params;
    snd_pcm_uframes_t buffer_size, period_size;
    uint64_t layout = ctx->streams[0]->codecpar->channel_layout;

    if (ctx->filename[0] == 0) audio_device = "default";
    else                       audio_device = ctx->filename;

    if (*codec_id == AV_CODEC_ID_NONE)
        *codec_id = DEFAULT_CODEC_ID;
    format = codec_id_to_pcm_format(*codec_id);
    if (format == SND_PCM_FORMAT_UNKNOWN) {
        av_log(ctx, AV_LOG_ERROR, "sample format 0x%04x is not supported\n", *codec_id);
        return AVERROR(ENOSYS);
    }
    s->frame_size = av_get_bits_per_sample(*codec_id) / 8 * channels;

    if (ctx->flags & AVFMT_FLAG_NONBLOCK) {
        flags = SND_PCM_NONBLOCK;
    }
    res = snd_pcm_open(&h, audio_device, mode, flags);
    if (res < 0) {
        av_log(ctx, AV_LOG_ERROR, "cannot open audio device %s (%s)\n",
               audio_device, snd_strerror(res));
        return AVERROR(EIO);
    }

    res = snd_pcm_hw_params_malloc(&hw_params);
    if (res < 0) {
        av_log(ctx, AV_LOG_ERROR, "cannot allocate hardware parameter structure (%s)\n",
               snd_strerror(res));
        goto fail1;
    }

    res = snd_pcm_hw_params_any(h, hw_params);
    if (res < 0) {
        av_log(ctx, AV_LOG_ERROR, "cannot initialize hardware parameter structure (%s)\n",
               snd_strerror(res));
        goto fail;
    }

    res = snd_pcm_hw_params_set_access(h, hw_params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (res < 0) {
        av_log(ctx, AV_LOG_ERROR, "cannot set access type (%s)\n",
               snd_strerror(res));
        goto fail;
    }

    res = snd_pcm_hw_params_set_format(h, hw_params, format);
    if (res < 0) {
        av_log(ctx, AV_LOG_ERROR, "cannot set sample format 0x%04x %d (%s)\n",
               *codec_id, format, snd_strerror(res));
        goto fail;
    }

    res = snd_pcm_hw_params_set_rate_near(h, hw_params, sample_rate, 0);
    if (res < 0) {
        av_log(ctx, AV_LOG_ERROR, "cannot set sample rate (%s)\n",
               snd_strerror(res));
        goto fail;
    }

    res = snd_pcm_hw_params_set_channels(h, hw_params, channels);
    if (res < 0) {
        av_log(ctx, AV_LOG_ERROR, "cannot set channel count to %d (%s)\n",
               channels, snd_strerror(res));
        goto fail;
    }

    snd_pcm_hw_params_get_buffer_size_max(hw_params, &buffer_size);
    buffer_size = FFMIN(buffer_size, ALSA_BUFFER_SIZE_MAX);
    /* TODO: maybe use ctx->max_picture_buffer somehow */
    res = snd_pcm_hw_params_set_buffer_size_near(h, hw_params, &buffer_size);
    if (res < 0) {
        av_log(ctx, AV_LOG_ERROR, "cannot set ALSA buffer size (%s)\n",
               snd_strerror(res));
        goto fail;
    }

    snd_pcm_hw_params_get_period_size_min(hw_params, &period_size, NULL);
    if (!period_size)
        period_size = buffer_size / 4;
    res = snd_pcm_hw_params_set_period_size_near(h, hw_params, &period_size, NULL);
    if (res < 0) {
        av_log(ctx, AV_LOG_ERROR, "cannot set ALSA period size (%s)\n",
               snd_strerror(res));
        goto fail;
    }
    s->period_size = period_size;

    res = snd_pcm_hw_params(h, hw_params);
    if (res < 0) {
        av_log(ctx, AV_LOG_ERROR, "cannot set parameters (%s)\n",
               snd_strerror(res));
        goto fail;
    }

    snd_pcm_hw_params_free(hw_params);

    if (channels > 2 && layout) {
        if (find_reorder_func(s, *codec_id, layout, mode == SND_PCM_STREAM_PLAYBACK) < 0) {
            char name[128];
            av_get_channel_layout_string(name, sizeof(name), channels, layout);
            av_log(ctx, AV_LOG_WARNING, "ALSA channel layout unknown or unimplemented for %s %s.\n",
                   name, mode == SND_PCM_STREAM_PLAYBACK ? "playback" : "capture");
        }
        if (s->reorder_func) {
            s->reorder_buf_size = buffer_size;
            s->reorder_buf = av_malloc(s->reorder_buf_size * s->frame_size);
            if (!s->reorder_buf)
                goto fail1;
        }
    }

    s->h = h;
    return 0;

fail:
    snd_pcm_hw_params_free(hw_params);
fail1:
    snd_pcm_close(h);
    return AVERROR(EIO);
}

/**
 * Close the ALSA PCM.
 *
 * @param s1 media file handle
 *
 * @return 0
 */
static av_cold int alsa_close(AVFormatContext *s1)
{
    AlsaData *s = s1->priv_data;

    av_freep(&s->reorder_buf);
    snd_pcm_close(s->h);
    return 0;
}

/**
 * Try to recover from ALSA buffer underrun.
 *
 * @param s1 media file handle
 * @param err error code reported by the previous ALSA call
 *
 * @return 0 if OK, AVERROR_xxx on error
 */
static int alsa_xrun_recover(AVFormatContext *s1, int err)
{
    AlsaData *s = s1->priv_data;
    snd_pcm_t *handle = s->h;

    av_log(s1, AV_LOG_WARNING, "ALSA buffer xrun.\n");
    if (err == -EPIPE) {
        err = snd_pcm_prepare(handle);
        if (err < 0) {
            av_log(s1, AV_LOG_ERROR, "cannot recover from underrun (snd_pcm_prepare failed: %s)\n", snd_strerror(err));

            return AVERROR(EIO);
        }
    } else if (err == -ESTRPIPE) {
        av_log(s1, AV_LOG_ERROR, "-ESTRPIPE... Unsupported!\n");

        return -1;
    }
    return err;
}

static av_cold int audio_read_header(AVFormatContext *s1)
{
    AlsaData *s = s1->priv_data;
    AVStream *st;
    int ret;
    enum AVCodecID codec_id;
    snd_pcm_sw_params_t *sw_params;

    st = avformat_new_stream(s1, NULL);
    if (!st) {
        av_log(s1, AV_LOG_ERROR, "Cannot add stream\n");

        return AVERROR(ENOMEM);
    }
    codec_id    = s1->audio_codec_id;

    ret = alsa_open(s1, SND_PCM_STREAM_CAPTURE, &s->sample_rate, s->channels,
                    &codec_id);
    if (ret < 0) {
        return AVERROR(EIO);
    }

    if (snd_pcm_type(s->h) != SND_PCM_TYPE_HW)
        av_log(s1, AV_LOG_WARNING,
               "capture with some ALSA plugins, especially dsnoop, "
               "may hang.\n");

    ret = snd_pcm_sw_params_malloc(&sw_params);
    if (ret < 0) {
        av_log(s1, AV_LOG_ERROR, "cannot allocate software parameters structure (%s)\n",
               snd_strerror(ret));
        goto fail;
    }

    snd_pcm_sw_params_current(s->h, sw_params);
    snd_pcm_sw_params_set_tstamp_mode(s->h, sw_params, SND_PCM_TSTAMP_ENABLE);

    ret = snd_pcm_sw_params(s->h, sw_params);
    snd_pcm_sw_params_free(sw_params);
    if (ret < 0) {
        av_log(s1, AV_LOG_ERROR, "cannot install ALSA software parameters (%s)\n",
               snd_strerror(ret));
        goto fail;
    }

    /* take real parameters */
    st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id    = codec_id;
    st->codecpar->sample_rate = s->sample_rate;
    st->codecpar->channels    = s->channels;
    avpriv_set_pts_info(st, 64, 1, 1000000);  /* 64 bits pts in us */

    return 0;

fail:
    snd_pcm_close(s->h);
    return AVERROR(EIO);
}

static int audio_read_packet(AVFormatContext *s1, AVPacket *pkt)
{
    AlsaData *s  = s1->priv_data;
    AVStream *st = s1->streams[0];
    int res;
    snd_htimestamp_t timestamp;
    snd_pcm_uframes_t ts_delay;

    if (av_new_packet(pkt, s->period_size) < 0) {
        return AVERROR(EIO);
    }

    while ((res = snd_pcm_readi(s->h, pkt->data, pkt->size / s->frame_size)) < 0) {
        if (res == -EAGAIN) {
            av_packet_unref(pkt);

            return AVERROR(EAGAIN);
        }
        if (alsa_xrun_recover(s1, res) < 0) {
            av_log(s1, AV_LOG_ERROR, "ALSA read error: %s\n",
                   snd_strerror(res));
            av_packet_unref(pkt);

            return AVERROR(EIO);
        }
    }

    snd_pcm_htimestamp(s->h, &ts_delay, &timestamp);
    ts_delay += res;
    pkt->pts = timestamp.tv_sec * 1000000LL
               + (timestamp.tv_nsec * st->codecpar->sample_rate
                  - (int64_t)ts_delay * 1000000000LL + st->codecpar->sample_rate * 500LL)
               / (st->codecpar->sample_rate * 1000LL);

    pkt->size = res * s->frame_size;

    return 0;
}

static const AVOption options[] = {
    { "sample_rate", "", offsetof(AlsaData, sample_rate), AV_OPT_TYPE_INT, {.i64 = 48000}, 1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { "channels",    "", offsetof(AlsaData, channels),    AV_OPT_TYPE_INT, {.i64 = 2},     1, INT_MAX, AV_OPT_FLAG_DECODING_PARAM },
    { NULL },
};

static const AVClass alsa_demuxer_class = {
    .class_name     = "ALSA demuxer",
    .item_name      = av_default_item_name,
    .option         = options,
    .version        = LIBAVUTIL_VERSION_INT,
};

AVInputFormat ff_alsa_demuxer = {
    .name           = "alsa",
    .long_name      = NULL_IF_CONFIG_SMALL("ALSA audio input"),
    .priv_data_size = sizeof(AlsaData),
    .read_header    = audio_read_header,
    .read_packet    = audio_read_packet,
    .read_close     = alsa_close,
    .flags          = AVFMT_NOFILE,
    .priv_class     = &alsa_demuxer_class,
};
