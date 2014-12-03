/*
 * Opus decoder
 * Copyright (c) 2012 Andrew D'Addesio
 * Copyright (c) 2013-2014 Mozilla Corporation
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
 * Opus decoder
 * @author Andrew D'Addesio, Anton Khirnov
 *
 * Codec homepage: http://opus-codec.org/
 * Specification: http://tools.ietf.org/html/rfc6716
 * Ogg Opus specification: https://tools.ietf.org/html/draft-ietf-codec-oggopus-03
 *
 * Ogg-contained .opus files can be produced with opus-tools:
 * http://git.xiph.org/?p=opus-tools.git
 */

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"

#include "libswresample/swresample.h"

#include "avcodec.h"
#include "celp_filters.h"
#include "fft.h"
#include "get_bits.h"
#include "internal.h"
#include "mathops.h"
#include "opus.h"

static const uint16_t silk_frame_duration_ms[16] = {
    10, 20, 40, 60,
    10, 20, 40, 60,
    10, 20, 40, 60,
    10, 20,
    10, 20,
};

/* number of samples of silence to feed to the resampler
 * at the beginning */
static const int silk_resample_delay[] = {
    4, 8, 11, 11, 11
};

static const uint8_t celt_band_end[] = { 13, 17, 17, 19, 21 };

static int get_silk_samplerate(int config)
{
    if (config < 4)
        return 8000;
    else if (config < 8)
        return 12000;
    return 16000;
}

/**
 * Range decoder
 */
static int opus_rc_init(OpusRangeCoder *rc, const uint8_t *data, int size)
{
    int ret = init_get_bits8(&rc->gb, data, size);
    if (ret < 0)
        return ret;

    rc->range = 128;
    rc->value = 127 - get_bits(&rc->gb, 7);
    rc->total_read_bits = 9;
    opus_rc_normalize(rc);

    return 0;
}

static void opus_raw_init(OpusRangeCoder *rc, const uint8_t *rightend,
                          unsigned int bytes)
{
    rc->rb.position = rightend;
    rc->rb.bytes    = bytes;
    rc->rb.cachelen = 0;
    rc->rb.cacheval = 0;
}

static void opus_fade(float *out,
                      const float *in1, const float *in2,
                      const float *window, int len)
{
    int i;
    for (i = 0; i < len; i++)
        out[i] = in2[i] * window[i] + in1[i] * (1.0 - window[i]);
}

static int opus_flush_resample(OpusStreamContext *s, int nb_samples)
{
    int celt_size = av_audio_fifo_size(s->celt_delay);
    int ret, i;
    ret = swr_convert(s->swr,
                      (uint8_t**)s->out, nb_samples,
                      NULL, 0);
    if (ret < 0)
        return ret;
    else if (ret != nb_samples) {
        av_log(s->avctx, AV_LOG_ERROR, "Wrong number of flushed samples: %d\n",
               ret);
        return AVERROR_BUG;
    }

    if (celt_size) {
        if (celt_size != nb_samples) {
            av_log(s->avctx, AV_LOG_ERROR, "Wrong number of CELT delay samples.\n");
            return AVERROR_BUG;
        }
        av_audio_fifo_read(s->celt_delay, (void**)s->celt_output, nb_samples);
        for (i = 0; i < s->output_channels; i++) {
            s->fdsp->vector_fmac_scalar(s->out[i],
                                        s->celt_output[i], 1.0,
                                        nb_samples);
        }
    }

    if (s->redundancy_idx) {
        for (i = 0; i < s->output_channels; i++)
            opus_fade(s->out[i], s->out[i],
                      s->redundancy_output[i] + 120 + s->redundancy_idx,
                      ff_celt_window2 + s->redundancy_idx, 120 - s->redundancy_idx);
        s->redundancy_idx = 0;
    }

    s->out[0]   += nb_samples;
    s->out[1]   += nb_samples;
    s->out_size -= nb_samples * sizeof(float);

    return 0;
}

static int opus_init_resample(OpusStreamContext *s)
{
    static const float delay[16] = { 0.0 };
    const uint8_t *delayptr[2] = { (uint8_t*)delay, (uint8_t*)delay };
    int ret;

    av_opt_set_int(s->swr, "in_sample_rate", s->silk_samplerate, 0);
    ret = swr_init(s->swr);
    if (ret < 0) {
        av_log(s->avctx, AV_LOG_ERROR, "Error opening the resampler.\n");
        return ret;
    }

    ret = swr_convert(s->swr,
                      NULL, 0,
                      delayptr, silk_resample_delay[s->packet.bandwidth]);
    if (ret < 0) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Error feeding initial silence to the resampler.\n");
        return ret;
    }

    return 0;
}

