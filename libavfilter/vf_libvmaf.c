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

#include "config_components.h"

#include <libvmaf.h>

#include "libavutil/avstring.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "formats.h"
#include "framesync.h"
#include "video.h"

#if CONFIG_LIBVMAF_CUDA_FILTER
#include <libvmaf_cuda.h>

#include "libavutil/hwcontext.h"
#include "libavutil/hwcontext_cuda_internal.h"
#endif

typedef struct LIBVMAFContext {
    const AVClass *class;
    FFFrameSync fs;
    char *log_path;
    char *log_fmt;
    char *pool;
    int n_threads;
    int n_subsample;
    char *model_cfg;
    char *feature_cfg;
    VmafContext *vmaf;
    VmafModel **model;
    unsigned model_cnt;
    unsigned frame_cnt;
    unsigned bpc;
#if CONFIG_LIBVMAF_CUDA_FILTER
    VmafCudaState *cu_state;
#endif
} LIBVMAFContext;

#define OFFSET(x) offsetof(LIBVMAFContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption libvmaf_options[] = {
    {"log_path",  "Set the file path to be used to write log.",                         OFFSET(log_path), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 1, FLAGS},
    {"log_fmt",  "Set the format of the log (csv, json, xml, or sub).",                 OFFSET(log_fmt), AV_OPT_TYPE_STRING, {.str="xml"}, 0, 1, FLAGS},
    {"pool",  "Set the pool method to be used for computing vmaf.",                     OFFSET(pool), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 1, FLAGS},
    {"n_threads", "Set number of threads to be used when computing vmaf.",              OFFSET(n_threads), AV_OPT_TYPE_INT, {.i64=0}, 0, UINT_MAX, FLAGS},
    {"n_subsample", "Set interval for frame subsampling used when computing vmaf.",     OFFSET(n_subsample), AV_OPT_TYPE_INT, {.i64=1}, 1, UINT_MAX, FLAGS},
    {"model",  "Set the model to be used for computing vmaf.",                          OFFSET(model_cfg), AV_OPT_TYPE_STRING, {.str="version=vmaf_v0.6.1"}, 0, 1, FLAGS},
    {"feature",  "Set the feature to be used for computing vmaf.",                      OFFSET(feature_cfg), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 1, FLAGS},
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(libvmaf, LIBVMAFContext, fs);

static enum VmafPixelFormat pix_fmt_map(enum AVPixelFormat av_pix_fmt)
{
    switch (av_pix_fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV420P10LE:
    case AV_PIX_FMT_YUV420P12LE:
    case AV_PIX_FMT_YUV420P16LE:
        return VMAF_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV422P10LE:
    case AV_PIX_FMT_YUV422P12LE:
    case AV_PIX_FMT_YUV422P16LE:
        return VMAF_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUV444P10LE:
    case AV_PIX_FMT_YUV444P12LE:
    case AV_PIX_FMT_YUV444P16LE:
        return VMAF_PIX_FMT_YUV444P;
    default:
        return VMAF_PIX_FMT_UNKNOWN;
    }
}

static int copy_picture_data(AVFrame *src, VmafPicture *dst, unsigned bpc)
{
    const int bytes_per_value = bpc > 8 ? 2 : 1;
    int err = vmaf_picture_alloc(dst, pix_fmt_map(src->format), bpc,
                                 src->width, src->height);
    if (err)
        return AVERROR(ENOMEM);

    for (unsigned i = 0; i < 3; i++) {
        uint8_t *src_data = src->data[i];
        uint8_t *dst_data = dst->data[i];
        for (unsigned j = 0; j < dst->h[i]; j++) {
            memcpy(dst_data, src_data, bytes_per_value * dst->w[i]);
            src_data += src->linesize[i];
            dst_data += dst->stride[i];
        }
    }

    return 0;
}

