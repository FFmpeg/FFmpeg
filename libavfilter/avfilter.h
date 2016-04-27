/*
 * filter layer
 * Copyright (c) 2007 Bobby Bingham
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVFILTER_AVFILTER_H
#define AVFILTER_AVFILTER_H

/**
 * @file
 * @ingroup lavfi
 * Main libavfilter public API header
 */

/**
 * @defgroup lavfi Libavfilter - graph-based frame editing library
 * @{
 */

#include "libavutil/attributes.h"
#include "libavutil/avutil.h"
#include "libavutil/buffer.h"
#include "libavutil/frame.h"
#include "libavutil/log.h"
#include "libavutil/samplefmt.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"

#include <stddef.h>

#include "libavfilter/version.h"

/**
 * Return the LIBAVFILTER_VERSION_INT constant.
 */
unsigned avfilter_version(void);

/**
 * Return the libavfilter build-time configuration.
 */
const char *avfilter_configuration(void);

/**
 * Return the libavfilter license.
 */
const char *avfilter_license(void);


typedef struct AVFilterContext AVFilterContext;
typedef struct AVFilterLink    AVFilterLink;
typedef struct AVFilterPad     AVFilterPad;
typedef struct AVFilterFormats AVFilterFormats;

/**
 * Get the number of elements in a NULL-terminated array of AVFilterPads (e.g.
 * AVFilter.inputs/outputs).
 */
int avfilter_pad_count(const AVFilterPad *pads);

/**
 * Get the name of an AVFilterPad.
 *
 * @param pads an array of AVFilterPads
 * @param pad_idx index of the pad in the array it; is the caller's
 *                responsibility to ensure the index is valid
 *
 * @return name of the pad_idx'th pad in pads
 */
const char *avfilter_pad_get_name(const AVFilterPad *pads, int pad_idx);

/**
 * Get the type of an AVFilterPad.
 *
 * @param pads an array of AVFilterPads
 * @param pad_idx index of the pad in the array; it is the caller's
 *                responsibility to ensure the index is valid
 *
 * @return type of the pad_idx'th pad in pads
 */
enum AVMediaType avfilter_pad_get_type(const AVFilterPad *pads, int pad_idx);

/**
 * The number of the filter inputs is not determined just by AVFilter.inputs.
 * The filter might add additional inputs during initialization depending on the
 * options supplied to it.
 */
#define AVFILTER_FLAG_DYNAMIC_INPUTS        (1 << 0)
/**
 * The number of the filter outputs is not determined just by AVFilter.outputs.
 * The filter might add additional outputs during initialization depending on
 * the options supplied to it.
 */
#define AVFILTER_FLAG_DYNAMIC_OUTPUTS       (1 << 1)
/**
 * The filter supports multithreading by splitting frames into multiple parts
 * and processing them concurrently.
 */
#define AVFILTER_FLAG_SLICE_THREADS         (1 << 2)

/**
 * Filter definition. This defines the pads a filter contains, and all the
 * callback functions used to interact with the filter.
 */
