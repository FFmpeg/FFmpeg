/*
 * VorbisComment writer
 * Copyright (c) 2009 James Darnley
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

#ifndef AVFORMAT_VORBISCOMMENT_H
#define AVFORMAT_VORBISCOMMENT_H

#include "avformat.h"
#include "metadata.h"

/**
 * Calculate the length in bytes of a VorbisComment. This is the minimum
 * size required by ff_vorbiscomment_write().
 *
 * @param m The metadata structure to be parsed. For no metadata, set to NULL.
 * @param vendor_string The vendor string to be added into the VorbisComment.
 * For no string, set to an empty string.
 * @param count Pointer to store the number of tags in m because m->count is "not allowed"
 * @return The length in bytes.
 */
int ff_vorbiscomment_length(AVMetadata *m, const char *vendor_string,
                            unsigned *count);

/**
 * Writes a VorbisComment into a buffer. The buffer, p, must have enough
 * data to hold the whole VorbisComment. The minimum size required can be
 * obtained by passing the same AVMetadata and vendor_string to
 * ff_vorbiscomment_length()
 *
 * @param p The buffer in which to write.
 * @param m The metadata struct to write.
 * @param vendor_string The vendor string to write.
 * @param count The number of tags in m because m->count is "not allowed"
 */
int ff_vorbiscomment_write(uint8_t **p, AVMetadata **m,
                           const char *vendor_string, const unsigned count);

extern const AVMetadataConv ff_vorbiscomment_metadata_conv[];

#endif /* AVFORMAT_VORBISCOMMENT_H */
