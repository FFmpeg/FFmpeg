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
#include "libavutil/ffmath.h"
#include "libavutil/float_dsp.h"
#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"

#include "libswresample/swresample.h"

#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"
#include "opus.h"
#include "tab.h"
#include "celt.h"
#include "parse.h"
#include "rc.h"
#include "silk.h"

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

typedef struct OpusStreamContext {
    AVCodecContext *avctx;
    int output_channels;

    /* number of decoded samples for this stream */
    int decoded_samples;
    /* current output buffers for this stream */
    float *out[2];
    int out_size;
    /* Buffer with samples from this stream for synchronizing
     * the streams when they have different resampling delays */
    AVAudioFifo *sync_buffer;

    OpusRangeCoder rc;
    OpusRangeCoder redundancy_rc;
    SilkContext *silk;
    CeltFrame *celt;
    AVFloatDSPContext *fdsp;

    float silk_buf[2][960];
    float *silk_output[2];
    DECLARE_ALIGNED(32, float, celt_buf)[2][960];
    float *celt_output[2];

    DECLARE_ALIGNED(32, float, redundancy_buf)[2][960];
    float *redundancy_output[2];

    /* buffers for the next samples to be decoded */
    float *cur_out[2];
    int remaining_out_size;

    float *out_dummy;
    int    out_dummy_allocated_size;

    SwrContext *swr;
    AVAudioFifo *celt_delay;
    int silk_samplerate;
    /* number of samples we still want to get from the resampler */
    int delayed_samples;

    OpusPacket packet;

    int redundancy_idx;
} OpusStreamContext;

typedef struct OpusContext {
    AVClass *av_class;

    struct OpusStreamContext *streams;
    int apply_phase_inv;

    AVFloatDSPContext *fdsp;
    float   gain;

    OpusParseContext p;
} OpusContext;

