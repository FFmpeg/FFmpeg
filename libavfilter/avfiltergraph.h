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

#ifndef FFMPEG_AVFILTERGRAPH_H
#define FFMPEG_AVFILTERGRAPH_H

#include "avfilter.h"

/** Linked-list of filters to create for an AVFilterGraphDesc */
typedef struct AVFilterGraphDescFilter
{
    char *name;             ///< filter instance name
    char *filter;           ///< name of filter type
    char *args;             ///< filter parameters
    struct AVFilterGraphDescFilter *next;
} AVFilterGraphDescFilter;

/** Linked-list of links between filters */
typedef struct AVFilterGraphDescLink
{
    /* TODO: allow referencing pads by name, not just by index */
    char *src;              ///< name of the source filter
    unsigned srcpad;        ///< index of the output pad on the source filter

    char *dst;              ///< name of the dest filter
    unsigned dstpad;        ///< index of the input pad on the dest filter

    struct AVFilterGraphDescLink *next;
} AVFilterGraphDescLink;

/** Linked-list of filter pads to be exported from the graph */
typedef struct AVFilterGraphDescExport
{
    /* TODO: allow referencing pads by name, not just by index */
    char *name;             ///< name of the exported pad
    char *filter;           ///< name of the filter
    unsigned pad;           ///< index of the pad to be exported

    struct AVFilterGraphDescExport *next;
} AVFilterGraphDescExport;

/** Sections of a filter graph description */
typedef enum
{
    SEC_NONE = 0,
    SEC_FILTERS,
    SEC_LINKS,
    SEC_INPUTS,
    SEC_OUTPUTS
} AVFilterGraphDescSection;

/** Description of a graph to be loaded from a file, etc */
typedef struct
{
    AVFilterGraphDescFilter *filters;   ///< filters in the graph
    AVFilterGraphDescLink   *links;     ///< links between the filters
    AVFilterGraphDescExport *inputs;    ///< inputs to export
    AVFilterGraphDescExport *outputs;   ///< outputs to export
} AVFilterGraphDesc;

typedef struct
{
    AVFilterGraphDescSection section;   ///< current section being parsed

    AVFilterGraphDescFilter **filterp;  ///< last parsed filter
    AVFilterGraphDescLink   **linkp;    ///< last parsed link
    AVFilterGraphDescExport **inputp;   ///< last parsed exported input
    AVFilterGraphDescExport **outputp;  ///< last parsed exported output
} AVFilterGraphDescParser;

extern AVFilter avfilter_vf_graph;
extern AVFilter avfilter_vf_graphfile;
extern AVFilter avfilter_vf_graphdesc;

/**
 * Parse a graph composed of a simple chain of filters which is described by
 * a single string.
 * @param filters String listing filters and their arguments.
 * @return        The parsed graph description.
 */
AVFilterGraphDesc *avfilter_graph_parse_chain(const char *filters);

/** Parse a line of a filter graph description.
 * @param desc   Pointer to an AVFilterGraphDesc pointer. If *desc is NULL,
 *               a new AVFilterGraphDesc structure will be created for you.
 *               Must be the same between multiple invocations when parsing
 *               the same description.
 * @param parser Parser state. Must be the same between multiple invocations
 *               when parsing the same description
 * @param line   Line of the graph description to parse.
 * @return       Zero on success, negative on error.
 */
int avfilter_graph_parse_desc(AVFilterGraphDesc **desc,
                              AVFilterGraphDescParser **parser,
                              char *line);

/**
 * Load a filter graph description from a file.
 * @param filename Name of the file from which to load the description.
 * @return         Pointer to the description on success,  NULL on failure.
 */
AVFilterGraphDesc *avfilter_graph_load_desc(const char *filename);

/**
 * Free a filter graph description.
 * @param desc The graph description to free
 */
void avfilter_graph_free_desc(AVFilterGraphDesc *desc);

/**
 * Add an existing filter instance to a filter graph.
 * @param graph  The filter graph
 * @param filter The filter to be added
 */
void avfilter_graph_add_filter(AVFilterContext *graphctx, AVFilterContext *filter);

/**
 * Configure the formats of all the links in the graph.
 */
int avfilter_graph_config_formats(AVFilterContext *graphctx);

/**
 * Configure the parameters (resolution, etc) of all links in the graph.
 */
int avfilter_graph_config_links(AVFilterContext *graphctx);

#endif  /* FFMPEG_AVFILTERGRAPH_H */
