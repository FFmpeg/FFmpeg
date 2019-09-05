/*
 * Copyright (c) 2013 Vittorio Giovara <vittorio.giovara@gmail.com>
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

#include <stdint.h>
#include <string.h>

#include "common.h"
#include "mem.h"
#include "stereo3d.h"

AVStereo3D *av_stereo3d_alloc(void)
{
    return av_mallocz(sizeof(AVStereo3D));
}

AVStereo3D *av_stereo3d_create_side_data(AVFrame *frame)
{
    AVFrameSideData *side_data = av_frame_new_side_data(frame,
                                                        AV_FRAME_DATA_STEREO3D,
                                                        sizeof(AVStereo3D));
    if (!side_data)
        return NULL;

    memset(side_data->data, 0, sizeof(AVStereo3D));

    return (AVStereo3D *)side_data->data;
}

static const char * const stereo3d_type_names[] = {
    [AV_STEREO3D_2D]                  = "2D",
    [AV_STEREO3D_SIDEBYSIDE]          = "side by side",
    [AV_STEREO3D_TOPBOTTOM]           = "top and bottom",
    [AV_STEREO3D_FRAMESEQUENCE]       = "frame alternate",
    [AV_STEREO3D_CHECKERBOARD]        = "checkerboard",
    [AV_STEREO3D_SIDEBYSIDE_QUINCUNX] = "side by side (quincunx subsampling)",
    [AV_STEREO3D_LINES]               = "interleaved lines",
    [AV_STEREO3D_COLUMNS]             = "interleaved columns",
};

const char *av_stereo3d_type_name(unsigned int type)
{
    if (type >= FF_ARRAY_ELEMS(stereo3d_type_names))
        return "unknown";

    return stereo3d_type_names[type];
}

int av_stereo3d_from_name(const char *name)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(stereo3d_type_names); i++) {
        size_t len = strlen(stereo3d_type_names[i]);
        if (!strncmp(stereo3d_type_names[i], name, len))
            return i;
    }

    return -1;
}
