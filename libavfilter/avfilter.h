/*
 * filter layer
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

#ifndef AVFILTER_AVFILTER_H
#define AVFILTER_AVFILTER_H

#define LIBAVFILTER_VERSION_MAJOR  0
#define LIBAVFILTER_VERSION_MINOR  4
#define LIBAVFILTER_VERSION_MICRO  0

#define LIBAVFILTER_VERSION_INT AV_VERSION_INT(LIBAVFILTER_VERSION_MAJOR, \
                                               LIBAVFILTER_VERSION_MINOR, \
                                               LIBAVFILTER_VERSION_MICRO)
#define LIBAVFILTER_VERSION     AV_VERSION(LIBAVFILTER_VERSION_MAJOR,   \
                                           LIBAVFILTER_VERSION_MINOR,   \
                                           LIBAVFILTER_VERSION_MICRO)
#define LIBAVFILTER_BUILD       LIBAVFILTER_VERSION_INT

#include <stddef.h>
#include "libavcodec/avcodec.h"

/**
 * Returns the LIBAVFILTER_VERSION_INT constant.
 */
unsigned avfilter_version(void);

typedef struct AVFilterContext AVFilterContext;
typedef struct AVFilterLink    AVFilterLink;
typedef struct AVFilterPad     AVFilterPad;

/* TODO: look for other flags which may be useful in this structure (interlace
 * flags, etc)
 */
/**
 * A reference-counted picture data type used by the filter system.  Filters
 * should not store pointers to this structure directly, but instead use the
 * AVFilterPicRef structure below.
 */
typedef struct AVFilterPic
{
    uint8_t *data[4];           ///< picture data for each plane
    int linesize[4];            ///< number of bytes per line
    enum PixelFormat format;    ///< colorspace

    unsigned refcount;          ///< number of references to this image

    /** private data to be used by a custom free function */
    void *priv;
    /**
     * A pointer to the function to deallocate this image if the default
     * function is not sufficient.  This could, for example, add the memory
     * back into a memory pool to be reused later without the overhead of
     * reallocating it from scratch.
     */
    void (*free)(struct AVFilterPic *pic);
} AVFilterPic;

/**
 * A reference to an AVFilterPic.  Since filters can manipulate the origin of
 * a picture to, for example, crop image without any memcpy, the picture origin
 * and dimensions are per-reference properties.  Linesize is also useful for
 * image flipping, frame to field filters, etc, and so is also per-reference.
 *
 * TODO: add anything necessary for frame reordering
 */
typedef struct AVFilterPicRef
{
    AVFilterPic *pic;           ///< the picture that this is a reference to
    uint8_t *data[4];           ///< picture data for each plane
    int linesize[4];            ///< number of bytes per line
    int w;                      ///< image width
    int h;                      ///< image height

    int64_t pts;                ///< presentation timestamp in units of 1/AV_TIME_BASE

    AVRational pixel_aspect;    ///< pixel aspect ratio

    int perms;                  ///< permissions
#define AV_PERM_READ     0x01   ///< can read from the buffer
#define AV_PERM_WRITE    0x02   ///< can write to the buffer
#define AV_PERM_PRESERVE 0x04   ///< nobody else can overwrite the buffer
#define AV_PERM_REUSE    0x08   ///< can output the buffer multiple times, with the same contents each time
#define AV_PERM_REUSE2   0x10   ///< can output the buffer multiple times, modified each time
} AVFilterPicRef;

/**
 * Add a new reference to a picture.
 * @param ref   an existing reference to the picture
 * @param pmask a bitmask containing the allowable permissions in the new
 *              reference
 * @return      a new reference to the picture with the same properties as the
 *              old, excluding any permissions denied by pmask
 */
AVFilterPicRef *avfilter_ref_pic(AVFilterPicRef *ref, int pmask);

/**
 * Remove a reference to a picture.  If this is the last reference to the
 * picture, the picture itself is also automatically freed.
 * @param ref reference to the picture
 */
void avfilter_unref_pic(AVFilterPicRef *ref);

