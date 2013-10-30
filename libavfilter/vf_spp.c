/*
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2013 Clément Bœsch <u pkh me>
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
 * Simple post processing filter
 *
 * This implementation is based on an algorithm described in
 * "Aria Nosratinia Embedded Post-Processing for
 * Enhancement of Compressed Images (1999)"
 *
 * Originally written by Michael Niedermayer for the MPlayer project, and
 * ported by Clément Bœsch for FFmpeg.
 */

#include "libavcodec/dsputil.h"
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "internal.h"
#include "vf_spp.h"

enum mode {
    MODE_HARD,
    MODE_SOFT,
    NB_MODES
};

#define OFFSET(x) offsetof(SPPContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption spp_options[] = {
    { "quality", "set quality", OFFSET(log2_count), AV_OPT_TYPE_INT, {.i64 = 3}, 0, MAX_LEVEL, FLAGS },
    { "qp", "force a constant quantizer parameter", OFFSET(qp), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 63, FLAGS },
    { "mode", "set thresholding mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64 = MODE_HARD}, 0, NB_MODES - 1, FLAGS, "mode" },
        { "hard", "hard thresholding", 0, AV_OPT_TYPE_CONST, {.i64 = MODE_HARD}, INT_MIN, INT_MAX, FLAGS, "mode" },
        { "soft", "soft thresholding", 0, AV_OPT_TYPE_CONST, {.i64 = MODE_SOFT}, INT_MIN, INT_MAX, FLAGS, "mode" },
    { "use_bframe_qp", "use B-frames' QP", OFFSET(use_bframe_qp), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(spp);

// XXX: share between filters?
DECLARE_ALIGNED(8, static const uint8_t, ldither)[8][8] = {
    {  0,  48,  12,  60,   3,  51,  15,  63 },
    { 32,  16,  44,  28,  35,  19,  47,  31 },
    {  8,  56,   4,  52,  11,  59,   7,  55 },
    { 40,  24,  36,  20,  43,  27,  39,  23 },
    {  2,  50,  14,  62,   1,  49,  13,  61 },
    { 34,  18,  46,  30,  33,  17,  45,  29 },
    { 10,  58,   6,  54,   9,  57,   5,  53 },
    { 42,  26,  38,  22,  41,  25,  37,  21 },
};

static const uint8_t offset[127][2] = {
    {0,0},
    {0,0}, {4,4},                                           // quality = 1
    {0,0}, {2,2}, {6,4}, {4,6},                             // quality = 2
    {0,0}, {5,1}, {2,2}, {7,3}, {4,4}, {1,5}, {6,6}, {3,7}, // quality = 3

    {0,0}, {4,0}, {1,1}, {5,1}, {3,2}, {7,2}, {2,3}, {6,3}, // quality = 4
    {0,4}, {4,4}, {1,5}, {5,5}, {3,6}, {7,6}, {2,7}, {6,7},

    {0,0}, {0,2}, {0,4}, {0,6}, {1,1}, {1,3}, {1,5}, {1,7}, // quality = 5
    {2,0}, {2,2}, {2,4}, {2,6}, {3,1}, {3,3}, {3,5}, {3,7},
    {4,0}, {4,2}, {4,4}, {4,6}, {5,1}, {5,3}, {5,5}, {5,7},
    {6,0}, {6,2}, {6,4}, {6,6}, {7,1}, {7,3}, {7,5}, {7,7},

    {0,0}, {4,4}, {0,4}, {4,0}, {2,2}, {6,6}, {2,6}, {6,2}, // quality = 6
    {0,2}, {4,6}, {0,6}, {4,2}, {2,0}, {6,4}, {2,4}, {6,0},
    {1,1}, {5,5}, {1,5}, {5,1}, {3,3}, {7,7}, {3,7}, {7,3},
    {1,3}, {5,7}, {1,7}, {5,3}, {3,1}, {7,5}, {3,5}, {7,1},
    {0,1}, {4,5}, {0,5}, {4,1}, {2,3}, {6,7}, {2,7}, {6,3},
    {0,3}, {4,7}, {0,7}, {4,3}, {2,1}, {6,5}, {2,5}, {6,1},
    {1,0}, {5,4}, {1,4}, {5,0}, {3,2}, {7,6}, {3,6}, {7,2},
    {1,2}, {5,6}, {1,6}, {5,2}, {3,0}, {7,4}, {3,4}, {7,0},
};

static void hardthresh_c(int16_t dst[64], const int16_t src[64],
                         int qp, const uint8_t *permutation)
{
    int i;
    int bias = 0; // FIXME

    unsigned threshold1 = qp * ((1<<4) - bias) - 1;
    unsigned threshold2 = threshold1 << 1;

    memset(dst, 0, 64 * sizeof(dst[0]));
    dst[0] = (src[0] + 4) >> 3;

    for (i = 1; i < 64; i++) {
        int level = src[i];
        if (((unsigned)(level + threshold1)) > threshold2) {
            const int j = permutation[i];
            dst[j] = (level + 4) >> 3;
        }
    }
}

static void softthresh_c(int16_t dst[64], const int16_t src[64],
                         int qp, const uint8_t *permutation)
{
    int i;
    int bias = 0; //FIXME

    unsigned threshold1 = qp * ((1<<4) - bias) - 1;
    unsigned threshold2 = threshold1 << 1;

    memset(dst, 0, 64 * sizeof(dst[0]));
    dst[0] = (src[0] + 4) >> 3;

    for (i = 1; i < 64; i++) {
        int level = src[i];
        if (((unsigned)(level + threshold1)) > threshold2) {
            const int j = permutation[i];
            if (level > 0) dst[j] = (level - threshold1 + 4) >> 3;
            else           dst[j] = (level + threshold1 + 4) >> 3;
        }
    }
}

static void store_slice_c(uint8_t *dst, const int16_t *src,
                          int dst_linesize, int src_linesize,
                          int width, int height, int log2_scale,
                          const uint8_t dither[8][8])
{
    int y, x;

#define STORE(pos) do {                                                     \
    temp = ((src[x + y*src_linesize + pos] << log2_scale) + d[pos]) >> 6;   \
    if (temp & 0x100)                                                       \
        temp = ~(temp >> 31);                                               \
    dst[x + y*dst_linesize + pos] = temp;                                   \
} while (0)

    for (y = 0; y < height; y++) {
        const uint8_t *d = dither[y];
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

static inline void add_block(int16_t *dst, int linesize, const int16_t block[64])
{
    int y;

    for (y = 0; y < 8; y++) {
        *(uint32_t *)&dst[0 + y*linesize] += *(uint32_t *)&block[0 + y*8];
        *(uint32_t *)&dst[2 + y*linesize] += *(uint32_t *)&block[2 + y*8];
        *(uint32_t *)&dst[4 + y*linesize] += *(uint32_t *)&block[4 + y*8];
        *(uint32_t *)&dst[6 + y*linesize] += *(uint32_t *)&block[6 + y*8];
    }
}

// XXX: export the function?
static inline int norm_qscale(int qscale, int type)
{
    switch (type) {
    case FF_QSCALE_TYPE_MPEG1: return qscale;
    case FF_QSCALE_TYPE_MPEG2: return qscale >> 1;
    case FF_QSCALE_TYPE_H264:  return qscale >> 2;
    case FF_QSCALE_TYPE_VP56:  return (63 - qscale + 2) >> 2;
    }
    return qscale;
}

static void filter(SPPContext *p, uint8_t *dst, uint8_t *src,
                   int dst_linesize, int src_linesize, int width, int height,
                   const uint8_t *qp_table, int qp_stride, int is_luma)
{
    int x, y, i;
    const int count = 1 << p->log2_count;
    const int linesize = is_luma ? p->temp_linesize : FFALIGN(width+16, 16);
    DECLARE_ALIGNED(16, uint64_t, block_align)[32];
    int16_t *block  = (int16_t *)block_align;
    int16_t *block2 = (int16_t *)(block_align + 16);

    for (y = 0; y < height; y++) {
        int index = 8 + 8*linesize + y*linesize;
        memcpy(p->src + index, src + y*src_linesize, width);
        for (x = 0; x < 8; x++) {
            p->src[index         - x - 1] = p->src[index +         x    ];
            p->src[index + width + x    ] = p->src[index + width - x - 1];
        }
    }
    for (y = 0; y < 8; y++) {
        memcpy(p->src + (       7-y)*linesize, p->src + (       y+8)*linesize, linesize);
        memcpy(p->src + (height+8+y)*linesize, p->src + (height-y+7)*linesize, linesize);
    }

    for (y = 0; y < height + 8; y += 8) {
        memset(p->temp + (8 + y) * linesize, 0, 8 * linesize * sizeof(*p->temp));
        for (x = 0; x < width + 8; x += 8) {
            int qp;

            if (p->qp) {
                qp = p->qp;
            } else{
                const int qps = 3 + is_luma;
                qp = qp_table[(FFMIN(x, width - 1) >> qps) + (FFMIN(y, height - 1) >> qps) * qp_stride];
                qp = FFMAX(1, norm_qscale(qp, p->qscale_type));
            }
            for (i = 0; i < count; i++) {
                const int x1 = x + offset[i + count - 1][0];
                const int y1 = y + offset[i + count - 1][1];
                const int index = x1 + y1*linesize;
                p->dsp.get_pixels(block, p->src + index, linesize);
                p->dsp.fdct(block);
                p->requantize(block2, block, qp, p->dsp.idct_permutation);
                p->dsp.idct(block2);
                add_block(p->temp + index, linesize, block2);
            }
        }
        if (y)
            p->store_slice(dst + (y - 8) * dst_linesize, p->temp + 8 + y*linesize,
                           dst_linesize, linesize, width,
                           FFMIN(8, height + 8 - y), MAX_LEVEL - p->log2_count,
                           ldither);
    }
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV411P,
        AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_NONE
    };
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    SPPContext *spp = inlink->dst->priv;
    const int h = FFALIGN(inlink->h + 16, 16);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    spp->hsub = desc->log2_chroma_w;
    spp->vsub = desc->log2_chroma_h;
    spp->temp_linesize = FFALIGN(inlink->w + 16, 16);
    spp->temp = av_malloc(spp->temp_linesize * h * sizeof(*spp->temp));
    spp->src  = av_malloc(spp->temp_linesize * h * sizeof(*spp->src));
    if (!spp->use_bframe_qp) {
        /* we are assuming here the qp blocks will not be smaller that 16x16 */
        spp->non_b_qp_alloc_size = FF_CEIL_RSHIFT(inlink->w, 4) * FF_CEIL_RSHIFT(inlink->h, 4);
        spp->non_b_qp_table = av_calloc(spp->non_b_qp_alloc_size, sizeof(*spp->non_b_qp_table));
        if (!spp->non_b_qp_table)
            return AVERROR(ENOMEM);
    }
    if (!spp->temp || !spp->src)
        return AVERROR(ENOMEM);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    SPPContext *spp = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = in;
    int qp_stride = 0;
    const int8_t *qp_table = NULL;

    /* if we are not in a constant user quantizer mode and we don't want to use
     * the quantizers from the B-frames (B-frames often have a higher QP), we
     * need to save the qp table from the last non B-frame; this is what the
     * following code block does */
    if (!spp->qp) {
        qp_table = av_frame_get_qp_table(in, &qp_stride, &spp->qscale_type);

        if (qp_table && !spp->use_bframe_qp && in->pict_type != AV_PICTURE_TYPE_B) {
            int w, h;

            /* if the qp stride is not set, it means the QP are only defined on
             * a line basis */
            if (!qp_stride) {
                w = FF_CEIL_RSHIFT(inlink->w, 4);
                h = 1;
            } else {
                w = FF_CEIL_RSHIFT(qp_stride, 4);
                h = FF_CEIL_RSHIFT(inlink->h, 4);
            }
            av_assert0(w * h <= spp->non_b_qp_alloc_size);
            memcpy(spp->non_b_qp_table, qp_table, w * h);
        }
    }

    if (spp->log2_count && !ctx->is_disabled) {
        if (!spp->use_bframe_qp && spp->non_b_qp_table)
            qp_table = spp->non_b_qp_table;

        if (qp_table || spp->qp) {
            const int cw = FF_CEIL_RSHIFT(inlink->w, spp->hsub);
            const int ch = FF_CEIL_RSHIFT(inlink->h, spp->vsub);

            /* get a new frame if in-place is not possible or if the dimensions
             * are not multiple of 8 */
            if (!av_frame_is_writable(in) || (inlink->w & 7) || (inlink->h & 7)) {
                const int aligned_w = FFALIGN(inlink->w, 8);
                const int aligned_h = FFALIGN(inlink->h, 8);

                out = ff_get_video_buffer(outlink, aligned_w, aligned_h);
                if (!out) {
                    av_frame_free(&in);
                    return AVERROR(ENOMEM);
                }
                av_frame_copy_props(out, in);
                out->width  = in->width;
                out->height = in->height;
            }

            filter(spp, out->data[0], in->data[0], out->linesize[0], in->linesize[0], inlink->w, inlink->h, qp_table, qp_stride, 1);
            filter(spp, out->data[1], in->data[1], out->linesize[1], in->linesize[1], cw,        ch,        qp_table, qp_stride, 0);
            filter(spp, out->data[2], in->data[2], out->linesize[2], in->linesize[2], cw,        ch,        qp_table, qp_stride, 0);
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
    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    SPPContext *spp = ctx->priv;

    if (!strcmp(cmd, "level")) {
        if (!strcmp(args, "max"))
            spp->log2_count = MAX_LEVEL;
        else
            spp->log2_count = av_clip(strtol(args, NULL, 10), 0, MAX_LEVEL);
        return 0;
    }
    return AVERROR(ENOSYS);
}

static av_cold int init(AVFilterContext *ctx)
{
    SPPContext *spp = ctx->priv;

    spp->avctx = avcodec_alloc_context3(NULL);
    if (!spp->avctx)
        return AVERROR(ENOMEM);
    avpriv_dsputil_init(&spp->dsp, spp->avctx);
    spp->store_slice = store_slice_c;
    switch (spp->mode) {
    case MODE_HARD: spp->requantize = hardthresh_c; break;
    case MODE_SOFT: spp->requantize = softthresh_c; break;
    }
    if (ARCH_X86)
        ff_spp_init_x86(spp);
    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SPPContext *spp = ctx->priv;

    av_freep(&spp->temp);
    av_freep(&spp->src);
    if (spp->avctx) {
        avcodec_close(spp->avctx);
        av_freep(&spp->avctx);
    }
    av_freep(&spp->non_b_qp_table);
}

static const AVFilterPad spp_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad spp_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_spp = {
    .name            = "spp",
    .description     = NULL_IF_CONFIG_SMALL("Apply a simple post processing filter."),
    .priv_size       = sizeof(SPPContext),
    .init            = init,
    .uninit          = uninit,
    .query_formats   = query_formats,
    .inputs          = spp_inputs,
    .outputs         = spp_outputs,
    .process_command = process_command,
    .priv_class      = &spp_class,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
