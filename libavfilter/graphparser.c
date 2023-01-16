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

#include <string.h>
#include <stdio.h>

#include "libavutil/avstring.h"
#include "libavutil/dict.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"

#include "avfilter.h"
#include "internal.h"

#define WHITESPACES " \n\t\r"

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
    if (!name)
        return NULL;

    if (!name[0]) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Bad (empty?) label found in the following: \"%s\".\n", start);
        goto fail;
    }

    if (**buf != ']') {
        av_log(log_ctx, AV_LOG_ERROR,
               "Mismatched '[' found in the following: \"%s\".\n", start);
    fail:
        av_freep(&name);
        return NULL;
    }
    (*buf)++;

    return name;
}

/**
 * Create an instance of a filter, initialize and insert it in the
 * filtergraph in *ctx.
 *
 * @param filt_ctx put here a filter context in case of successful creation and configuration, NULL otherwise.
 * @param ctx the filtergraph context
 * @param index an index which is supposed to be unique for each filter instance added to the filtergraph
 * @param name the name of the filter to create, can be filter name or filter_name\@id as instance name
 * @param args the arguments provided to the filter during its initialization
 * @param log_ctx the log context to use
 * @return >= 0 in case of success, a negative AVERROR code otherwise
 */
static int create_filter(AVFilterContext **filt_ctx, AVFilterGraph *ctx, int index,
                         const char *name, const char *args, void *log_ctx)
{
    const AVFilter *filt;
    char name2[30];
    const char *inst_name = NULL, *filt_name = NULL;
    int ret, k;

    av_strlcpy(name2, name, sizeof(name2));

    for (k = 0; name2[k]; k++) {
        if (name2[k] == '@' && name[k+1]) {
            name2[k] = 0;
            inst_name = name;
            filt_name = name2;
            break;
        }
    }

    if (!inst_name) {
        snprintf(name2, sizeof(name2), "Parsed_%s_%d", name, index);
        inst_name = name2;
        filt_name = name;
    }

    filt = avfilter_get_by_name(filt_name);

    if (!filt) {
        av_log(log_ctx, AV_LOG_ERROR,
               "No such filter: '%s'\n", filt_name);
        return AVERROR(EINVAL);
    }

    *filt_ctx = avfilter_graph_alloc_filter(ctx, filt, inst_name);
    if (!*filt_ctx) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error creating filter '%s'\n", filt_name);
        return AVERROR(ENOMEM);
    }

    if (!strcmp(filt_name, "scale") && ctx->scale_sws_opts) {
        ret = av_set_options_string(*filt_ctx, ctx->scale_sws_opts, "=", ":");
        if (ret < 0)
            return ret;
    }

    ret = avfilter_init_str(*filt_ctx, args);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error initializing filter '%s'", filt_name);
        if (args)
            av_log(log_ctx, AV_LOG_ERROR, " with args '%s'", args);
        av_log(log_ctx, AV_LOG_ERROR, "\n");
        avfilter_free(*filt_ctx);
        *filt_ctx = NULL;
    }

    return ret;
}

/**
 * Parse a string of the form FILTER_NAME[=PARAMS], and create a
 * corresponding filter instance which is added to graph with
 * create_filter().
 *
 * @param filt_ctx Pointer that is set to the created and configured filter
 *                 context on success, set to NULL on failure.
 * @param filt_ctx put here a pointer to the created filter context on
 * success, NULL otherwise
 * @param buf pointer to the buffer to parse, *buf will be updated to
 * point to the char next after the parsed string
 * @param index an index which is assigned to the created filter
 * instance, and which is supposed to be unique for each filter
 * instance added to the filtergraph
 * @return >= 0 in case of success, a negative AVERROR code otherwise
 */