static int do_vmaf(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    LIBVMAFContext *s = ctx->priv;
    VmafPicture pic_ref, pic_dist;
    AVFrame *ref, *dist;
    int err = 0;

    int ret = ff_framesync_dualinput_get(fs, &dist, &ref);
    if (ret < 0)
        return ret;
    if (ctx->is_disabled || !ref)
        return ff_filter_frame(ctx->outputs[0], dist);

    if (dist->color_range != ref->color_range) {
        av_log(ctx, AV_LOG_WARNING, "distorted and reference "
               "frames use different color ranges (%s != %s)\n",
               av_color_range_name(dist->color_range),
               av_color_range_name(ref->color_range));
    }

    err = copy_picture_data(ref, &pic_ref, s->bpc);
    if (err) {
        av_log(s, AV_LOG_ERROR, "problem during vmaf_picture_alloc.\n");
        return AVERROR(ENOMEM);
    }

    err = copy_picture_data(dist, &pic_dist, s->bpc);
    if (err) {
        av_log(s, AV_LOG_ERROR, "problem during vmaf_picture_alloc.\n");
        vmaf_picture_unref(&pic_ref);
        return AVERROR(ENOMEM);
    }

    err = vmaf_read_pictures(s->vmaf, &pic_ref, &pic_dist, s->frame_cnt++);
    if (err) {
        av_log(s, AV_LOG_ERROR, "problem during vmaf_read_pictures.\n");
        return AVERROR(EINVAL);
    }

    return ff_filter_frame(ctx->outputs[0], dist);
}

static AVDictionary **delimited_dict_parse(char *str, unsigned *cnt)
{
    AVDictionary **dict = NULL;
    char *str_copy = NULL;
    char *saveptr = NULL;
    unsigned cnt2;
    int err = 0;

    if (!str)
        return NULL;

    cnt2 = 1;
    for (char *p = str; *p; p++) {
        if (*p == '|')
            cnt2++;
    }

    dict = av_calloc(cnt2, sizeof(*dict));
    if (!dict)
        goto fail;

    str_copy = av_strdup(str);
    if (!str_copy)
        goto fail;

    *cnt = 0;
    for (unsigned i = 0; i < cnt2; i++) {
        char *s = av_strtok(i == 0 ? str_copy : NULL, "|", &saveptr);
        if (!s)
            continue;
        err = av_dict_parse_string(&dict[(*cnt)++], s, "=", ":", 0);
        if (err)
            goto fail;
    }

    av_free(str_copy);
    return dict;

fail:
    if (dict) {
        for (unsigned i = 0; i < *cnt; i++) {
            if (dict[i])
                av_dict_free(&dict[i]);
        }
        av_free(dict);
    }

    av_free(str_copy);
    *cnt = 0;
    return NULL;
}

static int parse_features(AVFilterContext *ctx)
{
    LIBVMAFContext *s = ctx->priv;
    AVDictionary **dict = NULL;
    unsigned dict_cnt;
    int err = 0;

    if (!s->feature_cfg)
        return 0;

    dict = delimited_dict_parse(s->feature_cfg, &dict_cnt);
    if (!dict) {
        av_log(ctx, AV_LOG_ERROR,
               "could not parse feature config: %s\n", s->feature_cfg);
        return AVERROR(EINVAL);
    }

    for (unsigned i = 0; i < dict_cnt; i++) {
        char *feature_name = NULL;
        VmafFeatureDictionary *feature_opts_dict = NULL;
        const AVDictionaryEntry *e = NULL;

        while (e = av_dict_iterate(dict[i], e)) {
            if (!strcmp(e->key, "name")) {
                feature_name = e->value;
                continue;
            }

            err = vmaf_feature_dictionary_set(&feature_opts_dict, e->key,
                                              e->value);
            if (err) {
                av_log(ctx, AV_LOG_ERROR,
                       "could not set feature option: %s.%s=%s\n",
                       feature_name, e->key, e->value);
                goto exit;
            }
        }

        err = vmaf_use_feature(s->vmaf, feature_name, feature_opts_dict);
        if (err) {
            av_log(ctx, AV_LOG_ERROR,
                   "problem during vmaf_use_feature: %s\n", feature_name);
            goto exit;
        }
    }

exit:
    for (unsigned i = 0; i < dict_cnt; i++) {
        if (dict[i])
            av_dict_free(&dict[i]);
    }
    av_free(dict);
    return err;
}

