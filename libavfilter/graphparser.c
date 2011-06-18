/*
 * filter graph parser
 * Copyright (c) 2008 Vitor Sessak
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

#include <ctype.h>
#include <string.h>

#include "libavutil/avstring.h"
#include "avfilter.h"
#include "avfiltergraph.h"

#define WHITESPACES " \n\t"

/**
 * Link two filters together.
 *
 * @see avfilter_link()
 */
static int link_filter(AVFilterContext *src, int srcpad,
                       AVFilterContext *dst, int dstpad,
                       void *log_ctx)
{
    int ret;
    if ((ret = avfilter_link(src, srcpad, dst, dstpad))) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Cannot create the link %s:%d -> %s:%d\n",
               src->filter->name, srcpad, dst->filter->name, dstpad);
        return ret;
    }

    return 0;
}

/**
 * Parse the name of a link, which has the format "[linkname]".
 *
 * @return a pointer (that need to be freed after use) to the name
 * between parenthesis
 */
static char *parse_link_name(const char **buf, void *log_ctx)
{
    const char *start = *buf;
    char *name;
    (*buf)++;

    name = av_get_token(buf, "]");

    if (!name[0]) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Bad (empty?) label found in the following: \"%s\".\n", start);
        goto fail;
    }

    if (*(*buf)++ != ']') {
        av_log(log_ctx, AV_LOG_ERROR,
               "Mismatched '[' found in the following: \"%s\".\n", start);
    fail:
        av_freep(&name);
    }

    return name;
}

/**
 * Create an instance of a filter, initialize and insert it in the
 * filtergraph in *ctx.
 *
 * @param ctx the filtergraph context
 * @param put here a filter context in case of successful creation and configuration, NULL otherwise.
 * @param index an index which is supposed to be unique for each filter instance added to the filtergraph
 * @param filt_name the name of the filter to create
 * @param args the arguments provided to the filter during its initialization
 * @param log_ctx the log context to use
 * @return 0 in case of success, a negative AVERROR code otherwise
 */
static int create_filter(AVFilterContext **filt_ctx, AVFilterGraph *ctx, int index,
                         const char *filt_name, const char *args, void *log_ctx)
{
    AVFilter *filt;
    char inst_name[30];
    char tmp_args[256];
    int ret;

    snprintf(inst_name, sizeof(inst_name), "Parsed filter %d %s", index, filt_name);

    filt = avfilter_get_by_name(filt_name);

    if (!filt) {
        av_log(log_ctx, AV_LOG_ERROR,
               "No such filter: '%s'\n", filt_name);
        return AVERROR(EINVAL);
    }

    ret = avfilter_open(filt_ctx, filt, inst_name);
    if (!*filt_ctx) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error creating filter '%s'\n", filt_name);
        return ret;
    }

    if ((ret = avfilter_graph_add_filter(ctx, *filt_ctx)) < 0) {
        avfilter_free(*filt_ctx);
        return ret;
    }

    if (!strcmp(filt_name, "scale") && args && !strstr(args, "flags")) {
        snprintf(tmp_args, sizeof(tmp_args), "%s:%s",
                 args, ctx->scale_sws_opts);
        args = tmp_args;
    }

    if ((ret = avfilter_init_filter(*filt_ctx, args, NULL)) < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error initializing filter '%s' with args '%s'\n", filt_name, args);
        return ret;
    }

    return 0;
}

/**
 * Parse a string of the form FILTER_NAME[=PARAMS], and create a
 * corresponding filter instance which is added to graph with
 * create_filter().
 *
 * @param filt_ctx put here a pointer to the created filter context on
 * success, NULL otherwise
 * @param buf pointer to the buffer to parse, *buf will be updated to
 * point to the char next after the parsed string
 * @param index an index which is assigned to the created filter
 * instance, and which is supposed to be unique for each filter
 * instance added to the filtergraph
 * @return 0 in case of success, a negative AVERROR code otherwise
 */
static int parse_filter(AVFilterContext **filt_ctx, const char **buf, AVFilterGraph *graph,
                        int index, void *log_ctx)
{
    char *opts = NULL;
    char *name = av_get_token(buf, "=,;[\n");
    int ret;

    if (**buf == '=') {
        (*buf)++;
        opts = av_get_token(buf, "[],;\n");
    }

    ret = create_filter(filt_ctx, graph, index, name, opts, log_ctx);
    av_free(name);
    av_free(opts);
    return ret;
}

AVFilterInOut *avfilter_inout_alloc(void)
{
    return av_mallocz(sizeof(AVFilterInOut));
}

void avfilter_inout_free(AVFilterInOut **inout)
{
    while (*inout) {
        AVFilterInOut *next = (*inout)->next;
        av_freep(&(*inout)->name);
        av_freep(inout);
        *inout = next;
    }
}

static AVFilterInOut *extract_inout(const char *label, AVFilterInOut **links)
{
    AVFilterInOut *ret;

    while (*links && strcmp((*links)->name, label))
        links = &((*links)->next);

    ret = *links;

    if (ret)
        *links = ret->next;

    return ret;
}

static void insert_inout(AVFilterInOut **inouts, AVFilterInOut *element)
{
    element->next = *inouts;
    *inouts = element;
}