static int get_silk_samplerate(int config)
{
    if (config < 4)
        return 8000;
    else if (config < 8)
        return 12000;
    return 16000;
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
                      (uint8_t**)s->cur_out, nb_samples,
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
            s->fdsp->vector_fmac_scalar(s->cur_out[i],
                                        s->celt_output[i], 1.0,
                                        nb_samples);
        }
    }

    if (s->redundancy_idx) {
        for (i = 0; i < s->output_channels; i++)
            opus_fade(s->cur_out[i], s->cur_out[i],
                      s->redundancy_output[i] + 120 + s->redundancy_idx,
                      ff_celt_window2 + s->redundancy_idx, 120 - s->redundancy_idx);
        s->redundancy_idx = 0;
    }

    s->cur_out[0]         += nb_samples;
    s->cur_out[1]         += nb_samples;
    s->remaining_out_size -= nb_samples * sizeof(float);

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
    int ret = ff_opus_rc_dec_init(&s->redundancy_rc, data, size);
    if (ret < 0)
        goto fail;
    ff_opus_rc_dec_raw_init(&s->redundancy_rc, data + size, size);

    ret = ff_celt_decode_frame(s->celt, &s->redundancy_rc,
                               s->redundancy_output,
                               s->packet.stereo + 1, 240,
                               0, ff_celt_band_end[s->packet.bandwidth]);
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

    ret = ff_opus_rc_dec_init(&s->rc, data, size);
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
                              (uint8_t**)s->cur_out, s->packet.frame_duration,
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
        redundancy = ff_opus_rc_dec_log(&s->rc, 12);
    else if (s->packet.mode == OPUS_MODE_SILK && consumed + 17 <= size * 8)
        redundancy = 1;

    if (redundancy) {
        redundancy_pos = ff_opus_rc_dec_log(&s->rc, 1);

        if (s->packet.mode == OPUS_MODE_HYBRID)
            redundancy_size = ff_opus_rc_dec_uint(&s->rc, 256) + 2;
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
        float *out_tmp[2] = { s->cur_out[0], s->cur_out[1] };
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

        ff_opus_rc_dec_raw_init(&s->rc, data + size, size);

        ret = ff_celt_decode_frame(s->celt, &s->rc, dst,
                                   s->packet.stereo + 1,
                                   s->packet.frame_duration,
                                   (s->packet.mode == OPUS_MODE_HYBRID) ? 17 : 0,
                                   ff_celt_band_end[s->packet.bandwidth]);
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
            opus_fade(s->cur_out[i], s->cur_out[i],
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
                opus_fade(s->cur_out[i] + samples - 120 + delayed_samples,
                          s->cur_out[i] + samples - 120 + delayed_samples,
                          s->redundancy_output[i] + 120,
                          ff_celt_window2, 120 - delayed_samples);
                if (delayed_samples)
                    s->redundancy_idx = 120 - delayed_samples;
            }
        } else {
            for (i = 0; i < s->output_channels; i++) {
                memcpy(s->cur_out[i] + delayed_samples, s->redundancy_output[i], 120 * sizeof(float));
                opus_fade(s->cur_out[i] + 120 + delayed_samples,
                          s->redundancy_output[i] + 120,
                          s->cur_out[i] + 120 + delayed_samples,
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

    s->cur_out[0]         = s->out[0];
    s->cur_out[1]         = s->out[1];
    s->remaining_out_size = s->out_size;

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
    if (!s->cur_out[0] ||
        (s->output_channels == 2 && !s->cur_out[1])) {
        av_fast_malloc(&s->out_dummy, &s->out_dummy_allocated_size,
                       s->remaining_out_size);
        if (!s->out_dummy)
            return AVERROR(ENOMEM);
        if (!s->cur_out[0])
            s->cur_out[0] = s->out_dummy;
        if (!s->cur_out[1])
            s->cur_out[1] = s->out_dummy;
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
                memset(s->cur_out[j], 0, s->packet.frame_duration * sizeof(float));
            samples = s->packet.frame_duration;
        }
        output_samples += samples;

        for (j = 0; j < s->output_channels; j++)
            s->cur_out[j] += samples;
        s->remaining_out_size -= samples * sizeof(float);
    }

finish:
    s->cur_out[0] = s->cur_out[1] = NULL;
    s->remaining_out_size = 0;

    return output_samples;
}

static int opus_decode_packet(AVCodecContext *avctx, AVFrame *frame,
                              int *got_frame_ptr, AVPacket *avpkt)
{
    OpusContext *c      = avctx->priv_data;
    const uint8_t *buf  = avpkt->data;
    int buf_size        = avpkt->size;
    int coded_samples   = 0;
    int decoded_samples = INT_MAX;
    int delayed_samples = 0;
    int i, ret;

    /* calculate the number of delayed samples */
    for (int i = 0; i < c->p.nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];
        s->out[0] =
        s->out[1] = NULL;
        delayed_samples = FFMAX(delayed_samples,
                                s->delayed_samples + av_audio_fifo_size(s->sync_buffer));
    }

    /* decode the header of the first sub-packet to find out the sample count */
    if (buf) {
        OpusPacket *pkt = &c->streams[0].packet;
        ret = ff_opus_parse_packet(pkt, buf, buf_size, c->p.nb_streams > 1);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error parsing the packet header.\n");
            return ret;
        }
        coded_samples += pkt->frame_count * pkt->frame_duration;
        c->streams[0].silk_samplerate = get_silk_samplerate(pkt->config);
    }

    frame->nb_samples = coded_samples + delayed_samples;

    /* no input or buffered data => nothing to do */
    if (!frame->nb_samples) {
        *got_frame_ptr = 0;
        return 0;
    }

    /* setup the data buffers */
    ret = ff_get_buffer(avctx, frame, 0);
    if (ret < 0)
        return ret;
    frame->nb_samples = 0;

    for (i = 0; i < avctx->ch_layout.nb_channels; i++) {
        ChannelMap *map = &c->p.channel_maps[i];
        if (!map->copy)
            c->streams[map->stream_idx].out[map->channel_idx] = (float*)frame->extended_data[i];
    }

    /* read the data from the sync buffers */
    for (int i = 0; i < c->p.nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];
        float          **out = s->out;
        int sync_size = av_audio_fifo_size(s->sync_buffer);

        float sync_dummy[32];
        int out_dummy = (!out[0]) | ((!out[1]) << 1);

        if (!out[0])
            out[0] = sync_dummy;
        if (!out[1])
            out[1] = sync_dummy;
        if (out_dummy && sync_size > FF_ARRAY_ELEMS(sync_dummy))
            return AVERROR_BUG;

        ret = av_audio_fifo_read(s->sync_buffer, (void**)out, sync_size);
        if (ret < 0)
            return ret;

        if (out_dummy & 1)
            out[0] = NULL;
        else
            out[0] += ret;
        if (out_dummy & 2)
            out[1] = NULL;
        else
            out[1] += ret;

        s->out_size = frame->linesize[0] - ret * sizeof(float);
    }

    /* decode each sub-packet */
    for (int i = 0; i < c->p.nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];

        if (i && buf) {
            ret = ff_opus_parse_packet(&s->packet, buf, buf_size, i != c->p.nb_streams - 1);
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

        ret = opus_decode_subpacket(&c->streams[i], buf, s->packet.data_size,
                                    coded_samples);
        if (ret < 0)
            return ret;
        s->decoded_samples = ret;
        decoded_samples       = FFMIN(decoded_samples, ret);

        buf      += s->packet.packet_size;
        buf_size -= s->packet.packet_size;
    }

    /* buffer the extra samples */
    for (int i = 0; i < c->p.nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];
        int   buffer_samples = s->decoded_samples - decoded_samples;
        if (buffer_samples) {
            float *buf[2] = { s->out[0] ? s->out[0] : (float*)frame->extended_data[0],
                              s->out[1] ? s->out[1] : (float*)frame->extended_data[0] };
            buf[0] += decoded_samples;
            buf[1] += decoded_samples;
            ret = av_audio_fifo_write(s->sync_buffer, (void**)buf, buffer_samples);
            if (ret < 0)
                return ret;
        }
    }

    for (i = 0; i < avctx->ch_layout.nb_channels; i++) {
        ChannelMap *map = &c->p.channel_maps[i];

        /* handle copied channels */
        if (map->copy) {
            memcpy(frame->extended_data[i],
                   frame->extended_data[map->copy_idx],
                   frame->linesize[0]);
        } else if (map->silence) {
            memset(frame->extended_data[i], 0, frame->linesize[0]);
        }

        if (c->p.gain_i && decoded_samples > 0) {
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

    for (int i = 0; i < c->p.nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];

        memset(&s->packet, 0, sizeof(s->packet));
        s->delayed_samples = 0;

        av_audio_fifo_drain(s->celt_delay, av_audio_fifo_size(s->celt_delay));
        swr_close(s->swr);

        av_audio_fifo_drain(s->sync_buffer, av_audio_fifo_size(s->sync_buffer));

        ff_silk_flush(s->silk);
        ff_celt_flush(s->celt);
    }
}

