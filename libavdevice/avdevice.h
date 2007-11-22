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

#ifndef FFMPEG_AVDEVICE_H
#define FFMPEG_AVDEVICE_H

#define LIBAVDEVICE_VERSION_INT ((52<<16)+(0<<8)+0)
#define LIBAVDEVICE_VERSION     52.0.0
#define LIBAVDEVICE_BUILD       LIBAVDEVICE_VERSION_INT

/**
 * Initialize libavdevice and register all the input and output devices.
 * @warning This function is not thread safe.
 */
void avdevice_register_all(void);

#endif /* FFMPEG_AVDEVICE_H */

