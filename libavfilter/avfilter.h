/*
 * filter layer
 * Copyright (c) 2007 Bobby Bingham
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

#ifndef AVFILTER_AVFILTER_H
#define AVFILTER_AVFILTER_H

#include <stddef.h>

#include "libavutil/avutil.h"
#include "libavutil/dict.h"
#include "libavutil/log.h"
#include "libavutil/samplefmt.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"

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

/**
 * Get the class for the AVFilterContext struct.
 */
const AVClass *avfilter_get_class(void);

typedef struct AVFilterContext AVFilterContext;
typedef struct AVFilterLink    AVFilterLink;
typedef struct AVFilterPad     AVFilterPad;
typedef struct AVFilterFormats AVFilterFormats;

/**
 * A reference-counted buffer data type used by the filter system. Filters
 * should not store pointers to this structure directly, but instead use the
 * AVFilterBufferRef structure below.
 */
typedef struct AVFilterBuffer {
    uint8_t *data[8];           ///< buffer data for each plane/channel

    /**
     * pointers to the data planes/channels.
     *
     * For video, this should simply point to data[].
     *
     * For planar audio, each channel has a separate data pointer, and
     * linesize[0] contains the size of each channel buffer.
     * For packed audio, there is just one data pointer, and linesize[0]
     * contains the total size of the buffer for all channels.
     *
     * Note: Both data and extended_data will always be set, but for planar
     * audio with more channels that can fit in data, extended_data must be used
     * in order to access all channels.
     */
    uint8_t **extended_data;
    int linesize[8];            ///< number of bytes per line

    /** private data to be used by a custom free function */
    void *priv;
    /**
     * A pointer to the function to deallocate this buffer if the default
     * function is not sufficient. This could, for example, add the memory
     * back into a memory pool to be reused later without the overhead of
     * reallocating it from scratch.
     */
    void (*free)(struct AVFilterBuffer *buf);

    int format;                 ///< media format
    int w, h;                   ///< width and height of the allocated buffer
    unsigned refcount;          ///< number of references to this buffer
} AVFilterBuffer;

#define AV_PERM_READ     0x01   ///< can read from the buffer
#define AV_PERM_WRITE    0x02   ///< can write to the buffer
#define AV_PERM_PRESERVE 0x04   ///< nobody else can overwrite the buffer
#define AV_PERM_REUSE    0x08   ///< can output the buffer multiple times, with the same contents each time
#define AV_PERM_REUSE2   0x10   ///< can output the buffer multiple times, modified each time
#define AV_PERM_NEG_LINESIZES 0x20  ///< the buffer requested can have negative linesizes
#define AV_PERM_ALIGN    0x40   ///< the buffer must be aligned

#define AVFILTER_ALIGN 16 //not part of ABI

/**
 * Audio specific properties in a reference to an AVFilterBuffer. Since
 * AVFilterBufferRef is common to different media formats, audio specific
 * per reference properties must be separated out.
 */
typedef struct AVFilterBufferRefAudioProps {
    uint64_t channel_layout;    ///< channel layout of audio buffer
    int nb_samples;             ///< number of audio samples per channel
    int sample_rate;            ///< audio buffer sample rate
    int channels;               ///< number of channels
} AVFilterBufferRefAudioProps;

/**
 * Video specific properties in a reference to an AVFilterBuffer. Since
 * AVFilterBufferRef is common to different media formats, video specific
 * per reference properties must be separated out.
 */
typedef struct AVFilterBufferRefVideoProps {
    int w;                      ///< image width
    int h;                      ///< image height
    AVRational sample_aspect_ratio; ///< sample aspect ratio
    int interlaced;             ///< is frame interlaced
    int top_field_first;        ///< field order
    enum AVPictureType pict_type; ///< picture type of the frame
    int key_frame;              ///< 1 -> keyframe, 0-> not
    int qp_table_linesize;                ///< qp_table stride
    int qp_table_size;            ///< qp_table size
    int8_t *qp_table;             ///< array of Quantization Parameters
} AVFilterBufferRefVideoProps;

/**
 * A reference to an AVFilterBuffer. Since filters can manipulate the origin of
 * a buffer to, for example, crop image without any memcpy, the buffer origin
 * and dimensions are per-reference properties. Linesize is also useful for
 * image flipping, frame to field filters, etc, and so is also per-reference.
 *
 * TODO: add anything necessary for frame reordering
 */