typedef struct AVFilter {
    /**
     * Filter name. Must be non-NULL and unique among filters.
     */
    const char *name;

    /**
     * A description of the filter. May be NULL.
     *
     * You should use the NULL_IF_CONFIG_SMALL() macro to define it.
     */
    const char *description;

    /**
     * List of inputs, terminated by a zeroed element.
     *
     * NULL if there are no (static) inputs. Instances of filters with
     * AVFILTER_FLAG_DYNAMIC_INPUTS set may have more inputs than present in
     * this list.
     */
    const AVFilterPad *inputs;
    /**
     * List of outputs, terminated by a zeroed element.
     *
     * NULL if there are no (static) outputs. Instances of filters with
     * AVFILTER_FLAG_DYNAMIC_OUTPUTS set may have more outputs than present in
     * this list.
     */
    const AVFilterPad *outputs;

    /**
     * A class for the private data, used to declare filter private AVOptions.
     * This field is NULL for filters that do not declare any options.
     *
     * If this field is non-NULL, the first member of the filter private data
     * must be a pointer to AVClass, which will be set by libavfilter generic
     * code to this class.
     */
    const AVClass *priv_class;

    /**
     * A combination of AVFILTER_FLAG_*
     */
    int flags;

    /*****************************************************************
     * All fields below this line are not part of the public API. They
     * may not be used outside of libavfilter and can be changed and
     * removed at will.
     * New public fields should be added right above.
     *****************************************************************
     */

    /**
     * Filter initialization function.
     *
     * This callback will be called only once during the filter lifetime, after
     * all the options have been set, but before links between filters are
     * established and format negotiation is done.
     *
     * Basic filter initialization should be done here. Filters with dynamic
     * inputs and/or outputs should create those inputs/outputs here based on
     * provided options. No more changes to this filter's inputs/outputs can be
     * done after this callback.
     *
     * This callback must not assume that the filter links exist or frame
     * parameters are known.
     *
     * @ref AVFilter.uninit "uninit" is guaranteed to be called even if
     * initialization fails, so this callback does not have to clean up on
     * failure.
     *
     * @return 0 on success, a negative AVERROR on failure
     */
    int (*init)(AVFilterContext *ctx);

    /**
     * Should be set instead of @ref AVFilter.init "init" by the filters that
     * want to pass a dictionary of AVOptions to nested contexts that are
     * allocated during init.
     *
     * On return, the options dict should be freed and replaced with one that
     * contains all the options which could not be processed by this filter (or
     * with NULL if all the options were processed).
     *
     * Otherwise the semantics is the same as for @ref AVFilter.init "init".
     */
    int (*init_dict)(AVFilterContext *ctx, AVDictionary **options);

    /**
     * Filter uninitialization function.
     *
     * Called only once right before the filter is freed. Should deallocate any
     * memory held by the filter, release any buffer references, etc. It does
     * not need to deallocate the AVFilterContext.priv memory itself.
     *
     * This callback may be called even if @ref AVFilter.init "init" was not
     * called or failed, so it must be prepared to handle such a situation.
     */
    void (*uninit)(AVFilterContext *ctx);

    /**
     * Query formats supported by the filter on its inputs and outputs.
     *
     * This callback is called after the filter is initialized (so the inputs
     * and outputs are fixed), shortly before the format negotiation. This
     * callback may be called more than once.
     *
     * This callback must set AVFilterLink.out_formats on every input link and
     * AVFilterLink.in_formats on every output link to a list of pixel/sample
     * formats that the filter supports on that link. For audio links, this
     * filter must also set @ref AVFilterLink.in_samplerates "in_samplerates" /
     * @ref AVFilterLink.out_samplerates "out_samplerates" and
     * @ref AVFilterLink.in_channel_layouts "in_channel_layouts" /
     * @ref AVFilterLink.out_channel_layouts "out_channel_layouts" analogously.
     *
     * This callback may be NULL for filters with one input, in which case
     * libavfilter assumes that it supports all input formats and preserves
     * them on output.
     *
     * @return zero on success, a negative value corresponding to an
     * AVERROR code otherwise
     */
    int (*query_formats)(AVFilterContext *);

    int priv_size;      ///< size of private data to allocate for the filter

    /**
     * Used by the filter registration system. Must not be touched by any other
     * code.
     */
    struct AVFilter *next;
} AVFilter;

/**
 * Process multiple parts of the frame concurrently.
 */
#define AVFILTER_THREAD_SLICE (1 << 0)

typedef struct AVFilterInternal AVFilterInternal;

/** An instance of a filter */
struct AVFilterContext {
    const AVClass *av_class;              ///< needed for av_log()

    const AVFilter *filter;         ///< the AVFilter of which this is an instance

