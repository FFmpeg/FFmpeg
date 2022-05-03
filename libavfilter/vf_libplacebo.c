/*
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

#include "libavutil/file.h"
#include "libavutil/opt.h"
#include "internal.h"
#include "vulkan_filter.h"
#include "scale_eval.h"

#include <libplacebo/renderer.h>
#include <libplacebo/utils/libav.h>
#include <libplacebo/vulkan.h>

enum {
    TONE_MAP_AUTO,
    TONE_MAP_CLIP,
    TONE_MAP_BT2390,
    TONE_MAP_BT2446A,
    TONE_MAP_SPLINE,
    TONE_MAP_REINHARD,
    TONE_MAP_MOBIUS,
    TONE_MAP_HABLE,
    TONE_MAP_GAMMA,
    TONE_MAP_LINEAR,
    TONE_MAP_COUNT,
};

static const struct pl_tone_map_function * const tonemapping_funcs[TONE_MAP_COUNT] = {
    [TONE_MAP_AUTO]     = &pl_tone_map_auto,
    [TONE_MAP_CLIP]     = &pl_tone_map_clip,
    [TONE_MAP_BT2390]   = &pl_tone_map_bt2390,
    [TONE_MAP_BT2446A]  = &pl_tone_map_bt2446a,
    [TONE_MAP_SPLINE]   = &pl_tone_map_spline,
    [TONE_MAP_REINHARD] = &pl_tone_map_reinhard,
    [TONE_MAP_MOBIUS]   = &pl_tone_map_mobius,
    [TONE_MAP_HABLE]    = &pl_tone_map_hable,
    [TONE_MAP_GAMMA]    = &pl_tone_map_gamma,
    [TONE_MAP_LINEAR]   = &pl_tone_map_linear,
};

typedef struct LibplaceboContext {
    /* lavfi vulkan*/
    FFVulkanContext vkctx;
    int initialized;

    /* libplacebo */
    pl_log log;
    pl_vulkan vulkan;
    pl_gpu gpu;
    pl_renderer renderer;

    /* settings */
    char *out_format_string;
    char *w_expr;
    char *h_expr;
    AVRational target_sar;
    float pad_crop_ratio;
    int force_original_aspect_ratio;
    int force_divisible_by;
    int normalize_sar;
    int apply_filmgrain;
    int apply_dovi;
    int colorspace;
    int color_range;
    int color_primaries;
    int color_trc;

    /* pl_render_params */
    char *upscaler;
    char *downscaler;
    int lut_entries;
    float antiringing;
    int sigmoid;
    int skip_aa;
    float polar_cutoff;
    int disable_linear;
    int disable_builtin;
    int force_icc_lut;
    int force_dither;
    int disable_fbos;

    /* pl_deband_params */
    int deband;
    int deband_iterations;
    float deband_threshold;
    float deband_radius;
    float deband_grain;

    /* pl_color_adjustment */
    float brightness;
    float contrast;
    float saturation;
    float hue;
    float gamma;

    /* pl_peak_detect_params */
    int peakdetect;
    float smoothing;
    float min_peak;
    float scene_low;
    float scene_high;
    float overshoot;

    /* pl_color_map_params */
    int intent;
    int gamut_mode;
    int tonemapping;
    float tonemapping_param;
    int tonemapping_mode;
    int inverse_tonemapping;
    float crosstalk;
    int tonemapping_lut_size;
    /* for backwards compatibility */
    float desat_str;
    float desat_exp;
    int gamut_warning;
    int gamut_clipping;

     /* pl_dither_params */
    int dithering;
    int dither_lut_size;
    int dither_temporal;

    /* pl_cone_params */
    int cones;
    float cone_str;

    /* custom shaders */
    char *shader_path;
    void *shader_bin;
    int shader_bin_len;
    const struct pl_hook *hooks[2];
    int num_hooks;
} LibplaceboContext;

static inline enum pl_log_level get_log_level(void)
{
    int av_lev = av_log_get_level();
    return av_lev >= AV_LOG_TRACE   ? PL_LOG_TRACE :
           av_lev >= AV_LOG_DEBUG   ? PL_LOG_DEBUG :
           av_lev >= AV_LOG_VERBOSE ? PL_LOG_INFO :
           av_lev >= AV_LOG_WARNING ? PL_LOG_WARN :
           av_lev >= AV_LOG_ERROR   ? PL_LOG_ERR :
           av_lev >= AV_LOG_FATAL   ? PL_LOG_FATAL :
                                      PL_LOG_NONE;
}

static void pl_av_log(void *log_ctx, enum pl_log_level level, const char *msg)
{
    int av_lev;

    switch (level) {
    case PL_LOG_FATAL:  av_lev = AV_LOG_FATAL;   break;
    case PL_LOG_ERR:    av_lev = AV_LOG_ERROR;   break;
    case PL_LOG_WARN:   av_lev = AV_LOG_WARNING; break;
    case PL_LOG_INFO:   av_lev = AV_LOG_VERBOSE; break;
    case PL_LOG_DEBUG:  av_lev = AV_LOG_DEBUG;   break;
    case PL_LOG_TRACE:  av_lev = AV_LOG_TRACE;   break;
    default: return;
    }

    av_log(log_ctx, av_lev, "%s\n", msg);
}