typedef struct AVFilterBufferRef {
    AVFilterBuffer *buf;        ///< the buffer that this is a reference to
    uint8_t *data[8];           ///< picture/audio data for each plane
    /**
     * pointers to the data planes/channels.
     *
     * For video, this should simply point to data[].
     *
     * For planar audio, each channel has a separate data pointer, and
     * linesize[0] contains the size of each channel buffer.
     * For packed audio, there is just one data pointer, and linesize[0]
     * contains the total size of the buffer for all channels.
     *
     * Note: Both data and extended_data will always be set, but for planar
     * audio with more channels that can fit in data, extended_data must be used
     * in order to access all channels.
     */
    uint8_t **extended_data;
    int linesize[8];            ///< number of bytes per line

    AVFilterBufferRefVideoProps *video; ///< video buffer specific properties
    AVFilterBufferRefAudioProps *audio; ///< audio buffer specific properties

    /**
     * presentation timestamp. The time unit may change during
     * filtering, as it is specified in the link and the filter code
     * may need to rescale the PTS accordingly.
     */
    int64_t pts;
    int64_t pos;                ///< byte position in stream, -1 if unknown

    int format;                 ///< media format

    int perms;                  ///< permissions, see the AV_PERM_* flags

    enum AVMediaType type;      ///< media type of buffer data

    AVDictionary *metadata;     ///< dictionary containing metadata key=value tags
} AVFilterBufferRef;

/**
 * Copy properties of src to dst, without copying the actual data
 */
void avfilter_copy_buffer_ref_props(AVFilterBufferRef *dst, AVFilterBufferRef *src);

/**
 * Add a new reference to a buffer.
 *
 * @param ref   an existing reference to the buffer
 * @param pmask a bitmask containing the allowable permissions in the new
 *              reference
 * @return      a new reference to the buffer with the same properties as the
 *              old, excluding any permissions denied by pmask
 */
AVFilterBufferRef *avfilter_ref_buffer(AVFilterBufferRef *ref, int pmask);

/**
 * Remove a reference to a buffer. If this is the last reference to the
 * buffer, the buffer itself is also automatically freed.
 *
 * @param ref reference to the buffer, may be NULL
 *
 * @note it is recommended to use avfilter_unref_bufferp() instead of this
 * function
 */
void avfilter_unref_buffer(AVFilterBufferRef *ref);

/**
 * Remove a reference to a buffer and set the pointer to NULL.
 * If this is the last reference to the buffer, the buffer itself
 * is also automatically freed.
 *
 * @param ref pointer to the buffer reference
 */
void avfilter_unref_bufferp(AVFilterBufferRef **ref);

