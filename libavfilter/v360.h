/*
 * Copyright (c) 2019 Eugene Lyapustin
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

#ifndef AVFILTER_V360_H
#define AVFILTER_V360_H
#include "avfilter.h"

enum Projections {
    EQUIRECTANGULAR,
    CUBEMAP_3_2,
    CUBEMAP_6_1,
    EQUIANGULAR,
    FLAT,
    DUAL_FISHEYE,
    BARREL,
    CUBEMAP_1_6,
    NB_PROJECTIONS,
};

enum InterpMethod {
    NEAREST,
    BILINEAR,
    BICUBIC,
    LANCZOS,
    NB_INTERP_METHODS,
};

enum Faces {
    TOP_LEFT,
    TOP_MIDDLE,
    TOP_RIGHT,
    BOTTOM_LEFT,
    BOTTOM_MIDDLE,
    BOTTOM_RIGHT,
    NB_FACES,
};

enum Direction {
    RIGHT,  ///< Axis +X
    LEFT,   ///< Axis -X
    UP,     ///< Axis +Y
    DOWN,   ///< Axis -Y
    FRONT,  ///< Axis -Z
    BACK,   ///< Axis +Z
    NB_DIRECTIONS,
};

enum Rotation {
    ROT_0,
    ROT_90,
    ROT_180,
    ROT_270,
    NB_ROTATIONS,
};

enum RotationOrder {
    YAW,
    PITCH,
    ROLL,
    NB_RORDERS,
};

typedef struct V360Context {
    const AVClass *class;
    int in, out;
    int interp;
    int width, height;
    char *in_forder;
    char *out_forder;
    char *in_frot;
    char *out_frot;
    char *rorder;

    int in_cubemap_face_order[6];
    int out_cubemap_direction_order[6];
    int in_cubemap_face_rotation[6];
    int out_cubemap_face_rotation[6];
    int rotation_order[3];

    float in_pad, out_pad;

    float yaw, pitch, roll;

    int h_flip, v_flip, d_flip;

    float h_fov, v_fov;
    float flat_range[3];

    int planewidth[4], planeheight[4];
    int inplanewidth[4], inplaneheight[4];
    int nb_planes;
    int nb_allocated;

    uint16_t *u[4], *v[4];
    int16_t *ker[4];
    unsigned map[4];

    int (*remap_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

    void (*remap_line)(uint8_t *dst, int width, const uint8_t *src, ptrdiff_t in_linesize,
                       const uint16_t *u, const uint16_t *v, const int16_t *ker);
} V360Context;

void ff_v360_init(V360Context *s, int depth);
void ff_v360_init_x86(V360Context *s, int depth);

#endif /* AVFILTER_V360_H */
