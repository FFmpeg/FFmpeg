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
    uint8_t *data[4];
    int linesize[4];    ///< number of bytes per line
    enum PixelFormat format;

    unsigned refcount;
    void *priv;
    void (*free)(struct AVFilterPic *pic);
} AVFilterPic;

/**
 * A reference to an AVFilterPic.  Since filters can manipulate the origin of
 * a picture to, for example, crop image without any memcpy, the picture origin
 * and dimensions are per-reference properties.  Linesize is also useful for
 * image flipping, frame to field filters, etc, and so is also per-reference.
 *
 * TODO: add pts, and anything necessary for frame reordering
 */
typedef struct AVFilterPicRef
{
    AVFilterPic *pic;
    uint8_t *data[4];
    int linesize[4];
    int w, h;

    int perms;                  ///< permissions
#define AV_PERM_READ     0x01   ///< can read from the buffer
#define AV_PERM_WRITE    0x02   ///< can write to the buffer
#define AV_PERM_PRESERVE 0x04   ///< nobody else can overwrite the buffer
#define AV_PERM_REUSE    0x08   ///< can output the buffer multiple times
} AVFilterPicRef;

/**
 * Add a new reference to a picture.
 * @param ref An existing reference to the picture
 * @param pmask A bitmask containing the allowable permissions in the new reference
 * @return A new reference to the picture with the same properties as the old
 */
AVFilterPicRef *avfilter_ref_pic(AVFilterPicRef *ref, int pmask);

/**
 * Remove a reference to a picture.  If this is the last reference to the
 * picture, the picture itself is also automatically freed.
 * @param ref Reference to the picture.
 */
void avfilter_unref_pic(AVFilterPicRef *ref);

struct AVFilterPad
{
    /**
     * Pad name.  The name is unique among inputs and among oututs, but an
     * input may have the same name as an output.
     */
    char *name;

    /**
     * AVFilterPad type.  Only video supported now, hopefully someone will
     * add audio in the future.
     */
    int type;
#define AV_PAD_VIDEO 0

    /**
     * Callback to get a list of supported formats.  The returned list should
     * be terminated by -1.  This is used for both input and output pads and
     * is required for both.
     */
    int *(*query_formats)(AVFilterLink *link);

    /**
     * Callback called before passing the first slice of a new frame.  If
     * NULL, the filter layer will default to storing a reference to the
     * picture inside the link structure.
     */
    void (*start_frame)(AVFilterLink *link, AVFilterPicRef *picref);

    /**
     * Callback function to get a buffer.  If NULL, the filter system will
     * handle buffer requests.  Only required for input video pads.
     */
    AVFilterPicRef *(*get_video_buffer)(AVFilterLink *link, int perms);

    /**
     * Callback called after the slices of a frame are completely sent.  If
     * NULL, the filter layer will default to releasing the reference stored
     * in the link structure during start_frame().
     */
    void (*end_frame)(AVFilterLink *link);

    /**
     * Slice drawing callback.  This is where a filter receives video data
     * and should do its processing.  Only required for input video pads.
     */
    void (*draw_slice)(AVFilterLink *link, uint8_t *data[4], int y, int height);

    /**
     * Frame request callback.  A call to this should result in at least one
     * frame being output over the given link.  Video output pads only.
     */
    void (*request_frame)(AVFilterLink *link);

    /**
     * Link configuration callback.  For output pads, this should set the link
     * properties such as width/height.  NOTE: this should not set the format
     * property - that is negotiated between filters by the filter system using
     * the query_formats() callback.
     *
     * For input pads, this should check the properties of the link, and update
     * the filter's internal state as necessary.
     */
    int (*config_props)(AVFilterLink *link);
};

/* the default implementations of start_frame() and end_frame() */
void avfilter_default_start_frame(AVFilterLink *link, AVFilterPicRef *picref);
void avfilter_default_end_frame(AVFilterLink *link);

typedef struct
{
    char *name;
    char *author;

    int priv_size;

    /**
     * Filter initialization function.  Args contains the user-supplied
     * parameters.  FIXME: maybe an AVOption-based system would be better?
     */
    int (*init)(AVFilterContext *ctx, const char *args);
    void (*uninit)(AVFilterContext *ctx);

    const AVFilterPad *inputs;  /// NULL terminated list of inputs. NULL if none
    const AVFilterPad *outputs; /// NULL terminated list of outputs. NULL if none
} AVFilter;

struct AVFilterContext
{
    AVClass *av_class;

    AVFilter *filter;

    AVFilterLink **inputs;
    AVFilterLink **outputs;

    void *priv;
};

struct AVFilterLink
{
    AVFilterContext *src;
    unsigned int srcpad;

    AVFilterContext *dst;
    unsigned int dstpad;

    int w, h;
    enum PixelFormat format;

    AVFilterPicRef *cur_pic;
    AVFilterPicRef *outpic;
};

/** Link two filters together */
int avfilter_link(AVFilterContext *src, unsigned srcpad,
                  AVFilterContext *dst, unsigned dstpad);

AVFilterPicRef *avfilter_get_video_buffer(AVFilterLink *link, int perms);
void avfilter_request_frame(AVFilterLink *link);
void avfilter_start_frame(AVFilterLink *link, AVFilterPicRef *picref);
void avfilter_end_frame(AVFilterLink *link);
void avfilter_draw_slice(AVFilterLink *link, uint8_t *data[4], int y, int h);

void avfilter_init(void);
void avfilter_uninit(void);
void avfilter_register(AVFilter *filter);
AVFilter *avfilter_get_by_name(char *name);

AVFilterContext *avfilter_create(AVFilter *filter);
AVFilterContext *avfilter_create_by_name(char *name);
int avfilter_init_filter(AVFilterContext *filter, const char *args);
void avfilter_destroy(AVFilterContext *filter);

int *avfilter_make_format_list(int len, ...);

#endif  /* FFMPEG_AVFILTER_H */
