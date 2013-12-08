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

av_cold void ff_vp9_init_static(AVCodec *codec)
{
    if (    vpx_codec_version_major() < 1
        || (vpx_codec_version_major() == 1 && vpx_codec_version_minor() < 3))
        codec->capabilities |= CODEC_CAP_EXPERIMENTAL;
}