static int opus_decode_redundancy(OpusStreamContext *s, const uint8_t *data, int size)
{
    int ret;
    enum OpusBandwidth bw = s->packet.bandwidth;

    if (s->packet.mode == OPUS_MODE_SILK &&
        bw == OPUS_BANDWIDTH_MEDIUMBAND)
        bw = OPUS_BANDWIDTH_WIDEBAND;

    ret = opus_rc_init(&s->redundancy_rc, data, size);
    if (ret < 0)
        goto fail;
    opus_raw_init(&s->redundancy_rc, data + size, size);

    ret = ff_celt_decode_frame(s->celt, &s->redundancy_rc,
                               s->redundancy_output,
                               s->packet.stereo + 1, 240,
                               0, celt_band_end[s->packet.bandwidth]);
    if (ret < 0)
        goto fail;

    return 0;
fail:
    av_log(s->avctx, AV_LOG_ERROR, "Error decoding the redundancy frame.\n");
    return ret;
}

static int opus_decode_frame(OpusStreamContext *s, const uint8_t *data, int size)
{
    int samples    = s->packet.frame_duration;
    int redundancy = 0;
    int redundancy_size, redundancy_pos;
    int ret, i, consumed;
    int delayed_samples = s->delayed_samples;

    ret = opus_rc_init(&s->rc, data, size);
    if (ret < 0)
        return ret;

    /* decode the silk frame */
    if (s->packet.mode == OPUS_MODE_SILK || s->packet.mode == OPUS_MODE_HYBRID) {
        if (!swr_is_initialized(s->swr)) {
            ret = opus_init_resample(s);
            if (ret < 0)
                return ret;
        }

        samples = ff_silk_decode_superframe(s->silk, &s->rc, s->silk_output,
                                            FFMIN(s->packet.bandwidth, OPUS_BANDWIDTH_WIDEBAND),
                                            s->packet.stereo + 1,
                                            silk_frame_duration_ms[s->packet.config]);
        if (samples < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Error decoding a SILK frame.\n");
            return samples;
        }
        samples = swr_convert(s->swr,
                              (uint8_t**)s->out, s->packet.frame_duration,
                              (const uint8_t**)s->silk_output, samples);
        if (samples < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Error resampling SILK data.\n");
            return samples;
        }
        av_assert2((samples & 7) == 0);
        s->delayed_samples += s->packet.frame_duration - samples;
    } else
        ff_silk_flush(s->silk);

    // decode redundancy information
    consumed = opus_rc_tell(&s->rc);
    if (s->packet.mode == OPUS_MODE_HYBRID && consumed + 37 <= size * 8)
        redundancy = opus_rc_p2model(&s->rc, 12);
    else if (s->packet.mode == OPUS_MODE_SILK && consumed + 17 <= size * 8)
        redundancy = 1;

    if (redundancy) {
        redundancy_pos = opus_rc_p2model(&s->rc, 1);

        if (s->packet.mode == OPUS_MODE_HYBRID)
            redundancy_size = opus_rc_unimodel(&s->rc, 256) + 2;
        else
            redundancy_size = size - (consumed + 7) / 8;
        size -= redundancy_size;
        if (size < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Invalid redundancy frame size.\n");
            return AVERROR_INVALIDDATA;
        }

        if (redundancy_pos) {
            ret = opus_decode_redundancy(s, data + size, redundancy_size);
            if (ret < 0)
                return ret;
            ff_celt_flush(s->celt);
        }
    }

    /* decode the CELT frame */
    if (s->packet.mode == OPUS_MODE_CELT || s->packet.mode == OPUS_MODE_HYBRID) {
        float *out_tmp[2] = { s->out[0], s->out[1] };
        float **dst = (s->packet.mode == OPUS_MODE_CELT) ?
                      out_tmp : s->celt_output;
        int celt_output_samples = samples;
        int delay_samples = av_audio_fifo_size(s->celt_delay);

        if (delay_samples) {
            if (s->packet.mode == OPUS_MODE_HYBRID) {
                av_audio_fifo_read(s->celt_delay, (void**)s->celt_output, delay_samples);

                for (i = 0; i < s->output_channels; i++) {
                    s->fdsp->vector_fmac_scalar(out_tmp[i], s->celt_output[i], 1.0,
                                                delay_samples);
                    out_tmp[i] += delay_samples;
                }
                celt_output_samples -= delay_samples;
            } else {
                av_log(s->avctx, AV_LOG_WARNING,
                       "Spurious CELT delay samples present.\n");
                av_audio_fifo_drain(s->celt_delay, delay_samples);
                if (s->avctx->err_recognition & AV_EF_EXPLODE)
                    return AVERROR_BUG;
            }
        }

        opus_raw_init(&s->rc, data + size, size);

        ret = ff_celt_decode_frame(s->celt, &s->rc, dst,
                                   s->packet.stereo + 1,
                                   s->packet.frame_duration,
                                   (s->packet.mode == OPUS_MODE_HYBRID) ? 17 : 0,
                                   celt_band_end[s->packet.bandwidth]);
        if (ret < 0)
            return ret;

        if (s->packet.mode == OPUS_MODE_HYBRID) {
            int celt_delay = s->packet.frame_duration - celt_output_samples;
            void *delaybuf[2] = { s->celt_output[0] + celt_output_samples,
                                  s->celt_output[1] + celt_output_samples };

            for (i = 0; i < s->output_channels; i++) {
                s->fdsp->vector_fmac_scalar(out_tmp[i],
                                            s->celt_output[i], 1.0,
                                            celt_output_samples);
            }

            ret = av_audio_fifo_write(s->celt_delay, delaybuf, celt_delay);
            if (ret < 0)
                return ret;
        }
    } else
        ff_celt_flush(s->celt);

    if (s->redundancy_idx) {
        for (i = 0; i < s->output_channels; i++)
            opus_fade(s->out[i], s->out[i],
                      s->redundancy_output[i] + 120 + s->redundancy_idx,
                      ff_celt_window2 + s->redundancy_idx, 120 - s->redundancy_idx);
        s->redundancy_idx = 0;
    }
    if (redundancy) {
        if (!redundancy_pos) {
            ff_celt_flush(s->celt);
            ret = opus_decode_redundancy(s, data + size, redundancy_size);
            if (ret < 0)
                return ret;

            for (i = 0; i < s->output_channels; i++) {
                opus_fade(s->out[i] + samples - 120 + delayed_samples,
                          s->out[i] + samples - 120 + delayed_samples,
                          s->redundancy_output[i] + 120,
                          ff_celt_window2, 120 - delayed_samples);
                if (delayed_samples)
                    s->redundancy_idx = 120 - delayed_samples;
            }
        } else {
            for (i = 0; i < s->output_channels; i++) {
                memcpy(s->out[i] + delayed_samples, s->redundancy_output[i], 120 * sizeof(float));
                opus_fade(s->out[i] + 120 + delayed_samples,
                          s->redundancy_output[i] + 120,
                          s->out[i] + 120 + delayed_samples,
                          ff_celt_window2, 120);
            }
        }
    }

    return samples;
}

