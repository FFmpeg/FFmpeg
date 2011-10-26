/*
 * AMR Audio decoder stub
 * Copyright (c) 2003 the ffmpeg project
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

#include "avcodec.h"
#include "libavutil/avstring.h"
#include "libavutil/opt.h"

static void amr_decode_fix_avctx(AVCodecContext *avctx)
{
    const int is_amr_wb = 1 + (avctx->codec_id == CODEC_ID_AMR_WB);

    if (!avctx->sample_rate)
        avctx->sample_rate = 8000 * is_amr_wb;

    if (!avctx->channels)
        avctx->channels = 1;

    avctx->frame_size = 160 * is_amr_wb;
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;
}

#if CONFIG_LIBOPENCORE_AMRNB

#include <opencore-amrnb/interf_dec.h>
#include <opencore-amrnb/interf_enc.h>

/* Common code for fixed and float version*/
typedef struct AMR_bitrates {
    int       rate;
    enum Mode mode;
} AMR_bitrates;

/* Match desired bitrate */
static int get_bitrate_mode(int bitrate, void *log_ctx)
{
    /* make the correspondance between bitrate and mode */
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

typedef struct AMRContext {
    AVClass *av_class;
    void *dec_state;
    void *enc_state;
    int   enc_bitrate;
    int   enc_mode;
    int   enc_dtx;
} AMRContext;

static const AVOption options[] = {
    { "dtx", "Allow DTX (generate comfort noise)", offsetof(AMRContext, enc_dtx), AV_OPT_TYPE_INT, { 0 }, 0, 1, AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM },
    { NULL }
};

static const AVClass class = {
    "libopencore_amrnb", av_default_item_name, options, LIBAVUTIL_VERSION_INT
};

static av_cold int amr_nb_decode_init(AVCodecContext *avctx)
{
    AMRContext *s  = avctx->priv_data;

    s->dec_state   = Decoder_Interface_init();
    if (!s->dec_state) {
        av_log(avctx, AV_LOG_ERROR, "Decoder_Interface_init error\n");
        return -1;
    }

    amr_decode_fix_avctx(avctx);

    if (avctx->channels > 1) {
        av_log(avctx, AV_LOG_ERROR, "amr_nb: multichannel decoding not supported\n");
        return AVERROR(ENOSYS);
    }

    return 0;
}

static av_cold int amr_nb_decode_close(AVCodecContext *avctx)
{
    AMRContext *s = avctx->priv_data;

    Decoder_Interface_exit(s->dec_state);
    return 0;
}

static int amr_nb_decode_frame(AVCodecContext *avctx, void *data,
                               int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AMRContext *s      = avctx->priv_data;
    static const uint8_t block_size[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0 };
    enum Mode dec_mode;
    int packet_size, out_size;

    av_dlog(avctx, "amr_decode_frame buf=%p buf_size=%d frame_count=%d!!\n",
            buf, buf_size, avctx->frame_number);

    out_size = 160 * av_get_bytes_per_sample(avctx->sample_fmt);
    if (*data_size < out_size) {
        av_log(avctx, AV_LOG_ERROR, "output buffer is too small\n");
        return AVERROR(EINVAL);
    }

    dec_mode    = (buf[0] >> 3) & 0x000F;
    packet_size = block_size[dec_mode] + 1;

    if (packet_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "amr frame too short (%u, should be %u)\n",
               buf_size, packet_size);
        return AVERROR_INVALIDDATA;
    }

    av_dlog(avctx, "packet_size=%d buf= 0x%X %X %X %X\n",
              packet_size, buf[0], buf[1], buf[2], buf[3]);
    /* call decoder */
    Decoder_Interface_Decode(s->dec_state, buf, data, 0);
    *data_size = out_size;

    return packet_size;
}

AVCodec ff_libopencore_amrnb_decoder = {
    .name           = "libopencore_amrnb",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_AMR_NB,
    .priv_data_size = sizeof(AMRContext),
    .init           = amr_nb_decode_init,
    .close          = amr_nb_decode_close,
    .decode         = amr_nb_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("OpenCORE Adaptive Multi-Rate (AMR) Narrow-Band"),
};

