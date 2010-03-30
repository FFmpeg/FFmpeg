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

static void amr_decode_fix_avctx(AVCodecContext *avctx)
{
    const int is_amr_wb = 1 + (avctx->codec_id == CODEC_ID_AMR_WB);

    if (!avctx->sample_rate)
        avctx->sample_rate = 8000 * is_amr_wb;

    if (!avctx->channels)
        avctx->channels = 1;

    avctx->frame_size = 160 * is_amr_wb;
    avctx->sample_fmt = SAMPLE_FMT_S16;
}

#if CONFIG_LIBOPENCORE_AMRNB

#include <opencore-amrnb/interf_dec.h>
#include <opencore-amrnb/interf_enc.h>

static const char nb_bitrate_unsupported[] =
    "bitrate not supported: use one of 4.75k, 5.15k, 5.9k, 6.7k, 7.4k, 7.95k, 10.2k or 12.2k\n";

/* Common code for fixed and float version*/
typedef struct AMR_bitrates {
    int       rate;
    enum Mode mode;
} AMR_bitrates;

/* Match desired bitrate */
static int getBitrateMode(int bitrate)
{
    /* make the correspondance between bitrate and mode */
    AMR_bitrates rates[] = { { 4750, MR475},
                             { 5150, MR515},
                             { 5900, MR59},
                             { 6700, MR67},
                             { 7400, MR74},
                             { 7950, MR795},
                             {10200, MR102},
                             {12200, MR122}, };
    int i;

    for (i = 0; i < 8; i++)
        if (rates[i].rate == bitrate)
            return rates[i].mode;
    /* no bitrate matching, return an error */
    return -1;
}

typedef struct AMRContext {
    int   frameCount;
    void *decState;
    int  *enstate;
    int   enc_bitrate;
} AMRContext;

static av_cold int amr_nb_decode_init(AVCodecContext *avctx)
{
    AMRContext *s = avctx->priv_data;

    s->frameCount = 0;
    s->decState   = Decoder_Interface_init();
    if (!s->decState) {
        av_log(avctx, AV_LOG_ERROR, "Decoder_Interface_init error\r\n");
        return -1;
    }

    amr_decode_fix_avctx(avctx);

    if (avctx->channels > 1) {
        av_log(avctx, AV_LOG_ERROR, "amr_nb: multichannel decoding not supported\n");
        return -1;
    }

    return 0;
}

static av_cold int amr_nb_decode_close(AVCodecContext *avctx)
{
    AMRContext *s = avctx->priv_data;

    Decoder_Interface_exit(s->decState);
    return 0;
}

static int amr_nb_decode_frame(AVCodecContext *avctx, void *data,
                               int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AMRContext *s = avctx->priv_data;
    const uint8_t *amrData = buf;
    static const uint8_t block_size[16] = { 12, 13, 15, 17, 19, 20, 26, 31, 5, 0, 0, 0, 0, 0, 0, 0 };
    enum Mode dec_mode;
    int packet_size;

    /* av_log(NULL, AV_LOG_DEBUG, "amr_decode_frame buf=%p buf_size=%d frameCount=%d!!\n",
              buf, buf_size, s->frameCount); */

    dec_mode = (buf[0] >> 3) & 0x000F;
    packet_size = block_size[dec_mode] + 1;

    if (packet_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "amr frame too short (%u, should be %u)\n",
               buf_size, packet_size);
        return -1;
    }

    s->frameCount++;
    /* av_log(NULL, AV_LOG_DEBUG, "packet_size=%d amrData= 0x%X %X %X %X\n",
              packet_size, amrData[0], amrData[1], amrData[2], amrData[3]); */
    /* call decoder */
    Decoder_Interface_Decode(s->decState, amrData, data, 0);
    *data_size = 160 * 2;

    return packet_size;
}

AVCodec libopencore_amrnb_decoder = {
    "libopencore_amrnb",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_AMR_NB,
    sizeof(AMRContext),
    amr_nb_decode_init,
    NULL,
    amr_nb_decode_close,
    amr_nb_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("OpenCORE Adaptive Multi-Rate (AMR) Narrow-Band"),
};

static av_cold int amr_nb_encode_init(AVCodecContext *avctx)
{
    AMRContext *s = avctx->priv_data;

    s->frameCount = 0;

    if (avctx->sample_rate != 8000) {
        av_log(avctx, AV_LOG_ERROR, "Only 8000Hz sample rate supported\n");
        return -1;
    }

    if (avctx->channels != 1) {
        av_log(avctx, AV_LOG_ERROR, "Only mono supported\n");
        return -1;
    }

    avctx->frame_size  = 160;
    avctx->coded_frame = avcodec_alloc_frame();

    s->enstate=Encoder_Interface_init(0);
    if (!s->enstate) {
        av_log(avctx, AV_LOG_ERROR, "Encoder_Interface_init error\n");
        return -1;
    }

    if ((s->enc_bitrate = getBitrateMode(avctx->bit_rate)) < 0) {
        av_log(avctx, AV_LOG_ERROR, nb_bitrate_unsupported);
        return -1;
    }

    return 0;
}

