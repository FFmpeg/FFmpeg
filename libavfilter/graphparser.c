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

static AVFilterContext *create_filter(AVFilterGraph *ctx, int index,
                                      const char *name, const char *args)
{
    AVFilterContext *filt;

    AVFilter *filterdef;
    char inst_name[30];

    snprintf(inst_name, sizeof(inst_name), "Parsed filter %d", index);

    if(!(filterdef = avfilter_get_by_name(name))) {
        av_log(&log_ctx, AV_LOG_ERROR,
               "no such filter: '%s'\n", name);
        return NULL;
    }

    if(!(filt = avfilter_open(filterdef, inst_name))) {
        av_log(&log_ctx, AV_LOG_ERROR,
               "error creating filter '%s'\n", name);
        return NULL;
    }

    if(avfilter_graph_add_filter(ctx, filt) < 0)
        return NULL;

    if(avfilter_init_filter(filt, args, NULL)) {
        av_log(&log_ctx, AV_LOG_ERROR,
               "error initializing filter '%s' with args '%s'\n", name, args);
        return NULL;
    }

    return filt;
}

static int link_filter(AVFilterContext *src, int srcpad,
                       AVFilterContext *dst, int dstpad)
{
    if(avfilter_link(src, srcpad, dst, dstpad)) {
        av_log(&log_ctx, AV_LOG_ERROR,
               "cannot create the link %s:%d -> %s:%d\n",
               src->filter->name, srcpad, dst->filter->name, dstpad);
        return -1;
    }

    return 0;
}

static void consume_whitespace(const char **buf)
{
    *buf += strspn(*buf, " \n\t");
}

/**
 * Consumes a string from *buf.
 * @return a copy of the consumed string, which should be free'd after use
 */
static char *consume_string(const char **buf)
{
    char *out = av_malloc(strlen(*buf) + 1);
    char *ret = out;

    consume_whitespace(buf);

    do{
        char c = *(*buf)++;
        switch (c) {
        case '\\':
            *out++= *(*buf)++;
            break;
        case '\'':
            while(**buf && **buf != '\'')
                *out++= *(*buf)++;
            if(**buf) (*buf)++;
            break;
        case 0:
        case ']':
        case '[':
        case '=':
        case ',':
        case ' ':
        case '\n':
            *out++= 0;
            break;
        default:
            *out++= c;
        }
    } while(out[-1]);

    (*buf)--;
    consume_whitespace(buf);

    return ret;
}

/**
 * Parse "[linkname]"
 * @arg name a pointer (that need to be free'd after use) to the name between
 *           parenthesis
 */
static void parse_link_name(const char **buf, char **name)
{
    const char *start = *buf;
    (*buf)++;

    *name = consume_string(buf);

    if(!*name[0]) {
        av_log(&log_ctx, AV_LOG_ERROR,
               "Bad (empty?) label found in the following: \"%s\".\n", start);
        goto fail;
    }

    if(*(*buf)++ != ']') {
        av_log(&log_ctx, AV_LOG_ERROR,
               "Mismatched '[' found in the following: \"%s\".\n", start);
    fail:
        av_freep(name);
    }
}

/**
 * Parse "filter=params"
 * @arg name a pointer (that need to be free'd after use) to the name of the
 *           filter
 * @arg ars  a pointer (that need to be free'd after use) to the args of the
 *           filter
 */
