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
#include "avfiltergraph.h"
#include "formats.h"
#include "framepool.h"
#include "framequeue.h"
#include "thread.h"
#include "version.h"
#include "video.h"
#include "libavcodec/avcodec.h"

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

/**
 * A filter pad used for either input or output.
 */
struct AVFilterPad {
    /**
     * Pad name. The name is unique among inputs and among outputs, but an
     * input may have the same name as an output. This may be NULL if this
     * pad has no need to ever be referenced by name.
     */
    const char *name;

    /**
     * AVFilterPad type.
     */
    enum AVMediaType type;

    /**
     * Callback function to get a video buffer. If NULL, the filter system will
     * use ff_default_get_video_buffer().
     *
     * Input video pads only.
     */
    AVFrame *(*get_video_buffer)(AVFilterLink *link, int w, int h);

    /**
     * Callback function to get an audio buffer. If NULL, the filter system will
     * use ff_default_get_audio_buffer().
     *
     * Input audio pads only.
     */
    AVFrame *(*get_audio_buffer)(AVFilterLink *link, int nb_samples);

    /**
     * Filtering callback. This is where a filter receives a frame with
     * audio/video data and should do its processing.
     *
     * Input pads only.
     *
     * @return >= 0 on success, a negative AVERROR on error. This function
     * must ensure that frame is properly unreferenced on error if it
     * hasn't been passed on to another filter.
     */
    int (*filter_frame)(AVFilterLink *link, AVFrame *frame);

    /**
     * Frame poll callback. This returns the number of immediately available
     * samples. It should return a positive value if the next request_frame()
     * is guaranteed to return one frame (with no delay).
     *
     * Defaults to just calling the source poll_frame() method.
     *
     * Output pads only.
     */
    int (*poll_frame)(AVFilterLink *link);

    /**
     * Frame request callback. A call to this should result in some progress
     * towards producing output over the given link. This should return zero
     * on success, and another value on error.
     *
     * Output pads only.
     */
    int (*request_frame)(AVFilterLink *link);

    /**
     * Link configuration callback.
     *
     * For output pads, this should set the link properties such as
     * width/height. This should NOT set the format property - that is
     * negotiated between filters by the filter system using the
     * query_formats() callback before this function is called.
     *
     * For input pads, this should check the properties of the link, and update
     * the filter's internal state as necessary.
     *
     * For both input and output filters, this should return zero on success,
     * and another value on error.
     */
    int (*config_props)(AVFilterLink *link);

    /**
     * The filter expects a fifo to be inserted on its input link,
     * typically because it has a delay.
     *
     * input pads only.
     */
    int needs_fifo;

    /**
     * The filter expects writable frames from its input link,
     * duplicating data buffers if needed.
     *
     * input pads only.
     */
    int needs_writable;
};

struct AVFilterGraphInternal {
    void *thread;
    avfilter_execute_func *thread_execute;
    FFFrameQueueGlobal frame_queues;
};

struct AVFilterInternal {
    avfilter_execute_func *execute;
};

/**
 * Tell if an integer is contained in the provided -1-terminated list of integers.
 * This is useful for determining (for instance) if an AVPixelFormat is in an
 * array of supported formats.
 *
 * @param fmt provided format
 * @param fmts -1-terminated list of formats
 * @return 1 if present, 0 if absent
 */
int ff_fmt_is_in(int fmt, const int *fmts);

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
 * Parse a time base.
 *
 * @param ret unsigned AVRational pointer to where the value should be written
 * @param arg string to parse
 * @param log_ctx log context
 * @return >= 0 in case of success, a negative AVERROR code on error
 */
av_warn_unused_result
int ff_parse_time_base(AVRational *ret, const char *arg, void *log_ctx);

/**
 * Parse a sample format name or a corresponding integer representation.
 *
 * @param ret integer pointer to where the value should be written
 * @param arg string to parse
 * @param log_ctx log context
 * @return >= 0 in case of success, a negative AVERROR code on error
 */
av_warn_unused_result
int ff_parse_sample_format(int *ret, const char *arg, void *log_ctx);

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
int ff_parse_channel_layout(int64_t *ret, int *nret, const char *arg,
                            void *log_ctx);

void ff_update_link_current_pts(AVFilterLink *link, int64_t pts);

/**
 * Set the status field of a link from the source filter.
 * The pts should reflect the timestamp of the status change,
 * in link time base and relative to the frames timeline.
 * In particular, for AVERROR_EOF, it should reflect the
 * end time of the last frame.
 */
void ff_avfilter_link_set_in_status(AVFilterLink *link, int status, int64_t pts);

/**
 * Set the status field of a link from the destination filter.
 * The pts should probably be left unset (AV_NOPTS_VALUE).
 */
void ff_avfilter_link_set_out_status(AVFilterLink *link, int status, int64_t pts);

void ff_command_queue_pop(AVFilterContext *filter);

/* misc trace functions */

/* #define FF_AVFILTER_TRACE */

#ifdef FF_AVFILTER_TRACE
#    define ff_tlog(pctx, ...) av_log(pctx, AV_LOG_DEBUG, __VA_ARGS__)
#else
#    define ff_tlog(pctx, ...) do { if (0) av_log(pctx, AV_LOG_DEBUG, __VA_ARGS__); } while (0)
#endif

