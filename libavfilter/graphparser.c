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

static AVFilterContext *create_filter(AVFilterGraph *ctx, int index,
                                      const char *name, const char *args,
                                      AVClass *log_ctx)
{
    AVFilterContext *filt;

    AVFilter *filterdef;
    char inst_name[30];

    snprintf(inst_name, sizeof(inst_name), "Parsed filter %d", index);

    if(!(filterdef = avfilter_get_by_name(name))) {
        av_log(log_ctx, AV_LOG_ERROR,
               "no such filter: '%s'\n", name);
        return NULL;
    }

    if(!(filt = avfilter_open(filterdef, inst_name))) {
        av_log(log_ctx, AV_LOG_ERROR,
               "error creating filter '%s'\n", name);
        return NULL;
    }

    if(avfilter_graph_add_filter(ctx, filt) < 0)
        return NULL;

    if(avfilter_init_filter(filt, args, NULL)) {
        av_log(log_ctx, AV_LOG_ERROR,
               "error initializing filter '%s' with args '%s'\n", name, args);
        return NULL;
    }

    return filt;
}

static int link_filter(AVFilterContext *src, int srcpad,
                       AVFilterContext *dst, int dstpad,
                       AVClass *log_ctx)
{
    if(avfilter_link(src, srcpad, dst, dstpad)) {
        av_log(log_ctx, AV_LOG_ERROR,
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
        case ';':
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
static void parse_link_name(const char **buf, char **name, AVClass *log_ctx)
{
    const char *start = *buf;
    (*buf)++;

    *name = consume_string(buf);

    if(!*name[0]) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Bad (empty?) label found in the following: \"%s\".\n", start);
        goto fail;
    }

    if(*(*buf)++ != ']') {
        av_log(log_ctx, AV_LOG_ERROR,
               "Mismatched '[' found in the following: \"%s\".\n", start);
    fail:
        av_freep(name);
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
    const char *name;
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

static AVFilterInOut *extract_inout(const char *label, AVFilterInOut **links)
{
    AVFilterInOut *ret;


    while(*links && strcmp((*links)->name, label))
        links= &((*links)->next);

    ret= *links;

    if(ret)
        *links= ret->next;

    return ret;
}


static int link_filter_inouts(AVFilterContext *filter,
                              AVFilterInOut **currInputs,
                              AVFilterInOut **openLinks, AVClass *log_ctx)
{
    AVFilterInOut *p;
    int pad = 0;

    pad = filter->input_count;
    while(pad--) {
        p = *currInputs;
        if(!p) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Not enough inputs specified for the \"%s\" filter.\n",
                   filter->name);
            return -1;
        }

        if(p->filter) {
            if(link_filter(p->filter, p->pad_idx, filter, pad, log_ctx))
                return -1;
            *currInputs = (*currInputs)->next;
            av_free(p);
        } else {
            *currInputs = (*currInputs)->next;
            p->filter = filter;
            p->pad_idx = pad;
            p->next = *openLinks;
            *openLinks = p;
        }
    }


    if(*currInputs) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Too many inputs specified for the \"%s\" filter.\n",
               filter->name);
        return -1;
    }

    pad = filter->output_count;
    while(pad--) {
        AVFilterInOut *currlinkn = av_malloc(sizeof(AVFilterInOut));
        currlinkn->name    = NULL;
        currlinkn->type    = LinkTypeOut;
        currlinkn->filter  = filter;
        currlinkn->pad_idx = pad;
        currlinkn->next    = *currInputs;
        *currInputs = currlinkn;
    }

    return 0;
}

/**
 * Parse "filter=params"
 * @arg name a pointer (that need to be free'd after use) to the name of the
 *           filter
 * @arg ars  a pointer (that need to be free'd after use) to the args of the
 *           filter
 */
static AVFilterContext *parse_filter(const char **buf, AVFilterGraph *graph,
                                     int index, AVClass *log_ctx)
{
    char *opts;
    char *name = consume_string(buf);

    if(**buf == '=') {
        (*buf)++;
        opts = consume_string(buf);
    } else {
        opts = NULL;
    }

    return create_filter(graph, index, name, opts, log_ctx);
}