static av_cold int amr_nb_encode_init(AVCodecContext *avctx)
{
    AMRContext *s = avctx->priv_data;

    if (avctx->sample_rate != 8000) {
        av_log(avctx, AV_LOG_ERROR, "Only 8000Hz sample rate supported\n");
        return AVERROR(ENOSYS);
    }

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return AVERROR(ENOSYS);
    }

    avctx->frame_size  = 160;
    avctx->coded_frame = avcodec_alloc_frame();

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
    av_freep(&avctx->coded_frame);
    return 0;
}

static int amr_nb_encode_frame(AVCodecContext *avctx,
                               unsigned char *frame/*out*/,
                               int buf_size, void *data/*in*/)
{
    AMRContext *s = avctx->priv_data;
    int written;

    if (s->enc_bitrate != avctx->bit_rate) {
        s->enc_mode    = get_bitrate_mode(avctx->bit_rate, avctx);
        s->enc_bitrate = avctx->bit_rate;
    }

    written = Encoder_Interface_Encode(s->enc_state, s->enc_mode, data,
                                       frame, 0);
    av_dlog(avctx, "amr_nb_encode_frame encoded %u bytes, bitrate %u, first byte was %#02x\n",
            written, s->enc_mode, frame[0]);

    return written;
}

AVCodec ff_libopencore_amrnb_encoder = {
    .name           = "libopencore_amrnb",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_AMR_NB,
    .priv_data_size = sizeof(AMRContext),
    .init           = amr_nb_encode_init,
    .encode         = amr_nb_encode_frame,
    .close          = amr_nb_encode_close,
    .sample_fmts = (const enum AVSampleFormat[]){AV_SAMPLE_FMT_S16,AV_SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("OpenCORE Adaptive Multi-Rate (AMR) Narrow-Band"),
    .priv_class = &class,
};

#endif

/* -----------AMR wideband ------------*/
#if CONFIG_LIBOPENCORE_AMRWB

#include <opencore-amrwb/dec_if.h>
#include <opencore-amrwb/if_rom.h>

typedef struct AMRWBContext {
    void  *state;
} AMRWBContext;

static av_cold int amr_wb_decode_init(AVCodecContext *avctx)
{
    AMRWBContext *s = avctx->priv_data;

    s->state        = D_IF_init();

    amr_decode_fix_avctx(avctx);

    if (avctx->channels > 1) {
        av_log(avctx, AV_LOG_ERROR, "amr_wb: multichannel decoding not supported\n");
        return AVERROR(ENOSYS);
    }

    return 0;
}

static int amr_wb_decode_frame(AVCodecContext *avctx, void *data,
                               int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AMRWBContext *s    = avctx->priv_data;
    int mode;
    int packet_size, out_size;
    static const uint8_t block_size[16] = {18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 6, 0, 0, 0, 1, 1};

    out_size = 320 * av_get_bytes_per_sample(avctx->sample_fmt);
    if (*data_size < out_size) {
        av_log(avctx, AV_LOG_ERROR, "output buffer is too small\n");
        return AVERROR(EINVAL);
    }

    mode        = (buf[0] >> 3) & 0x000F;
    packet_size = block_size[mode];

    if (packet_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "amr frame too short (%u, should be %u)\n",
               buf_size, packet_size + 1);
        return AVERROR_INVALIDDATA;
    }

    D_IF_decode(s->state, buf, data, _good_frame);
    *data_size = out_size;
    return packet_size;
}

static int amr_wb_decode_close(AVCodecContext *avctx)
{
    AMRWBContext *s = avctx->priv_data;

    D_IF_exit(s->state);
    return 0;
}

AVCodec ff_libopencore_amrwb_decoder = {
    .name           = "libopencore_amrwb",
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = CODEC_ID_AMR_WB,
    .priv_data_size = sizeof(AMRWBContext),
    .init           = amr_wb_decode_init,
    .close          = amr_wb_decode_close,
    .decode         = amr_wb_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("OpenCORE Adaptive Multi-Rate (AMR) Wide-Band"),
};

#endif /* CONFIG_LIBOPENCORE_AMRWB */
