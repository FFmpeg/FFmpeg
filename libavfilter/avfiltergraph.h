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

/**
 * Add an existing filter instance to a filter graph.
 * @param graph  The filter graph
 * @param filter The filter to be added
 */
void avfilter_graph_add_filter(AVFilterContext *graphctx, AVFilterContext *filter);

/**
 * Configure the colorspace, resolution, etc of all links in the graph
 */
int avfilter_graph_config_links(AVFilterContext *graphctx);

#endif  /* FFMPEG_AVFILTER_H */
