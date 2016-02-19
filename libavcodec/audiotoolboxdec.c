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
#include "avcodec.h"
#include "bytestream.h"
#include "internal.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"

typedef struct ATDecodeContext {
    AVClass *av_class;

    AudioConverterRef converter;
    AudioStreamPacketDescription pkt_desc;
    AVPacket in_pkt;
    AVPacket new_in_pkt;

    unsigned pkt_size;
    int64_t last_pts;
    int eof;
} ATDecodeContext;

static UInt32 ffat_get_format_id(enum AVCodecID codec, int profile)
{
    switch (codec) {
    case AV_CODEC_ID_AAC:
        return kAudioFormatMPEG4AAC;
    case AV_CODEC_ID_AC3:
        return kAudioFormatAC3;
    case AV_CODEC_ID_ADPCM_IMA_QT:
        return kAudioFormatAppleIMA4;
    case AV_CODEC_ID_ALAC:
        return kAudioFormatAppleLossless;
    case AV_CODEC_ID_AMR_NB:
        return kAudioFormatAMR;
    case AV_CODEC_ID_GSM_MS:
        return kAudioFormatMicrosoftGSM;
    case AV_CODEC_ID_ILBC:
        return kAudioFormatiLBC;
    case AV_CODEC_ID_MP1:
        return kAudioFormatMPEGLayer1;
    case AV_CODEC_ID_MP2:
        return kAudioFormatMPEGLayer2;
    case AV_CODEC_ID_MP3:
        return kAudioFormatMPEGLayer3;
    case AV_CODEC_ID_PCM_ALAW:
        return kAudioFormatALaw;
    case AV_CODEC_ID_PCM_MULAW:
        return kAudioFormatULaw;
    case AV_CODEC_ID_QDMC:
        return kAudioFormatQDesign;
    case AV_CODEC_ID_QDM2:
        return kAudioFormatQDesign2;
    default:
        av_assert0(!"Invalid codec ID!");
        return 0;
    }
}

static void ffat_update_ctx(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    AudioStreamBasicDescription in_format;
    UInt32 size = sizeof(in_format);
    if (!AudioConverterGetProperty(at->converter,
                                   kAudioConverterCurrentInputStreamDescription,
                                   &size, &in_format)) {
        avctx->channels = in_format.mChannelsPerFrame;
        at->pkt_size = in_format.mFramesPerPacket;
    }

    if (!at->pkt_size)
        at->pkt_size = 2048;
}

static void put_descr(PutByteContext *pb, int tag, unsigned int size)
{
    int i = 3;
    bytestream2_put_byte(pb, tag);
    for (; i > 0; i--)
        bytestream2_put_byte(pb, (size >> (7 * i)) | 0x80);
    bytestream2_put_byte(pb, size & 0x7F);
}

static av_cold int ffat_init_decoder(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    OSStatus status;

    enum AVSampleFormat sample_fmt = (avctx->bits_per_raw_sample == 32) ?
                                     AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16;

    AudioStreamBasicDescription in_format = {
        .mSampleRate = avctx->sample_rate ? avctx->sample_rate : 44100,
        .mFormatID = ffat_get_format_id(avctx->codec_id, avctx->profile),
        .mBytesPerPacket = avctx->block_align,
        .mChannelsPerFrame = avctx->channels ? avctx->channels : 1,
    };
    AudioStreamBasicDescription out_format = {
        .mSampleRate = in_format.mSampleRate,
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
        .mFramesPerPacket = 1,
        .mChannelsPerFrame = in_format.mChannelsPerFrame,
        .mBitsPerChannel = av_get_bytes_per_sample(sample_fmt) * 8,
    };

    avctx->sample_fmt = sample_fmt;

    if (avctx->codec_id == AV_CODEC_ID_ADPCM_IMA_QT)
        in_format.mFramesPerPacket = 64;

    status = AudioConverterNew(&in_format, &out_format, &at->converter);

    if (status != 0) {
        av_log(avctx, AV_LOG_ERROR, "AudioToolbox init error: %i\n", (int)status);
        return AVERROR_UNKNOWN;
    }

    if (avctx->extradata_size) {
        char *extradata = avctx->extradata;
        int extradata_size = avctx->extradata_size;
        if (avctx->codec_id == AV_CODEC_ID_AAC) {
            PutByteContext pb;
            extradata_size = 5 + 3 + 5+13 + 5+avctx->extradata_size;
            if (!(extradata = av_malloc(extradata_size)))
                return AVERROR(ENOMEM);

            bytestream2_init_writer(&pb, extradata, extradata_size);

            // ES descriptor
            put_descr(&pb, 0x03, 3 + 5+13 + 5+avctx->extradata_size);
            bytestream2_put_be16(&pb, 0);
            bytestream2_put_byte(&pb, 0x00); // flags (= no flags)

            // DecoderConfig descriptor
            put_descr(&pb, 0x04, 13 + 5+avctx->extradata_size);

            // Object type indication
            bytestream2_put_byte(&pb, 0x40);

            bytestream2_put_byte(&pb, 0x15); // flags (= Audiostream)

            bytestream2_put_be24(&pb, 0); // Buffersize DB

            bytestream2_put_be32(&pb, 0); // maxbitrate
            bytestream2_put_be32(&pb, 0); // avgbitrate

            // DecoderSpecific info descriptor
            put_descr(&pb, 0x05, avctx->extradata_size);
            bytestream2_put_buffer(&pb, avctx->extradata, avctx->extradata_size);
        }

        status = AudioConverterSetProperty(at->converter,
                                           kAudioConverterDecompressionMagicCookie,
                                           extradata_size, extradata);
        if (status != 0)
            av_log(avctx, AV_LOG_WARNING, "AudioToolbox cookie error: %i\n", (int)status);
    }

    ffat_update_ctx(avctx);

    at->last_pts = AV_NOPTS_VALUE;

    return 0;
}