static int opus_decode_subpacket(OpusStreamContext *s,
                                 const uint8_t *buf, int buf_size,
                                 int nb_samples)
{
    int output_samples = 0;
    int flush_needed   = 0;
    int i, j, ret;

    /* check if we need to flush the resampler */
    if (swr_is_initialized(s->swr)) {
        if (buf) {
            int64_t cur_samplerate;
            av_opt_get_int(s->swr, "in_sample_rate", 0, &cur_samplerate);
            flush_needed = (s->packet.mode == OPUS_MODE_CELT) || (cur_samplerate != s->silk_samplerate);
        } else {
            flush_needed = !!s->delayed_samples;
        }
    }

    if (!buf && !flush_needed)
        return 0;

    /* use dummy output buffers if the channel is not mapped to anything */
    if (!s->out[0] ||
        (s->output_channels == 2 && !s->out[1])) {
        av_fast_malloc(&s->out_dummy, &s->out_dummy_allocated_size, s->out_size);
        if (!s->out_dummy)
            return AVERROR(ENOMEM);
        if (!s->out[0])
            s->out[0] = s->out_dummy;
        if (!s->out[1])
            s->out[1] = s->out_dummy;
    }

    /* flush the resampler if necessary */
    if (flush_needed) {
        ret = opus_flush_resample(s, s->delayed_samples);
        if (ret < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Error flushing the resampler.\n");
            return ret;
        }
        swr_close(s->swr);
        output_samples += s->delayed_samples;
        s->delayed_samples = 0;

        if (!buf)
            goto finish;
    }

    /* decode all the frames in the packet */
    for (i = 0; i < s->packet.frame_count; i++) {
        int size = s->packet.frame_size[i];
        int samples = opus_decode_frame(s, buf + s->packet.frame_offset[i], size);

        if (samples < 0) {
            av_log(s->avctx, AV_LOG_ERROR, "Error decoding an Opus frame.\n");
            if (s->avctx->err_recognition & AV_EF_EXPLODE)
                return samples;

            for (j = 0; j < s->output_channels; j++)
                memset(s->out[j], 0, s->packet.frame_duration * sizeof(float));
            samples = s->packet.frame_duration;
        }
        output_samples += samples;

        for (j = 0; j < s->output_channels; j++)
            s->out[j] += samples;
        s->out_size -= samples * sizeof(float);
    }

finish:
    s->out[0] = s->out[1] = NULL;
    s->out_size = 0;

    return output_samples;
}

