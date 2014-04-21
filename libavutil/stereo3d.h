/*
 * Copyright (c) 2013 Vittorio Giovara <vittorio.giovara@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef AVUTIL_STEREO3D_H
#define AVUTIL_STEREO3D_H

#include <stdint.h>

#include "frame.h"

/**
 * List of possible 3D Types
 */
enum AVStereo3DType {
    /**
     * Video is not stereoscopic (and metadata has to be there).
     */
    AV_STEREO3D_2D,

    /**
     * Views are next to each other.
     *
     *    LLLLRRRR
     *    LLLLRRRR
     *    LLLLRRRR
     *    ...
     */
    AV_STEREO3D_SIDEBYSIDE,

    /**
     * Views are on top of each other.
     *
     *    LLLLLLLL
     *    LLLLLLLL
     *    RRRRRRRR
     *    RRRRRRRR
     */
    AV_STEREO3D_TOPBOTTOM,

    /**
     * Views are alternated temporally.
     *
     *     frame0   frame1   frame2   ...
     *    LLLLLLLL RRRRRRRR LLLLLLLL
     *    LLLLLLLL RRRRRRRR LLLLLLLL
     *    LLLLLLLL RRRRRRRR LLLLLLLL
     *    ...      ...      ...
     */
    AV_STEREO3D_FRAMESEQUENCE,

    /**
     * Views are packed in a checkerboard-like structure per pixel.
     *
     *    LRLRLRLR
     *    RLRLRLRL
     *    LRLRLRLR
     *    ...
     */
    AV_STEREO3D_CHECKERBOARD,

    /**
     * Views are next to each other, but when upscaling
     * apply a checkerboard pattern.
     *
     *     LLLLRRRR          L L L L    R R R R
     *     LLLLRRRR    =>     L L L L  R R R R
     *     LLLLRRRR          L L L L    R R R R
     *     LLLLRRRR           L L L L  R R R R
     */
    AV_STEREO3D_SIDEBYSIDE_QUINCUNX,

    /**
     * Views are packed per line, as if interlaced.
     *
     *    LLLLLLLL
     *    RRRRRRRR
     *    LLLLLLLL
     *    ...
     */
    AV_STEREO3D_LINES,

    /**
     * Views are packed per column.
     *
     *    LRLRLRLR
     *    LRLRLRLR
     *    LRLRLRLR
     *    ...
     */
    AV_STEREO3D_COLUMNS,
};


/**
 * Inverted views, Right/Bottom represents the left view.
 */
#define AV_STEREO3D_FLAG_INVERT     (1 << 0)

/**
 * Stereo 3D type: this structure describes how two videos are packed
 * within a single video surface, with additional information as needed.
 *
 * @note The struct must be allocated with av_stereo3d_alloc() and
 *       its size is not a part of the public ABI.
 */
typedef struct AVStereo3D {
    /**
     * How views are packed within the video.
     */
    enum AVStereo3DType type;

    /**
     * Additional information about the frame packing.
     */
    int flags;
} AVStereo3D;

/**
 * Allocate an AVStereo3D structure and set its fields to default values.
 * The resulting struct can be freed using av_freep().
 *
 * @return An AVStereo3D filled with default values or NULL on failure.
 */
AVStereo3D *av_stereo3d_alloc(void);

/**
 * Allocate a complete AVFrameSideData and add it to the frame.
 *
 * @param frame The frame which side data is added to.
 *
 * @return The AVStereo3D structure to be filled by caller.
 */
AVStereo3D *av_stereo3d_create_side_data(AVFrame *frame);

#endif /* AVUTIL_STEREO3D_H */
