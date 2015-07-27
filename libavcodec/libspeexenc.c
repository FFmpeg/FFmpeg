/*
 * Copyright (C) 2009 Justin Ruggles
 * Copyright (c) 2009 Xuggle Incorporated
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
 * libspeex Speex audio encoder
 *
 * Usage Guide
 * This explains the values that need to be set prior to initialization in
 * order to control various encoding parameters.
 *
 * Channels
 *     Speex only supports mono or stereo, so avctx->channels must be set to
 *     1 or 2.
 *
 * Sample Rate / Encoding Mode
 *     Speex has 3 modes, each of which uses a specific sample rate.
 *         narrowband     :  8 kHz
 *         wideband       : 16 kHz
 *         ultra-wideband : 32 kHz
 *     avctx->sample_rate must be set to one of these 3 values.  This will be
 *     used to set the encoding mode.
 *
 * Rate Control
 *     VBR mode is turned on by setting AV_CODEC_FLAG_QSCALE in avctx->flags.
 *     avctx->global_quality is used to set the encoding quality.
 *     For CBR mode, avctx->bit_rate can be used to set the constant bitrate.
 *     Alternatively, the 'cbr_quality' option can be set from 0 to 10 to set
 *     a constant bitrate based on quality.
 *     For ABR mode, set avctx->bit_rate and set the 'abr' option to 1.
 *     Approx. Bitrate Range:
 *         narrowband     : 2400 - 25600 bps
 *         wideband       : 4000 - 43200 bps
 *         ultra-wideband : 4400 - 45200 bps
 *
 * Complexity
 *     Encoding complexity is controlled by setting avctx->compression_level.
 *     The valid range is 0 to 10.  A higher setting gives generally better
 *     quality at the expense of encoding speed.  This does not affect the
 *     bit rate.
 *
 * Frames-per-Packet
 *     The encoder defaults to using 1 frame-per-packet.  However, it is
 *     sometimes desirable to use multiple frames-per-packet to reduce the
 *     amount of container overhead.  This can be done by setting the
 *     'frames_per_packet' option to a value 1 to 8.
 *
 *
 * Optional features
 * Speex encoder supports several optional features, which can be useful
 * for some conditions.
 *
 * Voice Activity Detection
 *     When enabled, voice activity detection detects whether the audio
 *     being encoded is speech or silence/background noise. VAD is always
 *     implicitly activated when encoding in VBR, so the option is only useful
 *     in non-VBR operation. In this case, Speex detects non-speech periods and
 *     encodes them with just enough bits to reproduce the background noise.
 *
 * Discontinuous Transmission (DTX)
 *     DTX is an addition to VAD/VBR operation, that makes it possible to stop transmitting
 *     completely when the background noise is stationary.
 *     In file-based operation only 5 bits are used for such frames.
 */

#include <speex/speex.h>
#include <speex/speex_header.h>
#include <speex/speex_stereo.h>

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "internal.h"
#include "audio_frame_queue.h"

/* TODO: Think about converting abr, vad, dtx and such flags to a bit field */
typedef struct LibSpeexEncContext {
    AVClass *class;             ///< AVClass for private options
    SpeexBits bits;             ///< libspeex bitwriter context
    SpeexHeader header;         ///< libspeex header struct
    void *enc_state;            ///< libspeex encoder state
    int frames_per_packet;      ///< number of frames to encode in each packet
    float vbr_quality;          ///< VBR quality 0.0 to 10.0
    int cbr_quality;            ///< CBR quality 0 to 10
    int abr;                    ///< flag to enable ABR
    int vad;                    ///< flag to enable VAD
    int dtx;                    ///< flag to enable DTX
    int pkt_frame_count;        ///< frame count for the current packet
    AudioFrameQueue afq;        ///< frame queue
} LibSpeexEncContext;

static av_cold void print_enc_params(AVCodecContext *avctx,
                                     LibSpeexEncContext *s)
{
    const char *mode_str = "unknown";

    av_log(avctx, AV_LOG_DEBUG, "channels: %d\n", avctx->channels);
    switch (s->header.mode) {
    case SPEEX_MODEID_NB:  mode_str = "narrowband";     break;
    case SPEEX_MODEID_WB:  mode_str = "wideband";       break;
    case SPEEX_MODEID_UWB: mode_str = "ultra-wideband"; break;
    }
    av_log(avctx, AV_LOG_DEBUG, "mode: %s\n", mode_str);
    if (s->header.vbr) {
        av_log(avctx, AV_LOG_DEBUG, "rate control: VBR\n");
        av_log(avctx, AV_LOG_DEBUG, "  quality: %f\n", s->vbr_quality);
    } else if (s->abr) {
        av_log(avctx, AV_LOG_DEBUG, "rate control: ABR\n");
        av_log(avctx, AV_LOG_DEBUG, "  bitrate: %d bps\n", avctx->bit_rate);
    } else {
        av_log(avctx, AV_LOG_DEBUG, "rate control: CBR\n");
        av_log(avctx, AV_LOG_DEBUG, "  bitrate: %d bps\n", avctx->bit_rate);
    }
    av_log(avctx, AV_LOG_DEBUG, "complexity: %d\n",
           avctx->compression_level);
    av_log(avctx, AV_LOG_DEBUG, "frame size: %d samples\n",
           avctx->frame_size);
    av_log(avctx, AV_LOG_DEBUG, "frames per packet: %d\n",
           s->frames_per_packet);
    av_log(avctx, AV_LOG_DEBUG, "packet size: %d\n",
           avctx->frame_size * s->frames_per_packet);
    av_log(avctx, AV_LOG_DEBUG, "voice activity detection: %d\n", s->vad);
    av_log(avctx, AV_LOG_DEBUG, "discontinuous transmission: %d\n", s->dtx);
}

