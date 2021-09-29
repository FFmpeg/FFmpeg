/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2014 Arwa Arif <arwaarif1994@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * Ultra Slow/Simple Post-processing filter.
 *
 * Originally written by Michael Niedermayer for the MPlayer project, and
 * ported by Arwa Arif for FFmpeg.
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavcodec/avcodec.h"
#include "internal.h"
#include "qp_table.h"
#include "avfilter.h"

#define MAX_LEVEL 8 /* quality levels */
#define BLOCK 16

typedef struct USPPContext {
    const AVClass *av_class;
    int log2_count;
    int hsub, vsub;
    int qp;
    int qscale_type;
    int temp_stride[3];
    uint8_t *src[3];
    uint16_t *temp[3];
    int outbuf_size;
    uint8_t *outbuf;
    AVCodecContext *avctx_enc[BLOCK*BLOCK];
    AVPacket *pkt;
    AVFrame *frame;
    AVFrame *frame_dec;
    int8_t *non_b_qp_table;
    int non_b_qp_stride;
    int use_bframe_qp;
} USPPContext;

#define OFFSET(x) offsetof(USPPContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption uspp_options[] = {
    { "quality",       "set quality",                          OFFSET(log2_count),    AV_OPT_TYPE_INT, {.i64 = 3}, 0, MAX_LEVEL, FLAGS },
    { "qp",            "force a constant quantizer parameter", OFFSET(qp),            AV_OPT_TYPE_INT, {.i64 = 0}, 0, 63,        FLAGS },
    { "use_bframe_qp", "use B-frames' QP",                     OFFSET(use_bframe_qp), AV_OPT_TYPE_BOOL,{.i64 = 0}, 0, 1,         FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(uspp);

DECLARE_ALIGNED(8, static const uint8_t, dither)[8][8] = {
    {  0*4,  48*4,  12*4,  60*4,   3*4,  51*4,  15*4,  63*4, },
    { 32*4,  16*4,  44*4,  28*4,  35*4,  19*4,  47*4,  31*4, },
    {  8*4,  56*4,   4*4,  52*4,  11*4,  59*4,   7*4,  55*4, },
    { 40*4,  24*4,  36*4,  20*4,  43*4,  27*4,  39*4,  23*4, },
    {  2*4,  50*4,  14*4,  62*4,   1*4,  49*4,  13*4,  61*4, },
    { 34*4,  18*4,  46*4,  30*4,  33*4,  17*4,  45*4,  29*4, },
    { 10*4,  58*4,   6*4,  54*4,   9*4,  57*4,   5*4,  53*4, },
    { 42*4,  26*4,  38*4,  22*4,  41*4,  25*4,  37*4,  21*4, },
};

static const uint8_t offset[511][2] = {
    { 0, 0},
    { 0, 0}, { 8, 8},                                                              // quality 1
    { 0, 0}, { 4, 4}, {12, 8}, { 8,12},                                            // quality 2
    { 0, 0}, {10, 2}, { 4, 4}, {14, 6}, { 8, 8}, { 2,10}, {12,12}, { 6,14},        // quality 3

    { 0, 0}, {10, 2}, { 4, 4}, {14, 6}, { 8, 8}, { 2,10}, {12,12}, { 6,14},
    { 5, 1}, {15, 3}, { 9, 5}, { 3, 7}, {13, 9}, { 7,11}, { 1,13}, {11,15},        // quality 4

    { 0, 0}, { 8, 0}, { 0, 8}, { 8, 8}, { 5, 1}, {13, 1}, { 5, 9}, {13, 9},
    { 2, 2}, {10, 2}, { 2,10}, {10,10}, { 7, 3}, {15, 3}, { 7,11}, {15,11},
    { 4, 4}, {12, 4}, { 4,12}, {12,12}, { 1, 5}, { 9, 5}, { 1,13}, { 9,13},
    { 6, 6}, {14, 6}, { 6,14}, {14,14}, { 3, 7}, {11, 7}, { 3,15}, {11,15},        // quality 5

    { 0, 0}, { 8, 0}, { 0, 8}, { 8, 8}, { 4, 0}, {12, 0}, { 4, 8}, {12, 8},
    { 1, 1}, { 9, 1}, { 1, 9}, { 9, 9}, { 5, 1}, {13, 1}, { 5, 9}, {13, 9},
    { 3, 2}, {11, 2}, { 3,10}, {11,10}, { 7, 2}, {15, 2}, { 7,10}, {15,10},
    { 2, 3}, {10, 3}, { 2,11}, {10,11}, { 6, 3}, {14, 3}, { 6,11}, {14,11},
    { 0, 4}, { 8, 4}, { 0,12}, { 8,12}, { 4, 4}, {12, 4}, { 4,12}, {12,12},
    { 1, 5}, { 9, 5}, { 1,13}, { 9,13}, { 5, 5}, {13, 5}, { 5,13}, {13,13},
    { 3, 6}, {11, 6}, { 3,14}, {11,14}, { 7, 6}, {15, 6}, { 7,14}, {15,14},
    { 2, 7}, {10, 7}, { 2,15}, {10,15}, { 6, 7}, {14, 7}, { 6,15}, {14,15},        // quality 6

    { 0, 0}, { 8, 0}, { 0, 8}, { 8, 8}, { 0, 2}, { 8, 2}, { 0,10}, { 8,10},
    { 0, 4}, { 8, 4}, { 0,12}, { 8,12}, { 0, 6}, { 8, 6}, { 0,14}, { 8,14},
    { 1, 1}, { 9, 1}, { 1, 9}, { 9, 9}, { 1, 3}, { 9, 3}, { 1,11}, { 9,11},
    { 1, 5}, { 9, 5}, { 1,13}, { 9,13}, { 1, 7}, { 9, 7}, { 1,15}, { 9,15},
    { 2, 0}, {10, 0}, { 2, 8}, {10, 8}, { 2, 2}, {10, 2}, { 2,10}, {10,10},
    { 2, 4}, {10, 4}, { 2,12}, {10,12}, { 2, 6}, {10, 6}, { 2,14}, {10,14},
    { 3, 1}, {11, 1}, { 3, 9}, {11, 9}, { 3, 3}, {11, 3}, { 3,11}, {11,11},
    { 3, 5}, {11, 5}, { 3,13}, {11,13}, { 3, 7}, {11, 7}, { 3,15}, {11,15},
    { 4, 0}, {12, 0}, { 4, 8}, {12, 8}, { 4, 2}, {12, 2}, { 4,10}, {12,10},
    { 4, 4}, {12, 4}, { 4,12}, {12,12}, { 4, 6}, {12, 6}, { 4,14}, {12,14},
    { 5, 1}, {13, 1}, { 5, 9}, {13, 9}, { 5, 3}, {13, 3}, { 5,11}, {13,11},
    { 5, 5}, {13, 5}, { 5,13}, {13,13}, { 5, 7}, {13, 7}, { 5,15}, {13,15},
    { 6, 0}, {14, 0}, { 6, 8}, {14, 8}, { 6, 2}, {14, 2}, { 6,10}, {14,10},
    { 6, 4}, {14, 4}, { 6,12}, {14,12}, { 6, 6}, {14, 6}, { 6,14}, {14,14},
    { 7, 1}, {15, 1}, { 7, 9}, {15, 9}, { 7, 3}, {15, 3}, { 7,11}, {15,11},
    { 7, 5}, {15, 5}, { 7,13}, {15,13}, { 7, 7}, {15, 7}, { 7,15}, {15,15},        // quality 7

    { 0, 0}, { 8, 0}, { 0, 8}, { 8, 8}, { 4, 4}, {12, 4}, { 4,12}, {12,12},
    { 0, 4}, { 8, 4}, { 0,12}, { 8,12}, { 4, 0}, {12, 0}, { 4, 8}, {12, 8},
    { 2, 2}, {10, 2}, { 2,10}, {10,10}, { 6, 6}, {14, 6}, { 6,14}, {14,14},
    { 2, 6}, {10, 6}, { 2,14}, {10,14}, { 6, 2}, {14, 2}, { 6,10}, {14,10},
    { 0, 2}, { 8, 2}, { 0,10}, { 8,10}, { 4, 6}, {12, 6}, { 4,14}, {12,14},
    { 0, 6}, { 8, 6}, { 0,14}, { 8,14}, { 4, 2}, {12, 2}, { 4,10}, {12,10},
    { 2, 0}, {10, 0}, { 2, 8}, {10, 8}, { 6, 4}, {14, 4}, { 6,12}, {14,12},
    { 2, 4}, {10, 4}, { 2,12}, {10,12}, { 6, 0}, {14, 0}, { 6, 8}, {14, 8},
    { 1, 1}, { 9, 1}, { 1, 9}, { 9, 9}, { 5, 5}, {13, 5}, { 5,13}, {13,13},
    { 1, 5}, { 9, 5}, { 1,13}, { 9,13}, { 5, 1}, {13, 1}, { 5, 9}, {13, 9},
    { 3, 3}, {11, 3}, { 3,11}, {11,11}, { 7, 7}, {15, 7}, { 7,15}, {15,15},
    { 3, 7}, {11, 7}, { 3,15}, {11,15}, { 7, 3}, {15, 3}, { 7,11}, {15,11},
    { 1, 3}, { 9, 3}, { 1,11}, { 9,11}, { 5, 7}, {13, 7}, { 5,15}, {13,15},
    { 1, 7}, { 9, 7}, { 1,15}, { 9,15}, { 5, 3}, {13, 3}, { 5,11}, {13,11},        // quality 8
    { 3, 1}, {11, 1}, { 3, 9}, {11, 9}, { 7, 5}, {15, 5}, { 7,13}, {15,13},
    { 3, 5}, {11, 5}, { 3,13}, {11,13}, { 7, 1}, {15, 1}, { 7, 9}, {15, 9},
    { 0, 1}, { 8, 1}, { 0, 9}, { 8, 9}, { 4, 5}, {12, 5}, { 4,13}, {12,13},
    { 0, 5}, { 8, 5}, { 0,13}, { 8,13}, { 4, 1}, {12, 1}, { 4, 9}, {12, 9},
    { 2, 3}, {10, 3}, { 2,11}, {10,11}, { 6, 7}, {14, 7}, { 6,15}, {14,15},
    { 2, 7}, {10, 7}, { 2,15}, {10,15}, { 6, 3}, {14, 3}, { 6,11}, {14,11},
    { 0, 3}, { 8, 3}, { 0,11}, { 8,11}, { 4, 7}, {12, 7}, { 4,15}, {12,15},
    { 0, 7}, { 8, 7}, { 0,15}, { 8,15}, { 4, 3}, {12, 3}, { 4,11}, {12,11},
    { 2, 1}, {10, 1}, { 2, 9}, {10, 9}, { 6, 5}, {14, 5}, { 6,13}, {14,13},
    { 2, 5}, {10, 5}, { 2,13}, {10,13}, { 6, 1}, {14, 1}, { 6, 9}, {14, 9},
    { 1, 0}, { 9, 0}, { 1, 8}, { 9, 8}, { 5, 4}, {13, 4}, { 5,12}, {13,12},
    { 1, 4}, { 9, 4}, { 1,12}, { 9,12}, { 5, 0}, {13, 0}, { 5, 8}, {13, 8},
    { 3, 2}, {11, 2}, { 3,10}, {11,10}, { 7, 6}, {15, 6}, { 7,14}, {15,14},
    { 3, 6}, {11, 6}, { 3,14}, {11,14}, { 7, 2}, {15, 2}, { 7,10}, {15,10},
    { 1, 2}, { 9, 2}, { 1,10}, { 9,10}, { 5, 6}, {13, 6}, { 5,14}, {13,14},
    { 1, 6}, { 9, 6}, { 1,14}, { 9,14}, { 5, 2}, {13, 2}, { 5,10}, {13,10},
    { 3, 0}, {11, 0}, { 3, 8}, {11, 8}, { 7, 4}, {15, 4}, { 7,12}, {15,12},
    { 3, 4}, {11, 4}, { 3,12}, {11,12}, { 7, 0}, {15, 0}, { 7, 8}, {15, 8},
};

static void store_slice_c(uint8_t *dst, const uint16_t *src,
                          int dst_stride, int src_stride,
                          int width, int height, int log2_scale)
{
    int y, x;

#define STORE(pos) do {                                                     \
    temp = ((src[x + y * src_stride + pos] << log2_scale) + d[pos]) >> 8;   \
    if (temp & 0x100) temp = ~(temp >> 31);                                 \
    dst[x + y * dst_stride + pos] = temp;                                   \
} while (0)

    for (y = 0; y < height; y++) {
        const uint8_t *d = dither[y&7];
        for (x = 0; x < width; x += 8) {
            int temp;
            STORE(0);
            STORE(1);
            STORE(2);
            STORE(3);
            STORE(4);
            STORE(5);
            STORE(6);
            STORE(7);
        }
    }
}

