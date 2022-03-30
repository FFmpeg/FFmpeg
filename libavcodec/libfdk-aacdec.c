/*
 * AAC decoder wrapper
 * Copyright (c) 2012 Martin Storsjo
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

#include <fdk-aac/aacdecoder_lib.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "internal.h"

#ifdef AACDECODER_LIB_VL0
#define FDKDEC_VER_AT_LEAST(vl0, vl1) \
    ((AACDECODER_LIB_VL0 > vl0) || \
     (AACDECODER_LIB_VL0 == vl0 && AACDECODER_LIB_VL1 >= vl1))
#else
#define FDKDEC_VER_AT_LEAST(vl0, vl1) 0
#endif

#if !FDKDEC_VER_AT_LEAST(2, 5) // < 2.5.10
#define AAC_PCM_MAX_OUTPUT_CHANNELS AAC_PCM_OUTPUT_CHANNELS
#endif

enum ConcealMethod {
    CONCEAL_METHOD_SPECTRAL_MUTING      =  0,
    CONCEAL_METHOD_NOISE_SUBSTITUTION   =  1,
    CONCEAL_METHOD_ENERGY_INTERPOLATION =  2,
    CONCEAL_METHOD_NB,
};

typedef struct FDKAACDecContext {
    const AVClass *class;
    HANDLE_AACDECODER handle;
    uint8_t *decoder_buffer;
    int decoder_buffer_size;
    uint8_t *anc_buffer;
    int conceal_method;
    int drc_level;
    int drc_boost;
    int drc_heavy;
    int drc_effect;
    int drc_cut;
    int album_mode;
    int level_limit;
#if FDKDEC_VER_AT_LEAST(2, 5) // 2.5.10
    int output_delay_set;
    int flush_samples;
    int delay_samples;
#endif
    AVChannelLayout downmix_layout;
} FDKAACDecContext;


#define DMX_ANC_BUFFSIZE       128
#define DECODER_MAX_CHANNELS     8
#define DECODER_BUFFSIZE      2048 * sizeof(INT_PCM)

#define OFFSET(x) offsetof(FDKAACDecContext, x)
#define AD AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption fdk_aac_dec_options[] = {
    { "conceal", "Error concealment method", OFFSET(conceal_method), AV_OPT_TYPE_INT, { .i64 = CONCEAL_METHOD_NOISE_SUBSTITUTION }, CONCEAL_METHOD_SPECTRAL_MUTING, CONCEAL_METHOD_NB - 1, AD, "conceal" },
    { "spectral", "Spectral muting",      0, AV_OPT_TYPE_CONST, { .i64 = CONCEAL_METHOD_SPECTRAL_MUTING },      INT_MIN, INT_MAX, AD, "conceal" },
    { "noise",    "Noise Substitution",   0, AV_OPT_TYPE_CONST, { .i64 = CONCEAL_METHOD_NOISE_SUBSTITUTION },   INT_MIN, INT_MAX, AD, "conceal" },
    { "energy",   "Energy Interpolation", 0, AV_OPT_TYPE_CONST, { .i64 = CONCEAL_METHOD_ENERGY_INTERPOLATION }, INT_MIN, INT_MAX, AD, "conceal" },
    { "drc_boost", "Dynamic Range Control: boost, where [0] is none and [127] is max boost",
                     OFFSET(drc_boost),      AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 127, AD, NULL    },
    { "drc_cut",   "Dynamic Range Control: attenuation factor, where [0] is none and [127] is max compression",
                     OFFSET(drc_cut),        AV_OPT_TYPE_INT,   { .i64 = -1 }, -1, 127, AD, NULL    },
    { "drc_level", "Dynamic Range Control: reference level, quantized to 0.25dB steps where [0] is 0dB and [127] is -31.75dB, -1 for auto, and -2 for disabled",
                     OFFSET(drc_level),      AV_OPT_TYPE_INT,   { .i64 = -1},  -2, 127, AD, NULL    },
    { "drc_heavy", "Dynamic Range Control: heavy compression, where [1] is on (RF mode) and [0] is off",
                     OFFSET(drc_heavy),      AV_OPT_TYPE_INT,   { .i64 = -1},  -1, 1,   AD, NULL    },
#if FDKDEC_VER_AT_LEAST(2, 5) // 2.5.10
    { "level_limit", "Signal level limiting",
                     OFFSET(level_limit),    AV_OPT_TYPE_BOOL,  { .i64 = -1 }, -1, 1, AD },
#endif
#if FDKDEC_VER_AT_LEAST(3, 0) // 3.0.0
    { "drc_effect","Dynamic Range Control: effect type, where e.g. [0] is none and [6] is general",
                     OFFSET(drc_effect),     AV_OPT_TYPE_INT,   { .i64 = -1},  -1, 8,   AD, NULL    },
#endif
#if FDKDEC_VER_AT_LEAST(3, 1) // 3.1.0
    { "album_mode","Dynamic Range Control: album mode, where [0] is off and [1] is on",
                     OFFSET(album_mode),     AV_OPT_TYPE_INT,   { .i64 = -1},  -1, 1,   AD, NULL    },
#endif
    { "downmix", "Request a specific channel layout from the decoder", OFFSET(downmix_layout), AV_OPT_TYPE_CHLAYOUT, {.str = NULL}, .flags = AD },
    { NULL }
};

static const AVClass fdk_aac_dec_class = {
    .class_name = "libfdk-aac decoder",
    .item_name  = av_default_item_name,
    .option     = fdk_aac_dec_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int get_stream_info(AVCodecContext *avctx)
{
    FDKAACDecContext *s   = avctx->priv_data;
    CStreamInfo *info     = aacDecoder_GetStreamInfo(s->handle);
    int channel_counts[0x24] = { 0 };
    int i, ch_error       = 0;
    uint64_t ch_layout    = 0;

    if (!info) {
        av_log(avctx, AV_LOG_ERROR, "Unable to get stream info\n");
        return AVERROR_UNKNOWN;
    }

    if (info->sampleRate <= 0) {
        av_log(avctx, AV_LOG_ERROR, "Stream info not initialized\n");
        return AVERROR_UNKNOWN;
    }
    avctx->sample_rate = info->sampleRate;
    avctx->frame_size  = info->frameSize;
#if FDKDEC_VER_AT_LEAST(2, 5) // 2.5.10
    if (!s->output_delay_set && info->outputDelay) {
        // Set this only once.
        s->flush_samples    = info->outputDelay;
        s->delay_samples    = info->outputDelay;
        s->output_delay_set = 1;
    }
#endif

    for (i = 0; i < info->numChannels; i++) {
        AUDIO_CHANNEL_TYPE ctype = info->pChannelType[i];
        if (ctype <= ACT_NONE || ctype >= FF_ARRAY_ELEMS(channel_counts)) {
            av_log(avctx, AV_LOG_WARNING, "unknown channel type\n");
            break;
        }
        channel_counts[ctype]++;
    }
    av_log(avctx, AV_LOG_DEBUG,
           "%d channels - front:%d side:%d back:%d lfe:%d top:%d\n",
           info->numChannels,
           channel_counts[ACT_FRONT], channel_counts[ACT_SIDE],
           channel_counts[ACT_BACK],  channel_counts[ACT_LFE],
           channel_counts[ACT_FRONT_TOP] + channel_counts[ACT_SIDE_TOP] +
           channel_counts[ACT_BACK_TOP]  + channel_counts[ACT_TOP]);

    switch (channel_counts[ACT_FRONT]) {
    case 4:
        ch_layout |= AV_CH_LAYOUT_STEREO | AV_CH_FRONT_LEFT_OF_CENTER |
                     AV_CH_FRONT_RIGHT_OF_CENTER;
        break;
    case 3:
        ch_layout |= AV_CH_LAYOUT_STEREO | AV_CH_FRONT_CENTER;
        break;
    case 2:
        ch_layout |= AV_CH_LAYOUT_STEREO;
        break;
    case 1:
        ch_layout |= AV_CH_FRONT_CENTER;
        break;
    default:
        av_log(avctx, AV_LOG_WARNING,
               "unsupported number of front channels: %d\n",
               channel_counts[ACT_FRONT]);
        ch_error = 1;
        break;
    }
    if (channel_counts[ACT_SIDE] > 0) {
        if (channel_counts[ACT_SIDE] == 2) {
            ch_layout |= AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT;
        } else {
            av_log(avctx, AV_LOG_WARNING,
                   "unsupported number of side channels: %d\n",
                   channel_counts[ACT_SIDE]);
            ch_error = 1;
        }
    }
    if (channel_counts[ACT_BACK] > 0) {
        switch (channel_counts[ACT_BACK]) {
        case 3:
            ch_layout |= AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT | AV_CH_BACK_CENTER;
            break;
        case 2:
            ch_layout |= AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT;
            break;
        case 1:
            ch_layout |= AV_CH_BACK_CENTER;
            break;
        default:
            av_log(avctx, AV_LOG_WARNING,
                   "unsupported number of back channels: %d\n",
                   channel_counts[ACT_BACK]);
            ch_error = 1;
            break;
        }
    }
    if (channel_counts[ACT_LFE] > 0) {
        if (channel_counts[ACT_LFE] == 1) {
            ch_layout |= AV_CH_LOW_FREQUENCY;
        } else {
            av_log(avctx, AV_LOG_WARNING,
                   "unsupported number of LFE channels: %d\n",
                   channel_counts[ACT_LFE]);
            ch_error = 1;
        }
    }

    av_channel_layout_uninit(&avctx->ch_layout);
    av_channel_layout_from_mask(&avctx->ch_layout, ch_layout);
    if (!ch_error && avctx->ch_layout.nb_channels != info->numChannels) {
        av_log(avctx, AV_LOG_WARNING, "unsupported channel configuration\n");
        ch_error = 1;
    }
    if (ch_error)
        avctx->ch_layout.order = AV_CHANNEL_ORDER_UNSPEC;

    return 0;
}

static av_cold int fdk_aac_decode_close(AVCodecContext *avctx)
{
    FDKAACDecContext *s = avctx->priv_data;

    if (s->handle)
        aacDecoder_Close(s->handle);
    av_freep(&s->decoder_buffer);
    av_freep(&s->anc_buffer);

    return 0;
}

static av_cold int fdk_aac_decode_init(AVCodecContext *avctx)
{
    FDKAACDecContext *s = avctx->priv_data;
    AAC_DECODER_ERROR err;

    s->handle = aacDecoder_Open(avctx->extradata_size ? TT_MP4_RAW : TT_MP4_ADTS, 1);
    if (!s->handle) {
        av_log(avctx, AV_LOG_ERROR, "Error opening decoder\n");
        return AVERROR_UNKNOWN;
    }

    if (avctx->extradata_size) {
        if ((err = aacDecoder_ConfigRaw(s->handle, &avctx->extradata,
                                        &avctx->extradata_size)) != AAC_DEC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set extradata\n");
            return AVERROR_INVALIDDATA;
        }
    }

    if ((err = aacDecoder_SetParam(s->handle, AAC_CONCEAL_METHOD,
                                   s->conceal_method)) != AAC_DEC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set error concealment method\n");
        return AVERROR_UNKNOWN;
    }

#if FF_API_OLD_CHANNEL_LAYOUT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->request_channel_layout) {
        av_channel_layout_uninit(&s->downmix_layout);
        av_channel_layout_from_mask(&s->downmix_layout, avctx->request_channel_layout);
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    if (s->downmix_layout.nb_channels > 0 &&
        s->downmix_layout.order != AV_CHANNEL_ORDER_NATIVE) {
        int downmix_channels = -1;

        switch (s->downmix_layout.u.mask) {
        case AV_CH_LAYOUT_STEREO:
        case AV_CH_LAYOUT_STEREO_DOWNMIX:
            downmix_channels = 2;
            break;
        case AV_CH_LAYOUT_MONO:
            downmix_channels = 1;
            break;
        default:
            av_log(avctx, AV_LOG_WARNING, "Invalid downmix option\n");
            break;
        }

        if (downmix_channels != -1) {
            if (aacDecoder_SetParam(s->handle, AAC_PCM_MAX_OUTPUT_CHANNELS,
                                    downmix_channels) != AAC_DEC_OK) {
               av_log(avctx, AV_LOG_WARNING, "Unable to set output channels in the decoder\n");
            } else {
               s->anc_buffer = av_malloc(DMX_ANC_BUFFSIZE);
               if (!s->anc_buffer) {
                   av_log(avctx, AV_LOG_ERROR, "Unable to allocate ancillary buffer for the decoder\n");
                   return AVERROR(ENOMEM);
               }
               if (aacDecoder_AncDataInit(s->handle, s->anc_buffer, DMX_ANC_BUFFSIZE)) {
                   av_log(avctx, AV_LOG_ERROR, "Unable to register downmix ancillary buffer in the decoder\n");
                   return AVERROR_UNKNOWN;
               }
            }
        }
    }

    if (s->drc_boost != -1) {
        if (aacDecoder_SetParam(s->handle, AAC_DRC_BOOST_FACTOR, s->drc_boost) != AAC_DEC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set DRC boost factor in the decoder\n");
            return AVERROR_UNKNOWN;
        }
    }

    if (s->drc_cut != -1) {
        if (aacDecoder_SetParam(s->handle, AAC_DRC_ATTENUATION_FACTOR, s->drc_cut) != AAC_DEC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set DRC attenuation factor in the decoder\n");
            return AVERROR_UNKNOWN;
        }
    }

    if (s->drc_level != -1) {
        // This option defaults to -1, i.e. not calling
        // aacDecoder_SetParam(AAC_DRC_REFERENCE_LEVEL) at all, which defaults
        // to the level from DRC metadata, if available. The user can set
        // -drc_level -2, which calls aacDecoder_SetParam(
        // AAC_DRC_REFERENCE_LEVEL) with a negative value, which then
        // explicitly disables the feature.
        if (aacDecoder_SetParam(s->handle, AAC_DRC_REFERENCE_LEVEL, s->drc_level) != AAC_DEC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set DRC reference level in the decoder\n");
            return AVERROR_UNKNOWN;
        }
    }

    if (s->drc_heavy != -1) {
        if (aacDecoder_SetParam(s->handle, AAC_DRC_HEAVY_COMPRESSION, s->drc_heavy) != AAC_DEC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set DRC heavy compression in the decoder\n");
            return AVERROR_UNKNOWN;
        }
    }

#if FDKDEC_VER_AT_LEAST(2, 5) // 2.5.10
    // Setting this parameter to -1 enables the auto behaviour in the library.
    if (aacDecoder_SetParam(s->handle, AAC_PCM_LIMITER_ENABLE, s->level_limit) != AAC_DEC_OK) {
        av_log(avctx, AV_LOG_ERROR, "Unable to set in signal level limiting in the decoder\n");
        return AVERROR_UNKNOWN;
    }
#endif

#if FDKDEC_VER_AT_LEAST(3, 0) // 3.0.0
    if (s->drc_effect != -1) {
        if (aacDecoder_SetParam(s->handle, AAC_UNIDRC_SET_EFFECT, s->drc_effect) != AAC_DEC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set DRC effect type in the decoder\n");
            return AVERROR_UNKNOWN;
        }
    }
#endif

#if FDKDEC_VER_AT_LEAST(3, 1) // 3.1.0
    if (s->album_mode != -1) {
        if (aacDecoder_SetParam(s->handle, AAC_UNIDRC_ALBUM_MODE, s->album_mode) != AAC_DEC_OK) {
            av_log(avctx, AV_LOG_ERROR, "Unable to set album mode in the decoder\n");
            return AVERROR_UNKNOWN;
        }
    }
#endif

    avctx->sample_fmt = AV_SAMPLE_FMT_S16;

    s->decoder_buffer_size = DECODER_BUFFSIZE * DECODER_MAX_CHANNELS;
    s->decoder_buffer = av_malloc(s->decoder_buffer_size);
    if (!s->decoder_buffer)
        return AVERROR(ENOMEM);

    return 0;
}

static int fdk_aac_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                                int *got_frame_ptr, AVPacket *avpkt)
{
    FDKAACDecContext *s = avctx->priv_data;
    int ret;
    AAC_DECODER_ERROR err;
    UINT valid = avpkt->size;
    UINT flags = 0;
    int input_offset = 0;

    if (avpkt->size) {
        err = aacDecoder_Fill(s->handle, &avpkt->data, &avpkt->size, &valid);
        if (err != AAC_DEC_OK) {
            av_log(avctx, AV_LOG_ERROR, "aacDecoder_Fill() failed: %x\n", err);
            return AVERROR_INVALIDDATA;
        }
    } else {
#if FDKDEC_VER_AT_LEAST(2, 5) // 2.5.10
        /* Handle decoder draining */
        if (s->flush_samples > 0) {
            flags |= AACDEC_FLUSH;
        } else {
            return AVERROR_EOF;
        }
