/*
 * FDK-XHE-AAC encoder wrapper
 * Copyright (c) 2026 Martin Storsjo
 *
 * This file is part of FFmpeg.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "audio_frame_queue.h"
#include "codec_internal.h"
#include "encode.h"
#include "profiles.h"

#include <fdk-xhe-aac/xHEAACEnc.h>

typedef struct XHEAACContext {
    const AVClass *class;
    IIS_XHEAACENC_INSTANCE_HANDLE handle;

    int vbr;
    float live_loudness_level;
    int64_t rap_interval;
    int stream_id;

    int sample_rate;

    AudioFrameQueue afq;

    float *sample_queue;
    int sample_queue_size;
    int nb_sample_queue;
} XHEAACContext;

static const AVOption xhe_aac_enc_options[] = {
    { "vbr", "VBR mode (0-6)", offsetof(XHEAACContext, vbr), AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 6, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "live_loudness_level", "target loudness level in live encoding mode [LUFS]", offsetof(XHEAACContext, live_loudness_level), AV_OPT_TYPE_FLOAT, { .dbl = -24 }, -31.0, -10.0, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "rap_interval", "Random Access Point interval", offsetof(XHEAACContext, rap_interval), AV_OPT_TYPE_DURATION, { .i64 = -1 }, -1, INT64_MAX, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { "stream_id", "unique ID of this stream within one adaptation set", offsetof(XHEAACContext, stream_id), AV_OPT_TYPE_INT, {.i64 = -1}, -1, 65535, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    FF_AAC_PROFILE_OPTS
    { NULL }
};

static const AVClass xhe_aac_enc_class = {
    .class_name = "libfdk_xhe_aac",
    .item_name  = av_default_item_name,
    .option     = xhe_aac_enc_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const char *xhe_aac_get_error(IIS_XHEAACENC_RETURN_CODE err)
{
    switch (err) {
    case IIS_XHEAACENC_NO_ERROR:
        return "No error";
    case IIS_XHEAACENC_ERROR_PARAM_BITRATE:
        return "Invalid or missing parameter for bitrate";
    case IIS_XHEAACENC_ERROR_TOO_MANY_INPUT_SAMPLES:
        return "Too many input samples";
    case IIS_XHEAACENC_ERROR_PARAMCOMB_BASE:
        return "Invalid parameter combination of AOT, operating mode, bitrate, bitrate mode, channel config or transport format";
    case IIS_XHEAACENC_ERROR_INSAMPLERATE_UNSUPPORTED:
        return "Unsupported input sample rate";
    case IIS_XHEAACENC_ERROR_COMB_INSAMPLERATE_UNSUPPORTED:
        return "Unsupported input sample rate for a specific configuration";
    case IIS_XHEAACENC_ERROR_PARAMCOMB_STREAMID_SWITCHABLE_AOT:
        return "Operating mode switchable requires a stream ID";
    case IIS_XHEAACENC_ERROR_PARAMCOMB_RAP_INTERVAL_RAP_MIN_INTERVAL:
        return "The RAP interval is below the allowed minimum";
    default:
        return "Unknown error";
    }
}

static av_cold int xhe_aac_encode_close(AVCodecContext *avctx)
{
    XHEAACContext *s = avctx->priv_data;
    IIS_XHEAACENC_RETURN_CODE err;

    if (s->handle) {
        err = IIS_xHEAACEnc_Delete(s->handle);
        if (err != IIS_XHEAACENC_NO_ERROR)
            av_log(avctx, AV_LOG_ERROR, "Unexpected error on close: %s (%d)\n",
                   xhe_aac_get_error(err), err);
        s->handle = NULL;
    }
    ff_af_queue_close(&s->afq);
    av_freep(&s->sample_queue);

    return 0;
}

static av_cold int xhe_aac_encode_init(AVCodecContext *avctx)
{
    XHEAACContext *s = avctx->priv_data;
    int ret = AVERROR(EINVAL);
    AVCPBProperties *cpb_props;
    IIS_XHEAACENC_CONFIG_INSTANCE_HANDLE config_handle = NULL;
    IIS_XHEAACENC_CHANNELCONFIG channel_config;
    IIS_XHEAACENC_BITRATEMODE bitrate_mode;
    IIS_XHEAACENC_RETURN_CODE err;
    int out_framesize, out_max_bitrate, out_avg_bitrate;

    if (avctx->profile != AV_PROFILE_UNKNOWN && avctx->profile != AV_PROFILE_AAC_USAC) {
        av_log(avctx, AV_LOG_ERROR, "This encoder can only encode xHE-AAC (AOT 42).\n");
        goto error;
    }
    avctx->profile = AV_PROFILE_AAC_USAC;

    switch (avctx->ch_layout.nb_channels) {
    case 1: channel_config = IIS_XHEAACENC_CHANNELCONFIG_MONO;   break;
    case 2: channel_config = IIS_XHEAACENC_CHANNELCONFIG_STEREO; break;
    default:
        av_log(avctx, AV_LOG_ERROR,
               "Unsupported number of channels %d\n", avctx->ch_layout.nb_channels);
        goto error;
    }

    err = IIS_xHEAACEnc_Config_Open(&config_handle, avctx->sample_rate, channel_config);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to open config: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }

    if (s->vbr >= 0) {
        switch (s->vbr) {
        case 0: bitrate_mode = IIS_XHEAACENC_BITRATEMODE_AAC_VBR0; break;
        case 1: bitrate_mode = IIS_XHEAACENC_BITRATEMODE_AAC_VBR1; break;
        case 2: bitrate_mode = IIS_XHEAACENC_BITRATEMODE_AAC_VBR2; break;
        case 3: bitrate_mode = IIS_XHEAACENC_BITRATEMODE_AAC_VBR3; break;
        case 4: bitrate_mode = IIS_XHEAACENC_BITRATEMODE_AAC_VBR4; break;
        case 5: bitrate_mode = IIS_XHEAACENC_BITRATEMODE_AAC_VBR5; break;
        case 6: bitrate_mode = IIS_XHEAACENC_BITRATEMODE_AAC_VBR6; break;
        }
    } else {
        bitrate_mode = IIS_XHEAACENC_BITRATEMODE_CBR;

        int orig_bit_rate = avctx->bit_rate;
        if (avctx->bit_rate <= 0) {
            avctx->bit_rate = (32 + 32*avctx->ch_layout.nb_channels) * avctx->sample_rate / 44;
            /* The encoder fails with bitrates below 12 kbps. */
            avctx->bit_rate = FFMAX(avctx->bit_rate, 12000);
        }

        /* The bitrate needs to be a multiple of 2000 */
        avctx->bit_rate = (avctx->bit_rate / 2000) * 2000;

        if (orig_bit_rate > 0 && orig_bit_rate != avctx->bit_rate)
            av_log(avctx, AV_LOG_WARNING,
                   "Bitrate %d is not a multiple of 2000, using %"PRId64"\n",
                   orig_bit_rate, avctx->bit_rate);

        err = IIS_xHEAACEnc_Config_AddParamValueInt(config_handle,
                  IIS_XHEAACENC_PARAMETER_BITRATE, avctx->bit_rate);
        if (err != IIS_XHEAACENC_NO_ERROR) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set bitrate: %s (%d)\n",
                   xhe_aac_get_error(err), err);
            goto error;
        }
    }
    err = IIS_xHEAACEnc_Config_AddParamValueInt(config_handle,
              IIS_XHEAACENC_PARAMETER_BITRATEMODE, bitrate_mode);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set bitrate mode: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }

    /* We always output a raw bitstream, with an ASC in extradata, regardless
     * of the AV_CODEC_FLAG_GLOBAL_HEADER flag. xHE-AAC can't be muxed in
     * ADTS. It can be muxed in LATM/LOAS, but in libavcodec, that uses a
     * different codec ID - so we can't conditionally switch to that here.
     * Therefore, always output the raw/ASC form. */
    err = IIS_xHEAACEnc_Config_AddParamValueInt(config_handle,
              IIS_XHEAACENC_PARAMETER_TRANSPORTFORMAT,
              IIS_XHEAACENC_TRANSPORTFORMAT_RAW);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set transport format: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }

    err = IIS_xHEAACEnc_Config_AddParamValueInt(config_handle,
              IIS_XHEAACENC_PARAMETER_AOT, IIS_XHEAACENC_AOT_USAC);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set AOT: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }

    if (s->stream_id < 0) {
        /* The encoder requires us to provide a stream ID. Within an
         * adaptation set, the stream ID needs to be unique. If the user didn't
         * specify one, try to make one up that probably is unique. */
        if (s->vbr >= 0)
            s->stream_id = s->vbr;
        else
            s->stream_id = avctx->bit_rate / 1000;
    }
    err = IIS_xHEAACEnc_Config_AddParamValueInt(config_handle,
              IIS_XHEAACENC_PARAMETER_STREAMID, s->stream_id);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set stream ID: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }
    err = IIS_xHEAACEnc_Config_AddParamValueInt(config_handle,
              IIS_XHEAACENC_PARAMETER_RAP_OCCURRENCE,
              IIS_XHEAACENC_RAP_OCCURRENCE_CONSTANT_INTERVAL);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set RAP occurrence: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }
    if (s->rap_interval > 0) {
        err = IIS_xHEAACEnc_Config_AddParamValueInt(config_handle,
                  IIS_XHEAACENC_PARAMETER_RAP_INTERVAL_MS, s->rap_interval / 1000);
        if (err != IIS_XHEAACENC_NO_ERROR) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set RAP interval: %s (%d)\n",
                   xhe_aac_get_error(err), err);
            goto error;
        }
    }
    err = IIS_xHEAACEnc_Config_AddParamValueFloat(config_handle,
              IIS_XHEAACENC_PARAMETER_LIVE_LOUDNESS_LEVEL, s->live_loudness_level);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set live loudness level: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }

    err = IIS_xHEAACEnc_Config_Finalize(config_handle);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to finalize config: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }

    err = IIS_xHEAACEnc_Open(&s->handle, config_handle);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to open encoder: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }

    err = IIS_xHEAACEnc_Config_Delete(config_handle);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to delete config: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }
    config_handle = NULL;

    err = IIS_xHEAACEnc_GetParam(s->handle, IIS_XHEAACENC_PARAMETER_OUTSAMPLERATE,
                                 IIS_XHEAACENC_PARAM_INT,
                                 &s->sample_rate, sizeof(s->sample_rate));
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get sample rate: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }
    if (s->sample_rate != avctx->sample_rate) {
        av_log(avctx, AV_LOG_ERROR, "Encoder resamples %d into %d - not supported\n",
               avctx->sample_rate, s->sample_rate);
        goto error;
    }

    err = IIS_xHEAACEnc_GetParam(s->handle, IIS_XHEAACENC_PARAMETER_FRAMESAMPLES,
                                 IIS_XHEAACENC_PARAM_INT,
                                 &out_framesize, sizeof(out_framesize));
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get frame size: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }
    avctx->frame_size = out_framesize;

    /* Note - this encoder doesn't incur any extra avctx->initial_padding
     * that needs to be signaled and removed after decoding. (There exists
     * a parameter IIS_XHEAACENC_PARAMETER_PRIMING we could query, but
     * it returns zero.) */

    ff_af_queue_init(avctx, &s->afq);

    IIS_XHEAACENC_ASCINFO asc_info;
    err = IIS_xHEAACEnc_GetAscInfo(s->handle, &asc_info);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get ASC info: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }

    avctx->extradata_size = (asc_info.ascSizeBits + 7) / 8;
    avctx->extradata      = av_mallocz(avctx->extradata_size +
                                       AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata) {
        ret = AVERROR(ENOMEM);
        goto error;
    }
    memcpy(avctx->extradata, asc_info.ascBuffer, avctx->extradata_size);

    cpb_props = ff_encode_add_cpb_side_data(avctx);
    if (!cpb_props) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    err = IIS_xHEAACEnc_GetParam(s->handle, IIS_XHEAACENC_PARAMETER_BITRATELIMIT,
                                 IIS_XHEAACENC_PARAM_INT,
                                 &out_max_bitrate, sizeof(out_max_bitrate));
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get max bitrate: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }

    err = IIS_xHEAACEnc_GetParam(s->handle, IIS_XHEAACENC_PARAMETER_BITRATE,
                                 IIS_XHEAACENC_PARAM_INT,
                                 &out_avg_bitrate, sizeof(out_avg_bitrate));
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get avg bitrate: %s (%d)\n",
               xhe_aac_get_error(err), err);
        goto error;
    }
    cpb_props->max_bitrate = out_max_bitrate;
    cpb_props->avg_bitrate = out_avg_bitrate;
    if (avctx->bit_rate <= 0)
        avctx->bit_rate = out_avg_bitrate;

    s->sample_queue_size = 4096*20;
    s->sample_queue = av_malloc(s->sample_queue_size * sizeof(*s->sample_queue));
    if (!s->sample_queue) {
        ret = AVERROR(ENOMEM);
        goto error;
    }

    return 0;
