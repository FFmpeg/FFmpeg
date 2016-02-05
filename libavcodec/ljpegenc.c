/*
 * lossless JPEG encoder
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2003 Alex Beregszaszi
 * Copyright (c) 2003-2004 Michael Niedermayer
 *
 * Support for external huffman table, various fixes (AVID workaround),
 * aspecting, new decode_frame mechanism and apple mjpeg-b support
 *                                  by Alex Beregszaszi
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

/**
 * @file
 * lossless JPEG encoder.
 */

#include "libavutil/frame.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"

#include "avcodec.h"
#include "idctdsp.h"
#include "internal.h"
#include "jpegtables.h"
#include "mjpegenc_common.h"
#include "mjpeg.h"
#include "mjpegenc.h"

typedef struct LJpegEncContext {
    AVClass *class;
    IDCTDSPContext idsp;
    ScanTable scantable;
    uint16_t matrix[64];

    int vsample[4];
    int hsample[4];

    uint16_t huff_code_dc_luminance[12];
    uint16_t huff_code_dc_chrominance[12];
    uint8_t  huff_size_dc_luminance[12];
    uint8_t  huff_size_dc_chrominance[12];

    uint16_t (*scratch)[4];
    int pred;
} LJpegEncContext;

static int ljpeg_encode_bgr(AVCodecContext *avctx, PutBitContext *pb,
                            const AVFrame *frame)
{
    LJpegEncContext *s    = avctx->priv_data;
    const int width       = frame->width;
    const int height      = frame->height;
    const int linesize    = frame->linesize[0];
    uint16_t (*buffer)[4] = s->scratch;
    int left[4], top[4], topleft[4];
    int x, y, i;

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->prediction_method)
        s->pred = avctx->prediction_method + 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    for (i = 0; i < 4; i++)
        buffer[0][i] = 1 << (9 - 1);

    for (y = 0; y < height; y++) {
        const int modified_predictor = y ? s->pred : 1;
        uint8_t *ptr = frame->data[0] + (linesize * y);

        if (pb->buf_end - pb->buf - (put_bits_count(pb) >> 3) < width * 4 * 4) {
            av_log(avctx, AV_LOG_ERROR, "encoded frame too large\n");
            return -1;
        }

        for (i = 0; i < 4; i++)
            top[i]= left[i]= topleft[i]= buffer[0][i];

        for (x = 0; x < width; x++) {
            if(avctx->pix_fmt == AV_PIX_FMT_BGR24){
                buffer[x][1] =  ptr[3 * x + 0] -     ptr[3 * x + 1] + 0x100;
                buffer[x][2] =  ptr[3 * x + 2] -     ptr[3 * x + 1] + 0x100;
                buffer[x][0] = (ptr[3 * x + 0] + 2 * ptr[3 * x + 1] + ptr[3 * x + 2]) >> 2;
            }else{
                buffer[x][1] =  ptr[4 * x + 0] -     ptr[4 * x + 1] + 0x100;
                buffer[x][2] =  ptr[4 * x + 2] -     ptr[4 * x + 1] + 0x100;
                buffer[x][0] = (ptr[4 * x + 0] + 2 * ptr[4 * x + 1] + ptr[4 * x + 2]) >> 2;
                if (avctx->pix_fmt == AV_PIX_FMT_BGRA)
                    buffer[x][3] =  ptr[4 * x + 3];
            }

            for (i = 0; i < 3 + (avctx->pix_fmt == AV_PIX_FMT_BGRA); i++) {
                int pred, diff;

                PREDICT(pred, topleft[i], top[i], left[i], modified_predictor);

                topleft[i] = top[i];
                top[i]     = buffer[x+1][i];

                left[i]    = buffer[x][i];

                diff       = ((left[i] - pred + 0x100) & 0x1FF) - 0x100;

                if (i == 0 || i == 3)
                    ff_mjpeg_encode_dc(pb, diff, s->huff_size_dc_luminance, s->huff_code_dc_luminance); //FIXME ugly
                else
                    ff_mjpeg_encode_dc(pb, diff, s->huff_size_dc_chrominance, s->huff_code_dc_chrominance);
            }
        }
    }

    return 0;
}

