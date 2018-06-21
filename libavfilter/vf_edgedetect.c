/*
 * Copyright (c) 2012-2014 Clément Bœsch <u pkh me>
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

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define PLANE_R 0x4
#define PLANE_G 0x1
#define PLANE_B 0x2
#define PLANE_Y 0x1
#define PLANE_U 0x2
#define PLANE_V 0x4
#define PLANE_A 0x8

enum FilterMode {
    MODE_WIRES,
    MODE_COLORMIX,
    MODE_CANNY,
    NB_MODE
};

struct plane_info {
    uint8_t  *tmpbuf;
    uint16_t *gradients;
    char     *directions;
    int      width, height;
};

typedef struct EdgeDetectContext {
    const AVClass *class;
    struct plane_info planes[3];
    int filter_planes;
    int nb_planes;
    double   low, high;
    uint8_t  low_u8, high_u8;
    int mode;
} EdgeDetectContext;

#define OFFSET(x) offsetof(EdgeDetectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption edgedetect_options[] = {
    { "high", "set high threshold", OFFSET(high), AV_OPT_TYPE_DOUBLE, {.dbl=50/255.}, 0, 1, FLAGS },
    { "low",  "set low threshold",  OFFSET(low),  AV_OPT_TYPE_DOUBLE, {.dbl=20/255.}, 0, 1, FLAGS },
    { "mode", "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=MODE_WIRES}, 0, NB_MODE-1, FLAGS, "mode" },
        { "wires",    "white/gray wires on black",  0, AV_OPT_TYPE_CONST, {.i64=MODE_WIRES},    INT_MIN, INT_MAX, FLAGS, "mode" },
        { "colormix", "mix colors",                 0, AV_OPT_TYPE_CONST, {.i64=MODE_COLORMIX}, INT_MIN, INT_MAX, FLAGS, "mode" },
        { "canny",    "detect edges on planes",     0, AV_OPT_TYPE_CONST, {.i64=MODE_CANNY},    INT_MIN, INT_MAX, FLAGS, "mode" },
    { "planes", "set planes to filter",  OFFSET(filter_planes), AV_OPT_TYPE_FLAGS, {.i64=7}, 1, 0x7, FLAGS, "flags" },
        { "y", "filter luma plane",  0, AV_OPT_TYPE_CONST, {.i64=PLANE_Y}, 0, 0, FLAGS, "flags" },
        { "u", "filter u plane",     0, AV_OPT_TYPE_CONST, {.i64=PLANE_U}, 0, 0, FLAGS, "flags" },
        { "v", "filter v plane",     0, AV_OPT_TYPE_CONST, {.i64=PLANE_V}, 0, 0, FLAGS, "flags" },
        { "r", "filter red plane",   0, AV_OPT_TYPE_CONST, {.i64=PLANE_R}, 0, 0, FLAGS, "flags" },
        { "g", "filter green plane", 0, AV_OPT_TYPE_CONST, {.i64=PLANE_G}, 0, 0, FLAGS, "flags" },
        { "b", "filter blue plane",  0, AV_OPT_TYPE_CONST, {.i64=PLANE_B}, 0, 0, FLAGS, "flags" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(edgedetect);

static av_cold int init(AVFilterContext *ctx)
{
    EdgeDetectContext *edgedetect = ctx->priv;

    edgedetect->low_u8  = edgedetect->low  * 255. + .5;
    edgedetect->high_u8 = edgedetect->high * 255. + .5;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    const EdgeDetectContext *edgedetect = ctx->priv;
    static const enum AVPixelFormat wires_pix_fmts[] = {AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE};
    static const enum AVPixelFormat canny_pix_fmts[] = {AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_GBRP, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE};
    static const enum AVPixelFormat colormix_pix_fmts[] = {AV_PIX_FMT_GBRP, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE};
    AVFilterFormats *fmts_list;
    const enum AVPixelFormat *pix_fmts = NULL;

    if (edgedetect->mode == MODE_WIRES) {
        pix_fmts = wires_pix_fmts;
    } else if (edgedetect->mode == MODE_COLORMIX) {
        pix_fmts = colormix_pix_fmts;
    } else if (edgedetect->mode == MODE_CANNY) {
        pix_fmts = canny_pix_fmts;
    } else {
        av_assert0(0);
    }
    fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_props(AVFilterLink *inlink)
{
    int p;
    AVFilterContext *ctx = inlink->dst;
    EdgeDetectContext *edgedetect = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    edgedetect->nb_planes = inlink->format == AV_PIX_FMT_GRAY8 ? 1 : 3;
    for (p = 0; p < edgedetect->nb_planes; p++) {
        struct plane_info *plane = &edgedetect->planes[p];
        int vsub = p ? desc->log2_chroma_h : 0;
        int hsub = p ? desc->log2_chroma_w : 0;

        plane->width      = AV_CEIL_RSHIFT(inlink->w, hsub);
        plane->height     = AV_CEIL_RSHIFT(inlink->h, vsub);
        plane->tmpbuf     = av_malloc(plane->width * plane->height);
        plane->gradients  = av_calloc(plane->width * plane->height, sizeof(*plane->gradients));
        plane->directions = av_malloc(plane->width * plane->height);
        if (!plane->tmpbuf || !plane->gradients || !plane->directions)
            return AVERROR(ENOMEM);
    }
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

static void sobel(int w, int h,
                       uint16_t *dst, int dst_linesize,
                         int8_t *dir, int dir_linesize,
                  const uint8_t *src, int src_linesize)
{
    int i, j;

    for (j = 1; j < h - 1; j++) {
        dst += dst_linesize;
        dir += dir_linesize;
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
            dir[i] = get_rounded_direction(gx, gy);
        }
    }
}

static void non_maximum_suppression(int w, int h,
                                          uint8_t  *dst, int dst_linesize,
                                    const  int8_t  *dir, int dir_linesize,
                                    const uint16_t *src, int src_linesize)
{
    int i, j;

#define COPY_MAXIMA(ay, ax, by, bx) do {                \
    if (src[i] > src[(ay)*src_linesize + i+(ax)] &&     \
        src[i] > src[(by)*src_linesize + i+(bx)])       \
        dst[i] = av_clip_uint8(src[i]);                 \
} while (0)

    for (j = 1; j < h - 1; j++) {
        dst += dst_linesize;
        dir += dir_linesize;
        src += src_linesize;
        for (i = 1; i < w - 1; i++) {
            switch (dir[i]) {
            case DIRECTION_45UP:        COPY_MAXIMA( 1, -1, -1,  1); break;
            case DIRECTION_45DOWN:      COPY_MAXIMA(-1, -1,  1,  1); break;
            case DIRECTION_HORIZONTAL:  COPY_MAXIMA( 0, -1,  0,  1); break;
            case DIRECTION_VERTICAL:    COPY_MAXIMA(-1,  0,  1,  0); break;
            }
        }
    }
}

static void double_threshold(int low, int high, int w, int h,
                                   uint8_t *dst, int dst_linesize,
                             const uint8_t *src, int src_linesize)
{
    int i, j;

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

static void color_mix(int w, int h,
                            uint8_t *dst, int dst_linesize,
                      const uint8_t *src, int src_linesize)
{
    int i, j;

    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++)
            dst[i] = (dst[i] + src[i]) >> 1;
        dst += dst_linesize;
        src += src_linesize;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    EdgeDetectContext *edgedetect = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int p, direct = 0;
    AVFrame *out;

    if (edgedetect->mode != MODE_COLORMIX && av_frame_is_writable(in)) {
        direct = 1;
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    for (p = 0; p < edgedetect->nb_planes; p++) {
        struct plane_info *plane = &edgedetect->planes[p];
        uint8_t  *tmpbuf     = plane->tmpbuf;
        uint16_t *gradients  = plane->gradients;
        int8_t   *directions = plane->directions;
        const int width      = plane->width;
        const int height     = plane->height;

        if (!((1 << p) & edgedetect->filter_planes)) {
            if (!direct)
                av_image_copy_plane(out->data[p], out->linesize[p],
                                    in->data[p], in->linesize[p],
                                    width, height);
            continue;
        }

        /* gaussian filter to reduce noise  */
        gaussian_blur(ctx, width, height,
                      tmpbuf,      width,
                      in->data[p], in->linesize[p]);

        /* compute the 16-bits gradients and directions for the next step */
        sobel(width, height,
              gradients, width,
              directions,width,
              tmpbuf,    width);

        /* non_maximum_suppression() will actually keep & clip what's necessary and
         * ignore the rest, so we need a clean output buffer */
        memset(tmpbuf, 0, width * height);
        non_maximum_suppression(width, height,
                                tmpbuf,    width,
                                directions,width,
                                gradients, width);

        /* keep high values, or low values surrounded by high values */
        double_threshold(edgedetect->low_u8, edgedetect->high_u8,
                         width, height,
                         out->data[p], out->linesize[p],
                         tmpbuf,       width);

        if (edgedetect->mode == MODE_COLORMIX) {
            color_mix(width, height,
                      out->data[p], out->linesize[p],
                      in->data[p], in->linesize[p]);
        }
    }

    if (!direct)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int p;
    EdgeDetectContext *edgedetect = ctx->priv;

    for (p = 0; p < edgedetect->nb_planes; p++) {
        struct plane_info *plane = &edgedetect->planes[p];
        av_freep(&plane->tmpbuf);
        av_freep(&plane->gradients);
        av_freep(&plane->directions);
    }
}

static const AVFilterPad edgedetect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad edgedetect_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_edgedetect = {
    .name          = "edgedetect",
    .description   = NULL_IF_CONFIG_SMALL("Detect and draw edge."),
    .priv_size     = sizeof(EdgeDetectContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = edgedetect_inputs,
    .outputs       = edgedetect_outputs,
    .priv_class    = &edgedetect_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