static OSStatus ffat_decode_callback(AudioConverterRef converter, UInt32 *nb_packets,
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

    av_packet_move_ref(&at->in_pkt, &at->new_in_pkt);
    at->new_in_pkt.data = 0;
    at->new_in_pkt.size = 0;

    if (!at->in_pkt.data) {
        *nb_packets = 0;
        return 1;
    }

    data->mNumberBuffers              = 1;
    data->mBuffers[0].mNumberChannels = 0;
    data->mBuffers[0].mDataByteSize   = at->in_pkt.size;
    data->mBuffers[0].mData           = at->in_pkt.data;
    *nb_packets = 1;

    if (packets) {
        *packets = &at->pkt_desc;
        at->pkt_desc.mDataByteSize = at->in_pkt.size;
    }

    return 0;
}

static int ffat_decode(AVCodecContext *avctx, void *data,
                       int *got_frame_ptr, AVPacket *avpkt)
{
    ATDecodeContext *at = avctx->priv_data;
    AVFrame *frame = data;
    OSStatus ret;

    AudioBufferList out_buffers = {
        .mNumberBuffers = 1,
        .mBuffers = {
            {
                .mNumberChannels = avctx->channels,
                .mDataByteSize = av_get_bytes_per_sample(avctx->sample_fmt) * at->pkt_size * avctx->channels,
            }
        }
    };

    av_packet_unref(&at->new_in_pkt);

    if (avpkt->size) {
        if ((ret = av_packet_ref(&at->new_in_pkt, avpkt)) < 0)
            return ret;
    } else {
        at->eof = 1;
    }

    frame->sample_rate = avctx->sample_rate;

    frame->nb_samples = at->pkt_size;
    ff_get_buffer(avctx, frame, 0);

    out_buffers.mBuffers[0].mData = frame->data[0];

    ret = AudioConverterFillComplexBuffer(at->converter, ffat_decode_callback, avctx,
                                          &frame->nb_samples, &out_buffers, NULL);
    if ((!ret || ret == 1) && frame->nb_samples) {
        *got_frame_ptr = 1;
        if (at->last_pts != AV_NOPTS_VALUE) {
            frame->pts = at->last_pts;
            at->last_pts = avpkt->pts;
        }
    } else if (ret && ret != 1) {
        av_log(avctx, AV_LOG_WARNING, "Decode error: %i\n", ret);
    } else {
        at->last_pts = avpkt->pts;
    }

    return avpkt->size;
}

static av_cold void ffat_decode_flush(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    AudioConverterReset(at->converter);
    av_packet_unref(&at->new_in_pkt);
    av_packet_unref(&at->in_pkt);
}

static av_cold int ffat_close_decoder(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    AudioConverterDispose(at->converter);
    av_packet_unref(&at->new_in_pkt);
    av_packet_unref(&at->in_pkt);
    return 0;
}

#define FFAT_DEC_CLASS(NAME) \
    static const AVClass ffat_##NAME##_dec_class = { \
        .class_name = "at_" #NAME "_dec", \
        .version    = LIBAVUTIL_VERSION_INT, \
    };

#define FFAT_DEC(NAME, ID) \
    FFAT_DEC_CLASS(NAME) \
    AVCodec ff_##NAME##_at_decoder = { \
        .name           = #NAME "_at", \
        .long_name      = NULL_IF_CONFIG_SMALL(#NAME " (AudioToolbox)"), \
        .type           = AVMEDIA_TYPE_AUDIO, \
        .id             = ID, \
        .priv_data_size = sizeof(ATDecodeContext), \
        .init           = ffat_init_decoder, \
        .close          = ffat_close_decoder, \
        .decode         = ffat_decode, \
        .flush          = ffat_decode_flush, \
        .priv_class     = &ffat_##NAME##_dec_class, \
        .capabilities   = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY, \
        .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE, \
    };

FFAT_DEC(aac,          AV_CODEC_ID_AAC)
FFAT_DEC(ac3,          AV_CODEC_ID_AC3)
FFAT_DEC(adpcm_ima_qt, AV_CODEC_ID_ADPCM_IMA_QT)
FFAT_DEC(alac,         AV_CODEC_ID_ALAC)
FFAT_DEC(amr_nb,       AV_CODEC_ID_AMR_NB)
FFAT_DEC(gsm_ms,       AV_CODEC_ID_GSM_MS)
FFAT_DEC(ilbc,         AV_CODEC_ID_ILBC)
FFAT_DEC(mp1,          AV_CODEC_ID_MP1)
FFAT_DEC(mp2,          AV_CODEC_ID_MP2)
FFAT_DEC(mp3,          AV_CODEC_ID_MP3)
FFAT_DEC(pcm_alaw,     AV_CODEC_ID_PCM_ALAW)
FFAT_DEC(pcm_mulaw,    AV_CODEC_ID_PCM_MULAW)
FFAT_DEC(qdmc,         AV_CODEC_ID_QDMC)
FFAT_DEC(qdm2,         AV_CODEC_ID_QDM2)