#if FF_API_AVFILTERPAD_PUBLIC
/**
 * A filter pad used for either input or output.
 *
 * See doc/filter_design.txt for details on how to implement the methods.
 *
 * @warning this struct might be removed from public API.
 * users should call avfilter_pad_get_name() and avfilter_pad_get_type()
 * to access the name and type fields; there should be no need to access
 * any other fields from outside of libavfilter.
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
     * Input pads:
     * Minimum required permissions on incoming buffers. Any buffer with
     * insufficient permissions will be automatically copied by the filter
     * system to a new buffer which provides the needed access permissions.
     *
     * Output pads:
     * Guaranteed permissions on outgoing buffers. Any buffer pushed on the
     * link must have at least these permissions; this fact is checked by
     * asserts. It can be used to optimize buffer allocation.
     */
    int min_perms;

    /**
     * Input pads:
     * Permissions which are not accepted on incoming buffers. Any buffer
     * which has any of these permissions set will be automatically copied
     * by the filter system to a new buffer which does not have those
     * permissions. This can be used to easily disallow buffers with
     * AV_PERM_REUSE.
     *
     * Output pads:
     * Permissions which are automatically removed on outgoing buffers. It
     * can be used to optimize buffer allocation.
     */
    int rej_perms;

    /**
     * Callback called before passing the first slice of a new frame. If
     * NULL, the filter layer will default to storing a reference to the
     * picture inside the link structure.
     *
     * The reference given as argument is also available in link->cur_buf.
     * It can be stored elsewhere or given away, but then clearing
     * link->cur_buf is advised, as it is automatically unreferenced.
     * The reference must not be unreferenced before end_frame(), as it may
     * still be in use by the automatic copy mechanism.
     *
     * Input video pads only.
     *
     * @return >= 0 on success, a negative AVERROR on error. picref will be
     * unreferenced by the caller in case of error.
     */
    int (*start_frame)(AVFilterLink *link, AVFilterBufferRef *picref);

    /**
     * Callback function to get a video buffer. If NULL, the filter system will
     * use ff_default_get_video_buffer().
     *
     * Input video pads only.
     */
    AVFilterBufferRef *(*get_video_buffer)(AVFilterLink *link, int perms, int w, int h);

    /**
     * Callback function to get an audio buffer. If NULL, the filter system will
     * use ff_default_get_audio_buffer().
     *
     * Input audio pads only.
     */
    AVFilterBufferRef *(*get_audio_buffer)(AVFilterLink *link, int perms,
                                           int nb_samples);

    /**
     * Callback called after the slices of a frame are completely sent. If
     * NULL, the filter layer will default to releasing the reference stored
     * in the link structure during start_frame().
     *
     * Input video pads only.
     *
     * @return >= 0 on success, a negative AVERROR on error.
     */
    int (*end_frame)(AVFilterLink *link);

    /**
     * Slice drawing callback. This is where a filter receives video data
     * and should do its processing.
     *
     * Input video pads only.
     *
     * @return >= 0 on success, a negative AVERROR on error.
     */
    int (*draw_slice)(AVFilterLink *link, int y, int height, int slice_dir);

    /**
     * Samples filtering callback. This is where a filter receives audio data
     * and should do its processing.
     *
     * Input audio pads only.
     *
     * @return >= 0 on success, a negative AVERROR on error. This function
     * must ensure that samplesref is properly unreferenced on error if it
     * hasn't been passed on to another filter.
     */
    int (*filter_samples)(AVFilterLink *link, AVFilterBufferRef *samplesref);

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
     * Frame request callback. A call to this should result in at least one
     * frame being output over the given link. This should return zero on
     * success, and another value on error.
     * See ff_request_frame() for the error codes with a specific
     * meaning.
     *
     * Output pads only.
     */
    int (*request_frame)(AVFilterLink *link);

    /**
     * Link configuration callback.
     *
     * For output pads, this should set the following link properties:
     * video: width, height, sample_aspect_ratio, time_base
     * audio: sample_rate.
     *
     * This should NOT set properties such as format, channel_layout, etc which
     * are negotiated between filters by the filter system using the
     * query_formats() callback before this function is called.
     *
     * For input pads, this should check the properties of the link, and update
     * the filter's internal state as necessary.
     *
     * For both input and output pads, this should return zero on success,
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
};
#endif

/**
 * Get the name of an AVFilterPad.
 *
 * @param pads an array of AVFilterPads
 * @param pad_idx index of the pad in the array it; is the caller's
 *                responsibility to ensure the index is valid
 *
 * @return name of the pad_idx'th pad in pads
 */
const char *avfilter_pad_get_name(AVFilterPad *pads, int pad_idx);

/**
 * Get the type of an AVFilterPad.
 *
 * @param pads an array of AVFilterPads
 * @param pad_idx index of the pad in the array; it is the caller's
 *                responsibility to ensure the index is valid
 *
 * @return type of the pad_idx'th pad in pads
 */
enum AVMediaType avfilter_pad_get_type(AVFilterPad *pads, int pad_idx);

/**
 * Filter definition. This defines the pads a filter contains, and all the
 * callback functions used to interact with the filter.
 */
typedef struct AVFilter {
    const char *name;         ///< filter name

    /**
     * A description for the filter. You should use the
     * NULL_IF_CONFIG_SMALL() macro to define it.
     */
    const char *description;

    const AVFilterPad *inputs;  ///< NULL terminated list of inputs. NULL if none
    const AVFilterPad *outputs; ///< NULL terminated list of outputs. NULL if none

    /*****************************************************************
     * All fields below this line are not part of the public API. They
     * may not be used outside of libavfilter and can be changed and
     * removed at will.
     * New public fields should be added right above.
     *****************************************************************
     */

    /**
     * Filter initialization function. Args contains the user-supplied
     * parameters. FIXME: maybe an AVOption-based system would be better?
     */
    int (*init)(AVFilterContext *ctx, const char *args);

    /**
     * Filter uninitialization function. Should deallocate any memory held
     * by the filter, release any buffer references, etc. This does not need
     * to deallocate the AVFilterContext->priv memory itself.
     */
    void (*uninit)(AVFilterContext *ctx);

    /**
     * Queries formats/layouts supported by the filter and its pads, and sets
     * the in_formats/in_chlayouts for links connected to its output pads,
     * and out_formats/out_chlayouts for links connected to its input pads.
     *
     * @return zero on success, a negative value corresponding to an
     * AVERROR code otherwise
     */
    int (*query_formats)(AVFilterContext *);

    int priv_size;      ///< size of private data to allocate for the filter

    /**
     * Make the filter instance process a command.
     *
     * @param cmd    the command to process, for handling simplicity all commands must be alphanumeric only
     * @param arg    the argument for the command
     * @param res    a buffer with size res_size where the filter(s) can return a response. This must not change when the command is not supported.
     * @param flags  if AVFILTER_CMD_FLAG_FAST is set and the command would be
     *               time consuming then a filter should treat it like an unsupported command
     *
     * @returns >=0 on success otherwise an error code.
     *          AVERROR(ENOSYS) on unsupported commands
     */
    int (*process_command)(AVFilterContext *, const char *cmd, const char *arg, char *res, int res_len, int flags);

    /**
     * Filter initialization function, alternative to the init()
     * callback. Args contains the user-supplied parameters, opaque is
     * used for providing binary data.
     */
    int (*init_opaque)(AVFilterContext *ctx, const char *args, void *opaque);

    const AVClass *priv_class;      ///< private class, containing filter specific options
} AVFilter;