/**
 * A list of supported formats for one end of a filter link. This is used
 * during the format negotiation process to try to pick the best format to
 * use to minimize the number of necessary conversions. Each filter gives a
 * list of the formats supported by each input and output pad. The list
 * given for each pad need not be distinct - they may be references to the
 * same list of formats, as is often the case when a filter supports multiple
 * formats, but will always output the same format as it is given in input.
 *
 * In this way, a list of possible input formats and a list of possible
 * output formats are associated with each link. When a set of formats is
 * negotiated over a link, the input and output lists are merged to form a
 * new list containing only the common elements of each list. In the case
 * that there were no common elements, a format conversion is necessary.
 * Otherwise, the lists are merged, and all other links which reference
 * either of the format lists involved in the merge are also affected.
 *
 * For example, consider the filter chain:
 * filter (a) --> (b) filter (b) --> (c) filter
 *
 * where the letters in parenthesis indicate a list of formats supported on
 * the input or output of the link. Suppose the lists are as follows:
 * (a) = {A, B}
 * (b) = {A, B, C}
 * (c) = {B, C}
 *
 * First, the first link's lists are merged, yielding:
 * filter (a) --> (a) filter (a) --> (c) filter
 *
 * Notice that format list (b) now refers to the same list as filter list (a).
 * Next, the lists for the second link are merged, yielding:
 * filter (a) --> (a) filter (a) --> (a) filter
 *
 * where (a) = {B}.
 *
 * Unfortunately, when the format lists at the two ends of a link are merged,
 * we must ensure that all links which reference either pre-merge format list
 * get updated as well. Therefore, we have the format list structure store a
 * pointer to each of the pointers to itself.
 */
typedef struct AVFilterFormats AVFilterFormats;
struct AVFilterFormats
{
    unsigned format_count;      ///< number of formats
    int *formats;               ///< list of formats

    unsigned refcount;          ///< number of references to this list
    AVFilterFormats ***refs;    ///< references to this list
};

/**
 * Helper function to create a list of supported formats.  This is intended
 * for use in AVFilter->query_formats().
 * @param len the number of formats supported
 * @param ... a list of the supported formats
 * @return    the format list, with no existing references
 */
AVFilterFormats *avfilter_make_format_list(int len, ...);

/**
 * Returns a list of all colorspaces supported by FFmpeg.
 */
AVFilterFormats *avfilter_all_colorspaces(void);

/**
 * Returns a format list which contains the intersection of the formats of
 * a and b. Also, all the references of a, all the references of b, and
 * a and b themselves will be deallocated.
 *
 * If a and b do not share any common formats, neither is modified, and NULL
 * is returned.
 */
AVFilterFormats *avfilter_merge_formats(AVFilterFormats *a, AVFilterFormats *b);

/**
 * Adds *ref as a new reference to formats.
 * That is the pointers will point like in the ascii art below:
 *   ________
 *  |formats |<--------.
 *  |  ____  |     ____|___________________
 *  | |refs| |    |  __|_
 *  | |* * | |    | |  | |  AVFilterLink
 *  | |* *--------->|*ref|
 *  | |____| |    | |____|
 *  |________|    |________________________
 */
void avfilter_formats_ref(AVFilterFormats *formats, AVFilterFormats **ref);

/**
 * Remove *ref as a reference to the format list it currently points to,
 * deallocate that list if this was the last reference, and set *ref to NULL.
 *
 *         Before                                 After
 *   ________                               ________         NULL
 *  |formats |<--------.                   |formats |         ^
 *  |  ____  |     ____|________________   |  ____  |     ____|________________
 *  | |refs| |    |  __|_                  | |refs| |    |  __|_
 *  | |* * | |    | |  | |  AVFilterLink   | |* * | |    | |  | |  AVFilterLink
 *  | |* *--------->|*ref|                 | |*   | |    | |*ref|
 *  | |____| |    | |____|                 | |____| |    | |____|
 *  |________|    |_____________________   |________|    |_____________________
 */
void avfilter_formats_unref(AVFilterFormats **ref);

/**
 *
 *         Before                                 After
 *   ________                         ________
 *  |formats |<---------.            |formats |<---------.
 *  |  ____  |       ___|___         |  ____  |       ___|___
 *  | |refs| |      |   |   |        | |refs| |      |   |   |   NULL
 *  | |* *--------->|*oldref|        | |* *--------->|*newref|     ^
 *  | |* * | |      |_______|        | |* * | |      |_______|  ___|___
 *  | |____| |                       | |____| |                |   |   |
 *  |________|                       |________|                |*oldref|
 *                                                             |_______|
 */
