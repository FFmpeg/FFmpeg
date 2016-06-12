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

enum {
    /**
     * The quirks field has been set by the user and should not be detected
     * automatically by av_hwdevice_ctx_init().
     */
    AV_VAAPI_DRIVER_QUIRK_USER_SET = (1 << 0),
    /**
     * The driver does not destroy parameter buffers when they are used by
     * vaRenderPicture().  Additional code will be required to destroy them
     * separately afterwards.
     */
    AV_VAAPI_DRIVER_QUIRK_RENDER_PARAM_BUFFERS = (1 << 1),
};

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
    /**
     * Driver quirks to apply - this is filled by av_hwdevice_ctx_init(),
     * with reference to a table of known drivers, unless the
     * AV_VAAPI_DRIVER_QUIRK_USER_SET bit is already present.  The user
     * may need to refer to this field when performing any later
     * operations using VAAPI with the same VADisplay.
     */
    unsigned int driver_quirks;
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
