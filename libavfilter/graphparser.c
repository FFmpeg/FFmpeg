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

#include "graphparser.h"
#include "avfilter.h"
#include "avfiltergraph.h"

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

static int consume_whitespace(const char *buf)
{
    return strspn(buf, " \n\t");
}

/**
 * Consumes a string from *buf.
 * @return a copy of the consumed string, which should be free'd after use
 */
static char *consume_string(const char **buf)
{
    char *out = av_malloc(strlen(*buf) + 1);
    char *ret = out;

    *buf += consume_whitespace(*buf);

    do{
        char c = *(*buf)++;
        switch (c) {
        case '\\':
            *out++ = *(*buf)++;
            break;
        case '\'':
            while(**buf && **buf != '\'')
                *out++ = *(*buf)++;
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
            *out++ = 0;
            break;
        default:
            *out++ = c;
        }
    } while(out[-1]);

    (*buf)--;
    *buf += consume_whitespace(*buf);

    return ret;
}

/**
 * Parse "[linkname]"
 * @param name a pointer (that need to be free'd after use) to the name between
 *        parenthesis
 */
static char *parse_link_name(const char **buf, AVClass *log_ctx)
{
    const char *start = *buf;
    char *name;
    (*buf)++;

    name = consume_string(buf);

    if(!name[0]) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Bad (empty?) label found in the following: \"%s\".\n", start);
        goto fail;
    }

    if(*(*buf)++ != ']') {
        av_log(log_ctx, AV_LOG_ERROR,
               "Mismatched '[' found in the following: \"%s\".\n", start);
    fail:
        av_freep(&name);
    }

    return name;
}

static AVFilterContext *create_filter(AVFilterGraph *ctx, int index,
                                      const char *filt_name, const char *args,
                                      AVClass *log_ctx)
{
    AVFilterContext *filt_ctx;

    AVFilter *filt;
    char inst_name[30];

    snprintf(inst_name, sizeof(inst_name), "Parsed filter %d", index);

    filt = avfilter_get_by_name(filt_name);

    if(!filt) {
        av_log(log_ctx, AV_LOG_ERROR,
               "no such filter: '%s'\n", filt_name);
        return NULL;
    }

    filt_ctx = avfilter_open(filt, inst_name);
    if(!filt_ctx) {
        av_log(log_ctx, AV_LOG_ERROR,
               "error creating filter '%s'\n", filt_name);
        return NULL;
    }

    if(avfilter_graph_add_filter(ctx, filt_ctx) < 0) {
        avfilter_destroy(filt_ctx);
        return NULL;
    }

    if(avfilter_init_filter(filt_ctx, args, NULL)) {
        av_log(log_ctx, AV_LOG_ERROR,
               "error initializing filter '%s' with args '%s'\n", filt_name, args);
        return NULL;
    }

    return filt_ctx;
}

/**
 * Parse "filter=params"
 */
static AVFilterContext *parse_filter(const char **buf, AVFilterGraph *graph,
                                     int index, AVClass *log_ctx)
{
    char *opts = NULL;
    char *name = consume_string(buf);
    AVFilterContext *ret;

    if(**buf == '=') {
        (*buf)++;
        opts = consume_string(buf);
    }

    ret = create_filter(graph, index, name, opts, log_ctx);
    av_free(name);
    av_free(opts);
    return ret;
}

static void free_inout(AVFilterInOut *head)
{
    while(head) {
        AVFilterInOut *next = head->next;
        av_free(head->name);
        av_free(head);
        head = next;
    }
}

static AVFilterInOut *extract_inout(const char *label, AVFilterInOut **links)
{
    AVFilterInOut *ret;

    while(*links && strcmp((*links)->name, label))
        links = &((*links)->next);

    ret = *links;

    if(ret)
        *links = ret->next;

    return ret;
}

static void insert_inout(AVFilterInOut **inouts, AVFilterInOut *element)
{
    element->next = *inouts;
    *inouts = element;
}

