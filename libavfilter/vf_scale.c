/*
 * Copyright (c) 2007 Bobby Bingham
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
 * scale video filter
 */

#include <stdio.h>
#include <string.h>

#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "scale.h"
#include "video.h"
#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/imgutils.h"
#include "libavutil/avassert.h"
#include "libswscale/swscale.h"

enum EvalMode {
    EVAL_MODE_INIT,
    EVAL_MODE_FRAME,
    EVAL_MODE_NB
};

typedef struct ScaleContext {
    const AVClass *class;
    struct SwsContext *sws;     ///< software scaler context
    struct SwsContext *isws[2]; ///< software scaler context for interlaced material
    AVDictionary *opts;

    /**
     * New dimensions. Special values are:
     *   0 = original width/height
     *  -1 = keep original aspect
     *  -N = try to keep aspect but make sure it is divisible by N
     */
    int w, h;
    char *size_str;
    unsigned int flags;         ///sws flags
    double param[2];            // sws params

    int hsub, vsub;             ///< chroma subsampling
    int slice_y;                ///< top of current output slice
    int input_is_pal;           ///< set to 1 if the input format is paletted
    int output_is_pal;          ///< set to 1 if the output format is paletted
    int interlaced;

    char *w_expr;               ///< width  expression string
    char *h_expr;               ///< height expression string
    char *flags_str;

    char *in_color_matrix;
    char *out_color_matrix;

    int in_range;
    int out_range;

    int out_h_chr_pos;
    int out_v_chr_pos;
    int in_h_chr_pos;
    int in_v_chr_pos;

    int force_original_aspect_ratio;

    int nb_slices;

    int eval_mode;              ///< expression evaluation mode

} ScaleContext;

AVFilter ff_vf_scale2ref;

static av_cold int init_dict(AVFilterContext *ctx, AVDictionary **opts)
{
    ScaleContext *scale = ctx->priv;
    int ret;

    if (scale->size_str && (scale->w_expr || scale->h_expr)) {
        av_log(ctx, AV_LOG_ERROR,
               "Size and width/height expressions cannot be set at the same time.\n");
            return AVERROR(EINVAL);
    }

    if (scale->w_expr && !scale->h_expr)
        FFSWAP(char *, scale->w_expr, scale->size_str);

    if (scale->size_str) {
        char buf[32];
        if ((ret = av_parse_video_size(&scale->w, &scale->h, scale->size_str)) < 0) {
            av_log(ctx, AV_LOG_ERROR,
                   "Invalid size '%s'\n", scale->size_str);
            return ret;
        }
        snprintf(buf, sizeof(buf)-1, "%d", scale->w);
        av_opt_set(scale, "w", buf, 0);
        snprintf(buf, sizeof(buf)-1, "%d", scale->h);
        av_opt_set(scale, "h", buf, 0);
    }
    if (!scale->w_expr)
        av_opt_set(scale, "w", "iw", 0);
    if (!scale->h_expr)
        av_opt_set(scale, "h", "ih", 0);

    av_log(ctx, AV_LOG_VERBOSE, "w:%s h:%s flags:'%s' interl:%d\n",
           scale->w_expr, scale->h_expr, (char *)av_x_if_null(scale->flags_str, ""), scale->interlaced);

    scale->flags = 0;

    if (scale->flags_str) {
        const AVClass *class = sws_get_class();
        const AVOption    *o = av_opt_find(&class, "sws_flags", NULL, 0,
                                           AV_OPT_SEARCH_FAKE_OBJ);
        int ret = av_opt_eval_flags(&class, o, scale->flags_str, &scale->flags);
        if (ret < 0)
            return ret;
    }
    scale->opts = *opts;
    *opts = NULL;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ScaleContext *scale = ctx->priv;
    sws_freeContext(scale->sws);
    sws_freeContext(scale->isws[0]);
    sws_freeContext(scale->isws[1]);
    scale->sws = NULL;
    av_dict_free(&scale->opts);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats;
    enum AVPixelFormat pix_fmt;
    int ret;

    if (ctx->inputs[0]) {
        const AVPixFmtDescriptor *desc = NULL;
        formats = NULL;
        while ((desc = av_pix_fmt_desc_next(desc))) {
            pix_fmt = av_pix_fmt_desc_get_id(desc);
            if ((sws_isSupportedInput(pix_fmt) ||
                 sws_isSupportedEndiannessConversion(pix_fmt))
                && (ret = ff_add_format(&formats, pix_fmt)) < 0) {
                return ret;
            }
        }
        if ((ret = ff_formats_ref(formats, &ctx->inputs[0]->out_formats)) < 0)
            return ret;
    }
    if (ctx->outputs[0]) {
        const AVPixFmtDescriptor *desc = NULL;
        formats = NULL;
        while ((desc = av_pix_fmt_desc_next(desc))) {
            pix_fmt = av_pix_fmt_desc_get_id(desc);
            if ((sws_isSupportedOutput(pix_fmt) || pix_fmt == AV_PIX_FMT_PAL8 ||
                 sws_isSupportedEndiannessConversion(pix_fmt))
                && (ret = ff_add_format(&formats, pix_fmt)) < 0) {
                return ret;
            }
        }
        if ((ret = ff_formats_ref(formats, &ctx->outputs[0]->in_formats)) < 0)
            return ret;
    }

    return 0;
}

