/*
 * Intel MediaSDK QSV based H.264 decoder
 *
 * copyright (c) 2013 Luca Barbato
 * copyright (c) 2015 Anton Khirnov
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

#include <mfx/mfxvideo.h>

#include "libavutil/common.h"
#include "libavutil/fifo.h"
#include "libavutil/opt.h"

#include "avcodec.h"
#include "internal.h"
#include "qsvdec.h"

typedef struct QSVH264Context {
    AVClass *class;
    QSVContext qsv;

    // the filter for converting to Annex B
    AVBitStreamFilterContext *bsf;

} QSVH264Context;

static av_cold int qsv_decode_close(AVCodecContext *avctx)
{
    QSVH264Context *s = avctx->priv_data;

    ff_qsv_decode_close(&s->qsv);

    av_bitstream_filter_close(s->bsf);

    return 0;
}

static av_cold int qsv_decode_init(AVCodecContext *avctx)
{
    QSVH264Context *s = avctx->priv_data;
    int ret;

    s->bsf = av_bitstream_filter_init("h264_mp4toannexb");
    if (!s->bsf) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    return 0;
fail:
    qsv_decode_close(avctx);
    return ret;
}

static int qsv_decode_frame(AVCodecContext *avctx, void *data,
                            int *got_frame, AVPacket *avpkt)
{
    QSVH264Context *s = avctx->priv_data;
    AVFrame *frame    = data;
    int ret;
    uint8_t *p_filtered = NULL;
    int      n_filtered = NULL;
    AVPacket pkt_filtered = { 0 };

    if (avpkt->size) {
        if (avpkt->size > 3 && !avpkt->data[0] &&
            !avpkt->data[1] && !avpkt->data[2] && 1==avpkt->data[3]) {
            /* we already have annex-b prefix */
            return ff_qsv_decode(avctx, &s->qsv, frame, got_frame, avpkt);

        } else {
            /* no annex-b prefix. try to restore: */
            ret = av_bitstream_filter_filter(s->bsf, avctx, NULL,
                                         &p_filtered, &n_filtered,
                                         avpkt->data, avpkt->size, 0);
            if (ret>=0) {
                pkt_filtered.pts  = avpkt->pts;
                pkt_filtered.data = p_filtered;
                pkt_filtered.size = n_filtered;

                ret = ff_qsv_decode(avctx, &s->qsv, frame, got_frame, &pkt_filtered);

                if (p_filtered != avpkt->data)
                    av_free(p_filtered);
                return ret > 0 ? avpkt->size : ret;
            }
        }
    }

    return ff_qsv_decode(avctx, &s->qsv, frame, got_frame, avpkt);
}

static void qsv_decode_flush(AVCodecContext *avctx)
{
//    QSVH264Context *s = avctx->priv_data;
    /* TODO: flush qsv engine if necessary */
}

AVHWAccel ff_h264_qsv_hwaccel = {
    .name           = "h264_qsv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .pix_fmt        = AV_PIX_FMT_QSV,
};

#define OFFSET(x) offsetof(QSVH264Context, x)
#define VD AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "async_depth", "Internal parallelization depth, the higher the value the higher the latency.", OFFSET(qsv.async_depth), AV_OPT_TYPE_INT, { .i64 = ASYNC_DEPTH_DEFAULT }, 0, INT_MAX, VD },
    { NULL },
};

static const AVClass class = {
    .class_name = "h264_qsv",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_h264_qsv_decoder = {
    .name           = "h264_qsv",
    .long_name      = NULL_IF_CONFIG_SMALL("H.264 / AVC / MPEG-4 AVC / MPEG-4 part 10 (Intel Quick Sync Video acceleration)"),
    .priv_data_size = sizeof(QSVH264Context),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_H264,
    .init           = qsv_decode_init,
    .decode         = qsv_decode_frame,
    .flush          = qsv_decode_flush,
    .close          = qsv_decode_close,
    .capabilities   = CODEC_CAP_DELAY,
    .priv_class     = &class,
};