    char *name;                     ///< name of this filter instance

    AVFilterPad   *input_pads;      ///< array of input pads
    AVFilterLink **inputs;          ///< array of pointers to input links
    unsigned    nb_inputs;          ///< number of input pads

    AVFilterPad   *output_pads;     ///< array of output pads
    AVFilterLink **outputs;         ///< array of pointers to output links
    unsigned    nb_outputs;         ///< number of output pads

    void *priv;                     ///< private data for use by the filter

    struct AVFilterGraph *graph;    ///< filtergraph this filter belongs to

    /**
     * Type of multithreading being allowed/used. A combination of
     * AVFILTER_THREAD_* flags.
     *
     * May be set by the caller before initializing the filter to forbid some
     * or all kinds of multithreading for this filter. The default is allowing
     * everything.
     *
     * When the filter is initialized, this field is combined using bit AND with
     * AVFilterGraph.thread_type to get the final mask used for determining
     * allowed threading types. I.e. a threading type needs to be set in both
     * to be allowed.
     *
     * After the filter is initialzed, libavfilter sets this field to the
     * threading type that is actually used (0 for no multithreading).
     */
    int thread_type;

    /**
     * An opaque struct for libavfilter internal use.
     */
    AVFilterInternal *internal;

    /**
     * For filters which will create hardware frames, sets the device the
     * filter should create them in.  All other filters will ignore this field:
     * in particular, a filter which consumes or processes hardware frames will
     * instead use the hw_frames_ctx field in AVFilterLink to carry the
     * hardware context information.
     */
    AVBufferRef *hw_device_ctx;
};

/**
 * A link between two filters. This contains pointers to the source and
 * destination filters between which this link exists, and the indexes of
 * the pads involved. In addition, this link also contains the parameters
 * which have been negotiated and agreed upon between the filter, such as
 * image dimensions, format, etc.
 */
struct AVFilterLink {
    AVFilterContext *src;       ///< source filter
    AVFilterPad *srcpad;        ///< output pad on the source filter

    AVFilterContext *dst;       ///< dest filter
    AVFilterPad *dstpad;        ///< input pad on the dest filter

    enum AVMediaType type;      ///< filter media type

    /* These parameters apply only to video */
    int w;                      ///< agreed upon image width
    int h;                      ///< agreed upon image height
    AVRational sample_aspect_ratio; ///< agreed upon sample aspect ratio
    /* These two parameters apply only to audio */
    uint64_t channel_layout;    ///< channel layout of current buffer (see libavutil/channel_layout.h)
    int sample_rate;            ///< samples per second

    int format;                 ///< agreed upon media format

    /**
     * Define the time base used by the PTS of the frames/samples
     * which will pass through this link.
     * During the configuration stage, each filter is supposed to
     * change only the output timebase, while the timebase of the
     * input link is assumed to be an unchangeable property.
     */
    AVRational time_base;

    /*****************************************************************
     * All fields below this line are not part of the public API. They
     * may not be used outside of libavfilter and can be changed and
     * removed at will.
     * New public fields should be added right above.
     *****************************************************************
     */
    /**
     * Lists of formats supported by the input and output filters respectively.
     * These lists are used for negotiating the format to actually be used,
     * which will be loaded into the format member, above, when chosen.
     */
    AVFilterFormats *in_formats;
    AVFilterFormats *out_formats;

    /**
     * Lists of channel layouts and sample rates used for automatic
     * negotiation.
     */
    AVFilterFormats  *in_samplerates;
    AVFilterFormats *out_samplerates;
    struct AVFilterChannelLayouts  *in_channel_layouts;
    struct AVFilterChannelLayouts *out_channel_layouts;

    /**
     * Audio only, the destination filter sets this to a non-zero value to
     * request that buffers with the given number of samples should be sent to
     * it. AVFilterPad.needs_fifo must also be set on the corresponding input
     * pad.
     * Last buffer before EOF will be padded with silence.
     */
    int request_samples;

