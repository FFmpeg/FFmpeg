/*
 * Copyright (c) 2017 Ronald S. Bultje <rsbultje@gmail.com>
 * Copyright (c) 2017 Ashish Pratap Singh <ashk43712@gmail.com>
 * Copyright (c) 2021 Paul B Mahol
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
 * Calculate VIF between two input videos.
 */

#include <float.h>

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "framesync.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "vif.h"
#include "video.h"

typedef struct VIFContext {
    const AVClass *class;
    FFFrameSync fs;
    const AVPixFmtDescriptor *desc;
    int width;
    int height;
    int nb_threads;
    float factor;
    float *data_buf[13];
    float **temp;
    float *ref_data;
    float *main_data;
    double vif_sum[4];
    double vif_min[4];
    double vif_max[4];
    uint64_t nb_frames;
} VIFContext;

#define OFFSET(x) offsetof(VIFContext, x)

static const AVOption vif_options[] = {
    { NULL }
};

AVFILTER_DEFINE_CLASS(vif);

static const uint8_t vif_filter1d_width1[4] = { 17, 9, 5, 3 };

static const float vif_filter1d_table[4][17] =
{
    {
        0.00745626912, 0.0142655009, 0.0250313189, 0.0402820669, 0.0594526194,
        0.0804751068, 0.0999041125, 0.113746084, 0.118773937, 0.113746084,
        0.0999041125, 0.0804751068, 0.0594526194, 0.0402820669, 0.0250313189,
        0.0142655009, 0.00745626912
    },
    {
        0.0189780835, 0.0558981746, 0.120920904, 0.192116052, 0.224173605,
        0.192116052, 0.120920904, 0.0558981746, 0.0189780835
    },
    {
        0.054488685, 0.244201347, 0.402619958, 0.244201347, 0.054488685
    },
    {
        0.166378498, 0.667243004, 0.166378498
    }
};

typedef struct ThreadData {
    const float *filter;
    const float *src;
    float *dst;
    int w, h;
    int src_stride;
    int dst_stride;
    int filter_width;
    float **temp;
} ThreadData;

static void vif_dec2(const float *src, float *dst, int w, int h,
                     int src_stride, int dst_stride)
{
    const int dst_px_stride = dst_stride / 2;

    for (int i = 0; i < h / 2; i++) {
        for (int j = 0; j < w / 2; j++)
            dst[i * dst_px_stride + j] = src[(i * 2) * src_stride + (j * 2)];
    }
}

static void vif_statistic(const float *mu1_sq, const float *mu2_sq,
                          const float *mu1_mu2, const float *xx_filt,
                          const float *yy_filt, const float *xy_filt,
                          float *num, float *den, int w, int h)
{
    static const float sigma_nsq = 2;
    float mu1_sq_val, mu2_sq_val, mu1_mu2_val, xx_filt_val, yy_filt_val, xy_filt_val;
    float sigma1_sq, sigma2_sq, sigma12, g, sv_sq, eps = 1.0e-10f;
    float gain_limit = 100.f;
    float num_val, den_val;
    float accum_num = 0.0f;
    float accum_den = 0.0f;

    for (int i = 0; i < h; i++) {
        float accum_inner_num = 0.f;
        float accum_inner_den = 0.f;

        for (int j = 0; j < w; j++) {
            mu1_sq_val  = mu1_sq[i * w + j];
            mu2_sq_val  = mu2_sq[i * w + j];
            mu1_mu2_val = mu1_mu2[i * w + j];
            xx_filt_val = xx_filt[i * w + j];
            yy_filt_val = yy_filt[i * w + j];
            xy_filt_val = xy_filt[i * w + j];

            sigma1_sq = xx_filt_val - mu1_sq_val;
            sigma2_sq = yy_filt_val - mu2_sq_val;
            sigma12   = xy_filt_val - mu1_mu2_val;

            sigma1_sq = FFMAX(sigma1_sq, 0.0f);
            sigma2_sq = FFMAX(sigma2_sq, 0.0f);
            sigma12   = FFMAX(sigma12,   0.0f);

            g = sigma12 / (sigma1_sq + eps);
            sv_sq = sigma2_sq - g * sigma12;

            if (sigma1_sq < eps) {
                g = 0.0f;
                sv_sq = sigma2_sq;
                sigma1_sq = 0.0f;
            }

            if (sigma2_sq < eps) {
                g = 0.0f;
                sv_sq = 0.0f;
            }

            if (g < 0.0f) {
                sv_sq = sigma2_sq;
                g = 0.0f;
            }
            sv_sq = FFMAX(sv_sq, eps);

            g = FFMIN(g, gain_limit);

            num_val = log2f(1.0f + g * g * sigma1_sq / (sv_sq + sigma_nsq));
            den_val = log2f(1.0f + sigma1_sq / sigma_nsq);

            if (isnan(den_val))
                num_val = den_val = 1.f;

            accum_inner_num += num_val;
            accum_inner_den += den_val;
        }

        accum_num += accum_inner_num;
        accum_den += accum_inner_den;
    }

    num[0] = accum_num;
    den[0] = accum_den;
}

