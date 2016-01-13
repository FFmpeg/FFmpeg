/*
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

#ifndef AVUTIL_HWCONTEXT_INTERNAL_H
#define AVUTIL_HWCONTEXT_INTERNAL_H

#include <stddef.h>

#include "buffer.h"
#include "hwcontext.h"
#include "frame.h"
#include "pixfmt.h"

typedef struct HWContextType {
    enum AVHWDeviceType type;
    const char         *name;

    /**
     * An array of pixel formats supported by the AVHWFramesContext instances
     * Terminated by AV_PIX_FMT_NONE.
     */
    const enum AVPixelFormat *pix_fmts;

    /**
     * size of the public hardware-specific context,
     * i.e. AVHWDeviceContext.hwctx
     */
    size_t             device_hwctx_size;
    /**
     * size of the private data, i.e.
     * AVHWDeviceInternal.priv
     */
    size_t             device_priv_size;

    /**
     * Size of the hardware-specific device configuration.
     * (Used to query hwframe constraints.)
     */
    size_t             device_hwconfig_size;

    /**
     * size of the public frame pool hardware-specific context,
     * i.e. AVHWFramesContext.hwctx
     */
    size_t             frames_hwctx_size;
    /**
     * size of the private data, i.e.
     * AVHWFramesInternal.priv
     */
    size_t             frames_priv_size;

    int              (*device_create)(AVHWDeviceContext *ctx, const char *device,
                                      AVDictionary *opts, int flags);

    int              (*device_init)(AVHWDeviceContext *ctx);
    void             (*device_uninit)(AVHWDeviceContext *ctx);

    int              (*frames_get_constraints)(AVHWDeviceContext *ctx,
                                               const void *hwconfig,
                                               AVHWFramesConstraints *constraints);

    int              (*frames_init)(AVHWFramesContext *ctx);
    void             (*frames_uninit)(AVHWFramesContext *ctx);

    int              (*frames_get_buffer)(AVHWFramesContext *ctx, AVFrame *frame);
    int              (*transfer_get_formats)(AVHWFramesContext *ctx,
                                             enum AVHWFrameTransferDirection dir,
                                             enum AVPixelFormat **formats);
    int              (*transfer_data_to)(AVHWFramesContext *ctx, AVFrame *dst,
                                         const AVFrame *src);
    int              (*transfer_data_from)(AVHWFramesContext *ctx, AVFrame *dst,
                                           const AVFrame *src);
} HWContextType;

struct AVHWDeviceInternal {
    const HWContextType *hw_type;
    void                *priv;
};

struct AVHWFramesInternal {
    const HWContextType *hw_type;
    void                *priv;

    AVBufferPool *pool_internal;
};

extern const HWContextType ff_hwcontext_type_cuda;
extern const HWContextType ff_hwcontext_type_dxva2;
extern const HWContextType ff_hwcontext_type_qsv;
extern const HWContextType ff_hwcontext_type_vaapi;
extern const HWContextType ff_hwcontext_type_vdpau;

#endif /* AVUTIL_HWCONTEXT_INTERNAL_H */
