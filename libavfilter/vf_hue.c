/*
 * Copyright (c) 2003 Michael Niedermayer
 * Copyright (c) 2012 Jeremy Tran
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
 * Apply a hue/saturation filter to the input video
 * Ported from MPlayer libmpcodecs/vf_hue.c.
 */

#include <float.h>
#include "libavutil/eval.h"
#include "libavutil/imgutils.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"

#include "avfilter.h"
#include "filters.h"
#include "video.h"

#define SAT_MIN_VAL -10
#define SAT_MAX_VAL 10

static const char *const var_names[] = {
    "n",   // frame count
    "pts", // presentation timestamp expressed in AV_TIME_BASE units
    "r",   // frame rate
    "t",   // timestamp expressed in seconds
    "tb",  // timebase
    NULL
};

enum var_name {
    VAR_N,
    VAR_PTS,
    VAR_R,
    VAR_T,
    VAR_TB,
    VAR_NB
};

typedef struct HueContext {
    const    AVClass *class;
    float    hue_deg; /* hue expressed in degrees */
    float    hue; /* hue expressed in radians */
    char     *hue_deg_expr;
    char     *hue_expr;
    AVExpr   *hue_deg_pexpr;
    AVExpr   *hue_pexpr;
    float    saturation;
    char     *saturation_expr;
    AVExpr   *saturation_pexpr;
    float    brightness;
    char     *brightness_expr;
    AVExpr   *brightness_pexpr;
    int      hsub;
    int      vsub;
    int is_first;
    int32_t hue_sin;
    int32_t hue_cos;
    double   var_values[VAR_NB];
    uint8_t  lut_l[256];
    uint8_t  lut_u[256][256];
    uint8_t  lut_v[256][256];
    uint16_t  lut_l16[65536];
    uint16_t  lut_u10[1024][1024];
    uint16_t  lut_v10[1024][1024];
} HueContext;