static inline void ljpeg_encode_yuv_mb(LJpegEncContext *s, PutBitContext *pb,
                                       const AVFrame *frame, int predictor,
                                       int mb_x, int mb_y)
{
    int i;

    if (mb_x == 0 || mb_y == 0) {
        for (i = 0; i < 3; i++) {
            uint8_t *ptr;
            int x, y, h, v, linesize;
            h = s->hsample[i];
            v = s->vsample[i];
            linesize = frame->linesize[i];

            for (y = 0; y < v; y++) {
                for (x = 0; x < h; x++) {
                    int pred;

                    ptr = frame->data[i] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
                    if (y == 0 && mb_y == 0) {
                        if (x == 0 && mb_x == 0)
                            pred = 128;
                        else
                            pred = ptr[-1];
                    } else {
                        if (x == 0 && mb_x == 0) {
                            pred = ptr[-linesize];
                        } else {
                            PREDICT(pred, ptr[-linesize - 1], ptr[-linesize],
                                    ptr[-1], predictor);
                        }
                    }

                    if (i == 0)
                        ff_mjpeg_encode_dc(pb, *ptr - pred, s->huff_size_dc_luminance, s->huff_code_dc_luminance); //FIXME ugly
                    else
                        ff_mjpeg_encode_dc(pb, *ptr - pred, s->huff_size_dc_chrominance, s->huff_code_dc_chrominance);
                }
            }
        }
    } else {
        for (i = 0; i < 3; i++) {
            uint8_t *ptr;
            int x, y, h, v, linesize;
            h = s->hsample[i];
            v = s->vsample[i];
            linesize = frame->linesize[i];

            for (y = 0; y < v; y++) {
                for (x = 0; x < h; x++) {
                    int pred;

                    ptr = frame->data[i] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
                    PREDICT(pred, ptr[-linesize - 1], ptr[-linesize], ptr[-1], predictor);

                    if (i == 0)
                        ff_mjpeg_encode_dc(pb, *ptr - pred, s->huff_size_dc_luminance, s->huff_code_dc_luminance); //FIXME ugly
                    else
                        ff_mjpeg_encode_dc(pb, *ptr - pred, s->huff_size_dc_chrominance, s->huff_code_dc_chrominance);
                }
            }
        }
    }
}

static int ljpeg_encode_yuv(AVCodecContext *avctx, PutBitContext *pb,
                            const AVFrame *frame)
{
    LJpegEncContext *s  = avctx->priv_data;
    const int mb_width  = (avctx->width  + s->hsample[0] - 1) / s->hsample[0];
    const int mb_height = (avctx->height + s->vsample[0] - 1) / s->vsample[0];
    int mb_x, mb_y;

#if FF_API_PRIVATE_OPT
FF_DISABLE_DEPRECATION_WARNINGS
    if (avctx->prediction_method)
        s->pred = avctx->prediction_method + 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    for (mb_y = 0; mb_y < mb_height; mb_y++) {
        if (pb->buf_end - pb->buf - (put_bits_count(pb) >> 3) <
            mb_width * 4 * 3 * s->hsample[0] * s->vsample[0]) {
            av_log(avctx, AV_LOG_ERROR, "encoded frame too large\n");
            return -1;
        }

        for (mb_x = 0; mb_x < mb_width; mb_x++)
            ljpeg_encode_yuv_mb(s, pb, frame, s->pred, mb_x, mb_y);
    }

    return 0;
}

static int ljpeg_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                              const AVFrame *pict, int *got_packet)
{
    LJpegEncContext *s = avctx->priv_data;
    PutBitContext pb;
    const int width  = avctx->width;
    const int height = avctx->height;
    const int mb_width  = (width  + s->hsample[0] - 1) / s->hsample[0];
    const int mb_height = (height + s->vsample[0] - 1) / s->vsample[0];
    int max_pkt_size = AV_INPUT_BUFFER_MIN_SIZE;
    int ret, header_bits;

    if(    avctx->pix_fmt == AV_PIX_FMT_BGR0
        || avctx->pix_fmt == AV_PIX_FMT_BGR24)
        max_pkt_size += width * height * 3 * 4;
    else if(avctx->pix_fmt == AV_PIX_FMT_BGRA)
        max_pkt_size += width * height * 4 * 4;
    else {
        max_pkt_size += mb_width * mb_height * 3 * 4
                        * s->hsample[0] * s->vsample[0];
    }

    if ((ret = ff_alloc_packet2(avctx, pkt, max_pkt_size, 0)) < 0)
        return ret;

    init_put_bits(&pb, pkt->data, pkt->size);

    ff_mjpeg_encode_picture_header(avctx, &pb, &s->scantable,
                                   s->pred, s->matrix, s->matrix);

    header_bits = put_bits_count(&pb);

    if(    avctx->pix_fmt == AV_PIX_FMT_BGR0
        || avctx->pix_fmt == AV_PIX_FMT_BGRA
        || avctx->pix_fmt == AV_PIX_FMT_BGR24)
        ret = ljpeg_encode_bgr(avctx, &pb, pict);
    else
        ret = ljpeg_encode_yuv(avctx, &pb, pict);
    if (ret < 0)
        return ret;

    emms_c();

    ff_mjpeg_escape_FF(&pb, header_bits >> 3);
    ff_mjpeg_encode_picture_trailer(&pb, header_bits);

    flush_put_bits(&pb);
    pkt->size   = put_bits_ptr(&pb) - pb.buf;
    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;
}

