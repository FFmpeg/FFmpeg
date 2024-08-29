/*
 * Filters implementation helper functions
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFILTER_FILTERS_H
#define AVFILTER_FILTERS_H

/**
 * Filters implementation helper functions
 */

#include "avfilter.h"

/**
 * Special return code when activate() did not do anything.
 */
#define FFERROR_NOT_READY FFERRTAG('N','R','D','Y')

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

/**
 * Link properties exposed to filter code, but not external callers.
 *
 * Cf. AVFilterLink for public properties, FilterLinkInternal for
 * properties private to the generic layer.
 */
typedef struct FilterLink {
    AVFilterLink pub;

    /**
     * Graph the filter belongs to.
     */
    struct AVFilterGraph *graph;

    /**
     * Current timestamp of the link, as defined by the most recent
     * frame(s), in link time_base units.
     */
    int64_t current_pts;

    /**
     * Current timestamp of the link, as defined by the most recent
     * frame(s), in AV_TIME_BASE units.
     */
    int64_t current_pts_us;

    /**
     * Minimum number of samples to filter at once.
     *
     * May be set by the link destination filter in its config_props().
     * If 0, all related fields are ignored.
     */
    int min_samples;

    /**
     * Maximum number of samples to filter at once. If filter_frame() is
     * called with more samples, it will split them.
     *
     * May be set by the link destination filter in its config_props().
     */
    int max_samples;

    /**
     * Number of past frames sent through the link.
     */
    int64_t frame_count_in, frame_count_out;

    /**
     * Number of past samples sent through the link.
     */
    int64_t sample_count_in, sample_count_out;

    /**
     * Frame rate of the stream on the link, or 1/0 if unknown or variable.
     *
     * May be set by the link source filter in its config_props(); if left to
     * 0/0, will be automatically copied from the first input of the source
     * filter if it exists.
     *
     * Sources should set it to the best estimation of the real frame rate.
     * If the source frame rate is unknown or variable, set this to 1/0.
     * Filters should update it if necessary depending on their function.
     * Sinks can use it to set a default output frame rate.
     * It is similar to the r_frame_rate field in AVStream.
     */
    AVRational frame_rate;

    /**
     * For hwaccel pixel formats, this should be a reference to the
     * AVHWFramesContext describing the frames.
     *
     * May be set by the link source filter in its config_props().
     */
    AVBufferRef *hw_frames_ctx;
} FilterLink;

static inline FilterLink* ff_filter_link(AVFilterLink *link)
{
    return (FilterLink*)link;
}

/**
 * The filter is aware of hardware frames, and any hardware frame context
 * should not be automatically propagated through it.
 */
#define FF_FILTER_FLAG_HWFRAME_AWARE (1 << 0)

/**
 * Find the index of a link.
 *
 * I.e. find i such that link == ctx->(in|out)puts[i]
 */
#define FF_INLINK_IDX(link)  ((int)((link)->dstpad - (link)->dst->input_pads))
#define FF_OUTLINK_IDX(link) ((int)((link)->srcpad - (link)->src->output_pads))

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
    FF_FILTER_FORMATS_QUERY_FUNC2,      ///< formats.query_func2 active.
    FF_FILTER_FORMATS_PIXFMT_LIST,      ///< formats.pixels_list active.
    FF_FILTER_FORMATS_SAMPLEFMTS_LIST,  ///< formats.samples_list active.
    FF_FILTER_FORMATS_SINGLE_PIXFMT,    ///< formats.pix_fmt active
    FF_FILTER_FORMATS_SINGLE_SAMPLEFMT, ///< formats.sample_fmt active.
};

#define FILTER_QUERY_FUNC(func)        \
        .formats.query_func   = func,  \
        .formats_state        = FF_FILTER_FORMATS_QUERY_FUNC
#define FILTER_QUERY_FUNC2(func)       \
        .formats.query_func2  = func,  \
        .formats_state        = FF_FILTER_FORMATS_QUERY_FUNC2
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

#define D2TS(d)      (isnan(d) ? AV_NOPTS_VALUE : (int64_t)(d))
#define TS2D(ts)     ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts))
#define TS2T(ts, tb) ((ts) == AV_NOPTS_VALUE ? NAN : (double)(ts) * av_q2d(tb))

