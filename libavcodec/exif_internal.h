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
#include "libavutil/frame.h"

#include "exif.h"
#include "version_major.h"

#if FF_API_OLD_EXIF
/* Used by the AVI demuxer */
int avpriv_exif_decode_ifd(void *logctx, const uint8_t *buf, int size,
                           int le, int depth, AVDictionary **metadata);
#endif /* FF_API_OLD_EXIF */

#define MAKERNOTE_TAG          0x927c
#define ORIENTATION_TAG        0x112
#define EXIFIFD_TAG            0x8769
#define IMAGE_WIDTH_TAG        0x100
#define IMAGE_LENGTH_TAG       0x101
#define PIXEL_X_TAG            0xa002
#define PIXEL_Y_TAG            0xa003

/**
 * Compares values in the IFD with data in the provided AVFrame and sets the values
 * in that IFD to match the ones in that AVFrame. This is mostly useful for an
 * encoder that wishes to use ff_exif_get_buffer, but would prefer to modify the
 * IFD after it is sanitized and call av_exif_write afterward.
 */
int ff_exif_sanitize_ifd(void *logctx, const AVFrame *frame, AVExifMetadata *ifd);

/**
 * Gets all relevant side data, collects it into an IFD, and writes it into the
 * corresponding buffer pointer. This includes both AV_FRAME_DATA_EXIF and other
 * side data types that are included in the frame data, such as possibly an
 * instance of AV_FRAME_DATA_DISPLAYMATRIX. It also sets width and height tags
 * to match those of the AVFrame if they are different.
 *
 * Upon error, *buffer will be NULL. The buffer becomes owned by the caller upon
 * success. The *buffer argument must be NULL before calling. If *buffer is NULL
 * upon return then a negative return value indicates an error, and a zero return
 * value indicates that there was no EXIF data to write.
 */
int ff_exif_get_buffer(void *logctx, const AVFrame *frame, AVBufferRef **buffer, enum AVExifHeaderMode header_mode);

#endif /* AVCODEC_EXIF_INTERNAL_H */
