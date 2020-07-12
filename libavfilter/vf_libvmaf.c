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
 * Calculate the VMAF between two input videos.
 */

#include <pthread.h>
#include <libvmaf.h>
#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "video.h"

typedef struct LIBVMAFContext {
    const AVClass *class;
    FFFrameSync fs;
    const AVPixFmtDescriptor *desc;
    int width;
    int height;
    double vmaf_score;
    int vmaf_thread_created;
    pthread_t vmaf_thread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    int eof;
    AVFrame *gmain;
    AVFrame *gref;
    int frame_set;
    char *model_path;
    char *log_path;
    char *log_fmt;
    int disable_clip;
    int disable_avx;
    int enable_transform;
    int phone_model;
    int psnr;
    int ssim;
    int ms_ssim;
    char *pool;
    int n_threads;
    int n_subsample;
    int enable_conf_interval;
    int error;
} LIBVMAFContext;

#define OFFSET(x) offsetof(LIBVMAFContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption libvmaf_options[] = {
    {"model_path",  "Set the model to be used for computing vmaf.",                     OFFSET(model_path), AV_OPT_TYPE_STRING, {.str="/usr/local/share/model/vmaf_v0.6.1.pkl"}, 0, 1, FLAGS},
    {"log_path",  "Set the file path to be used to store logs.",                        OFFSET(log_path), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 1, FLAGS},
    {"log_fmt",  "Set the format of the log (xml or json).",                            OFFSET(log_fmt), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 1, FLAGS},
    {"enable_transform",  "Enables transform for computing vmaf.",                      OFFSET(enable_transform), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"phone_model",  "Invokes the phone model that will generate higher VMAF scores.",  OFFSET(phone_model), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"psnr",  "Enables computing psnr along with vmaf.",                                OFFSET(psnr), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"ssim",  "Enables computing ssim along with vmaf.",                                OFFSET(ssim), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"ms_ssim",  "Enables computing ms-ssim along with vmaf.",                          OFFSET(ms_ssim), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    {"pool",  "Set the pool method to be used for computing vmaf.",                     OFFSET(pool), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 1, FLAGS},
    {"n_threads", "Set number of threads to be used when computing vmaf.",              OFFSET(n_threads), AV_OPT_TYPE_INT, {.i64=0}, 0, UINT_MAX, FLAGS},
    {"n_subsample", "Set interval for frame subsampling used when computing vmaf.",     OFFSET(n_subsample), AV_OPT_TYPE_INT, {.i64=1}, 1, UINT_MAX, FLAGS},
    {"enable_conf_interval",  "Enables confidence interval.",                           OFFSET(enable_conf_interval), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(libvmaf, LIBVMAFContext, fs);

#define read_frame_fn(type, bits)                                               \
    static int read_frame_##bits##bit(float *ref_data, float *main_data,        \
                                      float *temp_data, int stride, void *ctx)  \
{                                                                               \
    LIBVMAFContext *s = (LIBVMAFContext *) ctx;                                 \
    int ret;                                                                    \
    \
    pthread_mutex_lock(&s->lock);                                               \
    \
    while (!s->frame_set && !s->eof) {                                          \
        pthread_cond_wait(&s->cond, &s->lock);                                  \
    }                                                                           \
    \
    if (s->frame_set) {                                                         \
        int ref_stride = s->gref->linesize[0];                                  \
        int main_stride = s->gmain->linesize[0];                                \
        \
        const type *ref_ptr = (const type *) s->gref->data[0];                  \
        const type *main_ptr = (const type *) s->gmain->data[0];                \
        \
        float *ptr = ref_data;                                                  \
        float factor = 1.f / (1 << (bits - 8));                                 \
        \
        int h = s->height;                                                      \
        int w = s->width;                                                       \
        \
        int i,j;                                                                \
        \
        for (i = 0; i < h; i++) {                                               \
            for ( j = 0; j < w; j++) {                                          \
                ptr[j] = ref_ptr[j] * factor;                                   \
            }                                                                   \
            ref_ptr += ref_stride / sizeof(*ref_ptr);                           \
            ptr += stride / sizeof(*ptr);                                       \
        }                                                                       \
        \
        ptr = main_data;                                                        \
        \
        for (i = 0; i < h; i++) {                                               \
            for (j = 0; j < w; j++) {                                           \
                ptr[j] = main_ptr[j] * factor;                                  \
            }                                                                   \
            main_ptr += main_stride / sizeof(*main_ptr);                        \
            ptr += stride / sizeof(*ptr);                                       \
        }                                                                       \
    }                                                                           \
    \
    ret = !s->frame_set;                                                        \
    \
    av_frame_unref(s->gref);                                                    \
    av_frame_unref(s->gmain);                                                   \
    s->frame_set = 0;                                                           \
    \
    pthread_cond_signal(&s->cond);                                              \
    pthread_mutex_unlock(&s->lock);                                             \
    \
    if (ret) {                                                                  \
        return 2;                                                               \
    }                                                                           \
    \
    return 0;                                                                   \
}

read_frame_fn(uint8_t, 8);
read_frame_fn(uint16_t, 10);

static void compute_vmaf_score(LIBVMAFContext *s)
{
    int (*read_frame)(float *ref_data, float *main_data, float *temp_data,
                      int stride, void *ctx);
    char *format;

    if (s->desc->comp[0].depth <= 8) {
        read_frame = read_frame_8bit;
    } else {
        read_frame = read_frame_10bit;
    }

    format = (char *) s->desc->name;

    s->error = compute_vmaf(&s->vmaf_score, format, s->width, s->height,
                            read_frame, s, s->model_path, s->log_path,
                            s->log_fmt, 0, 0, s->enable_transform,
                            s->phone_model, s->psnr, s->ssim,
                            s->ms_ssim, s->pool,
                            s->n_threads, s->n_subsample, s->enable_conf_interval);
}

static void *call_vmaf(void *ctx)
{
    LIBVMAFContext *s = (LIBVMAFContext *) ctx;
    compute_vmaf_score(s);
    if (!s->error) {
        av_log(ctx, AV_LOG_INFO, "VMAF score: %f\n",s->vmaf_score);
    } else {
        pthread_mutex_lock(&s->lock);
        pthread_cond_signal(&s->cond);
        pthread_mutex_unlock(&s->lock);
    }
    pthread_exit(NULL);
    return NULL;
}

static int do_vmaf(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    LIBVMAFContext *s = ctx->priv;
    AVFrame *master, *ref;
    int ret;

    ret = ff_framesync_dualinput_get(fs, &master, &ref);
    if (ret < 0)
        return ret;
    if (!ref)
        return ff_filter_frame(ctx->outputs[0], master);

    pthread_mutex_lock(&s->lock);

    while (s->frame_set && !s->error) {
        pthread_cond_wait(&s->cond, &s->lock);
    }

    if (s->error) {
        av_log(ctx, AV_LOG_ERROR,
               "libvmaf encountered an error, check log for details\n");
        pthread_mutex_unlock(&s->lock);
        return AVERROR(EINVAL);
    }

    av_frame_ref(s->gref, ref);
    av_frame_ref(s->gmain, master);

    s->frame_set = 1;

    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);

    return ff_filter_frame(ctx->outputs[0], master);
}