/**
 * Mark a filter ready and schedule it for activation.
 *
 * This is automatically done when something happens to the filter (queued
 * frame, status change, request on output).
 * Filters implementing the activate callback can call it directly to
 * perform one more round of processing later.
 * It is also useful for filters reacting to external or asynchronous
 * events.
 */
void ff_filter_set_ready(AVFilterContext *filter, unsigned priority);

/**
 * Get the number of frames available on the link.
 * @return the number of frames available in the link fifo.
 */
size_t ff_inlink_queued_frames(AVFilterLink *link);

/**
 * Test if a frame is available on the link.
 * @return  >0 if a frame is available
 */
int ff_inlink_check_available_frame(AVFilterLink *link);


/***
  * Get the number of samples available on the link.
  * @return the numer of samples available on the link.
  */
int ff_inlink_queued_samples(AVFilterLink *link);

/**
 * Test if enough samples are available on the link.
 * @return  >0 if enough samples are available
 * @note  on EOF and error, min becomes 1
 */
int ff_inlink_check_available_samples(AVFilterLink *link, unsigned min);

/**
 * Take a frame from the link's FIFO and update the link's stats.
 *
 * If ff_inlink_check_available_frame() was previously called, the
 * preferred way of expressing it is "av_assert1(ret);" immediately after
 * ff_inlink_consume_frame(). Negative error codes must still be checked.
 *
 * @note  May trigger process_command() and/or update is_disabled.
 * @return  >0 if a frame is available,
 *          0 and set rframe to NULL if no frame available,
 *          or AVERROR code
 */
int ff_inlink_consume_frame(AVFilterLink *link, AVFrame **rframe);

/**
 * Take samples from the link's FIFO and update the link's stats.
 *
 * If ff_inlink_check_available_samples() was previously called, the
 * preferred way of expressing it is "av_assert1(ret);" immediately after
 * ff_inlink_consume_samples(). Negative error codes must still be checked.
 *
 * @note  May trigger process_command() and/or update is_disabled.
 * @return  >0 if a frame is available,
 *          0 and set rframe to NULL if no frame available,
 *          or AVERROR code
 */
int ff_inlink_consume_samples(AVFilterLink *link, unsigned min, unsigned max,
                            AVFrame **rframe);

/**
 * Access a frame in the link fifo without consuming it.
 * The first frame is numbered 0; the designated frame must exist.
 * @return the frame at idx position in the link fifo.
 */
AVFrame *ff_inlink_peek_frame(AVFilterLink *link, size_t idx);

/**
 * Make sure a frame is writable.
 * This is similar to av_frame_make_writable() except it uses the link's
 * buffer allocation callback, and therefore allows direct rendering.
 */
int ff_inlink_make_frame_writable(AVFilterLink *link, AVFrame **rframe);

/**
 * Test and acknowledge the change of status on the link.
 *
 * Status means EOF or an error condition; a change from the normal (0)
 * status to a non-zero status can be queued in a filter's input link, it
 * becomes relevant after the frames queued in the link's FIFO are
 * processed. This function tests if frames are still queued and if a queued
 * status change has not yet been processed. In that case it performs basic
 * treatment (updating the link's timestamp) and returns a positive value to
 * let the filter do its own treatments (flushing...).
 *
 * Filters implementing the activate callback should call this function when
 * they think it might succeed (usually after checking unsuccessfully for a
 * queued frame).
 * Filters implementing the filter_frame and request_frame callbacks do not
 * need to call that since the same treatment happens in ff_filter_frame().
 *
 * @param[out] rstatus  new or current status
 * @param[out] rpts     current timestamp of the link in link time base
 * @return  >0 if status changed, <0 if status already acked, 0 otherwise
 */
int ff_inlink_acknowledge_status(AVFilterLink *link, int *rstatus, int64_t *rpts);

/**
 * Mark that a frame is wanted on the link.
 * Unlike ff_filter_frame(), it must not be called when the link has a
 * non-zero status, and thus does not acknowledge it.
 * Also it cannot fail.
 */
void ff_inlink_request_frame(AVFilterLink *link);

/**
 * Set the status on an input link.
 * Also discard all frames in the link's FIFO.
 */
void ff_inlink_set_status(AVFilterLink *link, int status);

/**
 * Test if a frame is wanted on an output link.
 */
int ff_outlink_frame_wanted(AVFilterLink *link);

