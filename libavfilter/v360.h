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

enum StereoFormats {
    STEREO_2D,
    STEREO_SBS,
    STEREO_TB,
    NB_STEREO_FMTS,
};

enum Projections {
    EQUIRECTANGULAR,
    CUBEMAP_3_2,
    CUBEMAP_6_1,
    EQUIANGULAR,
    FLAT,
    DUAL_FISHEYE,
    BARREL,
    CUBEMAP_1_6,
    STEREOGRAPHIC,
    MERCATOR,
    BALL,
    HAMMER,
    SINUSOIDAL,
    FISHEYE,
    PANNINI,
    CYLINDRICAL,
    PERSPECTIVE,
    TETRAHEDRON,
    NB_PROJECTIONS,
};

enum InterpMethod {
    NEAREST,
    BILINEAR,
    BICUBIC,
    LANCZOS,
    SPLINE16,
    GAUSSIAN,
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

typedef struct XYRemap {
    int16_t u[4][4];
    int16_t v[4][4];
    float ker[4][4];
} XYRemap;

typedef struct V360Context {
    const AVClass *class;
    int in, out;
    int interp;
    int alpha;
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

    int in_stereo, out_stereo;

    float in_pad, out_pad;
    int fin_pad, fout_pad;

    float yaw, pitch, roll;

    int ih_flip, iv_flip;
    int h_flip, v_flip, d_flip;
    int in_transpose, out_transpose;

    float h_fov, v_fov, d_fov;
    float ih_fov, iv_fov, id_fov;
    float flat_range[2];
    float iflat_range[2];

    float rot_mat[3][3];

    float input_mirror_modifier[2];
    float output_mirror_modifier[3];

    int in_width, in_height;
    int out_width, out_height;

    int pr_width[4], pr_height[4];

    int in_offset_w[4], in_offset_h[4];
    int out_offset_w[4], out_offset_h[4];

    int planewidth[4], planeheight[4];
    int inplanewidth[4], inplaneheight[4];
    int uv_linesize[4];
    int nb_planes;
    int nb_allocated;
    int elements;
    int mask_size;
    int max_value;

    int16_t *u[2], *v[2];
    int16_t *ker[2];
    uint8_t *mask;
    unsigned map[4];

    int (*in_transform)(const struct V360Context *s,
                        const float *vec, int width, int height,
                        int16_t us[4][4], int16_t vs[4][4], float *du, float *dv);

    int (*out_transform)(const struct V360Context *s,
                         int i, int j, int width, int height,
                         float *vec);

    void (*calculate_kernel)(float du, float dv, const XYRemap *rmap,
                             int16_t *u, int16_t *v, int16_t *ker);

    int (*remap_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);

    void (*remap_line)(uint8_t *dst, int width, const uint8_t *const src, ptrdiff_t in_linesize,
                       const int16_t *const u, const int16_t *const v, const int16_t *const ker);
} V360Context;

void ff_v360_init(V360Context *s, int depth);
void ff_v360_init_x86(V360Context *s, int depth);

#endif /* AVFILTER_V360_H */
