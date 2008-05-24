/*
 * filter graph parser
 * copyright (c) 2008 Vitor Sessak
 * copyright (c) 2007 Bobby Bingham
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

#include <ctype.h>
#include <string.h>

#include "avfilter.h"
#include "avfiltergraph.h"

/**
 * For use in av_log
 */
static const char *log_name(void *p)
{
    return "Filter parser";
}

static const AVClass filter_parser_class = {
    "Filter parser",
    log_name
};

static const AVClass *log_ctx = &filter_parser_class;

static int create_filter(AVFilterGraph *ctx, int index, char *name,
                         char *args)
{
    AVFilterContext *filt;

    AVFilter *filterdef;
    char tmp[20];

    snprintf(tmp, 20, "%d", index);
    if(!(filterdef = avfilter_get_by_name(name)) ||
       !(filt = avfilter_open(filterdef, tmp))) {
        av_log(&log_ctx, AV_LOG_ERROR,
               "error creating filter '%s'\n", name);
        return -1;
    }
    avfilter_graph_add_filter(ctx, filt);
    if(avfilter_init_filter(filt, args, NULL)) {
        av_log(&log_ctx, AV_LOG_ERROR,
               "error initializing filter '%s'\n", name);
        return -1;
    }

    return 0;
}

static int link_filter(AVFilterGraph *ctx, int src, int srcpad,
                       int dst, int dstpad)
{
    AVFilterContext *filt, *filtb;

    char tmp[20];

    snprintf(tmp, 20, "%d", src);
    if(!(filt = avfilter_graph_get_filter(ctx, tmp))) {
        av_log(&log_ctx, AV_LOG_ERROR, "link source does not exist in graph\n");
        return -1;
    }
    snprintf(tmp, 20, "%d", dst);
    if(!(filtb = avfilter_graph_get_filter(ctx, tmp))) {
        av_log(&log_ctx, AV_LOG_ERROR, "link destination does not exist in graph\n");
        return -1;
    }
    if(avfilter_link(filt, srcpad, filtb, dstpad)) {
        av_log(&log_ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
        return -1;
    }

    return 0;
}

static void consume_whitespace(const char **buf)
{
    *buf += strspn(*buf, " \n\t");
}

/**
 * Copy the first size bytes of input string to a null-terminated string,
 * removing any control character. Ex: "aaa'bb'c\'c\\" -> "aaabbc'c\"
 */
static void copy_unquoted(char *out, const char *in, int size)
{
    int i;
    for (i=0; i < size; i++) {
        if (in[i] == '\'')
            continue;
        else if (in[i] == '\\') {
            if (i+1 == size) {
                *out = 0;
                return;
            }
            i++;
        }
        *out++ = in[i];
    }
    *out=0;
}

/**
 * Consumes a string from *buf.
 * @return a copy of the consumed string, which should be free'd after use
 */
static char *consume_string(const char **buf)
{
    const char *start;
    char *ret;
    int size;

    consume_whitespace(buf);

    if (!(**buf))
        return av_mallocz(1);

    start = *buf;

    while(1) {
        *buf += strcspn(*buf, " ()=,'\\");
        if (**buf == '\\')
            *buf+=2;
        else
            break;
    }

    if (**buf == '\'') {
        const char *p = *buf;
        do {
            p++;
            p = strchr(p, '\'');
        } while (p && p[-1] == '\\');
        if (p)
            *buf = p + 1;
        else
            *buf += strlen(*buf); // Move the pointer to the null end byte
    }

    size = *buf - start + 1;
    ret = av_malloc(size);
    copy_unquoted(ret, start, size-1);

    return ret;
}

/**
 * Parse "(linkname)"
 * @arg name a pointer (that need to be free'd after use) to the name between
 *           parenthesis
 */
static void parse_link_name(const char **buf, char **name)
{
    (*buf)++;

    *name = consume_string(buf);

    if (!*name[0])
        goto fail;

    if (*(*buf)++ != ')')
        goto fail;

    return;
 fail:
    av_freep(name);
    av_log(&log_ctx, AV_LOG_ERROR, "Could not parse link name!\n");
}

/**
 * Parse "filter=params"
 * @arg name a pointer (that need to be free'd after use) to the name of the
 *           filter
 * @arg ars  a pointer (that need to be free'd after use) to the args of the
 *           filter
 */
static int parse_filter(const char **buf, AVFilterGraph *graph, int index)
{
    char *name, *opts;
    name = consume_string(buf);

    if (**buf == '=') {
        (*buf)++;
        opts = consume_string(buf);
    } else {
        opts = NULL;
    }

    return create_filter(graph, index, name, opts);
}

enum LinkType {
    LinkTypeIn,
    LinkTypeOut,
};

/**
 * A linked-list of the inputs/outputs of the filter chain.
 */
typedef struct AVFilterInOut {
    enum LinkType type;
    char *name;
    int instance;
    int pad_idx;

    struct AVFilterInOut *next;
} AVFilterInOut;

static void free_inout(AVFilterInOut *head)
{
    while (head) {
        AVFilterInOut *next;
        next = head->next;
        av_free(head);
        head = next;
    }
}

/**
 * Parse "(a1)(link2) ... (etc)"
 */
static int parse_inouts(const char **buf, AVFilterInOut **inout, int firstpad,
                        enum LinkType type, int instance)
{
    int pad = firstpad;
    while (**buf == '(') {
        AVFilterInOut *inoutn = av_malloc(sizeof(AVFilterInOut));
        parse_link_name(buf, &inoutn->name);
        inoutn->type = type;
        inoutn->instance = instance;
        inoutn->pad_idx = pad++;
        inoutn->next = *inout;
        *inout = inoutn;
    }
    return pad;
}

/**
 * Parse a string describing a filter graph.
 */
int avfilter_graph_parse_chain(AVFilterGraph *graph, const char *filters, AVFilterContext *in, int inpad, AVFilterContext *out, int outpad)
{
    AVFilterInOut *inout=NULL;
    AVFilterInOut  *head=NULL;

    int index = 0;
    char chr = 0;
    int pad = 0;
    int has_out = 0;

    char tmp[20];
    AVFilterContext *filt;

    consume_whitespace(&filters);

    do {
        int oldpad = pad;

        pad = parse_inouts(&filters, &inout, chr == ',', LinkTypeIn, index);

        if (parse_filter(&filters, graph, index) < 0)
            goto fail;

        // If the first filter has an input and none was given, it is
        // implicitly the input of the whole graph.
        if (pad == 0 && graph->filters[graph->filter_count-1]->input_count == 1) {
            snprintf(tmp, 20, "%d", index);
            if(!(filt = avfilter_graph_get_filter(graph, tmp))) {
                av_log(&log_ctx, AV_LOG_ERROR, "filter owning exported pad does not exist\n");
                goto fail;
            }
            if(avfilter_link(in, inpad, filt, 0)) {
                av_log(&log_ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
                goto fail;
            }
        }

        if(chr == ',') {
            if (link_filter(graph, index-1, oldpad, index, 0) < 0)
                goto fail;

        }
        pad = parse_inouts(&filters, &inout, 0, LinkTypeOut, index);
        chr = *filters++;
        index++;
    } while (chr == ',' || chr == ';');

    head = inout;
    for (; inout != NULL; inout = inout->next) {
        if (inout->instance == -1)
            continue; // Already processed

        if (!strcmp(inout->name, "in")) {
            snprintf(tmp, 20, "%d", inout->instance);
            if(!(filt = avfilter_graph_get_filter(graph, tmp))) {
                av_log(&log_ctx, AV_LOG_ERROR, "filter owning exported pad does not exist\n");
                goto fail;
            }
            if(avfilter_link(in, inpad, filt, inout->pad_idx)) {
                av_log(&log_ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
                goto fail;
            }
        } else if (!strcmp(inout->name, "out")) {
            has_out = 1;
            snprintf(tmp, 20, "%d", inout->instance);
            if(!(filt = avfilter_graph_get_filter(graph, tmp))) {
                av_log(&log_ctx, AV_LOG_ERROR, "filter owning exported pad does not exist\n");
                goto fail;
            }

            if(avfilter_link(filt, inout->pad_idx, out, outpad)) {
                av_log(&log_ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
                goto fail;
        }

        } else {
            AVFilterInOut *p, *src, *dst;
            for (p = inout->next;
                 p && strcmp(p->name,inout->name); p = p->next);

            if (!p) {
                av_log(&log_ctx, AV_LOG_ERROR, "Unmatched link: %s.\n",
                       inout->name);
                goto fail;
            }

            if (p->type == LinkTypeIn && inout->type == LinkTypeOut) {
                src = inout;
                dst = p;
            } else if (p->type == LinkTypeOut && inout->type == LinkTypeIn) {
                src = p;
                dst = inout;
            } else {
                av_log(&log_ctx, AV_LOG_ERROR, "Two links named '%s' are either both input or both output\n",
                       inout->name);
                goto fail;
            }

            if (link_filter(graph, src->instance, src->pad_idx, dst->instance, dst->pad_idx) < 0)
                goto fail;

            src->instance = -1;
            dst->instance = -1;
        }
    }

    free_inout(head);

    if (!has_out) {
        snprintf(tmp, 20, "%d", index-1);
        if(!(filt = avfilter_graph_get_filter(graph, tmp))) {
            av_log(&log_ctx, AV_LOG_ERROR, "filter owning exported pad does not exist\n");
            goto fail;
        }

        if(avfilter_link(filt, pad, out, outpad)) {
            av_log(&log_ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
            goto fail;
        }

    }

    return 0;

 fail:
    free_inout(head);
    avfilter_destroy_graph(graph);
    return -1;
}
