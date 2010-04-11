/*
 * Filter graphs
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

#ifndef AVFILTER_AVFILTERGRAPH_H
#define AVFILTER_AVFILTERGRAPH_H

#include "avfilter.h"

typedef struct AVFilterGraph {
    unsigned filter_count;
    AVFilterContext **filters;

    char *scale_sws_opts; ///< sws options to use for the auto-inserted scale filters
} AVFilterGraph;

/**
 * Gets a filter instance with name name from graph.
 *
 * @return the pointer to the found filter instance or NULL if it
 * cannot be found.
 */
AVFilterContext *avfilter_graph_get_filter(AVFilterGraph *graph, char *name);

/**
 * Adds an existing filter instance to a filter graph.
 *
 * @param graph  the filter graph
 * @param filter the filter to be added
 */
int avfilter_graph_add_filter(AVFilterGraph *graphctx, AVFilterContext *filter);

/**
 * Checks for the validity of graph.
 *
 * A graph is considered valid if all its input and output pads are
 * connected.
 *
 * @return 0 in case of success, a negative value otherwise
 */
int avfilter_graph_check_validity(AVFilterGraph *graphctx, AVClass *log_ctx);

/**
 * Configures all the links of graphctx.
 *
 * @return 0 in case of success, a negative value otherwise
 */
int avfilter_graph_config_links(AVFilterGraph *graphctx, AVClass *log_ctx);

/**
 * Configures the formats of all the links in the graph.
 */
int avfilter_graph_config_formats(AVFilterGraph *graphctx, AVClass *log_ctx);

/**
 * Frees a graph and destroys its links.
 */
void avfilter_graph_destroy(AVFilterGraph *graph);

#endif  /* AVFILTER_AVFILTERGRAPH_H */
