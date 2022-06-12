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

#include "libavutil/imgutils.h"
#include "libavutil/mem_internal.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "internal.h"
#include "qp_table.h"
#include "vf_spp.h"

enum mode {
    MODE_HARD,
    MODE_SOFT,
    NB_MODES
};

static const AVClass *child_class_iterate(void **iter)
{
    const AVClass *c = *iter ? NULL : avcodec_dct_get_class();
    *iter = (void*)(uintptr_t)c;
    return c;
}

static void *child_next(void *obj, void *prev)
{
    SPPContext *s = obj;
    return prev ? NULL : s->dct;
}

#define OFFSET(x) offsetof(SPPContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define TFLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
static const AVOption spp_options[] = {
    { "quality", "set quality", OFFSET(log2_count), AV_OPT_TYPE_INT, {.i64 = 3}, 0, MAX_LEVEL, TFLAGS },
    { "qp", "force a constant quantizer parameter", OFFSET(qp), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 63, FLAGS },
    { "mode", "set thresholding mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64 = MODE_HARD}, 0, NB_MODES - 1, FLAGS, "mode" },
        { "hard", "hard thresholding", 0, AV_OPT_TYPE_CONST, {.i64 = MODE_HARD}, INT_MIN, INT_MAX, FLAGS, "mode" },
        { "soft", "soft thresholding", 0, AV_OPT_TYPE_CONST, {.i64 = MODE_SOFT}, INT_MIN, INT_MAX, FLAGS, "mode" },
    { "use_bframe_qp", "use B-frames' QP", OFFSET(use_bframe_qp), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS },
    { NULL }
};

static const AVClass spp_class = {
    .class_name       = "spp",
    .item_name        = av_default_item_name,
    .option           = spp_options,
    .version          = LIBAVUTIL_VERSION_INT,
    .category         = AV_CLASS_CATEGORY_FILTER,
    .child_class_iterate = child_class_iterate,
    .child_next       = child_next,
};

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

static const uint8_t offset[128][2] = {
    {0,0},                                                  // unused
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

static void store_slice16_c(uint16_t *dst, const int16_t *src,
                            int dst_linesize, int src_linesize,
                            int width, int height, int log2_scale,
                            const uint8_t dither[8][8], int depth)
{
    int y, x;
    unsigned int mask = -1<<depth;

#define STORE16(pos) do {                                                   \
    temp = ((src[x + y*src_linesize + pos] << log2_scale) + (d[pos]>>1)) >> 5;   \
    if (temp & mask )                                                       \
        temp = ~(temp >> 31);                                               \
    dst[x + y*dst_linesize + pos] = temp;                                   \
} while (0)

    for (y = 0; y < height; y++) {
        const uint8_t *d = dither[y];
        for (x = 0; x < width; x += 8) {
            int temp;
            STORE16(0);
            STORE16(1);
            STORE16(2);
            STORE16(3);
            STORE16(4);
            STORE16(5);
            STORE16(6);
            STORE16(7);
        }
    }
}

static inline void add_block(uint16_t *dst, int linesize, const int16_t block[64])
{
    int y;

    for (y = 0; y < 8; y++) {
        dst[0 + y*linesize] += block[0 + y*8];
        dst[1 + y*linesize] += block[1 + y*8];
        dst[2 + y*linesize] += block[2 + y*8];
        dst[3 + y*linesize] += block[3 + y*8];
        dst[4 + y*linesize] += block[4 + y*8];
        dst[5 + y*linesize] += block[5 + y*8];
        dst[6 + y*linesize] += block[6 + y*8];
        dst[7 + y*linesize] += block[7 + y*8];
    }
}

static void filter(SPPContext *p, uint8_t *dst, uint8_t *src,
                   int dst_linesize, int src_linesize, int width, int height,
                   const uint8_t *qp_table, int qp_stride, int is_luma, int depth)
{
    int x, y, i;
    const int count = 1 << p->log2_count;
    const int linesize = is_luma ? p->temp_linesize : FFALIGN(width+16, 16);
    DECLARE_ALIGNED(16, uint64_t, block_align)[32];
    int16_t *block  = (int16_t *)block_align;
    int16_t *block2 = (int16_t *)(block_align + 16);
    uint16_t *psrc16 = (uint16_t*)p->src;
    const int sample_bytes = (depth+7) / 8;

    for (y = 0; y < height; y++) {
        int index = 8 + 8*linesize + y*linesize;
        memcpy(p->src + index*sample_bytes, src + y*src_linesize, width*sample_bytes);
        if (sample_bytes == 1) {
            for (x = 0; x < 8; x++) {
                p->src[index         - x - 1] = p->src[index +         x    ];
                p->src[index + width + x    ] = p->src[index + width - x - 1];
            }
        } else {
            for (x = 0; x < 8; x++) {
                psrc16[index         - x - 1] = psrc16[index +         x    ];
                psrc16[index + width + x    ] = psrc16[index + width - x - 1];
            }
        }
    }
    for (y = 0; y < 8; y++) {
        memcpy(p->src + (       7-y)*linesize * sample_bytes, p->src + (       y+8)*linesize * sample_bytes, linesize * sample_bytes);
        memcpy(p->src + (height+8+y)*linesize * sample_bytes, p->src + (height-y+7)*linesize * sample_bytes, linesize * sample_bytes);
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
                qp = FFMAX(1, ff_norm_qscale(qp, p->qscale_type));
            }
            for (i = 0; i < count; i++) {
                const int x1 = x + offset[i + count][0];
                const int y1 = y + offset[i + count][1];
                const int index = x1 + y1*linesize;
                p->dct->get_pixels_unaligned(block, p->src + sample_bytes*index, sample_bytes*linesize);
                p->dct->fdct(block);
                p->requantize(block2, block, qp, p->dct->idct_permutation);
                p->dct->idct(block2);
                add_block(p->temp + index, linesize, block2);
            }
        }
        if (y) {
            if (sample_bytes == 1) {
                p->store_slice(dst + (y - 8) * dst_linesize, p->temp + 8 + y*linesize,
                               dst_linesize, linesize, width,
                               FFMIN(8, height + 8 - y), MAX_LEVEL - p->log2_count,
                               ldither);
            } else {
                store_slice16_c((uint16_t*)(dst + (y - 8) * dst_linesize), p->temp + 8 + y*linesize,
                                dst_linesize/2, linesize, width,
                                FFMIN(8, height + 8 - y), MAX_LEVEL - p->log2_count,
                                ldither, depth);
            }
        }
    }
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV420P,  AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV410P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUV444P10,  AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV444P9,  AV_PIX_FMT_YUV422P9,
    AV_PIX_FMT_YUV420P9,
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP9,
    AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_NONE
};

