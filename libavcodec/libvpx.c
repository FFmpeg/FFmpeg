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
#include "config_components.h"

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
    AV_PIX_FMT_GBRP,
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
    AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12,
    AV_PIX_FMT_NONE
};
#endif

av_cold void ff_vp9_init_static(FFCodec *codec)
{
    codec->p.pix_fmts = vp9_pix_fmts_def;
#if CONFIG_LIBVPX_VP9_ENCODER
    {
        vpx_codec_caps_t codec_caps = vpx_codec_get_caps(vpx_codec_vp9_cx());
        if (codec_caps & VPX_CODEC_CAP_HIGHBITDEPTH)
            codec->p.pix_fmts = vp9_pix_fmts_highbd;
        else
            codec->p.pix_fmts = vp9_pix_fmts_highcol;
    }
#endif
}
