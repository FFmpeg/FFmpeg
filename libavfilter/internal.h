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
#include "formats.h"
#include "framequeue.h"
#include "version.h"
#include "video.h"

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
     * The filter expects writable frames from its input link,
     * duplicating data buffers if needed.
     *
     * input pads only.
     */
#define AVFILTERPAD_FLAG_NEEDS_WRITABLE                  (1 << 0)

    /**
     * The pad's name is allocated and should be freed generically.
     */
#define AVFILTERPAD_FLAG_FREE_NAME                       (1 << 1)

    /**
     * A combination of AVFILTERPAD_FLAG_* flags.
     */
    int flags;

    /**
     * Callback functions to get a video/audio buffers. If NULL,
     * the filter system will use ff_default_get_video_buffer() for video
     * and ff_default_get_audio_buffer() for audio.
     *
     * The state of the union is determined by type.
     *
     * Input pads only.
     */
    union {
        AVFrame *(*video)(AVFilterLink *link, int w, int h);
        AVFrame *(*audio)(AVFilterLink *link, int nb_samples);
    } get_buffer;

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
};

struct AVFilterGraphInternal {
    void *thread;
    avfilter_execute_func *thread_execute;
    FFFrameQueueGlobal frame_queues;
};

struct AVFilterInternal {
    avfilter_execute_func *execute;
};

static av_always_inline int ff_filter_execute(AVFilterContext *ctx, avfilter_action_func *func,
                                              void *arg, int *ret, int nb_jobs)
{
    return ctx->internal->execute(ctx, func, arg, ret, nb_jobs);
}

enum FilterFormatsState {
    /**
     * The default value meaning that this filter supports all formats
     * and (for audio) sample rates and channel layouts/counts as long
     * as these properties agree for all inputs and outputs.
     * This state is only allowed in case all inputs and outputs actually
     * have the same type.
     * The union is unused in this state.
     *
     * This value must always be zero (for default static initialization).
     */
    FF_FILTER_FORMATS_PASSTHROUGH = 0,
    FF_FILTER_FORMATS_QUERY_FUNC,       ///< formats.query active.
    FF_FILTER_FORMATS_PIXFMT_LIST,      ///< formats.pixels_list active.
    FF_FILTER_FORMATS_SAMPLEFMTS_LIST,  ///< formats.samples_list active.
    FF_FILTER_FORMATS_SINGLE_PIXFMT,    ///< formats.pix_fmt active
    FF_FILTER_FORMATS_SINGLE_SAMPLEFMT, ///< formats.sample_fmt active.
};

#define FILTER_QUERY_FUNC(func)        \
        .formats.query_func   = func,  \
        .formats_state        = FF_FILTER_FORMATS_QUERY_FUNC
#define FILTER_PIXFMTS_ARRAY(array)    \
        .formats.pixels_list  = array, \
        .formats_state        = FF_FILTER_FORMATS_PIXFMT_LIST
#define FILTER_SAMPLEFMTS_ARRAY(array) \
        .formats.samples_list = array, \
        .formats_state        = FF_FILTER_FORMATS_SAMPLEFMTS_LIST
#define FILTER_PIXFMTS(...)            \
    FILTER_PIXFMTS_ARRAY(((const enum AVPixelFormat []) { __VA_ARGS__, AV_PIX_FMT_NONE }))
#define FILTER_SAMPLEFMTS(...)         \
    FILTER_SAMPLEFMTS_ARRAY(((const enum AVSampleFormat[]) { __VA_ARGS__, AV_SAMPLE_FMT_NONE }))
#define FILTER_SINGLE_PIXFMT(pix_fmt_)  \
        .formats.pix_fmt = pix_fmt_,    \
        .formats_state   = FF_FILTER_FORMATS_SINGLE_PIXFMT
#define FILTER_SINGLE_SAMPLEFMT(sample_fmt_) \
        .formats.sample_fmt = sample_fmt_,   \
        .formats_state      = FF_FILTER_FORMATS_SINGLE_SAMPLEFMT

#define FILTER_INOUTPADS(inout, array) \
       .inout        = array, \
       .nb_ ## inout = FF_ARRAY_ELEMS(array)
#define FILTER_INPUTS(array) FILTER_INOUTPADS(inputs, (array))
#define FILTER_OUTPUTS(array) FILTER_INOUTPADS(outputs, (array))

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

#define D2TS(d)      (isnan(d) ? AV_NOPTS_VALUE : (int64_t)(d))
#define TS2D(ts)     ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts))
#define TS2T(ts, tb) ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts) * av_q2d(tb))

/* misc trace functions */

#define FF_TPRINTF_START(ctx, func) ff_tlog(NULL, "%-16s: ", #func)

char *ff_get_ref_perms_string(char *buf, size_t buf_size, int perms);

void ff_tlog_link(void *ctx, AVFilterLink *link, int end);

/**
 * Append a new input/output pad to the filter's list of such pads.
 *
 * The *_free_name versions will set the AVFILTERPAD_FLAG_FREE_NAME flag
 * ensuring that the name will be freed generically (even on insertion error).
 */
int ff_append_inpad (AVFilterContext *f, AVFilterPad *p);
int ff_append_outpad(AVFilterContext *f, AVFilterPad *p);
int ff_append_inpad_free_name (AVFilterContext *f, AVFilterPad *p);
int ff_append_outpad_free_name(AVFilterContext *f, AVFilterPad *p);

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

#define AVFILTER_DEFINE_CLASS_EXT(name, desc, options) \
    static const AVClass name##_class = {       \
        .class_name = desc,                     \
        .item_name  = av_default_item_name,     \
        .option     = options,                  \
        .version    = LIBAVUTIL_VERSION_INT,    \
        .category   = AV_CLASS_CATEGORY_FILTER, \
    }
#define AVFILTER_DEFINE_CLASS(fname) \
    AVFILTER_DEFINE_CLASS_EXT(fname, #fname, fname##_options)

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
 * Get number of threads for current filter instance.
 * This number is always same or less than graph->nb_threads.
 */
int ff_filter_get_nb_threads(AVFilterContext *ctx) av_pure;

/**
 * Generic processing of user supplied commands that are set
 * in the same way as the filter options.
 * NOTE: 'enable' option is handled separately, and not by
 * this function.
 */
int ff_filter_process_command(AVFilterContext *ctx, const char *cmd,
                              const char *arg, char *res, int res_len, int flags);

/**
 * Perform any additional setup required for hardware frames.
 *
 * link->hw_frames_ctx must be set before calling this function.
 * Inside link->hw_frames_ctx, the fields format, sw_format, width and
 * height must be set.  If dynamically allocated pools are not supported,
 * then initial_pool_size must also be set, to the minimum hardware frame
 * pool size necessary for the filter to work (taking into account any
 * frames which need to stored for use in operations as appropriate).  If
 * default_pool_size is nonzero, then it will be used as the pool size if
 * no other modification takes place (this can be used to preserve
 * compatibility).
 */
int ff_filter_init_hw_frames(AVFilterContext *avctx, AVFilterLink *link,
                             int default_pool_size);

#endif /* AVFILTER_INTERNAL_H */
