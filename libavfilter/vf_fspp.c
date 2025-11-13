/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (C) 2005 Nikolaj Poroshin <porosh3@psu.ru>
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
 * Fast Simple Post-processing filter
 * This implementation is based on an algorithm described in
 * "Aria Nosratinia Embedded Post-Processing for
 * Enhancement of Compressed Images (1999)"
 * (http://www.utdallas.edu/~aria/papers/vlsisp99.pdf)
 * Further, with splitting (I)DCT into horizontal/vertical passes, one of
 * them can be performed once per block, not per pixel. This allows for much
 * higher speed.
 *
 * Originally written by Michael Niedermayer and Nikolaj for the MPlayer
 * project, and ported by Arwa Arif for FFmpeg.
 */

#include "libavutil/emms.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/video_enc_params.h"

#include "avfilter.h"
#include "filters.h"
#include "qp_table.h"
#include "vf_fsppdsp.h"
#include "video.h"

#define BLOCKSZ  12
#define MAX_LEVEL 5

typedef struct FSPPContext {
    const struct AVClass *class;

    int log2_count;
    int strength;
    int hsub;
    int vsub;
    int temp_stride;
    int qp;
    enum AVVideoEncParamsType qscale_type;
    int prev_q;
    uint8_t *src;
    int16_t *temp;
    int8_t  *non_b_qp_table;
    int non_b_qp_stride;
    int use_bframe_qp;

    FSPPDSPContext dsp;

    DECLARE_ALIGNED(16, int16_t, threshold_mtx_noq)[8 * 8];
    DECLARE_ALIGNED(16, int16_t, threshold_mtx)[8 * 8];
} FSPPContext;


#define OFFSET(x) offsetof(FSPPContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption fspp_options[] = {
    { "quality",       "set quality",                          OFFSET(log2_count),    AV_OPT_TYPE_INT, {.i64 = 4},   4, MAX_LEVEL, FLAGS },
    { "qp",            "force a constant quantizer parameter", OFFSET(qp),            AV_OPT_TYPE_INT, {.i64 = 0},   0, 64,        FLAGS },
    { "strength",      "set filter strength",                  OFFSET(strength),      AV_OPT_TYPE_INT, {.i64 = 0}, -15, 32,        FLAGS },
    { "use_bframe_qp", "use B-frames' QP",                     OFFSET(use_bframe_qp), AV_OPT_TYPE_BOOL,{.i64 = 0},   0, 1,         FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(fspp);

static const short custom_threshold[64] = {
// values (296) can't be too high
// -it causes too big quant dependence
// or maybe overflow(check), which results in some flashing
// reorder coefficients to the order in which columns are processed
#define REORDER(a,b,c,d,e,f,g,h) c, g, a, e, f, d, b, h
    REORDER( 71, 296, 295, 237,  71,  40,  38,  19),
    REORDER(245, 193, 185, 121, 102,  73,  53,  27),
    REORDER(158, 129, 141, 107,  97,  73,  50,  26),
    REORDER(102, 116, 109,  98,  82,  66,  45,  23),
    REORDER( 71,  94,  95,  81,  70,  56,  38,  20),
    REORDER( 56,  77,  74,  66,  56,  44,  30,  15),
    REORDER( 38,  53,  50,  45,  38,  30,  21,  11),
    REORDER( 20,  27,  26,  23,  20,  15,  11,   5)
};

static void filter(FSPPContext *p, uint8_t *dst, uint8_t *src,
                   int dst_stride, int src_stride,
                   int width, int height,
                   uint8_t *qp_store, int qp_stride, int is_luma)
{
    int x, x0, y, es, qy, t;

    const int stride = is_luma ? p->temp_stride : (width + 16);
    const int step = 6 - p->log2_count;
    const int qpsh = 4 - p->hsub * !is_luma;
    const int qpsv = 4 - p->vsub * !is_luma;

    DECLARE_ALIGNED(16, int16_t, block_align)[8 * 8 * BLOCKSZ + 8 * 8 * BLOCKSZ];
    int16_t *block  = block_align;
    int16_t *block3 = block_align + 8 * 8 * BLOCKSZ;

    memset(block3, 0, 4 * 8 * BLOCKSZ);

    if (!src || !dst) return;

    for (y = 0; y < height; y++) {
        int index = 8 + 8 * stride + y * stride;
        memcpy(p->src + index, src + y * src_stride, width);
        for (x = 0; x < 8; x++) {
            p->src[index         - x - 1] = p->src[index +         x    ];
            p->src[index + width + x    ] = p->src[index + width - x - 1];
        }
    }

    for (y = 0; y < 8; y++) {
        memcpy(p->src + (     7 - y    ) * stride, p->src + (     y + 8    ) * stride, stride);
        memcpy(p->src + (height + 8 + y) * stride, p->src + (height - y + 7) * stride, stride);
    }
    //FIXME (try edge emu)

    for (y = 8; y < 24; y++)
        memset(p->temp + 8 + y * stride, 0, width * sizeof(int16_t));

    for (y = step; y < height + 8; y += step) {    //step= 1,2
        const int y1 = y - 8 + step;                 //l5-7  l4-6;
        qy = y - 4;

        if (qy > height - 1) qy = height - 1;
        if (qy < 0) qy = 0;

        qy = (qy >> qpsv) * qp_stride;
        p->dsp.row_fdct(block, p->src + y * stride + 2 - (y&1), stride, 2);

        for (x0 = 0; x0 < width + 8 - 8 * (BLOCKSZ - 1); x0 += 8 * (BLOCKSZ - 1)) {
            p->dsp.row_fdct(block + 8 * 8, p->src + y * stride + 8 + x0 + 2 - (y&1), stride, 2 * (BLOCKSZ - 1));

            if (p->qp)
                p->dsp.column_fidct(p->threshold_mtx, block + 0 * 8, block3 + 0 * 8, 8 * (BLOCKSZ - 1)); //yes, this is a HOTSPOT
            else
                for (x = 0; x < 8 * (BLOCKSZ - 1); x += 8) {
                    t = x + x0 - 2;                    //correct t=x+x0-2-(y&1), but its the same

                    if (t < 0) t = 0;                   //t always < width-2

                    t = qp_store[qy + (t >> qpsh)];
                    t = ff_norm_qscale(t, p->qscale_type);

                    if (t != p->prev_q) {
                        p->prev_q = t;
                        p->dsp.mul_thrmat(p->threshold_mtx_noq, p->threshold_mtx, t);
                    }
                    p->dsp.column_fidct(p->threshold_mtx, block + x * 8, block3 + x * 8, 8); //yes, this is a HOTSPOT
                }
            p->dsp.row_idct(block3 + 0 * 8, p->temp + (y & 15) * stride + x0 + 2 - (y & 1), stride, 2 * (BLOCKSZ - 1));
            memmove(block,  block  + (BLOCKSZ - 1) * 64, 8 * 8 * sizeof(int16_t)); //cycling
            memmove(block3, block3 + (BLOCKSZ - 1) * 64, 6 * 8 * sizeof(int16_t));
        }

        es = width + 8 - x0; //  8, ...
        if (es > 8)
            p->dsp.row_fdct(block + 8 * 8, p->src + y * stride + 8 + x0 + 2 - (y & 1), stride, (es - 4) >> 2);

        p->dsp.column_fidct(p->threshold_mtx, block, block3, es&(~1));
        if (es > 3)
            p->dsp.row_idct(block3 + 0 * 8, p->temp + (y & 15) * stride + x0 + 2 - (y & 1), stride, es >> 2);

        if (!(y1 & 7) && y1) {
            if (y1 & 8)
                p->dsp.store_slice(dst + (y1 - 8) * dst_stride, p->temp + 8 + 8 * stride,
                                   dst_stride, stride, width, 8, 5 - p->log2_count);
            else
                p->dsp.store_slice2(dst + (y1 - 8) * dst_stride, p->temp + 8 + 0 * stride,
                                    dst_stride, stride, width, 8, 5 - p->log2_count);
        }
    }

    if (y & 7) {  // height % 8 != 0
        if (y & 8)
            p->dsp.store_slice(dst + ((y - 8) & ~7) * dst_stride, p->temp + 8 + 8 * stride,
                               dst_stride, stride, width, y&7, 5 - p->log2_count);
        else
            p->dsp.store_slice2(dst + ((y - 8) & ~7) * dst_stride, p->temp + 8 + 0 * stride,
                            dst_stride, stride, width, y&7, 5 - p->log2_count);
    }
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    FSPPContext *fspp = ctx->priv;
    const int h = FFALIGN(inlink->h + 16, 16);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    fspp->hsub = desc->log2_chroma_w;
    fspp->vsub = desc->log2_chroma_h;

    fspp->temp_stride = FFALIGN(inlink->w + 16, 16);
    fspp->temp = av_malloc_array(fspp->temp_stride, h * sizeof(*fspp->temp));
    fspp->src  = av_malloc_array(fspp->temp_stride, h * sizeof(*fspp->src));

    if (!fspp->temp || !fspp->src)
        return AVERROR(ENOMEM);

    ff_fsppdsp_init(&fspp->dsp);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    FSPPContext *fspp = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = in;

    int qp_stride = 0;
    int8_t *qp_table = NULL;
    int ret = 0;

    //FIXME: tune custom_threshold[] and remove this !
    for (int i = 0, bias = (1 << 4) + fspp->strength; i < 64; ++i)
        fspp->threshold_mtx_noq[i] = (int)(custom_threshold[i] * (bias / 71.0) + 0.5);

    if (fspp->qp) {
        fspp->prev_q = fspp->qp;
        fspp->dsp.mul_thrmat(fspp->threshold_mtx_noq, fspp->threshold_mtx, fspp->qp);
    }

    /* if we are not in a constant user quantizer mode and we don't want to use
     * the quantizers from the B-frames (B-frames often have a higher QP), we
     * need to save the qp table from the last non B-frame; this is what the
     * following code block does */
    if (!fspp->qp && (fspp->use_bframe_qp || in->pict_type != AV_PICTURE_TYPE_B)) {
        ret = ff_qp_table_extract(in, &qp_table, &qp_stride, NULL, &fspp->qscale_type);
        if (ret < 0) {
            av_frame_free(&in);
            return ret;
        }

        if (!fspp->use_bframe_qp && in->pict_type != AV_PICTURE_TYPE_B) {
            av_freep(&fspp->non_b_qp_table);
            fspp->non_b_qp_table  = qp_table;
            fspp->non_b_qp_stride = qp_stride;
        }
    }

    if (fspp->log2_count && !ctx->is_disabled) {
        if (!fspp->use_bframe_qp && fspp->non_b_qp_table) {
            qp_table = fspp->non_b_qp_table;
            qp_stride = fspp->non_b_qp_stride;
        }

        if (qp_table || fspp->qp) {
            const int cw = AV_CEIL_RSHIFT(inlink->w, fspp->hsub);
            const int ch = AV_CEIL_RSHIFT(inlink->h, fspp->vsub);

            /* get a new frame if in-place is not possible or if the dimensions
             * are not multiple of 8 */
            if (!av_frame_is_writable(in) || (inlink->w & 7) || (inlink->h & 7)) {
                const int aligned_w = FFALIGN(inlink->w, 8);
                const int aligned_h = FFALIGN(inlink->h, 8);

                out = ff_get_video_buffer(outlink, aligned_w, aligned_h);
                if (!out) {
                    av_frame_free(&in);
                    ret = AVERROR(ENOMEM);
                    goto finish;
                }
                av_frame_copy_props(out, in);
                out->width = in->width;
                out->height = in->height;
            }

            filter(fspp, out->data[0], in->data[0], out->linesize[0], in->linesize[0],
                   inlink->w, inlink->h, qp_table, qp_stride, 1);
            filter(fspp, out->data[1], in->data[1], out->linesize[1], in->linesize[1],
                   cw,        ch,        qp_table, qp_stride, 0);
            filter(fspp, out->data[2], in->data[2], out->linesize[2], in->linesize[2],
                   cw,        ch,        qp_table, qp_stride, 0);
            emms_c();
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
finish:
    if (qp_table != fspp->non_b_qp_table)
        av_freep(&qp_table);
    return ret;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    FSPPContext *fspp = ctx->priv;
    av_freep(&fspp->temp);
    av_freep(&fspp->src);
    av_freep(&fspp->non_b_qp_table);
}

static const AVFilterPad fspp_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const FFFilter ff_vf_fspp = {
    .p.name          = "fspp",
    .p.description   = NULL_IF_CONFIG_SMALL("Apply Fast Simple Post-processing filter."),
    .p.priv_class    = &fspp_class,
    .p.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .priv_size       = sizeof(FSPPContext),
    .uninit          = uninit,
    FILTER_INPUTS(fspp_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
};
