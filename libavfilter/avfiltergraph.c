/*
 * filter graphs
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


/** Linked-list of filters to create for an AVFilterGraphDesc */
typedef struct AVFilterGraphDescFilter
{
    int index;              ///< filter instance index
    char *filter;           ///< name of filter type
    char *args;             ///< filter parameters
    struct AVFilterGraphDescFilter *next;
} AVFilterGraphDescFilter;

/** Linked-list of links between filters */
typedef struct AVFilterGraphDescLink
{
    /* TODO: allow referencing pads by name, not just by index */
    int src;                ///< index of the source filter
    unsigned srcpad;        ///< index of the output pad on the source filter

    int dst;                ///< index of the dest filter
    unsigned dstpad;        ///< index of the input pad on the dest filter

    struct AVFilterGraphDescLink *next;
} AVFilterGraphDescLink;

/** Linked-list of filter pads to be exported from the graph */
typedef struct AVFilterGraphDescExport
{
    /* TODO: allow referencing pads by name, not just by index */
    char *name;             ///< name of the exported pad
    int filter;             ///< index of the filter
    unsigned pad;           ///< index of the pad to be exported

    struct AVFilterGraphDescExport *next;
} AVFilterGraphDescExport;

/** Description of a graph to be loaded from a file, etc */
typedef struct
{
    AVFilterGraphDescFilter *filters;   ///< filters in the graph
    AVFilterGraphDescLink   *links;     ///< links between the filters
    AVFilterGraphDescExport *inputs;    ///< inputs to export
    AVFilterGraphDescExport *outputs;   ///< outputs to export
} AVFilterGraphDesc;

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

static void uninit(AVFilterGraph *graph)
{
    for(; graph->filter_count > 0; graph->filter_count --)
        avfilter_destroy(graph->filters[graph->filter_count - 1]);
    av_freep(&graph->filters);
}

/* TODO: insert in sorted order */
void avfilter_graph_add_filter(AVFilterGraph *graph, AVFilterContext *filter)
{
    graph->filters = av_realloc(graph->filters,
                                sizeof(AVFilterContext*) * ++graph->filter_count);
    graph->filters[graph->filter_count - 1] = filter;
}

/* search intelligently, once we insert in order */
AVFilterContext *avfilter_graph_get_filter(AVFilterGraph *graph, char *name)
{
    int i;

    if(!name)
        return NULL;

    for(i = 0; i < graph->filter_count; i ++)
        if(graph->filters[i]->name && !strcmp(name, graph->filters[i]->name))
            return graph->filters[i];

    return NULL;
}

static int query_formats(AVFilterGraph *graph)
{
    int i, j;

    /* ask all the sub-filters for their supported colorspaces */
    for(i = 0; i < graph->filter_count; i ++) {
        if(graph->filters[i]->filter->query_formats)
            graph->filters[i]->filter->query_formats(graph->filters[i]);
        else
            avfilter_default_query_formats(graph->filters[i]);
    }

    /* go through and merge as many format lists as possible */
    for(i = 0; i < graph->filter_count; i ++) {
        AVFilterContext *filter = graph->filters[i];

        for(j = 0; j < filter->input_count; j ++) {
            AVFilterLink *link;
            if(!(link = filter->inputs[j]))
                continue;
            if(link->in_formats != link->out_formats) {
                if(!avfilter_merge_formats(link->in_formats,
                                           link->out_formats)) {
                    /* couldn't merge format lists. auto-insert scale filter */
                    AVFilterContext *scale;

                    if(!(scale =
                         avfilter_open(avfilter_get_by_name("scale"), NULL)))
                        return -1;
                    if(scale->filter->init(scale, NULL, NULL) ||
                       avfilter_insert_filter(link, scale, 0, 0)) {
                        avfilter_destroy(scale);
                        return -1;
                    }

                    avfilter_graph_add_filter(graph, scale);
                    scale->filter->query_formats(scale);
                    if(!avfilter_merge_formats(scale-> inputs[0]->in_formats,
                                              scale-> inputs[0]->out_formats) ||
                       !avfilter_merge_formats(scale->outputs[0]->in_formats,
                                              scale->outputs[0]->out_formats))
                        return -1;
                }
            }
        }
    }

    return 0;
}

static void pick_format(AVFilterLink *link)
{
    if(!link || !link->in_formats)
        return;

    link->in_formats->format_count = 1;
    link->format = link->in_formats->formats[0];

    avfilter_formats_unref(&link->in_formats);
    avfilter_formats_unref(&link->out_formats);
}