static int parse_models(AVFilterContext *ctx)
{
    LIBVMAFContext *s = ctx->priv;
    AVDictionary **dict;
    unsigned dict_cnt;
    int err = 0;

    if (!s->model_cfg) return 0;

    dict_cnt = 0;
    dict = delimited_dict_parse(s->model_cfg, &dict_cnt);
    if (!dict) {
        av_log(ctx, AV_LOG_ERROR,
               "could not parse model config: %s\n", s->model_cfg);
        return AVERROR(EINVAL);
    }

    s->model_cnt = dict_cnt;
    s->model = av_calloc(s->model_cnt, sizeof(*s->model));
    if (!s->model)
        return AVERROR(ENOMEM);

    for (unsigned i = 0; i < dict_cnt; i++) {
        VmafModelConfig model_cfg = { 0 };
        const AVDictionaryEntry *e = NULL;
        char *version = NULL;
        char  *path = NULL;

        while (e = av_dict_iterate(dict[i], e)) {
            if (!strcmp(e->key, "disable_clip")) {
                model_cfg.flags |= !strcmp(e->value, "true") ?
                    VMAF_MODEL_FLAG_DISABLE_CLIP : 0;
                continue;
            }

            if (!strcmp(e->key, "enable_transform")) {
                model_cfg.flags |= !strcmp(e->value, "true") ?
                    VMAF_MODEL_FLAG_ENABLE_TRANSFORM : 0;
                continue;
            }

            if (!strcmp(e->key, "name")) {
                model_cfg.name = e->value;
                continue;
            }

            if (!strcmp(e->key, "version")) {
                version = e->value;
                continue;
            }

            if (!strcmp(e->key, "path")) {
                path = e->value;
                continue;
            }
        }

        if (version) {
            err = vmaf_model_load(&s->model[i], &model_cfg, version);
            if (err) {
                av_log(ctx, AV_LOG_ERROR,
                       "could not load libvmaf model with version: %s\n",
                       version);
                goto exit;
            }
        }

        if (path && !s->model[i]) {
            err = vmaf_model_load_from_path(&s->model[i], &model_cfg, path);
            if (err) {
                av_log(ctx, AV_LOG_ERROR,
                       "could not load libvmaf model with path: %s\n",
                       path);
                goto exit;
            }
        }

        if (!s->model[i]) {
            av_log(ctx, AV_LOG_ERROR,
                   "could not load libvmaf model with config: %s\n",
                   s->model_cfg);
            goto exit;
        }

        while (e = av_dict_iterate(dict[i], e)) {
            VmafFeatureDictionary *feature_opts_dict = NULL;
            char *feature_opt = NULL;

            char *feature_name = av_strtok(e->key, ".", &feature_opt);
            if (!feature_opt)
                continue;

            err = vmaf_feature_dictionary_set(&feature_opts_dict,
                                              feature_opt, e->value);
            if (err) {
                av_log(ctx, AV_LOG_ERROR,
                       "could not set feature option: %s.%s=%s\n",
                       feature_name, feature_opt, e->value);
                err = AVERROR(EINVAL);
                goto exit;
            }

            err = vmaf_model_feature_overload(s->model[i], feature_name,
                                              feature_opts_dict);
            if (err) {
                av_log(ctx, AV_LOG_ERROR,
                       "could not overload feature: %s\n", feature_name);
                err = AVERROR(EINVAL);
                goto exit;
            }
        }
    }

    for (unsigned i = 0; i < s->model_cnt; i++) {
        err = vmaf_use_features_from_model(s->vmaf, s->model[i]);
        if (err) {
            av_log(ctx, AV_LOG_ERROR,
                   "problem during vmaf_use_features_from_model\n");
            err = AVERROR(EINVAL);
            goto exit;
        }
    }

exit:
    for (unsigned i = 0; i < dict_cnt; i++) {
        if (dict[i])
            av_dict_free(&dict[i]);
    }
    av_free(dict);
    return err;
}

