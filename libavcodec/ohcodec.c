/*
 * This file is part of FFmpeg.
 *
 * Copyright (c) 2025 Zhao Zhili <quinkblack@foxmail.com>
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

#include "ohcodec.h"

#include "libavutil/error.h"

int ff_oh_err_to_ff_err(OH_AVErrCode err)
{
    switch (err) {
    case AV_ERR_OK:
        return 0;
    case AV_ERR_NO_MEMORY:
        return AVERROR(ENOMEM);
    case AV_ERR_OPERATE_NOT_PERMIT:
        return AVERROR(EPERM);
    case AV_ERR_INVALID_VAL:
        return AVERROR(EINVAL);
    case AV_ERR_IO:
        return AVERROR(EIO);
    case AV_ERR_TIMEOUT:
        return AVERROR(ETIMEDOUT);
    case AV_ERR_UNKNOWN:
        return AVERROR_UNKNOWN;
    case AV_ERR_SERVICE_DIED:
        return AVERROR_EXTERNAL;
    case AV_ERR_INVALID_STATE:
        return AVERROR(EINVAL);
    case AV_ERR_UNSUPPORT:
        return AVERROR(ENOTSUP);
    default:
        return AVERROR_EXTERNAL;
    }
}

static const struct {
    OH_AVPixelFormat oh_pix;
    enum AVPixelFormat pix;
} oh_pix_map[] = {
        {AV_PIXEL_FORMAT_NV12,           AV_PIX_FMT_NV12},
        {AV_PIXEL_FORMAT_NV21,           AV_PIX_FMT_NV21},
        {AV_PIXEL_FORMAT_YUVI420,        AV_PIX_FMT_YUV420P},
        {AV_PIXEL_FORMAT_SURFACE_FORMAT, AV_PIX_FMT_OHCODEC},
};

enum AVPixelFormat ff_oh_pix_to_ff_pix(OH_AVPixelFormat oh_pix)
{
    for (size_t i = 0; i < FF_ARRAY_ELEMS(oh_pix_map); i++)
        if (oh_pix_map[i].oh_pix == oh_pix)
            return oh_pix_map[i].pix;

    return AV_PIX_FMT_NONE;
}

int ff_oh_pix_from_ff_pix(enum AVPixelFormat pix)
{
    for (size_t i = 0; i < FF_ARRAY_ELEMS(oh_pix_map); i++)
        if (oh_pix_map[i].pix == pix)
            return oh_pix_map[i].oh_pix;

    return 0;
}
