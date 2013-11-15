/*
 * Copyright (c) 2013 Guillaume Martres <smarter@ubuntu.com>
 *
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

#include <vpx/vpx_codec.h>

#include "libvpx.h"

int ff_vp9_check_experimental(AVCodecContext *avctx)
{
    if (avctx->strict_std_compliance > FF_COMPLIANCE_EXPERIMENTAL &&
        (vpx_codec_version_major() < 1 ||
         (vpx_codec_version_major() == 1 && vpx_codec_version_minor() < 3))) {
        av_log(avctx, AV_LOG_ERROR,
               "Non-experimental support of VP9 requires libvpx >= 1.3.0\n");
        return AVERROR_EXPERIMENTAL;
    }
    return 0;
}