static void vif_xx_yy_xy(const float *x, const float *y, float *xx, float *yy,
                         float *xy, int w, int h)
{
    for (int i = 0; i < h; i++) {
        for (int j = 0; j < w; j++) {
            float xval = x[j];
            float yval = y[j];
            float xxval = xval * xval;
            float yyval = yval * yval;
            float xyval = xval * yval;

            xx[j] = xxval;
            yy[j] = yyval;
            xy[j] = xyval;
        }

        xx += w;
        yy += w;
        xy += w;
        x  += w;
        y  += w;
    }
}

static int vif_filter1d(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ThreadData *td = arg;
    const float *filter = td->filter;
    const float *src = td->src;
    float *dst = td->dst;
    int w = td->w;
    int h = td->h;
    int src_stride = td->src_stride;
    int dst_stride = td->dst_stride;
    int filt_w = td->filter_width;
    float *temp = td->temp[jobnr];
    const int slice_start = (h * jobnr) / nb_jobs;
    const int slice_end = (h * (jobnr+1)) / nb_jobs;

    for (int i = slice_start; i < slice_end; i++) {
        /** Vertical pass. */
        for (int j = 0; j < w; j++) {
            float sum = 0.f;

            if (i >= filt_w / 2 && i < h - filt_w / 2 - 1) {
                for (int filt_i = 0; filt_i < filt_w; filt_i++) {
                    const float filt_coeff = filter[filt_i];
                    float img_coeff;
                    int ii = i - filt_w / 2 + filt_i;

                    img_coeff = src[ii * src_stride + j];
                    sum += filt_coeff * img_coeff;
                }
            } else {
                for (int filt_i = 0; filt_i < filt_w; filt_i++) {
                    const float filt_coeff = filter[filt_i];
                    int ii = i - filt_w / 2 + filt_i;
                    float img_coeff;

                    ii = ii < 0 ? -ii : (ii >= h ? 2 * h - ii - 1 : ii);

                    img_coeff = src[ii * src_stride + j];
                    sum += filt_coeff * img_coeff;
                }
            }

            temp[j] = sum;
        }

        /** Horizontal pass. */
        for (int j = 0; j < w; j++) {
            float sum = 0.f;

            if (j >= filt_w / 2 && j < w - filt_w / 2 - 1) {
                for (int filt_j = 0; filt_j < filt_w; filt_j++) {
                    const float filt_coeff = filter[filt_j];
                    int jj = j - filt_w / 2 + filt_j;
                    float img_coeff;

                    img_coeff = temp[jj];
                    sum += filt_coeff * img_coeff;
                }
            } else {
                for (int filt_j = 0; filt_j < filt_w; filt_j++) {
                    const float filt_coeff = filter[filt_j];
                    int jj = j - filt_w / 2 + filt_j;
                    float img_coeff;

                    jj = jj < 0 ? -jj : (jj >= w ? 2 * w - jj - 1 : jj);

                    img_coeff = temp[jj];
                    sum += filt_coeff * img_coeff;
                }
            }

            dst[i * dst_stride + j] = sum;
        }
    }

    return 0;
}

