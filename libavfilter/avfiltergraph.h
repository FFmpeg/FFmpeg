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

#ifndef FFMPEG_AVFILTER_GRAPH_H
#define FFMPEG_AVFILTER_GRAPH_H

#include "avfilter.h"

typedef struct AVFilterGraph AVFilterGraph;

/**
 * Create a new filter graph
 */
AVFilterGraph *avfilter_create_graph(void);

/**
 * Destroy a filter graph, and any filters in it.
 * @param graph The filter graph to destroy
 */
void avfilter_destroy_graph(AVFilterGraph *graph);

/**
 * Add an existing filter instance to a filter graph.
 * @param graph  The filter graph
 * @param filter The filter to be added
 */
void avfilter_graph_add_filter(AVFilterGraph *graph, AVFilterContext *filter);

/**
 * Loads the filter graph with a simple chain described by filters.
 * @param graph   The filter graph to load filters into
 * @param count   The number of filters to be created
 * @param filters_list An array of strings describing the filters to be created.
 *                The format of each string is "name=params".
 * @param first   If non-NULL, will be set to the first filter in the chain.
 * @param last    If non-NULL, will be set to the last filter in the chain.
 * @return 0 on success.  -1 on error.
 */
int avfilter_graph_load_chain(AVFilterGraph *graph,
                              unsigned count, char **filter_list,
                              AVFilterContext **first, AVFilterContext **last);
#endif  /* FFMPEG_AVFILTER_H */
