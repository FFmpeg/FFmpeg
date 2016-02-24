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

#ifndef AVUTIL_HWCONTEXT_H
#define AVUTIL_HWCONTEXT_H

#include "buffer.h"
#include "frame.h"
#include "log.h"
#include "pixfmt.h"

enum AVHWDeviceType {
    AV_HWDEVICE_TYPE_VDPAU,
    AV_HWDEVICE_TYPE_CUDA,
};

typedef struct AVHWDeviceInternal AVHWDeviceInternal;

/**
 * This struct aggregates all the (hardware/vendor-specific) "high-level" state,
 * i.e. state that is not tied to a concrete processing configuration.
 * E.g., in an API that supports hardware-accelerated encoding and decoding,
 * this struct will (if possible) wrap the state that is common to both encoding
 * and decoding and from which specific instances of encoders or decoders can be
 * derived.
 *
 * This struct is reference-counted with the AVBuffer mechanism. The
 * av_hwdevice_ctx_alloc() constructor yields a reference, whose data field
 * points to the actual AVHWDeviceContext. Further objects derived from
 * AVHWDeviceContext (such as AVHWFramesContext, describing a frame pool with
 * specific properties) will hold an internal reference to it. After all the
 * references are released, the AVHWDeviceContext itself will be freed,
 * optionally invoking a user-specified callback for uninitializing the hardware
 * state.
 */
typedef struct AVHWDeviceContext {
    /**
     * A class for logging. Set by av_hwdevice_ctx_alloc().
     */
    const AVClass *av_class;

    /**
     * Private data used internally by libavutil. Must not be accessed in any
     * way by the caller.
     */
    AVHWDeviceInternal *internal;

    /**
     * This field identifies the underlying API used for hardware access.
     *
     * This field is set when this struct is allocated and never changed
     * afterwards.
     */
    enum AVHWDeviceType type;

    /**
     * The format-specific data, allocated and freed by libavutil along with
     * this context.
     *
     * Should be cast by the user to the format-specific context defined in the
     * corresponding header (hwcontext_*.h) and filled as described in the
     * documentation before calling av_hwdevice_ctx_init().
     *
     * After calling av_hwdevice_ctx_init() this struct should not be modified
     * by the caller.
     */
    void *hwctx;

    /**
     * This field may be set by the caller before calling av_hwdevice_ctx_init().
     *
     * If non-NULL, this callback will be called when the last reference to
     * this context is unreferenced, immediately before it is freed.
     *
     * @note when other objects (e.g an AVHWFramesContext) are derived from this
     *       struct, this callback will be invoked after all such child objects
     *       are fully uninitialized and their respective destructors invoked.
     */
    void (*free)(struct AVHWDeviceContext *ctx);

    /**
     * Arbitrary user data, to be used e.g. by the free() callback.
     */
    void *user_opaque;
} AVHWDeviceContext;

typedef struct AVHWFramesInternal AVHWFramesInternal;

/**
 * This struct describes a set or pool of "hardware" frames (i.e. those with
 * data not located in normal system memory). All the frames in the pool are
 * assumed to be allocated in the same way and interchangeable.
 *
 * This struct is reference-counted with the AVBuffer mechanism and tied to a
 * given AVHWDeviceContext instance. The av_hwframe_ctx_alloc() constructor
 * yields a reference, whose data field points to the actual AVHWFramesContext
 * struct.
 */
typedef struct AVHWFramesContext {
    /**
     * A class for logging.
     */
    const AVClass *av_class;

    /**
     * Private data used internally by libavutil. Must not be accessed in any
     * way by the caller.
     */
    AVHWFramesInternal *internal;

    /**
     * A reference to the parent AVHWDeviceContext. This reference is owned and
     * managed by the enclosing AVHWFramesContext, but the caller may derive
     * additional references from it.
     */
    AVBufferRef *device_ref;

    /**
     * The parent AVHWDeviceContext. This is simply a pointer to
     * device_ref->data provided for convenience.
     *
     * Set by libavutil in av_hwframe_ctx_init().
     */
    AVHWDeviceContext *device_ctx;

    /**
     * The format-specific data, allocated and freed automatically along with
     * this context.
     *
     * Should be cast by the user to the format-specific context defined in the
     * corresponding header (hwframe_*.h) and filled as described in the
     * documentation before calling av_hwframe_ctx_init().
     *
     * After any frames using this context are created, the contents of this
     * struct should not be modified by the caller.
     */
    void *hwctx;

    /**
     * This field may be set by the caller before calling av_hwframe_ctx_init().
     *
     * If non-NULL, this callback will be called when the last reference to
     * this context is unreferenced, immediately before it is freed.
     */
    void (*free)(struct AVHWFramesContext *ctx);

    /**
     * Arbitrary user data, to be used e.g. by the free() callback.
     */
    void *user_opaque;

    /**
     * A pool from which the frames are allocated by av_hwframe_get_buffer().
     * This field may be set by the caller before calling av_hwframe_ctx_init().
     * The buffers returned by calling av_buffer_pool_get() on this pool must
     * have the properties described in the documentation in the correponding hw
     * type's header (hwcontext_*.h). The pool will be freed strictly before
     * this struct's free() callback is invoked.
     *
     * This field may be NULL, then libavutil will attempt to allocate a pool
     * internally. Note that certain device types enforce pools allocated at
     * fixed size (frame count), which cannot be extended dynamically. In such a
     * case, initial_pool_size must be set appropriately.
     */
    AVBufferPool *pool;

    /**
     * Initial size of the frame pool. If a device type does not support
     * dynamically resizing the pool, then this is also the maximum pool size.
     *
     * May be set by the caller before calling av_hwframe_ctx_init(). Must be
     * set if pool is NULL and the device type does not support dynamic pools.
     */
    int initial_pool_size;

    /**
     * The pixel format identifying the underlying HW surface type.
     *
     * Must be a hwaccel format, i.e. the corresponding descriptor must have the
     * AV_PIX_FMT_FLAG_HWACCEL flag set.
     *
     * Must be set by the user before calling av_hwframe_ctx_init().
     */
    enum AVPixelFormat format;

    /**
     * The pixel format identifying the actual data layout of the hardware
     * frames.
     *
     * Must be set by the caller before calling av_hwframe_ctx_init().
     *
     * @note when the underlying API does not provide the exact data layout, but
     * only the colorspace/bit depth, this field should be set to the fully
     * planar version of that format (e.g. for 8-bit 420 YUV it should be
     * AV_PIX_FMT_YUV420P, not AV_PIX_FMT_NV12 or anything else).
     */
    enum AVPixelFormat sw_format;

    /**
     * The allocated dimensions of the frames in this pool.
     *
     * Must be set by the user before calling av_hwframe_ctx_init().
     */
    int width, height;
} AVHWFramesContext;

