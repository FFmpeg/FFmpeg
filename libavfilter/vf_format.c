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
 * format and noformat video filters
 */

#include "config_components.h"

#include <string.h>

#include "libavutil/internal.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

typedef struct FormatContext {
    const AVClass *class;
    char *pix_fmts;
    char *csps;
    char *ranges;

    AVFilterFormats *formats; ///< parsed from `pix_fmts`
    AVFilterFormats *color_spaces; ///< parsed from `csps`
    AVFilterFormats *color_ranges; ///< parsed from `ranges`
} FormatContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    FormatContext *s = ctx->priv;
    ff_formats_unref(&s->formats);
    ff_formats_unref(&s->color_spaces);
    ff_formats_unref(&s->color_ranges);
}

static av_cold int invert_formats(AVFilterFormats **fmts,
                                  AVFilterFormats *allfmts)
{
    if (!allfmts)
        return AVERROR(ENOMEM);
    if (!*fmts) {
        /* empty fmt list means no restriction, regardless of filter type */
        ff_formats_unref(&allfmts);
        return 0;
    }

    for (int i = 0; i < allfmts->nb_formats; i++) {
        for (int j = 0; j < (*fmts)->nb_formats; j++) {
            if (allfmts->formats[i] == (*fmts)->formats[j]) {
                /* format is forbidden, remove it from allfmts list */
                memmove(&allfmts->formats[i], &allfmts->formats[i+1],
                        (allfmts->nb_formats - (i+1)) * sizeof(*allfmts->formats));
                allfmts->nb_formats--;
                i--; /* repeat loop with same idx */
                break;
            }
        }
    }

    ff_formats_unref(fmts);
    *fmts = allfmts;
    return 0;
}

static int parse_pixel_format(enum AVPixelFormat *ret, const char *arg, void *log_ctx)
{
    char *tail;
    int pix_fmt = av_get_pix_fmt(arg);
    if (pix_fmt == AV_PIX_FMT_NONE) {
        pix_fmt = strtol(arg, &tail, 0);
        if (*tail || !av_pix_fmt_desc_get(pix_fmt)) {
            av_log(log_ctx, AV_LOG_ERROR, "Invalid pixel format '%s'\n", arg);
            return AVERROR(EINVAL);
        }
    }
    *ret = pix_fmt;
    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    FormatContext *s = ctx->priv;
    enum AVPixelFormat pix_fmt;
    int ret;

    for (char *sep, *cur = s->pix_fmts; cur; cur = sep) {
        sep = strchr(cur, '|');
        if (sep && *sep)
            *sep++ = 0;
        if ((ret = parse_pixel_format(&pix_fmt, cur, ctx)) < 0 ||
            (ret = ff_add_format(&s->formats, pix_fmt)) < 0)
            return ret;
    }

    for (char *sep, *cur = s->csps; cur; cur = sep) {
        sep = strchr(cur, '|');
        if (sep && *sep)
            *sep++ = 0;
        if ((ret = av_color_space_from_name(cur)) < 0 ||
            (ret = ff_add_format(&s->color_spaces, ret)) < 0)
            return ret;
    }

    for (char *sep, *cur = s->ranges; cur; cur = sep) {
        sep = strchr(cur, '|');
        if (sep && *sep)
            *sep++ = 0;
        if ((ret = av_color_range_from_name(cur)) < 0 ||
            (ret = ff_add_format(&s->color_ranges, ret)) < 0)
            return ret;
    }

    if (!strcmp(ctx->filter->name, "noformat")) {
        if ((ret = invert_formats(&s->formats,      ff_all_formats(AVMEDIA_TYPE_VIDEO))) < 0 ||
            (ret = invert_formats(&s->color_spaces, ff_all_color_spaces())) < 0 ||
            (ret = invert_formats(&s->color_ranges, ff_all_color_ranges())) < 0)
            return ret;
    }

    /* hold on to a ref for the lifetime of the filter */
    if (s->formats      && (ret = ff_formats_ref(s->formats,      &s->formats)) < 0 ||
        s->color_spaces && (ret = ff_formats_ref(s->color_spaces, &s->color_spaces)) < 0 ||
        s->color_ranges && (ret = ff_formats_ref(s->color_ranges, &s->color_ranges)) < 0)
        return ret;

    return 0;
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    FormatContext *s = ctx->priv;
    int ret;

    if (s->formats      && (ret = ff_set_common_formats2     (ctx, cfg_in, cfg_out, s->formats)) < 0 ||
        s->color_spaces && (ret = ff_set_common_color_spaces2(ctx, cfg_in, cfg_out, s->color_spaces)) < 0 ||
        s->color_ranges && (ret = ff_set_common_color_ranges2(ctx, cfg_in, cfg_out, s->color_ranges)) < 0)
        return ret;

    return 0;
}


#define OFFSET(x) offsetof(FormatContext, x)
static const AVOption options[] = {
    { "pix_fmts", "A '|'-separated list of pixel formats", OFFSET(pix_fmts), AV_OPT_TYPE_STRING, .flags = AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
    { "color_spaces", "A '|'-separated list of color spaces", OFFSET(csps), AV_OPT_TYPE_STRING, .flags = AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
    { "color_ranges", "A '|'-separated list of color ranges", OFFSET(ranges), AV_OPT_TYPE_STRING, .flags = AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
    { NULL }
};

AVFILTER_DEFINE_CLASS_EXT(format, "(no)format", options);

static const AVFilterPad inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_buffer.video = ff_null_get_video_buffer,
    },
};

#if CONFIG_FORMAT_FILTER
const FFFilter ff_vf_format = {
    .p.name        = "format",
    .p.description = NULL_IF_CONFIG_SMALL("Convert the input video to one of the specified pixel formats."),
    .p.priv_class  = &format_class,

    .p.flags       = AVFILTER_FLAG_METADATA_ONLY,

    .init          = init,
    .uninit        = uninit,

    .priv_size     = sizeof(FormatContext),

    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),

    FILTER_QUERY_FUNC2(query_formats),
};
#endif /* CONFIG_FORMAT_FILTER */

#if CONFIG_NOFORMAT_FILTER
const FFFilter ff_vf_noformat = {
    .p.name        = "noformat",
    .p.description = NULL_IF_CONFIG_SMALL("Force libavfilter not to use any of the specified pixel formats for the input to the next filter."),
    .p.priv_class  = &format_class,

    .p.flags       = AVFILTER_FLAG_METADATA_ONLY,

    .init          = init,
    .uninit        = uninit,

    .priv_size     = sizeof(FormatContext),

    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),

    FILTER_QUERY_FUNC2(query_formats),
};
#endif /* CONFIG_NOFORMAT_FILTER */
