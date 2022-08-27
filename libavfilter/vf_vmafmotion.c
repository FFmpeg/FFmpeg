/*
 * Copyright (c) 2017 Ronald S. Bultje <rsbultje@gmail.com>
 * Copyright (c) 2017 Ashish Pratap Singh <ashk43712@gmail.com>
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
 * Calculate VMAF Motion score.
 */

#include "libavutil/file_open.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "vmaf_motion.h"

#define BIT_SHIFT 15

static const float FILTER_5[5] = {
    0.054488685,
    0.244201342,
    0.402619947,
    0.244201342,
    0.054488685
};

typedef struct VMAFMotionContext {
    const AVClass *class;
    VMAFMotionData data;
    FILE *stats_file;
    char *stats_file_str;
} VMAFMotionContext;

#define OFFSET(x) offsetof(VMAFMotionContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption vmafmotion_options[] = {
    {"stats_file", "Set file where to store per-frame difference information", OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(vmafmotion);

static uint64_t image_sad(const uint16_t *img1, const uint16_t *img2, int w,
                          int h, ptrdiff_t _img1_stride, ptrdiff_t _img2_stride)
{
    ptrdiff_t img1_stride = _img1_stride / sizeof(*img1);
    ptrdiff_t img2_stride = _img2_stride / sizeof(*img2);
    uint64_t sum = 0;
    int i, j;

    for (i = 0; i < h; i++) {
        for (j = 0; j < w; j++) {
            sum += abs(img1[j] - img2[j]);
        }
        img1 += img1_stride;
        img2 += img2_stride;
    }

    return sum;
}

static void convolution_x(const uint16_t *filter, int filt_w, const uint16_t *src,
                          uint16_t *dst, int w, int h, ptrdiff_t _src_stride,
                          ptrdiff_t _dst_stride)
{
    ptrdiff_t src_stride = _src_stride / sizeof(*src);
    ptrdiff_t dst_stride = _dst_stride / sizeof(*dst);
    int radius = filt_w / 2;
    int borders_left = radius;
    int borders_right = w - (filt_w - radius);
    int i, j, k;
    int sum = 0;

    for (i = 0; i < h; i++) {
        for (j = 0; j < borders_left; j++) {
            sum = 0;
            for (k = 0; k < filt_w; k++) {
                int j_tap = FFABS(j - radius + k);
                if (j_tap >= w) {
                    j_tap = w - (j_tap - w + 1);
                }
                sum += filter[k] * src[i * src_stride + j_tap];
            }
            dst[i * dst_stride + j] = sum >> BIT_SHIFT;
        }

        for (j = borders_left; j < borders_right; j++) {
            int sum = 0;
            for (k = 0; k < filt_w; k++) {
                sum += filter[k] * src[i * src_stride + j - radius + k];
            }
            dst[i * dst_stride + j] = sum >> BIT_SHIFT;
        }

        for (j = borders_right; j < w; j++) {
            sum = 0;
            for (k = 0; k < filt_w; k++) {
                int j_tap = FFABS(j - radius + k);
                if (j_tap >= w) {
                    j_tap = w - (j_tap - w + 1);
                }
                sum += filter[k] * src[i * src_stride + j_tap];
            }
            dst[i * dst_stride + j] = sum >> BIT_SHIFT;
        }
    }
}

#define conv_y_fn(type, bits) \
static void convolution_y_##bits##bit(const uint16_t *filter, int filt_w, \
                                      const uint8_t *_src, uint16_t *dst, \
                                      int w, int h, ptrdiff_t _src_stride, \
                                      ptrdiff_t _dst_stride) \
{ \
    const type *src = (const type *) _src; \
    ptrdiff_t src_stride = _src_stride / sizeof(*src); \
    ptrdiff_t dst_stride = _dst_stride / sizeof(*dst); \
    int radius = filt_w / 2; \
    int borders_top = radius; \
    int borders_bottom = h - (filt_w - radius); \
    int i, j, k; \
    int sum = 0; \
    \
    for (i = 0; i < borders_top; i++) { \
        for (j = 0; j < w; j++) { \
            sum = 0; \
            for (k = 0; k < filt_w; k++) { \
                int i_tap = FFABS(i - radius + k); \
                if (i_tap >= h) { \
                    i_tap = h - (i_tap - h + 1); \
                } \
                sum += filter[k] * src[i_tap * src_stride + j]; \
            } \
            dst[i * dst_stride + j] = sum >> bits; \
        } \
    } \
    for (i = borders_top; i < borders_bottom; i++) { \
        for (j = 0; j < w; j++) { \
            sum = 0; \
            for (k = 0; k < filt_w; k++) { \
                sum += filter[k] * src[(i - radius + k) * src_stride + j]; \
            } \
            dst[i * dst_stride + j] = sum >> bits; \
        } \
    } \
    for (i = borders_bottom; i < h; i++) { \
        for (j = 0; j < w; j++) { \
            sum = 0; \
            for (k = 0; k < filt_w; k++) { \
                int i_tap = FFABS(i - radius + k); \
                if (i_tap >= h) { \
                    i_tap = h - (i_tap - h + 1); \
                } \
                sum += filter[k] * src[i_tap * src_stride + j]; \
            } \
            dst[i * dst_stride + j] = sum >> bits; \
        } \
    } \
}

conv_y_fn(uint8_t, 8)
conv_y_fn(uint16_t, 10)

static void vmafmotiondsp_init(VMAFMotionDSPContext *dsp, int bpp) {
    dsp->convolution_x = convolution_x;
    dsp->convolution_y = bpp == 10 ? convolution_y_10bit : convolution_y_8bit;
    dsp->sad = image_sad;
}