void avfilter_formats_changeref(AVFilterFormats **oldref,
                                AVFilterFormats **newref);

/**
 * A filter pad used for either input or output.
 */
struct AVFilterPad
{
    /**
     * Pad name.  The name is unique among inputs and among outputs, but an
     * input may have the same name as an output.  This may be NULL if this
     * pad has no need to ever be referenced by name.
     */
    const char *name;

    /**
     * AVFilterPad type.  Only video supported now, hopefully someone will
     * add audio in the future.
     */
    enum CodecType type;

    /**
     * Minimum required permissions on incoming buffers.  Any buffer with
     * insufficient permissions will be automatically copied by the filter
     * system to a new buffer which provides the needed access permissions.
     *
     * Input pads only.
     */
    int min_perms;

    /**
     * Permissions which are not accepted on incoming buffers.  Any buffer
     * which has any of these permissions set will be automatically copied
     * by the filter system to a new buffer which does not have those
     * permissions.  This can be used to easily disallow buffers with
     * AV_PERM_REUSE.
     *
     * Input pads only.
     */
    int rej_perms;

    /**
     * Callback called before passing the first slice of a new frame.  If
     * NULL, the filter layer will default to storing a reference to the
     * picture inside the link structure.
     *
     * Input video pads only.
     */
    void (*start_frame)(AVFilterLink *link, AVFilterPicRef *picref);

    /**
     * Callback function to get a buffer.  If NULL, the filter system will
     * handle buffer requests.
     *
     * Input video pads only.
     */
    AVFilterPicRef *(*get_video_buffer)(AVFilterLink *link, int perms);

    /**
     * Callback called after the slices of a frame are completely sent.  If
     * NULL, the filter layer will default to releasing the reference stored
     * in the link structure during start_frame().
     *
     * Input video pads only.
     */
    void (*end_frame)(AVFilterLink *link);

    /**
     * Slice drawing callback.  This is where a filter receives video data
     * and should do its processing.
     *
     * Input video pads only.
     */
    void (*draw_slice)(AVFilterLink *link, int y, int height);

    /**
     * Frame poll callback.  This returns the number of immediately available
     * frames. It should return a positive value if the next request_frame()
     * is guaranteed to return one frame (with no delay).
     *
     * Defaults to just calling the source poll_frame() method.
     *
     * Output video pads only.
     */
    int (*poll_frame)(AVFilterLink *link);

    /**
     * Frame request callback.  A call to this should result in at least one
     * frame being output over the given link.  This should return zero on
     * success, and another value on error.
     *
     * Output video pads only.
     */
    int (*request_frame)(AVFilterLink *link);

    /**
     * Link configuration callback.
     *
     * For output pads, this should set the link properties such as
     * width/height.  This should NOT set the format property - that is
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

/** default handler for start_frame() for video inputs */
void avfilter_default_start_frame(AVFilterLink *link, AVFilterPicRef *picref);
/** default handler for draw_slice() for video inputs */
void avfilter_default_draw_slice(AVFilterLink *link, int y, int h);
/** default handler for end_frame() for video inputs */
void avfilter_default_end_frame(AVFilterLink *link);
/** default handler for config_props() for video outputs */
int avfilter_default_config_output_link(AVFilterLink *link);
/** default handler for config_props() for video inputs */
int avfilter_default_config_input_link (AVFilterLink *link);
/** default handler for get_video_buffer() for video inputs */
AVFilterPicRef *avfilter_default_get_video_buffer(AVFilterLink *link,
                                                  int perms);
/**
 * A helper for query_formats() which sets all links to the same list of
 * formats. If there are no links hooked to this filter, the list of formats is
 * freed.
 */
void avfilter_set_common_formats(AVFilterContext *ctx, AVFilterFormats *formats);
/** Default handler for query_formats() */
int avfilter_default_query_formats(AVFilterContext *ctx);

/**
 * Filter definition.  This defines the pads a filter contains, and all the
 * callback functions used to interact with the filter.
 */
typedef struct
{
    const char *name;         ///< filter name

    int priv_size;      ///< size of private data to allocate for the filter

    /**
     * Filter initialization function.  Args contains the user-supplied
     * parameters.  FIXME: maybe an AVOption-based system would be better?
     * opaque is data provided by the code requesting creation of the filter,
     * and is used to pass data to the filter.
     */
    int (*init)(AVFilterContext *ctx, const char *args, void *opaque);

    /**
     * Filter uninitialization function.  Should deallocate any memory held
     * by the filter, release any picture references, etc.  This does not need
     * to deallocate the AVFilterContext->priv memory itself.
     */
    void (*uninit)(AVFilterContext *ctx);

    /**
     * Query formats supported by the filter and its pads. Should set the
     * in_formats for links connected to its output pads, and out_formats
     * for links connected to its input pads.
     *
     * Should return zero on success.
     */
    int (*query_formats)(AVFilterContext *);

    const AVFilterPad *inputs;  ///< NULL terminated list of inputs. NULL if none
    const AVFilterPad *outputs; ///< NULL terminated list of outputs. NULL if none
} AVFilter;

/** An instance of a filter */
struct AVFilterContext
{
    const AVClass *av_class;              ///< needed for av_log()