int ff_compute_vif2(AVFilterContext *ctx,
                    const float *ref, const float *main, int w, int h,
                    int ref_stride, int main_stride, float *score,
                    float *data_buf[14], float **temp,
                    int gnb_threads)
{
    ThreadData td;
    float *ref_scale = data_buf[0];
    float *main_scale = data_buf[1];
    float *ref_sq = data_buf[2];
    float *main_sq = data_buf[3];
    float *ref_main = data_buf[4];
    float *mu1 = data_buf[5];
    float *mu2 = data_buf[6];
    float *mu1_sq = data_buf[7];
    float *mu2_sq = data_buf[8];
    float *mu1_mu2 = data_buf[9];
    float *ref_sq_filt = data_buf[10];
    float *main_sq_filt = data_buf[11];
    float *ref_main_filt = data_buf[12];

    float *curr_ref_scale = (float *)ref;
    float *curr_main_scale = (float *)main;
    int curr_ref_stride = ref_stride;
    int curr_main_stride = main_stride;

    float num = 0.f;
    float den = 0.f;

    for (int scale = 0; scale < 4; scale++) {
        const float *filter = vif_filter1d_table[scale];
        int filter_width = vif_filter1d_width1[scale];
        const int nb_threads = FFMIN(h, gnb_threads);
        int buf_valid_w = w;
        int buf_valid_h = h;

        td.filter = filter;
        td.filter_width = filter_width;

        if (scale > 0) {
            td.src = curr_ref_scale;
            td.dst = mu1;
            td.w = w;
            td.h = h;
            td.src_stride = curr_ref_stride;
            td.dst_stride = w;
            td.temp = temp;
            ctx->internal->execute(ctx, vif_filter1d, &td, NULL, nb_threads);

            td.src = curr_main_scale;
            td.dst = mu2;
            td.src_stride = curr_main_stride;
            ctx->internal->execute(ctx, vif_filter1d, &td, NULL, nb_threads);

            vif_dec2(mu1, ref_scale, buf_valid_w, buf_valid_h, w, w);
            vif_dec2(mu2, main_scale, buf_valid_w, buf_valid_h, w, w);

            w = buf_valid_w / 2;
            h = buf_valid_h / 2;

            buf_valid_w = w;
            buf_valid_h = h;

            curr_ref_scale = ref_scale;
            curr_main_scale = main_scale;

            curr_ref_stride = w;
            curr_main_stride = w;
        }

        td.src = curr_ref_scale;
        td.dst = mu1;
        td.w = w;
        td.h = h;
        td.src_stride = curr_ref_stride;
        td.dst_stride = w;
        td.temp = temp;
        ctx->internal->execute(ctx, vif_filter1d, &td, NULL, nb_threads);

        td.src = curr_main_scale;
        td.dst = mu2;
        td.src_stride = curr_main_stride;
        ctx->internal->execute(ctx, vif_filter1d, &td, NULL, nb_threads);

        vif_xx_yy_xy(mu1, mu2, mu1_sq, mu2_sq, mu1_mu2, w, h);

        vif_xx_yy_xy(curr_ref_scale, curr_main_scale, ref_sq, main_sq, ref_main, w, h);

        td.src = ref_sq;
        td.dst = ref_sq_filt;
        td.src_stride = w;
        ctx->internal->execute(ctx, vif_filter1d, &td, NULL, nb_threads);

        td.src = main_sq;
        td.dst = main_sq_filt;
        td.src_stride = w;
        ctx->internal->execute(ctx, vif_filter1d, &td, NULL, nb_threads);

        td.src = ref_main;
        td.dst = ref_main_filt;
        ctx->internal->execute(ctx, vif_filter1d, &td, NULL, nb_threads);

        vif_statistic(mu1_sq, mu2_sq, mu1_mu2, ref_sq_filt, main_sq_filt,
                      ref_main_filt, &num, &den, w, h);

        score[scale] = den <= FLT_EPSILON ? 1.f : num / den;
    }

    return 0;
}

