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

#ifndef AVUTIL_HWCONTEXT_VAAPI_H
#define AVUTIL_HWCONTEXT_VAAPI_H

#include <va/va.h>

/**
 * @file
 * API-specific header for AV_HWDEVICE_TYPE_VAAPI.
 *
 * Dynamic frame pools are supported, but note that any pool used as a render
 * target is required to be of fixed size in order to be be usable as an
 * argument to vaCreateContext().
 *
 * For user-allocated pools, AVHWFramesContext.pool must return AVBufferRefs
 * with the data pointer set to a VASurfaceID.
 */

/**
 * VAAPI connection details.
 *
 * Allocated as AVHWDeviceContext.hwctx
 */
typedef struct AVVAAPIDeviceContext {
    /**
     * The VADisplay handle, to be filled by the user.
     */
    VADisplay display;
} AVVAAPIDeviceContext;

/**
 * VAAPI-specific data associated with a frame pool.
 *
 * Allocated as AVHWFramesContext.hwctx.
 */
typedef struct AVVAAPIFramesContext {
    /**
     * Set by the user to apply surface attributes to all surfaces in
     * the frame pool.  If null, default settings are used.
     */
    VASurfaceAttrib *attributes;
    int           nb_attributes;
    /**
     * The surfaces IDs of all surfaces in the pool after creation.
     * Only valid if AVHWFramesContext.initial_pool_size was positive.
     * These are intended to be used as the render_targets arguments to
     * vaCreateContext().
     */
    VASurfaceID     *surface_ids;
    int           nb_surfaces;
} AVVAAPIFramesContext;

/**
 * VAAPI hardware pipeline configuration details.
 *
 * Allocated with av_hwdevice_hwconfig_alloc().
 */
typedef struct AVVAAPIHWConfig {
    /**
     * ID of a VAAPI pipeline configuration.
     */
    VAConfigID config_id;
} AVVAAPIHWConfig;

#endif /* AVUTIL_HWCONTEXT_VAAPI_H */