    /** stage of the initialization of the link properties (dimensions, etc) */
    enum {
        AVLINK_UNINIT = 0,      ///< not started
        AVLINK_STARTINIT,       ///< started, but incomplete
        AVLINK_INIT             ///< complete
    } init_state;

    /**
     * Frame rate of the stream on the link, or 1/0 if unknown or variable;
     * if left to 0/0, will be automatically copied from the first input
     * of the source filter if it exists.
     *
     * Sources should set it to the real constant frame rate.
     * If the source frame rate is unknown or variable, set this to 1/0.
     * Filters should update it if necessary depending on their function.
     * Sinks can use it to set a default output frame rate.
     */
    AVRational frame_rate;

    /**
     * For hwaccel pixel formats, this should be a reference to the
     * AVHWFramesContext describing the frames.
     */
    AVBufferRef *hw_frames_ctx;
};

/**
 * Link two filters together.
 *
 * @param src    the source filter
 * @param srcpad index of the output pad on the source filter
 * @param dst    the destination filter
 * @param dstpad index of the input pad on the destination filter
 * @return       zero on success
 */
int avfilter_link(AVFilterContext *src, unsigned srcpad,
                  AVFilterContext *dst, unsigned dstpad);

/**
 * Negotiate the media format, dimensions, etc of all inputs to a filter.
 *
 * @param filter the filter to negotiate the properties for its inputs
 * @return       zero on successful negotiation
 */
int avfilter_config_links(AVFilterContext *filter);

/** Initialize the filter system. Register all builtin filters. */
void avfilter_register_all(void);

#if FF_API_OLD_FILTER_REGISTER
/** Uninitialize the filter system. Unregister all filters. */
attribute_deprecated
void avfilter_uninit(void);
#endif

/**
 * Register a filter. This is only needed if you plan to use
 * avfilter_get_by_name later to lookup the AVFilter structure by name. A
 * filter can still by instantiated with avfilter_graph_alloc_filter even if it
 * is not registered.
 *
 * @param filter the filter to register
 * @return 0 if the registration was successful, a negative value
 * otherwise
 */
int avfilter_register(AVFilter *filter);

/**
 * Get a filter definition matching the given name.
 *
 * @param name the filter name to find
 * @return     the filter definition, if any matching one is registered.
 *             NULL if none found.
 */
#if !FF_API_NOCONST_GET_NAME
const
#endif
AVFilter *avfilter_get_by_name(const char *name);

/**
 * Iterate over all registered filters.
 * @return If prev is non-NULL, next registered filter after prev or NULL if
 * prev is the last filter. If prev is NULL, return the first registered filter.
 */
const AVFilter *avfilter_next(const AVFilter *prev);

#if FF_API_OLD_FILTER_REGISTER
/**
 * If filter is NULL, returns a pointer to the first registered filter pointer,
 * if filter is non-NULL, returns the next pointer after filter.
 * If the returned pointer points to NULL, the last registered filter
 * was already reached.
 * @deprecated use avfilter_next()
 */
attribute_deprecated
AVFilter **av_filter_next(AVFilter **filter);
#endif

#if FF_API_AVFILTER_OPEN
/**
 * Create a filter instance.
 *
 * @param filter_ctx put here a pointer to the created filter context
 * on success, NULL on failure
 * @param filter    the filter to create an instance of
 * @param inst_name Name to give to the new instance. Can be NULL for none.
 * @return >= 0 in case of success, a negative error code otherwise
 * @deprecated use avfilter_graph_alloc_filter() instead
 */
attribute_deprecated
int avfilter_open(AVFilterContext **filter_ctx, AVFilter *filter, const char *inst_name);
#endif


