/*
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

#ifndef AVFILTER_INTERNAL_H
#define AVFILTER_INTERNAL_H

/**
 * @file
 * internal API functions
 */

#include "avfilter.h"
#include "avfiltergraph.h"

#define POOL_SIZE 32
typedef struct AVFilterPool {
    AVFilterBufferRef *pic[POOL_SIZE];
    int count;
} AVFilterPool;

typedef struct AVFilterCommand {
    double time;                ///< time expressed in seconds
    char *command;              ///< command
    char *arg;                  ///< optional argument for the command
    int flags;
    struct AVFilterCommand *next;
} AVFilterCommand;

/**
 * Check for the validity of graph.
 *
 * A graph is considered valid if all its input and output pads are
 * connected.
 *
 * @return 0 in case of success, a negative value otherwise
 */
int ff_avfilter_graph_check_validity(AVFilterGraph *graphctx, AVClass *log_ctx);

/**
 * Configure all the links of graphctx.
 *
 * @return 0 in case of success, a negative value otherwise
 */
int ff_avfilter_graph_config_links(AVFilterGraph *graphctx, AVClass *log_ctx);

/**
 * Configure the formats of all the links in the graph.
 */
int ff_avfilter_graph_config_formats(AVFilterGraph *graphctx, AVClass *log_ctx);

/** default handler for freeing audio/video buffer when there are no references left */
void ff_avfilter_default_free_buffer(AVFilterBuffer *buf);

/** Tell is a format is contained in the provided list terminated by -1. */
int ff_fmt_is_in(int fmt, const int *fmts);

/* Functions to parse audio format arguments */

/**
 * Parse a pixel format.
 *
 * @param ret pixel format pointer to where the value should be written
 * @param arg string to parse
 * @param log_ctx log context
 * @return 0 in case of success, a negative AVERROR code on error
 */
int ff_parse_pixel_format(enum PixelFormat *ret, const char *arg, void *log_ctx);

/**
 * Parse a sample rate.
 *
 * @param ret unsigned integer pointer to where the value should be written
 * @param arg string to parse
 * @param log_ctx log context
 * @return 0 in case of success, a negative AVERROR code on error
 */
int ff_parse_sample_rate(int *ret, const char *arg, void *log_ctx);

/**
 * Parse a sample format name or a corresponding integer representation.
 *
 * @param ret integer pointer to where the value should be written
 * @param arg string to parse
 * @param log_ctx log context
 * @return 0 in case of success, a negative AVERROR code on error
 */
int ff_parse_sample_format(int *ret, const char *arg, void *log_ctx);

/**
 * Parse a channel layout or a corresponding integer representation.
 *
 * @param ret 64bit integer pointer to where the value should be written.
 * @param arg string to parse
 * @param log_ctx log context
 * @return 0 in case of success, a negative AVERROR code on error
 */
int ff_parse_channel_layout(int64_t *ret, const char *arg, void *log_ctx);

/**
 * Parse a packing format or a corresponding integer representation.
 *
 * @param ret integer pointer to where the value should be written
 * @param arg string to parse
 * @param log_ctx log context
 * @return 0 in case of success, a negative AVERROR code on error
 */
int ff_parse_packing_format(int *ret, const char *arg, void *log_ctx);

#endif /* AVFILTER_INTERNAL_H */
