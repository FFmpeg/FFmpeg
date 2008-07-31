/*
 * audio conversion
 * Copyright (c) 2006 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2008 Peter Ross
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

#ifndef FFMPEG_AUDIOCONVERT_H
#define FFMPEG_AUDIOCONVERT_H

/**
 * @file audioconvert.h
 * Audio format conversion routines
 */


#include "avcodec.h"


/**
 * Generate string corresponding to the sample format with
 * number sample_fmt, or a header if sample_fmt is negative.
 *
 * @param[in] buf the buffer where to write the string
 * @param[in] buf_size the size of buf
 * @param[in] sample_fmt the number of the sample format to print the corresponding info string, or
 * a negative value to print the corresponding header.
 * Meaningful values for obtaining a sample format info vary from 0 to SAMPLE_FMT_NB -1.
 */
void avcodec_sample_fmt_string(char *buf, int buf_size, int sample_fmt);

/**
 * @return NULL on error
 */
const char *avcodec_get_sample_fmt_name(int sample_fmt);

/**
 * @return SAMPLE_FMT_NONE on error
 */
enum SampleFormat avcodec_get_sample_fmt(const char* name);

#endif /* FFMPEG_AUDIOCONVERT_H */