static enum VmafLogLevel log_level_map(int log_level)
{
    switch (log_level) {
    case AV_LOG_QUIET:
        return VMAF_LOG_LEVEL_NONE;
    case AV_LOG_ERROR:
        return VMAF_LOG_LEVEL_ERROR;
    case AV_LOG_WARNING:
        return VMAF_LOG_LEVEL_WARNING;
    case AV_LOG_INFO:
        return VMAF_LOG_LEVEL_INFO;
    case AV_LOG_DEBUG:
        return VMAF_LOG_LEVEL_DEBUG;
    default:
        return VMAF_LOG_LEVEL_INFO;
    }
}

static av_cold int init(AVFilterContext *ctx)
{
    LIBVMAFContext *s = ctx->priv;
    int err = 0;

    VmafConfiguration cfg = {
        .log_level = log_level_map(av_log_get_level()),
        .n_subsample = s->n_subsample,
        .n_threads = s->n_threads,
    };

    err = vmaf_init(&s->vmaf, cfg);
    if (err)
        return AVERROR(EINVAL);

    err = parse_models(ctx);
    if (err)
        return err;

    err = parse_features(ctx);
    if (err)
        return err;

    s->fs.on_event = do_vmaf;
    return 0;
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_YUV422P10LE, AV_PIX_FMT_YUV420P10LE,
    AV_PIX_FMT_YUV444P12LE, AV_PIX_FMT_YUV422P12LE, AV_PIX_FMT_YUV420P12LE,
    AV_PIX_FMT_YUV444P16LE, AV_PIX_FMT_YUV422P16LE, AV_PIX_FMT_YUV420P16LE,
    AV_PIX_FMT_NONE
};

static int config_input_ref(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LIBVMAFContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc;
    int err = 0;

    if (ctx->inputs[0]->w != ctx->inputs[1]->w) {
        av_log(ctx, AV_LOG_ERROR, "input width must match.\n");
        err |= AVERROR(EINVAL);
    }

    if (ctx->inputs[0]->h != ctx->inputs[1]->h) {
        av_log(ctx, AV_LOG_ERROR, "input height must match.\n");
        err |= AVERROR(EINVAL);
    }

    if (ctx->inputs[0]->format != ctx->inputs[1]->format) {
        av_log(ctx, AV_LOG_ERROR, "input pix_fmt must match.\n");
        err |= AVERROR(EINVAL);
    }

    if (err)
        return err;

    desc = av_pix_fmt_desc_get(inlink->format);
    s->bpc = desc->comp[0].depth;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    LIBVMAFContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    FilterLink *il = ff_filter_link(mainlink);
    FilterLink *ol = ff_filter_link(outlink);
    int ret;

    ret = ff_framesync_init_dualinput(&s->fs, ctx);
    if (ret < 0)
        return ret;
    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    ol->frame_rate = il->frame_rate;
    if ((ret = ff_framesync_configure(&s->fs)) < 0)
        return ret;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    LIBVMAFContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static enum VmafOutputFormat log_fmt_map(const char *log_fmt)
{
    if (log_fmt) {
        if (!strcmp(log_fmt, "xml"))
            return VMAF_OUTPUT_FORMAT_XML;
        if (!strcmp(log_fmt, "json"))
            return VMAF_OUTPUT_FORMAT_JSON;
        if (!strcmp(log_fmt, "csv"))
            return VMAF_OUTPUT_FORMAT_CSV;
        if (!strcmp(log_fmt, "sub"))
            return VMAF_OUTPUT_FORMAT_SUB;
    }

    return VMAF_OUTPUT_FORMAT_XML;
}

static enum VmafPoolingMethod pool_method_map(const char *pool_method)
{
    if (pool_method) {
        if (!strcmp(pool_method, "min"))
            return VMAF_POOL_METHOD_MIN;
        if (!strcmp(pool_method, "mean"))
            return VMAF_POOL_METHOD_MEAN;
        if (!strcmp(pool_method, "harmonic_mean"))
            return VMAF_POOL_METHOD_HARMONIC_MEAN;
    }

    return VMAF_POOL_METHOD_MEAN;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LIBVMAFContext *s = ctx->priv;
    int err = 0;

    ff_framesync_uninit(&s->fs);

    if (!s->frame_cnt)
        goto clean_up;

    err = vmaf_read_pictures(s->vmaf, NULL, NULL, 0);
    if (err) {
        av_log(ctx, AV_LOG_ERROR,
               "problem flushing libvmaf context.\n");
    }

    for (unsigned i = 0; i < s->model_cnt; i++) {
        double vmaf_score;
        err = vmaf_score_pooled(s->vmaf, s->model[i], pool_method_map(s->pool),
                                &vmaf_score, 0, s->frame_cnt - 1);
        if (err) {
            av_log(ctx, AV_LOG_ERROR,
                   "problem getting pooled vmaf score.\n");
        }

        av_log(ctx, AV_LOG_INFO, "VMAF score: %f\n", vmaf_score);
    }

    if (s->vmaf) {
        if (s->log_path && !err)
            vmaf_write_output(s->vmaf, s->log_path, log_fmt_map(s->log_fmt));
    }

clean_up:
    if (s->model) {
        for (unsigned i = 0; i < s->model_cnt; i++) {
            if (s->model[i])
                vmaf_model_destroy(s->model[i]);
        }
        av_free(s->model);
    }

    if (s->vmaf)
        vmaf_close(s->vmaf);
}

static const AVFilterPad libvmaf_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
    },
    {
        .name         = "reference",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_ref,
    },
};

