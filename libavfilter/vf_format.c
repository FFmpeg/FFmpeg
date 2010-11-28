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

#include "libavutil/pixdesc.h"
#include "avfilter.h"

typedef struct {
    /**
     * List of flags telling if a given image format has been listed
     * as argument to the filter.
     */
    int listed_pix_fmt_flags[PIX_FMT_NB];
} FormatContext;

#define PIX_FMT_NAME_MAXSIZE 32

static av_cold int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    FormatContext *format = ctx->priv;
    const char *cur, *sep;
    char             pix_fmt_name[PIX_FMT_NAME_MAXSIZE];
    int              pix_fmt_name_len;
    enum PixelFormat pix_fmt;

    /* parse the list of formats */
    for (cur = args; cur; cur = sep ? sep+1 : NULL) {
        if (!(sep = strchr(cur, ':')))
            pix_fmt_name_len = strlen(cur);
        else
            pix_fmt_name_len = sep - cur;
        if (pix_fmt_name_len >= PIX_FMT_NAME_MAXSIZE) {
            av_log(ctx, AV_LOG_ERROR, "Format name too long\n");
            return -1;
        }

        memcpy(pix_fmt_name, cur, pix_fmt_name_len);
        pix_fmt_name[pix_fmt_name_len] = 0;
        pix_fmt = av_get_pix_fmt(pix_fmt_name);

        if (pix_fmt == PIX_FMT_NONE) {
            av_log(ctx, AV_LOG_ERROR, "Unknown pixel format: %s\n", pix_fmt_name);
            return -1;
        }

        format->listed_pix_fmt_flags[pix_fmt] = 1;
    }

    return 0;
}

static AVFilterFormats *make_format_list(FormatContext *format, int flag)
{
    AVFilterFormats *formats;
    enum PixelFormat pix_fmt;

    formats = av_mallocz(sizeof(AVFilterFormats));
    formats->formats = av_malloc(sizeof(enum PixelFormat) * PIX_FMT_NB);

    for (pix_fmt = 0; pix_fmt < PIX_FMT_NB; pix_fmt++)
        if (format->listed_pix_fmt_flags[pix_fmt] == flag)
            formats->formats[formats->format_count++] = pix_fmt;

    return formats;
}

#if CONFIG_FORMAT_FILTER
static int query_formats_format(AVFilterContext *ctx)
{
    avfilter_set_common_formats(ctx, make_format_list(ctx->priv, 1));
    return 0;
}

AVFilter avfilter_vf_format = {
    .name      = "format",
    .description = NULL_IF_CONFIG_SMALL("Convert the input video to one of the specified pixel formats."),

    .init      = init,

    .query_formats = query_formats_format,

    .priv_size = sizeof(FormatContext),

    .inputs    = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .get_video_buffer= avfilter_null_get_video_buffer,
                                    .start_frame     = avfilter_null_start_frame,
                                    .draw_slice      = avfilter_null_draw_slice,
                                    .end_frame       = avfilter_null_end_frame, },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO },
                                  { .name = NULL}},
};
#endif /* CONFIG_FORMAT_FILTER */

#if CONFIG_NOFORMAT_FILTER
static int query_formats_noformat(AVFilterContext *ctx)
{
    avfilter_set_common_formats(ctx, make_format_list(ctx->priv, 0));
    return 0;
}

AVFilter avfilter_vf_noformat = {
    .name      = "noformat",
    .description = NULL_IF_CONFIG_SMALL("Force libavfilter not to use any of the specified pixel formats for the input to the next filter."),

    .init      = init,

    .query_formats = query_formats_noformat,

    .priv_size = sizeof(FormatContext),

    .inputs    = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO,
                                    .get_video_buffer= avfilter_null_get_video_buffer,
                                    .start_frame     = avfilter_null_start_frame,
                                    .draw_slice      = avfilter_null_draw_slice,
                                    .end_frame       = avfilter_null_end_frame, },
                                  { .name = NULL}},
    .outputs   = (AVFilterPad[]) {{ .name            = "default",
                                    .type            = AVMEDIA_TYPE_VIDEO },
                                  { .name = NULL}},
};
#endif /* CONFIG_NOFORMAT_FILTER */

