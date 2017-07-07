/*
 * Copyright (c) 2013 Guillaume Martres <smarter@ubuntu.com>
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

#include <vpx/vpx_codec.h>
#include "libvpx.h"
#include "config.h"

#if CONFIG_LIBVPX_VP9_ENCODER
#include <vpx/vpx_encoder.h>
#include <vpx/vp8cx.h>
#endif

static const enum AVPixelFormat vp9_pix_fmts_def[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_NONE
};

#if CONFIG_LIBVPX_VP9_ENCODER
static const enum AVPixelFormat vp9_pix_fmts_highcol[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUV444P,
#if VPX_IMAGE_ABI_VERSION >= 3
    AV_PIX_FMT_GBRP,
#endif
    AV_PIX_FMT_NONE
};

static const enum AVPixelFormat vp9_pix_fmts_highbd[] = {
    AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVA420P,
    AV_PIX_FMT_YUV422P,
    AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUV444P,
    AV_PIX_FMT_YUV420P10,
    AV_PIX_FMT_YUV422P10,
    AV_PIX_FMT_YUV440P10,
    AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12,
    AV_PIX_FMT_YUV422P12,
    AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV444P12,
#if VPX_IMAGE_ABI_VERSION >= 3
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12,
#endif
    AV_PIX_FMT_NONE
};
#endif

av_cold void ff_vp9_init_static(AVCodec *codec)
{
    if (    vpx_codec_version_major() < 1
        || (vpx_codec_version_major() == 1 && vpx_codec_version_minor() < 3))
        codec->capabilities |= AV_CODEC_CAP_EXPERIMENTAL;
    codec->pix_fmts = vp9_pix_fmts_def;
#if CONFIG_LIBVPX_VP9_ENCODER
    if (    vpx_codec_version_major() > 1
        || (vpx_codec_version_major() == 1 && vpx_codec_version_minor() >= 4)) {
#ifdef VPX_CODEC_CAP_HIGHBITDEPTH
        vpx_codec_caps_t codec_caps = vpx_codec_get_caps(vpx_codec_vp9_cx());
        if (codec_caps & VPX_CODEC_CAP_HIGHBITDEPTH)
            codec->pix_fmts = vp9_pix_fmts_highbd;
        else
#endif
            codec->pix_fmts = vp9_pix_fmts_highcol;
    }
#endif
}
#if 0
enum AVPixelFormat ff_vpx_imgfmt_to_pixfmt(vpx_img_fmt_t img)
{
    switch (img) {
    case VPX_IMG_FMT_RGB24:     return AV_PIX_FMT_RGB24;
    case VPX_IMG_FMT_RGB565:    return AV_PIX_FMT_RGB565BE;
    case VPX_IMG_FMT_RGB555:    return AV_PIX_FMT_RGB555BE;
    case VPX_IMG_FMT_UYVY:      return AV_PIX_FMT_UYVY422;
    case VPX_IMG_FMT_YUY2:      return AV_PIX_FMT_YUYV422;
    case VPX_IMG_FMT_YVYU:      return AV_PIX_FMT_YVYU422;
    case VPX_IMG_FMT_BGR24:     return AV_PIX_FMT_BGR24;
    case VPX_IMG_FMT_ARGB:      return AV_PIX_FMT_ARGB;
    case VPX_IMG_FMT_ARGB_LE:   return AV_PIX_FMT_BGRA;
    case VPX_IMG_FMT_RGB565_LE: return AV_PIX_FMT_RGB565LE;
    case VPX_IMG_FMT_RGB555_LE: return AV_PIX_FMT_RGB555LE;
    case VPX_IMG_FMT_I420:      return AV_PIX_FMT_YUV420P;
    case VPX_IMG_FMT_I422:      return AV_PIX_FMT_YUV422P;
    case VPX_IMG_FMT_I444:      return AV_PIX_FMT_YUV444P;
    case VPX_IMG_FMT_444A:      return AV_PIX_FMT_YUVA444P;
#if VPX_IMAGE_ABI_VERSION >= 3
    case VPX_IMG_FMT_I440:      return AV_PIX_FMT_YUV440P;
    case VPX_IMG_FMT_I42016:    return AV_PIX_FMT_YUV420P16BE;
    case VPX_IMG_FMT_I42216:    return AV_PIX_FMT_YUV422P16BE;
    case VPX_IMG_FMT_I44416:    return AV_PIX_FMT_YUV444P16BE;
#endif
    default:                    return AV_PIX_FMT_NONE;
    }
}

vpx_img_fmt_t ff_vpx_pixfmt_to_imgfmt(enum AVPixelFormat pix)
{
    switch (pix) {
    case AV_PIX_FMT_RGB24:        return VPX_IMG_FMT_RGB24;
    case AV_PIX_FMT_RGB565BE:     return VPX_IMG_FMT_RGB565;
    case AV_PIX_FMT_RGB555BE:     return VPX_IMG_FMT_RGB555;
    case AV_PIX_FMT_UYVY422:      return VPX_IMG_FMT_UYVY;
    case AV_PIX_FMT_YUYV422:      return VPX_IMG_FMT_YUY2;
    case AV_PIX_FMT_YVYU422:      return VPX_IMG_FMT_YVYU;
    case AV_PIX_FMT_BGR24:        return VPX_IMG_FMT_BGR24;
    case AV_PIX_FMT_ARGB:         return VPX_IMG_FMT_ARGB;
    case AV_PIX_FMT_BGRA:         return VPX_IMG_FMT_ARGB_LE;
    case AV_PIX_FMT_RGB565LE:     return VPX_IMG_FMT_RGB565_LE;
    case AV_PIX_FMT_RGB555LE:     return VPX_IMG_FMT_RGB555_LE;
    case AV_PIX_FMT_YUV420P:      return VPX_IMG_FMT_I420;
    case AV_PIX_FMT_YUV422P:      return VPX_IMG_FMT_I422;
    case AV_PIX_FMT_YUV444P:      return VPX_IMG_FMT_I444;
    case AV_PIX_FMT_YUVA444P:     return VPX_IMG_FMT_444A;
#if VPX_IMAGE_ABI_VERSION >= 3
    case AV_PIX_FMT_YUV440P:      return VPX_IMG_FMT_I440;
    case AV_PIX_FMT_YUV420P16BE:  return VPX_IMG_FMT_I42016;
    case AV_PIX_FMT_YUV422P16BE:  return VPX_IMG_FMT_I42216;
    case AV_PIX_FMT_YUV444P16BE:  return VPX_IMG_FMT_I44416;
#endif
    default:                      return VPX_IMG_FMT_NONE;
    }
}
#endif