/**
 * Allocate an AVHWDeviceContext for a given pixel format.
 *
 * @param format a hwaccel pixel format (AV_PIX_FMT_FLAG_HWACCEL must be set
 *               on the corresponding format descriptor)
 * @return a reference to the newly created AVHWDeviceContext on success or NULL
 *         on failure.
 */
AVBufferRef *av_hwdevice_ctx_alloc(enum AVHWDeviceType type);

/**
 * Finalize the device context before use. This function must be called after
 * the context is filled with all the required information and before it is
 * used in any way.
 *
 * @param ref a reference to the AVHWDeviceContext
 * @return 0 on success, a negative AVERROR code on failure
 */
int av_hwdevice_ctx_init(AVBufferRef *ref);

/**
 * Allocate an AVHWFramesContext tied to a given device context.
 *
 * @param device_ctx a reference to a AVHWDeviceContext. This function will make
 *                   a new reference for internal use, the one passed to the
 *                   function remains owned by the caller.
 * @return a reference to the newly created AVHWFramesContext on success or NULL
 *         on failure.
 */
AVBufferRef *av_hwframe_ctx_alloc(AVBufferRef *device_ctx);

/**
 * Finalize the context before use. This function must be called after the
 * context is filled with all the required information and before it is attached
 * to any frames.
 *
 * @param ref a reference to the AVHWFramesContext
 * @return 0 on success, a negative AVERROR code on failure
 */
int av_hwframe_ctx_init(AVBufferRef *ref);

/**
 * Allocate a new frame attached to the given AVHWFramesContext.
 *
 * @param hwframe_ctx a reference to an AVHWFramesContext
 * @param frame an empty (freshly allocated or unreffed) frame to be filled with
 *              newly allocated buffers.
 * @param flags currently unused, should be set to zero
 * @return 0 on success, a negative AVERROR code on failure
 */
int av_hwframe_get_buffer(AVBufferRef *hwframe_ctx, AVFrame *frame, int flags);

/**
 * Copy data to or from a hw surface. At least one of dst/src must have an
 * AVHWFramesContext attached.
 *
 * If src has an AVHWFramesContext attached, then the format of dst (if set)
 * must use one of the formats returned by av_hwframe_transfer_get_formats(src,
 * AV_HWFRAME_TRANSFER_DIRECTION_FROM).
 * If dst has an AVHWFramesContext attached, then the format of src must use one
 * of the formats returned by av_hwframe_transfer_get_formats(dst,
 * AV_HWFRAME_TRANSFER_DIRECTION_TO)
 *
 * dst may be "clean" (i.e. with data/buf pointers unset), in which case the
 * data buffers will be allocated by this function using av_frame_get_buffer().
 * If dst->format is set, then this format will be used, otherwise (when
 * dst->format is AV_PIX_FMT_NONE) the first acceptable format will be chosen.
 *
 * @param dst the destination frame. dst is not touched on failure.
 * @param src the source frame.
 * @param flags currently unused, should be set to zero
 * @return 0 on success, a negative AVERROR error code on failure.
 */
int av_hwframe_transfer_data(AVFrame *dst, const AVFrame *src, int flags);

enum AVHWFrameTransferDirection {
    /**
     * Transfer the data from the queried hw frame.
     */
    AV_HWFRAME_TRANSFER_DIRECTION_FROM,

    /**
     * Transfer the data to the queried hw frame.
     */
    AV_HWFRAME_TRANSFER_DIRECTION_TO,
};

/**
 * Get a list of possible source or target formats usable in
 * av_hwframe_transfer_data().
 *
 * @param hwframe_ctx the frame context to obtain the information for
 * @param dir the direction of the transfer
 * @param formats the pointer to the output format list will be written here.
 *                The list is terminated with AV_PIX_FMT_NONE and must be freed
 *                by the caller when no longer needed using av_free().
 *                If this function returns successfully, the format list will
 *                have at least one item (not counting the terminator).
 *                On failure, the contents of this pointer are unspecified.
 * @param flags currently unused, should be set to zero
 * @return 0 on success, a negative AVERROR code on failure.
 */
int av_hwframe_transfer_get_formats(AVBufferRef *hwframe_ctx,
                                    enum AVHWFrameTransferDirection dir,
                                    enum AVPixelFormat **formats, int flags);


#endif /* AVUTIL_HWCONTEXT_H */
