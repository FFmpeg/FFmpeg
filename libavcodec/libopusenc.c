/*
 * Opus encoder using libopus
 * Copyright (c) 2012 Nathan Caldwell
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

#include <opus.h>
#include <opus_multistream.h>

#include "libavutil/opt.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "libopus.h"
#include "vorbis.h"
#include "audio_frame_queue.h"

typedef struct LibopusEncOpts {
    int vbr;
    int application;
    int packet_loss;
    int complexity;
    float frame_duration;
    int packet_size;
    int max_bandwidth;
} LibopusEncOpts;

typedef struct LibopusEncContext {
    AVClass *class;
    OpusMSEncoder *enc;
    int stream_count;
    uint8_t *samples;
    LibopusEncOpts opts;
    AudioFrameQueue afq;
} LibopusEncContext;

static const uint8_t opus_coupled_streams[8] = {
    0, 1, 1, 2, 2, 2, 2, 3
};

/* Opus internal to Vorbis channel order mapping written in the header */
static const uint8_t opus_vorbis_channel_map[8][8] = {
    { 0 },
    { 0, 1 },
    { 0, 2, 1 },
    { 0, 1, 2, 3 },
    { 0, 4, 1, 2, 3 },
    { 0, 4, 1, 2, 3, 5 },
    { 0, 4, 1, 2, 3, 5, 6 },
    { 0, 6, 1, 2, 3, 4, 5, 7 },
};

/* libavcodec to libopus channel order mapping, passed to libopus */
static const uint8_t libavcodec_libopus_channel_map[8][8] = {
    { 0 },
    { 0, 1 },
    { 0, 1, 2 },
    { 0, 1, 2, 3 },
    { 0, 1, 3, 4, 2 },
    { 0, 1, 4, 5, 2, 3 },
    { 0, 1, 5, 6, 2, 4, 3 },
    { 0, 1, 6, 7, 4, 5, 2, 3 },
};

static void libopus_write_header(AVCodecContext *avctx, int stream_count,
                                 int coupled_stream_count,
                                 const uint8_t *channel_mapping)
{
    uint8_t *p   = avctx->extradata;
    int channels = avctx->channels;

    bytestream_put_buffer(&p, "OpusHead", 8);
    bytestream_put_byte(&p, 1); /* Version */
    bytestream_put_byte(&p, channels);
    bytestream_put_le16(&p, avctx->initial_padding); /* Lookahead samples at 48kHz */
    bytestream_put_le32(&p, avctx->sample_rate); /* Original sample rate */
    bytestream_put_le16(&p, 0); /* Gain of 0dB is recommended. */

    /* Channel mapping */
    if (channels > 2) {
        bytestream_put_byte(&p, channels <= 8 ? 1 : 255);
        bytestream_put_byte(&p, stream_count);
        bytestream_put_byte(&p, coupled_stream_count);
        bytestream_put_buffer(&p, channel_mapping, channels);
    } else {
        bytestream_put_byte(&p, 0);
    }
}

static int libopus_configure_encoder(AVCodecContext *avctx, OpusMSEncoder *enc,
                                     LibopusEncOpts *opts)
{
    int ret;

    if (avctx->global_quality) {
        av_log(avctx, AV_LOG_ERROR,
               "Quality-based encoding not supported, "
               "please specify a bitrate and VBR setting.\n");
        return AVERROR(EINVAL);
    }

    ret = opus_multistream_encoder_ctl(enc, OPUS_SET_BITRATE(avctx->bit_rate));
    if (ret != OPUS_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to set bitrate: %s\n", opus_strerror(ret));
        return ret;
    }

    ret = opus_multistream_encoder_ctl(enc,
                                       OPUS_SET_COMPLEXITY(opts->complexity));
    if (ret != OPUS_OK)
        av_log(avctx, AV_LOG_WARNING,
               "Unable to set complexity: %s\n", opus_strerror(ret));

    ret = opus_multistream_encoder_ctl(enc, OPUS_SET_VBR(!!opts->vbr));
    if (ret != OPUS_OK)
        av_log(avctx, AV_LOG_WARNING,
               "Unable to set VBR: %s\n", opus_strerror(ret));

    ret = opus_multistream_encoder_ctl(enc,
                                       OPUS_SET_VBR_CONSTRAINT(opts->vbr == 2));
    if (ret != OPUS_OK)
        av_log(avctx, AV_LOG_WARNING,
               "Unable to set constrained VBR: %s\n", opus_strerror(ret));

    ret = opus_multistream_encoder_ctl(enc,
                                       OPUS_SET_PACKET_LOSS_PERC(opts->packet_loss));
    if (ret != OPUS_OK)
        av_log(avctx, AV_LOG_WARNING,
               "Unable to set expected packet loss percentage: %s\n",
               opus_strerror(ret));

    if (avctx->cutoff) {
        ret = opus_multistream_encoder_ctl(enc,
                                           OPUS_SET_MAX_BANDWIDTH(opts->max_bandwidth));
        if (ret != OPUS_OK)
            av_log(avctx, AV_LOG_WARNING,
                   "Unable to set maximum bandwidth: %s\n", opus_strerror(ret));
    }

    return OPUS_OK;
}