static const AVFilterPad libvmaf_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const AVFilter ff_vf_libvmaf = {
    .name          = "libvmaf",
    .description   = NULL_IF_CONFIG_SMALL("Calculate the VMAF between two video streams."),
    .preinit       = libvmaf_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    .priv_size     = sizeof(LIBVMAFContext),
    .priv_class    = &libvmaf_class,
    FILTER_INPUTS(libvmaf_inputs),
    FILTER_OUTPUTS(libvmaf_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
};

#if CONFIG_LIBVMAF_CUDA_FILTER
static const enum AVPixelFormat supported_formats[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUV444P16,
};

static int format_is_supported(enum AVPixelFormat fmt)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(supported_formats); i++)
        if (supported_formats[i] == fmt)
            return 1;
    return 0;
}

static int config_props_cuda(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *ctx = outlink->src;
    LIBVMAFContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    AVHWFramesContext *frames_ctx = (AVHWFramesContext*) inl->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = frames_ctx->device_ctx->hwctx;
    CUcontext cu_ctx = device_hwctx->cuda_ctx;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(frames_ctx->sw_format);

    VmafConfiguration cfg = {
        .log_level = log_level_map(av_log_get_level()),
        .n_subsample = s->n_subsample,
        .n_threads = s->n_threads,
    };

    VmafCudaPictureConfiguration cuda_pic_cfg = {
        .pic_params = {
            .bpc = desc->comp[0].depth,
            .w = inlink->w,
            .h = inlink->h,
            .pix_fmt = pix_fmt_map(frames_ctx->sw_format),
        },
        .pic_prealloc_method = VMAF_CUDA_PICTURE_PREALLOCATION_METHOD_DEVICE,
    };

    VmafCudaConfiguration cuda_cfg = {
        .cu_ctx = cu_ctx,
    };

    if (!format_is_supported(frames_ctx->sw_format)) {
        av_log(s, AV_LOG_ERROR,
               "Unsupported input format: %s\n", desc->name);
        return AVERROR(EINVAL);
    }

    err = vmaf_init(&s->vmaf, cfg);
    if (err)
        return AVERROR(EINVAL);

    err = vmaf_cuda_state_init(&s->cu_state, cuda_cfg);
    if (err)
        return AVERROR(EINVAL);

    err = vmaf_cuda_import_state(s->vmaf, s->cu_state);
    if (err)
        return AVERROR(EINVAL);

    err = vmaf_cuda_preallocate_pictures(s->vmaf, cuda_pic_cfg);
    if (err < 0)
        return err;

    err = parse_models(ctx);
    if (err)
        return err;

    err = parse_features(ctx);
    if (err)
        return err;

    return config_output(outlink);
}