#define OFFSET(x) offsetof(HueContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM
static const AVOption hue_options[] = {
    { "h", "set the hue angle degrees expression", OFFSET(hue_deg_expr), AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS },
    { "s", "set the saturation expression", OFFSET(saturation_expr), AV_OPT_TYPE_STRING,
      { .str = "1" }, .flags = FLAGS },
    { "H", "set the hue angle radians expression", OFFSET(hue_expr), AV_OPT_TYPE_STRING,
      { .str = NULL }, .flags = FLAGS },
    { "b", "set the brightness expression", OFFSET(brightness_expr), AV_OPT_TYPE_STRING,
      { .str = "0" }, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(hue);

static inline void compute_sin_and_cos(HueContext *hue)
{
    /*
     * Scale the value to the norm of the resulting (U,V) vector, that is
     * the saturation.
     * This will be useful in the apply_lut function.
     */
    hue->hue_sin = lrint(sin(hue->hue) * (1 << 16) * hue->saturation);
    hue->hue_cos = lrint(cos(hue->hue) * (1 << 16) * hue->saturation);
}

static inline void create_luma_lut(HueContext *h)
{
    const float b = h->brightness;
    int i;

    for (i = 0; i < 256; i++) {
        h->lut_l[i] = av_clip_uint8(i + b * 25.5);
    }
    for (i = 0; i < 65536; i++) {
        h->lut_l16[i] = av_clip_uintp2(i + b * 102.4, 10);
    }
}

static inline void create_chrominance_lut(HueContext *h, const int32_t c,
                                          const int32_t s)
{
    int32_t i, j, u, v, new_u, new_v;

    /*
     * If we consider U and V as the components of a 2D vector then its angle
     * is the hue and the norm is the saturation
     */
    for (i = 0; i < 256; i++) {
        for (j = 0; j < 256; j++) {
            /* Normalize the components from range [16;240] to [-112;112] */
            u = i - 128;
            v = j - 128;
            /*
             * Apply the rotation of the vector : (c * u) - (s * v)
             *                                    (s * u) + (c * v)
             * De-normalize the components (without forgetting to scale 128
             * by << 16)
             * Finally scale back the result by >> 16
             */
            new_u = ((c * u) - (s * v) + (1 << 15) + (128 << 16)) >> 16;
            new_v = ((s * u) + (c * v) + (1 << 15) + (128 << 16)) >> 16;

            /* Prevent a potential overflow */
            h->lut_u[i][j] = av_clip_uint8(new_u);
            h->lut_v[i][j] = av_clip_uint8(new_v);
        }
    }
    for (i = 0; i < 1024; i++) {
        for (j = 0; j < 1024; j++) {
            u = i - 512;
            v = j - 512;
            /*
             * Apply the rotation of the vector : (c * u) - (s * v)
             *                                    (s * u) + (c * v)
             * De-normalize the components (without forgetting to scale 512
             * by << 16)
             * Finally scale back the result by >> 16
             */
            new_u = ((c * u) - (s * v) + (1 << 15) + (512 << 16)) >> 16;
            new_v = ((s * u) + (c * v) + (1 << 15) + (512 << 16)) >> 16;

            /* Prevent a potential overflow */
            h->lut_u10[i][j] = av_clip_uintp2(new_u, 10);
            h->lut_v10[i][j] = av_clip_uintp2(new_v, 10);
        }
    }
}

static int set_expr(AVExpr **pexpr_ptr, char **expr_ptr,
                    const char *expr, const char *option, void *log_ctx)
{
    int ret;
    AVExpr *new_pexpr;
    char *new_expr;

    new_expr = av_strdup(expr);
    if (!new_expr)
        return AVERROR(ENOMEM);
    ret = av_expr_parse(&new_pexpr, expr, var_names,
                        NULL, NULL, NULL, NULL, 0, log_ctx);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error when evaluating the expression '%s' for %s\n",
               expr, option);
        av_free(new_expr);
        return ret;
    }

    if (*pexpr_ptr)
        av_expr_free(*pexpr_ptr);
    *pexpr_ptr = new_pexpr;
    av_freep(expr_ptr);
    *expr_ptr = new_expr;

    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    HueContext *hue = ctx->priv;
    int ret;

    if (hue->hue_expr && hue->hue_deg_expr) {
        av_log(ctx, AV_LOG_ERROR,
               "H and h options are incompatible and cannot be specified "
               "at the same time\n");
        return AVERROR(EINVAL);
    }

#define SET_EXPR(expr, option)                                          \
    if (hue->expr##_expr) do {                                          \
        ret = set_expr(&hue->expr##_pexpr, &hue->expr##_expr,           \
                       hue->expr##_expr, option, ctx);                  \
        if (ret < 0)                                                    \
            return ret;                                                 \
    } while (0)
    SET_EXPR(brightness, "b");
    SET_EXPR(saturation, "s");
    SET_EXPR(hue_deg,    "h");
    SET_EXPR(hue,        "H");
#undef SET_EXPR

    av_log(ctx, AV_LOG_VERBOSE,
           "H_expr:%s h_deg_expr:%s s_expr:%s b_expr:%s\n",
           hue->hue_expr, hue->hue_deg_expr, hue->saturation_expr, hue->brightness_expr);
    compute_sin_and_cos(hue);
    hue->is_first = 1;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    HueContext *hue = ctx->priv;

    av_expr_free(hue->brightness_pexpr);
    av_expr_free(hue->hue_deg_pexpr);
    av_expr_free(hue->hue_pexpr);
    av_expr_free(hue->saturation_pexpr);
}

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUV444P,      AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV420P,      AV_PIX_FMT_YUV411P,
    AV_PIX_FMT_YUV410P,      AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVA444P,     AV_PIX_FMT_YUVA422P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUV444P10,      AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUVA444P10,     AV_PIX_FMT_YUVA422P10,
    AV_PIX_FMT_YUVA420P10,
    AV_PIX_FMT_NONE
};

static int config_props(AVFilterLink *inlink)
{
    HueContext *hue = inlink->dst->priv;
    FilterLink   *l = ff_filter_link(inlink);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    hue->hsub = desc->log2_chroma_w;
    hue->vsub = desc->log2_chroma_h;

    hue->var_values[VAR_N]  = 0;
    hue->var_values[VAR_TB] = av_q2d(inlink->time_base);
    hue->var_values[VAR_R]  = l->frame_rate.num == 0 || l->frame_rate.den == 0 ?
        NAN : av_q2d(l->frame_rate);

    return 0;
}

