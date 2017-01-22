/*
 * Copyright (c) 2016 Vittorio Giovara <vittorio.giovara@gmail.com>
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
 * Spherical video
 */

#ifndef AVUTIL_SPHERICAL_H
#define AVUTIL_SPHERICAL_H

#include <stddef.h>
#include <stdint.h>

/**
 * @addtogroup lavu_video
 * @{
 *
 * @defgroup lavu_video_spherical Spherical video mapping
 * @{
 */

/**
 * @addtogroup lavu_video_spherical
 * A spherical video file contains surfaces that need to be mapped onto a
 * sphere. Depending on how the frame was converted, a different distortion
 * transformation or surface recomposition function needs to be applied before
 * the video should be mapped and displayed.
 */

/**
 * Projection of the video surface(s) on a sphere.
 */
enum AVSphericalProjection {
    /**
     * Video represents a sphere mapped on a flat surface using
     * equirectangular projection.
     */
    AV_SPHERICAL_EQUIRECTANGULAR,

    /**
     * Video frame is split into 6 faces of a cube, and arranged on a
     * 3x2 layout. Faces are oriented upwards for the front, left, right,
     * and back faces. The up face is oriented so the top of the face is
     * forwards and the down face is oriented so the top of the face is
     * to the back.
     */
    AV_SPHERICAL_CUBEMAP,
};

/**
 * This structure describes how to handle spherical videos, outlining
 * information about projection, initial layout, and any other view modifier.
 *
 * @note The struct must be allocated with av_spherical_alloc() and
 *       its size is not a part of the public ABI.
 */
typedef struct AVSphericalMapping {
    /**
     * Projection type.
     */
    enum AVSphericalProjection projection;

    /**
     * @name Initial orientation
     * @{
     * There fields describe additional rotations applied to the sphere after
     * the video frame is mapped onto it. The sphere is rotated around the
     * viewer, who remains stationary. The order of transformation is always
     * yaw, followed by pitch, and finally by roll.
     *
     * The coordinate system matches the one defined in OpenGL, where the
     * forward vector (z) is coming out of screen, and it is equivalent to
     * a rotation matrix of R = r_y(yaw) * r_x(pitch) * r_z(roll).
     *
     * A positive yaw rotates the portion of the sphere in front of the viewer
     * toward their right. A positive pitch rotates the portion of the sphere
     * in front of the viewer upwards. A positive roll tilts the portion of
     * the sphere in front of the viewer to the viewer's right.
     *
     * These values are exported as 16.16 fixed point.
     *
     * See this equirectangular projection as example:
     *
     * @code{.unparsed}
     *                   Yaw
     *     -180           0           180
     *   90 +-------------+-------------+  180
     *      |             |             |                  up
     * P    |             |             |                 y|    forward
     * i    |             ^             |                  |   /z
     * t  0 +-------------X-------------+    0 Roll        |  /
     * c    |             |             |                  | /
     * h    |             |             |                 0|/_____right
     *      |             |             |                        x
     *  -90 +-------------+-------------+ -180
     *
     * X - the default camera center
     * ^ - the default up vector
     * @endcode
     */
    int32_t yaw;   ///< Rotation around the up vector [-180, 180].
    int32_t pitch; ///< Rotation around the right vector [-90, 90].
    int32_t roll;  ///< Rotation around the forward vector [-180, 180].
    /**
     * @}
     */
} AVSphericalMapping;

/**
 * Allocate a AVSphericalVideo structure and initialize its fields to default
 * values.
 *
 * @return the newly allocated struct or NULL on failure
 */
AVSphericalMapping *av_spherical_alloc(size_t *size);

/**
 * @}
 * @}
 */

#endif /* AVUTIL_SPHERICAL_H */
