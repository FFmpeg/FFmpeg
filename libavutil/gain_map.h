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

#ifndef AVUTIL_GAIN_MAP_H
#define AVUTIL_GAIN_MAP_H

#include <stddef.h>

#include "rational.h"
#include "frame.h"

#define AV_ISO21496_VERSION    0
#define AV_ISO21496_IDENTIFIER "urn:iso:std:iso:ts:21496:-1"

/**
 * Parameters describing how to combine a gain map rendition with its base
 * rendition to recover the alternate rendition, as defined by
 * ISO 21496-1:2025 "Digital photography - Gain map metadata for image
 * conversion".
 *
 * Note: sizeof(AVGainMapParams) is not part of the ABI. New fields may be
 * added at the end of the struct.
 */
typedef struct AVGainMapParams {
    /**
     * The version of this gain map struct. Reserved to allow future
     * extensions of the struct. Must be <= AV_ISO21496_VERSION.
     */
    int version;

    /**
     * If non-zero, the gain map is applied in the colour space of the base
     * rendition; otherwise in that of the alternate rendition.
     */
    int use_base_color_space;

    /**
     * log2 of the display headroom at which the base rendition is shown
     * unmodified, and at which the map is applied in full.
     */
    AVRational base_hdr_headroom;
    AVRational alternate_hdr_headroom;

    struct AVGainMapChannel {
        /**
        * log2-domain minimum and maximum gain applied by the map, i.e. the
        * values the map's 0.0 and 1.0 endpoints decode to.
        */
        AVRational gain_map_min;
        AVRational gain_map_max;

        /**
        * Encoding gamma of the stored map values. Must be > 0.
        */
        AVRational gamma;

        /**
        * Small constants added before the log-domain math to avoid a
        * singularity at zero.
        */
        AVRational base_offset;
        AVRational alternate_offset;
    } channels[3]; /* R, G, B */

    /**
     * Must be 1 or 3. If 1, all channels share the same set of parameters.
     */
    int nb_channels;
} AVGainMapParams;

/**
 * Returns >= 0 if the given AVGainMapParams struct contains valid data;
 * or a negative AVERROR otherwise.
 */
int av_gain_map_params_validate(const AVGainMapParams *p);

/**
 * Returns 1 if all channels of the gain map have identical parameters,
 * 0 otherwise.
 */
int av_gain_map_channels_identical(const AVGainMapParams *p);

/**
 * Allocate an AVGainMapParams structure and initialize it to default values.
 * The resulting pointer must be freed using av_free().
 *
 * @param size if non-NULL, set to sizeof(AVGainMapParams)
 * @return the newly allocated struct, or NULL on failure
 */
AVGainMapParams *av_gain_map_params_alloc(size_t *size);

/**
 * Allocate and add an AVGainMapParams structure to an existing AVFrameSideData
 * array as AV_FRAME_DATA_GAIN_MAP_PARAMS side data.
 *
 * @return the newly allocated struct, or NULL on failure
 */
AVGainMapParams *av_gain_map_params_create_side_data(AVFrameSideData ***sd, int *nb_sd);

#endif /* AVUTIL_GAIN_MAP_H */