static int parse_filter(AVFilterContext **filt_ctx, const char **buf, AVFilterGraph *graph,
                        int index, void *log_ctx)
{
    char *opts = NULL;
    char *name = av_get_token(buf, "=,;[");
    int ret;

    if (!name)
        return AVERROR(ENOMEM);

    if (**buf == '=') {
        (*buf)++;
        opts = av_get_token(buf, "[],;");
        if (!opts) {
            av_free(name);
            return AVERROR(ENOMEM);
        }
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

    while (*links && (!(*links)->name || strcmp((*links)->name, label)))
        links = &((*links)->next);

    ret = *links;

    if (ret) {
        *links = ret->next;
        ret->next = NULL;
    }

    return ret;
}

static void insert_inout(AVFilterInOut **inouts, AVFilterInOut *element)
{
    element->next = *inouts;
    *inouts = element;
}

static void append_inout(AVFilterInOut **inouts, AVFilterInOut **element)
{
    while (*inouts && (*inouts)->next)
        inouts = &((*inouts)->next);

    if (!*inouts)
        *inouts = *element;
    else
        (*inouts)->next = *element;
    *element = NULL;
}

static int link_filter_inouts(AVFilterContext *filt_ctx,
                              AVFilterInOut **curr_inputs,
                              AVFilterInOut **open_inputs, void *log_ctx)
{
    int pad, ret;

    for (pad = 0; pad < filt_ctx->nb_inputs; pad++) {
        AVFilterInOut *p = *curr_inputs;

        if (p) {
            *curr_inputs = (*curr_inputs)->next;
            p->next = NULL;
        } else if (!(p = av_mallocz(sizeof(*p))))
            return AVERROR(ENOMEM);

        if (p->filter_ctx) {
            ret = link_filter(p->filter_ctx, p->pad_idx, filt_ctx, pad, log_ctx);
            av_freep(&p->name);
            av_freep(&p);
            if (ret < 0)
                return ret;
        } else {
            p->filter_ctx = filt_ctx;
            p->pad_idx = pad;
            append_inout(open_inputs, &p);
        }
    }

    if (*curr_inputs) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Too many inputs specified for the \"%s\" filter.\n",
               filt_ctx->filter->name);
        return AVERROR(EINVAL);
    }

    pad = filt_ctx->nb_outputs;
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
    AVFilterInOut *parsed_inputs = NULL;
    int pad = 0;

    while (**buf == '[') {
        char *name = parse_link_name(buf, log_ctx);
        AVFilterInOut *match;

        if (!name) {
            avfilter_inout_free(&parsed_inputs);
            return AVERROR(EINVAL);
        }

        /* First check if the label is not in the open_outputs list */
        match = extract_inout(name, open_outputs);

        if (match) {
            av_free(name);
        } else {
            /* Not in the list, so add it as an input */
            if (!(match = av_mallocz(sizeof(AVFilterInOut)))) {
                avfilter_inout_free(&parsed_inputs);
                av_free(name);
                return AVERROR(ENOMEM);
            }
            match->name    = name;
            match->pad_idx = pad;
        }

        append_inout(&parsed_inputs, &match);

        *buf += strspn(*buf, WHITESPACES);
        pad++;
    }

    append_inout(&parsed_inputs, curr_inputs);
    *curr_inputs = parsed_inputs;

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

        if (!name)
            return AVERROR(EINVAL);

        if (!input) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "No output pad can be associated to link label '%s'.\n", name);
            av_free(name);
            return AVERROR(EINVAL);
        }
        *curr_inputs = (*curr_inputs)->next;

        /* First check if the label is not in the open_inputs list */
        match = extract_inout(name, open_inputs);

        if (match) {
            ret = link_filter(input->filter_ctx, input->pad_idx,
                              match->filter_ctx, match->pad_idx, log_ctx);
            av_freep(&match->name);
            av_freep(&name);
            av_freep(&match);
            av_freep(&input);
            if (ret < 0)
                return ret;
        } else {
            /* Not in the list, so add the first input as an open_output */
            input->name = name;
            insert_inout(open_outputs, input);
        }
        *buf += strspn(*buf, WHITESPACES);
        pad++;
    }

    return pad;
}

static int parse_sws_flags(const char **buf, char **dst, void *log_ctx)
{
    char *p = strchr(*buf, ';');

    if (strncmp(*buf, "sws_flags=", 10))
        return 0;

    if (!p) {
        av_log(log_ctx, AV_LOG_ERROR, "sws_flags not terminated with ';'.\n");
        return AVERROR(EINVAL);
    }

    *buf += 4;  // keep the 'flags=' part

    av_freep(dst);
    if (!(*dst = av_mallocz(p - *buf + 1)))
        return AVERROR(ENOMEM);
    av_strlcpy(*dst, *buf, p - *buf + 1);

    *buf = p + 1;
    return 0;
}