static const int *parse_yuv_type(const char *s, enum AVColorSpace colorspace)
{
    if (!s)
        s = "bt601";

    if (s && strstr(s, "bt709")) {
        colorspace = AVCOL_SPC_BT709;
    } else if (s && strstr(s, "fcc")) {
        colorspace = AVCOL_SPC_FCC;
    } else if (s && strstr(s, "smpte240m")) {
        colorspace = AVCOL_SPC_SMPTE240M;
    } else if (s && (strstr(s, "bt601") || strstr(s, "bt470") || strstr(s, "smpte170m"))) {
        colorspace = AVCOL_SPC_BT470BG;
    } else if (s && strstr(s, "bt2020")) {
        colorspace = AVCOL_SPC_BT2020_NCL;
    }

    if (colorspace < 1 || colorspace > 10 || colorspace == 8) {
        colorspace = AVCOL_SPC_BT470BG;
    }

    return sws_getCoefficients(colorspace);
}

static int config_props(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink0 = outlink->src->inputs[0];
    AVFilterLink *inlink  = ctx->filter == &ff_vf_scale2ref ?
                            outlink->src->inputs[1] :
                            outlink->src->inputs[0];
    enum AVPixelFormat outfmt = outlink->format;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    ScaleContext *scale = ctx->priv;
    int w, h;
    int ret;

    if ((ret = ff_scale_eval_dimensions(ctx,
                                        scale->w_expr, scale->h_expr,
                                        inlink, outlink,
                                        &w, &h)) < 0)
        goto fail;

    /* Note that force_original_aspect_ratio may overwrite the previous set
     * dimensions so that it is not divisible by the set factors anymore. */
    if (scale->force_original_aspect_ratio) {
        int tmp_w = av_rescale(h, inlink->w, inlink->h);
        int tmp_h = av_rescale(w, inlink->h, inlink->w);

        if (scale->force_original_aspect_ratio == 1) {
             w = FFMIN(tmp_w, w);
             h = FFMIN(tmp_h, h);
        } else {
             w = FFMAX(tmp_w, w);
             h = FFMAX(tmp_h, h);
        }
    }

    if (w > INT_MAX || h > INT_MAX ||
        (h * inlink->w) > INT_MAX  ||
        (w * inlink->h) > INT_MAX)
        av_log(ctx, AV_LOG_ERROR, "Rescaled value for width or height is too big.\n");

    outlink->w = w;
    outlink->h = h;

    /* TODO: make algorithm configurable */

    scale->input_is_pal = desc->flags & AV_PIX_FMT_FLAG_PAL;
    if (outfmt == AV_PIX_FMT_PAL8) outfmt = AV_PIX_FMT_BGR8;
    scale->output_is_pal = av_pix_fmt_desc_get(outfmt)->flags & AV_PIX_FMT_FLAG_PAL ||
                           av_pix_fmt_desc_get(outfmt)->flags & FF_PSEUDOPAL;

    if (scale->sws)
        sws_freeContext(scale->sws);
    if (scale->isws[0])
        sws_freeContext(scale->isws[0]);
    if (scale->isws[1])
        sws_freeContext(scale->isws[1]);
    scale->isws[0] = scale->isws[1] = scale->sws = NULL;
    if (inlink0->w == outlink->w &&
        inlink0->h == outlink->h &&
        !scale->out_color_matrix &&
        scale->in_range == scale->out_range &&
        inlink0->format == outlink->format)
        ;
    else {
        struct SwsContext **swscs[3] = {&scale->sws, &scale->isws[0], &scale->isws[1]};
        int i;

        for (i = 0; i < 3; i++) {
            int in_v_chr_pos = scale->in_v_chr_pos, out_v_chr_pos = scale->out_v_chr_pos;
            struct SwsContext **s = swscs[i];
            *s = sws_alloc_context();
            if (!*s)
                return AVERROR(ENOMEM);

            av_opt_set_int(*s, "srcw", inlink0 ->w, 0);
            av_opt_set_int(*s, "srch", inlink0 ->h >> !!i, 0);
            av_opt_set_int(*s, "src_format", inlink0->format, 0);
            av_opt_set_int(*s, "dstw", outlink->w, 0);
            av_opt_set_int(*s, "dsth", outlink->h >> !!i, 0);
            av_opt_set_int(*s, "dst_format", outfmt, 0);
            av_opt_set_int(*s, "sws_flags", scale->flags, 0);
            av_opt_set_int(*s, "param0", scale->param[0], 0);
            av_opt_set_int(*s, "param1", scale->param[1], 0);
            if (scale->in_range != AVCOL_RANGE_UNSPECIFIED)
                av_opt_set_int(*s, "src_range",
                               scale->in_range == AVCOL_RANGE_JPEG, 0);
            if (scale->out_range != AVCOL_RANGE_UNSPECIFIED)
                av_opt_set_int(*s, "dst_range",
                               scale->out_range == AVCOL_RANGE_JPEG, 0);

            if (scale->opts) {
                AVDictionaryEntry *e = NULL;
                while ((e = av_dict_get(scale->opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
                    if ((ret = av_opt_set(*s, e->key, e->value, 0)) < 0)
                        return ret;
                }
            }
            /* Override YUV420P default settings to have the correct (MPEG-2) chroma positions
             * MPEG-2 chroma positions are used by convention
             * XXX: support other 4:2:0 pixel formats */
            if (inlink0->format == AV_PIX_FMT_YUV420P && scale->in_v_chr_pos == -513) {
                in_v_chr_pos = (i == 0) ? 128 : (i == 1) ? 64 : 192;
            }

            if (outlink->format == AV_PIX_FMT_YUV420P && scale->out_v_chr_pos == -513) {
                out_v_chr_pos = (i == 0) ? 128 : (i == 1) ? 64 : 192;
            }

            av_opt_set_int(*s, "src_h_chr_pos", scale->in_h_chr_pos, 0);
            av_opt_set_int(*s, "src_v_chr_pos", in_v_chr_pos, 0);
            av_opt_set_int(*s, "dst_h_chr_pos", scale->out_h_chr_pos, 0);
            av_opt_set_int(*s, "dst_v_chr_pos", out_v_chr_pos, 0);

            if ((ret = sws_init_context(*s, NULL, NULL)) < 0)
                return ret;
            if (!scale->interlaced)
                break;
        }
    }

    if (inlink0->sample_aspect_ratio.num){
        outlink->sample_aspect_ratio = av_mul_q((AVRational){outlink->h * inlink0->w, outlink->w * inlink0->h}, inlink0->sample_aspect_ratio);
    } else
        outlink->sample_aspect_ratio = inlink0->sample_aspect_ratio;

    av_log(ctx, AV_LOG_VERBOSE, "w:%d h:%d fmt:%s sar:%d/%d -> w:%d h:%d fmt:%s sar:%d/%d flags:0x%0x\n",
           inlink ->w, inlink ->h, av_get_pix_fmt_name( inlink->format),
           inlink->sample_aspect_ratio.num, inlink->sample_aspect_ratio.den,
           outlink->w, outlink->h, av_get_pix_fmt_name(outlink->format),
           outlink->sample_aspect_ratio.num, outlink->sample_aspect_ratio.den,
           scale->flags);
    return 0;

fail:
    return ret;
}

static int config_props_ref(AVFilterLink *outlink)
{
    AVFilterLink *inlink = outlink->src->inputs[1];

    outlink->w = inlink->w;
    outlink->h = inlink->h;
    outlink->sample_aspect_ratio = inlink->sample_aspect_ratio;
    outlink->time_base = inlink->time_base;
    outlink->frame_rate = inlink->frame_rate;

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    return ff_request_frame(outlink->src->inputs[0]);
}

static int request_frame_ref(AVFilterLink *outlink)
{
    return ff_request_frame(outlink->src->inputs[1]);
}

static int scale_slice(AVFilterLink *link, AVFrame *out_buf, AVFrame *cur_pic, struct SwsContext *sws, int y, int h, int mul, int field)
{
    ScaleContext *scale = link->dst->priv;
    const uint8_t *in[4];
    uint8_t *out[4];
    int in_stride[4],out_stride[4];
    int i;

    for(i=0; i<4; i++){
        int vsub= ((i+1)&2) ? scale->vsub : 0;
         in_stride[i] = cur_pic->linesize[i] * mul;
        out_stride[i] = out_buf->linesize[i] * mul;
         in[i] = cur_pic->data[i] + ((y>>vsub)+field) * cur_pic->linesize[i];
        out[i] = out_buf->data[i] +            field  * out_buf->linesize[i];
    }
    if(scale->input_is_pal)
         in[1] = cur_pic->data[1];
    if(scale->output_is_pal)
        out[1] = out_buf->data[1];

    return sws_scale(sws, in, in_stride, y/mul, h,
                         out,out_stride);
}

static int filter_frame(AVFilterLink *link, AVFrame *in)
{
    ScaleContext *scale = link->dst->priv;
    AVFilterLink *outlink = link->dst->outputs[0];
    AVFrame *out;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
    char buf[32];
    int in_range;

    if (in->colorspace == AVCOL_SPC_YCGCO)
        av_log(link->dst, AV_LOG_WARNING, "Detected unsupported YCgCo colorspace.\n");

    if(   in->width  != link->w
       || in->height != link->h
       || in->format != link->format
       || in->sample_aspect_ratio.den != link->sample_aspect_ratio.den || in->sample_aspect_ratio.num != link->sample_aspect_ratio.num) {
        int ret;

        if (scale->eval_mode == EVAL_MODE_INIT) {
            snprintf(buf, sizeof(buf)-1, "%d", outlink->w);
            av_opt_set(scale, "w", buf, 0);
            snprintf(buf, sizeof(buf)-1, "%d", outlink->h);
            av_opt_set(scale, "h", buf, 0);
        }

        link->dst->inputs[0]->format = in->format;
        link->dst->inputs[0]->w      = in->width;
        link->dst->inputs[0]->h      = in->height;

        link->dst->inputs[0]->sample_aspect_ratio.den = in->sample_aspect_ratio.den;
        link->dst->inputs[0]->sample_aspect_ratio.num = in->sample_aspect_ratio.num;


        if ((ret = config_props(outlink)) < 0)
            return ret;
    }

    if (!scale->sws)
        return ff_filter_frame(outlink, in);

    scale->hsub = desc->log2_chroma_w;
    scale->vsub = desc->log2_chroma_h;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }

    av_frame_copy_props(out, in);
    out->width  = outlink->w;
    out->height = outlink->h;

    if(scale->output_is_pal)
        avpriv_set_systematic_pal2((uint32_t*)out->data[1], outlink->format == AV_PIX_FMT_PAL8 ? AV_PIX_FMT_BGR8 : outlink->format);

    in_range = in->color_range;

    if (   scale->in_color_matrix
        || scale->out_color_matrix
        || scale-> in_range != AVCOL_RANGE_UNSPECIFIED
        || in_range != AVCOL_RANGE_UNSPECIFIED
        || scale->out_range != AVCOL_RANGE_UNSPECIFIED) {
        int in_full, out_full, brightness, contrast, saturation;
        const int *inv_table, *table;

        sws_getColorspaceDetails(scale->sws, (int **)&inv_table, &in_full,
                                 (int **)&table, &out_full,
                                 &brightness, &contrast, &saturation);

        if (scale->in_color_matrix)
            inv_table = parse_yuv_type(scale->in_color_matrix, in->colorspace);
        if (scale->out_color_matrix)
            table     = parse_yuv_type(scale->out_color_matrix, AVCOL_SPC_UNSPECIFIED);
        else if (scale->in_color_matrix)
            table = inv_table;

        if (scale-> in_range != AVCOL_RANGE_UNSPECIFIED)
            in_full  = (scale-> in_range == AVCOL_RANGE_JPEG);
        else if (in_range != AVCOL_RANGE_UNSPECIFIED)
            in_full  = (in_range == AVCOL_RANGE_JPEG);
        if (scale->out_range != AVCOL_RANGE_UNSPECIFIED)
            out_full = (scale->out_range == AVCOL_RANGE_JPEG);

        sws_setColorspaceDetails(scale->sws, inv_table, in_full,
                                 table, out_full,
                                 brightness, contrast, saturation);
        if (scale->isws[0])
            sws_setColorspaceDetails(scale->isws[0], inv_table, in_full,
                                     table, out_full,
                                     brightness, contrast, saturation);
        if (scale->isws[1])
            sws_setColorspaceDetails(scale->isws[1], inv_table, in_full,
                                     table, out_full,
                                     brightness, contrast, saturation);

        out->color_range = out_full ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    }

    av_reduce(&out->sample_aspect_ratio.num, &out->sample_aspect_ratio.den,
              (int64_t)in->sample_aspect_ratio.num * outlink->h * link->w,
              (int64_t)in->sample_aspect_ratio.den * outlink->w * link->h,
              INT_MAX);

    if(scale->interlaced>0 || (scale->interlaced<0 && in->interlaced_frame)){
        scale_slice(link, out, in, scale->isws[0], 0, (link->h+1)/2, 2, 0);
        scale_slice(link, out, in, scale->isws[1], 0,  link->h   /2, 2, 1);
    }else if (scale->nb_slices) {
        int i, slice_h, slice_start, slice_end = 0;
        const int nb_slices = FFMIN(scale->nb_slices, link->h);
        for (i = 0; i < nb_slices; i++) {
            slice_start = slice_end;
            slice_end   = (link->h * (i+1)) / nb_slices;
            slice_h     = slice_end - slice_start;
            scale_slice(link, out, in, scale->sws, slice_start, slice_h, 1, 0);
        }
    }else{
        scale_slice(link, out, in, scale->sws, 0, link->h, 1, 0);
    }

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int filter_frame_ref(AVFilterLink *link, AVFrame *in)
{
    AVFilterLink *outlink = link->dst->outputs[1];

    return ff_filter_frame(outlink, in);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    ScaleContext *scale = ctx->priv;
    int ret;

    if (   !strcmp(cmd, "width")  || !strcmp(cmd, "w")
        || !strcmp(cmd, "height") || !strcmp(cmd, "h")) {

        int old_w = scale->w;
        int old_h = scale->h;
        AVFilterLink *outlink = ctx->outputs[0];

        av_opt_set(scale, cmd, args, 0);
        if ((ret = config_props(outlink)) < 0) {
            scale->w = old_w;
            scale->h = old_h;
        }
    } else
        ret = AVERROR(ENOSYS);

    return ret;
}

static const AVClass *child_class_next(const AVClass *prev)
{
    return prev ? NULL : sws_get_class();
}

#define OFFSET(x) offsetof(ScaleContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption scale_options[] = {
    { "w",     "Output video width",          OFFSET(w_expr),    AV_OPT_TYPE_STRING,        .flags = FLAGS },
    { "width", "Output video width",          OFFSET(w_expr),    AV_OPT_TYPE_STRING,        .flags = FLAGS },
    { "h",     "Output video height",         OFFSET(h_expr),    AV_OPT_TYPE_STRING,        .flags = FLAGS },
    { "height","Output video height",         OFFSET(h_expr),    AV_OPT_TYPE_STRING,        .flags = FLAGS },
    { "flags", "Flags to pass to libswscale", OFFSET(flags_str), AV_OPT_TYPE_STRING, { .str = "bilinear" }, .flags = FLAGS },
    { "interl", "set interlacing", OFFSET(interlaced), AV_OPT_TYPE_BOOL, {.i64 = 0 }, -1, 1, FLAGS },
    { "size",   "set video size",          OFFSET(size_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, FLAGS },
    { "s",      "set video size",          OFFSET(size_str), AV_OPT_TYPE_STRING, {.str = NULL}, 0, FLAGS },
    {  "in_color_matrix", "set input YCbCr type",   OFFSET(in_color_matrix),  AV_OPT_TYPE_STRING, { .str = "auto" }, .flags = FLAGS, "color" },
    { "out_color_matrix", "set output YCbCr type",  OFFSET(out_color_matrix), AV_OPT_TYPE_STRING, { .str = NULL }, .flags = FLAGS,  "color"},
        { "auto",        NULL, 0, AV_OPT_TYPE_CONST, { .str = "auto" },      0, 0, FLAGS, "color" },
        { "bt601",       NULL, 0, AV_OPT_TYPE_CONST, { .str = "bt601" },     0, 0, FLAGS, "color" },
        { "bt470",       NULL, 0, AV_OPT_TYPE_CONST, { .str = "bt470" },     0, 0, FLAGS, "color" },
        { "smpte170m",   NULL, 0, AV_OPT_TYPE_CONST, { .str = "smpte170m" }, 0, 0, FLAGS, "color" },
        { "bt709",       NULL, 0, AV_OPT_TYPE_CONST, { .str = "bt709" },     0, 0, FLAGS, "color" },
        { "fcc",         NULL, 0, AV_OPT_TYPE_CONST, { .str = "fcc" },       0, 0, FLAGS, "color" },
        { "smpte240m",   NULL, 0, AV_OPT_TYPE_CONST, { .str = "smpte240m" }, 0, 0, FLAGS, "color" },
        { "bt2020",      NULL, 0, AV_OPT_TYPE_CONST, { .str = "bt2020" },    0, 0, FLAGS, "color" },
    {  "in_range", "set input color range",  OFFSET( in_range), AV_OPT_TYPE_INT, {.i64 = AVCOL_RANGE_UNSPECIFIED }, 0, 2, FLAGS, "range" },
    { "out_range", "set output color range", OFFSET(out_range), AV_OPT_TYPE_INT, {.i64 = AVCOL_RANGE_UNSPECIFIED }, 0, 2, FLAGS, "range" },
    { "auto",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_UNSPECIFIED }, 0, 0, FLAGS, "range" },
    { "unknown", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_UNSPECIFIED }, 0, 0, FLAGS, "range" },
    { "full",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_JPEG}, 0, 0, FLAGS, "range" },
    { "limited",NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_MPEG}, 0, 0, FLAGS, "range" },
    { "jpeg",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_JPEG}, 0, 0, FLAGS, "range" },
    { "mpeg",   NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_MPEG}, 0, 0, FLAGS, "range" },
    { "tv",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_MPEG}, 0, 0, FLAGS, "range" },
    { "pc",     NULL, 0, AV_OPT_TYPE_CONST, {.i64 = AVCOL_RANGE_JPEG}, 0, 0, FLAGS, "range" },
    { "in_v_chr_pos",   "input vertical chroma position in luma grid/256"  ,   OFFSET(in_v_chr_pos),  AV_OPT_TYPE_INT, { .i64 = -513}, -513, 512, FLAGS },
    { "in_h_chr_pos",   "input horizontal chroma position in luma grid/256",   OFFSET(in_h_chr_pos),  AV_OPT_TYPE_INT, { .i64 = -513}, -513, 512, FLAGS },
    { "out_v_chr_pos",   "output vertical chroma position in luma grid/256"  , OFFSET(out_v_chr_pos), AV_OPT_TYPE_INT, { .i64 = -513}, -513, 512, FLAGS },
    { "out_h_chr_pos",   "output horizontal chroma position in luma grid/256", OFFSET(out_h_chr_pos), AV_OPT_TYPE_INT, { .i64 = -513}, -513, 512, FLAGS },
    { "force_original_aspect_ratio", "decrease or increase w/h if necessary to keep the original AR", OFFSET(force_original_aspect_ratio), AV_OPT_TYPE_INT, { .i64 = 0}, 0, 2, FLAGS, "force_oar" },
    { "disable",  NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 0 }, 0, 0, FLAGS, "force_oar" },
    { "decrease", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 1 }, 0, 0, FLAGS, "force_oar" },
    { "increase", NULL, 0, AV_OPT_TYPE_CONST, {.i64 = 2 }, 0, 0, FLAGS, "force_oar" },
    { "param0", "Scaler param 0",             OFFSET(param[0]),  AV_OPT_TYPE_DOUBLE, { .dbl = SWS_PARAM_DEFAULT  }, INT_MIN, INT_MAX, FLAGS },
    { "param1", "Scaler param 1",             OFFSET(param[1]),  AV_OPT_TYPE_DOUBLE, { .dbl = SWS_PARAM_DEFAULT  }, INT_MIN, INT_MAX, FLAGS },
    { "nb_slices", "set the number of slices (debug purpose only)", OFFSET(nb_slices), AV_OPT_TYPE_INT, { .i64 = 0 }, 0, INT_MAX, FLAGS },
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, {.i64 = EVAL_MODE_INIT}, 0, EVAL_MODE_NB-1, FLAGS, "eval" },
         { "init",  "eval expressions once during initialization", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_INIT},  .flags = FLAGS, .unit = "eval" },
         { "frame", "eval expressions during initialization and per-frame", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = FLAGS, .unit = "eval" },
    { NULL }
};

