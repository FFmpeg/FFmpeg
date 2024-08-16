/*
 * Copyright (c) 2002 A'rpi
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
 * border detection filter
 * Ported from MPlayer libmpcodecs/vf_cropdetect.c.
 */

#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/motion_vector.h"
#include "libavutil/qsort.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"
#include "edge_common.h"

typedef struct CropDetectContext {
    const AVClass *class;
    int x1, y1, x2, y2;
    float limit;
    float limit_upscaled;
    int round;
    int skip;
    int reset_count;
    int frame_nb;
    int max_pixsteps[4];
    int max_outliers;
    int mode;
    int window_size;
    int mv_threshold;
    int bitdepth;
    float   low, high;
    uint8_t low_u8, high_u8;
    uint8_t  *filterbuf;
    uint8_t  *tmpbuf;
    uint16_t *gradients;
    char     *directions;
    int      *bboxes[4];
} CropDetectContext;

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P,
    AV_PIX_FMT_YUV411P, AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_YUV420P9 , AV_PIX_FMT_YUV422P9 , AV_PIX_FMT_YUV444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_NV12,    AV_PIX_FMT_NV21,
    AV_PIX_FMT_RGB24,   AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGBA,    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_NONE
};

enum CropMode {
    MODE_BLACK,
    MODE_MV_EDGES,
    MODE_NB
};

static int comp(const int *a,const int *b)
{
    return FFDIFFSIGN(*a, *b);
}

static int checkline(void *ctx, const unsigned char *src, int stride, int len, int bpp)
{
    int total = 0;
    int div = len;
    const uint16_t *src16 = (const uint16_t *)src;

    switch (bpp) {
    case 1:
        while (len >= 8) {
            total += src[       0] + src[  stride] + src[2*stride] + src[3*stride]
                  +  src[4*stride] + src[5*stride] + src[6*stride] + src[7*stride];
            src += 8*stride;
            len -= 8;
        }
        while (--len >= 0) {
            total += src[0];
            src += stride;
        }
        break;
    case 2:
        stride >>= 1;
        while (len >= 8) {
            total += src16[       0] + src16[  stride] + src16[2*stride] + src16[3*stride]
                  +  src16[4*stride] + src16[5*stride] + src16[6*stride] + src16[7*stride];
            src16 += 8*stride;
            len -= 8;
        }
        while (--len >= 0) {
            total += src16[0];
            src16 += stride;
        }
        break;
    case 3:
    case 4:
        while (len >= 4) {
            total += src[0]        + src[1         ] + src[2         ]
                  +  src[  stride] + src[1+  stride] + src[2+  stride]
                  +  src[2*stride] + src[1+2*stride] + src[2+2*stride]
                  +  src[3*stride] + src[1+3*stride] + src[2+3*stride];
            src += 4*stride;
            len -= 4;
        }
        while (--len >= 0) {
            total += src[0] + src[1] + src[2];
            src += stride;
        }
        div *= 3;
        break;
    }
    total /= div;

    av_log(ctx, AV_LOG_DEBUG, "total:%d\n", total);
    return total;
}

static int checkline_edge(void *ctx, const unsigned char *src, int stride, int len, int bpp)
{
    const uint16_t *src16 = (const uint16_t *)src;

    switch (bpp) {
    case 1:
        while (--len >= 0) {
            if (src[0]) return 0;
            src += stride;
        }
        break;
    case 2:
        stride >>= 1;
        while (--len >= 0) {
            if (src16[0]) return 0;
            src16 += stride;
        }
        break;
    case 3:
    case 4:
        while (--len >= 0) {
            if (src[0] || src[1] || src[2]) return 0;
            src += stride;
        }
        break;
    }

    return 1;
}

