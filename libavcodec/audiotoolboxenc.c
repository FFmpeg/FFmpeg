/*
 * Audio Toolbox system codecs
 *
 * copyright (c) 2016 Rodger Combs
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include <AudioToolbox/AudioToolbox.h>

#include "config.h"
#include "audio_frame_queue.h"
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "libavformat/isom.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"

typedef struct ATDecodeContext {
    AVClass *av_class;
    int mode;
    int quality;

    AudioConverterRef converter;
    AudioStreamPacketDescription pkt_desc;
    AVFrame in_frame;
    AVFrame new_in_frame;

    unsigned pkt_size;
    AudioFrameQueue afq;
    int eof;
    int frame_size;
} ATDecodeContext;

static UInt32 ffat_get_format_id(enum AVCodecID codec, int profile)
{
    switch (codec) {
    case AV_CODEC_ID_AAC:
        switch (profile) {
        case FF_PROFILE_AAC_LOW:
        default:
            return kAudioFormatMPEG4AAC;
        case FF_PROFILE_AAC_HE:
            return kAudioFormatMPEG4AAC_HE;
        case FF_PROFILE_AAC_HE_V2:
            return kAudioFormatMPEG4AAC_HE_V2;
        case FF_PROFILE_AAC_LD:
            return kAudioFormatMPEG4AAC_LD;
        case FF_PROFILE_AAC_ELD:
            return kAudioFormatMPEG4AAC_ELD;
        }
    case AV_CODEC_ID_ADPCM_IMA_QT:
        return kAudioFormatAppleIMA4;
    case AV_CODEC_ID_ALAC:
        return kAudioFormatAppleLossless;
    case AV_CODEC_ID_ILBC:
        return kAudioFormatiLBC;
    case AV_CODEC_ID_PCM_ALAW:
        return kAudioFormatALaw;
    case AV_CODEC_ID_PCM_MULAW:
        return kAudioFormatULaw;
    default:
        av_assert0(!"Invalid codec ID!");
        return 0;
    }
}

static void ffat_update_ctx(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    UInt32 size = sizeof(unsigned);
    AudioConverterPrimeInfo prime_info;
    AudioStreamBasicDescription out_format;

    AudioConverterGetProperty(at->converter,
                              kAudioConverterPropertyMaximumOutputPacketSize,
                              &size, &at->pkt_size);

    if (at->pkt_size <= 0)
        at->pkt_size = 1024 * 50;

    size = sizeof(prime_info);

    if (!AudioConverterGetProperty(at->converter,
                                   kAudioConverterPrimeInfo,
                                   &size, &prime_info)) {
        avctx->initial_padding = prime_info.leadingFrames;
    }

    size = sizeof(out_format);
    if (!AudioConverterGetProperty(at->converter,
                                   kAudioConverterCurrentOutputStreamDescription,
                                   &size, &out_format)) {
        if (out_format.mFramesPerPacket)
            avctx->frame_size = out_format.mFramesPerPacket;
        if (out_format.mBytesPerPacket && avctx->codec_id == AV_CODEC_ID_ILBC)
            avctx->block_align = out_format.mBytesPerPacket;
    }

    at->frame_size = avctx->frame_size;
    if (avctx->codec_id == AV_CODEC_ID_PCM_MULAW ||
        avctx->codec_id == AV_CODEC_ID_PCM_ALAW) {
        at->pkt_size *= 1024;
        avctx->frame_size *= 1024;
    }
}

static int read_descr(GetByteContext *gb, int *tag)
{
    int len = 0;
    int count = 4;
    *tag = bytestream2_get_byte(gb);
    while (count--) {
        int c = bytestream2_get_byte(gb);
        len = (len << 7) | (c & 0x7f);
        if (!(c & 0x80))
            break;
    }
    return len;
}

static int get_ilbc_mode(AVCodecContext *avctx)
{
    if (avctx->block_align == 38)
        return 20;
    else if (avctx->block_align == 50)
        return 30;
    else if (avctx->bit_rate > 0)
        return avctx->bit_rate <= 14000 ? 30 : 20;
    else
        return 30;
}

static av_cold int ffat_init_encoder(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    OSStatus status;

    AudioStreamBasicDescription in_format = {
        .mSampleRate = avctx->sample_rate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = ((avctx->sample_fmt == AV_SAMPLE_FMT_FLT ||
                          avctx->sample_fmt == AV_SAMPLE_FMT_DBL) ? kAudioFormatFlagIsFloat
                        : avctx->sample_fmt == AV_SAMPLE_FMT_U8 ? 0
                        : kAudioFormatFlagIsSignedInteger)
                        | kAudioFormatFlagIsPacked,
        .mBytesPerPacket = av_get_bytes_per_sample(avctx->sample_fmt) * avctx->channels,
        .mFramesPerPacket = 1,
        .mBytesPerFrame = av_get_bytes_per_sample(avctx->sample_fmt) * avctx->channels,
        .mChannelsPerFrame = avctx->channels,
        .mBitsPerChannel = av_get_bytes_per_sample(avctx->sample_fmt) * 8,
    };
    AudioStreamBasicDescription out_format = {
        .mSampleRate = avctx->sample_rate,
        .mFormatID = ffat_get_format_id(avctx->codec_id, avctx->profile),
        .mChannelsPerFrame = in_format.mChannelsPerFrame,
    };
    AudioChannelLayout channel_layout = {
        .mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelBitmap,
        .mChannelBitmap = avctx->channel_layout,
    };
    UInt32 size = sizeof(channel_layout);

    if (avctx->codec_id == AV_CODEC_ID_ILBC) {
        int mode = get_ilbc_mode(avctx);
        out_format.mFramesPerPacket  = 8000 * mode / 1000;
        out_format.mBytesPerPacket   = (mode == 20 ? 38 : 50);
    }

    status = AudioConverterNew(&in_format, &out_format, &at->converter);

    if (status != 0) {
        av_log(avctx, AV_LOG_ERROR, "AudioToolbox init error: %i\n", (int)status);
        return AVERROR_UNKNOWN;
    }

    size = sizeof(UInt32);

    AudioConverterSetProperty(at->converter, kAudioConverterInputChannelLayout,
                              size, &channel_layout);
    AudioConverterSetProperty(at->converter, kAudioConverterOutputChannelLayout,
                              size, &channel_layout);

    if (avctx->bits_per_raw_sample) {
        size = sizeof(avctx->bits_per_raw_sample);
        AudioConverterSetProperty(at->converter,
                                  kAudioConverterPropertyBitDepthHint,
                                  size, &avctx->bits_per_raw_sample);
    }

    if (at->mode == -1)
        at->mode = (avctx->flags & AV_CODEC_FLAG_QSCALE) ?
                   kAudioCodecBitRateControlMode_Variable :
                   kAudioCodecBitRateControlMode_Constant;

    AudioConverterSetProperty(at->converter, kAudioCodecPropertyBitRateControlMode,
                              size, &at->mode);

    if (at->mode == kAudioCodecBitRateControlMode_Variable) {
        int q = avctx->global_quality / FF_QP2LAMBDA;
        if (q < 0 || q > 14) {
            av_log(avctx, AV_LOG_WARNING,
                   "VBR quality %d out of range, should be 0-14\n", q);
            q = av_clip(q, 0, 14);
        }
        q = 127 - q * 9;
        AudioConverterSetProperty(at->converter, kAudioCodecPropertySoundQualityForVBR,
                                  size, &q);
    } else if (avctx->bit_rate > 0) {
        UInt32 rate = avctx->bit_rate;
        AudioConverterSetProperty(at->converter, kAudioConverterEncodeBitRate,
                                  size, &rate);
    }

    at->quality = 96 - at->quality * 32;
    AudioConverterSetProperty(at->converter, kAudioConverterCodecQuality,
                              size, &at->quality);

    if (!AudioConverterGetPropertyInfo(at->converter, kAudioConverterCompressionMagicCookie,
                                       &avctx->extradata_size, NULL) &&
        avctx->extradata_size) {
        int extradata_size = avctx->extradata_size;
        uint8_t *extradata;
        if (!(avctx->extradata = av_mallocz(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE)))
            return AVERROR(ENOMEM);
        if (avctx->codec_id == AV_CODEC_ID_ALAC) {
            avctx->extradata_size = 0x24;
            AV_WB32(avctx->extradata,     0x24);
            AV_WB32(avctx->extradata + 4, MKBETAG('a','l','a','c'));
            extradata = avctx->extradata + 12;
            avctx->extradata_size = 0x24;
        } else {
            extradata = avctx->extradata;
        }
        status = AudioConverterGetProperty(at->converter,
                                           kAudioConverterCompressionMagicCookie,
                                           &extradata_size, extradata);
        if (status != 0) {
            av_log(avctx, AV_LOG_ERROR, "AudioToolbox cookie error: %i\n", (int)status);
            return AVERROR_UNKNOWN;
        } else if (avctx->codec_id == AV_CODEC_ID_AAC) {
            GetByteContext gb;
            int tag, len;
            bytestream2_init(&gb, extradata, extradata_size);
            do {
                len = read_descr(&gb, &tag);
                if (tag == MP4DecConfigDescrTag) {
                    bytestream2_skip(&gb, 13);
                    len = read_descr(&gb, &tag);
                    if (tag == MP4DecSpecificDescrTag) {
                        len = FFMIN(gb.buffer_end - gb.buffer, len);
                        memmove(extradata, gb.buffer, len);
                        avctx->extradata_size = len;
                        break;
                    }
                } else if (tag == MP4ESDescrTag) {
                    int flags;
                    bytestream2_skip(&gb, 2);
                    flags = bytestream2_get_byte(&gb);
                    if (flags & 0x80) //streamDependenceFlag
                        bytestream2_skip(&gb, 2);
                    if (flags & 0x40) //URL_Flag
                        bytestream2_skip(&gb, bytestream2_get_byte(&gb));
                    if (flags & 0x20) //OCRstreamFlag
                        bytestream2_skip(&gb, 2);
                }
            } while (bytestream2_get_bytes_left(&gb));
        } else if (avctx->codec_id != AV_CODEC_ID_ALAC) {
            avctx->extradata_size = extradata_size;
        }
    }

    ffat_update_ctx(avctx);

#if !TARGET_OS_IPHONE && __MAC_OS_X_VERSION_MIN_REQUIRED >= 1090
    if (at->mode == kAudioCodecBitRateControlMode_Variable && avctx->rc_max_rate) {
        int max_size = avctx->rc_max_rate * avctx->frame_size / avctx->sample_rate;
        if (max_size)
        AudioConverterSetProperty(at->converter, kAudioCodecPropertyPacketSizeLimitForVBR,
                                  size, &max_size);
    }
#endif

    ff_af_queue_init(avctx, &at->afq);

    return 0;
}

static OSStatus ffat_encode_callback(AudioConverterRef converter, UInt32 *nb_packets,
                                     AudioBufferList *data,
                                     AudioStreamPacketDescription **packets,
                                     void *inctx)
{
    AVCodecContext *avctx = inctx;
    ATDecodeContext *at = avctx->priv_data;

    if (at->eof) {
        *nb_packets = 0;
        if (packets) {
            *packets = &at->pkt_desc;
            at->pkt_desc.mDataByteSize = 0;
        }
        return 0;
    }

    av_frame_unref(&at->in_frame);
    av_frame_move_ref(&at->in_frame, &at->new_in_frame);

    if (!at->in_frame.data[0]) {
        *nb_packets = 0;
        return 1;
    }

    data->mNumberBuffers              = 1;
    data->mBuffers[0].mNumberChannels = 0;
    data->mBuffers[0].mDataByteSize   = at->in_frame.nb_samples *
                                        av_get_bytes_per_sample(avctx->sample_fmt) *
                                        avctx->channels;
    data->mBuffers[0].mData           = at->in_frame.data[0];
    *nb_packets = (at->in_frame.nb_samples + (at->frame_size - 1)) / at->frame_size;

    if (packets) {
        *packets = &at->pkt_desc;
        at->pkt_desc.mDataByteSize = data->mBuffers[0].mDataByteSize;
        at->pkt_desc.mVariableFramesInPacket = at->in_frame.nb_samples;
    }

    return 0;
}

static int ffat_encode(AVCodecContext *avctx, AVPacket *avpkt,
                       const AVFrame *frame, int *got_packet_ptr)
{
    ATDecodeContext *at = avctx->priv_data;
    OSStatus ret;

    AudioBufferList out_buffers = {
        .mNumberBuffers = 1,
        .mBuffers = {
            {
                .mNumberChannels = avctx->channels,
                .mDataByteSize = at->pkt_size,
            }
        }
    };
    AudioStreamPacketDescription out_pkt_desc = {0};

    if ((ret = ff_alloc_packet2(avctx, avpkt, at->pkt_size, 0)) < 0)
        return ret;

    av_frame_unref(&at->new_in_frame);

    if (frame) {
        if ((ret = ff_af_queue_add(&at->afq, frame)) < 0)
            return ret;
        if ((ret = av_frame_ref(&at->new_in_frame, frame)) < 0)
            return ret;
    } else {
        at->eof = 1;
    }

    out_buffers.mBuffers[0].mData = avpkt->data;

    *got_packet_ptr = avctx->frame_size / at->frame_size;

    ret = AudioConverterFillComplexBuffer(at->converter, ffat_encode_callback, avctx,
                                          got_packet_ptr, &out_buffers,
                                          (avctx->frame_size > at->frame_size) ? NULL : &out_pkt_desc);
    if ((!ret || ret == 1) && *got_packet_ptr) {
        avpkt->size = out_buffers.mBuffers[0].mDataByteSize;
        ff_af_queue_remove(&at->afq, out_pkt_desc.mVariableFramesInPacket ?
                                     out_pkt_desc.mVariableFramesInPacket :
                                     avctx->frame_size,
                           &avpkt->pts,
                           &avpkt->duration);
    } else if (ret && ret != 1) {
        av_log(avctx, AV_LOG_WARNING, "Encode error: %i\n", ret);
    }

    return 0;
}

static av_cold void ffat_encode_flush(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    AudioConverterReset(at->converter);
    av_frame_unref(&at->new_in_frame);
    av_frame_unref(&at->in_frame);
}

static av_cold int ffat_close_encoder(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    AudioConverterDispose(at->converter);
    av_frame_unref(&at->new_in_frame);
    av_frame_unref(&at->in_frame);
    ff_af_queue_close(&at->afq);
    return 0;
}

static const AVProfile aac_profiles[] = {
    { FF_PROFILE_AAC_LOW,   "LC"       },
    { FF_PROFILE_AAC_HE,    "HE-AAC"   },
    { FF_PROFILE_AAC_HE_V2, "HE-AACv2" },
    { FF_PROFILE_AAC_LD,    "LD"       },
    { FF_PROFILE_AAC_ELD,   "ELD"      },
    { FF_PROFILE_UNKNOWN },
};

#define AE AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    {"aac_at_mode", "ratecontrol mode", offsetof(ATDecodeContext, mode), AV_OPT_TYPE_INT, {.i64 = -1}, -1, kAudioCodecBitRateControlMode_Variable, AE, "mode"},
        {"auto", "VBR if global quality is given; CBR otherwise", 0, AV_OPT_TYPE_CONST, {.i64 = -1}, INT_MIN, INT_MAX, AE, "mode"},
        {"cbr",  "constant bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = kAudioCodecBitRateControlMode_Constant}, INT_MIN, INT_MAX, AE, "mode"},
        {"abr",  "long-term average bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = kAudioCodecBitRateControlMode_LongTermAverage}, INT_MIN, INT_MAX, AE, "mode"},
        {"cvbr", "constrained variable bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = kAudioCodecBitRateControlMode_VariableConstrained}, INT_MIN, INT_MAX, AE, "mode"},
        {"vbr" , "variable bitrate", 0, AV_OPT_TYPE_CONST, {.i64 = kAudioCodecBitRateControlMode_Variable}, INT_MIN, INT_MAX, AE, "mode"},
    {"aac_at_quality", "quality vs speed control", offsetof(ATDecodeContext, quality), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 2, AE},
    { NULL },
};

#define FFAT_ENC_CLASS(NAME) \
    static const AVClass ffat_##NAME##_enc_class = { \
        .class_name = "at_" #NAME "_enc", \
        .item_name  = av_default_item_name, \
        .option     = options, \
        .version    = LIBAVUTIL_VERSION_INT, \
    };

#define FFAT_ENC(NAME, ID, PROFILES, ...) \
    FFAT_ENC_CLASS(NAME) \
    AVCodec ff_##NAME##_at_encoder = { \
        .name           = #NAME "_at", \
        .long_name      = NULL_IF_CONFIG_SMALL(#NAME " (AudioToolbox)"), \
        .type           = AVMEDIA_TYPE_AUDIO, \
        .id             = ID, \
        .priv_data_size = sizeof(ATDecodeContext), \
        .init           = ffat_init_encoder, \
        .close          = ffat_close_encoder, \
        .encode2        = ffat_encode, \
        .flush          = ffat_encode_flush, \
        .priv_class     = &ffat_##NAME##_enc_class, \
        .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY __VA_ARGS__, \
        .sample_fmts    = (const enum AVSampleFormat[]) { \
            AV_SAMPLE_FMT_S16, \
            AV_SAMPLE_FMT_U8,  AV_SAMPLE_FMT_NONE \
        }, \
        .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE, \
        .profiles       = PROFILES, \
    };

FFAT_ENC(aac,          AV_CODEC_ID_AAC,          aac_profiles)
//FFAT_ENC(adpcm_ima_qt, AV_CODEC_ID_ADPCM_IMA_QT, NULL)
FFAT_ENC(alac,         AV_CODEC_ID_ALAC,         NULL, | AV_CODEC_CAP_VARIABLE_FRAME_SIZE | AV_CODEC_CAP_LOSSLESS)
FFAT_ENC(ilbc,         AV_CODEC_ID_ILBC,         NULL)
FFAT_ENC(pcm_alaw,     AV_CODEC_ID_PCM_ALAW,     NULL)
FFAT_ENC(pcm_mulaw,    AV_CODEC_ID_PCM_MULAW,    NULL)