static int config_input(AVFilterLink *inlink)
{
    SPPContext *s = inlink->dst->priv;
    const int h = FFALIGN(inlink->h + 16, 16);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int bps = desc->comp[0].depth;

    s->store_slice = store_slice_c;
    switch (s->mode) {
    case MODE_HARD: s->requantize = hardthresh_c; break;
    case MODE_SOFT: s->requantize = softthresh_c; break;
    }

    av_opt_set_int(s->dct, "bits_per_sample", bps, 0);
    avcodec_dct_init(s->dct);

#if ARCH_X86
    ff_spp_init_x86(s);
#endif

    s->hsub = desc->log2_chroma_w;
    s->vsub = desc->log2_chroma_h;
    s->temp_linesize = FFALIGN(inlink->w + 16, 16);
    s->temp = av_malloc_array(s->temp_linesize, h * sizeof(*s->temp));
    s->src  = av_malloc_array(s->temp_linesize, h * sizeof(*s->src) * 2);

    if (!s->temp || !s->src)
        return AVERROR(ENOMEM);
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    SPPContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out = in;
    int qp_stride = 0;
    int8_t *qp_table = NULL;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int depth = desc->comp[0].depth;
    int ret = 0;

    /* if we are not in a constant user quantizer mode and we don't want to use
     * the quantizers from the B-frames (B-frames often have a higher QP), we
     * need to save the qp table from the last non B-frame; this is what the
     * following code block does */
    if (!s->qp && (s->use_bframe_qp || in->pict_type != AV_PICTURE_TYPE_B)) {
        ret = ff_qp_table_extract(in, &qp_table, &qp_stride, NULL, &s->qscale_type);
        if (ret < 0) {
            av_frame_free(&in);
            return ret;
        }

        if (!s->use_bframe_qp && in->pict_type != AV_PICTURE_TYPE_B) {
            av_freep(&s->non_b_qp_table);
            s->non_b_qp_table  = qp_table;
            s->non_b_qp_stride = qp_stride;
        }
    }

    if (s->log2_count && !ctx->is_disabled) {
        if (!s->use_bframe_qp && s->non_b_qp_table) {
            qp_table  = s->non_b_qp_table;
            qp_stride = s->non_b_qp_stride;
        }

        if (qp_table || s->qp) {
            const int cw = AV_CEIL_RSHIFT(inlink->w, s->hsub);
            const int ch = AV_CEIL_RSHIFT(inlink->h, s->vsub);

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
                out->width  = in->width;
                out->height = in->height;
            }

            filter(s, out->data[0], in->data[0], out->linesize[0], in->linesize[0], inlink->w, inlink->h, qp_table, qp_stride, 1, depth);

            if (out->data[2]) {
                filter(s, out->data[1], in->data[1], out->linesize[1], in->linesize[1], cw,        ch,        qp_table, qp_stride, 0, depth);
                filter(s, out->data[2], in->data[2], out->linesize[2], in->linesize[2], cw,        ch,        qp_table, qp_stride, 0, depth);
            }
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
    if (qp_table != s->non_b_qp_table)
        av_freep(&qp_table);
    return ret;
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    SPPContext *s = ctx->priv;

    if (!strcmp(cmd, "level") || !strcmp(cmd, "quality")) {
        if (!strcmp(args, "max"))
            s->log2_count = MAX_LEVEL;
        else
            s->log2_count = av_clip(strtol(args, NULL, 10), 0, MAX_LEVEL);
        return 0;
    }
    return AVERROR(ENOSYS);
}

static av_cold int preinit(AVFilterContext *ctx)
{
    SPPContext *s = ctx->priv;

    s->dct = avcodec_dct_alloc();
    if (!s->dct)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SPPContext *s = ctx->priv;

    av_freep(&s->temp);
    av_freep(&s->src);
    av_freep(&s->dct);
    av_freep(&s->non_b_qp_table);
}

static const AVFilterPad spp_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

static const AVFilterPad spp_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_spp = {
    .name            = "spp",
    .description     = NULL_IF_CONFIG_SMALL("Apply a simple post processing filter."),
    .priv_size       = sizeof(SPPContext),
    .preinit         = preinit,
    .uninit          = uninit,
    FILTER_INPUTS(spp_inputs),
    FILTER_OUTPUTS(spp_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .process_command = process_command,
    .priv_class      = &spp_class,
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
