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

#include "avcodec.h"
#include "davs2.h"
#include "internal.h"

typedef struct DAVS2Context {
    void *decoder;

    AVFrame *frame;
    davs2_param_t    param;      // decoding parameters
    davs2_packet_t   packet;     // input bitstream

    davs2_picture_t  out_frame;  // output data, frame data
    davs2_seq_info_t headerset;  // output data, sequence header

}DAVS2Context;

static av_cold int davs2_init(AVCodecContext *avctx)
{
    DAVS2Context *cad = avctx->priv_data;
    int cpu_flags = av_get_cpu_flags();

    /* init the decoder */
    cad->param.threads      = avctx->thread_count;
    cad->param.info_level   = av_log_get_level() > AV_LOG_INFO
                                                 ? DAVS2_LOG_DEBUG
                                                 : DAVS2_LOG_WARNING;
    cad->param.disable_avx  = !(cpu_flags & AV_CPU_FLAG_AVX &&
                                cpu_flags & AV_CPU_FLAG_AVX2);
    cad->decoder            = davs2_decoder_open(&cad->param);

    if (!cad->decoder) {
        av_log(avctx, AV_LOG_ERROR, "decoder created error.");
        return AVERROR_EXTERNAL;
    }

    av_log(avctx, AV_LOG_VERBOSE, "decoder created. %p\n", cad->decoder);
    return 0;
}

static void davs2_frame_unref(void *opaque, uint8_t *data) {
    DAVS2Context    *cad = (DAVS2Context *)opaque;
    davs2_picture_t  pic;

    pic.magic = (davs2_picture_t *)data;

    if (cad->decoder) {
        davs2_decoder_frame_unref(cad->decoder, &pic);
    } else {
        av_log(NULL, AV_LOG_WARNING, "Decoder not found, frame unreference failed.\n");
    }
}