/**
 * Get the status on an output link.
 */
int ff_outlink_get_status(AVFilterLink *link);

/**
 * Set the status field of a link from the source filter.
 * The pts should reflect the timestamp of the status change,
 * in link time base and relative to the frames timeline.
 * In particular, for AVERROR_EOF, it should reflect the
 * end time of the last frame.
 */
void ff_avfilter_link_set_in_status(AVFilterLink *link, int status, int64_t pts);

/**
 * Set the status field of a link from the source filter.
 * The pts should reflect the timestamp of the status change,
 * in link time base and relative to the frames timeline.
 * In particular, for AVERROR_EOF, it should reflect the
 * end time of the last frame.
 */
static inline void ff_outlink_set_status(AVFilterLink *link, int status, int64_t pts)
{
    ff_avfilter_link_set_in_status(link, status, pts);
}

/**
 * Forward the status on an output link to an input link.
 * If the status is set, it will discard all queued frames and this macro
 * will return immediately.
 */
#define FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink) do { \
    int ret = ff_outlink_get_status(outlink); \
    if (ret) { \
        ff_inlink_set_status(inlink, ret); \
        return 0; \
    } \
} while (0)

/**
 * Forward the status on an output link to all input links.
 * If the status is set, it will discard all queued frames and this macro
 * will return immediately.
 */
#define FF_FILTER_FORWARD_STATUS_BACK_ALL(outlink, filter) do { \
    int ret = ff_outlink_get_status(outlink); \
    if (ret) { \
        unsigned i; \
        for (i = 0; i < filter->nb_inputs; i++) \
            ff_inlink_set_status(filter->inputs[i], ret); \
        return 0; \
    } \
} while (0)

/**
 * Acknowledge the status on an input link and forward it to an output link.
 * If the status is set, this macro will return immediately.
 */
#define FF_FILTER_FORWARD_STATUS(inlink, outlink) do { \
    int status; \
    int64_t pts; \
    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) { \
        ff_outlink_set_status(outlink, status, pts); \
        return 0; \
    } \
} while (0)

/**
 * Acknowledge the status on an input link and forward it to an output link.
 * If the status is set, this macro will return immediately.
 */
#define FF_FILTER_FORWARD_STATUS_ALL(inlink, filter) do { \
    int status; \
    int64_t pts; \
    if (ff_inlink_acknowledge_status(inlink, &status, &pts)) { \
        unsigned i; \
        for (i = 0; i < filter->nb_outputs; i++) \
            ff_outlink_set_status(filter->outputs[i], status, pts); \
        return 0; \
    } \
} while (0)

/**
 * Forward the frame_wanted_out flag from an output link to an input link.
 * If the flag is set, this macro will return immediately.
 */
#define FF_FILTER_FORWARD_WANTED(outlink, inlink) do { \
    if (ff_outlink_frame_wanted(outlink)) { \
        ff_inlink_request_frame(inlink); \
        return 0; \
    } \
} while (0)

/**
 * Check for flow control between input and output.
 * This is necessary for filters that may produce several output frames for
 * a single input event, otherwise they may produce them all at once,
 * causing excessive memory consumption.
 */
int ff_inoutlink_check_flow(AVFilterLink *inlink, AVFilterLink *outlink);

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

/**
 * Generic processing of user supplied commands that are set
 * in the same way as the filter options.
 * NOTE: 'enable' option is handled separately, and not by
 * this function.
 */
int ff_filter_process_command(AVFilterContext *ctx, const char *cmd,
                              const char *arg, char *res, int res_len, int flags);

/**
 * Get number of threads for current filter instance.
 * This number is always same or less than graph->nb_threads.
 */
int ff_filter_get_nb_threads(AVFilterContext *ctx) av_pure;

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
 * Tell if an integer is contained in the provided -1-terminated list of integers.
 * This is useful for determining (for instance) if an AVPixelFormat is in an
 * array of supported formats.
 *
 * @param fmt provided format
 * @param fmts -1-terminated list of formats
 * @return 1 if present, 0 if absent
 */
int ff_fmt_is_in(int fmt, const int *fmts);

int ff_filter_execute(AVFilterContext *ctx, avfilter_action_func *func,
                      void *arg, int *ret, int nb_jobs);

#endif /* AVFILTER_FILTERS_H */