#define offset_fn(type, bits)                            \
static void offset_##bits##bit(VIFContext *s,            \
                               const AVFrame *ref,       \
                               AVFrame *main, int stride)\
{                                                        \
    int w = s->width;                                    \
    int h = s->height;                                   \
                                                         \
    int ref_stride = ref->linesize[0];                   \
    int main_stride = main->linesize[0];                 \
                                                         \
    const type *ref_ptr = (const type *) ref->data[0];   \
    const type *main_ptr = (const type *) main->data[0]; \
                                            \
    const float factor = s->factor;         \
                                            \
    float *ref_ptr_data = s->ref_data;      \
    float *main_ptr_data = s->main_data;    \
                                            \
    for (int i = 0; i < h; i++) {           \
        for (int j = 0; j < w; j++) {       \
            ref_ptr_data[j] = ref_ptr[j] * factor - 128.f;   \
            main_ptr_data[j] = main_ptr[j] * factor - 128.f; \
        }                                   \
        ref_ptr += ref_stride / sizeof(type);   \
        ref_ptr_data += w;                      \
        main_ptr += main_stride / sizeof(type); \
        main_ptr_data += w;                     \
    } \
}

offset_fn(uint8_t, 8)
offset_fn(uint16_t, 16)

static void set_meta(AVDictionary **metadata, const char *key, float d)
{
    char value[257];
    snprintf(value, sizeof(value), "%f", d);
    av_dict_set(metadata, key, value, 0);
}

static AVFrame *do_vif(AVFilterContext *ctx, AVFrame *main, const AVFrame *ref)
{
    VIFContext *s = ctx->priv;
    AVDictionary **metadata = &main->metadata;
    float score[4];

    s->factor = 1.f / (1 << (s->desc->comp[0].depth - 8));
    if (s->desc->comp[0].depth <= 8) {
        offset_8bit(s, ref, main, s->width);
    } else {
        offset_16bit(s, ref, main, s->width);
    }

    ff_compute_vif2(ctx,
                    s->ref_data, s->main_data, s->width,
                    s->height, s->width, s->width,
                    score, s->data_buf, s->temp,
                    s->nb_threads);

    set_meta(metadata, "lavfi.vif.scale.0", score[0]);
    set_meta(metadata, "lavfi.vif.scale.1", score[1]);
    set_meta(metadata, "lavfi.vif.scale.2", score[2]);
    set_meta(metadata, "lavfi.vif.scale.3", score[3]);

    for (int i = 0; i < 4; i++) {
        s->vif_min[i]  = FFMIN(s->vif_min[i], score[i]);
        s->vif_max[i]  = FFMAX(s->vif_max[i], score[i]);
        s->vif_sum[i] += score[i];
    }

    s->nb_frames++;

    return main;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10,
        AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
#define PF(suf) AV_PIX_FMT_YUV420##suf,  AV_PIX_FMT_YUV422##suf,  AV_PIX_FMT_YUV444##suf
        PF(P9), PF(P10), PF(P12), PF(P14), PF(P16),
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input_ref(AVFilterLink *inlink)
{
    AVFilterContext *ctx  = inlink->dst;
    VIFContext *s = ctx->priv;

    if (ctx->inputs[0]->w != ctx->inputs[1]->w ||
        ctx->inputs[0]->h != ctx->inputs[1]->h) {
        av_log(ctx, AV_LOG_ERROR, "Width and height of input videos must be same.\n");
        return AVERROR(EINVAL);
    }
    if (ctx->inputs[0]->format != ctx->inputs[1]->format) {
        av_log(ctx, AV_LOG_ERROR, "Inputs must be of same pixel format.\n");
        return AVERROR(EINVAL);
    }

    s->desc = av_pix_fmt_desc_get(inlink->format);
    s->width = ctx->inputs[0]->w;
    s->height = ctx->inputs[0]->h;
    s->nb_threads = ff_filter_get_nb_threads(ctx);

    for (int i = 0; i < 4; i++) {
        s->vif_min[i] =  DBL_MAX;
        s->vif_max[i] = -DBL_MAX;
    }

    for (int i = 0; i < 13; i++) {
        if (!(s->data_buf[i] = av_calloc(s->width, s->height * sizeof(float))))
            return AVERROR(ENOMEM);
    }

    if (!(s->ref_data = av_calloc(s->width, s->height * sizeof(float))))
        return AVERROR(ENOMEM);

    if (!(s->main_data = av_calloc(s->width, s->height * sizeof(float))))
        return AVERROR(ENOMEM);

    if (!(s->temp = av_calloc(s->nb_threads, sizeof(s->temp[0]))))
        return AVERROR(ENOMEM);

    for (int i = 0; i < s->nb_threads; i++) {
        if (!(s->temp[i] = av_calloc(s->width, sizeof(float))))
            return AVERROR(ENOMEM);
    }

    return 0;
}

static int process_frame(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    VIFContext *s = fs->opaque;
    AVFilterLink *outlink = ctx->outputs[0];
    AVFrame *out_frame, *main_frame = NULL, *ref_frame = NULL;
    int ret;

    ret = ff_framesync_dualinput_get(fs, &main_frame, &ref_frame);
    if (ret < 0)
        return ret;

    if (ctx->is_disabled || !ref_frame) {
        out_frame = main_frame;
    } else {
        out_frame = do_vif(ctx, main_frame, ref_frame);
    }

    out_frame->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);

    return ff_filter_frame(outlink, out_frame);
}


