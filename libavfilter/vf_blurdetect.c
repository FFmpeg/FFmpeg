/*
 * Copyright (c) 2021 Thilo Borgmann <thilo.borgmann _at_ mail.de>
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
 * No-reference blurdetect filter
 *
 * Implementing:
 * Marziliano, Pina, et al. "A no-reference perceptual blur metric." Proceedings.
 * International conference on image processing. Vol. 3. IEEE, 2002.
 * https://infoscience.epfl.ch/record/111802/files/14%20A%20no-reference%20perceptual%20blur%20metric.pdf
 *
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "libavutil/qsort.h"
#include "internal.h"
#include "edge_common.h"

static int comp(const float *a,const float *b)
{
    return FFDIFFSIGN(*a, *b);
}

typedef struct BLRContext {
    const AVClass *class;

    int hsub, vsub;
    int nb_planes;

    float   low, high;
    uint8_t low_u8, high_u8;
    int     radius;        // radius during local maxima detection
    int     block_pct;     // percentage of "sharpest" blocks in the image to use for bluriness calculation
    int     block_width;   // width for block abbreviation
    int     block_height;  // height for block abbreviation
    int     planes;        // number of planes to filter

    double   blur_total;
    uint64_t nb_frames;

    float    *blks;
    uint8_t  *filterbuf;
    uint8_t  *tmpbuf;
    uint16_t *gradients;
    int8_t   *directions;
} BLRContext;

#define OFFSET(x) offsetof(BLRContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption blurdetect_options[] = {
    { "high",          "set high threshold", OFFSET(high), AV_OPT_TYPE_FLOAT, {.dbl=30/255.}, 0, 1, FLAGS },
    { "low",           "set low threshold",  OFFSET(low),  AV_OPT_TYPE_FLOAT, {.dbl=15/255.}, 0, 1, FLAGS },
    { "radius",        "search radius for maxima detection", OFFSET(radius), AV_OPT_TYPE_INT, {.i64=50}, 1, 100, FLAGS },
    { "block_pct",     "block pooling threshold when calculating blurriness", OFFSET(block_pct), AV_OPT_TYPE_INT, {.i64=80}, 1, 100, FLAGS },
    { "block_width",   "block size for block-based abbreviation of blurriness", OFFSET(block_width), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, FLAGS },
    { "block_height",  "block size for block-based abbreviation of blurriness", OFFSET(block_height), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, FLAGS },
    { "planes",        "set planes to filter", OFFSET(planes), AV_OPT_TYPE_INT, {.i64=1}, 0, 15, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(blurdetect);

static av_cold int blurdetect_init(AVFilterContext *ctx)
{
    BLRContext *s = ctx->priv;

    s->low_u8  = s->low  * 255. + .5;
    s->high_u8 = s->high * 255. + .5;

    return 0;
}

static int blurdetect_config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    BLRContext      *s   = ctx->priv;
    const int bufsize    = inlink->w * inlink->h;
    const AVPixFmtDescriptor *pix_desc;

    pix_desc = av_pix_fmt_desc_get(inlink->format);
    s->hsub = pix_desc->log2_chroma_w;
    s->vsub = pix_desc->log2_chroma_h;
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    if (s->block_width  < 1 || s->block_height < 1) {
        s->block_width  = inlink->w;
        s->block_height = inlink->h;
    }

    s->tmpbuf     = av_malloc(bufsize);
    s->filterbuf  = av_malloc(bufsize);
    s->gradients  = av_calloc(bufsize, sizeof(*s->gradients));
    s->directions = av_malloc(bufsize);
    s->blks       = av_calloc((inlink->w / s->block_width) * (inlink->h / s->block_height),
                              sizeof(*s->blks));

    if (!s->tmpbuf || !s->filterbuf || !s->gradients || !s->directions || !s->blks)
        return AVERROR(ENOMEM);

    return 0;
}

// edge width is defined as the distance between surrounding maxima of the edge pixel
static float edge_width(BLRContext *blr, int i, int j, int8_t dir, int w, int h,
                        int edge, const uint8_t *src, int src_linesize)
{
    float width = 0;
    int dX, dY;
    int sign;
    int tmp;
    int p1;
    int p2;
    int k, x, y;
    int radius = blr->radius;

    switch(dir) {
    case DIRECTION_HORIZONTAL: dX = 1; dY =  0; break;
    case DIRECTION_VERTICAL:   dX = 0; dY =  1; break;
    case DIRECTION_45UP:       dX = 1; dY = -1; break;
    case DIRECTION_45DOWN:     dX = 1; dY =  1; break;
    default:                   dX = 1; dY =  1; break;
    }

    // determines if search in direction dX/dY is looking for a maximum or minimum
    sign = src[j * src_linesize + i] > src[(j - dY) * src_linesize + i - dX] ? 1 : -1;

    // search in -(dX/dY) direction
    for (k = 0; k < radius; k++) {
        x = i - k*dX;
        y = j - k*dY;
        p1 = y * src_linesize + x;
        x -= dX;
        y -= dY;
        p2 = y * src_linesize + x;
        if (x < 0 || x >= w || y < 0 || y >= h)
            return 0;

        tmp = (src[p1] - src[p2]) * sign;

        if (tmp <= 0) // local maximum found
            break;
    }
    width += k;

    // search in +(dX/dY) direction
    for (k = 0; k < radius; k++) {
        x = i + k * dX;
        y = j + k * dY;
        p1 = y * src_linesize + x;
        x += dX;
        y += dY;
        p2 = y * src_linesize + x;
        if (x < 0 || x >= w || y < 0 || y >= h)
            return 0;

        tmp = (src[p1] - src[p2]) * sign;

        if (tmp >= 0) // local maximum found
            break;
    }
    width += k;

    // for 45 degree directions approximate edge width in pixel units: 0.7 ~= sqrt(2)/2
    if (dir == DIRECTION_45UP || dir == DIRECTION_45DOWN)
        width *= 0.7;

    return width;
}

static float calculate_blur(BLRContext *s, int w, int h, int hsub, int vsub,
                            int8_t* dir, int dir_linesize,
                            uint8_t* dst, int dst_linesize,
                            uint8_t* src, int src_linesize)
{
    float total_width = 0.0;
    int block_count;
    double block_total_width;

    int i, j;
    int blkcnt = 0;

    float *blks = s->blks;
    float block_pool_threshold = s->block_pct / 100.0;

    int block_width  = AV_CEIL_RSHIFT(s->block_width,  hsub);
    int block_height = AV_CEIL_RSHIFT(s->block_height, vsub);
    int brows = h / block_height;
    int bcols = w / block_width;

    for (int blkj = 0; blkj < brows; blkj++) {
        for (int blki = 0; blki < bcols; blki++) {
            block_total_width = 0.0;
            block_count = 0;
            for (int inj = 0; inj < block_height; inj++) {
                for (int ini = 0; ini < block_width; ini++) {
                    i = blki * block_width + ini;
                    j = blkj * block_height + inj;

                    if (dst[j * dst_linesize + i] > 0) {
                        float width = edge_width(s, i, j, dir[j*dir_linesize+i],
                                                 w, h, dst[j*dst_linesize+i],
                                                 src, src_linesize);
                        if (width > 0.001) { // throw away zeros
                            block_count++;
                            block_total_width += width;
                        }
                    }
                }
            }
            // if not enough edge pixels in a block, consider it smooth
            if (block_total_width >= 2 && block_count) {
                blks[blkcnt] = block_total_width / block_count;
                blkcnt++;
            }
        }
    }

    // simple block pooling by sorting and keeping the sharper blocks
    AV_QSORT(blks, blkcnt, float, comp);
    blkcnt = ceil(blkcnt * block_pool_threshold);
    for (int i = 0; i < blkcnt; i++) {
        total_width += blks[i];
    }

    return  total_width / blkcnt;
}

static void set_meta(AVDictionary **metadata, const char *key, float d)
{
    char value[128];
    snprintf(value, sizeof(value), "%f", d);
    av_dict_set(metadata, key, value, 0);
}

static int blurdetect_filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx  = inlink->dst;
    BLRContext *s         = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    const int inw = inlink->w;
    const int inh = inlink->h;

    uint8_t *tmpbuf     = s->tmpbuf;
    uint8_t *filterbuf  = s->filterbuf;
    uint16_t *gradients = s->gradients;
    int8_t *directions  = s->directions;

    float blur = 0.0f;
    int nplanes = 0;
    AVDictionary **metadata;
    metadata = &in->metadata;

    for (int plane = 0; plane < s->nb_planes; plane++) {
        int hsub = plane == 1 || plane == 2 ? s->hsub : 0;
        int vsub = plane == 1 || plane == 2 ? s->vsub : 0;
        int w = AV_CEIL_RSHIFT(inw, hsub);
        int h = AV_CEIL_RSHIFT(inh, vsub);

        if (!((1 << plane) & s->planes))
            continue;

        nplanes++;

        // gaussian filter to reduce noise
        ff_gaussian_blur(w, h,
                         filterbuf,  w,
                         in->data[plane], in->linesize[plane]);

        // compute the 16-bits gradients and directions for the next step
        ff_sobel(w, h, gradients, w, directions, w, filterbuf, w);

        // non_maximum_suppression() will actually keep & clip what's necessary and
        // ignore the rest, so we need a clean output buffer
        memset(tmpbuf, 0, inw * inh);
        ff_non_maximum_suppression(w, h, tmpbuf, w, directions, w, gradients, w);


        // keep high values, or low values surrounded by high values
        ff_double_threshold(s->low_u8, s->high_u8, w, h,
                            tmpbuf, w, tmpbuf, w);

        blur += calculate_blur(s, w, h, hsub, vsub, directions, w,
                              tmpbuf, w, filterbuf, w);
    }

    if (nplanes)
        blur /= nplanes;

    s->blur_total += blur;

    // write stats
    av_log(ctx, AV_LOG_VERBOSE, "blur: %.7f\n", blur);

    set_meta(metadata, "lavfi.blur", blur);

    s->nb_frames = inlink->frame_count_in;

    return ff_filter_frame(outlink, in);
}

static av_cold void blurdetect_uninit(AVFilterContext *ctx)
{
    BLRContext *s = ctx->priv;

    if (s->nb_frames > 0) {
        av_log(ctx, AV_LOG_INFO, "blur mean: %.7f\n",
               s->blur_total / s->nb_frames);
    }

    av_freep(&s->tmpbuf);
    av_freep(&s->filterbuf);
    av_freep(&s->gradients);
    av_freep(&s->directions);
    av_freep(&s->blks);
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GBRP,     AV_PIX_FMT_GBRAP,
    AV_PIX_FMT_YUV422P,  AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P,  AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUV411P,  AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_NONE
};

static const AVFilterPad blurdetect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = blurdetect_config_input,
        .filter_frame = blurdetect_filter_frame,
    },
};

static const AVFilterPad blurdetect_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_blurdetect = {
    .name          = "blurdetect",
    .description   = NULL_IF_CONFIG_SMALL("Blurdetect filter."),
    .priv_size     = sizeof(BLRContext),
    .init          = blurdetect_init,
    .uninit        = blurdetect_uninit,
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    FILTER_INPUTS(blurdetect_inputs),
    FILTER_OUTPUTS(blurdetect_outputs),
    .priv_class    = &blurdetect_class,
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
};