static int copy_picture_data_cuda(VmafContext* vmaf,
                                  AVCUDADeviceContext* device_hwctx,
                                  AVFrame* src, VmafPicture* dst,
                                  enum AVPixelFormat pix_fmt)
{
    const AVPixFmtDescriptor *pix_desc = av_pix_fmt_desc_get(pix_fmt);
    CudaFunctions *cu = device_hwctx->internal->cuda_dl;

    CUDA_MEMCPY2D m = {
        .srcMemoryType = CU_MEMORYTYPE_DEVICE,
        .dstMemoryType = CU_MEMORYTYPE_DEVICE,
    };

    int err = vmaf_cuda_fetch_preallocated_picture(vmaf, dst);
    if (err)
        return AVERROR(ENOMEM);

    err = cu->cuCtxPushCurrent(device_hwctx->cuda_ctx);
    if (err)
        return AVERROR_EXTERNAL;

    for (unsigned i = 0; i < pix_desc->nb_components; i++) {
        m.srcDevice = (CUdeviceptr) src->data[i];
        m.srcPitch = src->linesize[i];
        m.dstDevice = (CUdeviceptr) dst->data[i];
        m.dstPitch = dst->stride[i];
        m.WidthInBytes = dst->w[i] * ((dst->bpc + 7) / 8);
        m.Height = dst->h[i];

        err = cu->cuMemcpy2D(&m);
        if (err)
            return AVERROR_EXTERNAL;
        break;
    }

    err = cu->cuCtxPopCurrent(NULL);
    if (err)
        return AVERROR_EXTERNAL;

    return 0;
}

static int do_vmaf_cuda(FFFrameSync* fs)
{
    AVFilterContext* ctx = fs->parent;
    LIBVMAFContext* s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    FilterLink      *inl = ff_filter_link(inlink);
    AVHWFramesContext *frames_ctx = (AVHWFramesContext*)inl->hw_frames_ctx->data;
    AVCUDADeviceContext *device_hwctx = frames_ctx->device_ctx->hwctx;
    VmafPicture pic_ref, pic_dist;
    AVFrame *ref, *dist;

    int err = 0;

    err = ff_framesync_dualinput_get(fs, &dist, &ref);
    if (err < 0)
        return err;
    if (ctx->is_disabled || !ref)
        return ff_filter_frame(ctx->outputs[0], dist);

    err = copy_picture_data_cuda(s->vmaf, device_hwctx, ref, &pic_ref,
                                 frames_ctx->sw_format);
    if (err) {
        av_log(s, AV_LOG_ERROR, "problem during copy_picture_data_cuda.\n");
        return AVERROR(ENOMEM);
    }

    err = copy_picture_data_cuda(s->vmaf, device_hwctx, dist, &pic_dist,
                                 frames_ctx->sw_format);
    if (err) {
        av_log(s, AV_LOG_ERROR, "problem during copy_picture_data_cuda.\n");
        return AVERROR(ENOMEM);
    }

    err = vmaf_read_pictures(s->vmaf, &pic_ref, &pic_dist, s->frame_cnt++);
    if (err) {
        av_log(s, AV_LOG_ERROR, "problem during vmaf_read_pictures.\n");
        return AVERROR(EINVAL);
    }

    return ff_filter_frame(ctx->outputs[0], dist);
}

static av_cold int init_cuda(AVFilterContext *ctx)
{
    LIBVMAFContext *s = ctx->priv;
    s->fs.on_event = do_vmaf_cuda;
    return 0;
}

static const AVFilterPad libvmaf_outputs_cuda[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props_cuda,
    },
};

const AVFilter ff_vf_libvmaf_cuda = {
    .name           = "libvmaf_cuda",
    .description    = NULL_IF_CONFIG_SMALL("Calculate the VMAF between two video streams."),
    .preinit        = libvmaf_framesync_preinit,
    .init           = init_cuda,
    .uninit         = uninit,
    .activate       = activate,
    .priv_size      = sizeof(LIBVMAFContext),
    .priv_class     = &libvmaf_class,
    FILTER_INPUTS(libvmaf_inputs),
    FILTER_OUTPUTS(libvmaf_outputs_cuda),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_CUDA),
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
#endif