static AVFilterContext *parse_filter(const char **buf, AVFilterGraph *graph, int index)
{
    char *name, *opts;
    name = consume_string(buf);

    if(**buf == '=') {
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
    AVFilterContext *filter;
    int pad_idx;

    struct AVFilterInOut *next;
} AVFilterInOut;

static void free_inout(AVFilterInOut *head)
{
    while (head) {
        AVFilterInOut *next = head->next;
        av_free(head);
        head = next;
    }
}

/**
 * Process a link. This funcion looks for a matching label in the *inout
 * linked list. If none is found, it adds this link to the list.
 */
static int handle_link(char *name, AVFilterInOut **inout, int pad,
                       enum LinkType type, AVFilterContext *filter)
{
    AVFilterInOut *p = *inout;

    for (; p && strcmp(p->name, name); p = p->next);

    if(!p) {
        // First label apearence, add it to the linked list
        AVFilterInOut *inoutn = av_malloc(sizeof(AVFilterInOut));

        inoutn->name    = name;
        inoutn->type    = type;
        inoutn->filter  = filter;
        inoutn->pad_idx = pad;
        inoutn->next    = *inout;
        *inout = inoutn;
         return 0;
    }

    if(p->type == LinkTypeIn && type == LinkTypeOut) {
        if(link_filter(filter, pad, p->filter, p->pad_idx) < 0)
            goto fail;
    } else if(p->type == LinkTypeOut && type == LinkTypeIn) {
        if(link_filter(p->filter, p->pad_idx, filter, pad) < 0)
            goto fail;
    } else {
        av_log(&log_ctx, AV_LOG_ERROR,
               "Two links named '%s' are either both input or both output\n",
               name);
        goto fail;
    }

    p->filter = NULL;

    return 0;
 fail:
    return -1;
}


/**
 * Parse "[a1][link2] ... [etc]"
 */
static int parse_inouts(const char **buf, AVFilterInOut **inout, int pad,
                        enum LinkType type, AVFilterContext *filter)
{
    while (**buf == '[') {
        char *name;

        parse_link_name(buf, &name);

        if (!name)
            return -1;

        handle_link(name, inout, pad++, type, filter);
        consume_whitespace(buf);
    }
    return pad;
}

static const char *skip_inouts(const char *buf)
{
    while (*buf == '[') {
        buf += strcspn(buf, "]") + 1;
        consume_whitespace(&buf);
    }
    return buf;
}


/**
 * Parse a string describing a filter graph.
 */
int avfilter_parse_graph(AVFilterGraph *graph, const char *filters,
                         AVFilterContext *in, int inpad,
                         AVFilterContext *out, int outpad)
{
    AVFilterInOut *inout=NULL;
    AVFilterInOut  *head=NULL;

    int index = 0;
    char chr = 0;
    int pad = 0;
    int has_out = 0;

    AVFilterContext *last_filt = NULL;

    do {
        AVFilterContext *filter;
        int oldpad = pad;
        const char *inouts;

        consume_whitespace(&filters);
        inouts = filters;

        // We need to parse the inputs of the filter after we create it, so
        // skip it by now
        filters = skip_inouts(filters);

        if(!(filter = parse_filter(&filters, graph, index)))
            goto fail;

        pad = parse_inouts(&inouts, &inout, chr == ',', LinkTypeIn, filter);

        if(pad < 0)
            goto fail;

        // If the first filter has an input and none was given, it is
        // implicitly the input of the whole graph.
        if(pad == 0 && filter->input_count == 1) {
            if(link_filter(in, inpad, filter, 0))
                goto fail;
        }

        if(chr == ',') {
            if(link_filter(last_filt, oldpad, filter, 0) < 0)
                goto fail;
        }

        pad = parse_inouts(&filters, &inout, 0, LinkTypeOut, filter);

        if(pad < 0)
            goto fail;

        consume_whitespace(&filters);

        chr = *filters++;
        index++;
        last_filt = filter;
    } while (chr == ',' || chr == ';');

    head = inout;
    // Process remaining labels. Only inputs and outputs should be left.
    for (; inout; inout = inout->next) {
        if(!inout->filter)
            continue; // Already processed

        if(!strcmp(inout->name, "in")) {
            if(link_filter(in, inpad, inout->filter, inout->pad_idx))
                goto fail;

        } else if(!strcmp(inout->name, "out")) {
            has_out = 1;

            if(link_filter(inout->filter, inout->pad_idx, out, outpad))
                goto fail;

        } else {
            av_log(&log_ctx, AV_LOG_ERROR, "Unmatched link: %s.\n",
                   inout->name);
                goto fail;
        }
    }

    free_inout(head);

    if(!has_out) {
        if(link_filter(last_filt, pad, out, outpad))
            goto fail;
    }

    return 0;

 fail:
    free_inout(head);
    avfilter_destroy_graph(graph);
    return -1;
}
