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
#include "formats.h"

#define POOL_SIZE 32
typedef struct AVFilterPool {
    AVFilterBufferRef *pic[POOL_SIZE];
    int count;
    int refcount;
    int draining;
} AVFilterPool;

typedef struct AVFilterCommand {
    double time;                ///< time expressed in seconds
    char *command;              ///< command
    char *arg;                  ///< optional argument for the command
    int flags;
    struct AVFilterCommand *next;
} AVFilterCommand;

/**
 * Update the position of a link in the age heap.
 */
void ff_avfilter_graph_update_heap(AVFilterGraph *graph, AVFilterLink *link);

/** default handler for freeing audio/video buffer when there are no references left */
void ff_avfilter_default_free_buffer(AVFilterBuffer *buf);

/** Tell is a format is contained in the provided list terminated by -1. */
int ff_fmt_is_in(int fmt, const int *fmts);

/**
 * Return a copy of a list of integers terminated by -1, or NULL in
 * case of copy failure.
 */
int *ff_copy_int_list(const int * const list);

/**
 * Return a copy of a list of 64-bit integers, or NULL in case of
 * copy failure.
 */
int64_t *ff_copy_int64_list(const int64_t * const list);

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
 * Parse a time base.
 *
 * @param ret unsigned AVRational pointer to where the value should be written
 * @param arg string to parse
 * @param log_ctx log context
 * @return 0 in case of success, a negative AVERROR code on error
 */
int ff_parse_time_base(AVRational *ret, const char *arg, void *log_ctx);

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
 * Pass video frame along and keep an internal reference for later use.
 */
static inline void ff_null_start_frame_keep_ref(AVFilterLink *inlink,
                                                AVFilterBufferRef *picref)
{
    avfilter_start_frame(inlink->dst->outputs[0], avfilter_ref_buffer(picref, ~0));
}

void ff_update_link_current_pts(AVFilterLink *link, int64_t pts);

void ff_free_pool(AVFilterPool *pool);

void ff_command_queue_pop(AVFilterContext *filter);

/* misc debug functions */

#define FF_DPRINTF_START(ctx, func) av_dlog(NULL, "%-16s: ", #func)

char *ff_get_ref_perms_string(char *buf, size_t buf_size, int perms);

void ff_dlog_ref(void *ctx, AVFilterBufferRef *ref, int end);

void ff_dlog_link(void *ctx, AVFilterLink *link, int end);

/**
 * Insert a new pad.
 *
 * @param idx Insertion point. Pad is inserted at the end if this point
 *            is beyond the end of the list of pads.
 * @param count Pointer to the number of pads in the list
 * @param padidx_off Offset within an AVFilterLink structure to the element
 *                   to increment when inserting a new pad causes link
 *                   numbering to change
 * @param pads Pointer to the pointer to the beginning of the list of pads
 * @param links Pointer to the pointer to the beginning of the list of links
 * @param newpad The new pad to add. A copy is made when adding.
 */
void ff_insert_pad(unsigned idx, unsigned *count, size_t padidx_off,
                   AVFilterPad **pads, AVFilterLink ***links,
                   AVFilterPad *newpad);

/** Insert a new input pad for the filter. */
static inline void ff_insert_inpad(AVFilterContext *f, unsigned index,
                                   AVFilterPad *p)
{
    ff_insert_pad(index, &f->input_count, offsetof(AVFilterLink, dstpad),
                  &f->input_pads, &f->inputs, p);
}

/** Insert a new output pad for the filter. */
static inline void ff_insert_outpad(AVFilterContext *f, unsigned index,
                                    AVFilterPad *p)
{
    ff_insert_pad(index, &f->output_count, offsetof(AVFilterLink, srcpad),
                  &f->output_pads, &f->outputs, p);
}

/**
 * Poll a frame from the filter chain.
 *
 * @param  link the input link
 * @return the number of immediately available frames, a negative
 * number in case of error
 */
int ff_poll_frame(AVFilterLink *link);

/**
 * Request an input frame from the filter at the other end of the link.
 *
 * @param link the input link
 * @return     zero on success
 */
int ff_request_frame(AVFilterLink *link);

#endif /* AVFILTER_INTERNAL_H */