#if FF_API_AVFILTER_INIT_FILTER
/**
 * Initialize a filter.
 *
 * @param filter the filter to initialize
 * @param args   A string of parameters to use when initializing the filter.
 *               The format and meaning of this string varies by filter.
 * @param opaque Any extra non-string data needed by the filter. The meaning
 *               of this parameter varies by filter.
 * @return       zero on success
 */
attribute_deprecated
int avfilter_init_filter(AVFilterContext *filter, const char *args, void *opaque);
#endif

/**
 * Initialize a filter with the supplied parameters.
 *
 * @param ctx  uninitialized filter context to initialize
 * @param args Options to initialize the filter with. This must be a
 *             ':'-separated list of options in the 'key=value' form.
 *             May be NULL if the options have been set directly using the
 *             AVOptions API or there are no options that need to be set.
 * @return 0 on success, a negative AVERROR on failure
 */
int avfilter_init_str(AVFilterContext *ctx, const char *args);

/**
 * Initialize a filter with the supplied dictionary of options.
 *
 * @param ctx     uninitialized filter context to initialize
 * @param options An AVDictionary filled with options for this filter. On
 *                return this parameter will be destroyed and replaced with
 *                a dict containing options that were not found. This dictionary
 *                must be freed by the caller.
 *                May be NULL, then this function is equivalent to
 *                avfilter_init_str() with the second parameter set to NULL.
 * @return 0 on success, a negative AVERROR on failure
 *
 * @note This function and avfilter_init_str() do essentially the same thing,
 * the difference is in manner in which the options are passed. It is up to the
 * calling code to choose whichever is more preferable. The two functions also
 * behave differently when some of the provided options are not declared as
 * supported by the filter. In such a case, avfilter_init_str() will fail, but
 * this function will leave those extra options in the options AVDictionary and
 * continue as usual.
 */
int avfilter_init_dict(AVFilterContext *ctx, AVDictionary **options);

/**
 * Free a filter context. This will also remove the filter from its
 * filtergraph's list of filters.
 *
 * @param filter the filter to free
 */
void avfilter_free(AVFilterContext *filter);

/**
 * Insert a filter in the middle of an existing link.
 *
 * @param link the link into which the filter should be inserted
 * @param filt the filter to be inserted
 * @param filt_srcpad_idx the input pad on the filter to connect
 * @param filt_dstpad_idx the output pad on the filter to connect
 * @return     zero on success
 */
int avfilter_insert_filter(AVFilterLink *link, AVFilterContext *filt,
                           unsigned filt_srcpad_idx, unsigned filt_dstpad_idx);

/**
 * @return AVClass for AVFilterContext.
 *
 * @see av_opt_find().
 */
const AVClass *avfilter_get_class(void);

typedef struct AVFilterGraphInternal AVFilterGraphInternal;

/**
 * A function pointer passed to the @ref AVFilterGraph.execute callback to be
 * executed multiple times, possibly in parallel.
 *
 * @param ctx the filter context the job belongs to
 * @param arg an opaque parameter passed through from @ref
 *            AVFilterGraph.execute
 * @param jobnr the index of the job being executed
 * @param nb_jobs the total number of jobs
 *
 * @return 0 on success, a negative AVERROR on error
 */
typedef int (avfilter_action_func)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

/**
 * A function executing multiple jobs, possibly in parallel.
 *
 * @param ctx the filter context to which the jobs belong
 * @param func the function to be called multiple times
 * @param arg the argument to be passed to func
 * @param ret a nb_jobs-sized array to be filled with return values from each
 *            invocation of func
 * @param nb_jobs the number of jobs to execute
 *
 * @return 0 on success, a negative AVERROR on error
 */
typedef int (avfilter_execute_func)(AVFilterContext *ctx, avfilter_action_func *func,
                                    void *arg, int *ret, int nb_jobs);

