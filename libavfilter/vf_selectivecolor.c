/*
 * Copyright (c) 2015 Clément Bœsch <u pkh me>
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
 * @todo
 * - use integers so it can be made bitexact and a FATE test can be added
 * - >8 bit support
 */

#include "libavutil/avassert.h"
#include "libavutil/file.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum color_range {
    // WARNING: do NOT reorder (see parse_psfile())
    RANGE_REDS,
    RANGE_YELLOWS,
    RANGE_GREENS,
    RANGE_CYANS,
    RANGE_BLUES,
    RANGE_MAGENTAS,
    RANGE_WHITES,
    RANGE_NEUTRALS,
    RANGE_BLACKS,
    NB_RANGES
};

enum correction_method {
    CORRECTION_METHOD_ABSOLUTE,
    CORRECTION_METHOD_RELATIVE,
    NB_CORRECTION_METHODS,
};

static const char *color_names[NB_RANGES] = {
    "red", "yellow", "green", "cyan", "blue", "magenta", "white", "neutral", "black"
};

typedef int (*get_adjust_range_func)(int r, int g, int b, int min_val, int max_val);

struct process_range {
    int range_id;
    uint32_t mask;
    get_adjust_range_func get_adjust_range;
};

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

typedef struct {
    const AVClass *class;
    int correction_method;
    char *opt_cmyk_adjust[NB_RANGES];
    float cmyk_adjust[NB_RANGES][4];
    struct process_range process_ranges[NB_RANGES]; // color ranges to process
    int nb_process_ranges;
    char *psfile;
} SelectiveColorContext;

#define OFFSET(x) offsetof(SelectiveColorContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define RANGE_OPTION(color_name, range) \
    { color_name"s", "adjust "color_name" regions", OFFSET(opt_cmyk_adjust[range]), AV_OPT_TYPE_STRING, {.str=NULL}, CHAR_MIN, CHAR_MAX, FLAGS }

