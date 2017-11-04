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

#ifndef AVCODEC_HWACCELS_H
#define AVCODEC_HWACCELS_H

#include "avcodec.h"

extern const AVHWAccel ff_h263_vaapi_hwaccel;
extern const AVHWAccel ff_h264_cuvid_hwaccel;
extern const AVHWAccel ff_h264_d3d11va_hwaccel;
extern const AVHWAccel ff_h264_d3d11va2_hwaccel;
extern const AVHWAccel ff_h264_dxva2_hwaccel;
extern const AVHWAccel ff_h264_vaapi_hwaccel;
extern const AVHWAccel ff_h264_vda_hwaccel;
extern const AVHWAccel ff_h264_vda_old_hwaccel;
extern const AVHWAccel ff_h264_vdpau_hwaccel;
extern const AVHWAccel ff_hevc_cuvid_hwaccel;
extern const AVHWAccel ff_hevc_d3d11va_hwaccel;
extern const AVHWAccel ff_hevc_d3d11va2_hwaccel;
extern const AVHWAccel ff_hevc_dxva2_hwaccel;
extern const AVHWAccel ff_hevc_vaapi_hwaccel;
extern const AVHWAccel ff_hevc_vdpau_hwaccel;
extern const AVHWAccel ff_mpeg1_vdpau_hwaccel;
extern const AVHWAccel ff_mpeg2_d3d11va_hwaccel;
extern const AVHWAccel ff_mpeg2_d3d11va2_hwaccel;
extern const AVHWAccel ff_mpeg2_dxva2_hwaccel;
extern const AVHWAccel ff_mpeg2_vaapi_hwaccel;
extern const AVHWAccel ff_mpeg2_vdpau_hwaccel;
extern const AVHWAccel ff_mpeg4_vaapi_hwaccel;
extern const AVHWAccel ff_mpeg4_vdpau_hwaccel;
extern const AVHWAccel ff_vc1_d3d11va_hwaccel;
extern const AVHWAccel ff_vc1_d3d11va2_hwaccel;
extern const AVHWAccel ff_vc1_dxva2_hwaccel;
extern const AVHWAccel ff_vc1_vaapi_hwaccel;
extern const AVHWAccel ff_vc1_vdpau_hwaccel;
extern const AVHWAccel ff_vp8_vaapi_hwaccel;
extern const AVHWAccel ff_wmv3_d3d11va_hwaccel;
extern const AVHWAccel ff_wmv3_d3d11va2_hwaccel;
extern const AVHWAccel ff_wmv3_dxva2_hwaccel;
extern const AVHWAccel ff_wmv3_vaapi_hwaccel;
extern const AVHWAccel ff_wmv3_vdpau_hwaccel;

#endif /* AVCODEC_HWACCELS_H */
