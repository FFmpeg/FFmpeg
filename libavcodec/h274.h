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

#include "libavutil/film_grain_params.h"

// Must be initialized to {0} prior to first usage
typedef struct H274FilmGrainDatabase {
    // Database of film grain patterns, lazily computed as-needed
    int8_t db[13 /* h */][13 /* v */][64][64];
    uint16_t residency[13 /* h */]; // bit field of v

    // Temporary buffer for slice generation
    int16_t slice_tmp[64][64];
} H274FilmGrainDatabase;

/**
 * Check whether ff_h274_apply_film_grain() supports the given parameter combination.
 *
 * @param model_id model_id from AVFilmGrainParams to be supplied
 * @param pix_fmt  pixel format of the frames to be supplied
 */
static inline int ff_h274_film_grain_params_supported(int model_id, enum AVPixelFormat pix_fmt)
{
    return model_id == 0 && pix_fmt == AV_PIX_FMT_YUV420P;
}

// Synthesizes film grain on top of `in` and stores the result to `out`. `out`
// must already have been allocated and set to the same size and format as
// `in`.
//
// Returns a negative error code on error, such as invalid params.
// If ff_h274_film_grain_params_supported() indicated that the parameters
// are supported, no error will be returned if the arguments given to
// ff_h274_film_grain_params_supported() coincide with actual values
// from the frames and params.
int ff_h274_apply_film_grain(AVFrame *out, const AVFrame *in,
                             H274FilmGrainDatabase *db,
                             const AVFilmGrainParams *params);

typedef struct H274HashContext H274HashContext;

typedef struct H274SEIPictureHash {
    int present;
    union {
        uint8_t  md5[3][16];
        uint16_t crc[3];
        uint32_t checksum[3];
    };
    uint8_t hash_type;
} H274SEIPictureHash;

int ff_h274_hash_init(H274HashContext **c, int type);
int ff_h274_hash_verify(H274HashContext *c, const H274SEIPictureHash *hash,
    const AVFrame *frame, int coded_width, int coded_height);
void ff_h274_hash_freep(H274HashContext **c);

typedef struct H274SEIFrameFieldInfo {
    int present;
    int picture_struct;
    uint8_t display_elemental_periods;
    uint8_t source_scan_type;
    uint8_t duplicate_flag;
} H274SEIFrameFieldInfo;

#endif /* AVCODEC_H274_H */