#else
        return AVERROR_EOF;
#endif
    }

    err = aacDecoder_DecodeFrame(s->handle, (INT_PCM *) s->decoder_buffer,
                                 s->decoder_buffer_size / sizeof(INT_PCM),
                                 flags);
    if (err == AAC_DEC_NOT_ENOUGH_BITS) {
        ret = avpkt->size - valid;
        goto end;
    }
    if (err != AAC_DEC_OK) {
        av_log(avctx, AV_LOG_ERROR,
               "aacDecoder_DecodeFrame() failed: %x\n", err);
        ret = AVERROR_UNKNOWN;
        goto end;
    }

    if ((ret = get_stream_info(avctx)) < 0)
        goto end;
    frame->nb_samples = avctx->frame_size;

#if FDKDEC_VER_AT_LEAST(2, 5) // 2.5.10
    if (flags & AACDEC_FLUSH) {
        // Only return the right amount of samples at the end; if calling the
        // decoder with AACDEC_FLUSH, it will keep returning frames indefinitely.
        frame->nb_samples = FFMIN(s->flush_samples, frame->nb_samples);
        av_log(s, AV_LOG_DEBUG, "Returning %d/%d delayed samples.\n",
                                frame->nb_samples, s->flush_samples);
        s->flush_samples -= frame->nb_samples;
    } else {
        // Trim off samples from the start to compensate for extra decoder
        // delay. We could also just adjust the pts, but this avoids
        // including the extra samples in the output altogether.
        if (s->delay_samples) {
            int drop_samples = FFMIN(s->delay_samples, frame->nb_samples);
            av_log(s, AV_LOG_DEBUG, "Dropping %d/%d delayed samples.\n",
                                    drop_samples, s->delay_samples);
            s->delay_samples  -= drop_samples;
            frame->nb_samples -= drop_samples;
            input_offset = drop_samples * avctx->ch_layout.nb_channels;
            if (frame->nb_samples <= 0)
                return 0;
        }
    }