static int link_filter_inouts(AVFilterContext *filter,
                              AVFilterInOut **curr_inputs,
                              AVFilterInOut **open_inputs, AVClass *log_ctx)
{
    int pad = filter->input_count;

    while(pad--) {
        AVFilterInOut *p = *curr_inputs;
        if(!p) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Not enough inputs specified for the \"%s\" filter.\n",
                   filter->filter->name);
            return -1;
        }

        *curr_inputs = (*curr_inputs)->next;

        if(p->filter) {
            if(link_filter(p->filter, p->pad_idx, filter, pad, log_ctx))
                return -1;
            av_free(p->name);
            av_free(p);
        } else {
            p->filter = filter;
            p->pad_idx = pad;
            insert_inout(open_inputs, p);
        }
    }

    if(*curr_inputs) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Too many inputs specified for the \"%s\" filter.\n",
               filter->filter->name);
        return -1;
    }

    pad = filter->output_count;
    while(pad--) {
        AVFilterInOut *currlinkn = av_mallocz(sizeof(AVFilterInOut));
        currlinkn->filter  = filter;
        currlinkn->pad_idx = pad;
        insert_inout(curr_inputs, currlinkn);
    }

    return 0;
}

static int parse_inputs(const char **buf, AVFilterInOut **curr_inputs,
                        AVFilterInOut **open_outputs, AVClass *log_ctx)
{
    int pad = 0;

    while(**buf == '[') {
        char *name = parse_link_name(buf, log_ctx);
        AVFilterInOut *match;

        if(!name)
            return -1;

        /* First check if the label is not in the open_outputs list */
        match = extract_inout(name, open_outputs);

        if(match) {
            av_free(name);
        } else {
            /* Not in the list, so add it as an input */
            match = av_mallocz(sizeof(AVFilterInOut));
            match->name    = name;
            match->pad_idx = pad;
        }

        insert_inout(curr_inputs, match);

        *buf += consume_whitespace(*buf);
        pad++;
    }

    return pad;
}

static int parse_outputs(const char **buf, AVFilterInOut **curr_inputs,
                         AVFilterInOut **open_inputs,
                         AVFilterInOut **open_outputs, AVClass *log_ctx)
{
    int pad = 0;

    while(**buf == '[') {
        char *name = parse_link_name(buf, log_ctx);
        AVFilterInOut *match;

        AVFilterInOut *input = *curr_inputs;
        *curr_inputs = (*curr_inputs)->next;

        if(!name)
            return -1;

        /* First check if the label is not in the open_inputs list */
        match = extract_inout(name, open_inputs);

        if(match) {
            if(link_filter(input->filter, input->pad_idx,
                           match->filter, match->pad_idx, log_ctx) < 0)
                return -1;
            av_free(match->name);
            av_free(name);
            av_free(match);
            av_free(input);
        } else {
            /* Not in the list, so add the first input as a open_output */
            input->name = name;
            insert_inout(open_outputs, input);
        }
        *buf += consume_whitespace(*buf);
        pad++;
    }

    return pad;
}

int avfilter_graph_parse(AVFilterGraph *graph, const char *filters,
                         AVFilterInOut *open_inputs,
                         AVFilterInOut *open_outputs, AVClass *log_ctx)
{
    int index = 0;
    char chr = 0;

    AVFilterInOut *curr_inputs = NULL;

    do {
        AVFilterContext *filter;
        filters += consume_whitespace(filters);

        if(parse_inputs(&filters, &curr_inputs, &open_outputs, log_ctx) < 0)
            goto fail;

        filter = parse_filter(&filters, graph, index, log_ctx);

        if(!filter)
            goto fail;

        if(filter->input_count == 1 && !curr_inputs && !index) {
            /* First input can be omitted if it is "[in]" */
            const char *tmp = "[in]";
            if(parse_inputs(&tmp, &curr_inputs, &open_outputs, log_ctx) < 0)
                goto fail;
        }

        if(link_filter_inouts(filter, &curr_inputs, &open_inputs, log_ctx) < 0)
            goto fail;

        if(parse_outputs(&filters, &curr_inputs, &open_inputs, &open_outputs,
                         log_ctx) < 0)
            goto fail;

        filters += consume_whitespace(filters);
        chr = *filters++;

        if(chr == ';' && curr_inputs) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Could not find a output to link when parsing \"%s\"\n",
                   filters - 1);
            goto fail;
        }
        index++;
    } while(chr == ',' || chr == ';');

    if (chr) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Unable to parse graph description substring: \"%s\"\n",
               filters - 1);
        goto fail;
    }

    if(open_inputs && !strcmp(open_inputs->name, "out") && curr_inputs) {
        /* Last output can be omitted if it is "[out]" */
        const char *tmp = "[out]";
        if(parse_outputs(&tmp, &curr_inputs, &open_inputs,
                         &open_outputs, log_ctx) < 0)
            goto fail;
    }

    return 0;

 fail:
    avfilter_graph_destroy(graph);
    free_inout(open_inputs);
    free_inout(open_outputs);
    free_inout(curr_inputs);
    return -1;
}