static av_cold int encode_init(AVCodecContext *avctx)
{
    LibSpeexEncContext *s = avctx->priv_data;
    const SpeexMode *mode;
    uint8_t *header_data;
    int header_size;
    int32_t complexity;

    /* channels */
    if (avctx->channels < 1 || avctx->channels > 2) {
        av_log(avctx, AV_LOG_ERROR, "Invalid channels (%d). Only stereo and "
               "mono are supported\n", avctx->channels);
        return AVERROR(EINVAL);
    }

    /* sample rate and encoding mode */
    switch (avctx->sample_rate) {
    case  8000: mode = &speex_nb_mode;  break;
    case 16000: mode = &speex_wb_mode;  break;
    case 32000: mode = &speex_uwb_mode; break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Sample rate of %d Hz is not supported. "
               "Resample to 8, 16, or 32 kHz.\n", avctx->sample_rate);
        return AVERROR(EINVAL);
    }

    /* initialize libspeex */
    s->enc_state = speex_encoder_init(mode);
    if (!s->enc_state) {
        av_log(avctx, AV_LOG_ERROR, "Error initializing libspeex\n");
        return -1;
    }
    speex_init_header(&s->header, avctx->sample_rate, avctx->channels, mode);

    /* rate control method and parameters */
    if (avctx->flags & AV_CODEC_FLAG_QSCALE) {
        /* VBR */
        s->header.vbr = 1;
        s->vad = 1; /* VAD is always implicitly activated for VBR */
        speex_encoder_ctl(s->enc_state, SPEEX_SET_VBR, &s->header.vbr);
        s->vbr_quality = av_clipf(avctx->global_quality / (float)FF_QP2LAMBDA,
                                  0.0f, 10.0f);
        speex_encoder_ctl(s->enc_state, SPEEX_SET_VBR_QUALITY, &s->vbr_quality);
    } else {
        s->header.bitrate = avctx->bit_rate;
        if (avctx->bit_rate > 0) {
            /* CBR or ABR by bitrate */
            if (s->abr) {
                speex_encoder_ctl(s->enc_state, SPEEX_SET_ABR,
                                  &s->header.bitrate);
                speex_encoder_ctl(s->enc_state, SPEEX_GET_ABR,
                                  &s->header.bitrate);
            } else {
                speex_encoder_ctl(s->enc_state, SPEEX_SET_BITRATE,
                                  &s->header.bitrate);
                speex_encoder_ctl(s->enc_state, SPEEX_GET_BITRATE,
                                  &s->header.bitrate);
            }
        } else {
            /* CBR by quality */
            speex_encoder_ctl(s->enc_state, SPEEX_SET_QUALITY,
                              &s->cbr_quality);
            speex_encoder_ctl(s->enc_state, SPEEX_GET_BITRATE,
                              &s->header.bitrate);
        }
        /* stereo side information adds about 800 bps to the base bitrate */
        /* TODO: this should be calculated exactly */
        avctx->bit_rate = s->header.bitrate + (avctx->channels == 2 ? 800 : 0);
    }

    /* VAD is activated with VBR or can be turned on by itself */
    if (s->vad)
        speex_encoder_ctl(s->enc_state, SPEEX_SET_VAD, &s->vad);

    /* Activiting Discontinuous Transmission */
    if (s->dtx) {
        speex_encoder_ctl(s->enc_state, SPEEX_SET_DTX, &s->dtx);
        if (!(s->abr || s->vad || s->header.vbr))
            av_log(avctx, AV_LOG_WARNING, "DTX is not much of use without ABR, VAD or VBR\n");
    }

    /* set encoding complexity */
    if (avctx->compression_level > FF_COMPRESSION_DEFAULT) {
        complexity = av_clip(avctx->compression_level, 0, 10);
        speex_encoder_ctl(s->enc_state, SPEEX_SET_COMPLEXITY, &complexity);
    }
    speex_encoder_ctl(s->enc_state, SPEEX_GET_COMPLEXITY, &complexity);
    avctx->compression_level = complexity;

    /* set packet size */
    avctx->frame_size = s->header.frame_size;
    s->header.frames_per_packet = s->frames_per_packet;

    /* set encoding delay */
    speex_encoder_ctl(s->enc_state, SPEEX_GET_LOOKAHEAD, &avctx->initial_padding);
    ff_af_queue_init(avctx, &s->afq);

    /* create header packet bytes from header struct */
    /* note: libspeex allocates the memory for header_data, which is freed
             below with speex_header_free() */
    header_data = speex_header_to_packet(&s->header, &header_size);

    /* allocate extradata */
    avctx->extradata = av_malloc(header_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata) {
        speex_header_free(header_data);
        speex_encoder_destroy(s->enc_state);
        av_log(avctx, AV_LOG_ERROR, "memory allocation error\n");
        return AVERROR(ENOMEM);
    }

    /* copy header packet to extradata */
    memcpy(avctx->extradata, header_data, header_size);
    avctx->extradata_size = header_size;
    speex_header_free(header_data);

    /* init libspeex bitwriter */
    speex_bits_init(&s->bits);

    print_enc_params(avctx, s);
    return 0;
}

