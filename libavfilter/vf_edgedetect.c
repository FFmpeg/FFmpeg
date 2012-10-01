/*
 * Copyright (c) 2012 Clément Bœsch
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
 * Edge detection filter
 *
 * @see https://en.wikipedia.org/wiki/Canny_edge_detector
 */

#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct {
    const AVClass *class;
    uint8_t  *tmpbuf;
    uint16_t *gradients;
    char     *directions;
    double   low, high;
    uint8_t  low_u8, high_u8;
} EdgeDetectContext;

#define OFFSET(x) offsetof(EdgeDetectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption edgedetect_options[] = {
    { "high", "set high threshold", OFFSET(high), AV_OPT_TYPE_DOUBLE, {.dbl=50/255.}, 0, 1, FLAGS },
    { "low",  "set low threshold",  OFFSET(low),  AV_OPT_TYPE_DOUBLE, {.dbl=20/255.}, 0, 1, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(edgedetect);

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    int ret;
    EdgeDetectContext *edgedetect = ctx->priv;

    edgedetect->class = &edgedetect_class;
    av_opt_set_defaults(edgedetect);

    if ((ret = av_set_options_string(edgedetect, args, "=", ":")) < 0)
        return ret;

    edgedetect->low_u8  = edgedetect->low  * 255. + .5;
    edgedetect->high_u8 = edgedetect->high * 255. + .5;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {PIX_FMT_GRAY8, PIX_FMT_NONE};
    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    EdgeDetectContext *edgedetect = ctx->priv;

    edgedetect->tmpbuf     = av_malloc(inlink->w * inlink->h);
    edgedetect->gradients  = av_calloc(inlink->w * inlink->h, sizeof(*edgedetect->gradients));
    edgedetect->directions = av_malloc(inlink->w * inlink->h);
    if (!edgedetect->tmpbuf || !edgedetect->gradients || !edgedetect->directions)
        return AVERROR(ENOMEM);
    return 0;
}

static void gaussian_blur(AVFilterContext *ctx, int w, int h,
                                uint8_t *dst, int dst_linesize,
                          const uint8_t *src, int src_linesize)
{
    int i, j;

    memcpy(dst, src, w); dst += dst_linesize; src += src_linesize;
    memcpy(dst, src, w); dst += dst_linesize; src += src_linesize;
    for (j = 2; j < h - 2; j++) {
        dst[0] = src[0];
        dst[1] = src[1];
        for (i = 2; i < w - 2; i++) {
            /* Gaussian mask of size 5x5 with sigma = 1.4 */
            dst[i] = ((src[-2*src_linesize + i-2] + src[2*src_linesize + i-2]) * 2
                    + (src[-2*src_linesize + i-1] + src[2*src_linesize + i-1]) * 4
                    + (src[-2*src_linesize + i  ] + src[2*src_linesize + i  ]) * 5
                    + (src[-2*src_linesize + i+1] + src[2*src_linesize + i+1]) * 4
                    + (src[-2*src_linesize + i+2] + src[2*src_linesize + i+2]) * 2

                    + (src[  -src_linesize + i-2] + src[  src_linesize + i-2]) *  4
                    + (src[  -src_linesize + i-1] + src[  src_linesize + i-1]) *  9
                    + (src[  -src_linesize + i  ] + src[  src_linesize + i  ]) * 12
                    + (src[  -src_linesize + i+1] + src[  src_linesize + i+1]) *  9
                    + (src[  -src_linesize + i+2] + src[  src_linesize + i+2]) *  4

                    + src[i-2] *  5
                    + src[i-1] * 12
                    + src[i  ] * 15
                    + src[i+1] * 12
                    + src[i+2] *  5) / 159;
        }
        dst[i    ] = src[i    ];
        dst[i + 1] = src[i + 1];

        dst += dst_linesize;
        src += src_linesize;
    }
    memcpy(dst, src, w); dst += dst_linesize; src += src_linesize;
    memcpy(dst, src, w);
}

enum {
    DIRECTION_45UP,
    DIRECTION_45DOWN,
    DIRECTION_HORIZONTAL,
    DIRECTION_VERTICAL,
};

static int get_rounded_direction(int gx, int gy)
{
    /* reference angles:
     *   tan( pi/8) = sqrt(2)-1
     *   tan(3pi/8) = sqrt(2)+1
     * Gy/Gx is the tangent of the angle (theta), so Gy/Gx is compared against
     * <ref-angle>, or more simply Gy against <ref-angle>*Gx
     *
     * Gx and Gy bounds = [-1020;1020], using 16-bit arithmetic:
     *   round((sqrt(2)-1) * (1<<16)) =  27146
     *   round((sqrt(2)+1) * (1<<16)) = 158218
     */
    if (gx) {
        int tanpi8gx, tan3pi8gx;

        if (gx < 0)
            gx = -gx, gy = -gy;
        gy <<= 16;
        tanpi8gx  =  27146 * gx;
        tan3pi8gx = 158218 * gx;
        if (gy > -tan3pi8gx && gy < -tanpi8gx)  return DIRECTION_45UP;
        if (gy > -tanpi8gx  && gy <  tanpi8gx)  return DIRECTION_HORIZONTAL;
        if (gy >  tanpi8gx  && gy <  tan3pi8gx) return DIRECTION_45DOWN;
    }
    return DIRECTION_VERTICAL;
}

