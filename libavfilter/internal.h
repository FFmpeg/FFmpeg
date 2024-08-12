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

#include "libavutil/internal.h"
#include "avfilter.h"

/**
 * Returns true if a pixel format is "regular YUV", which includes all pixel
 * formats that are affected by YUV colorspace negotiation.
 */
int ff_fmt_is_regular_yuv(enum AVPixelFormat fmt);

/* Functions to parse audio format arguments */

/**
 * Parse a pixel format.
 *
 * @param ret pixel format pointer to where the value should be written
 * @param arg string to parse
 * @param log_ctx log context
 * @return >= 0 in case of success, a negative AVERROR code on error
 */
av_warn_unused_result
int ff_parse_pixel_format(enum AVPixelFormat *ret, const char *arg, void *log_ctx);

/**
 * Parse a sample rate.
 *
 * @param ret unsigned integer pointer to where the value should be written
 * @param arg string to parse
 * @param log_ctx log context
 * @return >= 0 in case of success, a negative AVERROR code on error
 */
av_warn_unused_result
int ff_parse_sample_rate(int *ret, const char *arg, void *log_ctx);

/**
 * Parse a channel layout or a corresponding integer representation.
 *
 * @param ret 64bit integer pointer to where the value should be written.
 * @param nret integer pointer to the number of channels;
 *             if not NULL, then unknown channel layouts are accepted
 * @param arg string to parse
 * @param log_ctx log context
 * @return >= 0 in case of success, a negative AVERROR code on error
 */
av_warn_unused_result
int ff_parse_channel_layout(AVChannelLayout *ret, int *nret, const char *arg,
                            void *log_ctx);

/**
 * Negotiate the media format, dimensions, etc of all inputs to a filter.
 *
 * @param filter the filter to negotiate the properties for its inputs
 * @return       zero on successful negotiation
 */
int ff_filter_config_links(AVFilterContext *filter);

/* misc trace functions */

#define FF_TPRINTF_START(ctx, func) ff_tlog(NULL, "%-16s: ", #func)

#ifdef TRACE
void ff_tlog_link(void *ctx, AVFilterLink *link, int end);
#else
#define ff_tlog_link(ctx, link, end) do { } while(0)
#endif

/**
 * Run one round of processing on a filter graph.
 */
int ff_filter_graph_run_once(AVFilterGraph *graph);

#endif /* AVFILTER_INTERNAL_H */
