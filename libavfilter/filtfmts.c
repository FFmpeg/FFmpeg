/*
 * Copyright (c) 2009 Stefano Sabatini
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

#include <stdio.h>

#include "libavformat/avformat.h"
#include "libavutil/pixdesc.h"
#include "libavutil/samplefmt.h"
#include "libavfilter/avfilter.h"
#include "libavfilter/formats.h"

static void print_formats(AVFilterContext *filter_ctx)
{
    int i, j;

#define PRINT_FMTS(inout, outin, INOUT)                                 \
    for (i = 0; i < filter_ctx->inout##put_count; i++) {                     \
        if (filter_ctx->inout##puts[i]->type == AVMEDIA_TYPE_VIDEO) {   \
            AVFilterFormats *fmts =                                     \
                filter_ctx->inout##puts[i]->outin##_formats;            \
            for (j = 0; j < fmts->format_count; j++)                    \
                if(av_get_pix_fmt_name(fmts->formats[j]))               \
                printf(#INOUT "PUT[%d] %s: fmt:%s\n",                   \
                       i, filter_ctx->filter->inout##puts[i].name,      \
                       av_get_pix_fmt_name(fmts->formats[j]));          \
        } else if (filter_ctx->inout##puts[i]->type == AVMEDIA_TYPE_AUDIO) { \
            AVFilterFormats *fmts;                                      \
            AVFilterChannelLayouts *layouts;                            \
                                                                        \
            fmts = filter_ctx->inout##puts[i]->outin##_formats;         \
            for (j = 0; j < fmts->format_count; j++)                    \
                printf(#INOUT "PUT[%d] %s: fmt:%s\n",                   \
                       i, filter_ctx->filter->inout##puts[i].name,      \
                       av_get_sample_fmt_name(fmts->formats[j]));       \
                                                                        \
            layouts = filter_ctx->inout##puts[i]->outin##_channel_layouts; \
            for (j = 0; j < layouts->nb_channel_layouts; j++) {                  \
                char buf[256];                                          \
                av_get_channel_layout_string(buf, sizeof(buf), -1,      \
                                             layouts->channel_layouts[j]);         \
                printf(#INOUT "PUT[%d] %s: chlayout:%s\n",              \
                       i, filter_ctx->filter->inout##puts[i].name, buf); \
            }                                                           \
        }                                                               \
    }                                                                   \

    PRINT_FMTS(in,  out, IN);
    PRINT_FMTS(out, in,  OUT);
}

int main(int argc, char **argv)
{
    AVFilter *filter;
    AVFilterContext *filter_ctx;
    const char *filter_name;
    const char *filter_args = NULL;
    int i;

    av_log_set_level(AV_LOG_DEBUG);

    if (argc < 2) {
        fprintf(stderr, "Missing filter name as argument\n");
        return 1;
    }

    filter_name = argv[1];
    if (argc > 2)
        filter_args = argv[2];

    avfilter_register_all();

    /* get a corresponding filter and open it */
    if (!(filter = avfilter_get_by_name(filter_name))) {
        fprintf(stderr, "Unrecognized filter with name '%s'\n", filter_name);
        return 1;
    }

    if (avfilter_open(&filter_ctx, filter, NULL) < 0) {
        fprintf(stderr, "Impossible to open filter with name '%s'\n",
                filter_name);
        return 1;
    }
    if (avfilter_init_str(filter_ctx, filter_args) < 0) {
        fprintf(stderr, "Impossible to init filter '%s' with arguments '%s'\n",
                filter_name, filter_args);
        return 1;
    }

    /* create a link for each of the input pads */
    for (i = 0; i < filter_ctx->input_count; i++) {
        AVFilterLink *link = av_mallocz(sizeof(AVFilterLink));
        link->type = filter_ctx->filter->inputs[i].type;
        filter_ctx->inputs[i] = link;
    }
    for (i = 0; i < filter_ctx->output_count; i++) {
        AVFilterLink *link = av_mallocz(sizeof(AVFilterLink));
        link->type = filter_ctx->filter->outputs[i].type;
        filter_ctx->outputs[i] = link;
    }

    if (filter->query_formats)
        filter->query_formats(filter_ctx);
    else
        ff_default_query_formats(filter_ctx);

    print_formats(filter_ctx);

    avfilter_free(filter_ctx);
    fflush(stdout);
    return 0;
}
