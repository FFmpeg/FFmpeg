/*
 * Filter graphs to bad ASCII-art
 * Copyright (c) 2012 Nicolas George
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

#include <string.h>

#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "avfiltergraph.h"

#define BPRINTF(...) \
    cur += snprintf(cur, buf_end - FFMIN(cur, buf_end), __VA_ARGS__)

#define BPAD(c, l) \
    do { \
        if (cur < buf_end) memset(cur, c, FFMIN(l, buf_end - cur)); cur += l; \
    } while (0)

static int snprint_link_prop(char *buf, char *buf_end, AVFilterLink *link)
{
    char *cur = buf, *format;
    char layout[64];

    switch (link->type) {
        case AVMEDIA_TYPE_VIDEO:
            format = av_x_if_null(av_get_pix_fmt_name(link->format), "?");
            BPRINTF("[%dx%d %d:%d %s]", link->w, link->h,
                    link->sample_aspect_ratio.num,
                    link->sample_aspect_ratio.den,
                    format);
            break;

        case AVMEDIA_TYPE_AUDIO:
            av_get_channel_layout_string(layout, sizeof(layout),
                                         -1, link->channel_layout);
            format = av_x_if_null(av_get_sample_fmt_name(link->format), "?");
            BPRINTF("[%dHz %s:%s:%s]",
                    (int)link->sample_rate, format, layout,
                    link->planar ? "planar" : "packed");
            break;

        default:
            BPRINTF("?");
            break;
    }
    return cur - buf;
}

static size_t avfilter_graph_dump_to_buf(AVFilterGraph *graph,
                                         char *buf, char *buf_end)
{
    char *cur = buf, *e;
    unsigned i, j, x;

    for (i = 0; i < graph->filter_count; i++) {
        AVFilterContext *filter = graph->filters[i];
        unsigned max_src_name = 0, max_dst_name = 0;
        unsigned max_in_name  = 0, max_out_name = 0;
        unsigned max_in_fmt   = 0, max_out_fmt  = 0;
        unsigned width, height, in_indent;
        unsigned lname = strlen(filter->name);
        unsigned ltype = strlen(filter->filter->name);

        for (j = 0; j < filter->input_count; j++) {
            AVFilterLink *l = filter->inputs[j];
            unsigned ln = strlen(l->src->name) + 1 + strlen(l->srcpad->name);
            max_src_name = FFMAX(max_src_name, ln);
            max_in_name = FFMAX(max_in_name, strlen(l->dstpad->name));
            max_in_fmt = FFMAX(max_in_fmt, snprint_link_prop(NULL, NULL, l));
        }
        for (j = 0; j < filter->output_count; j++) {
            AVFilterLink *l = filter->outputs[j];
            unsigned ln = strlen(l->dst->name) + 1 + strlen(l->dstpad->name);
            max_dst_name = FFMAX(max_dst_name, ln);
            max_out_name = FFMAX(max_out_name, strlen(l->srcpad->name));
            max_out_fmt = FFMAX(max_out_fmt, snprint_link_prop(NULL, NULL, l));
        }
        in_indent = max_src_name + max_in_name + max_in_fmt;
        in_indent += in_indent ? 4 : 0;
        width = FFMAX(lname + 2, ltype + 4);
        height = FFMAX3(2, filter->input_count, filter->output_count);
        BPAD(' ', in_indent);
        BPRINTF("+");
        BPAD('-', width);
        BPRINTF("+\n");
        for (j = 0; j < height; j++) {
            unsigned in_no  = j - (height - filter->input_count ) / 2;
            unsigned out_no = j - (height - filter->output_count) / 2;

            /* Input link */
            if (in_no < filter->input_count) {
                AVFilterLink *l = filter->inputs[in_no];
                e = cur + max_src_name + 2;
                BPRINTF("%s:%s", l->src->name, l->srcpad->name);
                BPAD('-', e - cur);
                e = cur + max_in_fmt + 2 +
                    max_in_name - strlen(l->dstpad->name);
                cur += snprint_link_prop(cur, buf_end, l);
                BPAD('-', e - cur);
                BPRINTF("%s", l->dstpad->name);
            } else {
                BPAD(' ', in_indent);
            }

            /* Filter */
            BPRINTF("|");
            if (j == (height - 2) / 2) {
                x = (width - lname) / 2;
                BPRINTF("%*s%-*s", x, "", width - x, filter->name);
            } else if (j == (height - 2) / 2 + 1) {
                x = (width - ltype - 2) / 2;
                BPRINTF("%*s(%s)%*s", x, "", filter->filter->name,
                        width - ltype - 2 - x, "");
            } else {
                BPAD(' ', width);
            }
            BPRINTF("|");

            /* Output link */
            if (out_no < filter->output_count) {
                AVFilterLink *l = filter->outputs[out_no];
                unsigned ln = strlen(l->dst->name) + 1 +
                              strlen(l->dstpad->name);
                e = cur + max_out_name + 2;
                BPRINTF("%s", l->srcpad->name);
                BPAD('-', e - cur);
                e = cur + max_out_fmt + 2 +
                    max_dst_name - ln;
                cur += snprint_link_prop(cur, buf_end, l);
                BPAD('-', e - cur);
                BPRINTF("%s:%s", l->dst->name, l->dstpad->name);
            }
            BPRINTF("\n");
        }
        BPAD(' ', in_indent);
        BPRINTF("+");
        BPAD('-', width);
        BPRINTF("+\n");
        BPRINTF("\n");
    }
    if (cur < buf_end)
        *(cur++) = 0;
    return cur - buf;
}

char *avfilter_graph_dump(AVFilterGraph *graph, const char *options)
{
    size_t buf_size = avfilter_graph_dump_to_buf(graph, NULL, NULL);
    char *buf = av_malloc(buf_size);
    if (!buf)
        return NULL;
    avfilter_graph_dump_to_buf(graph, buf, buf + buf_size);
    return buf;
}
