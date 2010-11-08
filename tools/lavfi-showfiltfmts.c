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

#include "libavformat/avformat.h"
#include "libavutil/pixdesc.h"
#include "libavfilter/avfilter.h"

int main(int argc, char **argv)
{
    AVFilter *filter;
    AVFilterContext *filter_ctx;
    const char *filter_name;
    const char *filter_args = NULL;
    int i, j;

    av_log_set_level(AV_LOG_DEBUG);

    if (!argv[1]) {
        fprintf(stderr, "Missing filter name as argument\n");
        return 1;
    }

    filter_name = argv[1];
    if (argv[2])
        filter_args = argv[2];

    avfilter_register_all();

    /* get a corresponding filter and open it */
    if (!(filter = avfilter_get_by_name(filter_name))) {
        fprintf(stderr, "Unrecognized filter with name '%s'\n", filter_name);
        return 1;
    }

    if (avfilter_open(&filter_ctx, filter, NULL) < 0) {
        fprintf(stderr, "Inpossible to open filter with name '%s'\n", filter_name);
        return 1;
    }
    if (avfilter_init_filter(filter_ctx, filter_args, NULL) < 0) {
        fprintf(stderr, "Impossible to init filter '%s' with arguments '%s'\n", filter_name, filter_args);
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
        avfilter_default_query_formats(filter_ctx);

    /* print the supported formats in input */
    for (i = 0; i < filter_ctx->input_count; i++) {
        AVFilterFormats *fmts = filter_ctx->inputs[i]->out_formats;
        for (j = 0; j < fmts->format_count; j++)
            printf("INPUT[%d] %s: %s\n",
                   i, filter_ctx->filter->inputs[i].name,
                   av_pix_fmt_descriptors[fmts->formats[j]].name);
    }

    /* print the supported formats in output */
    for (i = 0; i < filter_ctx->output_count; i++) {
        AVFilterFormats *fmts = filter_ctx->outputs[i]->in_formats;
        for (j = 0; j < fmts->format_count; j++)
            printf("OUTPUT[%d] %s: %s\n",
                   i, filter_ctx->filter->outputs[i].name,
                   av_pix_fmt_descriptors[fmts->formats[j]].name);
    }

    avfilter_free(filter_ctx);
    fflush(stdout);
    return 0;
}
