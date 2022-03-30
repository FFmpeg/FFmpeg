/*
 * AMR Audio decoder stub
 * Copyright (c) 2003 The FFmpeg project
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

#include "config_components.h"

#include <inttypes.h>

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/opt.h"
#include "avcodec.h"
#include "audio_frame_queue.h"
#include "codec_internal.h"
#include "encode.h"
#include "internal.h"

#if CONFIG_LIBOPENCORE_AMRNB_DECODER || CONFIG_LIBOPENCORE_AMRWB_DECODER
static int amr_decode_fix_avctx(AVCodecContext *avctx)
{
    const int is_amr_wb = 1 + (avctx->codec_id == AV_CODEC_ID_AMR_WB);

    if (!avctx->sample_rate)
        avctx->sample_rate = 8000 * is_amr_wb;

    if (avctx->ch_layout.nb_channels > 1) {
        avpriv_report_missing_feature(avctx, "multi-channel AMR");
        return AVERROR_PATCHWELCOME;
    }

    av_channel_layout_uninit(&avctx->ch_layout);
    avctx->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;
    avctx->sample_fmt     = AV_SAMPLE_FMT_S16;
    return 0;
}
#endif

#if CONFIG_LIBOPENCORE_AMRNB

#include <opencore-amrnb/interf_dec.h>
#include <opencore-amrnb/interf_enc.h>

typedef struct AMRContext {
    AVClass *av_class;
    void *dec_state;
    void *enc_state;
    int   enc_bitrate;
    int   enc_mode;
    int   enc_dtx;
    int   enc_last_frame;
    AudioFrameQueue afq;
} AMRContext;

#if CONFIG_LIBOPENCORE_AMRNB_DECODER
static av_cold int amr_nb_decode_init(AVCodecContext *avctx)
{
    AMRContext *s  = avctx->priv_data;
    int ret;

    if ((ret = amr_decode_fix_avctx(avctx)) < 0)
        return ret;

    s->dec_state   = Decoder_Interface_init();
    if (!s->dec_state) {
        av_log(avctx, AV_LOG_ERROR, "Decoder_Interface_init error\n");
        return -1;
    }

    return 0;
}

static av_cold int amr_nb_decode_close(AVCodecContext *avctx)
{
    AMRContext *s = avctx->priv_data;

    Decoder_Interface_exit(s->dec_state);

    return 0;
}

static int amr_nb_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AMRContext *s      = avctx->priv_data;
    static const uint8_t block_size[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0 };
    enum Mode dec_mode;
    int packet_size, ret;

    ff_dlog(avctx, "amr_decode_frame buf=%p buf_size=%d frame_count=%d!!\n",
            buf, buf_size, avctx->frame_number);

    /* get output buffer */
    frame->nb_samples = 160;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    dec_mode    = (buf[0] >> 3) & 0x000F;
    packet_size = block_size[dec_mode] + 1;

    if (packet_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "AMR frame too short (%d, should be %d)\n",
               buf_size, packet_size);
        return AVERROR_INVALIDDATA;
    }

    ff_dlog(avctx, "packet_size=%d buf= 0x%"PRIx8" %"PRIx8" %"PRIx8" %"PRIx8"\n",
            packet_size, buf[0], buf[1], buf[2], buf[3]);
    /* call decoder */
    Decoder_Interface_Decode(s->dec_state, buf, (short *)frame->data[0], 0);

    *got_frame_ptr = 1;

    return packet_size;
}