static void apply_luma_lut(HueContext *s,
                           uint8_t *ldst, const int dst_linesize,
                           uint8_t *lsrc, const int src_linesize,
                           int w, int h)
{
    int i;

    while (h--) {
        for (i = 0; i < w; i++)
            ldst[i] = s->lut_l[lsrc[i]];

        lsrc += src_linesize;
        ldst += dst_linesize;
    }
}

static void apply_luma_lut10(HueContext *s,
                             uint16_t *ldst, const int dst_linesize,
                             uint16_t *lsrc, const int src_linesize,
                             int w, int h)
{
    int i;

    while (h--) {
        for (i = 0; i < w; i++)
            ldst[i] = s->lut_l16[lsrc[i]];

        lsrc += src_linesize;
        ldst += dst_linesize;
    }
}

static void apply_lut(HueContext *s,
                      uint8_t *udst, uint8_t *vdst, const int dst_linesize,
                      uint8_t *usrc, uint8_t *vsrc, const int src_linesize,
                      int w, int h)
{
    int i;

    while (h--) {
        for (i = 0; i < w; i++) {
            const int u = usrc[i];
            const int v = vsrc[i];

            udst[i] = s->lut_u[u][v];
            vdst[i] = s->lut_v[u][v];
        }

        usrc += src_linesize;
        vsrc += src_linesize;
        udst += dst_linesize;
        vdst += dst_linesize;
    }
}

