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

#ifndef AVCORE_SAMPLEFMT_H
#define AVCORE_SAMPLEFMT_H

/**
 * all in native-endian format
 */
enum AVSampleFormat {
    AV_SAMPLE_FMT_NONE = -1,
    AV_SAMPLE_FMT_U8,          ///< unsigned 8 bits
    AV_SAMPLE_FMT_S16,         ///< signed 16 bits
    AV_SAMPLE_FMT_S32,         ///< signed 32 bits
    AV_SAMPLE_FMT_FLT,         ///< float
    AV_SAMPLE_FMT_DBL,         ///< double
    AV_SAMPLE_FMT_NB           ///< Number of sample formats. DO NOT USE if dynamically linking to libavcore
};

/**
 * Return the name of sample_fmt, or NULL if sample_fmt is not
 * recognized.
 */
const char *av_get_sample_fmt_name(enum AVSampleFormat sample_fmt);

/**
 * Return a sample format corresponding to name, or AV_SAMPLE_FMT_NONE
 * on error.
 */
enum AVSampleFormat av_get_sample_fmt(const char *name);

/**
 * Generate a string corresponding to the sample format with
 * sample_fmt, or a header if sample_fmt is negative.
 *
 * @param buf the buffer where to write the string
 * @param buf_size the size of buf
 * @param sample_fmt the number of the sample format to print the
 * corresponding info string, or a negative value to print the
 * corresponding header.
 * @return the pointer to the filled buffer or NULL if sample_fmt is
 * unknown or in case of other errors
 */
char *av_get_sample_fmt_string(char *buf, int buf_size, enum AVSampleFormat sample_fmt);

/**
 * Return sample format bits per sample.
 *
 * @param sample_fmt the sample format
 * @return number of bits per sample or zero if unknown for the given
 * sample format
 */
int av_get_bits_per_sample_fmt(enum AVSampleFormat sample_fmt);

#endif /* AVCORE_SAMPLEFMT_H */