static int opus_decode_packet(AVCodecContext *avctx, void *data,
                              int *got_frame_ptr, AVPacket *avpkt)
{
    OpusContext *c      = avctx->priv_data;
    AVFrame *frame      = data;
    const uint8_t *buf  = avpkt->data;
    int buf_size        = avpkt->size;
    int coded_samples   = 0;
    int decoded_samples = 0;
    int i, ret;

    /* decode the header of the first sub-packet to find out the sample count */
    if (buf) {
        OpusPacket *pkt = &c->streams[0].packet;
        ret = ff_opus_parse_packet(pkt, buf, buf_size, c->nb_streams > 1);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error parsing the packet header.\n");
            return ret;
        }
        coded_samples += pkt->frame_count * pkt->frame_duration;
        c->streams[0].silk_samplerate = get_silk_samplerate(pkt->config);
    }

    frame->nb_samples = coded_samples + c->streams[0].delayed_samples;

    /* no input or buffered data => nothing to do */
    if (!frame->nb_samples) {
        *got_frame_ptr = 0;
        return 0;
    }

    /* setup the data buffers */
    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return ret;
    }
    frame->nb_samples = 0;

    for (i = 0; i < avctx->channels; i++) {
        ChannelMap *map = &c->channel_maps[i];
        if (!map->copy)
            c->streams[map->stream_idx].out[map->channel_idx] = (float*)frame->extended_data[i];
    }

    for (i = 0; i < c->nb_streams; i++)
        c->streams[i].out_size = frame->linesize[0];

    /* decode each sub-packet */
    for (i = 0; i < c->nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];

        if (i && buf) {
            ret = ff_opus_parse_packet(&s->packet, buf, buf_size, i != c->nb_streams - 1);
            if (ret < 0) {
                av_log(avctx, AV_LOG_ERROR, "Error parsing the packet header.\n");
                return ret;
            }
            if (coded_samples != s->packet.frame_count * s->packet.frame_duration) {
                av_log(avctx, AV_LOG_ERROR,
                       "Mismatching coded sample count in substream %d.\n", i);
                return AVERROR_INVALIDDATA;
            }

            s->silk_samplerate = get_silk_samplerate(s->packet.config);
        }

        ret = opus_decode_subpacket(&c->streams[i], buf,
                                    s->packet.data_size, coded_samples);
        if (ret < 0)
            return ret;
        if (decoded_samples && ret != decoded_samples) {
            av_log(avctx, AV_LOG_ERROR, "Different numbers of decoded samples "
                   "in a multi-channel stream\n");
            return AVERROR_INVALIDDATA;
        }
        decoded_samples = ret;
        buf      += s->packet.packet_size;
        buf_size -= s->packet.packet_size;
    }

    for (i = 0; i < avctx->channels; i++) {
        ChannelMap *map = &c->channel_maps[i];

        /* handle copied channels */
        if (map->copy) {
            memcpy(frame->extended_data[i],
                   frame->extended_data[map->copy_idx],
                   frame->linesize[0]);
        } else if (map->silence) {
            memset(frame->extended_data[i], 0, frame->linesize[0]);
        }

        if (c->gain_i) {
            c->fdsp->vector_fmul_scalar((float*)frame->extended_data[i],
                                       (float*)frame->extended_data[i],
                                       c->gain, FFALIGN(decoded_samples, 8));
        }
    }

    frame->nb_samples = decoded_samples;
    *got_frame_ptr    = !!decoded_samples;

    return avpkt->size;
}