typedef struct AVFilterGraph {
    const AVClass *av_class;
    AVFilterContext **filters;
    unsigned nb_filters;

    char *scale_sws_opts; ///< sws options to use for the auto-inserted scale filters
    char *resample_lavr_opts;   ///< libavresample options to use for the auto-inserted resample filters

    /**
     * Type of multithreading allowed for filters in this graph. A combination
     * of AVFILTER_THREAD_* flags.
     *
     * May be set by the caller at any point, the setting will apply to all
     * filters initialized after that. The default is allowing everything.
     *
     * When a filter in this graph is initialized, this field is combined using
     * bit AND with AVFilterContext.thread_type to get the final mask used for
     * determining allowed threading types. I.e. a threading type needs to be
     * set in both to be allowed.
     */
    int thread_type;

    /**
     * Maximum number of threads used by filters in this graph. May be set by
     * the caller before adding any filters to the filtergraph. Zero (the
     * default) means that the number of threads is determined automatically.
     */
    int nb_threads;

    /**
     * Opaque object for libavfilter internal use.
     */
    AVFilterGraphInternal *internal;

    /**
     * Opaque user data. May be set by the caller to an arbitrary value, e.g. to
     * be used from callbacks like @ref AVFilterGraph.execute.
     * Libavfilter will not touch this field in any way.
     */
    void *opaque;

    /**
     * This callback may be set by the caller immediately after allocating the
     * graph and before adding any filters to it, to provide a custom
     * multithreading implementation.
     *
     * If set, filters with slice threading capability will call this callback
     * to execute multiple jobs in parallel.
     *
     * If this field is left unset, libavfilter will use its internal
     * implementation, which may or may not be multithreaded depending on the
     * platform and build options.
     */
    avfilter_execute_func *execute;
} AVFilterGraph;

/**
 * Allocate a filter graph.
 *
 * @return the allocated filter graph on success or NULL.
 */
AVFilterGraph *avfilter_graph_alloc(void);

/**
 * Create a new filter instance in a filter graph.
 *
 * @param graph graph in which the new filter will be used
 * @param filter the filter to create an instance of
 * @param name Name to give to the new instance (will be copied to
 *             AVFilterContext.name). This may be used by the caller to identify
 *             different filters, libavfilter itself assigns no semantics to
 *             this parameter. May be NULL.
 *
 * @return the context of the newly created filter instance (note that it is
 *         also retrievable directly through AVFilterGraph.filters or with
 *         avfilter_graph_get_filter()) on success or NULL or failure.
 */
AVFilterContext *avfilter_graph_alloc_filter(AVFilterGraph *graph,
                                             const AVFilter *filter,
                                             const char *name);

/**
 * Get a filter instance with name name from graph.
 *
 * @return the pointer to the found filter instance or NULL if it
 * cannot be found.
 */
AVFilterContext *avfilter_graph_get_filter(AVFilterGraph *graph, char *name);

#if FF_API_AVFILTER_OPEN
/**
 * Add an existing filter instance to a filter graph.
 *
 * @param graphctx  the filter graph
 * @param filter the filter to be added
 *
 * @deprecated use avfilter_graph_alloc_filter() to allocate a filter in a
 * filter graph
 */
attribute_deprecated
int avfilter_graph_add_filter(AVFilterGraph *graphctx, AVFilterContext *filter);
#endif

/**
 * Create and add a filter instance into an existing graph.
 * The filter instance is created from the filter filt and inited
 * with the parameters args and opaque.
 *
 * In case of success put in *filt_ctx the pointer to the created
 * filter instance, otherwise set *filt_ctx to NULL.
 *
 * @param name the instance name to give to the created filter instance
 * @param graph_ctx the filter graph
 * @return a negative AVERROR error code in case of failure, a non
 * negative value otherwise
 */
int avfilter_graph_create_filter(AVFilterContext **filt_ctx, const AVFilter *filt,
                                 const char *name, const char *args, void *opaque,
                                 AVFilterGraph *graph_ctx);