error:
    if (config_handle) {
        err = IIS_xHEAACEnc_Config_Delete(config_handle);
        if (err != IIS_XHEAACENC_NO_ERROR)
            av_log(avctx, AV_LOG_ERROR, "Unable to delete config: %s (%d)\n",
                   xhe_aac_get_error(err), err);
    }
    return ret;
}

static int xhe_aac_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                            const AVFrame *frame, int *got_packet_ptr)
{
    XHEAACContext *s = avctx->priv_data;
    IIS_XHEAACENC_RETURN_CODE err;
    int samples_next;
    int ret;

    if (frame) {
        int nb_new_samples = frame->nb_samples * avctx->ch_layout.nb_channels;

        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;

        if (s->nb_sample_queue + nb_new_samples > s->sample_queue_size) {
            int size = s->nb_sample_queue + nb_new_samples + 4096;
            float *ptr = av_realloc(s->sample_queue, size * sizeof(*s->sample_queue));
            if (!ptr)
                return AVERROR(ENOMEM);
            s->sample_queue = ptr;
            s->sample_queue_size = size;
        }

        /* As long as we don't have enough samples, we could also feed them
         * straight into the encoder - it does accept getting too few samples.
         *
         * But we can't feed it too many samples, and samples_next doesn't
         * align with the frame size, so we need to buffer the remainder in any
         * case. Thus for simplicity, just buffer up all samples in the wrapper.
         *
         * (The encoder has got a parameter IIS_XHEAACENC_PARAMETER_SAMPLES_MAX
         * which is said to possibly be larger than IIS_XHEAACENC_PARAMETER_SAMPLES_NEXT,
         * but currently, at least at start, one can't pass more than
         * samples_next.)
         */
        memcpy(s->sample_queue + s->nb_sample_queue, frame->data[0],
               nb_new_samples * sizeof(*s->sample_queue));
        s->nb_sample_queue += nb_new_samples;
    }

    err = IIS_xHEAACEnc_GetParam(s->handle, IIS_XHEAACENC_PARAMETER_SAMPLES_NEXT,
                                 IIS_XHEAACENC_PARAM_INT,
                                 &samples_next, sizeof(samples_next));
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get number of required samples: %s (%d)\n",
               xhe_aac_get_error(err), err);
        return AVERROR(EINVAL);
    }

    if (s->nb_sample_queue < samples_next && frame) {
        /* Not enough samples yet, and not end of stream - don't push it
         * yet. */
        return 0;
    }

    int min_outbuf_size;
    err = IIS_xHEAACEnc_GetParam(s->handle, IIS_XHEAACENC_PARAMETER_MIN_OUTBUF_SIZE,
                                 IIS_XHEAACENC_PARAM_INT,
                                 &min_outbuf_size, sizeof(min_outbuf_size));
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get minimum output buffer size: %s (%d)\n",
               xhe_aac_get_error(err), err);
        return AVERROR(EINVAL);
    }
    ret = ff_alloc_packet(avctx, avpkt, min_outbuf_size);
    if (ret < 0)
        return ret;

    int nb_samples = FFMIN(s->nb_sample_queue, samples_next);
    int out_bytes;
    IIS_XHEAACENC_AUINFO au_info;

    err = IIS_xHEAACEnc_EncodeFrame(s->handle, s->sample_queue, nb_samples,
                                    (unsigned char *)avpkt->data, &out_bytes,
                                    avpkt->size, &au_info);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to encode frame: %s (%d)\n",
               xhe_aac_get_error(err), err);
        return AVERROR(EINVAL);
    }
    s->nb_sample_queue -= nb_samples;
    memmove(s->sample_queue, s->sample_queue + nb_samples,
            s->nb_sample_queue * sizeof(*s->sample_queue));

    IIS_XHEAACENC_ENCODER_STATE state;
    err = IIS_xHEAACEnc_GetEncoderState(s->handle, &state);
    if (err != IIS_XHEAACENC_NO_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get encoder state: %s (%d)\n",
               xhe_aac_get_error(err), err);
        return AVERROR(EINVAL);
    }

    /* If no output packet was generated and the encoder isn't ready to
     * close yet, force the encoder into flushing mode. */
    if (out_bytes <= 0 && !frame && state != IIS_XHEAACENC_ENCODER_STATE_READY_TO_CLOSE) {
        while (state != IIS_XHEAACENC_ENCODER_STATE_FLUSHING &&
               state != IIS_XHEAACENC_ENCODER_STATE_READY_TO_CLOSE) {
            err = IIS_xHEAACEnc_EncodeFrame(s->handle, NULL, 0,
                                            (unsigned char *)avpkt->data,
                                            &out_bytes, avpkt->size, &au_info);
            if (err != IIS_XHEAACENC_NO_ERROR) {
                av_log(avctx, AV_LOG_ERROR, "Unable to flush encoder: %s (%d)\n",
                       xhe_aac_get_error(err), err);
                return AVERROR(EINVAL);
            }

            err = IIS_xHEAACEnc_GetEncoderState(s->handle, &state);
            if (err != IIS_XHEAACENC_NO_ERROR) {
                av_log(avctx, AV_LOG_ERROR, "Unable to get encoder state: %s (%d)\n",
                       xhe_aac_get_error(err), err);
                return AVERROR(EINVAL);
            }
        }
    }

    avpkt->size = out_bytes;
    if (out_bytes > 0) {
        *got_packet_ptr = 1;

        if (au_info.auOffset != 0) {
            av_log(avctx, AV_LOG_ERROR,
                   "Encoder reports auOffset of %d. This is currently not implemented.\n",
                   au_info.auOffset);
            return AVERROR(EINVAL);
        }

        switch (au_info.isSyncFrame) {
        case IIS_XHEAACENC_SYNCFRAME_NO:
        case IIS_XHEAACENC_SYNCFRAME_INDEPENDENT_FRAME:
        case IIS_XHEAACENC_SYNCFRAME_STREAM_MUX_CONFIG_IF:
            break;
        case IIS_XHEAACENC_SYNCFRAME_IMMEDIATE_PLAY_OUT_FRAME:
        case IIS_XHEAACENC_SYNCFRAME_STREAM_MUX_CONFIG_IPF:
            avpkt->flags |= AV_PKT_FLAG_KEY;
            break;
        case IIS_XHEAACENC_SYNCFRAME_INVALID:
        default:
            av_log(avctx, AV_LOG_ERROR, "Unexpected isSyncFrame status %d\n",
                                        au_info.isSyncFrame);
            return AVERROR(EINVAL);
        }

        ff_af_queue_remove(&s->afq, au_info.auSamplesValid, avpkt);
    } else {
        avpkt->size = 0;
    }

    return 0;
}