static int parse_inputs(const char **buf, AVFilterInOut **currInputs,
                        AVFilterInOut **openLinks, AVClass *log_ctx)
{
    int pad = 0;
    AVFilterInOut *p;

    while (**buf == '[') {
        char *name;

        parse_link_name(buf, &name, log_ctx);

        if(!name)
            return -1;

        /* First check if the label is not in the openLinks list */
        p = extract_inout(name, openLinks);

        if(p) {
            /* A label of a open link. Make it one of the inputs of the next
               filter */
            AVFilterInOut *currlinkn = p;
            if (p->type != LinkTypeOut) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Label \"%s\" appears twice as input!\n", p->name);
                return -1;
            }
            currlinkn->next = *currInputs;
            *currInputs = currlinkn;
        } else {
            /* Not in the list, so add it as an input */
            AVFilterInOut *currlinkn = av_malloc(sizeof(AVFilterInOut));

            currlinkn->name    = name;
            currlinkn->type    = LinkTypeIn;
            currlinkn->filter  = NULL;
            currlinkn->pad_idx = pad;
            currlinkn->next    = *currInputs;
            *currInputs = currlinkn;
        }
        consume_whitespace(buf);
        pad++;
    }

    return pad;
}

static int parse_outputs(const char **buf, AVFilterInOut **currInputs,
                         AVFilterInOut **openLinks, AVClass *log_ctx)
{
    int pad = 0;

    while (**buf == '[') {
        char *name;
        AVFilterInOut *match;

        parse_link_name(buf, &name, log_ctx);

        if(!name)
            return -1;

        /* First check if the label is not in the openLinks list */
        match = extract_inout(name, openLinks);

        if(match) {
            /* A label of a open link. Link it. */
            AVFilterInOut *p = *currInputs;
            if (match->type != LinkTypeIn) {
                av_log(log_ctx, AV_LOG_ERROR,
                       "Label \"%s\" appears twice as output!\n", match->name);
                return -1;
            }

            *currInputs = (*currInputs)->next;
            if(link_filter(p->filter, p->pad_idx,
                           match->filter, match->pad_idx, log_ctx) < 0)
                return -1;
            av_free(match);
            av_free(p);
        } else {
            /* Not in the list, so add the first input as a openLink */
            AVFilterInOut *p = *currInputs;
            *currInputs = (*currInputs)->next;
            p->next = *openLinks;
            p->type = LinkTypeOut;
            p->name = name;
            *openLinks = p;
        }
        consume_whitespace(buf);
        pad++;
    }

    return pad;
}

/**
 * Parse a string describing a filter graph.
 */
int avfilter_parse_graph(AVFilterGraph *graph, const char *filters,
                         AVFilterContext *in, int inpad,
                         AVFilterContext *out, int outpad,
                         AVClass *log_ctx)
{
    int index = 0;
    char chr = 0;
    int pad = 0;

    AVFilterInOut *currInputs=NULL;
    AVFilterInOut *openLinks  = av_malloc(sizeof(AVFilterInOut));

    openLinks->name = "in";
    openLinks->filter = in;
    openLinks->type = LinkTypeOut;
    openLinks->pad_idx = inpad;
    openLinks->next = av_malloc(sizeof(AVFilterInOut));

    openLinks->next->name = "out";
    openLinks->next->filter = out;
    openLinks->next->type = LinkTypeIn;
    openLinks->next->pad_idx = outpad;
    openLinks->next->next = NULL;

    do {
        AVFilterContext *filter;
        consume_whitespace(&filters);

        pad = parse_inputs(&filters, &currInputs, &openLinks, log_ctx);

        if(pad < 0)
            goto fail;

        if(!(filter = parse_filter(&filters, graph, index, log_ctx)))
            goto fail;

        if(filter->input_count == 1 && !currInputs && !index) {
            // First input can be ommitted if it is "[in]"
            const char *tmp = "[in]";
            pad = parse_inputs(&tmp, &currInputs, &openLinks, log_ctx);
            if (pad < 0)
                goto fail;
        }

        if(link_filter_inouts(filter, &currInputs, &openLinks, log_ctx) < 0)
            goto fail;

        pad = parse_outputs(&filters, &currInputs, &openLinks, log_ctx);

        if(pad < 0)
            goto fail;

        consume_whitespace(&filters);
        chr = *filters++;

        if (chr == ';' && currInputs) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Could not find a output to link when parsing \"%s\"\n",
                   filters - 1);
            goto fail;
        }
        index++;
    } while (chr == ',' || chr == ';');

    if(openLinks && !strcmp(openLinks->name, "out") && currInputs) {
        // Last output can be ommitted if it is "[out]"
        const char *tmp = "[out]";
        if(parse_outputs(&tmp, &currInputs, &openLinks, log_ctx) < 0)
            goto fail;
    }

    return 0;

 fail:
    avfilter_destroy_graph(graph);
    free_inout(openLinks);
    free_inout(currInputs);
    return -1;
}