/**
 * Check validity and configure all the links and formats in the graph.
 *
 * @param graphctx the filter graph
 * @param log_ctx context used for logging
 * @return 0 in case of success, a negative AVERROR code otherwise
 */
int avfilter_graph_config(AVFilterGraph *graphctx, void *log_ctx);

/**
 * Free a graph, destroy its links, and set *graph to NULL.
 * If *graph is NULL, do nothing.
 */
void avfilter_graph_free(AVFilterGraph **graph);

/**
 * A linked-list of the inputs/outputs of the filter chain.
 *
 * This is mainly useful for avfilter_graph_parse() / avfilter_graph_parse2(),
 * where it is used to communicate open (unlinked) inputs and outputs from and
 * to the caller.
 * This struct specifies, per each not connected pad contained in the graph, the
 * filter context and the pad index required for establishing a link.
 */
typedef struct AVFilterInOut {
    /** unique name for this input/output in the list */
    char *name;

    /** filter context associated to this input/output */
    AVFilterContext *filter_ctx;

    /** index of the filt_ctx pad to use for linking */
    int pad_idx;

    /** next input/input in the list, NULL if this is the last */
    struct AVFilterInOut *next;
} AVFilterInOut;

/**
 * Allocate a single AVFilterInOut entry.
 * Must be freed with avfilter_inout_free().
 * @return allocated AVFilterInOut on success, NULL on failure.
 */
AVFilterInOut *avfilter_inout_alloc(void);

/**
 * Free the supplied list of AVFilterInOut and set *inout to NULL.
 * If *inout is NULL, do nothing.
 */
void avfilter_inout_free(AVFilterInOut **inout);

/**
 * Add a graph described by a string to a graph.
 *
 * @param graph   the filter graph where to link the parsed graph context
 * @param filters string to be parsed
 * @param inputs  linked list to the inputs of the graph
 * @param outputs linked list to the outputs of the graph
 * @return zero on success, a negative AVERROR code on error
 */
int avfilter_graph_parse(AVFilterGraph *graph, const char *filters,
                         AVFilterInOut *inputs, AVFilterInOut *outputs,
                         void *log_ctx);

/**
 * Add a graph described by a string to a graph.
 *
 * @param[in]  graph   the filter graph where to link the parsed graph context
 * @param[in]  filters string to be parsed
 * @param[out] inputs  a linked list of all free (unlinked) inputs of the
 *                     parsed graph will be returned here. It is to be freed
 *                     by the caller using avfilter_inout_free().
 * @param[out] outputs a linked list of all free (unlinked) outputs of the
 *                     parsed graph will be returned here. It is to be freed by the
 *                     caller using avfilter_inout_free().
 * @return zero on success, a negative AVERROR code on error
 *
 * @note the difference between avfilter_graph_parse2() and
 * avfilter_graph_parse() is that in avfilter_graph_parse(), the caller provides
 * the lists of inputs and outputs, which therefore must be known before calling
 * the function. On the other hand, avfilter_graph_parse2() \em returns the
 * inputs and outputs that are left unlinked after parsing the graph and the
 * caller then deals with them. Another difference is that in
 * avfilter_graph_parse(), the inputs parameter describes inputs of the
 * <em>already existing</em> part of the graph; i.e. from the point of view of
 * the newly created part, they are outputs. Similarly the outputs parameter
 * describes outputs of the already existing filters, which are provided as
 * inputs to the parsed filters.
 * avfilter_graph_parse2() takes the opposite approach -- it makes no reference
 * whatsoever to already existing parts of the graph and the inputs parameter
 * will on return contain inputs of the newly parsed part of the graph.
 * Analogously the outputs parameter will contain outputs of the newly created
 * filters.
 */
int avfilter_graph_parse2(AVFilterGraph *graph, const char *filters,
                          AVFilterInOut **inputs,
                          AVFilterInOut **outputs);

/**
 * @}
 */

#endif /* AVFILTER_AVFILTER_H */