/** An instance of a filter */
struct AVFilterContext {
    const AVClass *av_class;        ///< needed for av_log()

    AVFilter *filter;               ///< the AVFilter of which this is an instance

    char *name;                     ///< name of this filter instance

    AVFilterPad   *input_pads;      ///< array of input pads
    AVFilterLink **inputs;          ///< array of pointers to input links
#if FF_API_FOO_COUNT
    unsigned input_count;           ///< @deprecated use nb_inputs
#endif
    unsigned    nb_inputs;          ///< number of input pads

    AVFilterPad   *output_pads;     ///< array of output pads
    AVFilterLink **outputs;         ///< array of pointers to output links
#if FF_API_FOO_COUNT
    unsigned output_count;          ///< @deprecated use nb_outputs
#endif
    unsigned    nb_outputs;         ///< number of output pads

    void *priv;                     ///< private data for use by the filter

    struct AVFilterCommand *command_queue;
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
    /* These parameters apply only to audio */
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
     * Lists of formats and channel layouts supported by the input and output
     * filters respectively. These lists are used for negotiating the format
     * to actually be used, which will be loaded into the format and
     * channel_layout members, above, when chosen.
     *
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
     * The buffer reference currently being sent across the link by the source
     * filter. This is used internally by the filter system to allow
     * automatic copying of buffers which do not have sufficient permissions
     * for the destination. This should not be accessed directly by the
     * filters.
     */
    AVFilterBufferRef *src_buf;

    /**
     * The buffer reference to the frame sent across the link by the
     * source filter, which is read by the destination filter. It is
     * automatically set up by ff_start_frame().
     *
     * Depending on the permissions, it may either be the same as
     * src_buf or an automatic copy of it.
     *
     * It is automatically freed by the filter system when calling
     * ff_end_frame(). In case you save the buffer reference
     * internally (e.g. if you cache it for later reuse), or give it
     * away (e.g. if you pass the reference to the next filter) it
     * must be set to NULL before calling ff_end_frame().
     */
    AVFilterBufferRef *cur_buf;

    /**
     * The buffer reference to the frame which is sent to output by
     * the source filter.
     *
     * If no start_frame callback is defined on a link,
     * ff_start_frame() will automatically request a new buffer on the
     * first output link of the destination filter. The reference to
     * the buffer so obtained is stored in the out_buf field on the
     * output link.
     *
     * It can also be set by the filter code in case the filter needs
     * to access the output buffer later. For example the filter code
     * may set it in a custom start_frame, and access it in
     * draw_slice.
     *
     * It is automatically freed by the filter system in
     * ff_end_frame().
     */
    AVFilterBufferRef *out_buf;

    struct AVFilterPool *pool;

    /**
     * Graph the filter belongs to.
     */
    struct AVFilterGraph *graph;

    /**
     * Current timestamp of the link, as defined by the most recent
     * frame(s), in AV_TIME_BASE units.
     */
    int64_t current_pts;

    /**
     * Index in the age array.
     */
    int age_index;

    /**
     * Frame rate of the stream on the link, or 1/0 if unknown;
     * if left to 0/0, will be automatically be copied from the first input
     * of the source filter if it exists.
     *
     * Sources should set it to the best estimation of the real frame rate.
     * Filters should update it if necessary depending on their function.
     * Sinks can use it to set a default output frame rate.
     * It is similar to the r_frame_rate field in AVStream.
     */
    AVRational frame_rate;

    /**
     * Buffer partially filled with samples to achieve a fixed/minimum size.
     */
    AVFilterBufferRef *partial_buf;

    /**
     * Size of the partial buffer to allocate.
     * Must be between min_samples and max_samples.
     */
    int partial_buf_size;

