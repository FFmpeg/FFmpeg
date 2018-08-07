/*
 * AVS2 decoding using the davs2 library
 *
 * Copyright (C) 2018 Yiqun Xu, <yiqun.xu@vipl.ict.ac.cn>
 *                    Falei Luo, <falei.luo@gmail.com>
 *                    Huiwen Ren, <hwrenx@gmail.com>
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

#include "libavutil/avassert.h"
#include "libavutil/common.h"
#include "libavutil/avutil.h"
#include "avcodec.h"
#include "libavutil/imgutils.h"
#include "internal.h"

#include "davs2.h"

typedef struct DAVS2Context {
    void *decoder;

    AVFrame *frame;
    davs2_param_t    param;      // decoding parameters
    davs2_packet_t   packet;     // input bitstream

    int decoded_frames;

    davs2_picture_t  out_frame;  // output data, frame data
    davs2_seq_info_t headerset;  // output data, sequence header

}DAVS2Context;

static av_cold int davs2_init(AVCodecContext *avctx)
{
    DAVS2Context *cad = avctx->priv_data;

    /* init the decoder */
    cad->param.threads      = avctx->thread_count;
    cad->param.info_level   = 0;
    cad->decoder            = davs2_decoder_open(&cad->param);

    if (!cad->decoder) {
        av_log(avctx, AV_LOG_ERROR, "decoder created error.");
        return AVERROR(EINVAL);
    }

    av_log(avctx, AV_LOG_VERBOSE, "decoder created. %p\n", cad->decoder);
    return 0;
}

static int davs2_dump_frames(AVCodecContext *avctx, davs2_picture_t *pic,
                             davs2_seq_info_t *headerset, int ret_type, AVFrame *frame)
{
    DAVS2Context *cad    = avctx->priv_data;
    int bytes_per_sample = pic->bytes_per_sample;
    int plane = 0;
    int line  = 0;

    if (!headerset)
        return 0;

    if (!pic || ret_type == DAVS2_GOT_HEADER) {
        avctx->width     = headerset->width;
        avctx->height    = headerset->height;
        avctx->pix_fmt   = headerset->output_bit_depth == 10 ?
                           AV_PIX_FMT_YUV420P10 : AV_PIX_FMT_YUV420P;

        avctx->framerate = av_d2q(headerset->frame_rate,4096);
        return 0;
    }

    for (plane = 0; plane < 3; ++plane) {
        int size_line = pic->widths[plane] * bytes_per_sample;
        frame->buf[plane]  = av_buffer_alloc(size_line * pic->lines[plane]);

        if (!frame->buf[plane]){
            av_log(avctx, AV_LOG_ERROR, "dump error: alloc failed.\n");
            return AVERROR(EINVAL);
        }

        frame->data[plane]     = frame->buf[plane]->data;
        frame->linesize[plane] = pic->widths[plane];

        for (line = 0; line < pic->lines[plane]; ++line)
            memcpy(frame->data[plane] + line * size_line,
                   pic->planes[plane] + line * pic->strides[plane],
                   pic->widths[plane] * bytes_per_sample);
    }

    frame->width     = cad->headerset.width;
    frame->height    = cad->headerset.height;
    frame->pts       = cad->out_frame.pts;
    frame->pict_type = pic->type;
    frame->format    = avctx->pix_fmt;

    cad->decoded_frames++;
    return 1;
}

static av_cold int davs2_end(AVCodecContext *avctx)
{
    DAVS2Context *cad = avctx->priv_data;

    /* close the decoder */
    if (cad->decoder) {
        davs2_decoder_close(cad->decoder);
        cad->decoder = NULL;
    }

    return 0;
}

static int davs2_decode_frame(AVCodecContext *avctx, void *data,
                              int *got_frame, AVPacket *avpkt)
{
    DAVS2Context *cad      = avctx->priv_data;
    int           buf_size = avpkt->size;
    uint8_t      *buf_ptr  = avpkt->data;
    AVFrame      *frame    = data;
    int           ret      = DAVS2_DEFAULT;

    if (!buf_size) {
        return 0;
    }

    cad->packet.data = buf_ptr;
    cad->packet.len  = buf_size;
    cad->packet.pts  = avpkt->pts;
    cad->packet.dts  = avpkt->dts;

    ret = davs2_decoder_send_packet(cad->decoder, &cad->packet);


    if (ret == DAVS2_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Decoder error: can't read packet\n");
        return AVERROR(EINVAL);
    }

    ret = davs2_decoder_recv_frame(cad->decoder, &cad->headerset, &cad->out_frame);

    if (ret != DAVS2_DEFAULT) {
        *got_frame = davs2_dump_frames(avctx, &cad->out_frame, &cad->headerset, ret, frame);
        davs2_decoder_frame_unref(cad->decoder, &cad->out_frame);
    }

    return buf_size;
}

AVCodec ff_libdavs2_decoder = {
    .name           = "libdavs2",
    .long_name      = NULL_IF_CONFIG_SMALL("libdavs2 AVS2-P2/IEEE1857.4"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_AVS2,
    .priv_data_size = sizeof(DAVS2Context),
    .init           = davs2_init,
    .close          = davs2_end,
    .decode         = davs2_decode_frame,
    .capabilities   =  AV_CODEC_CAP_DELAY,//AV_CODEC_CAP_DR1 |
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10,
                                                     AV_PIX_FMT_NONE },
    .wrapper_name   = "libdavs2",
};