int avfilter_graph_parse2(AVFilterGraph *graph, const char *filters,
                          AVFilterInOut **inputs,
                          AVFilterInOut **outputs)
{
    int index = 0, ret = 0;
    char chr = 0;

    AVFilterInOut *curr_inputs = NULL, *open_inputs = NULL, *open_outputs = NULL;

    filters += strspn(filters, WHITESPACES);

    if ((ret = parse_sws_flags(&filters, &graph->scale_sws_opts, graph)) < 0)
        goto end;

    do {
        AVFilterContext *filter;
        filters += strspn(filters, WHITESPACES);

        if ((ret = parse_inputs(&filters, &curr_inputs, &open_outputs, graph)) < 0)
            goto end;
        if ((ret = parse_filter(&filter, &filters, graph, index, graph)) < 0)
            goto end;


        if ((ret = link_filter_inouts(filter, &curr_inputs, &open_inputs, graph)) < 0)
            goto end;

        if ((ret = parse_outputs(&filters, &curr_inputs, &open_inputs, &open_outputs,
                                 graph)) < 0)
            goto end;

        filters += strspn(filters, WHITESPACES);
        chr = *filters++;

        if (chr == ';' && curr_inputs)
            append_inout(&open_outputs, &curr_inputs);
        index++;
    } while (chr == ',' || chr == ';');

    if (chr) {
        av_log(graph, AV_LOG_ERROR,
               "Unable to parse graph description substring: \"%s\"\n",
               filters - 1);
        ret = AVERROR(EINVAL);
        goto end;
    }

    append_inout(&open_outputs, &curr_inputs);


    *inputs  = open_inputs;
    *outputs = open_outputs;
    return 0;

end:
    while (graph->nb_filters)
        avfilter_free(graph->filters[0]);
    av_freep(&graph->filters);
    avfilter_inout_free(&open_inputs);
    avfilter_inout_free(&open_outputs);
    avfilter_inout_free(&curr_inputs);

    *inputs  = NULL;
    *outputs = NULL;

    return ret;
}

int avfilter_graph_parse(AVFilterGraph *graph, const char *filters,
                         AVFilterInOut *open_inputs,
                         AVFilterInOut *open_outputs, void *log_ctx)
{
    int ret;
    AVFilterInOut *cur, *match, *inputs = NULL, *outputs = NULL;

    if ((ret = avfilter_graph_parse2(graph, filters, &inputs, &outputs)) < 0)
        goto fail;

    /* First input can be omitted if it is "[in]" */
    if (inputs && !inputs->name)
        inputs->name = av_strdup("in");
    for (cur = inputs; cur; cur = cur->next) {
        if (!cur->name) {
              av_log(log_ctx, AV_LOG_ERROR,
                     "Not enough inputs specified for the \"%s\" filter.\n",
                     cur->filter_ctx->filter->name);
              ret = AVERROR(EINVAL);
              goto fail;
        }
        if (!(match = extract_inout(cur->name, &open_outputs)))
            continue;
        ret = avfilter_link(match->filter_ctx, match->pad_idx,
                            cur->filter_ctx,   cur->pad_idx);
        avfilter_inout_free(&match);
        if (ret < 0)
            goto fail;
    }

    /* Last output can be omitted if it is "[out]" */
    if (outputs && !outputs->name)
        outputs->name = av_strdup("out");
    for (cur = outputs; cur; cur = cur->next) {
        if (!cur->name) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Invalid filterchain containing an unlabelled output pad: \"%s\"\n",
                   filters);
            ret = AVERROR(EINVAL);
            goto fail;
        }
        if (!(match = extract_inout(cur->name, &open_inputs)))
            continue;
        ret = avfilter_link(cur->filter_ctx,   cur->pad_idx,
                            match->filter_ctx, match->pad_idx);
        avfilter_inout_free(&match);
        if (ret < 0)
            goto fail;
    }

 fail:
    if (ret < 0) {
        while (graph->nb_filters)
            avfilter_free(graph->filters[0]);
        av_freep(&graph->filters);
    }
    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&open_inputs);
    avfilter_inout_free(&open_outputs);
    return ret;
}