static av_cold int opus_decode_close(AVCodecContext *avctx)
{
    OpusContext *c = avctx->priv_data;

    for (int i = 0; i < c->p.nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];

        ff_silk_free(&s->silk);
        ff_celt_free(&s->celt);

        av_freep(&s->out_dummy);
        s->out_dummy_allocated_size = 0;

        av_audio_fifo_free(s->sync_buffer);
        av_audio_fifo_free(s->celt_delay);
        swr_free(&s->swr);
    }

    av_freep(&c->streams);

    c->p.nb_streams = 0;

    av_freep(&c->p.channel_maps);
    av_freep(&c->fdsp);

    return 0;
}

static av_cold int opus_decode_init(AVCodecContext *avctx)
{
    OpusContext *c = avctx->priv_data;
    int ret;

    avctx->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    avctx->sample_rate = 48000;

    c->fdsp = avpriv_float_dsp_alloc(0);
    if (!c->fdsp)
        return AVERROR(ENOMEM);

    /* find out the channel configuration */
    ret = ff_opus_parse_extradata(avctx, &c->p);
    if (ret < 0)
        return ret;
    if (c->p.gain_i)
        c->gain = ff_exp10(c->p.gain_i / (20.0 * 256));

    /* allocate and init each independent decoder */
    c->streams = av_calloc(c->p.nb_streams, sizeof(*c->streams));
    if (!c->streams) {
        c->p.nb_streams = 0;
        return AVERROR(ENOMEM);
    }

    for (int i = 0; i < c->p.nb_streams; i++) {
        OpusStreamContext *s = &c->streams[i];
        AVChannelLayout layout;

        s->output_channels = (i < c->p.nb_stereo_streams) ? 2 : 1;

        s->avctx = avctx;

        for (int j = 0; j < s->output_channels; j++) {
            s->silk_output[j]       = s->silk_buf[j];
            s->celt_output[j]       = s->celt_buf[j];
            s->redundancy_output[j] = s->redundancy_buf[j];
        }

        s->fdsp = c->fdsp;

        s->swr =swr_alloc();
        if (!s->swr)
            return AVERROR(ENOMEM);

        layout = (s->output_channels == 1) ? (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO :
                                             (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        av_opt_set_int(s->swr, "in_sample_fmt",      avctx->sample_fmt,  0);
        av_opt_set_int(s->swr, "out_sample_fmt",     avctx->sample_fmt,  0);
        av_opt_set_chlayout(s->swr, "in_chlayout",   &layout,            0);
        av_opt_set_chlayout(s->swr, "out_chlayout",  &layout,            0);
        av_opt_set_int(s->swr, "out_sample_rate",    avctx->sample_rate, 0);
        av_opt_set_int(s->swr, "filter_size",        16,                 0);

        ret = ff_silk_init(avctx, &s->silk, s->output_channels);
        if (ret < 0)
            return ret;

        ret = ff_celt_init(avctx, &s->celt, s->output_channels, c->apply_phase_inv);
        if (ret < 0)
            return ret;

        s->celt_delay = av_audio_fifo_alloc(avctx->sample_fmt,
                                            s->output_channels, 1024);
        if (!s->celt_delay)
            return AVERROR(ENOMEM);

        s->sync_buffer = av_audio_fifo_alloc(avctx->sample_fmt,
                                             s->output_channels, 32);
        if (!s->sync_buffer)
            return AVERROR(ENOMEM);
    }

    return 0;
}

#define OFFSET(x) offsetof(OpusContext, x)
#define AD AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption opus_options[] = {
    { "apply_phase_inv", "Apply intensity stereo phase inversion", OFFSET(apply_phase_inv), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, AD },
    { NULL },
};

static const AVClass opus_class = {
    .class_name = "Opus Decoder",
    .item_name  = av_default_item_name,
    .option     = opus_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_opus_decoder = {
    .p.name          = "opus",
    CODEC_LONG_NAME("Opus"),
    .p.priv_class    = &opus_class,
    .p.type          = AVMEDIA_TYPE_AUDIO,
    .p.id            = AV_CODEC_ID_OPUS,
    .priv_data_size  = sizeof(OpusContext),
    .init            = opus_decode_init,
    .close           = opus_decode_close,
    FF_CODEC_DECODE_CB(opus_decode_packet),
    .flush           = opus_decode_flush,
    .p.capabilities  = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY | AV_CODEC_CAP_CHANNEL_CONF,
    .caps_internal   = FF_CODEC_CAP_INIT_CLEANUP,
};