#endif

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        goto end;

    memcpy(frame->extended_data[0], s->decoder_buffer + input_offset,
           avctx->ch_layout.nb_channels * frame->nb_samples *
           av_get_bytes_per_sample(avctx->sample_fmt));

    *got_frame_ptr = 1;
    ret = avpkt->size - valid;

end:
    return ret;
}

static av_cold void fdk_aac_decode_flush(AVCodecContext *avctx)
{
    FDKAACDecContext *s = avctx->priv_data;
    AAC_DECODER_ERROR err;

    if (!s->handle)
        return;

    if ((err = aacDecoder_SetParam(s->handle,
                                   AAC_TPDEC_CLEAR_BUFFER, 1)) != AAC_DEC_OK)
        av_log(avctx, AV_LOG_WARNING, "failed to clear buffer when flushing\n");
}

const FFCodec ff_libfdk_aac_decoder = {
    .p.name         = "libfdk_aac",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Fraunhofer FDK AAC"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AAC,
    .priv_data_size = sizeof(FDKAACDecContext),
    .init           = fdk_aac_decode_init,
    FF_CODEC_DECODE_CB(fdk_aac_decode_frame),
    .close          = fdk_aac_decode_close,
    .flush          = fdk_aac_decode_flush,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF
#if FDKDEC_VER_AT_LEAST(2, 5) // 2.5.10
                      | AV_CODEC_CAP_DELAY
#endif
    ,
    .p.priv_class   = &fdk_aac_dec_class,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE |
                      FF_CODEC_CAP_INIT_CLEANUP,
    .p.wrapper_name = "libfdk",
};