static av_cold void opus_decode_flush(AVCodecContext *ctx)
{
    OpusContext *c = ctx->priv_data;
    int i;

    for (i = 0; i < c->nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];

        memset(&s->packet, 0, sizeof(s->packet));
        s->delayed_samples = 0;

        if (s->celt_delay)
            av_audio_fifo_drain(s->celt_delay, av_audio_fifo_size(s->celt_delay));
        swr_close(s->swr);

        ff_silk_flush(s->silk);
        ff_celt_flush(s->celt);
    }
}

static av_cold int opus_decode_close(AVCodecContext *avctx)
{
    OpusContext *c = avctx->priv_data;
    int i;

    for (i = 0; i < c->nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];

        ff_silk_free(&s->silk);
        ff_celt_free(&s->celt);

        av_freep(&s->out_dummy);
        s->out_dummy_allocated_size = 0;

        av_audio_fifo_free(s->celt_delay);
        swr_free(&s->swr);
    }

    av_freep(&c->streams);
    c->nb_streams = 0;

    av_freep(&c->channel_maps);
    av_freep(&c->fdsp);

    return 0;
}

static av_cold int opus_decode_init(AVCodecContext *avctx)
{
    OpusContext *c = avctx->priv_data;
    int ret, i, j;

    avctx->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    avctx->sample_rate = 48000;

    c->fdsp = avpriv_float_dsp_alloc(0);
    if (!c->fdsp)
        return AVERROR(ENOMEM);

    /* find out the channel configuration */
    ret = ff_opus_parse_extradata(avctx, c);
    if (ret < 0)
        return ret;

    /* allocate and init each independent decoder */
    c->streams = av_mallocz_array(c->nb_streams, sizeof(*c->streams));
    if (!c->streams) {
        c->nb_streams = 0;
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (i = 0; i < c->nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];
        uint64_t layout;

        s->output_channels = (i < c->nb_stereo_streams) ? 2 : 1;

        s->avctx = avctx;

        for (j = 0; j < s->output_channels; j++) {
            s->silk_output[j]       = s->silk_buf[j];
            s->celt_output[j]       = s->celt_buf[j];
            s->redundancy_output[j] = s->redundancy_buf[j];
        }

        s->fdsp = c->fdsp;

        s->swr =swr_alloc();
        if (!s->swr)
            goto fail;

        layout = (s->output_channels == 1) ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
        av_opt_set_int(s->swr, "in_sample_fmt",      avctx->sample_fmt,  0);
        av_opt_set_int(s->swr, "out_sample_fmt",     avctx->sample_fmt,  0);
        av_opt_set_int(s->swr, "in_channel_layout",  layout,             0);
        av_opt_set_int(s->swr, "out_channel_layout", layout,             0);
        av_opt_set_int(s->swr, "out_sample_rate",    avctx->sample_rate, 0);
        av_opt_set_int(s->swr, "filter_size",        16,                 0);

        ret = ff_silk_init(avctx, &s->silk, s->output_channels);
        if (ret < 0)
            goto fail;

        ret = ff_celt_init(avctx, &s->celt, s->output_channels);
        if (ret < 0)
            goto fail;

        s->celt_delay = av_audio_fifo_alloc(avctx->sample_fmt,
                                            s->output_channels, 1024);
        if (!s->celt_delay) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    return 0;
fail:
    opus_decode_close(avctx);
    return ret;
}

AVCodec ff_opus_decoder = {
    .name            = "opus",
    .long_name       = NULL_IF_CONFIG_SMALL("Opus"),
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_OPUS,
    .priv_data_size  = sizeof(OpusContext),
    .init            = opus_decode_init,
    .close           = opus_decode_close,
    .decode          = opus_decode_packet,
    .flush           = opus_decode_flush,
    .capabilities    = CODEC_CAP_DR1 | CODEC_CAP_DELAY,
};
