/*
 * Filter layer
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

#ifndef FFMPEG_AVFILTER_H
#define FFMPEG_AVFILTER_H

#include <stddef.h>
#include "avcodec.h"

typedef struct AVFilterContext AVFilterContext;
typedef struct AVFilterLink    AVFilterLink;
typedef struct AVFilterPad     AVFilterPad;

/* TODO: look for other flags which may be useful in this structure (interlace
 * flags, etc)
 */
/**
 * A reference-counted picture data type used by the filter system.  Filters
 * should not store pointers to this structure directly, but instead use the
 * AVFilterPicRef structure below
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

    int perms;                  ///< permissions
#define AV_PERM_READ     0x01   ///< can read from the buffer
#define AV_PERM_WRITE    0x02   ///< can write to the buffer
#define AV_PERM_PRESERVE 0x04   ///< nobody else can overwrite the buffer
#define AV_PERM_REUSE    0x08   ///< can output the buffer multiple times, with the same contents each time
#define AV_PERM_REUSE2   0x10   ///< can output the buffer multiple times, modified each time
} AVFilterPicRef;

/**
 * Add a new reference to a picture.
 * @param ref   An existing reference to the picture
 * @param pmask A bitmask containing the allowable permissions in the new
 *              reference
 * @return      A new reference to the picture with the same properties as the
 *              old, excluding any permissions denied by pmask
 */
AVFilterPicRef *avfilter_ref_pic(AVFilterPicRef *ref, int pmask);

/**
 * Remove a reference to a picture.  If this is the last reference to the
 * picture, the picture itself is also automatically freed.
 * @param ref Reference to the picture.
 */
void avfilter_unref_pic(AVFilterPicRef *ref);

/**
 * A filter pad used for either input or output
 */
struct AVFilterPad
{
    /**
     * Pad name.  The name is unique among inputs and among outputs, but an
     * input may have the same name as an output.  This may be NULL if this
     * pad has no need to ever be referenced by name.
     */
    char *name;

    /**
     * AVFilterPad type.  Only video supported now, hopefully someone will
     * add audio in the future.
     */
    int type;
#define AV_PAD_VIDEO 0      ///< video pad

    /**
     * Minimum required permissions on incoming buffers.  Any buffers with
     * insufficient permissions will be automatically copied by the filter
     * system to a new buffer which provides the needed access permissions.
     *
     * Input pads only.
     */
    int min_perms;

    /**
     * Permissions which are not accepted on incoming buffers.  Any buffer
     * which has any of these permissions set be automatically copied by the
     * filter system to a new buffer which does not have those permissions.
     * This can be used to easily disallow buffers with AV_PERM_REUSE.
     *
     * Input pads only.
     */
    int rej_perms;

    /**
     * Callback to get a list of supported formats.  The returned list should
     * be terminated by -1 (see avfilter_make_format_list for an easy way to
     * create such a list).
     *
     * This is used for both input and output pads.  If ommitted from an output
     * pad, it is assumed that the only format supported is the same format
     * that is being used for the filter's first input.  If the filter has no
     * inputs, then this may not be ommitted for its output pads.
     */
    int *(*query_formats)(AVFilterLink *link);

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

/** Default handler for start_frame() for video inputs */
void avfilter_default_start_frame(AVFilterLink *link, AVFilterPicRef *picref);
/** Default handler for end_frame() for video inputs */
void avfilter_default_end_frame(AVFilterLink *link);
/** Default handler for config_props() for video outputs */
int avfilter_default_config_output_link(AVFilterLink *link);
/** Default handler for config_props() for video inputs */
int avfilter_default_config_input_link (AVFilterLink *link);
/** Default handler for query_formats() for video outputs */
int *avfilter_default_query_output_formats(AVFilterLink *link);
/** Default handler for get_video_buffer() for video inputs */
AVFilterPicRef *avfilter_default_get_video_buffer(AVFilterLink *link,
                                                  int perms);

/**
 * Filter definition.  This defines the pads a filter contains, and all the
 * callback functions used to interact with the filter.
 */
typedef struct
{
    char *name;         ///< filter name
    char *author;       ///< filter author

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

    const AVFilterPad *inputs;  ///< NULL terminated list of inputs. NULL if none
    const AVFilterPad *outputs; ///< NULL terminated list of outputs. NULL if none
} AVFilter;

/** An instance of a filter */
struct AVFilterContext
{
    AVClass *av_class;              ///< Needed for av_log()

    AVFilter *filter;               ///< The AVFilter of which this is an instance

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
 * A links between two filters.  This contains pointers to the source and
 * destination filters between which this link exists, and the indices of
 * the pads involved.  In addition, this link also contains the parameters
 * which have been negotiated and agreed upon between the filter, such as
 * image dimensions, format, etc
 */
struct AVFilterLink
{
    AVFilterContext *src;       ///< source filter
    unsigned int srcpad;        ///< index of the output pad on the source filter

    AVFilterContext *dst;       ///< dest filter
    unsigned int dstpad;        ///< index of the input pad on the dest filter