static av_cold int libopus_encode_init(AVCodecContext *avctx)
{
    LibopusEncContext *opus = avctx->priv_data;
    const uint8_t *channel_mapping;
    OpusMSEncoder *enc;
    int ret = OPUS_OK;
    int coupled_stream_count, header_size, frame_size;

    coupled_stream_count = opus_coupled_streams[avctx->channels - 1];
    opus->stream_count   = avctx->channels - coupled_stream_count;
    channel_mapping      = libavcodec_libopus_channel_map[avctx->channels - 1];

    /* FIXME: Opus can handle up to 255 channels. However, the mapping for
     * anything greater than 8 is undefined. */
    if (avctx->channels > 8) {
        av_log(avctx, AV_LOG_ERROR,
               "Channel layout undefined for %d channels.\n", avctx->channels);
        return AVERROR_PATCHWELCOME;
    }
    if (!avctx->bit_rate) {
        /* Sane default copied from opusenc */
        avctx->bit_rate = 64000 * opus->stream_count +
                          32000 * coupled_stream_count;
        av_log(avctx, AV_LOG_WARNING,
               "No bit rate set. Defaulting to %d bps.\n", avctx->bit_rate);
    }

    if (avctx->bit_rate < 500 || avctx->bit_rate > 256000 * avctx->channels) {
        av_log(avctx, AV_LOG_ERROR, "The bit rate %d bps is unsupported. "
               "Please choose a value between 500 and %d.\n", avctx->bit_rate,
               256000 * avctx->channels);
        return AVERROR(EINVAL);
    }

    frame_size = opus->opts.frame_duration * 48000 / 1000;
    switch (frame_size) {
    case 120:
    case 240:
        if (opus->opts.application != OPUS_APPLICATION_RESTRICTED_LOWDELAY)
            av_log(avctx, AV_LOG_WARNING,
                   "LPC mode cannot be used with a frame duration of less "
                   "than 10ms. Enabling restricted low-delay mode.\n"
                   "Use a longer frame duration if this is not what you want.\n");
        /* Frame sizes less than 10 ms can only use MDCT mode, so switching to
         * RESTRICTED_LOWDELAY avoids an unnecessary extra 2.5ms lookahead. */
        opus->opts.application = OPUS_APPLICATION_RESTRICTED_LOWDELAY;
    case 480:
    case 960:
    case 1920:
    case 2880:
        opus->opts.packet_size =
        avctx->frame_size      = frame_size * avctx->sample_rate / 48000;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid frame duration: %g.\n"
               "Frame duration must be exactly one of: 2.5, 5, 10, 20, 40 or 60.\n",
               opus->opts.frame_duration);
        return AVERROR(EINVAL);
    }

    if (avctx->compression_level < 0 || avctx->compression_level > 10) {
        av_log(avctx, AV_LOG_WARNING,
               "Compression level must be in the range 0 to 10. "
               "Defaulting to 10.\n");
        opus->opts.complexity = 10;
    } else {
        opus->opts.complexity = avctx->compression_level;
    }

    if (avctx->cutoff) {
        switch (avctx->cutoff) {
        case  4000:
            opus->opts.max_bandwidth = OPUS_BANDWIDTH_NARROWBAND;
            break;
        case  6000:
            opus->opts.max_bandwidth = OPUS_BANDWIDTH_MEDIUMBAND;
            break;
        case  8000:
            opus->opts.max_bandwidth = OPUS_BANDWIDTH_WIDEBAND;
            break;
        case 12000:
            opus->opts.max_bandwidth = OPUS_BANDWIDTH_SUPERWIDEBAND;
            break;
        case 20000:
            opus->opts.max_bandwidth = OPUS_BANDWIDTH_FULLBAND;
            break;
        default:
            av_log(avctx, AV_LOG_WARNING,
                   "Invalid frequency cutoff: %d. Using default maximum bandwidth.\n"
                   "Cutoff frequency must be exactly one of: 4000, 6000, 8000, 12000 or 20000.\n",
                   avctx->cutoff);
            avctx->cutoff = 0;
        }
    }

    enc = opus_multistream_encoder_create(avctx->sample_rate, avctx->channels,
                                          opus->stream_count,
                                          coupled_stream_count,
                                          channel_mapping,
                                          opus->opts.application, &ret);
    if (ret != OPUS_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create encoder: %s\n", opus_strerror(ret));
        return ff_opus_error_to_averror(ret);
    }

    ret = libopus_configure_encoder(avctx, enc, &opus->opts);
    if (ret != OPUS_OK) {
        ret = ff_opus_error_to_averror(ret);
        goto fail;
    }

    header_size = 19 + (avctx->channels > 2 ? 2 + avctx->channels : 0);
    avctx->extradata = av_malloc(header_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate extradata.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    avctx->extradata_size = header_size;

    opus->samples = av_mallocz(frame_size * avctx->channels *
                               av_get_bytes_per_sample(avctx->sample_fmt));
    if (!opus->samples) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate samples buffer.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    ret = opus_multistream_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&avctx->initial_padding));
    if (ret != OPUS_OK)
        av_log(avctx, AV_LOG_WARNING,
               "Unable to get number of lookahead samples: %s\n",
               opus_strerror(ret));

    libopus_write_header(avctx, opus->stream_count, coupled_stream_count,
                         opus_vorbis_channel_map[avctx->channels - 1]);

    ff_af_queue_init(avctx, &opus->afq);

    opus->enc = enc;

    return 0;