static int davs2_dump_frames(AVCodecContext *avctx, davs2_picture_t *pic, int *got_frame,
                             davs2_seq_info_t *headerset, int ret_type, AVFrame *frame)
{
    DAVS2Context *cad    = avctx->priv_data;
    int plane;

    if (!headerset) {
        *got_frame = 0;
        return 0;
    }

    if (!pic || ret_type == DAVS2_GOT_HEADER) {
        //avctx->width     = headerset->width;
        //avctx->height    = headerset->height;
        avctx->pix_fmt   = headerset->output_bit_depth == 10 ? AV_PIX_FMT_YUV420P10 : AV_PIX_FMT_YUV420P;
	avctx->framerate = av_d2q(headerset->frame_rate,4096);

        //avctx->pix_fmt = headerset->internal_bit_depth == 8 ? AV_PIX_FMT_YUV420P : AV_PIX_FMT_YUV420P10LE;
        ff_set_dimensions(avctx, headerset->width, headerset->height);

        *got_frame = 0;
        return 0;
    }

/*
    if (!pic || ret_type == DAVS2_GOT_HEADER) {
        avctx->width     = headerset->width;
        avctx->height    = headerset->height;
        avctx->pix_fmt   = headerset->output_bit_depth == 10 ?
                           AV_PIX_FMT_YUV420P10 : AV_PIX_FMT_YUV420P;

        avctx->framerate = av_d2q(headerset->frame_rate,4096);
        *got_frame = 0;
        return 0;
    }
*/

    switch (pic->type) {
    case DAVS2_PIC_I:
    case DAVS2_PIC_G:
        frame->pict_type = AV_PICTURE_TYPE_I;
        break;
    case DAVS2_PIC_P:
    case DAVS2_PIC_S:
        frame->pict_type = AV_PICTURE_TYPE_P;
        break;
    case DAVS2_PIC_B:
        frame->pict_type = AV_PICTURE_TYPE_B;
        break;
    case DAVS2_PIC_F:
        frame->pict_type = AV_PICTURE_TYPE_S;
        break;
    default:
        av_log(avctx, AV_LOG_ERROR, "Decoder error: unknown frame type\n");
        return AVERROR_EXTERNAL;
    }

    frame->width     = cad->headerset.width;
    frame->height    = cad->headerset.height;
    frame->pts       = cad->out_frame.pts;
    frame->format    = avctx->pix_fmt;

    /* handle the actual picture in magic */
    frame->buf[0]    = av_buffer_create((uint8_t *)pic->magic,
                                        sizeof(davs2_picture_t *),
                                        davs2_frame_unref,
                                        (void *)cad,
                                        AV_BUFFER_FLAG_READONLY);
    if (!frame->buf[0]) {
        av_log(avctx, AV_LOG_ERROR,
            "Decoder error: allocation failure, can't dump frames.\n");
        return AVERROR(ENOMEM);
    }

    for (plane = 0; plane < 3; ++plane) {
        frame->linesize[plane] = pic->strides[plane];
        frame->data[plane] = pic->planes[plane];
    }

    *got_frame = 1;
    return 0;
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

static void davs2_flush(AVCodecContext *avctx)
{
    DAVS2Context *cad      = avctx->priv_data;
    int           ret      = DAVS2_GOT_FRAME;

    while (ret == DAVS2_GOT_FRAME) {
        ret = davs2_decoder_flush(cad->decoder, &cad->headerset, &cad->out_frame);
        davs2_decoder_frame_unref(cad->decoder, &cad->out_frame);
    }

    if (ret == DAVS2_ERROR) {
        av_log(avctx, AV_LOG_WARNING, "Decoder flushing failed.\n");
    }
}

static int send_delayed_frame(AVCodecContext *avctx, AVFrame *frame, int *got_frame)
{
    DAVS2Context *cad      = avctx->priv_data;
    int           ret      = DAVS2_DEFAULT;

    ret = davs2_decoder_flush(cad->decoder, &cad->headerset, &cad->out_frame);
    if (ret == DAVS2_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Decoder error: can't flush delayed frame\n");
        return AVERROR_EXTERNAL;
    }
    if (ret == DAVS2_GOT_FRAME) {
        ret = davs2_dump_frames(avctx, &cad->out_frame, got_frame, &cad->headerset, ret, frame);
    }
    return ret;
}

static int davs2_decode_frame(AVCodecContext *avctx, void *data,
                              int *got_frame, AVPacket *avpkt)
{
    DAVS2Context *cad      = avctx->priv_data;
    int           buf_size = avpkt->size;
    uint8_t      *buf_ptr  = avpkt->data;
    AVFrame      *frame    = data;
    int           ret      = DAVS2_DEFAULT;

    /* end of stream, output what is still in the buffers */
    if (!buf_size) {
        return send_delayed_frame(avctx, frame, got_frame);
    }

    cad->packet.data = buf_ptr;
    cad->packet.len  = buf_size;
    cad->packet.pts  = avpkt->pts;
    cad->packet.dts  = avpkt->dts;

    ret = davs2_decoder_send_packet(cad->decoder, &cad->packet);


    if (ret == DAVS2_ERROR) {
        av_log(avctx, AV_LOG_ERROR, "Decoder error: can't read packet\n");
        return AVERROR_EXTERNAL;
    }

    ret = davs2_decoder_recv_frame(cad->decoder, &cad->headerset, &cad->out_frame);

    if (ret != DAVS2_DEFAULT) {
        ret = davs2_dump_frames(avctx, &cad->out_frame, got_frame, &cad->headerset, ret, frame);
    }

    return ret == 0 ? buf_size : ret;
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
    .flush          = davs2_flush,
    .capabilities   = AV_CODEC_CAP_DELAY | AV_CODEC_CAP_AUTO_THREADS,
    .pix_fmts       = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV420P10,
                                                     AV_PIX_FMT_NONE },
    .wrapper_name   = "libdavs2",
};
