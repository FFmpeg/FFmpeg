/*
 * EXIF metadata parser - internal functions
 * Copyright (c) 2013 Thilo Borgmann <thilo.borgmann _at_ mail.de>
 * Copyright (c) 2024-2025 Leo Izen <leo.izen@gmail.com>
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

/**
 * @file
 * EXIF metadata parser - internal functions
 * @author Thilo Borgmann <thilo.borgmann _at_ mail.de>
 * @author Leo Izen <leo.izen@gmail.com>
 */

#ifndef AVCODEC_EXIF_INTERNAL_H
#define AVCODEC_EXIF_INTERNAL_H

#include "libavutil/buffer.h"
#include "libavutil/dict.h"
#include "libavutil/frame.h"

#include "bytestream.h"
#include "exif.h"

/**
 * Attach the data buffer to the frame. This is mostly a wrapper for
 * av_side_data_new_from_buffer, but it checks if the orientation tag is
 * present in the provided EXIF buffer. If it is, it zeroes it out and
 * attaches that information as an AV_FRAME_DATA_DISPLAYMATRIX instead
 * of including it in the AV_FRAME_DATA_EXIF side data buffer.
 *
 * On a success, the caller loses ownership of the data buffer. Either it is
 * unrefed, or its ownership is transfered to the frame directly. On failure,
 * the data buffer is left owned by the caller.
 */
int ff_exif_attach_buffer(void *logctx, AVFrame *frame, AVBufferRef *data, enum AVExifHeaderMode header_mode);

/**
 * Attach an already-parsed EXIF metadata struct to the frame as a side data
 * buffer. It writes the EXIF IFD into the buffer and attaches the buffer to
 * the frame.
 *
 * If the metadata struct contains an orientation tag, it will be zeroed before
 * writing, and instead, an AV_FRAME_DATA_DISPLAYMATRIX will be attached in
 * addition to the AV_FRAME_DATA_EXIF side data.
 */
int ff_exif_attach_ifd(void *logctx, AVFrame *frame, const AVExifMetadata *ifd);

/* Used by mjpeg decoder */
int ff_exif_decode_ifd(void *logctx, GetByteContext *gb,
                       int le, int depth, AVDictionary **metadata);

#endif /* AVCODEC_EXIF_INTERNAL_H */