static av_cold int init(AVFilterContext *ctx)
{
    LIBVMAFContext *s = ctx->priv;

    s->gref = av_frame_alloc();
    s->gmain = av_frame_alloc();
    if (!s->gref || !s->gmain)
        return AVERROR(ENOMEM);

    s->error = 0;

    s->vmaf_thread_created = 0;
    pthread_mutex_init(&s->lock, NULL);
    pthread_cond_init (&s->cond, NULL);

    s->fs.on_event = do_vmaf;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P,
        AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_YUV420P10LE,
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
    LIBVMAFContext *s = ctx->priv;
    int th;

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

    th = pthread_create(&s->vmaf_thread, NULL, call_vmaf, (void *) s);
    if (th) {
        av_log(ctx, AV_LOG_ERROR, "Thread creation failed.\n");
        return AVERROR(EINVAL);
    }
    s->vmaf_thread_created = 1;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LIBVMAFContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    int ret;

    ret = ff_framesync_init_dualinput(&s->fs, ctx);
    if (ret < 0)
        return ret;
    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    outlink->frame_rate = mainlink->frame_rate;
    if ((ret = ff_framesync_configure(&s->fs)) < 0)
        return ret;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    LIBVMAFContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LIBVMAFContext *s = ctx->priv;

    ff_framesync_uninit(&s->fs);

    pthread_mutex_lock(&s->lock);
    s->eof = 1;
    pthread_cond_signal(&s->cond);
    pthread_mutex_unlock(&s->lock);

    if (s->vmaf_thread_created)
    {
        pthread_join(s->vmaf_thread, NULL);
        s->vmaf_thread_created = 0;
    }

    av_frame_free(&s->gref);
    av_frame_free(&s->gmain);

    pthread_mutex_destroy(&s->lock);
    pthread_cond_destroy(&s->cond);
}

static const AVFilterPad libvmaf_inputs[] = {
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

static const AVFilterPad libvmaf_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_libvmaf = {
    .name          = "libvmaf",
    .description   = NULL_IF_CONFIG_SMALL("Calculate the VMAF between two video streams."),
    .preinit       = libvmaf_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .priv_size     = sizeof(LIBVMAFContext),
    .priv_class    = &libvmaf_class,
    .inputs        = libvmaf_inputs,
    .outputs       = libvmaf_outputs,
};