static av_cold int init(AVFilterContext *ctx)
{
    CropDetectContext *s = ctx->priv;

    s->frame_nb = -1 * s->skip;
    s->low_u8   = s->low  * 255. + .5;
    s->high_u8  = s->high * 255. + .5;

    av_log(ctx, AV_LOG_VERBOSE, "limit:%f round:%d skip:%d reset_count:%d\n",
           s->limit, s->round, s->skip, s->reset_count);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    CropDetectContext *s = ctx->priv;

    av_freep(&s->tmpbuf);
    av_freep(&s->filterbuf);
    av_freep(&s->gradients);
    av_freep(&s->directions);
    av_freep(&s->bboxes[0]);
    av_freep(&s->bboxes[1]);
    av_freep(&s->bboxes[2]);
    av_freep(&s->bboxes[3]);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    CropDetectContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int bufsize = inlink->w * inlink->h;

    av_image_fill_max_pixsteps(s->max_pixsteps, NULL, desc);

    s->bitdepth = desc->comp[0].depth;

    if (s->limit < 1.0)
        s->limit_upscaled = s->limit * ((1 << s->bitdepth) - 1);
    else
        s->limit_upscaled = s->limit;

    s->x1 = inlink->w - 1;
    s->y1 = inlink->h - 1;
    s->x2 = 0;
    s->y2 = 0;

    s->window_size = FFMAX(s->reset_count, 15);
    s->tmpbuf      = av_malloc(bufsize);
    s->filterbuf   = av_malloc(bufsize * s->max_pixsteps[0]);
    s->gradients   = av_calloc(bufsize, sizeof(*s->gradients));
    s->directions  = av_malloc(bufsize);
    s->bboxes[0]   = av_malloc(s->window_size * sizeof(*s->bboxes[0]));
    s->bboxes[1]   = av_malloc(s->window_size * sizeof(*s->bboxes[1]));
    s->bboxes[2]   = av_malloc(s->window_size * sizeof(*s->bboxes[2]));
    s->bboxes[3]   = av_malloc(s->window_size * sizeof(*s->bboxes[3]));

    if (!s->tmpbuf    || !s->filterbuf || !s->gradients || !s->directions ||
        !s->bboxes[0] || !s->bboxes[1] || !s->bboxes[2] || !s->bboxes[3])
        return AVERROR(ENOMEM);

    return 0;
}