int avfilter_graph_parse_ptr(AVFilterGraph *graph, const char *filters,
                         AVFilterInOut **open_inputs_ptr, AVFilterInOut **open_outputs_ptr,
                         void *log_ctx)
{
    int index = 0, ret = 0;
    char chr = 0;

    AVFilterInOut *curr_inputs = NULL;
    AVFilterInOut *open_inputs  = open_inputs_ptr  ? *open_inputs_ptr  : NULL;
    AVFilterInOut *open_outputs = open_outputs_ptr ? *open_outputs_ptr : NULL;

    if ((ret = parse_sws_flags(&filters, &graph->scale_sws_opts, graph)) < 0)
        goto end;

    do {
        AVFilterContext *filter;
        const char *filterchain = filters;
        filters += strspn(filters, WHITESPACES);

        if ((ret = parse_inputs(&filters, &curr_inputs, &open_outputs, log_ctx)) < 0)
            goto end;

        if ((ret = parse_filter(&filter, &filters, graph, index, log_ctx)) < 0)
            goto end;

        if (filter->nb_inputs == 1 && !curr_inputs && !index) {
            /* First input pad, assume it is "[in]" if not specified */
            const char *tmp = "[in]";
            if ((ret = parse_inputs(&tmp, &curr_inputs, &open_outputs, log_ctx)) < 0)
                goto end;
        }

        if ((ret = link_filter_inouts(filter, &curr_inputs, &open_inputs, log_ctx)) < 0)
            goto end;

        if ((ret = parse_outputs(&filters, &curr_inputs, &open_inputs, &open_outputs,
                                 log_ctx)) < 0)
            goto end;

        filters += strspn(filters, WHITESPACES);
        chr = *filters++;

        if (chr == ';' && curr_inputs) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "Invalid filterchain containing an unlabelled output pad: \"%s\"\n",
                   filterchain);
            ret = AVERROR(EINVAL);
            goto end;
        }
        index++;
    } while (chr == ',' || chr == ';');

    if (chr) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Unable to parse graph description substring: \"%s\"\n",
               filters - 1);
        ret = AVERROR(EINVAL);
        goto end;
    }

    if (curr_inputs) {
        /* Last output pad, assume it is "[out]" if not specified */
        const char *tmp = "[out]";
        if ((ret = parse_outputs(&tmp, &curr_inputs, &open_inputs, &open_outputs,
                                 log_ctx)) < 0)
            goto end;
    }

end:
    /* clear open_in/outputs only if not passed as parameters */
    if (open_inputs_ptr) *open_inputs_ptr = open_inputs;
    else avfilter_inout_free(&open_inputs);
    if (open_outputs_ptr) *open_outputs_ptr = open_outputs;
    else avfilter_inout_free(&open_outputs);
    avfilter_inout_free(&curr_inputs);

    if (ret < 0) {
        while (graph->nb_filters)
            avfilter_free(graph->filters[0]);
        av_freep(&graph->filters);
    }
    return ret;
}

static void pad_params_free(AVFilterPadParams **pfpp)
{
    AVFilterPadParams *fpp = *pfpp;

    if (!fpp)
        return;

    av_freep(&fpp->label);

    av_freep(pfpp);
}

static void filter_params_free(AVFilterParams **pp)
{
    AVFilterParams *p = *pp;

    if (!p)
        return;

    for (unsigned i = 0; i < p->nb_inputs; i++)
        pad_params_free(&p->inputs[i]);
    av_freep(&p->inputs);

    for (unsigned i = 0; i < p->nb_outputs; i++)
        pad_params_free(&p->outputs[i]);
    av_freep(&p->outputs);

    av_dict_free(&p->opts);

    av_freep(&p->filter_name);
    av_freep(&p->instance_name);

    av_freep(pp);
}

static void chain_free(AVFilterChain **pch)
{
    AVFilterChain *ch = *pch;

    if (!ch)
        return;

    for (size_t i = 0; i < ch->nb_filters; i++)
        filter_params_free(&ch->filters[i]);
    av_freep(&ch->filters);

    av_freep(pch);
}