static void apply_lut10(HueContext *s,
                      uint16_t *udst, uint16_t *vdst, const int dst_linesize,
                      uint16_t *usrc, uint16_t *vsrc, const int src_linesize,
                      int w, int h)
{
    int i;

    while (h--) {
        for (i = 0; i < w; i++) {
            const int u = av_clip_uintp2(usrc[i], 10);
            const int v = av_clip_uintp2(vsrc[i], 10);

            udst[i] = s->lut_u10[u][v];
            vdst[i] = s->lut_v10[u][v];
        }

        usrc += src_linesize;
        vsrc += src_linesize;
        udst += dst_linesize;
        vdst += dst_linesize;
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpic)
{
    FilterLink *inl = ff_filter_link(inlink);
    HueContext *hue = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    AVFrame *outpic;
    const int32_t old_hue_sin = hue->hue_sin, old_hue_cos = hue->hue_cos;
    const float old_brightness = hue->brightness;
    int direct = 0;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int bps = desc->comp[0].depth > 8 ? 2 : 1;

    if (av_frame_is_writable(inpic)) {
        direct = 1;
        outpic = inpic;
    } else {
        outpic = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!outpic) {
            av_frame_free(&inpic);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(outpic, inpic);
    }

    hue->var_values[VAR_N]   = inl->frame_count_out;
    hue->var_values[VAR_T]   = TS2T(inpic->pts, inlink->time_base);
    hue->var_values[VAR_PTS] = TS2D(inpic->pts);

    if (hue->saturation_expr) {
        hue->saturation = av_expr_eval(hue->saturation_pexpr, hue->var_values, NULL);

        if (hue->saturation < SAT_MIN_VAL || hue->saturation > SAT_MAX_VAL) {
            hue->saturation = av_clip(hue->saturation, SAT_MIN_VAL, SAT_MAX_VAL);
            av_log(inlink->dst, AV_LOG_WARNING,
                   "Saturation value not in range [%d,%d]: clipping value to %0.1f\n",
                   SAT_MIN_VAL, SAT_MAX_VAL, hue->saturation);
        }
    }

    if (hue->brightness_expr) {
        hue->brightness = av_expr_eval(hue->brightness_pexpr, hue->var_values, NULL);

        if (hue->brightness < -10 || hue->brightness > 10) {
            hue->brightness = av_clipf(hue->brightness, -10, 10);
            av_log(inlink->dst, AV_LOG_WARNING,
                   "Brightness value not in range [%d,%d]: clipping value to %0.1f\n",
                   -10, 10, hue->brightness);
        }
    }

    if (hue->hue_deg_expr) {
        hue->hue_deg = av_expr_eval(hue->hue_deg_pexpr, hue->var_values, NULL);
        hue->hue = hue->hue_deg * M_PI / 180;
    } else if (hue->hue_expr) {
        hue->hue = av_expr_eval(hue->hue_pexpr, hue->var_values, NULL);
        hue->hue_deg = hue->hue * 180 / M_PI;
    }

    av_log(inlink->dst, AV_LOG_DEBUG,
           "H:%0.1f*PI h:%0.1f s:%0.1f b:%0.f t:%0.1f n:%d\n",
           hue->hue/M_PI, hue->hue_deg, hue->saturation, hue->brightness,
           hue->var_values[VAR_T], (int)hue->var_values[VAR_N]);

    compute_sin_and_cos(hue);
    if (hue->is_first || (old_hue_sin != hue->hue_sin || old_hue_cos != hue->hue_cos))
        create_chrominance_lut(hue, hue->hue_cos, hue->hue_sin);

    if (hue->is_first || (old_brightness != hue->brightness && hue->brightness))
        create_luma_lut(hue);

    if (!direct) {
        if (!hue->brightness)
            av_image_copy_plane(outpic->data[0], outpic->linesize[0],
                                inpic->data[0],   inpic->linesize[0],
                                inlink->w * bps, inlink->h);
        if (inpic->data[3])
            av_image_copy_plane(outpic->data[3], outpic->linesize[3],
                                inpic->data[3],   inpic->linesize[3],
                                inlink->w * bps, inlink->h);
    }

    if (bps > 1) {
        apply_lut10(hue, (uint16_t*)outpic->data[1], (uint16_t*)outpic->data[2], outpic->linesize[1]/2,
                         (uint16_t*) inpic->data[1], (uint16_t*) inpic->data[2],  inpic->linesize[1]/2,
                    AV_CEIL_RSHIFT(inlink->w, hue->hsub),
                    AV_CEIL_RSHIFT(inlink->h, hue->vsub));
        if (hue->brightness)
            apply_luma_lut10(hue, (uint16_t*)outpic->data[0], outpic->linesize[0]/2,
                                  (uint16_t*) inpic->data[0],  inpic->linesize[0]/2, inlink->w, inlink->h);
    } else {
        apply_lut(hue, outpic->data[1], outpic->data[2], outpic->linesize[1],
                       inpic->data[1],   inpic->data[2],  inpic->linesize[1],
                  AV_CEIL_RSHIFT(inlink->w, hue->hsub),
                  AV_CEIL_RSHIFT(inlink->h, hue->vsub));
        if (hue->brightness)
            apply_luma_lut(hue, outpic->data[0], outpic->linesize[0],
                                inpic->data[0],  inpic->linesize[0], inlink->w, inlink->h);
    }

    if (!direct)
        av_frame_free(&inpic);

    hue->is_first = 0;
    return ff_filter_frame(outlink, outpic);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    HueContext *hue = ctx->priv;
    int ret;

#define SET_EXPR(expr, option)                                          \
    do {                                                                \
        ret = set_expr(&hue->expr##_pexpr, &hue->expr##_expr,           \
                       args, option, ctx);                              \
        if (ret < 0)                                                    \
            return ret;                                                 \
    } while (0)

    if (!strcmp(cmd, "h")) {
        SET_EXPR(hue_deg, "h");
        av_freep(&hue->hue_expr);
    } else if (!strcmp(cmd, "H")) {
        SET_EXPR(hue, "H");
        av_freep(&hue->hue_deg_expr);
    } else if (!strcmp(cmd, "s")) {
        SET_EXPR(saturation, "s");
    } else if (!strcmp(cmd, "b")) {
        SET_EXPR(brightness, "b");
    } else
        return AVERROR(ENOSYS);

    return 0;
}

static const AVFilterPad hue_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_props,
    },
};

const FFFilter ff_vf_hue = {
    .p.name          = "hue",
    .p.description   = NULL_IF_CONFIG_SMALL("Adjust the hue and saturation of the input video."),
    .p.priv_class    = &hue_class,
    .p.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .priv_size       = sizeof(HueContext),
    .init            = init,
    .uninit          = uninit,
    .process_command = process_command,
    FILTER_INPUTS(hue_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
};