    AVFilter *filter;               ///< the AVFilter of which this is an instance

    char *name;                     ///< name of this filter instance

    unsigned input_count;           ///< number of input pads
    AVFilterPad   *input_pads;      ///< array of input pads
    AVFilterLink **inputs;          ///< array of pointers to input links

    unsigned output_count;          ///< number of output pads
    AVFilterPad   *output_pads;     ///< array of output pads
    AVFilterLink **outputs;         ///< array of pointers to output links

    void *priv;                     ///< private data for use by the filter
};

/**
 * A link between two filters.  This contains pointers to the source and
 * destination filters between which this link exists, and the indexes of
 * the pads involved.  In addition, this link also contains the parameters
 * which have been negotiated and agreed upon between the filter, such as
 * image dimensions, format, etc.
 */
struct AVFilterLink
{
    AVFilterContext *src;       ///< source filter
    unsigned int srcpad;        ///< index of the output pad on the source filter

    AVFilterContext *dst;       ///< dest filter
    unsigned int dstpad;        ///< index of the input pad on the dest filter

    /** stage of the initialization of the link properties (dimensions, etc) */
    enum {
        AVLINK_UNINIT = 0,      ///< not started
        AVLINK_STARTINIT,       ///< started, but incomplete
        AVLINK_INIT             ///< complete
    } init_state;

    int w;                      ///< agreed upon image width
    int h;                      ///< agreed upon image height
    enum PixelFormat format;    ///< agreed upon image colorspace

    /**
     * Lists of formats supported by the input and output filters respectively.
     * These lists are used for negotiating the format to actually be used,
     * which will be loaded into the format member, above, when chosen.
     */
    AVFilterFormats *in_formats;
    AVFilterFormats *out_formats;

    /**
     * The picture reference currently being sent across the link by the source
     * filter.  This is used internally by the filter system to allow
     * automatic copying of pictures which do not have sufficient permissions
     * for the destination.  This should not be accessed directly by the
     * filters.
     */
    AVFilterPicRef *srcpic;