static int parse_shader(AVFilterContext *avctx, const void *shader, size_t len)
{
    LibplaceboContext *s = avctx->priv;
    const struct pl_hook *hook;

    hook = pl_mpv_user_shader_parse(s->gpu, shader, len);
    if (!hook) {
        av_log(s, AV_LOG_ERROR, "Failed parsing custom shader!\n");
        return AVERROR(EINVAL);
    }

    s->hooks[s->num_hooks++] = hook;
    return 0;
}

static int find_scaler(AVFilterContext *avctx,
                       const struct pl_filter_config **opt,
                       const char *name)
{
    const struct pl_filter_preset *preset;
    if (!strcmp(name, "help")) {
        av_log(avctx, AV_LOG_INFO, "Available scaler presets:\n");
        for (preset = pl_scale_filters; preset->name; preset++)
            av_log(avctx, AV_LOG_INFO, "    %s\n", preset->name);
        return AVERROR_EXIT;
    }

    for (preset = pl_scale_filters; preset->name; preset++) {
        if (!strcmp(name, preset->name)) {
            *opt = preset->filter;
            return 0;
        }
    }

    av_log(avctx, AV_LOG_ERROR, "No such scaler preset '%s'.\n", name);
    return AVERROR(EINVAL);
}

static int libplacebo_init(AVFilterContext *avctx)
{
    LibplaceboContext *s = avctx->priv;

    /* Create libplacebo log context */
    s->log = pl_log_create(PL_API_VER, pl_log_params(
        .log_level = get_log_level(),
        .log_cb = pl_av_log,
        .log_priv = s,
    ));

    if (!s->log)
        return AVERROR(ENOMEM);

    /* Note: s->vulkan etc. are initialized later, when hwctx is available */
    return 0;
}