double ff_vmafmotion_process(VMAFMotionData *s, AVFrame *ref)
{
    double score;

    s->vmafdsp.convolution_y(s->filter, 5, ref->data[0], s->temp_data,
                             s->width, s->height, ref->linesize[0], s->stride);
    s->vmafdsp.convolution_x(s->filter, 5, s->temp_data, s->blur_data[0],
                             s->width, s->height, s->stride, s->stride);

    if (!s->nb_frames) {
        score = 0.0;
    } else {
        uint64_t sad = s->vmafdsp.sad(s->blur_data[1], s->blur_data[0],
                                      s->width, s->height, s->stride, s->stride);
        // the output score is always normalized to 8 bits
        score = (double) (sad * 1.0 / (s->width * s->height << (BIT_SHIFT - 8)));
    }

    FFSWAP(uint16_t *, s->blur_data[0], s->blur_data[1]);
    s->nb_frames++;
    s->motion_sum += score;

    return score;
}

static void set_meta(AVDictionary **metadata, const char *key, float d)
{
    char value[128];
    snprintf(value, sizeof(value), "%0.2f", d);
    av_dict_set(metadata, key, value, 0);
}

static void do_vmafmotion(AVFilterContext *ctx, AVFrame *ref)
{
    VMAFMotionContext *s = ctx->priv;
    double score;

    score = ff_vmafmotion_process(&s->data, ref);
    set_meta(&ref->metadata, "lavfi.vmafmotion.score", score);
    if (s->stats_file) {
        fprintf(s->stats_file,
                "n:%"PRId64" motion:%0.2lf\n", s->data.nb_frames, score);
    }
}


int ff_vmafmotion_init(VMAFMotionData *s,
                       int w, int h, enum AVPixelFormat fmt)
{
    size_t data_sz;
    int i;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);

    if (w < 3 || h < 3)
        return AVERROR(EINVAL);

    s->width = w;
    s->height = h;
    s->stride = FFALIGN(w * sizeof(uint16_t), 32);

    data_sz = (size_t) s->stride * h;
    if (!(s->blur_data[0] = av_malloc(data_sz)) ||
        !(s->blur_data[1] = av_malloc(data_sz)) ||
        !(s->temp_data    = av_malloc(data_sz))) {
        return AVERROR(ENOMEM);
    }

    for (i = 0; i < 5; i++) {
        s->filter[i] = lrint(FILTER_5[i] * (1 << BIT_SHIFT));
    }

    vmafmotiondsp_init(&s->vmafdsp, desc->comp[0].depth);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *fmts_list = NULL;
    int format, ret;

    for (format = 0; av_pix_fmt_desc_get(format); format++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(format);
        if (!(desc->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_PAL)) &&
            (desc->flags & AV_PIX_FMT_FLAG_PLANAR || desc->nb_components == 1) &&
            (!(desc->flags & AV_PIX_FMT_FLAG_BE) == !HAVE_BIGENDIAN || desc->comp[0].depth == 8) &&
            (desc->comp[0].depth == 8 || desc->comp[0].depth == 10) &&
            (ret = ff_add_format(&fmts_list, format)) < 0)
            return ret;
    }

    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input_ref(AVFilterLink *inlink)
{
    AVFilterContext *ctx  = inlink->dst;
    VMAFMotionContext *s = ctx->priv;

    return ff_vmafmotion_init(&s->data, ctx->inputs[0]->w,
                              ctx->inputs[0]->h, ctx->inputs[0]->format);
}

double ff_vmafmotion_uninit(VMAFMotionData *s)
{
    av_free(s->blur_data[0]);
    av_free(s->blur_data[1]);
    av_free(s->temp_data);

    return s->nb_frames > 0 ? s->motion_sum / s->nb_frames : 0.0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *ref)
{
    AVFilterContext *ctx = inlink->dst;
    do_vmafmotion(ctx, ref);
    return ff_filter_frame(ctx->outputs[0], ref);
}

static av_cold int init(AVFilterContext *ctx)
{
    VMAFMotionContext *s = ctx->priv;

    if (s->stats_file_str) {
        if (!strcmp(s->stats_file_str, "-")) {
            s->stats_file = stdout;
        } else {
            s->stats_file = avpriv_fopen_utf8(s->stats_file_str, "w");
            if (!s->stats_file) {
                int err = AVERROR(errno);
                char buf[128];
                av_strerror(err, buf, sizeof(buf));
                av_log(ctx, AV_LOG_ERROR, "Could not open stats file %s: %s\n",
                       s->stats_file_str, buf);
                return err;
            }
        }
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    VMAFMotionContext *s = ctx->priv;
    double avg_motion = ff_vmafmotion_uninit(&s->data);

    if (s->data.nb_frames > 0) {
        av_log(ctx, AV_LOG_INFO, "VMAF Motion avg: %.3f\n", avg_motion);
    }

    if (s->stats_file && s->stats_file != stdout)
        fclose(s->stats_file);
}

static const AVFilterPad vmafmotion_inputs[] = {
    {
        .name         = "reference",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input_ref,
    },
};

static const AVFilterPad vmafmotion_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_vmafmotion = {
    .name          = "vmafmotion",
    .description   = NULL_IF_CONFIG_SMALL("Calculate the VMAF Motion score."),
    .init          = init,
    .uninit        = uninit,
    .priv_size     = sizeof(VMAFMotionContext),
    .priv_class    = &vmafmotion_class,
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(vmafmotion_inputs),
    FILTER_OUTPUTS(vmafmotion_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
