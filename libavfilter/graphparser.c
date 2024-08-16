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
#include "avfilter_internal.h"
#include "filters.h"

#define WHITESPACES " \n\t\r"

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
    AVFilterGraphSegment *seg;
    int ret;

    ret = avfilter_graph_segment_parse(graph, filters, 0, &seg);
    if (ret < 0)
        return ret;

    ret = avfilter_graph_segment_apply(seg, 0, inputs, outputs);
    avfilter_graph_segment_free(&seg);
    if (ret < 0)
        goto end;

    return 0;

end:
    while (graph->nb_filters)
        avfilter_free(graph->filters[0]);
    av_freep(&graph->filters);

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

    ret = parse_sws_flags(&graph_str, &seg->scale_sws_opts, graph);
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
            char name[64];

            // skip already processed filters
            if (p->filter || !p->filter_name)
                continue;

            if (!f) {
                av_log(seg->graph, AV_LOG_ERROR,
                       "No such filter: '%s'\n", p->filter_name);
                return AVERROR_FILTER_NOT_FOUND;
            }

            if (!p->instance_name)
                snprintf(name, sizeof(name), "Parsed_%s_%zu", f->name, idx);
            else
                snprintf(name, sizeof(name), "%s@%s", f->name, p->instance_name);

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
            if (!p->filter || fffilterctx(p->filter)->initialized)
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

// print an error message if some options were not found
static void log_unknown_opt(const AVFilterGraphSegment *seg)
{
    for (size_t i = 0; i < seg->nb_chains; i++) {
        const AVFilterChain *ch = seg->chains[i];

        for (size_t j = 0; j < ch->nb_filters; j++) {
            const AVFilterParams *p = ch->filters[j];
            const AVDictionaryEntry *e;

            if (!p->filter)
                continue;

            e = av_dict_iterate(p->opts, NULL);

            if (e) {
                av_log(p->filter, AV_LOG_ERROR,
                       "Could not set non-existent option '%s' to value '%s'\n",
                       e->key, e->value);
                return;
            }
        }
    }

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
        if (ret == AVERROR_OPTION_NOT_FOUND)
            log_unknown_opt(seg);
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

int avfilter_graph_parse_ptr(AVFilterGraph *graph, const char *filters,
                         AVFilterInOut **open_inputs_ptr, AVFilterInOut **open_outputs_ptr,
                         void *log_ctx)
{
    AVFilterInOut *user_inputs  = open_inputs_ptr  ? *open_inputs_ptr  : NULL;
    AVFilterInOut *user_outputs = open_outputs_ptr ? *open_outputs_ptr : NULL;

    AVFilterInOut *inputs = NULL, *outputs = NULL;
    AVFilterGraphSegment *seg = NULL;
    AVFilterChain         *ch;
    AVFilterParams         *p;
    int ret;

    ret = avfilter_graph_segment_parse(graph, filters, 0, &seg);
    if (ret < 0)
        goto end;

    ret = avfilter_graph_segment_create_filters(seg, 0);
    if (ret < 0)
        goto end;

    ret = avfilter_graph_segment_apply_opts(seg, 0);
    if (ret < 0) {
        if (ret == AVERROR_OPTION_NOT_FOUND)
            log_unknown_opt(seg);
        goto end;
    }

    ret = avfilter_graph_segment_init(seg, 0);
    if (ret < 0)
        goto end;

    /* First input pad, assume it is "[in]" if not specified */
    p = seg->chains[0]->filters[0];
    if (p->filter->nb_inputs == 1 && !p->inputs) {
        const char *tmp = "[in]";

        ret = linklabels_parse(graph, &tmp, &p->inputs, &p->nb_inputs);
        if (ret < 0)
            goto end;
    }

    /* Last output pad, assume it is "[out]" if not specified */
    ch = seg->chains[seg->nb_chains - 1];
    p = ch->filters[ch->nb_filters - 1];
    if (p->filter->nb_outputs == 1 && !p->outputs) {
        const char *tmp = "[out]";

        ret = linklabels_parse(graph, &tmp, &p->outputs, &p->nb_outputs);
        if (ret < 0)
            goto end;
    }

    ret = avfilter_graph_segment_apply(seg, 0, &inputs, &outputs);
    avfilter_graph_segment_free(&seg);
    if (ret < 0)
        goto end;

    // process user-supplied inputs/outputs
    while (inputs) {
        AVFilterInOut *cur, *match = NULL;

        cur       = inputs;
        inputs    = cur->next;
        cur->next = NULL;

        if (cur->name)
            match = extract_inout(cur->name, &user_outputs);

        if (match) {
            ret = avfilter_link(match->filter_ctx, match->pad_idx,
                                cur->filter_ctx, cur->pad_idx);
            avfilter_inout_free(&match);
            avfilter_inout_free(&cur);
            if (ret < 0)
                goto end;
        } else
            append_inout(&user_inputs, &cur);
    }
    while (outputs) {
        AVFilterInOut *cur, *match = NULL;

        cur       = outputs;
        outputs   = cur->next;
        cur->next = NULL;

        if (cur->name)
            match = extract_inout(cur->name, &user_inputs);

        if (match) {
            ret = avfilter_link(cur->filter_ctx, cur->pad_idx,
                                match->filter_ctx, match->pad_idx);
            avfilter_inout_free(&match);
            avfilter_inout_free(&cur);
            if (ret < 0)
                goto end;
        } else
            append_inout(&user_outputs, &cur);
    }

end:
    avfilter_graph_segment_free(&seg);

    if (ret < 0) {
        av_log(graph, AV_LOG_ERROR, "Error processing filtergraph: %s\n",
               av_err2str(ret));

        while (graph->nb_filters)
            avfilter_free(graph->filters[0]);
        av_freep(&graph->filters);
    }

    /* clear open_in/outputs only if not passed as parameters */
    if (open_inputs_ptr) *open_inputs_ptr = user_inputs;
    else avfilter_inout_free(&user_inputs);
    if (open_outputs_ptr) *open_outputs_ptr = user_outputs;
    else avfilter_inout_free(&user_outputs);

    avfilter_inout_free(&inputs);
    avfilter_inout_free(&outputs);

    return ret;
}
