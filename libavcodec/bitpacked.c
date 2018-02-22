/*
 * Unpack bit-packed streams to formats supported by FFmpeg
 * Copyright (c) 2017 Savoir-faire Linux, Inc
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

/* Development sponsored by CBC/Radio-Canada */

/**
 * @file
 * Bitpacked
 */

#include "avcodec.h"
#include "internal.h"
#include "get_bits.h"
#include "libavutil/ancillary_data.h"
#include "libavutil/imgutils.h"

struct BitpackedContext {
    int (*decode)(AVCodecContext *avctx, AVFrame *frame, AVPacket *pkt, uint8_t field);
    AVFrame *cur_interlaced_frame;
    int prev_top_field;
};

/* For this format, it's a simple passthrough */
static int bitpacked_decode_uyvy422(AVCodecContext *avctx, AVFrame *frame,
                                    AVPacket *avpkt, uint8_t field)
{
    int ret;

    /* there is no need to copy as the data already match
     * a known pixel format */
    frame->buf[0] = av_buffer_ref(avpkt->buf);
    ret = av_image_fill_arrays(frame->data, frame->linesize, avpkt->data,
                               avctx->pix_fmt, avctx->width, avctx->height, 1);
    if (ret < 0) {
        av_buffer_unref(&frame->buf[0]);
        return ret;
    }

    return 0;
}

static int bitpacked_decode_yuv422p10(AVCodecContext *avctx, AVFrame *frame,
                                      AVPacket *avpkt, uint8_t field)
{
    uint64_t frame_size = avctx->width * avctx->height * 20LL;
    uint64_t packet_size = (uint64_t)avpkt->size * 8;
    int interlaced = frame->interlaced_frame;
    int top_field = (field & AV_ANCILLARY_DATA_FIELD_TOP_FIELD) ? 1 : 0;
    GetBitContext bc;
    uint16_t *y, *u, *v;
    int ret, i, j;

    if ((avctx->width % 2 ) || (avctx->height % 2))
        return AVERROR_PATCHWELCOME;

    /* check packet size depending on the interlaced/progressive format */
    if (interlaced) {
        if ((frame_size / 2) > packet_size)
            return AVERROR_INVALIDDATA;
    } else if (frame_size > packet_size) {
        return AVERROR_INVALIDDATA;
    }

    /*
     * if the frame is interlaced, the avpkt we are getting is either the top
     * or the bottom field. If it's the bottom field, it contains all the odd
     * lines of the recomposed frame, so we start at offset 1.
     */
    i = (interlaced && !top_field) ? 1 : 0;

    ret = init_get_bits(&bc, avpkt->data, frame_size);
    if (ret)
        return ret;

    /*
     * Packets from interlaced frames contain either even lines, or odd
     * lines, so increment by two in that case.
     */
    for (; i < avctx->height; i += 1 + interlaced) {
        y = (uint16_t*)(frame->data[0] + i * frame->linesize[0]);
        u = (uint16_t*)(frame->data[1] + i * frame->linesize[1]);
        v = (uint16_t*)(frame->data[2] + i * frame->linesize[2]);

        for (j = 0; j < avctx->width; j += 2) {
            *u++ = get_bits(&bc, 10);
            *y++ = get_bits(&bc, 10);
            *v++ = get_bits(&bc, 10);
            *y++ = get_bits(&bc, 10);
        }
    }

    return 0;
}

static av_cold int bitpacked_init_decoder(AVCodecContext *avctx)
{
    struct BitpackedContext *bc = avctx->priv_data;

    if (!avctx->codec_tag || !avctx->width || !avctx->height)
        return AVERROR_INVALIDDATA;

    if (avctx->codec_tag == MKTAG('U', 'Y', 'V', 'Y')) {
        if (avctx->bits_per_coded_sample == 16 &&
            avctx->pix_fmt == AV_PIX_FMT_UYVY422) {

            if (avctx->field_order > AV_FIELD_PROGRESSIVE) {
                av_log(avctx, AV_LOG_ERROR, "interlaced not yet supported for 8-bit\n");
                return AVERROR_PATCHWELCOME;
            }

            bc->decode = bitpacked_decode_uyvy422;
        } else if (avctx->bits_per_coded_sample == 20 &&
                 avctx->pix_fmt == AV_PIX_FMT_YUV422P10) {
            bc->decode = bitpacked_decode_yuv422p10;
        } else {
            return AVERROR_INVALIDDATA;
        }
    } else {
        return AVERROR_INVALIDDATA;
    }

    bc->cur_interlaced_frame = av_frame_alloc();

    return 0;
}

