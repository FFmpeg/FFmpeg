/*
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

#include "adts_header.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "decode.h"

typedef struct FTRContext {
    AVCodecContext *aac_avctx[64];   // wrapper context for AAC
    int nb_context;
    AVPacket *packet;
    AVFrame *frame;
} FTRContext;

static av_cold int ftr_init(AVCodecContext *avctx)
{
    FTRContext *s = avctx->priv_data;
    const AVCodec *codec;
    int ret;

    if (avctx->ch_layout.nb_channels > 64 ||
        avctx->ch_layout.nb_channels <= 0)
        return AVERROR(EINVAL);

    s->packet = av_packet_alloc();
    if (!s->packet)
        return AVERROR(ENOMEM);

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    s->nb_context = avctx->ch_layout.nb_channels;

    codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
    if (!codec)
        return AVERROR_DECODER_NOT_FOUND;

    for (int i = 0; i < s->nb_context; i++) {
        s->aac_avctx[i] = avcodec_alloc_context3(codec);
        if (!s->aac_avctx[i])
            return AVERROR(ENOMEM);
        ret = avcodec_open2(s->aac_avctx[i], codec, NULL);
        if (ret < 0)
            return ret;
    }

    avctx->sample_fmt = s->aac_avctx[0]->sample_fmt;
    if (!av_sample_fmt_is_planar(avctx->sample_fmt))
        return AVERROR(EINVAL);

    return 0;
}

static int ftr_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    FTRContext *s = avctx->priv_data;
    GetBitContext gb;
    int ret, ch_offset = 0;

    ret = init_get_bits8(&gb, avpkt->data, avpkt->size);
    if (ret < 0)
        return ret;

    frame->nb_samples = 0;

    for (int i = 0; i < s->nb_context; i++) {
        AVCodecContext *codec_avctx = s->aac_avctx[i];
        GetBitContext gb2 = gb;
        AACADTSHeaderInfo hdr_info;
        int size;

        if (get_bits_left(&gb) < 64)
            return AVERROR_INVALIDDATA;

        memset(&hdr_info, 0, sizeof(hdr_info));

        size = ff_adts_header_parse(&gb2, &hdr_info);
        if (size <= 0 || size * 8 > get_bits_left(&gb))
            return AVERROR_INVALIDDATA;

        if (size > s->packet->size) {
            ret = av_grow_packet(s->packet, size - s->packet->size);
            if (ret < 0)
                return ret;
        }

        ret = av_packet_make_writable(s->packet);
        if (ret < 0)
            return ret;

        memcpy(s->packet->data, avpkt->data + (get_bits_count(&gb) >> 3), size);
        s->packet->size = size;

        if (size > 12) {
            uint8_t *buf = s->packet->data;

            if (buf[3] & 0x20) {
                int tmp = buf[8];
                buf[ 9] = ~buf[9];
                buf[11] = ~buf[11];
                buf[12] = ~buf[12];
                buf[ 8] = ~buf[10];
                buf[10] = ~tmp;
            }
        }

        ret = avcodec_send_packet(codec_avctx, s->packet);
        if (ret < 0) {
            av_log(avctx, AV_LOG_ERROR, "Error submitting a packet for decoding\n");
            return ret;
        }

        ret = avcodec_receive_frame(codec_avctx, s->frame);
        if (ret < 0)
            return ret;

        if (!avctx->sample_rate) {
            avctx->sample_rate = codec_avctx->sample_rate;
        } else {
            if (avctx->sample_rate != codec_avctx->sample_rate)
                return AVERROR_INVALIDDATA;
        }

        if (!frame->nb_samples) {
            frame->nb_samples = s->frame->nb_samples;
            if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
                return ret;
        } else {
            if (frame->nb_samples != s->frame->nb_samples)
                return AVERROR_INVALIDDATA;
        }

        skip_bits_long(&gb, size * 8);

        if (ch_offset + s->frame->ch_layout.nb_channels > avctx->ch_layout.nb_channels)
            return AVERROR_INVALIDDATA;

        if (avctx->sample_fmt != codec_avctx->sample_fmt)
            return AVERROR_INVALIDDATA;

        for (int ch = 0; ch < s->frame->ch_layout.nb_channels; ch++)
            memcpy(frame->extended_data[ch_offset + ch],
                   s->frame->extended_data[ch],
                   av_get_bytes_per_sample(codec_avctx->sample_fmt) * s->frame->nb_samples);

        ch_offset += s->frame->ch_layout.nb_channels;

        if (ch_offset >= avctx->ch_layout.nb_channels)
            break;
    }

    *got_frame = 1;

    return get_bits_count(&gb) >> 3;
}

static void ftr_flush(AVCodecContext *avctx)
{
    FTRContext *s = avctx->priv_data;

    for (int i = 0; i < s->nb_context; i++)
        avcodec_flush_buffers(s->aac_avctx[i]);
}

static av_cold int ftr_close(AVCodecContext *avctx)
{
    FTRContext *s = avctx->priv_data;

    for (int i = 0; i < s->nb_context; i++)
        avcodec_free_context(&s->aac_avctx[i]);
    av_packet_free(&s->packet);
    av_frame_free(&s->frame);

    return 0;
}

const FFCodec ff_ftr_decoder = {
    .p.name         = "ftr",
    .p.long_name    = NULL_IF_CONFIG_SMALL("FTR Voice"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_FTR,
    .init           = ftr_init,
    FF_CODEC_DECODE_CB(ftr_decode_frame),
    .close          = ftr_close,
    .flush          = ftr_flush,
    .priv_data_size = sizeof(FTRContext),
    .p.capabilities = AV_CODEC_CAP_DR1,
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