static void sobel(AVFilterContext *ctx, int w, int h,
                        uint16_t *dst, int dst_linesize,
                  const uint8_t  *src, int src_linesize)
{
    int i, j;
    EdgeDetectContext *edgedetect = ctx->priv;

    for (j = 1; j < h - 1; j++) {
        dst += dst_linesize;
        src += src_linesize;
        for (i = 1; i < w - 1; i++) {
            const int gx =
                -1*src[-src_linesize + i-1] + 1*src[-src_linesize + i+1]
                -2*src[                i-1] + 2*src[                i+1]
                -1*src[ src_linesize + i-1] + 1*src[ src_linesize + i+1];
            const int gy =
                -1*src[-src_linesize + i-1] + 1*src[ src_linesize + i-1]
                -2*src[-src_linesize + i  ] + 2*src[ src_linesize + i  ]
                -1*src[-src_linesize + i+1] + 1*src[ src_linesize + i+1];

            dst[i] = FFABS(gx) + FFABS(gy);
            edgedetect->directions[j*w + i] = get_rounded_direction(gx, gy);
        }
    }
}

static void non_maximum_suppression(AVFilterContext *ctx, int w, int h,
                                          uint8_t  *dst, int dst_linesize,
                                    const uint16_t *src, int src_linesize)
{
    int i, j;
    EdgeDetectContext *edgedetect = ctx->priv;

#define COPY_MAXIMA(ay, ax, by, bx) do {                \
    if (src[i] > src[(ay)*src_linesize + i+(ax)] &&     \
        src[i] > src[(by)*src_linesize + i+(bx)])       \
        dst[i] = av_clip_uint8(src[i]);                 \
} while (0)

    for (j = 1; j < h - 1; j++) {
        dst += dst_linesize;
        src += src_linesize;
        for (i = 1; i < w - 1; i++) {
            switch (edgedetect->directions[j*w + i]) {
            case DIRECTION_45UP:        COPY_MAXIMA( 1, -1, -1,  1); break;
            case DIRECTION_45DOWN:      COPY_MAXIMA(-1, -1,  1,  1); break;
            case DIRECTION_HORIZONTAL:  COPY_MAXIMA( 0, -1,  0,  1); break;
            case DIRECTION_VERTICAL:    COPY_MAXIMA(-1,  0,  1,  0); break;
            }
        }
    }
}

static void double_threshold(AVFilterContext *ctx, int w, int h,
                                   uint8_t *dst, int dst_linesize,
                             const uint8_t *src, int src_linesize)
{
    int i, j;
    EdgeDetectContext *edgedetect = ctx->priv;
    const int low  = edgedetect->low_u8;
    const int high = edgedetect->high_u8;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            if (src[i] > high) {
                dst[i] = src[i];
                continue;
            }

            if ((!i || i == w - 1 || !j || j == h - 1) &&
                src[i] > low &&
                (src[-src_linesize + i-1] > high ||
                 src[-src_linesize + i  ] > high ||
                 src[-src_linesize + i+1] > high ||
                 src[                i-1] > high ||
                 src[                i+1] > high ||
                 src[ src_linesize + i-1] > high ||
                 src[ src_linesize + i  ] > high ||
                 src[ src_linesize + i+1] > high))
                dst[i] = src[i];
            else
                dst[i] = 0;
        }
        dst += dst_linesize;
        src += src_linesize;
    }
}

static int end_frame(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    EdgeDetectContext *edgedetect = ctx->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFilterBufferRef  *inpicref = inlink->cur_buf;
    AVFilterBufferRef *outpicref = outlink->out_buf;
    uint8_t  *tmpbuf    = edgedetect->tmpbuf;
    uint16_t *gradients = edgedetect->gradients;

    /* gaussian filter to reduce noise  */
    gaussian_blur(ctx, inlink->w, inlink->h,
                  tmpbuf,            inlink->w,
                  inpicref->data[0], inpicref->linesize[0]);

    /* compute the 16-bits gradients and directions for the next step */
    sobel(ctx, inlink->w, inlink->h,
          gradients, inlink->w,
          tmpbuf,    inlink->w);

    /* non_maximum_suppression() will actually keep & clip what's necessary and
     * ignore the rest, so we need a clean output buffer */
    memset(tmpbuf, 0, inlink->w * inlink->h);
    non_maximum_suppression(ctx, inlink->w, inlink->h,
                            tmpbuf,    inlink->w,
                            gradients, inlink->w);

    /* keep high values, or low values surrounded by high values */
    double_threshold(ctx, inlink->w, inlink->h,
                     outpicref->data[0], outpicref->linesize[0],
                     tmpbuf,             inlink->w);

    ff_draw_slice(outlink, 0, outlink->h, 1);
    return ff_end_frame(outlink);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    EdgeDetectContext *edgedetect = ctx->priv;
    av_freep(&edgedetect->tmpbuf);
    av_freep(&edgedetect->gradients);
    av_freep(&edgedetect->directions);
}

static int null_draw_slice(AVFilterLink *inlink, int y, int h, int slice_dir) { return 0; }

AVFilter avfilter_vf_edgedetect = {
    .name          = "edgedetect",
    .description   = NULL_IF_CONFIG_SMALL("Detect and draw edge."),
    .priv_size     = sizeof(EdgeDetectContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,

    .inputs    = (const AVFilterPad[]) {
       {
           .name             = "default",
           .type             = AVMEDIA_TYPE_VIDEO,
           .draw_slice       = null_draw_slice,
           .config_props     = config_props,
           .end_frame        = end_frame,
           .min_perms        = AV_PERM_READ
        },
        { .name = NULL }
    },
    .outputs   = (const AVFilterPad[]) {
        {
            .name            = "default",
            .type            = AVMEDIA_TYPE_VIDEO,
        },
        { .name = NULL }
    },
};
