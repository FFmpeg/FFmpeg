/*
 * Android MediaCodec H.264 decoder
 *
 * Copyright (c) 2015-2016 Matthieu Bouron <matthieu.bouron stupeflix.com>
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

#include <stdint.h>
#include <string.h>

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/pixfmt.h"
#include "libavutil/atomic.h"

#include "avcodec.h"
#include "internal.h"
#include "mediacodecdec.h"
#include "mediacodec_wrapper.h"

#define CODEC_MIME "video/avc"

typedef struct MediaCodecH264DecContext {

    MediaCodecDecContext ctx;

    AVBSFContext *bsf;

    AVFifoBuffer *fifo;

    AVPacket filtered_pkt;

} MediaCodecH264DecContext;

static int h264_extradata_to_annexb_sps_pps(AVCodecContext *avctx,
        uint8_t **extradata_annexb, int *extradata_annexb_size,
        int *sps_offset, int *sps_size,
        int *pps_offset, int *pps_size)
{
    uint16_t unit_size;
    uint64_t total_size = 0;

    uint8_t i, j, unit_nb;
    uint8_t sps_seen = 0;
    uint8_t pps_seen = 0;

    const uint8_t *extradata;
    static const uint8_t nalu_header[4] = { 0x00, 0x00, 0x00, 0x01 };

    if (avctx->extradata_size < 8) {
        av_log(avctx, AV_LOG_ERROR,
            "Too small extradata size, corrupted stream or invalid MP4/AVCC bitstream\n");
        return AVERROR(EINVAL);
    }

    *extradata_annexb = NULL;
    *extradata_annexb_size = 0;

    *sps_offset = *sps_size = 0;
    *pps_offset = *pps_size = 0;

    extradata = avctx->extradata + 4;

    /* skip length size */
    extradata++;

    for (j = 0; j < 2; j ++) {

        if (j == 0) {
            /* number of sps unit(s) */
            unit_nb = *extradata++ & 0x1f;
        } else {
            /* number of pps unit(s) */
            unit_nb = *extradata++;
        }

        for (i = 0; i < unit_nb; i++) {
            int err;

            unit_size   = AV_RB16(extradata);
            total_size += unit_size + 4;

            if (total_size > INT_MAX) {
                av_log(avctx, AV_LOG_ERROR,
                    "Too big extradata size, corrupted stream or invalid MP4/AVCC bitstream\n");
                av_freep(extradata_annexb);
                return AVERROR(EINVAL);
            }

            if (extradata + 2 + unit_size > avctx->extradata + avctx->extradata_size) {
                av_log(avctx, AV_LOG_ERROR, "Packet header is not contained in global extradata, "
                    "corrupted stream or invalid MP4/AVCC bitstream\n");
                av_freep(extradata_annexb);
                return AVERROR(EINVAL);
            }

            if ((err = av_reallocp(extradata_annexb, total_size)) < 0) {
                return err;
            }

            memcpy(*extradata_annexb + total_size - unit_size - 4, nalu_header, 4);
            memcpy(*extradata_annexb + total_size - unit_size, extradata + 2, unit_size);
            extradata += 2 + unit_size;
        }

        if (unit_nb) {
            if (j == 0) {
                sps_seen = 1;
                *sps_size = total_size;
            } else {
                pps_seen = 1;
                *pps_size = total_size - *sps_size;
                *pps_offset = *sps_size;
            }
        }
    }

    *extradata_annexb_size = total_size;

    if (!sps_seen)
        av_log(avctx, AV_LOG_WARNING,
               "Warning: SPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    if (!pps_seen)
        av_log(avctx, AV_LOG_WARNING,
               "Warning: PPS NALU missing or invalid. "
               "The resulting stream may not play.\n");

    return 0;
}

static av_cold int mediacodec_decode_close(AVCodecContext *avctx)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    ff_mediacodec_dec_close(avctx, &s->ctx);

    av_fifo_free(s->fifo);

    av_bsf_free(&s->bsf);
    av_packet_unref(&s->filtered_pkt);

    return 0;
}

