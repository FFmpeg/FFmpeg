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

#include "libavutil/cpu.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "davs2.h"

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
    cad->param.info_level   = 0;
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

static int davs2_dump_frames(AVCodecContext *avctx, davs2_picture_t *pic, int *got_frame,
                             davs2_seq_info_t *headerset, int ret_type, AVFrame *frame)
{
    DAVS2Context *cad    = avctx->priv_data;
    int bytes_per_sample = pic->bytes_per_sample;
    int plane = 0;
    int line  = 0;

    if (!headerset) {
        *got_frame = 0;
        return 0;
    }

    if (!pic || ret_type == DAVS2_GOT_HEADER) {
        avctx->width     = headerset->width;
        avctx->height    = headerset->height;
        avctx->pix_fmt   = headerset->output_bit_depth == 10 ?
                           AV_PIX_FMT_YUV420P10 : AV_PIX_FMT_YUV420P;

        avctx->framerate = av_d2q(headerset->frame_rate,4096);
        *got_frame = 0;
        return 0;
    }

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

    for (plane = 0; plane < 3; ++plane) {
        int size_line = pic->widths[plane] * bytes_per_sample;
        frame->buf[plane]  = av_buffer_alloc(size_line * pic->lines[plane]);

        if (!frame->buf[plane]){
            av_log(avctx, AV_LOG_ERROR, "Decoder error: allocation failure, can't dump frames.\n");
            return AVERROR(ENOMEM);
        }

        frame->data[plane]     = frame->buf[plane]->data;
        frame->linesize[plane] = size_line;

        for (line = 0; line < pic->lines[plane]; ++line)
            memcpy(frame->data[plane] + line * size_line,
                   pic->planes[plane] + line * pic->strides[plane],
                   pic->widths[plane] * bytes_per_sample);
    }

    frame->width     = cad->headerset.width;
    frame->height    = cad->headerset.height;
    frame->pts       = cad->out_frame.pts;
    frame->format    = avctx->pix_fmt;

    *got_frame = 1;
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
        davs2_decoder_frame_unref(cad->decoder, &cad->out_frame);
    }
    return ret;
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

static int davs2_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                              int *got_frame, AVPacket *avpkt)
{
    DAVS2Context *cad      = avctx->priv_data;
    int           buf_size = avpkt->size;
    uint8_t      *buf_ptr  = avpkt->data;
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
        davs2_decoder_frame_unref(cad->decoder, &cad->out_frame);
    }

    return ret == 0 ? buf_size : ret;
}

const FFCodec ff_libdavs2_decoder = {
    .p.name         = "libdavs2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("libdavs2 AVS2-P2/IEEE1857.4"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_AVS2,
    .priv_data_size = sizeof(DAVS2Context),
    .init           = davs2_init,
    .close          = davs2_end,
    FF_CODEC_DECODE_CB(davs2_decode_frame),
    .flush          = davs2_flush,
    .p.capabilities =  AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal  = FF_CODEC_CAP_AUTO_THREADS,
    .p.pix_fmts     = (const enum AVPixelFormat[]) { AV_PIX_FMT_YUV420P,
                                                     AV_PIX_FMT_NONE },
    .p.wrapper_name = "libdavs2",
};