fail:
    opus_multistream_encoder_destroy(enc);
    av_freep(&avctx->extradata);
    return ret;
}

static int libopus_encode(AVCodecContext *avctx, AVPacket *avpkt,
                          const AVFrame *frame, int *got_packet_ptr)
{
    LibopusEncContext *opus = avctx->priv_data;
    const int sample_size   = avctx->channels *
                              av_get_bytes_per_sample(avctx->sample_fmt);
    uint8_t *audio;
    int ret;
    int discard_padding;

    if (frame) {
        ret = ff_af_queue_add(&opus->afq, frame);
        if (ret < 0)
            return ret;
        if (frame->nb_samples < opus->opts.packet_size) {
            audio = opus->samples;
            memcpy(audio, frame->data[0], frame->nb_samples * sample_size);
        } else
            audio = frame->data[0];
    } else {
        if (!opus->afq.remaining_samples)
            return 0;
        audio = opus->samples;
        memset(audio, 0, opus->opts.packet_size * sample_size);
    }

    /* Maximum packet size taken from opusenc in opus-tools. 60ms packets
     * consist of 3 frames in one packet. The maximum frame size is 1275
     * bytes along with the largest possible packet header of 7 bytes. */
    if ((ret = ff_alloc_packet2(avctx, avpkt, (1275 * 3 + 7) * opus->stream_count)) < 0)
        return ret;

    if (avctx->sample_fmt == AV_SAMPLE_FMT_FLT)
        ret = opus_multistream_encode_float(opus->enc, (float *)audio,
                                            opus->opts.packet_size,
                                            avpkt->data, avpkt->size);
    else
        ret = opus_multistream_encode(opus->enc, (opus_int16 *)audio,
                                      opus->opts.packet_size,
                                      avpkt->data, avpkt->size);

    if (ret < 0) {
        av_log(avctx, AV_LOG_ERROR,
               "Error encoding frame: %s\n", opus_strerror(ret));
        return ff_opus_error_to_averror(ret);
    }

    av_shrink_packet(avpkt, ret);

    ff_af_queue_remove(&opus->afq, opus->opts.packet_size,
                       &avpkt->pts, &avpkt->duration);

    discard_padding = opus->opts.packet_size - avpkt->duration;
    // Check if subtraction resulted in an overflow
    if ((discard_padding < opus->opts.packet_size) != (avpkt->duration > 0)) {
        av_free_packet(avpkt);
        av_free(avpkt);
        return AVERROR(EINVAL);
    }
    if (discard_padding > 0) {
        uint8_t* side_data = av_packet_new_side_data(avpkt,
                                                     AV_PKT_DATA_SKIP_SAMPLES,
                                                     10);
        if(!side_data) {
            av_free_packet(avpkt);
            av_free(avpkt);
            return AVERROR(ENOMEM);
        }
        AV_WL32(side_data + 4, discard_padding);
    }

    *got_packet_ptr = 1;

    return 0;
}