static av_cold int mediacodec_decode_init(AVCodecContext *avctx)
{
    int ret;
    FFAMediaFormat *format = NULL;
    MediaCodecH264DecContext *s = avctx->priv_data;

    format = ff_AMediaFormat_new();
    if (!format) {
        av_log(avctx, AV_LOG_ERROR, "Failed to create media format\n");
        ret = AVERROR_EXTERNAL;
        goto done;
    }

    ff_AMediaFormat_setString(format, "mime", CODEC_MIME);
    ff_AMediaFormat_setInt32(format, "width", avctx->width);
    ff_AMediaFormat_setInt32(format, "height", avctx->height);

    if (avctx->extradata[0] == 1) {
        uint8_t *extradata = NULL;
        int extradata_size = 0;

        int sps_offset, sps_size;
        int pps_offset, pps_size;

        if ((ret = h264_extradata_to_annexb_sps_pps(avctx, &extradata, &extradata_size,
                &sps_offset, &sps_size, &pps_offset, &pps_size)) < 0) {
            goto done;
        }

        ff_AMediaFormat_setBuffer(format, "csd-0", extradata + sps_offset, sps_size);
        ff_AMediaFormat_setBuffer(format, "csd-1", extradata + pps_offset, pps_size);

        av_freep(&extradata);
    } else {
        ff_AMediaFormat_setBuffer(format, "csd-0", avctx->extradata, avctx->extradata_size);
    }

    if ((ret = ff_mediacodec_dec_init(avctx, &s->ctx, CODEC_MIME, format)) < 0) {
        goto done;
    }

    av_log(avctx, AV_LOG_INFO, "MediaCodec started successfully, ret = %d\n", ret);

    s->fifo = av_fifo_alloc(sizeof(AVPacket));
    if (!s->fifo) {
        ret = AVERROR(ENOMEM);
        goto done;
    }

    const AVBitStreamFilter *bsf = av_bsf_get_by_name("h264_mp4toannexb");
    if(!bsf) {
        ret = AVERROR_BSF_NOT_FOUND;
        goto done;
    }

    if ((ret = av_bsf_alloc(bsf, &s->bsf))) {
        goto done;
    }

    if (((ret = avcodec_parameters_from_context(s->bsf->par_in, avctx)) < 0) ||
        ((ret = av_bsf_init(s->bsf)) < 0)) {
          goto done;
    }

    av_init_packet(&s->filtered_pkt);

done:
    if (format) {
        ff_AMediaFormat_delete(format);
    }

    if (ret < 0) {
        mediacodec_decode_close(avctx);
    }
    return ret;
}


static int mediacodec_process_data(AVCodecContext *avctx, AVFrame *frame,
                                   int *got_frame, AVPacket *pkt)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    return ff_mediacodec_dec_decode(avctx, &s->ctx, frame, got_frame, pkt);
}

static int mediacodec_decode_frame(AVCodecContext *avctx, void *data,
                                   int *got_frame, AVPacket *avpkt)
{
    MediaCodecH264DecContext *s = avctx->priv_data;
    AVFrame *frame    = data;
    int ret;

    /* buffer the input packet */
    if (avpkt->size) {
        AVPacket input_pkt = { 0 };

        if (av_fifo_space(s->fifo) < sizeof(input_pkt)) {
            ret = av_fifo_realloc2(s->fifo,
                                   av_fifo_size(s->fifo) + sizeof(input_pkt));
            if (ret < 0)
                return ret;
        }

        ret = av_packet_ref(&input_pkt, avpkt);
        if (ret < 0)
            return ret;
        av_fifo_generic_write(s->fifo, &input_pkt, sizeof(input_pkt), NULL);
    }

    /* process buffered data */
    while (!*got_frame) {
        /* prepare the input data -- convert to Annex B if needed */
        if (s->filtered_pkt.size <= 0) {
            AVPacket input_pkt = { 0 };

            av_packet_unref(&s->filtered_pkt);

            /* no more data */
            if (av_fifo_size(s->fifo) < sizeof(AVPacket)) {
                return avpkt->size ? avpkt->size :
                    ff_mediacodec_dec_decode(avctx, &s->ctx, frame, got_frame, avpkt);
            }

            av_fifo_generic_read(s->fifo, &input_pkt, sizeof(input_pkt), NULL);

            ret = av_bsf_send_packet(s->bsf, &input_pkt);
            if (ret < 0) {
                return ret;
            }

            ret = av_bsf_receive_packet(s->bsf, &s->filtered_pkt);
            if (ret == AVERROR(EAGAIN)) {
                goto done;
            }

            /* h264_mp4toannexb is used here and does not requires flushing */
            av_assert0(ret != AVERROR_EOF);

            if (ret < 0) {
                return ret;
            }
        }

        ret = mediacodec_process_data(avctx, frame, got_frame, &s->filtered_pkt);
        if (ret < 0)
            return ret;

        s->filtered_pkt.size -= ret;
        s->filtered_pkt.data += ret;
    }
done:
    return avpkt->size;
}

static void mediacodec_decode_flush(AVCodecContext *avctx)
{
    MediaCodecH264DecContext *s = avctx->priv_data;

    while (av_fifo_size(s->fifo)) {
        AVPacket pkt;
        av_fifo_generic_read(s->fifo, &pkt, sizeof(pkt), NULL);
        av_packet_unref(&pkt);
    }
    av_fifo_reset(s->fifo);

    av_packet_unref(&s->filtered_pkt);

    ff_mediacodec_dec_flush(avctx, &s->ctx);
}

AVCodec ff_h264_mediacodec_decoder = {
    .name           = "h264_mediacodec",
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 Android MediaCodec decoder"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .priv_data_size = sizeof(MediaCodecH264DecContext),
    .init           = mediacodec_decode_init,
    .decode         = mediacodec_decode_frame,
    .flush          = mediacodec_decode_flush,
    .close          = mediacodec_decode_close,
    .capabilities   = CODEC_CAP_DELAY,
};
