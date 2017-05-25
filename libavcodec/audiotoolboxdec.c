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
#include "ac3_parser.h"
#include "bytestream.h"
#include "internal.h"
#include "mpegaudiodecheader.h"
#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "libavutil/log.h"

#if __MAC_OS_X_VERSION_MIN_REQUIRED < 101100
#define kAudioFormatEnhancedAC3 'ec-3'
#endif

typedef struct ATDecodeContext {
    AVClass *av_class;

    AudioConverterRef converter;
    AudioStreamPacketDescription pkt_desc;
    AVPacket in_pkt;
    AVPacket new_in_pkt;
    AVBSFContext *bsf;
    char *decoded_data;
    int channel_map[64];

    uint8_t *extradata;
    int extradata_size;

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
    case AV_CODEC_ID_EAC3:
        return kAudioFormatEnhancedAC3;
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

static int ffat_get_channel_id(AudioChannelLabel label)
{
    if (label == 0)
        return -1;
    else if (label <= kAudioChannelLabel_LFEScreen)
        return label - 1;
    else if (label <= kAudioChannelLabel_RightSurround)
        return label + 4;
    else if (label <= kAudioChannelLabel_CenterSurround)
        return label + 1;
    else if (label <= kAudioChannelLabel_RightSurroundDirect)
        return label + 23;
    else if (label <= kAudioChannelLabel_TopBackRight)
        return label - 1;
    else if (label < kAudioChannelLabel_RearSurroundLeft)
        return -1;
    else if (label <= kAudioChannelLabel_RearSurroundRight)
        return label - 29;
    else if (label <= kAudioChannelLabel_RightWide)
        return label - 4;
    else if (label == kAudioChannelLabel_LFE2)
        return ff_ctzll(AV_CH_LOW_FREQUENCY_2);
    else if (label == kAudioChannelLabel_Mono)
        return ff_ctzll(AV_CH_FRONT_CENTER);
    else
        return -1;
}

static int ffat_compare_channel_descriptions(const void* a, const void* b)
{
    const AudioChannelDescription* da = a;
    const AudioChannelDescription* db = b;
    return ffat_get_channel_id(da->mChannelLabel) - ffat_get_channel_id(db->mChannelLabel);
}

static AudioChannelLayout *ffat_convert_layout(AudioChannelLayout *layout, UInt32* size)
{
    AudioChannelLayoutTag tag = layout->mChannelLayoutTag;
    AudioChannelLayout *new_layout;
    if (tag == kAudioChannelLayoutTag_UseChannelDescriptions)
        return layout;
    else if (tag == kAudioChannelLayoutTag_UseChannelBitmap)
        AudioFormatGetPropertyInfo(kAudioFormatProperty_ChannelLayoutForBitmap,
                                   sizeof(UInt32), &layout->mChannelBitmap, size);
    else
        AudioFormatGetPropertyInfo(kAudioFormatProperty_ChannelLayoutForTag,
                                   sizeof(AudioChannelLayoutTag), &tag, size);
    new_layout = av_malloc(*size);
    if (!new_layout) {
        av_free(layout);
        return NULL;
    }
    if (tag == kAudioChannelLayoutTag_UseChannelBitmap)
        AudioFormatGetProperty(kAudioFormatProperty_ChannelLayoutForBitmap,
                               sizeof(UInt32), &layout->mChannelBitmap, size, new_layout);
    else
        AudioFormatGetProperty(kAudioFormatProperty_ChannelLayoutForTag,
                               sizeof(AudioChannelLayoutTag), &tag, size, new_layout);
    new_layout->mChannelLayoutTag = kAudioChannelLayoutTag_UseChannelDescriptions;
    av_free(layout);
    return new_layout;
}

static int ffat_update_ctx(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    AudioStreamBasicDescription format;
    UInt32 size = sizeof(format);
    if (!AudioConverterGetProperty(at->converter,
                                   kAudioConverterCurrentInputStreamDescription,
                                   &size, &format)) {
        if (format.mSampleRate)
            avctx->sample_rate = format.mSampleRate;
        avctx->channels = format.mChannelsPerFrame;
        avctx->channel_layout = av_get_default_channel_layout(avctx->channels);
        avctx->frame_size = format.mFramesPerPacket;
    }

    if (!AudioConverterGetProperty(at->converter,
                                   kAudioConverterCurrentOutputStreamDescription,
                                   &size, &format)) {
        format.mSampleRate = avctx->sample_rate;
        format.mChannelsPerFrame = avctx->channels;
        AudioConverterSetProperty(at->converter,
                                  kAudioConverterCurrentOutputStreamDescription,
                                  size, &format);
    }

    if (!AudioConverterGetPropertyInfo(at->converter, kAudioConverterOutputChannelLayout,
                                       &size, NULL) && size) {
        AudioChannelLayout *layout = av_malloc(size);
        uint64_t layout_mask = 0;
        int i;
        if (!layout)
            return AVERROR(ENOMEM);
        AudioConverterGetProperty(at->converter, kAudioConverterOutputChannelLayout,
                                  &size, layout);
        if (!(layout = ffat_convert_layout(layout, &size)))
            return AVERROR(ENOMEM);
        for (i = 0; i < layout->mNumberChannelDescriptions; i++) {
            int id = ffat_get_channel_id(layout->mChannelDescriptions[i].mChannelLabel);
            if (id < 0)
                goto done;
            if (layout_mask & (1 << id))
                goto done;
            layout_mask |= 1 << id;
            layout->mChannelDescriptions[i].mChannelFlags = i; // Abusing flags as index
        }
        avctx->channel_layout = layout_mask;
        qsort(layout->mChannelDescriptions, layout->mNumberChannelDescriptions,
              sizeof(AudioChannelDescription), &ffat_compare_channel_descriptions);
        for (i = 0; i < layout->mNumberChannelDescriptions; i++)
            at->channel_map[i] = layout->mChannelDescriptions[i].mChannelFlags;
done:
        av_free(layout);
    }

    if (!avctx->frame_size)
        avctx->frame_size = 2048;

    return 0;
}

static void put_descr(PutByteContext *pb, int tag, unsigned int size)
{
    int i = 3;
    bytestream2_put_byte(pb, tag);
    for (; i > 0; i--)
        bytestream2_put_byte(pb, (size >> (7 * i)) | 0x80);
    bytestream2_put_byte(pb, size & 0x7F);
}

static uint8_t* ffat_get_magic_cookie(AVCodecContext *avctx, UInt32 *cookie_size)
{
    ATDecodeContext *at = avctx->priv_data;
    if (avctx->codec_id == AV_CODEC_ID_AAC) {
        char *extradata;
        PutByteContext pb;
        *cookie_size = 5 + 3 + 5+13 + 5+at->extradata_size;
        if (!(extradata = av_malloc(*cookie_size)))
            return NULL;

        bytestream2_init_writer(&pb, extradata, *cookie_size);

        // ES descriptor
        put_descr(&pb, 0x03, 3 + 5+13 + 5+at->extradata_size);
        bytestream2_put_be16(&pb, 0);
        bytestream2_put_byte(&pb, 0x00); // flags (= no flags)

        // DecoderConfig descriptor
        put_descr(&pb, 0x04, 13 + 5+at->extradata_size);

        // Object type indication
        bytestream2_put_byte(&pb, 0x40);

        bytestream2_put_byte(&pb, 0x15); // flags (= Audiostream)

        bytestream2_put_be24(&pb, 0); // Buffersize DB

        bytestream2_put_be32(&pb, 0); // maxbitrate
        bytestream2_put_be32(&pb, 0); // avgbitrate

        // DecoderSpecific info descriptor
        put_descr(&pb, 0x05, at->extradata_size);
        bytestream2_put_buffer(&pb, at->extradata, at->extradata_size);
        return extradata;
    } else {
        *cookie_size = at->extradata_size;
        return at->extradata;
    }
}

static av_cold int ffat_usable_extradata(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    return at->extradata_size &&
           (avctx->codec_id == AV_CODEC_ID_ALAC ||
            avctx->codec_id == AV_CODEC_ID_QDM2 ||
            avctx->codec_id == AV_CODEC_ID_QDMC ||
            avctx->codec_id == AV_CODEC_ID_AAC);
}

static int ffat_set_extradata(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    if (ffat_usable_extradata(avctx)) {
        OSStatus status;
        UInt32 cookie_size;
        uint8_t *cookie = ffat_get_magic_cookie(avctx, &cookie_size);
        if (!cookie)
            return AVERROR(ENOMEM);

        status = AudioConverterSetProperty(at->converter,
                                           kAudioConverterDecompressionMagicCookie,
                                           cookie_size, cookie);
        if (status != 0)
            av_log(avctx, AV_LOG_WARNING, "AudioToolbox cookie error: %i\n", (int)status);

        if (cookie != at->extradata)
            av_free(cookie);
    }
    return 0;
}

static av_cold int ffat_create_decoder(AVCodecContext *avctx, AVPacket *pkt)
{
    ATDecodeContext *at = avctx->priv_data;
    OSStatus status;
    int i;

    enum AVSampleFormat sample_fmt = (avctx->bits_per_raw_sample == 32) ?
                                     AV_SAMPLE_FMT_S32 : AV_SAMPLE_FMT_S16;

    AudioStreamBasicDescription in_format = {
        .mFormatID = ffat_get_format_id(avctx->codec_id, avctx->profile),
        .mBytesPerPacket = (avctx->codec_id == AV_CODEC_ID_ILBC) ? avctx->block_align : 0,
    };
    AudioStreamBasicDescription out_format = {
        .mFormatID = kAudioFormatLinearPCM,
        .mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
        .mFramesPerPacket = 1,
        .mBitsPerChannel = av_get_bytes_per_sample(sample_fmt) * 8,
    };

    avctx->sample_fmt = sample_fmt;

    if (ffat_usable_extradata(avctx)) {
        UInt32 format_size = sizeof(in_format);
        UInt32 cookie_size;
        uint8_t *cookie = ffat_get_magic_cookie(avctx, &cookie_size);
        if (!cookie)
            return AVERROR(ENOMEM);
        status = AudioFormatGetProperty(kAudioFormatProperty_FormatInfo,
                                        cookie_size, cookie, &format_size, &in_format);
        if (cookie != at->extradata)
            av_free(cookie);
        if (status != 0) {
            av_log(avctx, AV_LOG_ERROR, "AudioToolbox header-parse error: %i\n", (int)status);
            return AVERROR_UNKNOWN;
        }
#if CONFIG_MP1_AT_DECODER || CONFIG_MP2_AT_DECODER || CONFIG_MP3_AT_DECODER
    } else if (pkt && pkt->size >= 4 &&
               (avctx->codec_id == AV_CODEC_ID_MP1 ||
                avctx->codec_id == AV_CODEC_ID_MP2 ||
                avctx->codec_id == AV_CODEC_ID_MP3)) {
        enum AVCodecID codec_id;
        int bit_rate;
        if (ff_mpa_decode_header(AV_RB32(pkt->data), &avctx->sample_rate,
                                 &in_format.mChannelsPerFrame, &avctx->frame_size,
                                 &bit_rate, &codec_id) < 0)
            return AVERROR_INVALIDDATA;
        avctx->bit_rate = bit_rate;
        in_format.mSampleRate = avctx->sample_rate;
#endif
#if CONFIG_AC3_AT_DECODER || CONFIG_EAC3_AT_DECODER
    } else if (pkt && pkt->size >= 7 &&
               (avctx->codec_id == AV_CODEC_ID_AC3 ||
                avctx->codec_id == AV_CODEC_ID_EAC3)) {
        AC3HeaderInfo hdr, *phdr = &hdr;
        GetBitContext gbc;
        init_get_bits(&gbc, pkt->data, pkt->size);
        if (avpriv_ac3_parse_header(&gbc, &phdr) < 0)
            return AVERROR_INVALIDDATA;
        in_format.mSampleRate = hdr.sample_rate;
        in_format.mChannelsPerFrame = hdr.channels;
        avctx->frame_size = hdr.num_blocks * 256;
        avctx->bit_rate = hdr.bit_rate;
#endif
    } else {
        in_format.mSampleRate = avctx->sample_rate ? avctx->sample_rate : 44100;
        in_format.mChannelsPerFrame = avctx->channels ? avctx->channels : 1;
    }

    avctx->sample_rate = out_format.mSampleRate = in_format.mSampleRate;
    avctx->channels = out_format.mChannelsPerFrame = in_format.mChannelsPerFrame;

    if (avctx->codec_id == AV_CODEC_ID_ADPCM_IMA_QT)
        in_format.mFramesPerPacket = 64;

    status = AudioConverterNew(&in_format, &out_format, &at->converter);

    if (status != 0) {
        av_log(avctx, AV_LOG_ERROR, "AudioToolbox init error: %i\n", (int)status);
        return AVERROR_UNKNOWN;
    }

    if ((status = ffat_set_extradata(avctx)) < 0)
        return status;

    for (i = 0; i < (sizeof(at->channel_map) / sizeof(at->channel_map[0])); i++)
        at->channel_map[i] = i;

    ffat_update_ctx(avctx);

    if(!(at->decoded_data = av_malloc(av_get_bytes_per_sample(avctx->sample_fmt)
                                      * avctx->frame_size * avctx->channels)))
        return AVERROR(ENOMEM);

    at->last_pts = AV_NOPTS_VALUE;

    return 0;
}

static av_cold int ffat_init_decoder(AVCodecContext *avctx)
{
    ATDecodeContext *at = avctx->priv_data;
    at->extradata = avctx->extradata;
    at->extradata_size = avctx->extradata_size;

    if ((avctx->channels && avctx->sample_rate) || ffat_usable_extradata(avctx))
        return ffat_create_decoder(avctx, NULL);
    else
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

    av_packet_unref(&at->in_pkt);
    av_packet_move_ref(&at->in_pkt, &at->new_in_pkt);

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

#define COPY_SAMPLES(type) \
    type *in_ptr = (type*)at->decoded_data; \
    type *end_ptr = in_ptr + frame->nb_samples * avctx->channels; \
    type *out_ptr = (type*)frame->data[0]; \
    for (; in_ptr < end_ptr; in_ptr += avctx->channels, out_ptr += avctx->channels) { \
        int c; \
        for (c = 0; c < avctx->channels; c++) \
            out_ptr[c] = in_ptr[at->channel_map[c]]; \
    }

static void ffat_copy_samples(AVCodecContext *avctx, AVFrame *frame)
{
    ATDecodeContext *at = avctx->priv_data;
    if (avctx->sample_fmt == AV_SAMPLE_FMT_S32) {
        COPY_SAMPLES(int32_t);
    } else {
        COPY_SAMPLES(int16_t);
    }
}

static int ffat_decode(AVCodecContext *avctx, void *data,
                       int *got_frame_ptr, AVPacket *avpkt)
{
    ATDecodeContext *at = avctx->priv_data;
    AVFrame *frame = data;
    int pkt_size = avpkt->size;
    AVPacket filtered_packet = {0};
    OSStatus ret;
    AudioBufferList out_buffers;

    if (avctx->codec_id == AV_CODEC_ID_AAC && avpkt->size > 2 &&
        (AV_RB16(avpkt->data) & 0xfff0) == 0xfff0) {
        AVPacket filter_pkt = {0};
        if (!at->bsf) {
            const AVBitStreamFilter *bsf = av_bsf_get_by_name("aac_adtstoasc");
            if(!bsf)
                return AVERROR_BSF_NOT_FOUND;
            if ((ret = av_bsf_alloc(bsf, &at->bsf)))
                return ret;
            if (((ret = avcodec_parameters_from_context(at->bsf->par_in, avctx)) < 0) ||
                ((ret = av_bsf_init(at->bsf)) < 0)) {
                av_bsf_free(&at->bsf);
                return ret;
            }
        }

        if ((ret = av_packet_ref(&filter_pkt, avpkt)) < 0)
            return ret;

        if ((ret = av_bsf_send_packet(at->bsf, &filter_pkt)) < 0) {
            av_packet_unref(&filter_pkt);
            return ret;
        }

        if ((ret = av_bsf_receive_packet(at->bsf, &filtered_packet)) < 0)
            return ret;

        if (!at->extradata_size) {
            uint8_t *side_data;
            int side_data_size = 0;

            side_data = av_packet_get_side_data(&filtered_packet, AV_PKT_DATA_NEW_EXTRADATA,
                                                &side_data_size);
            if (side_data_size) {
                at->extradata = av_mallocz(side_data_size + AV_INPUT_BUFFER_PADDING_SIZE);
                if (!at->extradata)
                    return AVERROR(ENOMEM);
                at->extradata_size = side_data_size;
                memcpy(at->extradata, side_data, side_data_size);
            }
        }

        avpkt = &filtered_packet;
    }

    if (!at->converter) {
        if ((ret = ffat_create_decoder(avctx, avpkt)) < 0) {
            av_packet_unref(&filtered_packet);
            return ret;
        }
    }

    out_buffers = (AudioBufferList){
        .mNumberBuffers = 1,
        .mBuffers = {
            {
                .mNumberChannels = avctx->channels,
                .mDataByteSize = av_get_bytes_per_sample(avctx->sample_fmt) * avctx->frame_size
                                 * avctx->channels,
            }
        }
    };

    av_packet_unref(&at->new_in_pkt);

    if (avpkt->size) {
        if (filtered_packet.data) {
            at->new_in_pkt = filtered_packet;
        } else if ((ret = av_packet_ref(&at->new_in_pkt, avpkt)) < 0) {
            return ret;
        }
    } else {
        at->eof = 1;
    }

    frame->sample_rate = avctx->sample_rate;

    frame->nb_samples = avctx->frame_size;

    out_buffers.mBuffers[0].mData = at->decoded_data;

    ret = AudioConverterFillComplexBuffer(at->converter, ffat_decode_callback, avctx,
                                          &frame->nb_samples, &out_buffers, NULL);
    if ((!ret || ret == 1) && frame->nb_samples) {
        if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
            return ret;
        ffat_copy_samples(avctx, frame);
        *got_frame_ptr = 1;
        if (at->last_pts != AV_NOPTS_VALUE) {
            frame->pts = at->last_pts;
#if FF_API_PKT_PTS
FF_DISABLE_DEPRECATION_WARNINGS
            frame->pkt_pts = at->last_pts;
FF_ENABLE_DEPRECATION_WARNINGS
#endif
            at->last_pts = avpkt->pts;
        }
    } else if (ret && ret != 1) {
        av_log(avctx, AV_LOG_WARNING, "Decode error: %i\n", ret);
    } else {
        at->last_pts = avpkt->pts;
    }

    return pkt_size;
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
    av_bsf_free(&at->bsf);
    av_packet_unref(&at->new_in_pkt);
    av_packet_unref(&at->in_pkt);
    av_free(at->decoded_data);
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
FFAT_DEC(eac3,         AV_CODEC_ID_EAC3)
FFAT_DEC(gsm_ms,       AV_CODEC_ID_GSM_MS)
FFAT_DEC(ilbc,         AV_CODEC_ID_ILBC)
FFAT_DEC(mp1,          AV_CODEC_ID_MP1)
FFAT_DEC(mp2,          AV_CODEC_ID_MP2)
FFAT_DEC(mp3,          AV_CODEC_ID_MP3)
FFAT_DEC(pcm_alaw,     AV_CODEC_ID_PCM_ALAW)
FFAT_DEC(pcm_mulaw,    AV_CODEC_ID_PCM_MULAW)
FFAT_DEC(qdmc,         AV_CODEC_ID_QDMC)
FFAT_DEC(qdm2,         AV_CODEC_ID_QDM2)