static void filter(USPPContext *p, uint8_t *dst[3], uint8_t *src[3],
                   int dst_stride[3], int src_stride[3], int width,
                   int height, uint8_t *qp_store, int qp_stride)
{
    int x, y, i, j;
    const int count = 1<<p->log2_count;
    int ret;

    for (i = 0; i < 3; i++) {
        int is_chroma = !!i;
        int w = AV_CEIL_RSHIFT(width,  is_chroma ? p->hsub : 0);
        int h = AV_CEIL_RSHIFT(height, is_chroma ? p->vsub : 0);
        int stride = p->temp_stride[i];
        int block = BLOCK >> (is_chroma ? p->hsub : 0);

        if (!src[i] || !dst[i])
            continue;
        for (y = 0; y < h; y++) {
            int index = block + block * stride + y * stride;

            memcpy(p->src[i] + index, src[i] + y * src_stride[i], w );
            for (x = 0; x < block; x++) {
                p->src[i][index     - x - 1] = p->src[i][index +     x    ];
                p->src[i][index + w + x    ] = p->src[i][index + w - x - 1];
            }
        }
        for (y = 0; y < block; y++) {
            memcpy(p->src[i] + (  block-1-y) * stride, p->src[i] + (  y+block  ) * stride, stride);
            memcpy(p->src[i] + (h+block  +y) * stride, p->src[i] + (h-y+block-1) * stride, stride);
        }

        p->frame->linesize[i] = stride;
        memset(p->temp[i], 0, (h + 2 * block) * stride * sizeof(int16_t));
    }

    if (p->qp)
        p->frame->quality = p->qp * FF_QP2LAMBDA;
    else {
        int qpsum=0;
        int qpcount = (height>>4) * (height>>4);

        for (y = 0; y < (height>>4); y++) {
            for (x = 0; x < (width>>4); x++)
                qpsum += qp_store[x + y * qp_stride];
        }
        p->frame->quality = ff_norm_qscale((qpsum + qpcount/2) / qpcount, p->qscale_type) * FF_QP2LAMBDA;
    }
//    init per MB qscale stuff FIXME
    p->frame->height = height + BLOCK;
    p->frame->width  = width + BLOCK;

    for (i = 0; i < count; i++) {
        const int x1 = offset[i+count-1][0];
        const int y1 = offset[i+count-1][1];
        const int x1c = x1 >> p->hsub;
        const int y1c = y1 >> p->vsub;
        const int BLOCKc = BLOCK >> p->hsub;
        int offset;
        AVPacket *pkt = p->pkt;
        int got_pkt_ptr;

        av_packet_unref(pkt);
        pkt->data = p->outbuf;
        pkt->size = p->outbuf_size;

        p->frame->data[0] = p->src[0] + x1   + y1   * p->frame->linesize[0];
        p->frame->data[1] = p->src[1] + x1c  + y1c  * p->frame->linesize[1];
        p->frame->data[2] = p->src[2] + x1c  + y1c  * p->frame->linesize[2];
        p->frame->format  = p->avctx_enc[i]->pix_fmt;

        ret = avcodec_encode_video2(p->avctx_enc[i], pkt, p->frame, &got_pkt_ptr);
        if (ret < 0) {
            av_log(p->avctx_enc[i], AV_LOG_ERROR, "Encoding failed\n");
            continue;
        }
        av_packet_unref(pkt);

        p->frame_dec = p->avctx_enc[i]->coded_frame;

        offset = (BLOCK-x1) + (BLOCK-y1) * p->frame_dec->linesize[0];

        for (y = 0; y < height; y++)
            for (x = 0; x < width; x++)
                p->temp[0][x + y * p->temp_stride[0]] += p->frame_dec->data[0][x + y * p->frame_dec->linesize[0] + offset];

        if (!src[2] || !dst[2])
            continue;

        offset = (BLOCKc-x1c) + (BLOCKc-y1c) * p->frame_dec->linesize[1];

        for (y = 0; y < AV_CEIL_RSHIFT(height, p->vsub); y++) {
            for (x = 0; x < AV_CEIL_RSHIFT(width, p->hsub); x++) {
                p->temp[1][x + y * p->temp_stride[1]] += p->frame_dec->data[1][x + y * p->frame_dec->linesize[1] + offset];
                p->temp[2][x + y * p->temp_stride[2]] += p->frame_dec->data[2][x + y * p->frame_dec->linesize[2] + offset];
            }
        }
    }

    for (j = 0; j < 3; j++) {
        int is_chroma = !!j;
        if (!dst[j])
            continue;
        store_slice_c(dst[j], p->temp[j], dst_stride[j], p->temp_stride[j],
                      AV_CEIL_RSHIFT(width,  is_chroma ? p->hsub : 0),
                      AV_CEIL_RSHIFT(height, is_chroma ? p->vsub : 0),
                      8-p->log2_count);
    }
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{

    AVFilterContext *ctx = inlink->dst;
    USPPContext *uspp = ctx->priv;
    const int height = inlink->h;
    const int width  = inlink->w;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int i;

    const AVCodec *enc = avcodec_find_encoder(AV_CODEC_ID_SNOW);
    if (!enc) {
        av_log(ctx, AV_LOG_ERROR, "SNOW encoder not found.\n");
        return AVERROR(EINVAL);
    }

    uspp->hsub = desc->log2_chroma_w;
    uspp->vsub = desc->log2_chroma_h;

    for (i = 0; i < 3; i++) {
        int is_chroma = !!i;
        int w = (width  + 4 * BLOCK-1) & (~(2 * BLOCK-1));
        int h = (height + 4 * BLOCK-1) & (~(2 * BLOCK-1));

        if (is_chroma) {
            w = AV_CEIL_RSHIFT(w, uspp->hsub);
            h = AV_CEIL_RSHIFT(h, uspp->vsub);
        }

        uspp->temp_stride[i] = w;
        if (!(uspp->temp[i] = av_malloc_array(uspp->temp_stride[i], h * sizeof(int16_t))))
            return AVERROR(ENOMEM);
        if (!(uspp->src [i] = av_malloc_array(uspp->temp_stride[i], h * sizeof(uint8_t))))
            return AVERROR(ENOMEM);
    }

    for (i = 0; i < (1<<uspp->log2_count); i++) {
        AVCodecContext *avctx_enc;
        AVDictionary *opts = NULL;
        int ret;

        if (!(uspp->avctx_enc[i] = avcodec_alloc_context3(NULL)))
            return AVERROR(ENOMEM);

        avctx_enc = uspp->avctx_enc[i];
        avctx_enc->width = width + BLOCK;
        avctx_enc->height = height + BLOCK;
        avctx_enc->time_base = (AVRational){1,25};  // meaningless
        avctx_enc->gop_size = INT_MAX;
        avctx_enc->max_b_frames = 0;
        avctx_enc->pix_fmt = inlink->format;
        avctx_enc->flags = AV_CODEC_FLAG_QSCALE | AV_CODEC_FLAG_LOW_DELAY;
        avctx_enc->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
        avctx_enc->global_quality = 123;
        av_dict_set(&opts, "no_bitstream", "1", 0);
        ret = avcodec_open2(avctx_enc, enc, &opts);
        av_dict_free(&opts);
        if (ret < 0)
            return ret;
        av_assert0(avctx_enc->codec);
    }

    uspp->outbuf_size = (width + BLOCK) * (height + BLOCK) * 10;
    if (!(uspp->frame = av_frame_alloc()))
        return AVERROR(ENOMEM);
    if (!(uspp->pkt = av_packet_alloc()))
        return AVERROR(ENOMEM);
    if (!(uspp->outbuf = av_malloc(uspp->outbuf_size)))
        return AVERROR(ENOMEM);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    USPPContext *uspp = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = in;

    int qp_stride = 0;
    int8_t *qp_table = NULL;
    int ret = 0;

    /* if we are not in a constant user quantizer mode and we don't want to use
     * the quantizers from the B-frames (B-frames often have a higher QP), we
     * need to save the qp table from the last non B-frame; this is what the
     * following code block does */
    if (!uspp->qp && (uspp->use_bframe_qp || in->pict_type != AV_PICTURE_TYPE_B)) {
        ret = ff_qp_table_extract(in, &qp_table, &qp_stride, NULL, &uspp->qscale_type);
        if (ret < 0) {
            av_frame_free(&in);
            return ret;
        }

        if (!uspp->use_bframe_qp && in->pict_type != AV_PICTURE_TYPE_B) {
            av_freep(&uspp->non_b_qp_table);
            uspp->non_b_qp_table  = qp_table;
            uspp->non_b_qp_stride = qp_stride;
        }
    }

    if (uspp->log2_count && !ctx->is_disabled) {
        if (!uspp->use_bframe_qp && uspp->non_b_qp_table) {
            qp_table = uspp->non_b_qp_table;
            qp_stride = uspp->non_b_qp_stride;
        }

        if (qp_table || uspp->qp) {

            /* get a new frame if in-place is not possible or if the dimensions
             * are not multiple of 8 */
            if (!av_frame_is_writable(in) || (inlink->w & 7) || (inlink->h & 7)) {
                const int aligned_w = FFALIGN(inlink->w, 8);
                const int aligned_h = FFALIGN(inlink->h, 8);

                out = ff_get_video_buffer(outlink, aligned_w, aligned_h);
                if (!out) {
                    av_frame_free(&in);
                    if (qp_table != uspp->non_b_qp_table)
                        av_free(qp_table);
                    return AVERROR(ENOMEM);
                }
                av_frame_copy_props(out, in);
                out->width  = in->width;
                out->height = in->height;
            }

            filter(uspp, out->data, in->data, out->linesize, in->linesize,
                   inlink->w, inlink->h, qp_table, qp_stride);
        }
    }

    if (in != out) {
        if (in->data[3])
            av_image_copy_plane(out->data[3], out->linesize[3],
                                in ->data[3], in ->linesize[3],
                                inlink->w, inlink->h);
        av_frame_free(&in);
    }
    ret = ff_filter_frame(outlink, out);
    if (qp_table != uspp->non_b_qp_table)
        av_freep(&qp_table);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    USPPContext *uspp = ctx->priv;
    int i;

    for (i = 0; i < 3; i++) {
        av_freep(&uspp->temp[i]);
        av_freep(&uspp->src[i]);
    }

    for (i = 0; i < (1 << uspp->log2_count); i++)
        avcodec_free_context(&uspp->avctx_enc[i]);

    av_freep(&uspp->non_b_qp_table);
    av_freep(&uspp->outbuf);
    av_packet_free(&uspp->pkt);
    av_frame_free(&uspp->frame);
}

static const AVFilterPad uspp_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad uspp_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_uspp = {
    .name            = "uspp",
    .description     = NULL_IF_CONFIG_SMALL("Apply Ultra Simple / Slow Post-processing filter."),
    .priv_size       = sizeof(USPPContext),
    .uninit          = uninit,
    FILTER_INPUTS(uspp_inputs),
    FILTER_OUTPUTS(uspp_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .priv_class      = &uspp_class,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