#define SET_META(key, value) \
    av_dict_set_int(metadata, key, value, 0)

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    CropDetectContext *s = ctx->priv;
    int bpp = s->max_pixsteps[0];
    int w, h, x, y, shrink_by, i;
    AVDictionary **metadata;
    int outliers, last_y;
    int limit_upscaled = lrint(s->limit_upscaled);
    char limit_str[22];

    const int inw = inlink->w;
    const int inh = inlink->h;
    uint8_t *tmpbuf     = s->tmpbuf;
    uint8_t *filterbuf  = s->filterbuf;
    uint16_t *gradients = s->gradients;
    int8_t *directions  = s->directions;
    const AVFrameSideData *sd = NULL;
    int scan_w, scan_h, bboff;

    void (*sobel)(int w, int h, uint16_t *dst, int dst_linesize,
                  int8_t *dir, int dir_linesize,
                  const uint8_t *src, int src_linesize, int src_stride) = (bpp == 2) ? &ff_sobel_16 : &ff_sobel_8;
    void (*gaussian_blur)(int w, int h,
                          uint8_t *dst, int dst_linesize,
                          const uint8_t *src, int src_linesize, int src_stride) = (bpp == 2) ? &ff_gaussian_blur_16 : &ff_gaussian_blur_8;


    // ignore first s->skip frames
    if (++s->frame_nb > 0) {
        metadata = &frame->metadata;

        // Reset the crop area every reset_count frames, if reset_count is > 0
        if (s->reset_count > 0 && s->frame_nb > s->reset_count) {
            s->x1 = frame->width  - 1;
            s->y1 = frame->height - 1;
            s->x2 = 0;
            s->y2 = 0;
            s->frame_nb = 1;
        }

#define FIND(DST, FROM, NOEND, INC, STEP0, STEP1, LEN) \
        outliers = 0;\
        for (last_y = y = FROM; NOEND; y = y INC) {\
            if (checkline(ctx, frame->data[0] + STEP0 * y, STEP1, LEN, bpp) > limit_upscaled) {\
                if (++outliers > s->max_outliers) { \
                    DST = last_y;\
                    break;\
                }\
            } else\
                last_y = y INC;\
        }

        if (s->mode == MODE_BLACK) {
            FIND(s->y1,                 0,               y < s->y1, +1, frame->linesize[0], bpp, frame->width);
            FIND(s->y2, frame->height - 1, y > FFMAX(s->y2, s->y1), -1, frame->linesize[0], bpp, frame->width);
            FIND(s->x1,                 0,               y < s->x1, +1, bpp, frame->linesize[0], frame->height);
            FIND(s->x2,  frame->width - 1, y > FFMAX(s->x2, s->x1), -1, bpp, frame->linesize[0], frame->height);
        } else { // MODE_MV_EDGES
            sd = av_frame_get_side_data(frame, AV_FRAME_DATA_MOTION_VECTORS);
            s->x1 = 0;
            s->y1 = 0;
            s->x2 = inw - 1;
            s->y2 = inh - 1;

            if (!sd) {
                av_log(ctx, AV_LOG_WARNING, "Cannot detect: no motion vectors available");
            } else {
                // gaussian filter to reduce noise
                gaussian_blur(inw, inh,
                              filterbuf,  inw*bpp,
                              frame->data[0], frame->linesize[0], bpp);

                // compute the 16-bits gradients and directions for the next step
                sobel(inw, inh, gradients, inw, directions, inw, filterbuf, inw*bpp, bpp);

                // non_maximum_suppression() will actually keep & clip what's necessary and
                // ignore the rest, so we need a clean output buffer
                memset(tmpbuf, 0, inw * inh);
                ff_non_maximum_suppression(inw, inh, tmpbuf, inw, directions, inw, gradients, inw);


                // keep high values, or low values surrounded by high values
                ff_double_threshold(s->low_u8, s->high_u8, inw, inh,
                                    tmpbuf, inw, tmpbuf, inw);

                // scan all MVs and store bounding box
                s->x1 = inw - 1;
                s->y1 = inh - 1;
                s->x2 = 0;
                s->y2 = 0;
                for (i = 0; i < sd->size / sizeof(AVMotionVector); i++) {
                    const AVMotionVector *mv = (const AVMotionVector*)sd->data + i;
                    const int mx = mv->dst_x - mv->src_x;
                    const int my = mv->dst_y - mv->src_y;

                    if (mv->dst_x >= 0 && mv->dst_x < inw &&
                        mv->dst_y >= 0 && mv->dst_y < inh &&
                        mv->src_x >= 0 && mv->src_x < inw &&
                        mv->src_y >= 0 && mv->src_y < inh &&
                        mx * mx + my * my >= s->mv_threshold * s->mv_threshold) {
                        s->x1 = mv->dst_x < s->x1 ? mv->dst_x : s->x1;
                        s->y1 = mv->dst_y < s->y1 ? mv->dst_y : s->y1;
                        s->x2 = mv->dst_x > s->x2 ? mv->dst_x : s->x2;
                        s->y2 = mv->dst_y > s->y2 ? mv->dst_y : s->y2;
                    }
                }

                // assert x1<x2, y1<y2
                if (s->x1 > s->x2) FFSWAP(int, s->x1, s->x2);
                if (s->y1 > s->y2) FFSWAP(int, s->y1, s->y2);

                // scan outward looking for 0-edge-lines in edge image
                scan_w = s->x2 - s->x1;
                scan_h = s->y2 - s->y1;

#define FIND_EDGE(DST, FROM, NOEND, INC, STEP0, STEP1, LEN)             \
    for (last_y = y = FROM; NOEND; y = y INC) {                         \
        if (checkline_edge(ctx, tmpbuf + STEP0 * y, STEP1, LEN, bpp)) { \
            if (last_y INC == y) {                                      \
                DST = y;                                                \
                break;                                                  \
            } else                                                      \
                last_y = y;                                             \
        }                                                               \
    }                                                                   \
    if (!(NOEND)) {                                                     \
        DST = y -(INC);                                                 \
    }

                FIND_EDGE(s->y1, s->y1, y >=  0, -1, inw, bpp, scan_w);
                FIND_EDGE(s->y2, s->y2, y < inh, +1, inw, bpp, scan_w);
                FIND_EDGE(s->x1, s->x1, y >=  0, -1, bpp, inw, scan_h);
                FIND_EDGE(s->x2, s->x2, y < inw, +1, bpp, inw, scan_h);

                // queue bboxes
                bboff = (s->frame_nb - 1) % s->window_size;
                s->bboxes[0][bboff] = s->x1;
                s->bboxes[1][bboff] = s->x2;
                s->bboxes[2][bboff] = s->y1;
                s->bboxes[3][bboff] = s->y2;

                // sort queue
                bboff = FFMIN(s->frame_nb, s->window_size);
                AV_QSORT(s->bboxes[0], bboff, int, comp);
                AV_QSORT(s->bboxes[1], bboff, int, comp);
                AV_QSORT(s->bboxes[2], bboff, int, comp);
                AV_QSORT(s->bboxes[3], bboff, int, comp);

                // return median of window_size elems
                s->x1 = s->bboxes[0][bboff/2];
                s->x2 = s->bboxes[1][bboff/2];
                s->y1 = s->bboxes[2][bboff/2];
                s->y2 = s->bboxes[3][bboff/2];
            }
        }

        // round x and y (up), important for yuv colorspaces
        // make sure they stay rounded!
        x = (s->x1+1) & ~1;
        y = (s->y1+1) & ~1;

        w = s->x2 - x + 1;
        h = s->y2 - y + 1;

        // w and h must be divisible by 2 as well because of yuv
        // colorspace problems.
        if (s->round <= 1)
            s->round = 16;
        if (s->round % 2)
            s->round *= 2;

        shrink_by = w % s->round;
        w -= shrink_by;
        x += (shrink_by/2 + 1) & ~1;

        shrink_by = h % s->round;
        h -= shrink_by;
        y += (shrink_by/2 + 1) & ~1;

        SET_META("lavfi.cropdetect.x1", s->x1);
        SET_META("lavfi.cropdetect.x2", s->x2);
        SET_META("lavfi.cropdetect.y1", s->y1);
        SET_META("lavfi.cropdetect.y2", s->y2);
        SET_META("lavfi.cropdetect.w",  w);
        SET_META("lavfi.cropdetect.h",  h);
        SET_META("lavfi.cropdetect.x",  x);
        SET_META("lavfi.cropdetect.y",  y);

        snprintf(limit_str, sizeof(limit_str), "%f", s->limit);
        av_dict_set(metadata, "lavfi.cropdetect.limit", limit_str, 0);

        av_log(ctx, AV_LOG_INFO,
               "x1:%d x2:%d y1:%d y2:%d w:%d h:%d x:%d y:%d pts:%"PRId64" t:%f limit:%f crop=%d:%d:%d:%d\n",
               s->x1, s->x2, s->y1, s->y2, w, h, x, y, frame->pts,
               frame->pts == AV_NOPTS_VALUE ? -1 : frame->pts * av_q2d(inlink->time_base),
               s->limit, w, h, x, y);
    }

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    CropDetectContext *s = ctx->priv;
    float old_limit = s->limit;
    int ret;

    if ((ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags)) < 0)
        return ret;

    if (old_limit != s->limit) {
        if (s->limit < 1.0)
            s->limit_upscaled = s->limit * ((1 << s->bitdepth) - 1);
        else
            s->limit_upscaled = s->limit;
        s->frame_nb = s->reset_count;
    }

    return 0;
}