static const AVProfile profiles[] = {
    { AV_PROFILE_AAC_USAC, "xHE-AAC" },
    { AV_PROFILE_UNKNOWN },
};

static const FFCodecDefault xhe_aac_encode_defaults[] = {
    { "b", "0" },
    { NULL }
};

static const AVChannelLayout xhe_aac_ch_layouts[] = {
    AV_CHANNEL_LAYOUT_MONO,
    AV_CHANNEL_LAYOUT_STEREO,
    { 0 },
};

static const int xhe_aac_sample_rates[] = {
    48000, 44100, 0
};

const FFCodec ff_libfdk_xhe_aac_encoder = {
    .p.name                = "libfdk_xhe_aac",
    CODEC_LONG_NAME("Fraunhofer FDK xHE-AAC"),
    .p.type                = AVMEDIA_TYPE_AUDIO,
    .p.id                  = AV_CODEC_ID_AAC,
    .p.capabilities        = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY |
                             AV_CODEC_CAP_SMALL_LAST_FRAME,
    .caps_internal         = FF_CODEC_CAP_NOT_INIT_THREADSAFE |
                             FF_CODEC_CAP_INIT_CLEANUP,
    .priv_data_size        = sizeof(XHEAACContext),
    .init                  = xhe_aac_encode_init,
    FF_CODEC_ENCODE_CB(xhe_aac_encode_frame),
    .close                 = xhe_aac_encode_close,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_FLT),
    .p.priv_class          = &xhe_aac_enc_class,
    .defaults              = xhe_aac_encode_defaults,
    .p.profiles            = profiles,
    CODEC_SAMPLERATES_ARRAY(xhe_aac_sample_rates),
    .p.wrapper_name        = "libfdk-xhe-aac",
    CODEC_CH_LAYOUTS_ARRAY(xhe_aac_ch_layouts),
};