    AVFilterPicRef *cur_pic;
    AVFilterPicRef *outpic;
};

/**
 * Link two filters together.
 * @param src    the source filter
 * @param srcpad index of the output pad on the source filter
 * @param dst    the destination filter
 * @param dstpad index of the input pad on the destination filter
 * @return       zero on success
 */
int avfilter_link(AVFilterContext *src, unsigned srcpad,
                  AVFilterContext *dst, unsigned dstpad);

/**
 * Negotiate the colorspace, dimensions, etc of all inputs to a filter.
 * @param filter the filter to negotiate the properties for its inputs
 * @return       zero on successful negotiation
 */
int avfilter_config_links(AVFilterContext *filter);

/**
 * Request a picture buffer with a specific set of permissions.
 * @param link  the output link to the filter from which the picture will
 *              be requested
 * @param perms the required access permissions
 * @return      A reference to the picture.  This must be unreferenced with
 *              avfilter_unref_pic when you are finished with it.
 */
AVFilterPicRef *avfilter_get_video_buffer(AVFilterLink *link, int perms);

/**
 * Request an input frame from the filter at the other end of the link.
 * @param link the input link
 * @return     zero on success
 */
int avfilter_request_frame(AVFilterLink *link);

/**
 * Poll a frame from the filter chain.
 * @param  link the input link
 * @return the number of immediately available frames, a negative
 * number in case of error
 */
int avfilter_poll_frame(AVFilterLink *link);

/**
 * Notify the next filter of the start of a frame.
 * @param link   the output link the frame will be sent over
 * @param picref A reference to the frame about to be sent.  The data for this
 *               frame need only be valid once draw_slice() is called for that
 *               portion.  The receiving filter will free this reference when
 *               it no longer needs it.
 */
void avfilter_start_frame(AVFilterLink *link, AVFilterPicRef *picref);

/**
 * Notify the next filter that the current frame has finished.
 * @param link the output link the frame was sent over
 */
void avfilter_end_frame(AVFilterLink *link);

/**
 * Send a slice to the next filter.
 * @param link the output link over which the frame is being sent
 * @param y    offset in pixels from the top of the image for this slice
 * @param h    height of this slice in pixels
 */
void avfilter_draw_slice(AVFilterLink *link, int y, int h);

/** Initialize the filter system.  Registers all builtin filters */
void avfilter_register_all(void);

/** Uninitialize the filter system.  Unregisters all filters */
void avfilter_uninit(void);

/**
 * Register a filter.  This is only needed if you plan to use
 * avfilter_get_by_name later to lookup the AVFilter structure by name. A
 * filter can still by instantiated with avfilter_open even if it is not
 * registered.
 * @param filter the filter to register
 */
void avfilter_register(AVFilter *filter);

/**
 * Gets a filter definition matching the given name.
 * @param name the filter name to find
 * @return     the filter definition, if any matching one is registered.
 *             NULL if none found.
 */
AVFilter *avfilter_get_by_name(const char *name);

/**
 * Create a filter instance.
 * @param filter    the filter to create an instance of
 * @param inst_name Name to give to the new instance.  Can be NULL for none.
 * @return          Pointer to the new instance on success.  NULL on failure.
 */
AVFilterContext *avfilter_open(AVFilter *filter, const char *inst_name);

/**
 * Initialize a filter.
 * @param filter the filter to initialize
 * @param args   A string of parameters to use when initializing the filter.
 *               The format and meaning of this string varies by filter.
 * @param opaque Any extra non-string data needed by the filter.  The meaning
 *               of this parameter varies by filter.
 * @return       zero on success
 */
int avfilter_init_filter(AVFilterContext *filter, const char *args, void *opaque);

/**
 * Destroy a filter.
 * @param filter the filter to destroy
 */
void avfilter_destroy(AVFilterContext *filter);

/**
 * Insert a filter in the middle of an existing link.
 * @param link the link into which the filter should be inserted
 * @param filt the filter to be inserted
 * @param in   the input pad on the filter to connect
 * @param out  the output pad on the filter to connect
 * @return     zero on success
 */
int avfilter_insert_filter(AVFilterLink *link, AVFilterContext *filt,
                           unsigned in, unsigned out);

/**
 * Insert a new pad.
 * @param idx Insertion point.  Pad is inserted at the end if this point
 *            is beyond the end of the list of pads.
 * @param count Pointer to the number of pads in the list
 * @param padidx_off Offset within an AVFilterLink structure to the element
 *                   to increment when inserting a new pad causes link
 *                   numbering to change
 * @param pads Pointer to the pointer to the beginning of the list of pads
 * @param links Pointer to the pointer to the beginning of the list of links
 * @param newpad The new pad to add. A copy is made when adding.
 */
void avfilter_insert_pad(unsigned idx, unsigned *count, size_t padidx_off,
                         AVFilterPad **pads, AVFilterLink ***links,
                         AVFilterPad *newpad);

/** Insert a new input pad for the filter. */
static inline void avfilter_insert_inpad(AVFilterContext *f, unsigned index,
                                         AVFilterPad *p)
{
    avfilter_insert_pad(index, &f->input_count, offsetof(AVFilterLink, dstpad),
                        &f->input_pads, &f->inputs, p);
}

/** Insert a new output pad for the filter. */
static inline void avfilter_insert_outpad(AVFilterContext *f, unsigned index,
                                          AVFilterPad *p)
{
    avfilter_insert_pad(index, &f->output_count, offsetof(AVFilterLink, srcpad),
                        &f->output_pads, &f->outputs, p);
}

#endif  /* AVFILTER_AVFILTER_H */