void avfilter_graph_segment_free(AVFilterGraphSegment **pseg)
{
    AVFilterGraphSegment *seg = *pseg;

    if (!seg)
        return;

    for (size_t i = 0; i < seg->nb_chains; i++)
        chain_free(&seg->chains[i]);
    av_freep(&seg->chains);

    av_freep(&seg->scale_sws_opts);

    av_freep(pseg);
}

static int linklabels_parse(void *logctx, const char **linklabels,
                            AVFilterPadParams ***res, unsigned *nb_res)
{
    AVFilterPadParams **pp = NULL;
    int nb = 0;
    int ret;

    while (**linklabels == '[') {
        char *label;
        AVFilterPadParams *par;

        label = parse_link_name(linklabels, logctx);
        if (!label) {
            ret = AVERROR(EINVAL);
            goto fail;
        }

        par = av_mallocz(sizeof(*par));
        if (!par) {
            av_freep(&label);
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        par->label = label;

        ret = av_dynarray_add_nofree(&pp, &nb, par);
        if (ret < 0) {
            pad_params_free(&par);
            goto fail;
        }

        *linklabels += strspn(*linklabels, WHITESPACES);
    }

    *res    = pp;
    *nb_res = nb;

    return 0;
fail:
    for (unsigned i = 0; i < nb; i++)
        pad_params_free(&pp[i]);
    av_freep(&pp);
    return ret;
}

static int filter_parse(void *logctx, const char **filter,
                        AVFilterParams **pp)
{
    AVFilterParams *p;
    char *inst_name;
    int ret;

    p = av_mallocz(sizeof(*p));
    if (!p)
        return AVERROR(ENOMEM);

    ret = linklabels_parse(logctx, filter, &p->inputs, &p->nb_inputs);
    if (ret < 0)
        goto fail;

    p->filter_name = av_get_token(filter, "=,;[");
    if (!p->filter_name) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    inst_name = strchr(p->filter_name, '@');
    if (inst_name) {
        *inst_name++ = 0;
        p->instance_name = av_strdup(inst_name);
        if (!p->instance_name) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }
    }

    if (**filter == '=') {
        const AVFilter *f = avfilter_get_by_name(p->filter_name);
        char *opts;

        (*filter)++;

        opts = av_get_token(filter, "[],;");
        if (!opts) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        ret = ff_filter_opt_parse(logctx, f ? f->priv_class : NULL,
                                  &p->opts, opts);
        av_freep(&opts);
        if (ret < 0)
            goto fail;
    }

    ret = linklabels_parse(logctx, filter, &p->outputs, &p->nb_outputs);
    if (ret < 0)
        goto fail;

    *filter += strspn(*filter, WHITESPACES);

    *pp = p;
    return 0;
fail:
    av_log(logctx, AV_LOG_ERROR,
           "Error parsing a filter description around: %s\n", *filter);
    filter_params_free(&p);
    return ret;
}

static int chain_parse(void *logctx, const char **pchain,
                       AVFilterChain **pch)
{
    const char *chain = *pchain;
    AVFilterChain *ch;
    int ret, nb_filters = 0;

    *pch = NULL;

    ch = av_mallocz(sizeof(*ch));
    if (!ch)
        return AVERROR(ENOMEM);

    while (*chain) {
        AVFilterParams *p;
        char chr;

        ret = filter_parse(logctx, &chain, &p);
        if (ret < 0)
            goto fail;

        ret = av_dynarray_add_nofree(&ch->filters, &nb_filters, p);
        if (ret < 0) {
            filter_params_free(&p);
            goto fail;
        }
        ch->nb_filters = nb_filters;

        // a filter ends with one of: , ; end-of-string
        chr = *chain;
        if (chr && chr != ',' && chr != ';') {
            av_log(logctx, AV_LOG_ERROR,
                   "Trailing garbage after a filter: %s\n", chain);
            ret = AVERROR(EINVAL);
            goto fail;
        }

        if (chr) {
            chain++;
            chain += strspn(chain, WHITESPACES);

            if (chr == ';')
                break;
        }
    }

    *pchain = chain;
    *pch    = ch;

    return 0;
fail:
    av_log(logctx, AV_LOG_ERROR,
           "Error parsing filterchain '%s' around: %s\n", *pchain, chain);
    chain_free(&ch);
    return ret;
}