static const AVOption selectivecolor_options[] = {
    { "correction_method", "select correction method", OFFSET(correction_method), AV_OPT_TYPE_INT, {.i64 = CORRECTION_METHOD_ABSOLUTE}, 0, NB_CORRECTION_METHODS-1, FLAGS, "correction_method" },
        { "absolute", NULL, 0, AV_OPT_TYPE_CONST, {.i64=CORRECTION_METHOD_ABSOLUTE}, INT_MIN, INT_MAX, FLAGS, "correction_method" },
        { "relative", NULL, 0, AV_OPT_TYPE_CONST, {.i64=CORRECTION_METHOD_RELATIVE}, INT_MIN, INT_MAX, FLAGS, "correction_method" },
    RANGE_OPTION("red",     RANGE_REDS),
    RANGE_OPTION("yellow",  RANGE_YELLOWS),
    RANGE_OPTION("green",   RANGE_GREENS),
    RANGE_OPTION("cyan",    RANGE_CYANS),
    RANGE_OPTION("blue",    RANGE_BLUES),
    RANGE_OPTION("magenta", RANGE_MAGENTAS),
    RANGE_OPTION("white",   RANGE_WHITES),
    RANGE_OPTION("neutral", RANGE_NEUTRALS),
    RANGE_OPTION("black",   RANGE_BLACKS),
    { "psfile", "set Photoshop selectivecolor file name", OFFSET(psfile), AV_OPT_TYPE_STRING, {.str=NULL}, .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(selectivecolor);

static inline int get_mid_val(int r, int g, int b)
{
    if ((r < g && r > b) || (r < b && r > g)) return r;
    if ((g < r && g > b) || (g < b && g > r)) return g;
    if ((b < r && b > g) || (b < g && b > r)) return b;
    return -1;
}

static int get_rgb_adjust_range(int r, int g, int b, int min_val, int max_val)
{
    // max - mid
    const int mid_val = get_mid_val(r, g, b);
    if (mid_val == -1) {
        // XXX: can be simplified
        if ((r != min_val && g == min_val && b == min_val) ||
            (r == min_val && g != min_val && b == min_val) ||
            (r == min_val && g == min_val && b != min_val))
            return max_val - min_val;
        return 0;
    }
    return max_val - mid_val;
}

static int get_cmy_adjust_range(int r, int g, int b, int min_val, int max_val)
{
    // mid - min
    const int mid_val = get_mid_val(r, g, b);
    if (mid_val == -1) {
        // XXX: refactor with rgb
        if ((r != max_val && g == max_val && b == max_val) ||
            (r == max_val && g != max_val && b == max_val) ||
            (r == max_val && g == max_val && b != max_val))
            return max_val - min_val;
        return 0;
    }
    return mid_val - min_val;
}

static int get_neutrals_adjust_range(int r, int g, int b, int min_val, int max_val)
{
    // 1 - (|max-0.5| + |min-0.5|)
    return (255*2 - (abs((max_val<<1) - 255) + abs((min_val<<1) - 255)) + 1) >> 1;
}

static int get_whites_adjust_range(int r, int g, int b, int min_val, int max_val)
{
    // (min - 0.5) * 2
    return (min_val<<1) - 255;
}

static int get_blacks_adjust_range(int r, int g, int b, int min_val, int max_val)
{
    // (0.5 - max) * 2
    return 255 - (max_val<<1);
}

static int register_range(SelectiveColorContext *s, int range_id)
{
    const float *cmyk = s->cmyk_adjust[range_id];

    /* If the color range has user settings, register the color range
     * as "to be processed" */
    if (cmyk[0] || cmyk[1] || cmyk[2] || cmyk[3]) {
        struct process_range *pr = &s->process_ranges[s->nb_process_ranges++];

        if (cmyk[0] < -1.0 || cmyk[0] > 1.0 ||
            cmyk[1] < -1.0 || cmyk[1] > 1.0 ||
            cmyk[2] < -1.0 || cmyk[2] > 1.0 ||
            cmyk[3] < -1.0 || cmyk[3] > 1.0) {
            av_log(s, AV_LOG_ERROR, "Invalid %s adjustments (%g %g %g %g). "
                   "Settings must be set in [-1;1] range\n",
                   color_names[range_id], cmyk[0], cmyk[1], cmyk[2], cmyk[3]);
            return AVERROR(EINVAL);
        }

        pr->range_id = range_id;
        pr->mask = 1 << range_id;
        if      (pr->mask & (1<<RANGE_REDS  | 1<<RANGE_GREENS   | 1<<RANGE_BLUES))   pr->get_adjust_range = get_rgb_adjust_range;
        else if (pr->mask & (1<<RANGE_CYANS | 1<<RANGE_MAGENTAS | 1<<RANGE_YELLOWS)) pr->get_adjust_range = get_cmy_adjust_range;
        else if (pr->mask & 1<<RANGE_WHITES)                                         pr->get_adjust_range = get_whites_adjust_range;
        else if (pr->mask & 1<<RANGE_NEUTRALS)                                       pr->get_adjust_range = get_neutrals_adjust_range;
        else if (pr->mask & 1<<RANGE_BLACKS)                                         pr->get_adjust_range = get_blacks_adjust_range;
        else
            av_assert0(0);
    }
    return 0;
}

static int parse_psfile(AVFilterContext *ctx, const char *fname)
{
    int16_t val;
    int ret, i, version;
    uint8_t *buf;
    size_t size;
    SelectiveColorContext *s = ctx->priv;

    ret = av_file_map(fname, &buf, &size, 0, NULL);
    if (ret < 0)
        return ret;

#define READ16(dst) do {                \
    if (size < 2) {                     \
        ret = AVERROR_INVALIDDATA;      \
        goto end;                       \
    }                                   \
    dst = AV_RB16(buf);                 \
    buf  += 2;                          \
    size -= 2;                          \
} while (0)

    READ16(version);
    if (version != 1)
        av_log(s, AV_LOG_WARNING, "Unsupported selective color file version %d, "
               "the settings might not be loaded properly\n", version);

    READ16(s->correction_method);

    // 1st CMYK entry is reserved/unused
    for (i = 0; i < FF_ARRAY_ELEMS(s->cmyk_adjust[0]); i++) {
        READ16(val);
        if (val)
            av_log(s, AV_LOG_WARNING, "%c value of first CMYK entry is not 0 "
                   "but %d\n", "CMYK"[i], val);
    }

    for (i = 0; i < FF_ARRAY_ELEMS(s->cmyk_adjust); i++) {
        int k;
        for (k = 0; k < FF_ARRAY_ELEMS(s->cmyk_adjust[0]); k++) {
            READ16(val);
            s->cmyk_adjust[i][k] = val / 100.;
        }
        ret = register_range(s, i);
        if (ret < 0)
            goto end;
    }

end:
    av_file_unmap(buf, size);
    return ret;
}

static av_cold int init(AVFilterContext *ctx)
{
    int i, ret;
    SelectiveColorContext *s = ctx->priv;

    /* If the following conditions are not met, it will cause trouble while
     * parsing the PS file */
    av_assert0(FF_ARRAY_ELEMS(s->cmyk_adjust) == 10 - 1);
    av_assert0(FF_ARRAY_ELEMS(s->cmyk_adjust[0]) == 4);

    if (s->psfile) {
        ret = parse_psfile(ctx, s->psfile);
        if (ret < 0)
            return ret;
    } else {
        for (i = 0; i < FF_ARRAY_ELEMS(s->opt_cmyk_adjust); i++) {
            const char *opt_cmyk_adjust = s->opt_cmyk_adjust[i];

            if (opt_cmyk_adjust) {
                float *cmyk = s->cmyk_adjust[i];

                sscanf(s->opt_cmyk_adjust[i], "%f %f %f %f", cmyk, cmyk+1, cmyk+2, cmyk+3);
                ret = register_range(s, i);
                if (ret < 0)
                    return ret;
            }
        }
    }

    av_log(s, AV_LOG_VERBOSE, "Adjustments:%s\n", s->nb_process_ranges ? "" : " none");
    for (i = 0; i < s->nb_process_ranges; i++) {
        const struct process_range *pr = &s->process_ranges[i];
        const float *cmyk = s->cmyk_adjust[pr->range_id];

        av_log(s, AV_LOG_VERBOSE, "%8ss: C=%6g M=%6g Y=%6g K=%6g\n",
               color_names[pr->range_id], cmyk[0], cmyk[1], cmyk[2], cmyk[3]);
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {AV_PIX_FMT_RGB32, AV_PIX_FMT_0RGB32, AV_PIX_FMT_NONE};
    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static inline int comp_adjust(int adjust_range, float value, float adjust, float k, int correction_method)
{
    const float min = -value;
    const float max = 1. - value;
    float res = (-1. - adjust) * k - adjust;
    if (correction_method == CORRECTION_METHOD_RELATIVE)
        res *= max;
    return lrint(av_clipf(res, min, max) * adjust_range);
}

static inline int selective_color(AVFilterContext *ctx, ThreadData *td,
                                  int jobnr, int nb_jobs, int direct, int correction_method)
{
    int i, x, y;
    const AVFrame *in = td->in;
    AVFrame *out = td->out;
    const SelectiveColorContext *s = ctx->priv;
    const int height = in->height;
    const int width  = in->width;
    const int slice_start = (height *  jobnr   ) / nb_jobs;
    const int slice_end   = (height * (jobnr+1)) / nb_jobs;
    const int dst_linesize = out->linesize[0];
    const int src_linesize =  in->linesize[0];
    uint8_t       *dst = out->data[0] + slice_start * dst_linesize;
    const uint8_t *src =  in->data[0] + slice_start * src_linesize;

    for (y = slice_start; y < slice_end; y++) {
        const uint32_t *src32 = (const uint32_t *)src;
        uint32_t       *dst32 = (uint32_t *)dst;

        for (x = 0; x < width; x++) {
            const uint32_t color = *src32++;
            const int r = color >> 16 & 0xff;
            const int g = color >>  8 & 0xff;
            const int b = color       & 0xff;
            const int min_color = FFMIN3(r, g, b);
            const int max_color = FFMAX3(r, g, b);
            const uint32_t range_flag = (r == max_color) << RANGE_REDS
                                      | (r == min_color) << RANGE_CYANS
                                      | (g == max_color) << RANGE_GREENS
                                      | (g == min_color) << RANGE_MAGENTAS
                                      | (b == max_color) << RANGE_BLUES
                                      | (b == min_color) << RANGE_YELLOWS
                                      | (r > 128 && g > 128 && b > 128) << RANGE_WHITES
                                      | (color && (color & 0xffffff) != 0xffffff) << RANGE_NEUTRALS
                                      | (r < 128 && g < 128 && b < 128) << RANGE_BLACKS;

            const float rnorm = r / 255.;
            const float gnorm = g / 255.;
            const float bnorm = b / 255.;
            int adjust_r = 0, adjust_g = 0, adjust_b = 0;

            for (i = 0; i < s->nb_process_ranges; i++) {
                const struct process_range *pr = &s->process_ranges[i];

                if (range_flag & pr->mask) {
                    const int adjust_range = pr->get_adjust_range(r, g, b, min_color, max_color);

                    if (adjust_range > 0) {
                        const float *cmyk_adjust = s->cmyk_adjust[pr->range_id];
                        const float adj_c = cmyk_adjust[0];
                        const float adj_m = cmyk_adjust[1];
                        const float adj_y = cmyk_adjust[2];
                        const float k = cmyk_adjust[3];

                        adjust_r += comp_adjust(adjust_range, rnorm, adj_c, k, correction_method);
                        adjust_g += comp_adjust(adjust_range, gnorm, adj_m, k, correction_method);
                        adjust_b += comp_adjust(adjust_range, bnorm, adj_y, k, correction_method);
                    }
                }
            }

            if (!direct || adjust_r || adjust_g || adjust_b)
                *dst32 = (color & 0xff000000)
                       | av_clip_uint8(r + adjust_r) << 16
                       | av_clip_uint8(g + adjust_g) <<  8
                       | av_clip_uint8(b + adjust_b);
            dst32++;
        }
        src += src_linesize;
        dst += dst_linesize;
    }
    return 0;
}

#define DEF_SELECTIVE_COLOR_FUNC(name, direct, correction_method)                           \
static int selective_color_##name(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)  \
{                                                                                           \
    return selective_color(ctx, arg, jobnr, nb_jobs, direct, correction_method);            \
}

DEF_SELECTIVE_COLOR_FUNC(indirect_absolute, 0, CORRECTION_METHOD_ABSOLUTE)
DEF_SELECTIVE_COLOR_FUNC(indirect_relative, 0, CORRECTION_METHOD_RELATIVE)
DEF_SELECTIVE_COLOR_FUNC(  direct_absolute, 1, CORRECTION_METHOD_ABSOLUTE)
DEF_SELECTIVE_COLOR_FUNC(  direct_relative, 1, CORRECTION_METHOD_RELATIVE)

typedef int (*selective_color_func_type)(AVFilterContext *ctx, void *td, int jobnr, int nb_jobs);

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    int direct;
    AVFrame *out;
    ThreadData td;
    const SelectiveColorContext *s = ctx->priv;
    static const selective_color_func_type funcs[2][2] = {
        {selective_color_indirect_absolute, selective_color_indirect_relative},
        {selective_color_direct_absolute,   selective_color_direct_relative},
    };

    if (av_frame_is_writable(in)) {
        direct = 1;
        out = in;
    } else {
        direct = 0;
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    td.in = in;
    td.out = out;
    ctx->internal->execute(ctx, funcs[direct][s->correction_method], &td, NULL,
                           FFMIN(inlink->h, ctx->graph->nb_threads));

    if (!direct)
        av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static const AVFilterPad selectivecolor_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad selectivecolor_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_selectivecolor = {
    .name          = "selectivecolor",
    .description   = NULL_IF_CONFIG_SMALL("Apply CMYK adjustments to specific color ranges."),
    .priv_size     = sizeof(SelectiveColorContext),
    .init          = init,
    .query_formats = query_formats,
    .inputs        = selectivecolor_inputs,
    .outputs       = selectivecolor_outputs,
    .priv_class    = &selectivecolor_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