#define FF_TPRINTF_START(ctx, func) ff_tlog(NULL, "%-16s: ", #func)

char *ff_get_ref_perms_string(char *buf, size_t buf_size, int perms);

void ff_tlog_ref(void *ctx, AVFrame *ref, int end);

void ff_tlog_link(void *ctx, AVFilterLink *link, int end);

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
 * @return >= 0 in case of success, a negative AVERROR code on error
 */
int ff_insert_pad(unsigned idx, unsigned *count, size_t padidx_off,
                   AVFilterPad **pads, AVFilterLink ***links,
                   AVFilterPad *newpad);

/** Insert a new input pad for the filter. */
static inline int ff_insert_inpad(AVFilterContext *f, unsigned index,
                                   AVFilterPad *p)
{
    return ff_insert_pad(index, &f->nb_inputs, offsetof(AVFilterLink, dstpad),
                  &f->input_pads, &f->inputs, p);
}

/** Insert a new output pad for the filter. */
static inline int ff_insert_outpad(AVFilterContext *f, unsigned index,
                                    AVFilterPad *p)
{
    return ff_insert_pad(index, &f->nb_outputs, offsetof(AVFilterLink, srcpad),
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
 * This function must not be used by filters using the activate callback,
 * use ff_link_set_frame_wanted() instead.
 *
 * The input filter may pass the request on to its inputs, fulfill the
 * request from an internal buffer or any other means specific to its function.
 *
 * When the end of a stream is reached AVERROR_EOF is returned and no further
 * frames are returned after that.
 *
 * When a filter is unable to output a frame for example due to its sources
 * being unable to do so or because it depends on external means pushing data
 * into it then AVERROR(EAGAIN) is returned.
 * It is important that a AVERROR(EAGAIN) return is returned all the way to the
 * caller (generally eventually a user application) as this step may (but does
 * not have to be) necessary to provide the input with the next frame.
 *
 * If a request is successful then some progress has been made towards
 * providing a frame on the link (through ff_filter_frame()). A filter that
 * needs several frames to produce one is allowed to return success if one
 * more frame has been processed but no output has been produced yet. A
 * filter is also allowed to simply forward a success return value.
 *
 * @param link the input link
 * @return     zero on success
 *             AVERROR_EOF on end of file
 *             AVERROR(EAGAIN) if the previous filter cannot output a frame
 *             currently and can neither guarantee that EOF has been reached.
 */
int ff_request_frame(AVFilterLink *link);

#define AVFILTER_DEFINE_CLASS(fname)            \
    static const AVClass fname##_class = {      \
        .class_name = #fname,                   \
        .item_name  = av_default_item_name,     \
        .option     = fname##_options,          \
        .version    = LIBAVUTIL_VERSION_INT,    \
        .category   = AV_CLASS_CATEGORY_FILTER, \
    }

/**
 * Find the index of a link.
 *
 * I.e. find i such that link == ctx->(in|out)puts[i]
 */
#define FF_INLINK_IDX(link)  ((int)((link)->dstpad - (link)->dst->input_pads))
#define FF_OUTLINK_IDX(link) ((int)((link)->srcpad - (link)->src->output_pads))

/**
 * Send a frame of data to the next filter.
 *
 * @param link   the output link over which the data is being sent
 * @param frame a reference to the buffer of data being sent. The
 *              receiving filter will free this reference when it no longer
 *              needs it or pass it on to the next filter.
 *
 * @return >= 0 on success, a negative AVERROR on error. The receiving filter
 * is responsible for unreferencing frame in case of error.
 */
int ff_filter_frame(AVFilterLink *link, AVFrame *frame);

/**
 * Allocate a new filter context and return it.
 *
 * @param filter what filter to create an instance of
 * @param inst_name name to give to the new filter context
 *
 * @return newly created filter context or NULL on failure
 */
AVFilterContext *ff_filter_alloc(const AVFilter *filter, const char *inst_name);

int ff_filter_activate(AVFilterContext *filter);

/**
 * Remove a filter from a graph;
 */
void ff_filter_graph_remove_filter(AVFilterGraph *graph, AVFilterContext *filter);

/**
 * The filter is aware of hardware frames, and any hardware frame context
 * should not be automatically propagated through it.
 */
#define FF_FILTER_FLAG_HWFRAME_AWARE (1 << 0)

/**
 * Run one round of processing on a filter graph.
 */
int ff_filter_graph_run_once(AVFilterGraph *graph);

/**
 * Normalize the qscale factor
 * FIXME the H264 qscale is a log based scale, mpeg1/2 is not, the code below
 *       cannot be optimal
 */
static inline int ff_norm_qscale(int qscale, int type)
{
    switch (type) {
    case FF_QSCALE_TYPE_MPEG1: return qscale;
    case FF_QSCALE_TYPE_MPEG2: return qscale >> 1;
    case FF_QSCALE_TYPE_H264:  return qscale >> 2;
    case FF_QSCALE_TYPE_VP56:  return (63 - qscale + 2) >> 2;
    }
    return qscale;
}

/**
 * Get number of threads for current filter instance.
 * This number is always same or less than graph->nb_threads.
 */
int ff_filter_get_nb_threads(AVFilterContext *ctx);

#endif /* AVFILTER_INTERNAL_H */
