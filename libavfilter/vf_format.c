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
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct FormatContext {
    const AVClass *class;
    char *pix_fmts;

    /**
     * pix_fmts parsed into AVPixelFormats and terminated with
     * AV_PIX_FMT_NONE
     */
    enum AVPixelFormat *formats;
} FormatContext;

static av_cold void uninit(AVFilterContext *ctx)
{
    FormatContext *s = ctx->priv;
    av_freep(&s->formats);
}

static av_cold int init(AVFilterContext *ctx)
{
    FormatContext *s = ctx->priv;
    char *cur, *sep;
    int nb_formats = 1;
    int i;
    int ret;

    if (!s->pix_fmts) {
        av_log(ctx, AV_LOG_ERROR, "Empty output format string.\n");
        return AVERROR(EINVAL);
    }

    /* count the formats */
    cur = s->pix_fmts;
    while ((cur = strchr(cur, '|'))) {
        nb_formats++;
        if (*cur)
            cur++;
    }

    s->formats = av_malloc_array(nb_formats + 1, sizeof(*s->formats));
    if (!s->formats)
        return AVERROR(ENOMEM);

    /* parse the list of formats */
    cur = s->pix_fmts;
    for (i = 0; i < nb_formats; i++) {
        sep = strchr(cur, '|');
        if (sep)
            *sep++ = 0;

        if ((ret = ff_parse_pixel_format(&s->formats[i], cur, ctx)) < 0)
            return ret;

        cur = sep;
    }
    s->formats[nb_formats] = AV_PIX_FMT_NONE;

    if (!strcmp(ctx->filter->name, "noformat")) {
        const AVPixFmtDescriptor *desc = NULL;
        enum AVPixelFormat *formats_allowed;
        int nb_formats_lavu = 0, nb_formats_allowed = 0;

        /* count the formats known to lavu */
        while ((desc = av_pix_fmt_desc_next(desc)))
            nb_formats_lavu++;

        formats_allowed = av_malloc_array(nb_formats_lavu + 1, sizeof(*formats_allowed));
        if (!formats_allowed)
            return AVERROR(ENOMEM);

        /* for each format known to lavu, check if it's in the list of
         * forbidden formats */
        while ((desc = av_pix_fmt_desc_next(desc))) {
            enum AVPixelFormat pix_fmt = av_pix_fmt_desc_get_id(desc);

            for (i = 0; i < nb_formats; i++) {
                if (s->formats[i] == pix_fmt)
                    break;
            }
            if (i < nb_formats)
                continue;

            formats_allowed[nb_formats_allowed++] = pix_fmt;
        }
        formats_allowed[nb_formats_allowed] = AV_PIX_FMT_NONE;
        av_freep(&s->formats);
        s->formats = formats_allowed;
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    FormatContext *s = ctx->priv;

    return ff_set_common_formats_from_list(ctx, s->formats);
}


#define OFFSET(x) offsetof(FormatContext, x)
static const AVOption options[] = {
    { "pix_fmts", "A '|'-separated list of pixel formats", OFFSET(pix_fmts), AV_OPT_TYPE_STRING, .flags = AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM },
    { NULL }
};

AVFILTER_DEFINE_CLASS_EXT(format, "(no)format", options);

#if CONFIG_FORMAT_FILTER

static const AVFilterPad avfilter_vf_format_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_buffer.video = ff_null_get_video_buffer,
    },
};

static const AVFilterPad avfilter_vf_format_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO
    },
};

const AVFilter ff_vf_format = {
    .name          = "format",
    .description   = NULL_IF_CONFIG_SMALL("Convert the input video to one of the specified pixel formats."),

    .init          = init,
    .uninit        = uninit,

    .priv_size     = sizeof(FormatContext),
    .priv_class    = &format_class,

    .flags         = AVFILTER_FLAG_METADATA_ONLY,

    FILTER_INPUTS(avfilter_vf_format_inputs),
    FILTER_OUTPUTS(avfilter_vf_format_outputs),

    FILTER_QUERY_FUNC(query_formats),
};
#endif /* CONFIG_FORMAT_FILTER */

#if CONFIG_NOFORMAT_FILTER

static const AVFilterPad avfilter_vf_noformat_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_VIDEO,
        .get_buffer.video = ff_null_get_video_buffer,
    },
};

static const AVFilterPad avfilter_vf_noformat_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO
    },
};

const AVFilter ff_vf_noformat = {
    .name          = "noformat",
    .description   = NULL_IF_CONFIG_SMALL("Force libavfilter not to use any of the specified pixel formats for the input to the next filter."),
    .priv_class    = &format_class,

    .init          = init,
    .uninit        = uninit,

    .priv_size     = sizeof(FormatContext),

    .flags         = AVFILTER_FLAG_METADATA_ONLY,

    FILTER_INPUTS(avfilter_vf_noformat_inputs),
    FILTER_OUTPUTS(avfilter_vf_noformat_outputs),

    FILTER_QUERY_FUNC(query_formats),
};
#endif /* CONFIG_NOFORMAT_FILTER */