static av_cold int bitpacked_end_decoder(AVCodecContext *avctx)
{
    struct BitpackedContext *bc = avctx->priv_data;

    av_frame_free(&bc->cur_interlaced_frame);

    return 0;
}

static int bitpacked_decode(AVCodecContext *avctx, void *data, int *got_frame,
                            AVPacket *avpkt)
{
    struct BitpackedContext *bc = avctx->priv_data;
    AVAncillaryData *ancillary;
    int buf_size = avpkt->size;
    AVFrame *frame = data;
    int res, size;
    uint8_t *side_data;
    uint8_t field = AV_ANCILLARY_DATA_FIELD_NONE;

    frame->pict_type = AV_PICTURE_TYPE_I;
    frame->key_frame = 1;

    side_data = av_packet_get_side_data(avpkt, AV_PKT_DATA_ANCILLARY, &size);
    if (side_data) {
          ancillary = (AVAncillaryData*)(side_data);
          field = ancillary->field;
    }

    if ((field & AV_ANCILLARY_DATA_FIELD_TOP_FIELD) &&
        (field & AV_ANCILLARY_DATA_FIELD_BOTTOM_FIELD)) {
        av_log(avctx, AV_LOG_WARNING, "Invalid field flags.\n");
        return AVERROR_INVALIDDATA;
    } else if (field & AV_ANCILLARY_DATA_FIELD_TOP_FIELD) {
        frame->interlaced_frame = 1;
        frame->top_field_first = 1;

        if((res = ff_get_buffer(avctx, frame, 0)) < 0)
            return res;

        /* always decode the top (1st) field and ref the result frame
         * but don't output anything */
        if ((res = bc->decode(avctx, frame, avpkt, field)) < 0)
            return res;

        av_frame_unref(bc->cur_interlaced_frame);
        if ((res = av_frame_ref(bc->cur_interlaced_frame, frame)) < 0)
            return res;

        bc->prev_top_field = 1;

        return 0;
    } else if (field & AV_ANCILLARY_DATA_FIELD_BOTTOM_FIELD) {
        if (!bc->prev_top_field) {
            av_log(avctx, AV_LOG_ERROR, "Top field missing.\n");
            return AVERROR_INVALIDDATA;
        }

        frame->interlaced_frame = 1;
        frame->top_field_first = 1;

        /* complete the ref'd frame with bottom field and output the
         * result */
        if ((res = bc->decode(avctx, bc->cur_interlaced_frame, avpkt, field)) < 0)
            return res;

        if ((res = av_frame_ref(frame, bc->cur_interlaced_frame)) < 0)
            return res;

        bc->prev_top_field = 0;
        *got_frame = 1;
        return buf_size;
    } else {
        /* No field: the frame is progressive. */
        if (bc->prev_top_field)
            av_frame_unref(bc->cur_interlaced_frame);

        if((res = ff_get_buffer(avctx, frame, 0)) < 0)
            return res;

        if ((res = bc->decode(avctx, frame, avpkt, field)) < 0)
            return res;

        *got_frame = 1;
        return buf_size;
    }
}

AVCodec ff_bitpacked_decoder = {
    .name   = "bitpacked",
    .long_name = NULL_IF_CONFIG_SMALL("Bitpacked"),
    .type = AVMEDIA_TYPE_VIDEO,
    .id = AV_CODEC_ID_BITPACKED,
    .priv_data_size        = sizeof(struct BitpackedContext),
    .init = bitpacked_init_decoder,
    .close = bitpacked_end_decoder,
    .decode = bitpacked_decode,
    .capabilities = AV_CODEC_CAP_EXPERIMENTAL,
};