    /**
     * Minimum number of samples to filter at once. If filter_samples() is
     * called with fewer samples, it will accumulate them in partial_buf.
     * This field and the related ones must not be changed after filtering
     * has started.
     * If 0, all related fields are ignored.
     */
    int min_samples;

    /**
     * Maximum number of samples to filter at once. If filter_samples() is
     * called with more samples, it will split them.
     */
    int max_samples;

    /**
     * The buffer reference currently being received across the link by the
     * destination filter. This is used internally by the filter system to
     * allow automatic copying of buffers which do not have sufficient
     * permissions for the destination. This should not be accessed directly
     * by the filters.
     */
    AVFilterBufferRef *cur_buf_copy;

    /**
     * True if the link is closed.
     * If set, all attemps of start_frame, filter_samples or request_frame
     * will fail with AVERROR_EOF, and if necessary the reference will be
     * destroyed.
     * If request_frame returns AVERROR_EOF, this flag is set on the
     * corresponding link.
     * It can be set also be set by either the source or the destination
     * filter.
     */
    int closed;
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
 * Free the link in *link, and set its pointer to NULL.
 */
void avfilter_link_free(AVFilterLink **link);

/**
 * Set the closed field of a link.
 */
void avfilter_link_set_closed(AVFilterLink *link, int closed);

/**
 * Negotiate the media format, dimensions, etc of all inputs to a filter.
 *
 * @param filter the filter to negotiate the properties for its inputs
 * @return       zero on successful negotiation
 */
int avfilter_config_links(AVFilterContext *filter);

/**
 * Create a buffer reference wrapped around an already allocated image
 * buffer.
 *
 * @param data pointers to the planes of the image to reference
 * @param linesize linesizes for the planes of the image to reference
 * @param perms the required access permissions
 * @param w the width of the image specified by the data and linesize arrays
 * @param h the height of the image specified by the data and linesize arrays
 * @param format the pixel format of the image specified by the data and linesize arrays
 */
AVFilterBufferRef *
avfilter_get_video_buffer_ref_from_arrays(uint8_t * const data[4], const int linesize[4], int perms,
                                          int w, int h, enum AVPixelFormat format);

/**
 * Create an audio buffer reference wrapped around an already
 * allocated samples buffer.
 *
 * @param data           pointers to the samples plane buffers
 * @param linesize       linesize for the samples plane buffers
 * @param perms          the required access permissions
 * @param nb_samples     number of samples per channel
 * @param sample_fmt     the format of each sample in the buffer to allocate
 * @param channel_layout the channel layout of the buffer
 */
AVFilterBufferRef *avfilter_get_audio_buffer_ref_from_arrays(uint8_t **data,
                                                             int linesize,
                                                             int perms,
                                                             int nb_samples,
                                                             enum AVSampleFormat sample_fmt,
                                                             uint64_t channel_layout);


#define AVFILTER_CMD_FLAG_ONE   1 ///< Stop once a filter understood the command (for target=all for example), fast filters are favored automatically
#define AVFILTER_CMD_FLAG_FAST  2 ///< Only execute command when its fast (like a video out that supports contrast adjustment in hw)

/**
 * Make the filter instance process a command.
 * It is recommended to use avfilter_graph_send_command().
 */
int avfilter_process_command(AVFilterContext *filter, const char *cmd, const char *arg, char *res, int res_len, int flags);

/** Initialize the filter system. Register all builtin filters. */
void avfilter_register_all(void);

/** Uninitialize the filter system. Unregister all filters. */
void avfilter_uninit(void);

/**
 * Register a filter. This is only needed if you plan to use
 * avfilter_get_by_name later to lookup the AVFilter structure by name. A
 * filter can still by instantiated with avfilter_open even if it is not
 * registered.
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
AVFilter *avfilter_get_by_name(const char *name);

/**
 * If filter is NULL, returns a pointer to the first registered filter pointer,
 * if filter is non-NULL, returns the next pointer after filter.
 * If the returned pointer points to NULL, the last registered filter
 * was already reached.
 */
AVFilter **av_filter_next(AVFilter **filter);

/**
 * Create a filter instance.
 *
 * @param filter_ctx put here a pointer to the created filter context
 * on success, NULL on failure
 * @param filter    the filter to create an instance of
 * @param inst_name Name to give to the new instance. Can be NULL for none.
 * @return >= 0 in case of success, a negative error code otherwise
 */
int avfilter_open(AVFilterContext **filter_ctx, AVFilter *filter, const char *inst_name);

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
int avfilter_init_filter(AVFilterContext *filter, const char *args, void *opaque);

/**
 * Free a filter context.
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

#endif /* AVFILTER_AVFILTER_H */