static const AVClass scale_class = {
    .class_name       = "scale",
    .item_name        = av_default_item_name,
    .option           = scale_options,
    .version          = LIBAVUTIL_VERSION_INT,
    .category         = AV_CLASS_CATEGORY_FILTER,
    .child_class_next = child_class_next,
};

static const AVFilterPad avfilter_vf_scale_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_scale_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
    },
    { NULL }
};

AVFilter ff_vf_scale = {
    .name            = "scale",
    .description     = NULL_IF_CONFIG_SMALL("Scale the input video size and/or convert the image format."),
    .init_dict       = init_dict,
    .uninit          = uninit,
    .query_formats   = query_formats,
    .priv_size       = sizeof(ScaleContext),
    .priv_class      = &scale_class,
    .inputs          = avfilter_vf_scale_inputs,
    .outputs         = avfilter_vf_scale_outputs,
    .process_command = process_command,
};

static const AVClass scale2ref_class = {
    .class_name       = "scale2ref",
    .item_name        = av_default_item_name,
    .option           = scale_options,
    .version          = LIBAVUTIL_VERSION_INT,
    .category         = AV_CLASS_CATEGORY_FILTER,
    .child_class_next = child_class_next,
};

static const AVFilterPad avfilter_vf_scale2ref_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    {
        .name         = "ref",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame_ref,
    },
    { NULL }
};

static const AVFilterPad avfilter_vf_scale2ref_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .request_frame= request_frame,
    },
    {
        .name         = "ref",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props_ref,
        .request_frame= request_frame_ref,
    },
    { NULL }
};

AVFilter ff_vf_scale2ref = {
    .name            = "scale2ref",
    .description     = NULL_IF_CONFIG_SMALL("Scale the input video size and/or convert the image format to the given reference."),
    .init_dict       = init_dict,
    .uninit          = uninit,
    .query_formats   = query_formats,
    .priv_size       = sizeof(ScaleContext),
    .priv_class      = &scale2ref_class,
    .inputs          = avfilter_vf_scale2ref_inputs,
    .outputs         = avfilter_vf_scale2ref_outputs,
    .process_command = process_command,
};