static int link_filter_inouts(AVFilterContext *filt_ctx,
                              AVFilterInOut **curr_inputs,
                              AVFilterInOut **open_inputs, void *log_ctx)
{
    int pad = filt_ctx->input_count, ret;

    while (pad--) {
        AVFilterInOut *p = *curr_inputs;
        if (!p) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Not enough inputs specified for the \"%s\" filter.\n",
                   filt_ctx->filter->name);
            return AVERROR(EINVAL);
        }

        *curr_inputs = (*curr_inputs)->next;

        if (p->filter_ctx) {
            if ((ret = link_filter(p->filter_ctx, p->pad_idx, filt_ctx, pad, log_ctx)) < 0)
                return ret;
            av_free(p->name);
            av_free(p);
        } else {
            p->filter_ctx = filt_ctx;
            p->pad_idx = pad;
            insert_inout(open_inputs, p);
        }
    }

    if (*curr_inputs) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Too many inputs specified for the \"%s\" filter.\n",
               filt_ctx->filter->name);
        return AVERROR(EINVAL);
    }

    pad = filt_ctx->output_count;
    while (pad--) {
        AVFilterInOut *currlinkn = av_mallocz(sizeof(AVFilterInOut));
        if (!currlinkn)
            return AVERROR(ENOMEM);
        currlinkn->filter_ctx  = filt_ctx;
        currlinkn->pad_idx = pad;
        insert_inout(curr_inputs, currlinkn);
    }

    return 0;
}

static int parse_inputs(const char **buf, AVFilterInOut **curr_inputs,
                        AVFilterInOut **open_outputs, void *log_ctx)
{
    int pad = 0;

    while (**buf == '[') {
        char *name = parse_link_name(buf, log_ctx);
        AVFilterInOut *match;

        if (!name)
            return AVERROR(EINVAL);

        /* First check if the label is not in the open_outputs list */
        match = extract_inout(name, open_outputs);

        if (match) {
            av_free(name);
        } else {
            /* Not in the list, so add it as an input */
            if (!(match = av_mallocz(sizeof(AVFilterInOut))))
                return AVERROR(ENOMEM);
            match->name    = name;
            match->pad_idx = pad;
        }

        insert_inout(curr_inputs, match);

        *buf += strspn(*buf, WHITESPACES);
        pad++;
    }

    return pad;
}

static int parse_outputs(const char **buf, AVFilterInOut **curr_inputs,
                         AVFilterInOut **open_inputs,
                         AVFilterInOut **open_outputs, void *log_ctx)
{
    int ret, pad = 0;

    while (**buf == '[') {
        char *name = parse_link_name(buf, log_ctx);
        AVFilterInOut *match;

        AVFilterInOut *input = *curr_inputs;
        if (!input) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "No output pad can be associated to link label '%s'.\n",
                   name);
            return AVERROR(EINVAL);
        }
        *curr_inputs = (*curr_inputs)->next;

        if (!name)
            return AVERROR(EINVAL);

        /* First check if the label is not in the open_inputs list */
        match = extract_inout(name, open_inputs);

        if (match) {
            if ((ret = link_filter(input->filter_ctx, input->pad_idx,
                                   match->filter_ctx, match->pad_idx, log_ctx)) < 0)
                return ret;
            av_free(match->name);
            av_free(name);
            av_free(match);
            av_free(input);
        } else {
            /* Not in the list, so add the first input as a open_output */
            input->name = name;
            insert_inout(open_outputs, input);
        }
        *buf += strspn(*buf, WHITESPACES);
        pad++;
    }

    return pad;
}

int avfilter_graph_parse(AVFilterGraph *graph, const char *filters,
                         AVFilterInOut **open_inputs, AVFilterInOut **open_outputs,
                         void *log_ctx)
{
    int index = 0, ret;
    char chr = 0;

    AVFilterInOut *curr_inputs = NULL;

    do {
        AVFilterContext *filter;
        const char *filterchain = filters;
        filters += strspn(filters, WHITESPACES);

        if ((ret = parse_inputs(&filters, &curr_inputs, open_outputs, log_ctx)) < 0)
            goto fail;

        if ((ret = parse_filter(&filter, &filters, graph, index, log_ctx)) < 0)
            goto fail;

        if (filter->input_count == 1 && !curr_inputs && !index) {
            /* First input can be omitted if it is "[in]" */
            const char *tmp = "[in]";
            if ((ret = parse_inputs(&tmp, &curr_inputs, open_outputs, log_ctx)) < 0)
                goto fail;
        }

        if ((ret = link_filter_inouts(filter, &curr_inputs, open_inputs, log_ctx)) < 0)
            goto fail;

        if ((ret = parse_outputs(&filters, &curr_inputs, open_inputs, open_outputs,
                                 log_ctx)) < 0)
            goto fail;

        filters += strspn(filters, WHITESPACES);
        chr = *filters++;

        if (chr == ';' && curr_inputs) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Invalid filterchain containing an unlabelled output pad: \"%s\"\n",
                   filterchain);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        index++;
    } while (chr == ',' || chr == ';');

    if (chr) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Unable to parse graph description substring: \"%s\"\n",
               filters - 1);
        ret = AVERROR(EINVAL);
        goto fail;
    }

    if (open_inputs && *open_inputs && !strcmp((*open_inputs)->name, "out") && curr_inputs) {
        /* Last output can be omitted if it is "[out]" */
        const char *tmp = "[out]";
        if ((ret = parse_outputs(&tmp, &curr_inputs, open_inputs, open_outputs,
                                 log_ctx)) < 0)
            goto fail;
    }

    return 0;

 fail:
    for (; graph->filter_count > 0; graph->filter_count--)
        avfilter_free(graph->filters[graph->filter_count - 1]);
    av_freep(&graph->filters);
    avfilter_inout_free(open_inputs);
    avfilter_inout_free(open_outputs);
    avfilter_inout_free(&curr_inputs);
    return ret;
}
