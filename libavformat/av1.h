/*
 * AV1 helper functions for muxers
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

#ifndef AVFORMAT_AV1_H
#define AVFORMAT_AV1_H

#include <stdint.h>

#include "avio.h"

typedef struct AV1SequenceParameters {
    uint8_t profile;
    uint8_t level;
    uint8_t tier;
    uint8_t bitdepth;
    uint8_t monochrome;
    uint8_t chroma_subsampling_x;
    uint8_t chroma_subsampling_y;
    uint8_t chroma_sample_position;
    uint8_t color_description_present_flag;
    uint8_t color_primaries;
    uint8_t transfer_characteristics;
    uint8_t matrix_coefficients;
    uint8_t color_range;
} AV1SequenceParameters;

/**
 * Filter out AV1 OBUs not meant to be present in ISOBMFF sample data and write
 * the resulting bitstream to the provided AVIOContext.
 *
 * @param pb pointer to the AVIOContext where the filtered bitstream shall be
 *           written
 * @param buf input data buffer
 * @param size size of the input data buffer
 *
 * @return the amount of bytes written in case of success, a negative AVERROR
 *         code in case of failure
 */
int ff_av1_filter_obus(AVIOContext *pb, const uint8_t *buf, int size);

/**
 * Filter out AV1 OBUs not meant to be present in ISOBMFF sample data and write
 * the resulting bitstream to a newly allocated data buffer.
 *
 * @param pb pointer to the AVIOContext where the filtered bitstream shall be
 *           written
 * @param buf input data buffer
 * @param out pointer to pointer that will hold the allocated data buffer
 * @param size size of the input data buffer. The size of the resulting output
               data buffer will be written here
 *
 * @return the amount of bytes written in case of success, a negative AVERROR
 *         code in case of failure. On failure, out and size are unchanged
 */
int ff_av1_filter_obus_buf(const uint8_t *buf, uint8_t **out, int *size);

/**
 * Parses a Sequence Header from the the provided buffer.
 *
 * @param seq pointer to the AV1SequenceParameters where the parsed values will
 *            be written
 * @param buf input data buffer
 * @param size size in bytes of the input data buffer
 *
 * @return >= 0 in case of success, a negative AVERROR code in case of failure
 */
int ff_av1_parse_seq_header(AV1SequenceParameters *seq, const uint8_t *buf, int size);

/**
 * Writes AV1 extradata (Sequence Header and Metadata OBUs) to the provided
 * AVIOContext.
 *
 * @param pb pointer to the AVIOContext where the av1C box shall be written
 * @param buf input data buffer
 * @param size size in bytes of the input data buffer
 *
 * @return >= 0 in case of success, a negative AVERROR code in case of failure
 */
int ff_isom_write_av1c(AVIOContext *pb, const uint8_t *buf, int size);

#endif /* AVFORMAT_AV1_H */
