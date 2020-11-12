/**
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

#include "film_grain_params.h"

AVFilmGrainParams *av_film_grain_params_alloc(size_t *size)
{
    AVFilmGrainParams *params = av_mallocz(sizeof(AVFilmGrainParams));

    if (size)
        *size = sizeof(*params);

    return params;
}

AVFilmGrainParams *av_film_grain_params_create_side_data(AVFrame *frame)
{
    AVFrameSideData *side_data = av_frame_new_side_data(frame,
                                                        AV_FRAME_DATA_FILM_GRAIN_PARAMS,
                                                        sizeof(AVFilmGrainParams));
    if (!side_data)
        return NULL;

    memset(side_data->data, 0, sizeof(AVFilmGrainParams));

    return (AVFilmGrainParams *)side_data->data;
}