static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    VIFContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    FFFrameSyncIn *in;
    int ret;

    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    outlink->frame_rate = mainlink->frame_rate;
    if ((ret = ff_framesync_init(&s->fs, ctx, 2)) < 0)
        return ret;

    in = s->fs.in;
    in[0].time_base = mainlink->time_base;
    in[1].time_base = ctx->inputs[1]->time_base;
    in[0].sync   = 2;
    in[0].before = EXT_STOP;
    in[0].after  = EXT_STOP;
    in[1].sync   = 1;
    in[1].before = EXT_STOP;
    in[1].after  = EXT_STOP;
    s->fs.opaque   = s;
    s->fs.on_event = process_frame;

    return ff_framesync_configure(&s->fs);
}

static int activate(AVFilterContext *ctx)
{
    VIFContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    VIFContext *s = ctx->priv;

    if (s->nb_frames > 0) {
        for (int i = 0; i < 4; i++)
            av_log(ctx, AV_LOG_INFO, "VIF scale=%d average:%f min:%f: max:%f\n",
                   i, s->vif_sum[i] / s->nb_frames, s->vif_min[i], s->vif_max[i]);
    }

    for (int i = 0; i < 13; i++)
        av_freep(&s->data_buf[i]);

    av_freep(&s->ref_data);
    av_freep(&s->main_data);

    for (int i = 0; i < s->nb_threads && s->temp; i++)
        av_freep(&s->temp[i]);

    av_freep(&s->temp);

    ff_framesync_uninit(&s->fs);
}

static const AVFilterPad vif_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
    },{
        .name         = "reference",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_ref,
    },
    { NULL }
};

static const AVFilterPad vif_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_vif = {
    .name          = "vif",
    .description   = NULL_IF_CONFIG_SMALL("Calculate the VIF between two video streams."),
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(VIFContext),
    .priv_class    = &vif_class,
    .activate      = activate,
    .inputs        = vif_inputs,
    .outputs       = vif_outputs,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL | AVFILTER_FLAG_SLICE_THREADS,
};