static av_cold int libopus_encode_close(AVCodecContext *avctx)
{
    LibopusEncContext *opus = avctx->priv_data;

    opus_multistream_encoder_destroy(opus->enc);

    ff_af_queue_close(&opus->afq);

    av_freep(&opus->samples);
    av_freep(&avctx->extradata);

    return 0;
}

#define OFFSET(x) offsetof(LibopusEncContext, opts.x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption libopus_options[] = {
    { "application",    "Intended application type",           OFFSET(application),    AV_OPT_TYPE_INT,   { .i64 = OPUS_APPLICATION_AUDIO }, OPUS_APPLICATION_VOIP, OPUS_APPLICATION_RESTRICTED_LOWDELAY, FLAGS, "application" },
        { "voip",           "Favor improved speech intelligibility",   0, AV_OPT_TYPE_CONST, { .i64 = OPUS_APPLICATION_VOIP },                0, 0, FLAGS, "application" },
        { "audio",          "Favor faithfulness to the input",         0, AV_OPT_TYPE_CONST, { .i64 = OPUS_APPLICATION_AUDIO },               0, 0, FLAGS, "application" },
        { "lowdelay",       "Restrict to only the lowest delay modes", 0, AV_OPT_TYPE_CONST, { .i64 = OPUS_APPLICATION_RESTRICTED_LOWDELAY }, 0, 0, FLAGS, "application" },
    { "frame_duration", "Duration of a frame in milliseconds", OFFSET(frame_duration), AV_OPT_TYPE_FLOAT, { .dbl = 20.0 }, 2.5, 60.0, FLAGS },
    { "packet_loss",    "Expected packet loss percentage",     OFFSET(packet_loss),    AV_OPT_TYPE_INT,   { .i64 = 0 },    0,   100,  FLAGS },
    { "vbr",            "Variable bit rate mode",              OFFSET(vbr),            AV_OPT_TYPE_INT,   { .i64 = 1 },    0,   2,    FLAGS, "vbr" },
        { "off",            "Use constant bit rate", 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, FLAGS, "vbr" },
        { "on",             "Use variable bit rate", 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, "vbr" },
        { "constrained",    "Use constrained VBR",   0, AV_OPT_TYPE_CONST, { .i64 = 2 }, 0, 0, FLAGS, "vbr" },
    { NULL },
};

static const AVClass libopus_class = {
    .class_name = "libopus",
    .item_name  = av_default_item_name,
    .option     = libopus_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault libopus_defaults[] = {
    { "b",                 "0" },
    { "compression_level", "10" },
    { NULL },
};

static const int libopus_sample_rates[] = {
    48000, 24000, 16000, 12000, 8000, 0,
};

AVCodec ff_libopus_encoder = {
    .name            = "libopus",
    .long_name       = NULL_IF_CONFIG_SMALL("libopus Opus"),
    .type            = AVMEDIA_TYPE_AUDIO,
    .id              = AV_CODEC_ID_OPUS,
    .priv_data_size  = sizeof(LibopusEncContext),
    .init            = libopus_encode_init,
    .encode2         = libopus_encode,
    .close           = libopus_encode_close,
    .capabilities    = CODEC_CAP_DELAY | CODEC_CAP_SMALL_LAST_FRAME,
    .sample_fmts     = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_FLT,
                                                      AV_SAMPLE_FMT_NONE },
    .channel_layouts = ff_vorbis_channel_layouts,
    .supported_samplerates = libopus_sample_rates,
    .priv_class      = &libopus_class,
    .defaults        = libopus_defaults,
};