static int init_vulkan(AVFilterContext *avctx)
{
    int err = 0;
    LibplaceboContext *s = avctx->priv;
    const AVVulkanDeviceContext *hwctx = s->vkctx.hwctx;
    uint8_t *buf = NULL;
    size_t buf_len;

    /* Import libavfilter vulkan context into libplacebo */
    s->vulkan = pl_vulkan_import(s->log, pl_vulkan_import_params(
        .instance       = hwctx->inst,
        .get_proc_addr  = hwctx->get_proc_addr,
        .phys_device    = hwctx->phys_dev,
        .device         = hwctx->act_dev,
        .extensions     = hwctx->enabled_dev_extensions,
        .num_extensions = hwctx->nb_enabled_dev_extensions,
        .features       = &hwctx->device_features,
        .queue_graphics = {
            .index = hwctx->queue_family_index,
            .count = hwctx->nb_graphics_queues,
        },
        .queue_compute = {
            .index = hwctx->queue_family_comp_index,
            .count = hwctx->nb_comp_queues,
        },
        .queue_transfer = {
            .index = hwctx->queue_family_tx_index,
            .count = hwctx->nb_tx_queues,
        },
        /* This is the highest version created by hwcontext_vulkan.c */
        .max_api_version = VK_API_VERSION_1_2,
    ));

    if (!s->vulkan) {
        av_log(s, AV_LOG_ERROR, "Failed importing vulkan device to libplacebo!\n");
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    /* Create the renderer */
    s->gpu = s->vulkan->gpu;
    s->renderer = pl_renderer_create(s->log, s->gpu);

    /* Parse the user shaders, if requested */
    if (s->shader_bin_len)
        RET(parse_shader(avctx, s->shader_bin, s->shader_bin_len));

    if (s->shader_path && s->shader_path[0]) {
        RET(av_file_map(s->shader_path, &buf, &buf_len, 0, s));
        RET(parse_shader(avctx, buf, buf_len));
    }

    /* fall through */
fail:
    if (buf)
        av_file_unmap(buf, buf_len);
    s->initialized =  1;
    return err;
}

static void libplacebo_uninit(AVFilterContext *avctx)
{
    LibplaceboContext *s = avctx->priv;

    for (int i = 0; i < s->num_hooks; i++)
        pl_mpv_user_shader_destroy(&s->hooks[i]);
    pl_renderer_destroy(&s->renderer);
    pl_vulkan_destroy(&s->vulkan);
    pl_log_destroy(&s->log);
    ff_vk_uninit(&s->vkctx);
    s->initialized = 0;
    s->gpu = NULL;
}

static int process_frames(AVFilterContext *avctx, AVFrame *out, AVFrame *in)
{
    int err = 0, ok;
    LibplaceboContext *s = avctx->priv;
    struct pl_render_params params;
    enum pl_tone_map_mode tonemapping_mode = s->tonemapping_mode;
    enum pl_gamut_mode gamut_mode = s->gamut_mode;
    struct pl_frame image, target;
    ok = pl_map_avframe_ex(s->gpu, &image, pl_avframe_params(
        .frame    = in,
        .map_dovi = s->apply_dovi,
    ));

    ok &= pl_map_avframe_ex(s->gpu, &target, pl_avframe_params(
        .frame    = out,
        .map_dovi = false,
    ));

    if (!ok) {
        err = AVERROR_EXTERNAL;
        goto fail;
    }

    if (!s->apply_filmgrain)
        image.film_grain.type = PL_FILM_GRAIN_NONE;

    if (s->target_sar.num) {
        float aspect = pl_rect2df_aspect(&target.crop) * av_q2d(s->target_sar);
        pl_rect2df_aspect_set(&target.crop, aspect, s->pad_crop_ratio);
    }

    /* backwards compatibility with older API */
    if (!tonemapping_mode && (s->desat_str >= 0.0f || s->desat_exp >= 0.0f)) {
        float str = s->desat_str < 0.0f ? 0.9f : s->desat_str;
        float exp = s->desat_exp < 0.0f ? 0.2f : s->desat_exp;
        if (str >= 0.9f && exp <= 0.1f) {
            tonemapping_mode = PL_TONE_MAP_RGB;
        } else if (str > 0.1f) {
            tonemapping_mode = PL_TONE_MAP_HYBRID;
        } else {
            tonemapping_mode = PL_TONE_MAP_LUMA;
        }
    }

    if (s->gamut_warning)
        gamut_mode = PL_GAMUT_WARN;
    if (s->gamut_clipping)
        gamut_mode = PL_GAMUT_DESATURATE;

    /* Update render params */
    params = (struct pl_render_params) {
        PL_RENDER_DEFAULTS
        .lut_entries = s->lut_entries,
        .antiringing_strength = s->antiringing,

        .deband_params = !s->deband ? NULL : pl_deband_params(
            .iterations = s->deband_iterations,
            .threshold = s->deband_threshold,
            .radius = s->deband_radius,
            .grain = s->deband_grain,
        ),

        .sigmoid_params = s->sigmoid ? &pl_sigmoid_default_params : NULL,

        .color_adjustment = &(struct pl_color_adjustment) {
            .brightness = s->brightness,
            .contrast = s->contrast,
            .saturation = s->saturation,
            .hue = s->hue,
            .gamma = s->gamma,
        },

        .peak_detect_params = !s->peakdetect ? NULL : pl_peak_detect_params(
            .smoothing_period = s->smoothing,
            .minimum_peak = s->min_peak,
            .scene_threshold_low = s->scene_low,
            .scene_threshold_high = s->scene_high,
            .overshoot_margin = s->overshoot,
        ),

        .color_map_params = pl_color_map_params(
            .intent = s->intent,
            .gamut_mode = gamut_mode,
            .tone_mapping_function = tonemapping_funcs[s->tonemapping],
            .tone_mapping_param = s->tonemapping_param,
            .tone_mapping_mode = tonemapping_mode,
            .inverse_tone_mapping = s->inverse_tonemapping,
            .tone_mapping_crosstalk = s->crosstalk,
            .lut_size = s->tonemapping_lut_size,
        ),

        .dither_params = s->dithering < 0 ? NULL : pl_dither_params(
            .method = s->dithering,
            .lut_size = s->dither_lut_size,
            .temporal = s->dither_temporal,
        ),

        .cone_params = !s->cones ? NULL : pl_cone_params(
            .cones = s->cones,
            .strength = s->cone_str,
        ),

        .hooks = s->hooks,
        .num_hooks = s->num_hooks,

        .skip_anti_aliasing = s->skip_aa,
        .polar_cutoff = s->polar_cutoff,
        .disable_linear_scaling = s->disable_linear,
        .disable_builtin_scalers = s->disable_builtin,
        .force_icc_lut = s->force_icc_lut,
        .force_dither = s->force_dither,
        .disable_fbos = s->disable_fbos,
    };

    RET(find_scaler(avctx, &params.upscaler, s->upscaler));
    RET(find_scaler(avctx, &params.downscaler, s->downscaler));

    pl_render_image(s->renderer, &image, &target, &params);
    pl_unmap_avframe(s->gpu, &image);
    pl_unmap_avframe(s->gpu, &target);

    /* Flush the command queues for performance */
    pl_gpu_flush(s->gpu);
    return 0;

fail:
    pl_unmap_avframe(s->gpu, &image);
    pl_unmap_avframe(s->gpu, &target);
    return err;
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    int err, changed_csp;
    AVFilterContext *ctx = link->dst;
    LibplaceboContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];

    AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        err = AVERROR(ENOMEM);
        goto fail;
    }

    pl_log_level_update(s->log, get_log_level());
    if (!s->initialized)
        RET(init_vulkan(ctx));

    RET(av_frame_copy_props(out, in));
    out->width = outlink->w;
    out->height = outlink->h;

    if (s->apply_dovi && av_frame_get_side_data(in, AV_FRAME_DATA_DOVI_METADATA)) {
        /* Output of dovi reshaping is always BT.2020+PQ, so infer the correct
         * output colorspace defaults */
        out->colorspace = AVCOL_SPC_BT2020_NCL;
        out->color_primaries = AVCOL_PRI_BT2020;
        out->color_trc = AVCOL_TRC_SMPTE2084;
    }

    if (s->colorspace >= 0)
        out->colorspace = s->colorspace;
    if (s->color_range >= 0)
        out->color_range = s->color_range;
    if (s->color_trc >= 0)
        out->color_trc = s->color_trc;
    if (s->color_primaries >= 0)
        out->color_primaries = s->color_primaries;

    changed_csp = in->colorspace      != out->colorspace     ||
                  in->color_range     != out->color_range    ||
                  in->color_trc       != out->color_trc      ||
                  in->color_primaries != out->color_primaries;

    /* Strip side data if no longer relevant */
    if (changed_csp) {
        av_frame_remove_side_data(out, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        av_frame_remove_side_data(out, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    }
    if (s->apply_dovi || changed_csp) {
        av_frame_remove_side_data(out, AV_FRAME_DATA_DOVI_RPU_BUFFER);
        av_frame_remove_side_data(out, AV_FRAME_DATA_DOVI_METADATA);
    }
    if (s->apply_filmgrain)
        av_frame_remove_side_data(out, AV_FRAME_DATA_FILM_GRAIN_PARAMS);

    RET(process_frames(ctx, out, in));

    av_frame_free(&in);

    return ff_filter_frame(outlink, out);

fail:
    av_frame_free(&in);
    av_frame_free(&out);
    return err;
}

static int libplacebo_config_output(AVFilterLink *outlink)
{
    int err;
    AVFilterContext *avctx = outlink->src;
    LibplaceboContext *s   = avctx->priv;
    AVFilterLink *inlink   = outlink->src->inputs[0];
    AVHWFramesContext *hwfc;
    AVVulkanFramesContext *vkfc;
    AVRational scale_sar;
    int *out_w = &s->vkctx.output_width;
    int *out_h = &s->vkctx.output_height;

    RET(ff_scale_eval_dimensions(s, s->w_expr, s->h_expr, inlink, outlink,
                                 out_w, out_h));

    ff_scale_adjust_dimensions(inlink, out_w, out_h,
                               s->force_original_aspect_ratio,
                               s->force_divisible_by);

    scale_sar = (AVRational){outlink->h * inlink->w, *out_w * *out_h};
    if (inlink->sample_aspect_ratio.num)
        scale_sar = av_mul_q(scale_sar, inlink->sample_aspect_ratio);

    if (s->normalize_sar) {
        /* Apply all SAR during scaling, so we don't need to set the out SAR */
        s->target_sar = scale_sar;
    } else {
        /* This is consistent with other scale_* filters, which only
         * set the outlink SAR to be equal to the scale SAR iff the input SAR
         * was set to something nonzero */
        if (inlink->sample_aspect_ratio.num)
            outlink->sample_aspect_ratio = scale_sar;
    }

    if (s->out_format_string) {
        s->vkctx.output_format = av_get_pix_fmt(s->out_format_string);
        if (s->vkctx.output_format == AV_PIX_FMT_NONE) {
            av_log(avctx, AV_LOG_ERROR, "Invalid output format.\n");
            return AVERROR(EINVAL);
        }
    } else {
        /* Default to re-using the input format */
        s->vkctx.output_format = s->vkctx.input_format;
    }

    RET(ff_vk_filter_config_output(outlink));
    hwfc = (AVHWFramesContext *) outlink->hw_frames_ctx->data;
    vkfc = hwfc->hwctx;
    vkfc->usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    return 0;

fail:
    return err;
}

#define OFFSET(x) offsetof(LibplaceboContext, x)
#define STATIC (AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)
#define DYNAMIC (STATIC | AV_OPT_FLAG_RUNTIME_PARAM)

static const AVOption libplacebo_options[] = {
    { "w", "Output video width",  OFFSET(w_expr), AV_OPT_TYPE_STRING, {.str = "iw"}, .flags = STATIC },
    { "h", "Output video height", OFFSET(h_expr), AV_OPT_TYPE_STRING, {.str = "ih"}, .flags = STATIC },
    { "format", "Output video format", OFFSET(out_format_string), AV_OPT_TYPE_STRING, .flags = STATIC },
    { "force_original_aspect_ratio", "decrease or increase w/h if necessary to keep the original AR", OFFSET(force_original_aspect_ratio), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, 2, STATIC, "force_oar" },
        { "disable",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0 }, 0, 0, STATIC, "force_oar" },
        { "decrease", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1 }, 0, 0, STATIC, "force_oar" },
        { "increase", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2 }, 0, 0, STATIC, "force_oar" },
    { "force_divisible_by", "enforce that the output resolution is divisible by a defined integer when force_original_aspect_ratio is used", OFFSET(force_divisible_by), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, 256, STATIC },
    { "normalize_sar", "force SAR normalization to 1:1", OFFSET(normalize_sar), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, STATIC },
    { "pad_crop_ratio", "ratio between padding and cropping when normalizing SAR (0=pad, 1=crop)", OFFSET(pad_crop_ratio), AV_OPT_TYPE_FLOAT, {.dbl=0.0}, 0.0, 1.0, DYNAMIC },

    {"colorspace", "select colorspace", OFFSET(colorspace), AV_OPT_TYPE_INT, {.i64=-1}, -1, AVCOL_SPC_NB-1, DYNAMIC, "colorspace"},
    {"auto", "keep the same colorspace",  0, AV_OPT_TYPE_CONST, {.i64=-1},                          INT_MIN, INT_MAX, STATIC, "colorspace"},
    {"gbr",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_RGB},               INT_MIN, INT_MAX, STATIC, "colorspace"},
    {"bt709",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_BT709},             INT_MIN, INT_MAX, STATIC, "colorspace"},
    {"unknown",                    NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_UNSPECIFIED},       INT_MIN, INT_MAX, STATIC, "colorspace"},
    {"bt470bg",                    NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_BT470BG},           INT_MIN, INT_MAX, STATIC, "colorspace"},
    {"smpte170m",                  NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_SMPTE170M},         INT_MIN, INT_MAX, STATIC, "colorspace"},
    {"smpte240m",                  NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_SMPTE240M},         INT_MIN, INT_MAX, STATIC, "colorspace"},
    {"ycgco",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_YCGCO},             INT_MIN, INT_MAX, STATIC, "colorspace"},
    {"bt2020nc",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_BT2020_NCL},        INT_MIN, INT_MAX, STATIC, "colorspace"},
    {"bt2020c",                    NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_BT2020_CL},         INT_MIN, INT_MAX, STATIC, "colorspace"},
    {"ictcp",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_SPC_ICTCP},             INT_MIN, INT_MAX, STATIC, "colorspace"},

    {"range", "select color range", OFFSET(color_range), AV_OPT_TYPE_INT, {.i64=-1}, -1, AVCOL_RANGE_NB-1, DYNAMIC, "range"},
    {"auto",  "keep the same color range",   0, AV_OPT_TYPE_CONST, {.i64=-1},                       0, 0, STATIC, "range"},
    {"unspecified",                  NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_UNSPECIFIED},  0, 0, STATIC, "range"},
    {"unknown",                      NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_UNSPECIFIED},  0, 0, STATIC, "range"},
    {"limited",                      NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_MPEG},         0, 0, STATIC, "range"},
    {"tv",                           NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_MPEG},         0, 0, STATIC, "range"},
    {"mpeg",                         NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_MPEG},         0, 0, STATIC, "range"},
    {"full",                         NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_JPEG},         0, 0, STATIC, "range"},
    {"pc",                           NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_JPEG},         0, 0, STATIC, "range"},
    {"jpeg",                         NULL,   0, AV_OPT_TYPE_CONST, {.i64=AVCOL_RANGE_JPEG},         0, 0, STATIC, "range"},

    {"color_primaries", "select color primaries", OFFSET(color_primaries), AV_OPT_TYPE_INT, {.i64=-1}, -1, AVCOL_PRI_NB-1, DYNAMIC, "color_primaries"},
    {"auto", "keep the same color primaries",  0, AV_OPT_TYPE_CONST, {.i64=-1},                     INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"bt709",                           NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT709},        INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"unknown",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_UNSPECIFIED},  INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"bt470m",                          NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT470M},       INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"bt470bg",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT470BG},      INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"smpte170m",                       NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE170M},    INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"smpte240m",                       NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE240M},    INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"film",                            NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_FILM},         INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"bt2020",                          NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_BT2020},       INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"smpte428",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE428},     INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"smpte431",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE431},     INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"smpte432",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_SMPTE432},     INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"jedec-p22",                       NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_JEDEC_P22},    INT_MIN, INT_MAX, STATIC, "color_primaries"},
    {"ebu3213",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_PRI_EBU3213},      INT_MIN, INT_MAX, STATIC, "color_primaries"},

    {"color_trc", "select color transfer", OFFSET(color_trc), AV_OPT_TYPE_INT, {.i64=-1}, -1, AVCOL_TRC_NB-1, DYNAMIC, "color_trc"},
    {"auto", "keep the same color transfer",  0, AV_OPT_TYPE_CONST, {.i64=-1},                     INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"bt709",                          NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT709},        INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"unknown",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_UNSPECIFIED},  INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"bt470m",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_GAMMA22},      INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"bt470bg",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_GAMMA28},      INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"smpte170m",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_SMPTE170M},    INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"smpte240m",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_SMPTE240M},    INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"linear",                         NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_LINEAR},       INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"iec61966-2-4",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_IEC61966_2_4}, INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"bt1361e",                        NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT1361_ECG},   INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"iec61966-2-1",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_IEC61966_2_1}, INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"bt2020-10",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT2020_10},    INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"bt2020-12",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_BT2020_12},    INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"smpte2084",                      NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_SMPTE2084},    INT_MIN, INT_MAX, STATIC, "color_trc"},
    {"arib-std-b67",                   NULL,  0, AV_OPT_TYPE_CONST, {.i64=AVCOL_TRC_ARIB_STD_B67}, INT_MIN, INT_MAX, STATIC, "color_trc"},

    { "upscaler", "Upscaler function", OFFSET(upscaler), AV_OPT_TYPE_STRING, {.str = "spline36"}, .flags = DYNAMIC },
    { "downscaler", "Downscaler function", OFFSET(downscaler), AV_OPT_TYPE_STRING, {.str = "mitchell"}, .flags = DYNAMIC },
    { "lut_entries", "Number of scaler LUT entries", OFFSET(lut_entries), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 256, DYNAMIC },
    { "antiringing", "Antiringing strength (for non-EWA filters)", OFFSET(antiringing), AV_OPT_TYPE_FLOAT, {.dbl = 0.0}, 0.0, 1.0, DYNAMIC },
    { "sigmoid", "Enable sigmoid upscaling", OFFSET(sigmoid), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, DYNAMIC },
    { "apply_filmgrain", "Apply film grain metadata", OFFSET(apply_filmgrain), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, DYNAMIC },
    { "apply_dolbyvision", "Apply Dolby Vision metadata", OFFSET(apply_dovi), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, DYNAMIC },

    { "deband", "Enable debanding", OFFSET(deband), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DYNAMIC },
    { "deband_iterations", "Deband iterations", OFFSET(deband_iterations), AV_OPT_TYPE_INT, {.i64 = 1}, 0, 16, DYNAMIC },
    { "deband_threshold", "Deband threshold", OFFSET(deband_threshold), AV_OPT_TYPE_FLOAT, {.dbl = 4.0}, 0.0, 1024.0, DYNAMIC },
    { "deband_radius", "Deband radius", OFFSET(deband_radius), AV_OPT_TYPE_FLOAT, {.dbl = 16.0}, 0.0, 1024.0, DYNAMIC },
    { "deband_grain", "Deband grain", OFFSET(deband_grain), AV_OPT_TYPE_FLOAT, {.dbl = 6.0}, 0.0, 1024.0, DYNAMIC },

    { "brightness", "Brightness boost", OFFSET(brightness), AV_OPT_TYPE_FLOAT, {.dbl = 0.0}, -1.0, 1.0, DYNAMIC },
    { "contrast", "Contrast gain", OFFSET(contrast), AV_OPT_TYPE_FLOAT, {.dbl = 1.0}, 0.0, 16.0, DYNAMIC },
    { "saturation", "Saturation gain", OFFSET(saturation), AV_OPT_TYPE_FLOAT, {.dbl = 1.0}, 0.0, 16.0, DYNAMIC },
    { "hue", "Hue shift", OFFSET(hue), AV_OPT_TYPE_FLOAT, {.dbl = 0.0}, -M_PI, M_PI, DYNAMIC },
    { "gamma", "Gamma adjustment", OFFSET(gamma), AV_OPT_TYPE_FLOAT, {.dbl = 1.0}, 0.0, 16.0, DYNAMIC },

    { "peak_detect", "Enable dynamic peak detection for HDR tone-mapping", OFFSET(peakdetect), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, DYNAMIC },
    { "smoothing_period", "Peak detection smoothing period", OFFSET(smoothing), AV_OPT_TYPE_FLOAT, {.dbl = 100.0}, 0.0, 1000.0, DYNAMIC },
    { "minimum_peak", "Peak detection minimum peak", OFFSET(min_peak), AV_OPT_TYPE_FLOAT, {.dbl = 1.0}, 0.0, 100.0, DYNAMIC },
    { "scene_threshold_low", "Scene change low threshold", OFFSET(scene_low), AV_OPT_TYPE_FLOAT, {.dbl = 5.5}, -1.0, 100.0, DYNAMIC },
    { "scene_threshold_high", "Scene change high threshold", OFFSET(scene_high), AV_OPT_TYPE_FLOAT, {.dbl = 10.0}, -1.0, 100.0, DYNAMIC },
    { "overshoot", "Tone-mapping overshoot margin", OFFSET(overshoot), AV_OPT_TYPE_FLOAT, {.dbl = 0.05}, 0.0, 1.0, DYNAMIC },

    { "intent", "Rendering intent", OFFSET(intent), AV_OPT_TYPE_INT, {.i64 = PL_INTENT_RELATIVE_COLORIMETRIC}, 0, 3, DYNAMIC, "intent" },
        { "perceptual", "Perceptual", 0, AV_OPT_TYPE_CONST, {.i64 = PL_INTENT_PERCEPTUAL}, 0, 0, STATIC, "intent" },
        { "relative", "Relative colorimetric", 0, AV_OPT_TYPE_CONST, {.i64 = PL_INTENT_RELATIVE_COLORIMETRIC}, 0, 0, STATIC, "intent" },
        { "absolute", "Absolute colorimetric", 0, AV_OPT_TYPE_CONST, {.i64 = PL_INTENT_ABSOLUTE_COLORIMETRIC}, 0, 0, STATIC, "intent" },
        { "saturation", "Saturation mapping", 0, AV_OPT_TYPE_CONST, {.i64 = PL_INTENT_SATURATION}, 0, 0, STATIC, "intent" },
    { "gamut_mode", "Gamut-mapping mode", OFFSET(gamut_mode), AV_OPT_TYPE_INT, {.i64 = PL_GAMUT_CLIP}, 0, PL_GAMUT_MODE_COUNT - 1, DYNAMIC, "gamut_mode" },
        { "clip", "Hard-clip gamut boundary", 0, AV_OPT_TYPE_CONST, {.i64 = PL_GAMUT_CLIP}, 0, 0, STATIC, "gamut_mode" },
        { "warn", "Highlight out-of-gamut colors", 0, AV_OPT_TYPE_CONST, {.i64 = PL_GAMUT_WARN}, 0, 0, STATIC, "gamut_mode" },
        { "darken", "Darken image to fit gamut", 0, AV_OPT_TYPE_CONST, {.i64 = PL_GAMUT_DARKEN}, 0, 0, STATIC, "gamut_mode" },
        { "desaturate", "Colorimetrically desaturate colors", 0, AV_OPT_TYPE_CONST, {.i64 = PL_GAMUT_DESATURATE}, 0, 0, STATIC, "gamut_mode" },
    { "tonemapping", "Tone-mapping algorithm", OFFSET(tonemapping), AV_OPT_TYPE_INT, {.i64 = TONE_MAP_AUTO}, 0, TONE_MAP_COUNT - 1, DYNAMIC, "tonemap" },
        { "auto", "Automatic selection", 0, AV_OPT_TYPE_CONST, {.i64 = TONE_MAP_AUTO}, 0, 0, STATIC, "tonemap" },
        { "clip", "No tone mapping (clip", 0, AV_OPT_TYPE_CONST, {.i64 = TONE_MAP_CLIP}, 0, 0, STATIC, "tonemap" },
        { "bt.2390", "ITU-R BT.2390 EETF", 0, AV_OPT_TYPE_CONST, {.i64 = TONE_MAP_BT2390}, 0, 0, STATIC, "tonemap" },
        { "bt.2446a", "ITU-R BT.2446 Method A", 0, AV_OPT_TYPE_CONST, {.i64 = TONE_MAP_BT2446A}, 0, 0, STATIC, "tonemap" },
        { "spline", "Single-pivot polynomial spline", 0, AV_OPT_TYPE_CONST, {.i64 = TONE_MAP_SPLINE}, 0, 0, STATIC, "tonemap" },
        { "reinhard", "Reinhard", 0, AV_OPT_TYPE_CONST, {.i64 = TONE_MAP_REINHARD}, 0, 0, STATIC, "tonemap" },
        { "mobius", "Mobius", 0, AV_OPT_TYPE_CONST, {.i64 = TONE_MAP_MOBIUS}, 0, 0, STATIC, "tonemap" },
        { "hable", "Filmic tone-mapping (Hable)", 0, AV_OPT_TYPE_CONST, {.i64 = TONE_MAP_HABLE}, 0, 0, STATIC, "tonemap" },
        { "gamma", "Gamma function with knee", 0, AV_OPT_TYPE_CONST, {.i64 = TONE_MAP_GAMMA}, 0, 0, STATIC, "tonemap" },
        { "linear", "Perceptually linear stretch", 0, AV_OPT_TYPE_CONST, {.i64 = TONE_MAP_LINEAR}, 0, 0, STATIC, "tonemap" },
    { "tonemapping_param", "Tunable parameter for some tone-mapping functions", OFFSET(tonemapping_param), AV_OPT_TYPE_FLOAT, {.dbl = 0.0}, 0.0, 100.0, .flags = DYNAMIC },
    { "tonemapping_mode", "Tone-mapping mode", OFFSET(tonemapping_mode), AV_OPT_TYPE_INT, {.i64 = PL_TONE_MAP_AUTO}, 0, PL_TONE_MAP_MODE_COUNT - 1, DYNAMIC, "tonemap_mode" },
        { "auto", "Automatic selection", 0, AV_OPT_TYPE_CONST, {.i64 = PL_TONE_MAP_AUTO}, 0, 0, STATIC, "tonemap_mode" },
        { "rgb", "Per-channel (RGB)", 0, AV_OPT_TYPE_CONST, {.i64 = PL_TONE_MAP_RGB}, 0, 0, STATIC, "tonemap_mode" },
        { "max", "Maximum component", 0, AV_OPT_TYPE_CONST, {.i64 = PL_TONE_MAP_MAX}, 0, 0, STATIC, "tonemap_mode" },
        { "hybrid", "Hybrid of Luma/RGB", 0, AV_OPT_TYPE_CONST, {.i64 = PL_TONE_MAP_HYBRID}, 0, 0, STATIC, "tonemap_mode" },
        { "luma", "Luminance", 0, AV_OPT_TYPE_CONST, {.i64 = PL_TONE_MAP_LUMA}, 0, 0, STATIC, "tonemap_mode" },
    { "inverse_tonemapping", "Inverse tone mapping (range expansion)", OFFSET(inverse_tonemapping), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DYNAMIC },
    { "tonemapping_crosstalk", "Crosstalk factor for tone-mapping", OFFSET(crosstalk), AV_OPT_TYPE_FLOAT, {.dbl = 0.04}, 0.0, 0.30, DYNAMIC },
    { "tonemapping_lut_size", "Tone-mapping LUT size", OFFSET(tonemapping_lut_size), AV_OPT_TYPE_INT, {.i64 = 256}, 2, 1024, DYNAMIC },
    /* deprecated options for backwards compatibility, defaulting to -1 to not override the new defaults */
    { "desaturation_strength", "Desaturation strength", OFFSET(desat_str), AV_OPT_TYPE_FLOAT, {.dbl = -1.0}, -1.0, 1.0, DYNAMIC | AV_OPT_FLAG_DEPRECATED },
    { "desaturation_exponent", "Desaturation exponent", OFFSET(desat_exp), AV_OPT_TYPE_FLOAT, {.dbl = -1.0}, -1.0, 10.0, DYNAMIC | AV_OPT_FLAG_DEPRECATED },
    { "gamut_warning", "Highlight out-of-gamut colors", OFFSET(gamut_warning), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DYNAMIC | AV_OPT_FLAG_DEPRECATED },
    { "gamut_clipping", "Enable colorimetric gamut clipping", OFFSET(gamut_clipping), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DYNAMIC | AV_OPT_FLAG_DEPRECATED },

    { "dithering", "Dither method to use", OFFSET(dithering), AV_OPT_TYPE_INT, {.i64 = PL_DITHER_BLUE_NOISE}, -1, PL_DITHER_METHOD_COUNT - 1, DYNAMIC, "dither" },
        { "none", "Disable dithering", 0, AV_OPT_TYPE_CONST, {.i64 = -1}, 0, 0, STATIC, "dither" },
        { "blue", "Blue noise", 0, AV_OPT_TYPE_CONST, {.i64 = PL_DITHER_BLUE_NOISE}, 0, 0, STATIC, "dither" },
        { "ordered", "Ordered LUT", 0, AV_OPT_TYPE_CONST, {.i64 = PL_DITHER_ORDERED_LUT}, 0, 0, STATIC, "dither" },
        { "ordered_fixed", "Fixed function ordered", 0, AV_OPT_TYPE_CONST, {.i64 = PL_DITHER_ORDERED_FIXED}, 0, 0, STATIC, "dither" },
        { "white", "White noise", 0, AV_OPT_TYPE_CONST, {.i64 = PL_DITHER_WHITE_NOISE}, 0, 0, STATIC, "dither" },
    { "dither_lut_size", "Dithering LUT size", OFFSET(dither_lut_size), AV_OPT_TYPE_INT, {.i64 = 6}, 1, 8, STATIC },
    { "dither_temporal", "Enable temporal dithering", OFFSET(dither_temporal), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DYNAMIC },

    { "cones", "Colorblindness adaptation model", OFFSET(cones), AV_OPT_TYPE_FLAGS, {.i64 = 0}, 0, PL_CONE_LMS, DYNAMIC, "cone" },
        { "l", "L cone", 0, AV_OPT_TYPE_CONST, {.i64 = PL_CONE_L}, 0, 0, STATIC, "cone" },
        { "m", "M cone", 0, AV_OPT_TYPE_CONST, {.i64 = PL_CONE_M}, 0, 0, STATIC, "cone" },
        { "s", "S cone", 0, AV_OPT_TYPE_CONST, {.i64 = PL_CONE_S}, 0, 0, STATIC, "cone" },
    { "cone-strength", "Colorblindness adaptation strength", OFFSET(cone_str), AV_OPT_TYPE_FLOAT, {.dbl = 0.0}, 0.0, 10.0, DYNAMIC },

    { "custom_shader_path", "Path to custom user shader (mpv .hook format)", OFFSET(shader_path), AV_OPT_TYPE_STRING, .flags = STATIC },
    { "custom_shader_bin", "Custom user shader as binary (mpv .hook format)", OFFSET(shader_bin), AV_OPT_TYPE_BINARY, .flags = STATIC },

    /* Performance/quality tradeoff options */
    { "skip_aa", "Skip anti-aliasing", OFFSET(skip_aa), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 0, DYNAMIC },
    { "polar_cutoff", "Polar LUT cutoff", OFFSET(polar_cutoff), AV_OPT_TYPE_FLOAT, {.dbl = 0}, 0.0, 1.0, DYNAMIC },
    { "disable_linear", "Disable linear scaling", OFFSET(disable_linear), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DYNAMIC },
    { "disable_builtin", "Disable built-in scalers", OFFSET(disable_builtin), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DYNAMIC },
    { "force_icc_lut", "Force the use of a full ICC 3DLUT for color mapping", OFFSET(force_icc_lut), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DYNAMIC },
    { "force_dither", "Force dithering", OFFSET(force_dither), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DYNAMIC },
    { "disable_fbos", "Force-disable FBOs", OFFSET(disable_fbos), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, DYNAMIC },
    { NULL },
};

AVFILTER_DEFINE_CLASS(libplacebo);

static const AVFilterPad libplacebo_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = &filter_frame,
        .config_props = &ff_vk_filter_config_input,
    },
};

static const AVFilterPad libplacebo_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = &libplacebo_config_output,
    },
};

const AVFilter ff_vf_libplacebo = {
    .name           = "libplacebo",
    .description    = NULL_IF_CONFIG_SMALL("Apply various GPU filters from libplacebo"),
    .priv_size      = sizeof(LibplaceboContext),
    .init           = &libplacebo_init,
    .uninit         = &libplacebo_uninit,
    .process_command = &ff_filter_process_command,
    FILTER_INPUTS(libplacebo_inputs),
    FILTER_OUTPUTS(libplacebo_outputs),
    FILTER_SINGLE_PIXFMT(AV_PIX_FMT_VULKAN),
    .priv_class     = &libplacebo_class,
    .flags_internal = FF_FILTER_FLAG_HWFRAME_AWARE,
};