static int encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                        const AVFrame *frame, int *got_packet_ptr)
{
    LibSpeexEncContext *s = avctx->priv_data;
    int16_t *samples      = frame ? (int16_t *)frame->data[0] : NULL;
    int ret;

    if (samples) {
        /* encode Speex frame */
        if (avctx->channels == 2)
            speex_encode_stereo_int(samples, s->header.frame_size, &s->bits);
        speex_encode_int(s->enc_state, samples, &s->bits);
        s->pkt_frame_count++;
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0)
            return ret;
    } else {
        /* handle end-of-stream */
        if (!s->pkt_frame_count)
            return 0;
        /* add extra terminator codes for unused frames in last packet */
        while (s->pkt_frame_count < s->frames_per_packet) {
            speex_bits_pack(&s->bits, 15, 5);
            s->pkt_frame_count++;
        }
    }

    /* write output if all frames for the packet have been encoded */
    if (s->pkt_frame_count == s->frames_per_packet) {
        s->pkt_frame_count = 0;
        if ((ret = ff_alloc_packet2(avctx, avpkt, speex_bits_nbytes(&s->bits), 0)) < 0)
            return ret;
        ret = speex_bits_write(&s->bits, avpkt->data, avpkt->size);
        speex_bits_reset(&s->bits);

        /* Get the next frame pts/duration */
        ff_af_queue_remove(&s->afq, s->frames_per_packet * avctx->frame_size,
                           &avpkt->pts, &avpkt->duration);

        avpkt->size = ret;
        *got_packet_ptr = 1;
        return 0;
    }
    return 0;
}

static av_cold int encode_close(AVCodecContext *avctx)
{
    LibSpeexEncContext *s = avctx->priv_data;

    speex_bits_destroy(&s->bits);
    speex_encoder_destroy(s->enc_state);

    ff_af_queue_close(&s->afq);
    av_freep(&avctx->extradata);

    return 0;
}

#define OFFSET(x) offsetof(LibSpeexEncContext, x)
#define AE AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "abr",               "Use average bit rate",                      OFFSET(abr),               AV_OPT_TYPE_INT, { .i64 = 0 }, 0,   1, AE },
    { "cbr_quality",       "Set quality value (0 to 10) for CBR",       OFFSET(cbr_quality),       AV_OPT_TYPE_INT, { .i64 = 8 }, 0,  10, AE },
    { "frames_per_packet", "Number of frames to encode in each packet", OFFSET(frames_per_packet), AV_OPT_TYPE_INT, { .i64 = 1 }, 1,   8, AE },
    { "vad",               "Voice Activity Detection",                  OFFSET(vad),               AV_OPT_TYPE_INT, { .i64 = 0 }, 0,   1, AE },
    { "dtx",               "Discontinuous Transmission",                OFFSET(dtx),               AV_OPT_TYPE_INT, { .i64 = 0 }, 0,   1, AE },
    { NULL },
};

static const AVClass speex_class = {
    .class_name = "libspeex",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const AVCodecDefault defaults[] = {
    { "b",                 "0" },
    { "compression_level", "3" },
    { NULL },
};

AVCodec ff_libspeex_encoder = {
    .name           = "libspeex",
    .long_name      = NULL_IF_CONFIG_SMALL("libspeex Speex"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_SPEEX,
    .priv_data_size = sizeof(LibSpeexEncContext),
    .init           = encode_init,
    .encode2        = encode_frame,
    .close          = encode_close,
    .capabilities   = AV_CODEC_CAP_DELAY,
    .sample_fmts    = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .channel_layouts = (const uint64_t[]){ AV_CH_LAYOUT_MONO,
                                           AV_CH_LAYOUT_STEREO,
                                           0 },
    .supported_samplerates = (const int[]){ 8000, 16000, 32000, 0 },
    .priv_class     = &speex_class,
    .defaults       = defaults,
};