static void pick_formats(AVFilterGraph *graph)
{
    int i, j;

    for(i = 0; i < graph->filter_count; i ++) {
        AVFilterContext *filter = graph->filters[i];

        for(j = 0; j < filter->input_count; j ++)
            pick_format(filter->inputs[j]);
        for(j = 0; j < filter->output_count; j ++)
            pick_format(filter->outputs[j]);
    }
}

int avfilter_graph_config_formats(AVFilterGraph *graph)
{
    /* find supported formats from sub-filters, and merge along links */
    if(query_formats(graph))
        return -1;

    /* Once everything is merged, it's possible that we'll still have
     * multiple valid colorspace choices. We pick the first one. */
    pick_formats(graph);

    return 0;
}

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

int graph_load_from_desc3(AVFilterGraph *graph, AVFilterGraphDesc *desc, AVFilterContext *in, int inpad, AVFilterContext *out, int outpad)
{
    AVFilterGraphDescExport *curpad;
    char tmp[20];
    AVFilterContext *filt;
    AVFilterGraphDescFilter *curfilt;
    AVFilterGraphDescLink   *curlink;


    /* create all filters */
    for(curfilt = desc->filters; curfilt; curfilt = curfilt->next) {
        if (create_filter(graph, curfilt->index, curfilt->filter,
                          curfilt->args) < 0)
            goto fail;
    }

    /* create all links */
    for(curlink = desc->links; curlink; curlink = curlink->next) {
        if (link_filter(graph, curlink->src, curlink->srcpad,
                          curlink->dst, curlink->dstpad) < 0)
            goto fail;
    }

    /* export all input pads */
    for(curpad = desc->inputs; curpad; curpad = curpad->next) {
        snprintf(tmp, 20, "%d", curpad->filter);
        if(!(filt = avfilter_graph_get_filter(graph, tmp))) {
            av_log(&log_ctx, AV_LOG_ERROR, "filter owning exported pad does not exist\n");
            goto fail;
        }
        if(avfilter_link(in, inpad, filt, curpad->pad)) {
            av_log(&log_ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
            goto fail;
        }
    }

    /* export all output pads */
    for(curpad = desc->outputs; curpad; curpad = curpad->next) {
        snprintf(tmp, 20, "%d", curpad->filter);
        if(!(filt = avfilter_graph_get_filter(graph, tmp))) {
            av_log(&log_ctx, AV_LOG_ERROR, "filter owning exported pad does not exist\n");
            goto fail;
        }

        if(avfilter_link(filt, curpad->pad, out, outpad)) {
            av_log(&log_ctx, AV_LOG_ERROR, "cannot create link between source and destination filters\n");
            goto fail;
        }
    }

    return 0;

fail:
    uninit(graph);
    return -1;
}


static void consume_whitespace(const char **buf)
{
    *buf += strspn(*buf, " \n\t");
}

/**
 * get the next non-whitespace char
 */
static char consume_char(const char **buf)
{
    char out;
    consume_whitespace(buf);

    out = **buf;

    if (out)
        (*buf)++;

    return out;
}

/**
 * remove the quotation marks from a string. Ex: "aaa'bb'cc" -> "aaabbcc"
 */
static void unquote(char *str)
{
    char *p1, *p2;
    p1=p2=str;
    while (*p1 != 0) {
        if (*p1 != '\'')
            *p2++ = *p1;
        p1++;
    }

    *p2 = 0;
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

    *buf += strcspn(*buf, " ()=,'");

    if (**buf == '\'') {
        char *p = strchr(*buf + 1, '\'');
        if (p)
            *buf = p + 1;
        else
            *buf += strlen(*buf); // Move the pointer to the null end byte
    }

    size = *buf - start + 1;
    ret = av_malloc(size);
    memcpy(ret, start, size - 1);
    ret[size-1] = 0;

    unquote(ret);

    return ret;
}

/**
 * Parse "(linkname)"
 * @arg name a pointer (that need to be free'd after use) to the name between
 *           parenthesis
 */
static void parse_link_name(const char **buf, char **name)
{
    consume_char(buf);

    *name = consume_string(buf);

    if (!*name[0])
        goto fail;

    if (consume_char(buf) != ')')
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
static void parse_filter(const char **buf, char **name, char **opts)
{
    *name = consume_string(buf);

    if (**buf == '=') {
        consume_char(buf);
        *opts = consume_string(buf);
    } else {
        *opts = NULL;
    }

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
 * Free a graph description.
 */
void avfilter_graph_free_desc(AVFilterGraphDesc *desc)
{
    void *next;

    while(desc->filters) {
        next = desc->filters->next;
        av_free(desc->filters->filter);
        av_free(desc->filters->args);
        av_free(desc->filters);
        desc->filters = next;
    }

    while(desc->links) {
        next = desc->links->next;
        av_free(desc->links);
        desc->links = next;
    }

    while(desc->inputs) {
        next = desc->inputs->next;
        av_free(desc->inputs);
        desc->inputs = next;
    }

    while(desc->outputs) {
        next = desc->outputs->next;
        av_free(desc->outputs);
        desc->outputs = next;
    }
}

static AVFilterGraphDesc *parse_chain(const char *filters, int has_in)
{
    AVFilterGraphDesc        *ret;
    AVFilterGraphDescFilter **filterp, *filtern;
    AVFilterGraphDescLink   **linkp,   *linkn;
    AVFilterInOut           *inout=NULL;
    AVFilterInOut           *head;

    int index = 0;
    char chr = 0;
    int pad = 0;
    int has_out = 0;

    consume_whitespace(&filters);

    if(!(ret = av_mallocz(sizeof(AVFilterGraphDesc))))
        return NULL;

    filterp = &ret->filters;
    linkp   = &ret->links;

    do {
        if(chr == ',') {
            linkn = av_mallocz(sizeof(AVFilterGraphDescLink));
            linkn->src = index-1;
            linkn->srcpad = pad;
            linkn->dst = index;
            linkn->dstpad = 0;

            *linkp = linkn;
            linkp = &linkn->next;
        }
        pad = parse_inouts(&filters, &inout, chr == ',' || (!has_in),
                           LinkTypeIn, index);

        filtern = av_mallocz(sizeof(AVFilterGraphDescFilter));
        filtern->index = index;
        parse_filter(&filters, &filtern->filter, &filtern->args);
        *filterp = filtern;
        filterp = &filtern->next;

        pad = parse_inouts(&filters, &inout, 0,
                           LinkTypeOut, index);
        chr = consume_char(&filters);
        index++;
    } while (chr == ',' || chr == ';');

    head = inout;
    for (; inout != NULL; inout = inout->next) {
        if (inout->instance == -1)
            continue; // Already processed

        if (!strcmp(inout->name, "in")) {
            if (!has_in)
                goto fail;
            ret->inputs = av_mallocz(sizeof(AVFilterGraphDescExport));
            ret->inputs->filter = inout->instance;
            ret->inputs->pad = inout->pad_idx;
        } else if (!strcmp(inout->name, "out")) {
            has_out = 1;
            ret->outputs = av_mallocz(sizeof(AVFilterGraphDescExport));
            ret->outputs->filter = inout->instance;
            ret->outputs->pad = inout->pad_idx;
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
            linkn = av_mallocz(sizeof(AVFilterGraphDescLink));

            linkn->src = src->instance;
            linkn->srcpad = src->pad_idx;
            linkn->dst = dst->instance;
            linkn->dstpad = dst->pad_idx;

            *linkp = linkn;
            linkp = &linkn->next;

            src->instance = -1;
            dst->instance = -1;
        }
    }

    free_inout(head);

    if (!has_in) {
        ret->inputs = av_mallocz(sizeof(AVFilterGraphDescExport));
        ret->inputs->filter = 0;
    }
    if (!has_out) {
        ret->outputs = av_mallocz(sizeof(AVFilterGraphDescExport));
        ret->outputs->filter = index-1;
    }

    return ret;

 fail:
    free_inout(head);

    avfilter_graph_free_desc(ret);
    return NULL;
}

/**
 * Parse a string describing a filter graph.
 */
int avfilter_graph_parse_chain(AVFilterGraph *graph, const char *filters, AVFilterContext *in, int inpad, AVFilterContext *out, int outpad)
{
    AVFilterGraphDesc *desc;

    /* Try first to parse supposing there is no (in) element */
    if (!(desc = parse_chain(filters, 0))) {
        /* If it didn't work, parse supposing there is an (in) element */
        desc = parse_chain(filters, 1);
    }
    if (!desc)
        return -1;

    if (graph_load_from_desc3(graph, desc, in, inpad, out, outpad) < 0) {
        avfilter_graph_free_desc(desc);
        return -1;
    }

    avfilter_graph_free_desc(desc);
    return 0;
}