static av_cold int amr_nb_encode_close(AVCodecContext *avctx)
{
    AMRContext *s = avctx->priv_data;

    Encoder_Interface_exit(s->enstate);
    av_freep(&avctx->coded_frame);
    return 0;
}

static int amr_nb_encode_frame(AVCodecContext *avctx,
                               unsigned char *frame/*out*/,
                               int buf_size, void *data/*in*/)
{
    AMRContext *s = avctx->priv_data;
    int written;

    if ((s->enc_bitrate = getBitrateMode(avctx->bit_rate)) < 0) {
        av_log(avctx, AV_LOG_ERROR, nb_bitrate_unsupported);
        return -1;
    }

    written = Encoder_Interface_Encode(s->enstate, s->enc_bitrate, data,
                                       frame, 0);
    /* av_log(NULL, AV_LOG_DEBUG, "amr_nb_encode_frame encoded %u bytes, bitrate %u, first byte was %#02x\n",
              written, s->enc_bitrate, frame[0] ); */

    return written;
}

AVCodec libopencore_amrnb_encoder = {
    "libopencore_amrnb",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_AMR_NB,
    sizeof(AMRContext),
    amr_nb_encode_init,
    amr_nb_encode_frame,
    amr_nb_encode_close,
    NULL,
    .sample_fmts = (const enum SampleFormat[]){SAMPLE_FMT_S16,SAMPLE_FMT_NONE},
    .long_name = NULL_IF_CONFIG_SMALL("OpenCORE Adaptive Multi-Rate (AMR) Narrow-Band"),
};

#endif

/* -----------AMR wideband ------------*/
#if CONFIG_LIBOPENCORE_AMRWB

#ifdef _TYPEDEF_H
//To avoid duplicate typedefs from typedef in amr-nb
#define typedef_h
#endif

#include <opencore-amrwb/dec_if.h>
#include <opencore-amrwb/if_rom.h>

static const char wb_bitrate_unsupported[] =
    "bitrate not supported: use one of 6.6k, 8.85k, 12.65k, 14.25k, 15.85k, 18.25k, 19.85k, 23.05k, or 23.85k\n";

/* Common code for fixed and float version*/
typedef struct AMRWB_bitrates {
    int rate;
    int mode;
} AMRWB_bitrates;

typedef struct AMRWBContext {
    int    frameCount;
    void  *state;
    int    mode;
    Word16 allow_dtx;
} AMRWBContext;

static av_cold int amr_wb_decode_init(AVCodecContext *avctx)
{
    AMRWBContext *s = avctx->priv_data;

    s->frameCount = 0;
    s->state      = D_IF_init();

    amr_decode_fix_avctx(avctx);

    if (avctx->channels > 1) {
        av_log(avctx, AV_LOG_ERROR, "amr_wb: multichannel decoding not supported\n");
        return -1;
    }

    return 0;
}

static int amr_wb_decode_frame(AVCodecContext *avctx, void *data,
                               int *data_size, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    AMRWBContext *s = avctx->priv_data;
    const uint8_t *amrData = buf;
    int mode;
    int packet_size;
    static const uint8_t block_size[16] = {18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 6, 0, 0, 0, 1, 1};

    if (!buf_size)
        /* nothing to do */
        return 0;

    mode = (amrData[0] >> 3) & 0x000F;
    packet_size = block_size[mode];

    if (packet_size > buf_size) {
        av_log(avctx, AV_LOG_ERROR, "amr frame too short (%u, should be %u)\n",
               buf_size, packet_size + 1);
        return -1;
    }

    s->frameCount++;
    D_IF_decode(s->state, amrData, data, _good_frame);
    *data_size = 320 * 2;
    return packet_size;
}

static int amr_wb_decode_close(AVCodecContext *avctx)
{
    AMRWBContext *s = avctx->priv_data;

    D_IF_exit(s->state);
    return 0;
}

AVCodec libopencore_amrwb_decoder = {
    "libopencore_amrwb",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_AMR_WB,
    sizeof(AMRWBContext),
    amr_wb_decode_init,
    NULL,
    amr_wb_decode_close,
    amr_wb_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("OpenCORE Adaptive Multi-Rate (AMR) Wide-Band"),
};

#endif /* CONFIG_LIBOPENCORE_AMRWB */