#define OFFSET(x) offsetof(CropDetectContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define TFLAGS AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption cropdetect_options[] = {
    { "limit", "Threshold below which the pixel is considered black", OFFSET(limit),       AV_OPT_TYPE_FLOAT, { .dbl = 24.0/255 }, 0, 65535, TFLAGS },
    { "round", "Value by which the width/height should be divisible", OFFSET(round),       AV_OPT_TYPE_INT, { .i64 = 16 }, 0, INT_MAX, FLAGS },
    { "reset", "Recalculate the crop area after this many frames",    OFFSET(reset_count), AV_OPT_TYPE_INT, { .i64 = 0 },  0, INT_MAX, FLAGS },
    { "skip",  "Number of initial frames to skip",                    OFFSET(skip),        AV_OPT_TYPE_INT, { .i64 = 2 },  0, INT_MAX, FLAGS },
    { "reset_count", "Recalculate the crop area after this many frames",OFFSET(reset_count),AV_OPT_TYPE_INT,{ .i64 = 0 },  0, INT_MAX, FLAGS },
    { "max_outliers", "Threshold count of outliers",                  OFFSET(max_outliers),AV_OPT_TYPE_INT, { .i64 = 0 },  0, INT_MAX, FLAGS },
    { "mode", "set mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=MODE_BLACK}, 0, MODE_NB-1, FLAGS, .unit = "mode" },
        { "black",    "detect black pixels surrounding the video",     0, AV_OPT_TYPE_CONST, {.i64=MODE_BLACK},    INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
        { "mvedges",  "detect motion and edged surrounding the video", 0, AV_OPT_TYPE_CONST, {.i64=MODE_MV_EDGES}, INT_MIN, INT_MAX, FLAGS, .unit = "mode" },
    { "high", "Set high threshold for edge detection",                OFFSET(high),        AV_OPT_TYPE_FLOAT, {.dbl=25/255.}, 0, 1, FLAGS },
    { "low", "Set low threshold for edge detection",                  OFFSET(low),         AV_OPT_TYPE_FLOAT, {.dbl=15/255.}, 0, 1, FLAGS },
    { "mv_threshold", "motion vector threshold when estimating video window size", OFFSET(mv_threshold), AV_OPT_TYPE_INT, {.i64=8}, 0, 100, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(cropdetect);

static const AVFilterPad avfilter_vf_cropdetect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
};

const AVFilter ff_vf_cropdetect = {
    .name          = "cropdetect",
    .description   = NULL_IF_CONFIG_SMALL("Auto-detect crop size."),
    .priv_size     = sizeof(CropDetectContext),
    .priv_class    = &cropdetect_class,
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(avfilter_vf_cropdetect_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_METADATA_ONLY,
    .process_command = process_command,
};
