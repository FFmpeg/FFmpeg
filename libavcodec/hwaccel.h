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

#ifndef AVCODEC_HWACCEL_H
#define AVCODEC_HWACCEL_H

#include "avcodec.h"
#include "hwaccels.h"


#define HWACCEL_CAP_ASYNC_SAFE      (1 << 0)


typedef struct AVCodecHWConfigInternal {
    /**
     * This is the structure which will be returned to the user by
     * avcodec_get_hw_config().
     */
    AVCodecHWConfig public;
    /**
     * If this configuration uses a hwaccel, a pointer to it.
     * If not, NULL.
     */
    const AVHWAccel *hwaccel;
} AVCodecHWConfigInternal;


// These macros are used to simplify AVCodecHWConfigInternal definitions.

#define HW_CONFIG_HWACCEL(device, frames, ad_hoc, format, device_type_, name) \
    &(const AVCodecHWConfigInternal) { \
        .public          = { \
            .pix_fmt     = AV_PIX_FMT_ ## format, \
            .methods     = (device ? AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX : 0) | \
                           (frames ? AV_CODEC_HW_CONFIG_METHOD_HW_FRAMES_CTX : 0) | \
                           (ad_hoc ? AV_CODEC_HW_CONFIG_METHOD_AD_HOC        : 0),  \
            .device_type = AV_HWDEVICE_TYPE_ ## device_type_, \
        }, \
        .hwaccel         = &name, \
    }

#define HW_CONFIG_INTERNAL(format) \
    &(const AVCodecHWConfigInternal) { \
        .public          = { \
            .pix_fmt     = AV_PIX_FMT_ ## format, \
            .methods     = AV_CODEC_HW_CONFIG_METHOD_INTERNAL, \
            .device_type = AV_HWDEVICE_TYPE_NONE, \
        }, \
        .hwaccel         = NULL, \
    }

#define HWACCEL_DXVA2(codec) \
    HW_CONFIG_HWACCEL(1, 1, 1, DXVA2_VLD,    DXVA2,        ff_ ## codec ## _dxva2_hwaccel)
#define HWACCEL_D3D11VA2(codec) \
    HW_CONFIG_HWACCEL(1, 1, 0, D3D11,        D3D11VA,      ff_ ## codec ## _d3d11va2_hwaccel)
#define HWACCEL_NVDEC(codec) \
    HW_CONFIG_HWACCEL(1, 1, 0, CUDA,         CUDA,         ff_ ## codec ## _nvdec_hwaccel)
#define HWACCEL_VAAPI(codec) \
    HW_CONFIG_HWACCEL(1, 1, 1, VAAPI,        VAAPI,        ff_ ## codec ## _vaapi_hwaccel)
#define HWACCEL_VDPAU(codec) \
    HW_CONFIG_HWACCEL(1, 1, 1, VDPAU,        VDPAU,        ff_ ## codec ## _vdpau_hwaccel)
#define HWACCEL_VIDEOTOOLBOX(codec) \
    HW_CONFIG_HWACCEL(1, 1, 1, VIDEOTOOLBOX, VIDEOTOOLBOX, ff_ ## codec ## _videotoolbox_hwaccel)
#define HWACCEL_D3D11VA(codec) \
    HW_CONFIG_HWACCEL(0, 0, 1, D3D11VA_VLD,  NONE,         ff_ ## codec ## _d3d11va_hwaccel)
#define HWACCEL_XVMC(codec) \
    HW_CONFIG_HWACCEL(0, 0, 1, XVMC,         NONE,         ff_ ## codec ## _xvmc_hwaccel)

#endif /* AVCODEC_HWACCEL_H */