static av_cold int ljpeg_encode_close(AVCodecContext *avctx)
{
    LJpegEncContext *s = avctx->priv_data;

    av_freep(&s->scratch);

    return 0;
}

static av_cold int ljpeg_encode_init(AVCodecContext *avctx)
{
    LJpegEncContext *s = avctx->priv_data;

    if ((avctx->pix_fmt == AV_PIX_FMT_YUV420P ||
         avctx->pix_fmt == AV_PIX_FMT_YUV422P ||
         avctx->pix_fmt == AV_PIX_FMT_YUV444P ||
         avctx->color_range == AVCOL_RANGE_MPEG) &&
        avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL) {
        av_log(avctx, AV_LOG_ERROR,
               "Limited range YUV is non-standard, set strict_std_compliance to "
               "at least unofficial to use it.\n");
        return AVERROR(EINVAL);
    }

#if FF_API_CODED_FRAME
FF_DISABLE_DEPRECATION_WARNINGS
    avctx->coded_frame->pict_type = AV_PICTURE_TYPE_I;
    avctx->coded_frame->key_frame = 1;
FF_ENABLE_DEPRECATION_WARNINGS
#endif

    s->scratch = av_malloc_array(avctx->width + 1, sizeof(*s->scratch));
    if (!s->scratch)
        goto fail;

    ff_idctdsp_init(&s->idsp, avctx);
    ff_init_scantable(s->idsp.idct_permutation, &s->scantable,
                      ff_zigzag_direct);

    ff_mjpeg_init_hvsample(avctx, s->hsample, s->vsample);

    ff_mjpeg_build_huffman_codes(s->huff_size_dc_luminance,
                                 s->huff_code_dc_luminance,
                                 avpriv_mjpeg_bits_dc_luminance,
                                 avpriv_mjpeg_val_dc);
    ff_mjpeg_build_huffman_codes(s->huff_size_dc_chrominance,
                                 s->huff_code_dc_chrominance,
                                 avpriv_mjpeg_bits_dc_chrominance,
                                 avpriv_mjpeg_val_dc);

    return 0;
fail:
    ljpeg_encode_close(avctx);
    return AVERROR(ENOMEM);
}

#define OFFSET(x) offsetof(LJpegEncContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
{ "pred", "Prediction method", OFFSET(pred), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 3, VE, "pred" },
    { "left",   NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 1 }, INT_MIN, INT_MAX, VE, "pred" },
    { "plane",  NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 2 }, INT_MIN, INT_MAX, VE, "pred" },
    { "median", NULL, 0, AV_OPT_TYPE_CONST, { .i64 = 3 }, INT_MIN, INT_MAX, VE, "pred" },

    { NULL},
};

static const AVClass ljpeg_class = {
    .class_name = "ljpeg",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_ljpeg_encoder = {
    .name           = "ljpeg",
    .long_name      = NULL_IF_CONFIG_SMALL("Lossless JPEG"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_LJPEG,
    .priv_data_size = sizeof(LJpegEncContext),
    .priv_class     = &ljpeg_class,
    .init           = ljpeg_encode_init,
    .encode2        = ljpeg_encode_frame,
    .close          = ljpeg_encode_close,
    .capabilities   = AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_INTRA_ONLY,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_BGR24   , AV_PIX_FMT_BGRA    , AV_PIX_FMT_BGR0,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUV420P , AV_PIX_FMT_YUV444P , AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_NONE},
};
