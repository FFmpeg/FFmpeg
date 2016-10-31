/*
 * Copyright (c) 2008-2010 Stefano Sabatini
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

#include "config.h"
#if HAVE_UNISTD_H
#include <unistd.h>             /* getopt */
#endif
#include <stdio.h>
#include <string.h>

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "libavutil/pixdesc.h"
#include "libavfilter/avfilter.h"

#if !HAVE_GETOPT
#include "compat/getopt.c"
#endif

static void usage(void)
{
    printf("Convert a libavfilter graph to a dot file.\n");
    printf("Usage: graph2dot [OPTIONS]\n");
    printf("\n"
           "Options:\n"
           "-i INFILE         set INFILE as input file, stdin if omitted\n"
           "-o OUTFILE        set OUTFILE as output file, stdout if omitted\n"
           "-h                print this help\n");
}

struct line {
    char data[256];
    struct line *next;
};

static void print_digraph(FILE *outfile, AVFilterGraph *graph)
{
    int i, j;

    fprintf(outfile, "digraph G {\n");
    fprintf(outfile, "node [shape=box]\n");
    fprintf(outfile, "rankdir=LR\n");

    for (i = 0; i < graph->nb_filters; i++) {
        char filter_ctx_label[128];
        const AVFilterContext *filter_ctx = graph->filters[i];

        snprintf(filter_ctx_label, sizeof(filter_ctx_label), "%s\\n(%s)",
                 filter_ctx->name,
                 filter_ctx->filter->name);

        for (j = 0; j < filter_ctx->nb_outputs; j++) {
            AVFilterLink *link = filter_ctx->outputs[j];
            if (link) {
                char dst_filter_ctx_label[128];
                const AVFilterContext *dst_filter_ctx = link->dst;

                snprintf(dst_filter_ctx_label, sizeof(dst_filter_ctx_label),
                         "%s\\n(%s)",
                         dst_filter_ctx->name,
                         dst_filter_ctx->filter->name);

                fprintf(outfile, "\"%s\" -> \"%s\" [ label= \"inpad:%s -> outpad:%s\\n",
                        filter_ctx_label, dst_filter_ctx_label,
                        avfilter_pad_get_name(link->srcpad, 0),
                        avfilter_pad_get_name(link->dstpad, 0));

                if (link->type == AVMEDIA_TYPE_VIDEO) {
                    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(link->format);
                    fprintf(outfile,
                            "fmt:%s w:%d h:%d tb:%d/%d",
                            desc->name,
                            link->w, link->h,
                            link->time_base.num, link->time_base.den);
                } else if (link->type == AVMEDIA_TYPE_AUDIO) {
                    char buf[255];
                    av_get_channel_layout_string(buf, sizeof(buf), -1,
                                                 link->channel_layout);
                    fprintf(outfile,
                            "fmt:%s sr:%d cl:%s tb:%d/%d",
                            av_get_sample_fmt_name(link->format),
                            link->sample_rate, buf,
                            link->time_base.num, link->time_base.den);
                }
                fprintf(outfile, "\" ];\n");
            }
        }
    }
    fprintf(outfile, "}\n");
}

int main(int argc, char **argv)
{
    const char *outfilename = NULL;
    const char *infilename  = NULL;
    FILE *outfile           = NULL;
    FILE *infile            = NULL;
    char *graph_string      = NULL;
    AVFilterGraph *graph = av_mallocz(sizeof(AVFilterGraph));
    char c;

    av_log_set_level(AV_LOG_DEBUG);

    while ((c = getopt(argc, argv, "hi:o:")) != -1) {
        switch (c) {
        case 'h':
            usage();
            return 0;
        case 'i':
            infilename = optarg;
            break;
        case 'o':
            outfilename = optarg;
            break;
        case '?':
            return 1;
        }
    }

    if (!infilename || !strcmp(infilename, "-"))
        infilename = "/dev/stdin";
    infile = fopen(infilename, "r");
    if (!infile) {
        fprintf(stderr, "Failed to open input file '%s': %s\n",
                infilename, strerror(errno));
        return 1;
    }

    if (!outfilename || !strcmp(outfilename, "-"))
        outfilename = "/dev/stdout";
    outfile = fopen(outfilename, "w");
    if (!outfile) {
        fprintf(stderr, "Failed to open output file '%s': %s\n",
                outfilename, strerror(errno));
        return 1;
    }

    /* read from infile and put it in a buffer */
    {
        int64_t count = 0;
        struct line *line, *last_line, *first_line;
        char *p;
        last_line = first_line = av_malloc(sizeof(struct line));
        if (!last_line) {
            fprintf(stderr, "Memory allocation failure\n");
            return 1;
        }

        while (fgets(last_line->data, sizeof(last_line->data), infile)) {
            struct line *new_line = av_malloc(sizeof(struct line));
            if (!new_line) {
                fprintf(stderr, "Memory allocation failure\n");
                return 1;
            }
            count += strlen(last_line->data);
            last_line->next = new_line;
            last_line       = new_line;
        }
        last_line->next = NULL;

        graph_string = av_malloc(count + 1);
        if (!graph_string) {
            fprintf(stderr, "Memory allocation failure\n");
            return 1;
        }
        p = graph_string;
        for (line = first_line; line->next; line = line->next) {
            size_t l = strlen(line->data);
            memcpy(p, line->data, l);
            p += l;
        }
        *p = '\0';
    }

    avfilter_register_all();

    if (avfilter_graph_parse(graph, graph_string, NULL, NULL, NULL) < 0) {
        fprintf(stderr, "Failed to parse the graph description\n");
        return 1;
    }

    if (avfilter_graph_config(graph, NULL) < 0)
        return 1;

    print_digraph(outfile, graph);
    fflush(outfile);

    return 0;
}