int avfilter_graph_segment_parse(AVFilterGraph *graph, const char *graph_str,
                                 int flags, AVFilterGraphSegment **pseg)
{
    AVFilterGraphSegment *seg;
    int ret, nb_chains = 0;

    *pseg = NULL;

    if (flags)
        return AVERROR(ENOSYS);

    seg = av_mallocz(sizeof(*seg));
    if (!seg)
        return AVERROR(ENOMEM);

    seg->graph = graph;

    graph_str += strspn(graph_str, WHITESPACES);

    ret = parse_sws_flags(&graph_str, &seg->scale_sws_opts, &graph);
    if (ret < 0)
        goto fail;

    graph_str += strspn(graph_str, WHITESPACES);

    while (*graph_str) {
        AVFilterChain *ch;

        ret = chain_parse(graph, &graph_str, &ch);
        if (ret < 0)
            goto fail;

        ret = av_dynarray_add_nofree(&seg->chains, &nb_chains, ch);
        if (ret < 0) {
            chain_free(&ch);
            goto fail;
        }
        seg->nb_chains = nb_chains;

        graph_str += strspn(graph_str, WHITESPACES);
    }

    if (!seg->nb_chains) {
        av_log(graph, AV_LOG_ERROR, "No filters specified in the graph description\n");
        ret = AVERROR(EINVAL);
        goto fail;
    }

    *pseg = seg;

    return 0;
fail:
    avfilter_graph_segment_free(&seg);
    return ret;
}

int avfilter_graph_segment_create_filters(AVFilterGraphSegment *seg, int flags)
{
    size_t idx = 0;

    if (flags)
        return AVERROR(ENOSYS);

    if (seg->scale_sws_opts) {
        av_freep(&seg->graph->scale_sws_opts);
        seg->graph->scale_sws_opts = av_strdup(seg->scale_sws_opts);
        if (!seg->graph->scale_sws_opts)
            return AVERROR(ENOMEM);
    }

    for (size_t i = 0; i < seg->nb_chains; i++) {
        AVFilterChain *ch = seg->chains[i];

        for (size_t j = 0; j < ch->nb_filters; j++) {
            AVFilterParams *p = ch->filters[j];
            const AVFilter *f = avfilter_get_by_name(p->filter_name);
            char inst_name[30], *name = p->instance_name ? p->instance_name :
                                                           inst_name;

            // skip already processed filters
            if (p->filter || !p->filter_name)
                continue;

            if (!f) {
                av_log(seg->graph, AV_LOG_ERROR,
                       "No such filter: '%s'\n", p->filter_name);
                return AVERROR_FILTER_NOT_FOUND;
            }

            if (!p->instance_name)
                snprintf(inst_name, sizeof(inst_name), "Parsed_%s_%zu", f->name, idx);

            p->filter = avfilter_graph_alloc_filter(seg->graph, f, name);
            if (!p->filter)
                return AVERROR(ENOMEM);

            if (!strcmp(f->name, "scale") && seg->graph->scale_sws_opts) {
                int ret = av_set_options_string(p->filter, seg->graph->scale_sws_opts,
                                                "=", ":");
                if (ret < 0) {
                    avfilter_free(p->filter);
                    p->filter = NULL;
                    return ret;
                }
            }

            av_freep(&p->filter_name);
            av_freep(&p->instance_name);

            idx++;
        }
    }

    return 0;
}

static int fail_creation_pending(AVFilterGraphSegment *seg, const char *fn,
                                 const char *func)
{
    av_log(seg->graph, AV_LOG_ERROR,
           "A creation-pending filter '%s' present in the segment. All filters "
           "must be created or disabled before calling %s().\n", fn, func);
    return AVERROR(EINVAL);
}