const FFCodec ff_libopencore_amrnb_decoder = {
    .p.name         = "libopencore_amrnb",
    .p.long_name    = NULL_IF_CONFIG_SMALL("OpenCORE AMR-NB (Adaptive Multi-Rate Narrow-Band)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AMR_NB,
    .priv_data_size = sizeof(AMRContext),
    .init           = amr_nb_decode_init,
    .close          = amr_nb_decode_close,
    FF_CODEC_DECODE_CB(amr_nb_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
};
#endif /* CONFIG_LIBOPENCORE_AMRNB_DECODER */

#if CONFIG_LIBOPENCORE_AMRNB_ENCODER
/* Common code for fixed and float version*/
typedef struct AMR_bitrates {
    int       rate;
    enum Mode mode;
} AMR_bitrates;

/* Match desired bitrate */
static int get_bitrate_mode(int bitrate, void *log_ctx)
{
    /* make the correspondence between bitrate and mode */
    static const AMR_bitrates rates[] = {
        { 4750, MR475 }, { 5150, MR515 }, {  5900, MR59  }, {  6700, MR67  },
        { 7400, MR74 },  { 7950, MR795 }, { 10200, MR102 }, { 12200, MR122 }
    };
    int i, best = -1, min_diff = 0;
    char log_buf[200];

    for (i = 0; i < 8; i++) {
        if (rates[i].rate == bitrate)
            return rates[i].mode;
        if (best < 0 || abs(rates[i].rate - bitrate) < min_diff) {
            best     = i;
            min_diff = abs(rates[i].rate - bitrate);
        }
    }
    /* no bitrate matching exactly, log a warning */
    snprintf(log_buf, sizeof(log_buf), "bitrate not supported: use one of ");
    for (i = 0; i < 8; i++)
        av_strlcatf(log_buf, sizeof(log_buf), "%.2fk, ", rates[i].rate    / 1000.f);
    av_strlcatf(log_buf, sizeof(log_buf), "using %.2fk", rates[best].rate / 1000.f);
    av_log(log_ctx, AV_LOG_WARNING, "%s\n", log_buf);

    return best;
}

static const AVOption options[] = {
    { "dtx", "Allow DTX (generate comfort noise)", offsetof(AMRContext, enc_dtx), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 1, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVClass amrnb_class = {
    .class_name = "libopencore_amrnb",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static av_cold int amr_nb_encode_init(AVCodecContext *avctx)
{
    AMRContext *s = avctx->priv_data;

    if (avctx->sample_rate != 8000 && avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL) {
        av_log(avctx, AV_LOG_ERROR, "Only 8000Hz sample rate supported\n");
        return AVERROR(ENOSYS);
    }

    if (avctx->ch_layout.nb_channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return AVERROR(ENOSYS);
    }

    avctx->frame_size  = 160;
    avctx->initial_padding = 50;
    ff_af_queue_init(avctx, &s->afq);

    s->enc_state = Encoder_Interface_init(s->enc_dtx);
    if (!s->enc_state) {
        av_log(avctx, AV_LOG_ERROR, "Encoder_Interface_init error\n");
        return -1;
    }

    s->enc_mode    = get_bitrate_mode(avctx->bit_rate, avctx);
    s->enc_bitrate = avctx->bit_rate;

    return 0;
}

static av_cold int amr_nb_encode_close(AVCodecContext *avctx)
{
    AMRContext *s = avctx->priv_data;

    Encoder_Interface_exit(s->enc_state);
    ff_af_queue_close(&s->afq);
    return 0;
}

static int amr_nb_encode_frame(AVCodecContext *avctx, AVPacket *avpkt,
                               const AVFrame *frame, int *got_packet_ptr)
{
    AMRContext *s = avctx->priv_data;
    int written, ret;
    int16_t *flush_buf = NULL;
    const int16_t *samples = frame ? (const int16_t *)frame->data[0] : NULL;

    if (s->enc_bitrate != avctx->bit_rate) {
        s->enc_mode    = get_bitrate_mode(avctx->bit_rate, avctx);
        s->enc_bitrate = avctx->bit_rate;
    }

    if ((ret = ff_alloc_packet(avctx, avpkt, 32)) < 0)
        return ret;

    if (frame) {
        if (frame->nb_samples < avctx->frame_size) {
            flush_buf = av_calloc(avctx->frame_size, sizeof(*flush_buf));
            if (!flush_buf)
                return AVERROR(ENOMEM);
            memcpy(flush_buf, samples, frame->nb_samples * sizeof(*flush_buf));
            samples = flush_buf;
            if (frame->nb_samples < avctx->frame_size - avctx->initial_padding)
                s->enc_last_frame = -1;
        }
        if ((ret = ff_af_queue_add(&s->afq, frame)) < 0) {
            av_freep(&flush_buf);
            return ret;
        }
    } else {
        if (s->enc_last_frame < 0)
            return 0;
        flush_buf = av_calloc(avctx->frame_size, sizeof(*flush_buf));
        if (!flush_buf)
            return AVERROR(ENOMEM);
        samples = flush_buf;
        s->enc_last_frame = -1;
    }

    written = Encoder_Interface_Encode(s->enc_state, s->enc_mode, samples,
                                       avpkt->data, 0);
    ff_dlog(avctx, "amr_nb_encode_frame encoded %u bytes, bitrate %u, first byte was %#02x\n",
            written, s->enc_mode, avpkt->data[0]);

    /* Get the next frame pts/duration */
    ff_af_queue_remove(&s->afq, avctx->frame_size, &avpkt->pts,
                       &avpkt->duration);

    avpkt->size = written;
    *got_packet_ptr = 1;
    av_freep(&flush_buf);
    return 0;
}

const FFCodec ff_libopencore_amrnb_encoder = {
    .p.name         = "libopencore_amrnb",
    .p.long_name    = NULL_IF_CONFIG_SMALL("OpenCORE AMR-NB (Adaptive Multi-Rate Narrow-Band)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AMR_NB,
    .priv_data_size = sizeof(AMRContext),
    .init           = amr_nb_encode_init,
    FF_CODEC_ENCODE_CB(amr_nb_encode_frame),
    .close          = amr_nb_encode_close,
    .p.capabilities = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_SMALL_LAST_FRAME,
    .p.sample_fmts  = (const enum AVSampleFormat[]){ AV_SAMPLE_FMT_S16,
                                                     AV_SAMPLE_FMT_NONE },
    .p.priv_class   = &amrnb_class,
};
#endif /* CONFIG_LIBOPENCORE_AMRNB_ENCODER */

#endif /* CONFIG_LIBOPENCORE_AMRNB */

/* -----------AMR wideband ------------*/
#if CONFIG_LIBOPENCORE_AMRWB_DECODER

#include <opencore-amrwb/dec_if.h>
#include <opencore-amrwb/if_rom.h>

typedef struct AMRWBContext {
    void  *state;
} AMRWBContext;

static av_cold int amr_wb_decode_init(AVCodecContext *avctx)
{
    AMRWBContext *s = avctx->priv_data;
    int ret;

    if ((ret = amr_decode_fix_avctx(avctx)) < 0)
        return ret;

    s->state        = D_IF_init();

    return 0;
}

static int amr_wb_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                               int *got_frame_ptr, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AMRWBContext *s    = avctx->priv_data;
    int mode, ret;
    int packet_size;
    static const uint8_t block_size[16] = {18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 6, 0, 0, 0, 1, 1};

    /* get output buffer */
    frame->nb_samples = 320;
    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    mode        = (buf[0] >> 3) & 0x000F;
    packet_size = block_size[mode];

    if (packet_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "AMR frame too short (%d, should be %d)\n",
               buf_size, packet_size + 1);
        return AVERROR_INVALIDDATA;
    }
    if (!packet_size) {
        av_log(avctx, AV_LOG_ERROR, "amr packet_size invalid\n");
        return AVERROR_INVALIDDATA;
    }

    D_IF_decode(s->state, buf, (short *)frame->data[0], _good_frame);

    *got_frame_ptr = 1;

    return packet_size;
}

static int amr_wb_decode_close(AVCodecContext *avctx)
{
    AMRWBContext *s = avctx->priv_data;

    D_IF_exit(s->state);
    return 0;
}

const FFCodec ff_libopencore_amrwb_decoder = {
    .p.name         = "libopencore_amrwb",
    .p.long_name    = NULL_IF_CONFIG_SMALL("OpenCORE AMR-WB (Adaptive Multi-Rate Wide-Band)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_AMR_WB,
    .p.capabilities = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_CHANNEL_CONF,
    .p.wrapper_name = "libopencore_amrwb",
    .priv_data_size = sizeof(AMRWBContext),
    .init           = amr_wb_decode_init,
    .close          = amr_wb_decode_close,
    FF_CODEC_DECODE_CB(amr_wb_decode_frame),
};

#endif /* CONFIG_LIBOPENCORE_AMRWB_DECODER */
