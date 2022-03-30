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

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"
#include "libopus.h"
#include "vorbis.h"
#include "audio_frame_queue.h"

typedef struct LibopusEncOpts {
    int vbr;
    int application;
    int packet_loss;
    int fec;
    int complexity;
    float frame_duration;
    int packet_size;
    int max_bandwidth;
    int mapping_family;
#ifdef OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST
    int apply_phase_inv;
#endif
} LibopusEncOpts;

typedef struct LibopusEncContext {
    AVClass *class;
    OpusMSEncoder *enc;
    int stream_count;
    uint8_t *samples;
    LibopusEncOpts opts;
    AudioFrameQueue afq;
    const uint8_t *encoder_channel_map;
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
                                 int mapping_family,
                                 const uint8_t *channel_mapping)
{
    uint8_t *p   = avctx->extradata;
    int channels = avctx->ch_layout.nb_channels;

    bytestream_put_buffer(&p, "OpusHead", 8);
    bytestream_put_byte(&p, 1); /* Version */
    bytestream_put_byte(&p, channels);
    bytestream_put_le16(&p, avctx->initial_padding * 48000 / avctx->sample_rate); /* Lookahead samples at 48kHz */
    bytestream_put_le32(&p, avctx->sample_rate); /* Original sample rate */
    bytestream_put_le16(&p, 0); /* Gain of 0dB is recommended. */

    /* Channel mapping */
    bytestream_put_byte(&p, mapping_family);
    if (mapping_family != 0) {
        bytestream_put_byte(&p, stream_count);
        bytestream_put_byte(&p, coupled_stream_count);
        bytestream_put_buffer(&p, channel_mapping, channels);
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

    ret = opus_multistream_encoder_ctl(enc,
                                       OPUS_SET_INBAND_FEC(opts->fec));
    if (ret != OPUS_OK)
        av_log(avctx, AV_LOG_WARNING,
               "Unable to set inband FEC: %s\n",
               opus_strerror(ret));

    if (avctx->cutoff) {
        ret = opus_multistream_encoder_ctl(enc,
                                           OPUS_SET_MAX_BANDWIDTH(opts->max_bandwidth));
        if (ret != OPUS_OK)
            av_log(avctx, AV_LOG_WARNING,
                   "Unable to set maximum bandwidth: %s\n", opus_strerror(ret));
    }

#ifdef OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST
    ret = opus_multistream_encoder_ctl(enc,
                                       OPUS_SET_PHASE_INVERSION_DISABLED(!opts->apply_phase_inv));
    if (ret != OPUS_OK)
        av_log(avctx, AV_LOG_WARNING,
               "Unable to set phase inversion: %s\n",
               opus_strerror(ret));
#endif
    return OPUS_OK;
}

static int libopus_check_max_channels(AVCodecContext *avctx,
                                      int max_channels) {
    if (avctx->ch_layout.nb_channels > max_channels) {
        av_log(avctx, AV_LOG_ERROR, "Opus mapping family undefined for %d channels.\n",
               avctx->ch_layout.nb_channels);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int libopus_check_vorbis_layout(AVCodecContext *avctx, int mapping_family) {
    av_assert2(avctx->ch_layout.nb_channels < FF_ARRAY_ELEMS(ff_vorbis_ch_layouts));

    if (avctx->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC) {
        av_log(avctx, AV_LOG_WARNING,
               "No channel layout specified. Opus encoder will use Vorbis "
               "channel layout for %d channels.\n", avctx->ch_layout.nb_channels);
    } else if (av_channel_layout_compare(&avctx->ch_layout, &ff_vorbis_ch_layouts[avctx->ch_layout.nb_channels - 1])) {
        char name[32];

        av_channel_layout_describe(&avctx->ch_layout, name, sizeof(name));
        av_log(avctx, AV_LOG_ERROR,
               "Invalid channel layout %s for specified mapping family %d.\n",
               name, mapping_family);

        return AVERROR(EINVAL);
    }

    return 0;
}

static int libopus_validate_layout_and_get_channel_map(
        AVCodecContext *avctx,
        int mapping_family,
        const uint8_t ** channel_map_result)
{
    const uint8_t * channel_map = NULL;
    int ret;

    switch (mapping_family) {
    case -1:
        ret = libopus_check_max_channels(avctx, 8);
        if (ret == 0) {
            ret = libopus_check_vorbis_layout(avctx, mapping_family);
            /* Channels do not need to be reordered. */
        }

        break;
    case 0:
        ret = libopus_check_max_channels(avctx, 2);
        if (ret == 0) {
            ret = libopus_check_vorbis_layout(avctx, mapping_family);
        }
        break;
    case 1:
        /* Opus expects channels to be in Vorbis order. */
        ret = libopus_check_max_channels(avctx, 8);
        if (ret == 0) {
            ret = libopus_check_vorbis_layout(avctx, mapping_family);
            channel_map = ff_vorbis_channel_layout_offsets[avctx->ch_layout.nb_channels - 1];
        }
        break;
    case 255:
        ret = libopus_check_max_channels(avctx, 254);
        break;
    default:
        av_log(avctx, AV_LOG_WARNING,
               "Unknown channel mapping family %d. Output channel layout may be invalid.\n",
               mapping_family);
        ret = 0;
    }

    *channel_map_result = channel_map;
    return ret;
}

static av_cold int libopus_encode_init(AVCodecContext *avctx)
{
    LibopusEncContext *opus = avctx->priv_data;
    OpusMSEncoder *enc;
    uint8_t libopus_channel_mapping[255];
    int ret = OPUS_OK;
    int channels = avctx->ch_layout.nb_channels;
    int av_ret;
    int coupled_stream_count, header_size, frame_size;
    int mapping_family;

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
#ifdef OPUS_FRAMESIZE_120_MS
    case 3840:
    case 4800:
    case 5760:
#endif
        opus->opts.packet_size =
        avctx->frame_size      = frame_size * avctx->sample_rate / 48000;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Invalid frame duration: %g.\n"
               "Frame duration must be exactly one of: 2.5, 5, 10, 20, 40"
#ifdef OPUS_FRAMESIZE_120_MS
               ", 60, 80, 100 or 120.\n",
#else
               " or 60.\n",
#endif
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

    /* Channels may need to be reordered to match opus mapping. */
    av_ret = libopus_validate_layout_and_get_channel_map(avctx, opus->opts.mapping_family,
                                                         &opus->encoder_channel_map);
    if (av_ret) {
        return av_ret;
    }

    if (opus->opts.mapping_family == -1) {
        /* By default, use mapping family 1 for the header but use the older
         * libopus multistream API to avoid surround masking. */

        /* Set the mapping family so that the value is correct in the header */
        mapping_family = channels > 2 ? 1 : 0;
        coupled_stream_count = opus_coupled_streams[channels - 1];
        opus->stream_count   = channels - coupled_stream_count;
        memcpy(libopus_channel_mapping,
               opus_vorbis_channel_map[channels - 1],
               channels * sizeof(*libopus_channel_mapping));

        enc = opus_multistream_encoder_create(
            avctx->sample_rate, channels, opus->stream_count,
            coupled_stream_count,
            libavcodec_libopus_channel_map[channels - 1],
            opus->opts.application, &ret);
    } else {
        /* Use the newer multistream API. The encoder will set the channel
         * mapping and coupled stream counts to its internal defaults and will
         * use surround masking analysis to save bits. */
        mapping_family = opus->opts.mapping_family;
        enc = opus_multistream_surround_encoder_create(
            avctx->sample_rate, channels, mapping_family,
            &opus->stream_count, &coupled_stream_count, libopus_channel_mapping,
            opus->opts.application, &ret);
    }

    if (ret != OPUS_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "Failed to create encoder: %s\n", opus_strerror(ret));
        return ff_opus_error_to_averror(ret);
    }

    if (!avctx->bit_rate) {
        /* Sane default copied from opusenc */
        avctx->bit_rate = 64000 * opus->stream_count +
                          32000 * coupled_stream_count;
        av_log(avctx, AV_LOG_WARNING,
               "No bit rate set. Defaulting to %"PRId64" bps.\n", avctx->bit_rate);
    }

    if (avctx->bit_rate < 500 || avctx->bit_rate > 256000 * channels) {
        av_log(avctx, AV_LOG_ERROR, "The bit rate %"PRId64" bps is unsupported. "
               "Please choose a value between 500 and %d.\n", avctx->bit_rate,
               256000 * channels);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    ret = libopus_configure_encoder(avctx, enc, &opus->opts);
    if (ret != OPUS_OK) {
        ret = ff_opus_error_to_averror(ret);
        goto fail;
    }

    /* Header includes channel mapping table if and only if mapping family is NOT 0 */
    header_size = 19 + (mapping_family == 0 ? 0 : 2 + channels);
    avctx->extradata = av_malloc(header_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata) {
        av_log(avctx, AV_LOG_ERROR, "Failed to allocate extradata.\n");
        ret = AVERROR(ENOMEM);
        goto fail;
    }
    avctx->extradata_size = header_size;

    opus->samples = av_calloc(frame_size, channels *
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
                         mapping_family, libopus_channel_mapping);

    ff_af_queue_init(avctx, &opus->afq);

    opus->enc = enc;

    return 0;

fail:
    opus_multistream_encoder_destroy(enc);
    return ret;
}

static void libopus_copy_samples_with_channel_map(
    uint8_t *dst, const uint8_t *src, const uint8_t *channel_map,
    int nb_channels, int nb_samples, int bytes_per_sample) {
    int sample, channel;
    for (sample = 0; sample < nb_samples; ++sample) {
        for (channel = 0; channel < nb_channels; ++channel) {
            const size_t src_pos = bytes_per_sample * (nb_channels * sample + channel);
            const size_t dst_pos = bytes_per_sample * (nb_channels * sample + channel_map[channel]);

            memcpy(&dst[dst_pos], &src[src_pos], bytes_per_sample);
        }
    }
}

static int libopus_encode(AVCodecContext *avctx, AVPacket *avpkt,
                          const AVFrame *frame, int *got_packet_ptr)
{
    LibopusEncContext *opus = avctx->priv_data;
    const int bytes_per_sample = av_get_bytes_per_sample(avctx->sample_fmt);
    const int channels         = avctx->ch_layout.nb_channels;
    const int sample_size      = channels * bytes_per_sample;
    uint8_t *audio;
    int ret;
    int discard_padding;

    if (frame) {
        ret = ff_af_queue_add(&opus->afq, frame);
        if (ret < 0)
            return ret;
        if (opus->encoder_channel_map != NULL) {
            audio = opus->samples;
            libopus_copy_samples_with_channel_map(
                audio, frame->data[0], opus->encoder_channel_map,
                channels, frame->nb_samples, bytes_per_sample);
        } else if (frame->nb_samples < opus->opts.packet_size) {
            audio = opus->samples;
            memcpy(audio, frame->data[0], frame->nb_samples * sample_size);
        } else
            audio = frame->data[0];
    } else {
        if (!opus->afq.remaining_samples || (!opus->afq.frame_alloc && !opus->afq.frame_count))
            return 0;
        audio = opus->samples;
        memset(audio, 0, opus->opts.packet_size * sample_size);
    }

    /* Maximum packet size taken from opusenc in opus-tools. 120ms packets
     * consist of 6 frames in one packet. The maximum frame size is 1275
     * bytes along with the largest possible packet header of 7 bytes. */
    if ((ret = ff_alloc_packet(avctx, avpkt, (1275 * 6 + 7) * opus->stream_count)) < 0)
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
        av_packet_unref(avpkt);
        return AVERROR(EINVAL);
    }
    if (discard_padding > 0) {
        uint8_t* side_data = av_packet_new_side_data(avpkt,
                                                     AV_PKT_DATA_SKIP_SAMPLES,
                                                     10);
        if(!side_data) {
            av_packet_unref(avpkt);
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

    return 0;
}

#define OFFSET(x) offsetof(LibopusEncContext, opts.x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption libopus_options[] = {
    { "application",    "Intended application type",           OFFSET(application),    AV_OPT_TYPE_INT,   { .i64 = OPUS_APPLICATION_AUDIO }, OPUS_APPLICATION_VOIP, OPUS_APPLICATION_RESTRICTED_LOWDELAY, FLAGS, "application" },
        { "voip",           "Favor improved speech intelligibility",   0, AV_OPT_TYPE_CONST, { .i64 = OPUS_APPLICATION_VOIP },                0, 0, FLAGS, "application" },
        { "audio",          "Favor faithfulness to the input",         0, AV_OPT_TYPE_CONST, { .i64 = OPUS_APPLICATION_AUDIO },               0, 0, FLAGS, "application" },
        { "lowdelay",       "Restrict to only the lowest delay modes", 0, AV_OPT_TYPE_CONST, { .i64 = OPUS_APPLICATION_RESTRICTED_LOWDELAY }, 0, 0, FLAGS, "application" },
    { "frame_duration", "Duration of a frame in milliseconds", OFFSET(frame_duration), AV_OPT_TYPE_FLOAT, { .dbl = 20.0 }, 2.5, 120.0, FLAGS },
    { "packet_loss",    "Expected packet loss percentage",     OFFSET(packet_loss),    AV_OPT_TYPE_INT,   { .i64 = 0 },    0,   100,  FLAGS },
    { "fec",             "Enable inband FEC. Expected packet loss must be non-zero",     OFFSET(fec),    AV_OPT_TYPE_BOOL,   { .i64 = 0 }, 0, 1, FLAGS },
    { "vbr",            "Variable bit rate mode",              OFFSET(vbr),            AV_OPT_TYPE_INT,   { .i64 = 1 },    0,   2,    FLAGS, "vbr" },
        { "off",            "Use constant bit rate", 0, AV_OPT_TYPE_CONST, { .i64 = 0 }, 0, 0, FLAGS, "vbr" },
        { "on",             "Use variable bit rate", 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, 0, 0, FLAGS, "vbr" },
        { "constrained",    "Use constrained VBR",   0, AV_OPT_TYPE_CONST, { .i64 = 2 }, 0, 0, FLAGS, "vbr" },
    { "mapping_family", "Channel Mapping Family",              OFFSET(mapping_family), AV_OPT_TYPE_INT,   { .i64 = -1 },   -1,  255,  FLAGS, "mapping_family" },
#ifdef OPUS_SET_PHASE_INVERSION_DISABLED_REQUEST
    { "apply_phase_inv", "Apply intensity stereo phase inversion", OFFSET(apply_phase_inv), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, FLAGS },
#endif
    { NULL },
};

static const AVClass libopus_class = {
    .class_name = "libopus",
    .item_name  = av_default_item_name,
    .option     = libopus_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const FFCodecDefault libopus_defaults[] = {
    { "b",                 "0" },
    { "compression_level", "10" },
    { NULL },
};

static const int libopus_sample_rates[] = {
    48000, 24000, 16000, 12000, 8000, 0,
};

const FFCodec ff_libopus_encoder = {
    .p.name          = "libopus",
    .p.long_name     = NULL_IF_CONFIG_SMALL("libopus Opus"),
    .p.type          = AVMEDIA_TYPE_AUDIO,
    .p.id            = AV_CODEC_ID_OPUS,
    .priv_data_size  = sizeof(LibopusEncContext),
    .init            = libopus_encode_init,
    FF_CODEC_ENCODE_CB(libopus_encode),
    .close           = libopus_encode_close,
    .p.capabilities  = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_SMALL_LAST_FRAME,
    .p.sample_fmts   = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_FLT,
                                                      AV_SAMPLE_FMT_NONE },
    .p.supported_samplerates = libopus_sample_rates,
    .p.priv_class    = &libopus_class,
    .defaults        = libopus_defaults,
    .p.wrapper_name  = "libopus",
};