int avfilter_graph_segment_apply_opts(AVFilterGraphSegment *seg, int flags)
{
    int ret, leftover_opts = 0;

    if (flags)
        return AVERROR(ENOSYS);

    for (size_t i = 0; i < seg->nb_chains; i++) {
        AVFilterChain *ch = seg->chains[i];

        for (size_t j = 0; j < ch->nb_filters; j++) {
            AVFilterParams *p = ch->filters[j];

            if (p->filter_name)
                return fail_creation_pending(seg, p->filter_name, __func__);
            if (!p->filter || !p->opts)
                continue;

            ret = av_opt_set_dict2(p->filter, &p->opts, AV_OPT_SEARCH_CHILDREN);
            if (ret < 0)
                return ret;

            if (av_dict_count(p->opts))
                leftover_opts = 1;
        }
    }

    return leftover_opts ? AVERROR_OPTION_NOT_FOUND : 0;
}

int avfilter_graph_segment_init(AVFilterGraphSegment *seg, int flags)
{
    if (flags)
        return AVERROR(ENOSYS);

    for (size_t i = 0; i < seg->nb_chains; i++) {
        AVFilterChain *ch = seg->chains[i];

        for (size_t j = 0; j < ch->nb_filters; j++) {
            AVFilterParams *p = ch->filters[j];
            int ret;

            if (p->filter_name)
                return fail_creation_pending(seg, p->filter_name, __func__);
            if (!p->filter || p->filter->internal->initialized)
                continue;

            ret = avfilter_init_dict(p->filter, NULL);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static unsigned
find_linklabel(AVFilterGraphSegment *seg, const char *label,
               int output, size_t idx_chain, size_t idx_filter,
               AVFilterParams **pp)
{
    for (; idx_chain < seg->nb_chains; idx_chain++) {
        AVFilterChain *ch = seg->chains[idx_chain];

        for (; idx_filter < ch->nb_filters; idx_filter++) {
            AVFilterParams *p = ch->filters[idx_filter];
            AVFilterPadParams **io = output ? p->outputs    : p->inputs;
            unsigned         nb_io = output ? p->nb_outputs : p->nb_inputs;
            AVFilterLink **l;
            unsigned nb_l;

            if (!p->filter)
                continue;

            l    = output ? p->filter->outputs    : p->filter->inputs;
            nb_l = output ? p->filter->nb_outputs : p->filter->nb_inputs;

            for (unsigned i = 0; i < FFMIN(nb_io, nb_l); i++)
                if (!l[i] && io[i]->label && !strcmp(io[i]->label, label)) {
                    *pp = p;
                    return i;
                }
        }

        idx_filter = 0;
    }

    *pp = NULL;
    return 0;
}

static int inout_add(AVFilterInOut **inouts, AVFilterContext *f, unsigned pad_idx,
                     const char *label)
{
    AVFilterInOut *io = av_mallocz(sizeof(*io));

    if (!io)
        return AVERROR(ENOMEM);

    io->filter_ctx = f;
    io->pad_idx    = pad_idx;

    if (label) {
        io->name = av_strdup(label);
        if (!io->name) {
            avfilter_inout_free(&io);
            return AVERROR(ENOMEM);
        }
    }

    append_inout(inouts, &io);

    return 0;
}

static int link_inputs(AVFilterGraphSegment *seg, size_t idx_chain,
                       size_t idx_filter, AVFilterInOut **inputs)
{
    AVFilterChain  *ch = seg->chains[idx_chain];
    AVFilterParams  *p = ch->filters[idx_filter];
    AVFilterContext *f = p->filter;

    int ret;

    if (f->nb_inputs < p->nb_inputs) {
        av_log(seg->graph, AV_LOG_ERROR,
               "More input link labels specified for filter '%s' than "
               "it has inputs: %u > %d\n", f->filter->name,
               p->nb_inputs, f->nb_inputs);
        return AVERROR(EINVAL);
    }

    for (unsigned in = 0; in < f->nb_inputs; in++) {
        const char *label = (in < p->nb_inputs) ? p->inputs[in]->label : NULL;

        // skip already linked inputs
        if (f->inputs[in])
            continue;

        if (label) {
            AVFilterParams *po = NULL;
            unsigned idx = find_linklabel(seg, label, 1, idx_chain, idx_filter, &po);

            if (po) {
                ret = avfilter_link(po->filter, idx, f, in);
                if (ret < 0)
                    return ret;

                continue;
            }
        }

        ret = inout_add(inputs, f, in, label);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int link_outputs(AVFilterGraphSegment *seg, size_t idx_chain,
                        size_t idx_filter, AVFilterInOut **outputs)
{
    AVFilterChain  *ch = seg->chains[idx_chain];
    AVFilterParams  *p = ch->filters[idx_filter];
    AVFilterContext *f = p->filter;

    int ret;

    if (f->nb_outputs < p->nb_outputs) {
        av_log(seg->graph, AV_LOG_ERROR,
               "More output link labels specified for filter '%s' than "
               "it has outputs: %u > %d\n", f->filter->name,
               p->nb_outputs, f->nb_outputs);
        return AVERROR(EINVAL);
    }
    for (unsigned out = 0; out < f->nb_outputs; out++) {
        char *label = (out < p->nb_outputs) ? p->outputs[out]->label : NULL;

        // skip already linked outputs
        if (f->outputs[out])
            continue;

        if (label) {
            AVFilterParams *po = NULL;
            unsigned idx = find_linklabel(seg, label, 0, idx_chain, idx_filter, &po);

            if (po) {
                ret = avfilter_link(f, out, po->filter, idx);
                if (ret < 0)
                    return ret;

                continue;
            }
        }

        // if this output is unlabeled, try linking it to an unlabeled
        // input in the next non-disabled filter in the chain
        for (size_t i = idx_filter + 1; i < ch->nb_filters && !label; i++) {
            AVFilterParams *p_next = ch->filters[i];

            if (!p_next->filter)
                continue;

            for (unsigned in = 0; in < p_next->filter->nb_inputs; in++) {
                if (!p_next->filter->inputs[in] &&
                    (in >= p_next->nb_inputs || !p_next->inputs[in]->label)) {
                    ret = avfilter_link(f, out, p_next->filter, in);
                    if (ret < 0)
                        return ret;

                    goto cont;
                }
            }
            break;
        }

        ret = inout_add(outputs, f, out, label);
        if (ret < 0)
            return ret;

cont:;
    }

    return 0;
}

int avfilter_graph_segment_link(AVFilterGraphSegment *seg, int flags,
                                AVFilterInOut **inputs,
                                AVFilterInOut **outputs)
{
    int ret;

    *inputs  = NULL;
    *outputs = NULL;

    if (flags)
        return AVERROR(ENOSYS);

    for (size_t idx_chain = 0; idx_chain < seg->nb_chains; idx_chain++) {
        AVFilterChain *ch = seg->chains[idx_chain];

        for (size_t idx_filter = 0; idx_filter < ch->nb_filters; idx_filter++) {
            AVFilterParams  *p = ch->filters[idx_filter];

            if (p->filter_name) {
                ret = fail_creation_pending(seg, p->filter_name, __func__);
                goto fail;
            }

            if (!p->filter)
                continue;

            ret = link_inputs(seg, idx_chain, idx_filter, inputs);
            if (ret < 0)
                goto fail;

            ret = link_outputs(seg, idx_chain, idx_filter, outputs);
            if (ret < 0)
                goto fail;
        }
    }
    return 0;
fail:
    avfilter_inout_free(inputs);
    avfilter_inout_free(outputs);
    return ret;
}

int avfilter_graph_segment_apply(AVFilterGraphSegment *seg, int flags,
                                 AVFilterInOut **inputs,
                                 AVFilterInOut **outputs)
{
    int ret;

    if (flags)
        return AVERROR(ENOSYS);

    ret = avfilter_graph_segment_create_filters(seg, 0);
    if (ret < 0) {
        av_log(seg->graph, AV_LOG_ERROR, "Error creating filters\n");
        return ret;
    }

    ret = avfilter_graph_segment_apply_opts(seg, 0);
    if (ret < 0) {
        av_log(seg->graph, AV_LOG_ERROR, "Error applying filter options\n");
        return ret;
    }

    ret = avfilter_graph_segment_init(seg, 0);
    if (ret < 0) {
        av_log(seg->graph, AV_LOG_ERROR, "Error initializing filters\n");
        return ret;
    }

    ret = avfilter_graph_segment_link(seg, 0, inputs, outputs);
    if (ret < 0) {
        av_log(seg->graph, AV_LOG_ERROR, "Error linking filters\n");
        return ret;
    }

    return 0;
}
