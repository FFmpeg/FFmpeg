/*
 * H.274 film grain synthesis
 * Copyright (c) 2021 Niklas Haas <ffmpeg@haasn.xyz>
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
 * H.274 film grain synthesis.
 * @author Niklas Haas <ffmpeg@haasn.xyz>
 */

#ifndef AVCODEC_H274_H
#define AVCODEC_H274_H

#include <libavutil/film_grain_params.h>

// Must be initialized to {0} prior to first usage
typedef struct H274FilmGrainDatabase {
    // Database of film grain patterns, lazily computed as-needed
    int8_t db[13 /* h */][13 /* v */][64][64];
    uint16_t residency[13 /* h */]; // bit field of v

    // Temporary buffer for slice generation
    int16_t slice_tmp[64][64];
} H274FilmGrainDatabase;

// Synthesizes film grain on top of `in` and stores the result to `out`. `out`
// must already have been allocated and set to the same size and format as
// `in`.
//
// Returns a negative error code on error, such as invalid params.
int ff_h274_apply_film_grain(AVFrame *out, const AVFrame *in,
                             H274FilmGrainDatabase *db,
                             const AVFilmGrainParams *params);

#endif /* AVCODEC_H274_H */