    int w;                      ///< agreed upon image width
    int h;                      ///< agreed upon image height
    enum PixelFormat format;    ///< agreed upon image colorspace

    /**
     * The picture reference currently being sent across the link by the source
     * filter.  This is used internally by the filter system to allow
     * automatic copying of pictures which d not have sufficient permissions
     * for the destination.  This should not be accessed directly by the
     * filters.
     */
    AVFilterPicRef *srcpic;

    AVFilterPicRef *cur_pic;
    AVFilterPicRef *outpic;
};

/**
 * Link two filters together
 * @param src    The source filter
 * @param srcpad Index of the output pad on the source filter
 * @param dst    The destination filter
 * @param dstpad Index of the input pad on the destination filter
 * @return       Zero on success
 */
int avfilter_link(AVFilterContext *src, unsigned srcpad,
                  AVFilterContext *dst, unsigned dstpad);

/**
 * Negotiate the colorspace, dimensions, etc of a link
 * @param link The link to negotiate the properties of
 * @return     Zero on successful negotiation
 */
int avfilter_config_link(AVFilterLink *link);

/**
 * Request a picture buffer with a specific set of permissions
 * @param link  The output link to the filter from which the picture will
 *              be requested
 * @param perms The required access permissions
 * @return      A reference to the picture.  This must be unreferenced with
 *              avfilter_unref_pic when you are finished with it.
 */
AVFilterPicRef *avfilter_get_video_buffer(AVFilterLink *link, int perms);

/**
 * Request an input frame from the filter at the other end of the link.
 * @param link The input link
 * @return     Zero on success
 */
int  avfilter_request_frame(AVFilterLink *link);

/**
 * Notify the next filter of the start of a frame.
 * @param link   The output link the frame will be sent over
 * @param picref A reference to the frame about to be sent.  The data for this
 *               frame need only be valid once draw_slice() is called for that
 *               portion.  The receiving filter will free this reference when
 *               it no longer needs it.
 */
void avfilter_start_frame(AVFilterLink *link, AVFilterPicRef *picref);

/**
 * Notify the next filter that the current frame has finished
 * @param link The output link the frame was sent over
 */
void avfilter_end_frame(AVFilterLink *link);

/**
 * Send a slice to the next filter
 * @param link The output link over which the frame is being sent
 * @param y    Offset in pixels from the top of the image for this slice
 * @param h    Height of this slice in pixels
 */
void avfilter_draw_slice(AVFilterLink *link, int y, int h);

/** Initialize the filter system.  Registers all builtin filters */
void avfilter_init(void);

/** Uninitialize the filter system.  Unregisters all filters */
void avfilter_uninit(void);

/**
 * Register a filter.  This is only needed if you plan to use
 * avfilter_get_by_name later to lookup the AVFilter structure by name. A
 * filter can still by instantiated with avfilter_open even if it is not
 * registered.
 * @param filter The filter to register
 */
void avfilter_register(AVFilter *filter);

/**
 * Gets a filter definition matching the given name
 * @param name The filter name to find
 * @return     The filter definition, if any matching one is registered.
 *             NULL if none found.
 */
AVFilter *avfilter_get_by_name(char *name);

/**
 * Create a filter instance
 * @param filter    The filter to create an instance of
 * @param inst_name Name to give to the new instance.  Can be NULL for none.
 * @return          Pointer to the new instance on success.  NULL on failure.
 */
AVFilterContext *avfilter_open(AVFilter *filter, char *inst_name);

/**
 * Initialize a filter
 * @param filter The filter to initialize
 * @param args   A string of parameters to use when initializing the filter.
 *               The format and meaning of this string varies by filter.
 * @param opaque Any extra non-string data needed by the filter.  The meaning
 *               of this parameter varies by filter.
 * @return       Zero on success
 */
int avfilter_init_filter(AVFilterContext *filter, const char *args, void *opaque);

/**
 * Destroy a filter
 * @param filter The filter to destroy
 */
void avfilter_destroy(AVFilterContext *filter);

/**
 * Helper function to create a list of supported formats.  This is intended
 * for use in AVFilterPad->query_formats().
 * @param len The number of formats supported
 * @param ... A list of the supported formats
 * @return    The format list in a form suitable for returning from
 *            AVFilterPad->query_formats()
 */
int *avfilter_make_format_list(int len, ...);

/**
 * Insert a new pad
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

/** insert a new input pad for the filter */
static inline void avfilter_insert_inpad(AVFilterContext *f, unsigned index,
                                         AVFilterPad *p)
{
    avfilter_insert_pad(index, &f->input_count, offsetof(AVFilterLink, dstpad),
                        &f->input_pads, &f->inputs, p);
}

/** insert a new output pad for the filter */
static inline void avfilter_insert_outpad(AVFilterContext *f, unsigned index,
                                          AVFilterPad *p)
{
    avfilter_insert_pad(index, &f->output_count, offsetof(AVFilterLink, srcpad),
                        &f->output_pads, &f->outputs, p);
}

#endif  /* FFMPEG_AVFILTER_H */
