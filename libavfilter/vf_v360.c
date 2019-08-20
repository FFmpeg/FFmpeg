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

/**
 * @file
 * 360 video conversion filter.
 * Principle of operation:
 *
 * (for each pixel in output frame)
 * 1) Calculate OpenGL-like coordinates (x, y, z) for pixel position (i, j)
 * 2) Apply 360 operations (rotation, mirror) to (x, y, z)
 * 3) Calculate pixel position (u, v) in input frame
 * 4) Calculate interpolation window and weight for each pixel
 *
 * (for each frame)
 * 5) Remap input frame to output frame using precalculated data
 */

#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

enum Projections {
    EQUIRECTANGULAR,
    CUBEMAP_3_2,
    CUBEMAP_6_1,
    EQUIANGULAR,
    FLAT,
    DUAL_FISHEYE,
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

typedef struct V360Context {
    const AVClass *class;
    int in, out;
    int interp;
    int width, height;
    char* in_forder;
    char* out_forder;
    char* in_frot;
    char* out_frot;

    int in_cubemap_face_order[6];
    int out_cubemap_direction_order[6];
    int in_cubemap_face_rotation[6];
    int out_cubemap_face_rotation[6];

    float in_pad, out_pad;

    float yaw, pitch, roll;

    int h_flip, v_flip, d_flip;

    float h_fov, v_fov;
    float flat_range[3];

    int planewidth[4], planeheight[4];
    int inplanewidth[4], inplaneheight[4];
    int nb_planes;

    void *remap[4];

    int (*remap_slice)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} V360Context;

typedef struct ThreadData {
    V360Context *s;
    AVFrame *in;
    AVFrame *out;
    int nb_planes;
} ThreadData;

#define OFFSET(x) offsetof(V360Context, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption v360_options[] = {
    {     "input", "set input projection",              OFFSET(in), AV_OPT_TYPE_INT,    {.i64=EQUIRECTANGULAR}, 0,    NB_PROJECTIONS-1, FLAGS, "in" },
    {         "e", "equirectangular",                            0, AV_OPT_TYPE_CONST,  {.i64=EQUIRECTANGULAR}, 0,                   0, FLAGS, "in" },
    {      "c3x2", "cubemap3x2",                                 0, AV_OPT_TYPE_CONST,  {.i64=CUBEMAP_3_2},     0,                   0, FLAGS, "in" },
    {      "c6x1", "cubemap6x1",                                 0, AV_OPT_TYPE_CONST,  {.i64=CUBEMAP_6_1},     0,                   0, FLAGS, "in" },
    {       "eac", "equi-angular",                               0, AV_OPT_TYPE_CONST,  {.i64=EQUIANGULAR},     0,                   0, FLAGS, "in" },
    {  "dfisheye", "dual fisheye",                               0, AV_OPT_TYPE_CONST,  {.i64=DUAL_FISHEYE},    0,                   0, FLAGS, "in" },
    {    "output", "set output projection",            OFFSET(out), AV_OPT_TYPE_INT,    {.i64=CUBEMAP_3_2},     0,    NB_PROJECTIONS-1, FLAGS, "out" },
    {         "e", "equirectangular",                            0, AV_OPT_TYPE_CONST,  {.i64=EQUIRECTANGULAR}, 0,                   0, FLAGS, "out" },
    {      "c3x2", "cubemap3x2",                                 0, AV_OPT_TYPE_CONST,  {.i64=CUBEMAP_3_2},     0,                   0, FLAGS, "out" },
    {      "c6x1", "cubemap6x1",                                 0, AV_OPT_TYPE_CONST,  {.i64=CUBEMAP_6_1},     0,                   0, FLAGS, "out" },
    {       "eac", "equi-angular",                               0, AV_OPT_TYPE_CONST,  {.i64=EQUIANGULAR},     0,                   0, FLAGS, "out" },
    {      "flat", "regular video",                              0, AV_OPT_TYPE_CONST,  {.i64=FLAT},            0,                   0, FLAGS, "out" },
    {    "interp", "set interpolation method",      OFFSET(interp), AV_OPT_TYPE_INT,    {.i64=BILINEAR},        0, NB_INTERP_METHODS-1, FLAGS, "interp" },
    {      "near", "nearest neighbour",                          0, AV_OPT_TYPE_CONST,  {.i64=NEAREST},         0,                   0, FLAGS, "interp" },
    {   "nearest", "nearest neighbour",                          0, AV_OPT_TYPE_CONST,  {.i64=NEAREST},         0,                   0, FLAGS, "interp" },
    {      "line", "bilinear interpolation",                     0, AV_OPT_TYPE_CONST,  {.i64=BILINEAR},        0,                   0, FLAGS, "interp" },
    {    "linear", "bilinear interpolation",                     0, AV_OPT_TYPE_CONST,  {.i64=BILINEAR},        0,                   0, FLAGS, "interp" },
    {      "cube", "bicubic interpolation",                      0, AV_OPT_TYPE_CONST,  {.i64=BICUBIC},         0,                   0, FLAGS, "interp" },
    {     "cubic", "bicubic interpolation",                      0, AV_OPT_TYPE_CONST,  {.i64=BICUBIC},         0,                   0, FLAGS, "interp" },
    {      "lanc", "lanczos interpolation",                      0, AV_OPT_TYPE_CONST,  {.i64=LANCZOS},         0,                   0, FLAGS, "interp" },
    {   "lanczos", "lanczos interpolation",                      0, AV_OPT_TYPE_CONST,  {.i64=LANCZOS},         0,                   0, FLAGS, "interp" },
    {         "w", "output width",                   OFFSET(width), AV_OPT_TYPE_INT,    {.i64=0},               0,             INT_MAX, FLAGS, "w"},
    {         "h", "output height",                 OFFSET(height), AV_OPT_TYPE_INT,    {.i64=0},               0,             INT_MAX, FLAGS, "h"},
    { "in_forder", "input cubemap face order",   OFFSET(in_forder), AV_OPT_TYPE_STRING, {.str="rludfb"},        0,     NB_DIRECTIONS-1, FLAGS, "in_forder"},
    {"out_forder", "output cubemap face order", OFFSET(out_forder), AV_OPT_TYPE_STRING, {.str="rludfb"},        0,     NB_DIRECTIONS-1, FLAGS, "out_forder"},
    {   "in_frot", "input cubemap face rotation",  OFFSET(in_frot), AV_OPT_TYPE_STRING, {.str="000000"},        0,     NB_DIRECTIONS-1, FLAGS, "in_frot"},
    {  "out_frot", "output cubemap face rotation",OFFSET(out_frot), AV_OPT_TYPE_STRING, {.str="000000"},        0,     NB_DIRECTIONS-1, FLAGS, "out_frot"},
    {    "in_pad", "input cubemap pads",            OFFSET(in_pad), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},           0.f,                 1.f, FLAGS, "in_pad"},
    {   "out_pad", "output cubemap pads",          OFFSET(out_pad), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},           0.f,                 1.f, FLAGS, "out_pad"},
    {       "yaw", "yaw rotation",                     OFFSET(yaw), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},        -180.f,               180.f, FLAGS, "yaw"},
    {     "pitch", "pitch rotation",                 OFFSET(pitch), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},        -180.f,               180.f, FLAGS, "pitch"},
    {      "roll", "roll rotation",                   OFFSET(roll), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},        -180.f,               180.f, FLAGS, "roll"},
    {     "h_fov", "horizontal field of view",       OFFSET(h_fov), AV_OPT_TYPE_FLOAT,  {.dbl=90.f},          0.f,               180.f, FLAGS, "h_fov"},
    {     "v_fov", "vertical field of view",         OFFSET(v_fov), AV_OPT_TYPE_FLOAT,  {.dbl=45.f},          0.f,                90.f, FLAGS, "v_fov"},
    {    "h_flip", "flip video horizontally",       OFFSET(h_flip), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1, FLAGS, "h_flip"},
    {    "v_flip", "flip video vertically",         OFFSET(v_flip), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1, FLAGS, "v_flip"},
    {    "d_flip", "flip video indepth",            OFFSET(d_flip), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1, FLAGS, "d_flip"},
    { NULL }
};

AVFILTER_DEFINE_CLASS(v360);

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        // YUVA444
        AV_PIX_FMT_YUVA444P,   AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12,
        AV_PIX_FMT_YUVA444P16,

        // YUVA422
        AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA422P9,
        AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12,
        AV_PIX_FMT_YUVA422P16,

        // YUVA420
        AV_PIX_FMT_YUVA420P,   AV_PIX_FMT_YUVA420P9,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,

        // YUVJ
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
        AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
        AV_PIX_FMT_YUVJ411P,

        // YUV444
        AV_PIX_FMT_YUV444P,   AV_PIX_FMT_YUV444P9,
        AV_PIX_FMT_YUV444P10, AV_PIX_FMT_YUV444P12,
        AV_PIX_FMT_YUV444P14, AV_PIX_FMT_YUV444P16,

        // YUV440
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV440P10,
        AV_PIX_FMT_YUV440P12,

        // YUV422
        AV_PIX_FMT_YUV422P,   AV_PIX_FMT_YUV422P9,
        AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV422P12,
        AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV422P16,

        // YUV420
        AV_PIX_FMT_YUV420P,   AV_PIX_FMT_YUV420P9,
        AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV420P12,
        AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV420P16,

        // YUV411
        AV_PIX_FMT_YUV411P,

        // YUV410
        AV_PIX_FMT_YUV410P,

        // GBR
        AV_PIX_FMT_GBRP,   AV_PIX_FMT_GBRP9,
        AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12,
        AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,

        // GBRA
        AV_PIX_FMT_GBRAP,   AV_PIX_FMT_GBRAP10,
        AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,

        // GRAY
        AV_PIX_FMT_GRAY8,  AV_PIX_FMT_GRAY9,
        AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12,
        AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,

        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

typedef struct XYRemap1 {
    uint16_t u;
    uint16_t v;
} XYRemap1;

/**
 * Generate no-interpolation remapping function with a given pixel depth.
 *
 * @param bits number of bits per pixel
 * @param div number of bytes per pixel
 */
#define DEFINE_REMAP1(bits, div)                                                             \
static int remap1_##bits##bit_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs) \
{                                                                                            \
    ThreadData *td = (ThreadData*)arg;                                                       \
    const V360Context *s = td->s;                                                            \
    const AVFrame *in = td->in;                                                              \
    AVFrame *out = td->out;                                                                  \
                                                                                             \
    int plane, x, y;                                                                         \
                                                                                             \
    for (plane = 0; plane < td->nb_planes; plane++) {                                        \
        const int in_linesize  = in->linesize[plane]  / div;                                 \
        const int out_linesize = out->linesize[plane] / div;                                 \
        const uint##bits##_t *src = (const uint##bits##_t *)in->data[plane];                 \
        uint##bits##_t *dst = (uint##bits##_t *)out->data[plane];                            \
        const XYRemap1 *remap = s->remap[plane];                                             \
        const int width = s->planewidth[plane];                                              \
        const int height = s->planeheight[plane];                                            \
                                                                                             \
        const int slice_start = (height *  jobnr     ) / nb_jobs;                            \
        const int slice_end   = (height * (jobnr + 1)) / nb_jobs;                            \
                                                                                             \
        for (y = slice_start; y < slice_end; y++) {                                          \
            uint##bits##_t *d = dst + y * out_linesize;                                      \
            for (x = 0; x < width; x++) {                                                    \
                const XYRemap1 *r = &remap[y * width + x];                                   \
                                                                                             \
                *d++ = src[r->v * in_linesize + r->u];                                       \
            }                                                                                \
        }                                                                                    \
    }                                                                                        \
                                                                                             \
    return 0;                                                                                \
}

DEFINE_REMAP1( 8, 1)
DEFINE_REMAP1(16, 2)

typedef struct XYRemap2 {
    uint16_t u[2][2];
    uint16_t v[2][2];
    float ker[2][2];
} XYRemap2;

typedef struct XYRemap4 {
    uint16_t u[4][4];
    uint16_t v[4][4];
    float ker[4][4];
} XYRemap4;

/**
 * Generate remapping function with a given window size and pixel depth.
 *
 * @param window_size size of interpolation window
 * @param bits number of bits per pixel
 * @param div number of bytes per pixel
 */
#define DEFINE_REMAP(window_size, bits, div)                                                               \
static int remap##window_size##_##bits##bit_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs) \
{                                                                                                          \
    ThreadData *td = (ThreadData*)arg;                                                                     \
    const V360Context *s = td->s;                                                                          \
    const AVFrame *in = td->in;                                                                            \
    AVFrame *out = td->out;                                                                                \
                                                                                                           \
    int plane, x, y, i, j;                                                                                 \
                                                                                                           \
    for (plane = 0; plane < td->nb_planes; plane++) {                                                      \
        const int in_linesize  = in->linesize[plane]  / div;                                               \
        const int out_linesize = out->linesize[plane] / div;                                               \
        const uint##bits##_t *src = (const uint##bits##_t *)in->data[plane];                               \
        uint##bits##_t *dst = (uint##bits##_t *)out->data[plane];                                          \
        const XYRemap##window_size *remap = s->remap[plane];                                               \
        const int width = s->planewidth[plane];                                                            \
        const int height = s->planeheight[plane];                                                          \
                                                                                                           \
        const int slice_start = (height *  jobnr     ) / nb_jobs;                                          \
        const int slice_end   = (height * (jobnr + 1)) / nb_jobs;                                          \
                                                                                                           \
        for (y = slice_start; y < slice_end; y++) {                                                        \
            uint##bits##_t *d = dst + y * out_linesize;                                                    \
            for (x = 0; x < width; x++) {                                                                  \
                const XYRemap##window_size *r = &remap[y * width + x];                                     \
                float tmp = 0.f;                                                                           \
                                                                                                           \
                for (i = 0; i < window_size; i++) {                                                        \
                    for (j = 0; j < window_size; j++) {                                                    \
                        tmp += r->ker[i][j] * src[r->v[i][j] * in_linesize + r->u[i][j]];                  \
                    }                                                                                      \
                }                                                                                          \
                                                                                                           \
                *d++ = av_clip_uint##bits(roundf(tmp));                                                    \
            }                                                                                              \
        }                                                                                                  \
    }                                                                                                      \
                                                                                                           \
    return 0;                                                                                              \
}

DEFINE_REMAP(2,  8, 1)
DEFINE_REMAP(4,  8, 1)
DEFINE_REMAP(2, 16, 2)
DEFINE_REMAP(4, 16, 2)

/**
 * Save nearest pixel coordinates for remapping.
 *
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 * @param shift shift for remap array
 * @param r_tmp calculated 4x4 window
 * @param r_void remap data
 */
static void nearest_kernel(float du, float dv, int shift, const XYRemap4 *r_tmp, void *r_void)
{
    XYRemap1 *r = (XYRemap1*)r_void + shift;
    const int i = roundf(dv) + 1;
    const int j = roundf(du) + 1;

    r->u = r_tmp->u[i][j];
    r->v = r_tmp->v[i][j];
}

/**
 * Calculate kernel for bilinear interpolation.
 *
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 * @param shift shift for remap array
 * @param r_tmp calculated 4x4 window
 * @param r_void remap data
 */
static void bilinear_kernel(float du, float dv, int shift, const XYRemap4 *r_tmp, void *r_void)
{
    XYRemap2 *r = (XYRemap2*)r_void + shift;
    int i, j;

    for (i = 0; i < 2; i++) {
        for (j = 0; j < 2; j++) {
            r->u[i][j] = r_tmp->u[i + 1][j + 1];
            r->v[i][j] = r_tmp->v[i + 1][j + 1];
        }
    }

    r->ker[0][0] = (1.f - du) * (1.f - dv);
    r->ker[0][1] =        du  * (1.f - dv);
    r->ker[1][0] = (1.f - du) *        dv;
    r->ker[1][1] =        du  *        dv;
}

/**
 * Calculate 1-dimensional cubic coefficients.
 *
 * @param t relative coordinate
 * @param coeffs coefficients
 */
static inline void calculate_bicubic_coeffs(float t, float *coeffs)
{
    const float tt  = t * t;
    const float ttt = t * t * t;

    coeffs[0] =     - t / 3.f + tt / 2.f - ttt / 6.f;
    coeffs[1] = 1.f - t / 2.f - tt       + ttt / 2.f;
    coeffs[2] =       t       + tt / 2.f - ttt / 2.f;
    coeffs[3] =     - t / 6.f            + ttt / 6.f;
}

/**
 * Calculate kernel for bicubic interpolation.
 *
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 * @param shift shift for remap array
 * @param r_tmp calculated 4x4 window
 * @param r_void remap data
 */
static void bicubic_kernel(float du, float dv, int shift, const XYRemap4 *r_tmp, void *r_void)
{
    XYRemap4 *r = (XYRemap4*)r_void + shift;
    int i, j;
    float du_coeffs[4];
    float dv_coeffs[4];

    calculate_bicubic_coeffs(du, du_coeffs);
    calculate_bicubic_coeffs(dv, dv_coeffs);

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            r->u[i][j] = r_tmp->u[i][j];
            r->v[i][j] = r_tmp->v[i][j];
            r->ker[i][j] = du_coeffs[j] * dv_coeffs[i];
        }
    }
}

/**
 * Calculate 1-dimensional lanczos coefficients.
 *
 * @param t relative coordinate
 * @param coeffs coefficients
 */
static inline void calculate_lanczos_coeffs(float t, float *coeffs)
{
    int i;
    float sum = 0.f;

    for (i = 0; i < 4; i++) {
        const float x = M_PI * (t - i + 1);
        if (x == 0.f) {
            coeffs[i] = 1.f;
        } else {
            coeffs[i] = sinf(x) * sinf(x / 2.f) / (x * x / 2.f);
        }
        sum += coeffs[i];
    }

    for (i = 0; i < 4; i++) {
        coeffs[i] /= sum;
    }
}

/**
 * Calculate kernel for lanczos interpolation.
 *
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 * @param shift shift for remap array
 * @param r_tmp calculated 4x4 window
 * @param r_void remap data
 */
static void lanczos_kernel(float du, float dv, int shift, const XYRemap4 *r_tmp, void *r_void)
{
    XYRemap4 *r = (XYRemap4*)r_void + shift;
    int i, j;
    float du_coeffs[4];
    float dv_coeffs[4];

    calculate_lanczos_coeffs(du, du_coeffs);
    calculate_lanczos_coeffs(dv, dv_coeffs);

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            r->u[i][j] = r_tmp->u[i][j];
            r->v[i][j] = r_tmp->v[i][j];
            r->ker[i][j] = du_coeffs[j] * dv_coeffs[i];
        }
    }
}

/**
 * Modulo operation with only positive remainders.
 *
 * @param a dividend
 * @param b divisor
 *
 * @return positive remainder of (a / b)
 */
static inline int mod(int a, int b)
{
    const int res = a % b;
    if (res < 0) {
        return res + b;
    } else {
        return res;
    }
}

/**
 * Convert char to corresponding direction.
 * Used for cubemap options.
 */
static int get_direction(char c)
{
    switch (c) {
    case 'r':
        return RIGHT;
    case 'l':
        return LEFT;
    case 'u':
        return UP;
    case 'd':
        return DOWN;
    case 'f':
        return FRONT;
    case 'b':
        return BACK;
    default:
        return -1;
    }
}

/**
 * Convert char to corresponding rotation angle.
 * Used for cubemap options.
 */
static int get_rotation(char c)
{
    switch (c) {
    case '0':
        return ROT_0;
    case '1':
        return ROT_90;
    case '2':
        return ROT_180;
    case '3':
        return ROT_270;
    default:
        return -1;
    }
}

/**
 * Prepare data for processing cubemap input format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_cube_in(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    for (int face = 0; face < NB_FACES; face++) {
        const char c = s->in_forder[face];
        int direction;

        if (c == '\0') {
            av_log(ctx, AV_LOG_ERROR,
                   "Incomplete in_forder option. Direction for all 6 faces should be specified.\n");
            return AVERROR(EINVAL);
        }

        direction = get_direction(c);
        if (direction == -1) {
            av_log(ctx, AV_LOG_ERROR,
                   "Incorrect direction symbol '%c' in in_forder option.\n", c);
            return AVERROR(EINVAL);
        }

        s->in_cubemap_face_order[direction] = face;
    }

    for (int face = 0; face < NB_FACES; face++) {
        const char c = s->in_frot[face];
        int rotation;

        if (c == '\0') {
            av_log(ctx, AV_LOG_ERROR,
                   "Incomplete in_frot option. Rotation for all 6 faces should be specified.\n");
            return AVERROR(EINVAL);
        }

        rotation = get_rotation(c);
        if (rotation == -1) {
            av_log(ctx, AV_LOG_ERROR,
                   "Incorrect rotation symbol '%c' in in_frot option.\n", c);
            return AVERROR(EINVAL);
        }

        s->in_cubemap_face_rotation[face] = rotation;
    }

    return 0;
}

/**
 * Prepare data for processing cubemap output format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_cube_out(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    for (int face = 0; face < NB_FACES; face++) {
        const char c = s->out_forder[face];
        int direction;

        if (c == '\0') {
            av_log(ctx, AV_LOG_ERROR,
                   "Incomplete out_forder option. Direction for all 6 faces should be specified.\n");
            return AVERROR(EINVAL);
        }

        direction = get_direction(c);
        if (direction == -1) {
            av_log(ctx, AV_LOG_ERROR,
                   "Incorrect direction symbol '%c' in out_forder option.\n", c);
            return AVERROR(EINVAL);
        }

        s->out_cubemap_direction_order[face] = direction;
    }

    for (int face = 0; face < NB_FACES; face++) {
        const char c = s->out_frot[face];
        int rotation;

        if (c == '\0') {
            av_log(ctx, AV_LOG_ERROR,
                   "Incomplete out_frot option. Rotation for all 6 faces should be specified.\n");
            return AVERROR(EINVAL);
        }

        rotation = get_rotation(c);
        if (rotation == -1) {
            av_log(ctx, AV_LOG_ERROR,
                   "Incorrect rotation symbol '%c' in out_frot option.\n", c);
            return AVERROR(EINVAL);
        }

        s->out_cubemap_face_rotation[face] = rotation;
    }

    return 0;
}

static inline void rotate_cube_face(float *uf, float *vf, int rotation)
{
    float tmp;

    switch (rotation) {
    case ROT_0:
        break;
    case ROT_90:
        tmp =  *uf;
        *uf = -*vf;
        *vf =  tmp;
        break;
    case ROT_180:
        *uf = -*uf;
        *vf = -*vf;
        break;
    case ROT_270:
        tmp = -*uf;
        *uf =  *vf;
        *vf =  tmp;
        break;
    }
}

static inline void rotate_cube_face_inverse(float *uf, float *vf, int rotation)
{
    float tmp;

    switch (rotation) {
    case ROT_0:
        break;
    case ROT_90:
        tmp = -*uf;
        *uf =  *vf;
        *vf =  tmp;
        break;
    case ROT_180:
        *uf = -*uf;
        *vf = -*vf;
        break;
    case ROT_270:
        tmp =  *uf;
        *uf = -*vf;
        *vf =  tmp;
        break;
    }
}

/**
 * Calculate 3D coordinates on sphere for corresponding cubemap position.
 * Common operation for every cubemap.
 *
 * @param s filter context
 * @param uf horizontal cubemap coordinate [0, 1)
 * @param vf vertical cubemap coordinate [0, 1)
 * @param face face of cubemap
 * @param vec coordinates on sphere
 */
static void cube_to_xyz(const V360Context *s,
                        float uf, float vf, int face,
                        float *vec)
{
    const int direction = s->out_cubemap_direction_order[face];
    float norm;
    float l_x, l_y, l_z;

    uf /= (1.f - s->out_pad);
    vf /= (1.f - s->out_pad);

    rotate_cube_face_inverse(&uf, &vf, s->out_cubemap_face_rotation[face]);

    switch (direction) {
    case RIGHT:
        l_x =  1.f;
        l_y = -vf;
        l_z =  uf;
        break;
    case LEFT:
        l_x = -1.f;
        l_y = -vf;
        l_z = -uf;
        break;
    case UP:
        l_x =  uf;
        l_y =  1.f;
        l_z = -vf;
        break;
    case DOWN:
        l_x =  uf;
        l_y = -1.f;
        l_z =  vf;
        break;
    case FRONT:
        l_x =  uf;
        l_y = -vf;
        l_z = -1.f;
        break;
    case BACK:
        l_x = -uf;
        l_y = -vf;
        l_z =  1.f;
        break;
    }

    norm = sqrtf(l_x * l_x + l_y * l_y + l_z * l_z);
    vec[0] = l_x / norm;
    vec[1] = l_y / norm;
    vec[2] = l_z / norm;
}

/**
 * Calculate cubemap position for corresponding 3D coordinates on sphere.
 * Common operation for every cubemap.
 *
 * @param s filter context
 * @param vec coordinated on sphere
 * @param uf horizontal cubemap coordinate [0, 1)
 * @param vf vertical cubemap coordinate [0, 1)
 * @param direction direction of view
 */
static void xyz_to_cube(const V360Context *s,
                        const float *vec,
                        float *uf, float *vf, int *direction)
{
    const float phi   = atan2f(vec[0], -vec[2]);
    const float theta = asinf(-vec[1]);
    float phi_norm, theta_threshold;
    int face;

    if (phi >= -M_PI_4 && phi < M_PI_4) {
        *direction = FRONT;
        phi_norm = phi;
    } else if (phi >= -(M_PI_2 + M_PI_4) && phi < -M_PI_4) {
        *direction = LEFT;
        phi_norm = phi + M_PI_2;
    } else if (phi >= M_PI_4 && phi < M_PI_2 + M_PI_4) {
        *direction = RIGHT;
        phi_norm = phi - M_PI_2;
    } else {
        *direction = BACK;
        phi_norm = phi + ((phi > 0.f) ? -M_PI : M_PI);
    }

    theta_threshold = atanf(cosf(phi_norm));
    if (theta > theta_threshold) {
        *direction = DOWN;
    } else if (theta < -theta_threshold) {
        *direction = UP;
    }

    switch (*direction) {
    case RIGHT:
        *uf =  vec[2] / vec[0];
        *vf = -vec[1] / vec[0];
        break;
    case LEFT:
        *uf =  vec[2] / vec[0];
        *vf =  vec[1] / vec[0];
        break;
    case UP:
        *uf =  vec[0] / vec[1];
        *vf = -vec[2] / vec[1];
        break;
    case DOWN:
        *uf = -vec[0] / vec[1];
        *vf = -vec[2] / vec[1];
        break;
    case FRONT:
        *uf = -vec[0] / vec[2];
        *vf =  vec[1] / vec[2];
        break;
    case BACK:
        *uf = -vec[0] / vec[2];
        *vf = -vec[1] / vec[2];
        break;
    }

    face = s->in_cubemap_face_order[*direction];
    rotate_cube_face(uf, vf, s->in_cubemap_face_rotation[face]);
}

/**
 * Find position on another cube face in case of overflow/underflow.
 * Used for calculation of interpolation window.
 *
 * @param s filter context
 * @param uf horizontal cubemap coordinate
 * @param vf vertical cubemap coordinate
 * @param direction direction of view
 * @param new_uf new horizontal cubemap coordinate
 * @param new_vf new vertical cubemap coordinate
 * @param face face position on cubemap
 */
static void process_cube_coordinates(const V360Context *s,
                                float uf, float vf, int direction,
                                float *new_uf, float *new_vf, int *face)
{
    /*
     *  Cubemap orientation
     *
     *           width
     *         <------->
     *         +-------+
     *         |       |                              U
     *         | up    |                   h       ------->
     * +-------+-------+-------+-------+ ^ e      |
     * |       |       |       |       | | i    V |
     * | left  | front | right | back  | | g      |
     * +-------+-------+-------+-------+ v h      v
     *         |       |                   t
     *         | down  |
     *         +-------+
     */

    *face = s->in_cubemap_face_order[direction];
    rotate_cube_face_inverse(&uf, &vf, s->in_cubemap_face_rotation[*face]);

    if ((uf < -1.f || uf >= 1.f) && (vf < -1.f || vf >= 1.f)) {
        // There are no pixels to use in this case
        *new_uf = uf;
        *new_vf = vf;
    } else if (uf < -1.f) {
        uf += 2.f;
        switch (direction) {
        case RIGHT:
            direction = FRONT;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        case LEFT:
            direction = BACK;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        case UP:
            direction = LEFT;
            *new_uf =  vf;
            *new_vf = -uf;
            break;
        case DOWN:
            direction = LEFT;
            *new_uf = -vf;
            *new_vf =  uf;
            break;
        case FRONT:
            direction = LEFT;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        case BACK:
            direction = RIGHT;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        }
    } else if (uf >= 1.f) {
        uf -= 2.f;
        switch (direction) {
        case RIGHT:
            direction = BACK;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        case LEFT:
            direction = FRONT;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        case UP:
            direction = RIGHT;
            *new_uf = -vf;
            *new_vf =  uf;
            break;
        case DOWN:
            direction = RIGHT;
            *new_uf =  vf;
            *new_vf = -uf;
            break;
        case FRONT:
            direction = RIGHT;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        case BACK:
            direction = LEFT;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        }
    } else if (vf < -1.f) {
        vf += 2.f;
        switch (direction) {
        case RIGHT:
            direction = UP;
            *new_uf =  vf;
            *new_vf = -uf;
            break;
        case LEFT:
            direction = UP;
            *new_uf = -vf;
            *new_vf =  uf;
            break;
        case UP:
            direction = BACK;
            *new_uf = -uf;
            *new_vf = -vf;
            break;
        case DOWN:
            direction = FRONT;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        case FRONT:
            direction = UP;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        case BACK:
            direction = UP;
            *new_uf = -uf;
            *new_vf = -vf;
            break;
        }
    } else if (vf >= 1.f) {
        vf -= 2.f;
        switch (direction) {
        case RIGHT:
            direction = DOWN;
            *new_uf = -vf;
            *new_vf =  uf;
            break;
        case LEFT:
            direction = DOWN;
            *new_uf =  vf;
            *new_vf = -uf;
            break;
        case UP:
            direction = FRONT;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        case DOWN:
            direction = BACK;
            *new_uf = -uf;
            *new_vf = -vf;
            break;
        case FRONT:
            direction = DOWN;
            *new_uf =  uf;
            *new_vf =  vf;
            break;
        case BACK:
            direction = DOWN;
            *new_uf = -uf;
            *new_vf = -vf;
            break;
        }
    } else {
        // Inside cube face
        *new_uf = uf;
        *new_vf = vf;
    }

    *face = s->in_cubemap_face_order[direction];
    rotate_cube_face(new_uf, new_vf, s->in_cubemap_face_rotation[*face]);
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in cubemap3x2 format.
 *
 * @param s filter context
 * @param i horizontal position on frame [0, height)
 * @param j vertical position on frame [0, width)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static void cube3x2_to_xyz(const V360Context *s,
                           int i, int j, int width, int height,
                           float *vec)
{
    const float ew = width  / 3.f;
    const float eh = height / 2.f;

    const int u_face = floorf(i / ew);
    const int v_face = floorf(j / eh);
    const int face = u_face + 3 * v_face;

    const int u_shift = ceilf(ew * u_face);
    const int v_shift = ceilf(eh * v_face);
    const int ewi = ceilf(ew * (u_face + 1)) - u_shift;
    const int ehi = ceilf(eh * (v_face + 1)) - v_shift;

    const float uf = 2.f * (i - u_shift) / ewi - 1.f;
    const float vf = 2.f * (j - v_shift) / ehi - 1.f;

    cube_to_xyz(s, uf, vf, face, vec);
}

/**
 * Calculate frame position in cubemap3x2 format for corresponding 3D coordinates on sphere.
 *
 * @param s filter context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static void xyz_to_cube3x2(const V360Context *s,
                           const float *vec, int width, int height,
                           uint16_t us[4][4], uint16_t vs[4][4], float *du, float *dv)
{
    const float ew = width  / 3.f;
    const float eh = height / 2.f;
    float uf, vf;
    int ui, vi;
    int ewi, ehi;
    int i, j;
    int direction, face;
    int u_face, v_face;

    xyz_to_cube(s, vec, &uf, &vf, &direction);

    uf *= (1.f - s->in_pad);
    vf *= (1.f - s->in_pad);

    face = s->in_cubemap_face_order[direction];
    u_face = face % 3;
    v_face = face / 3;
    ewi = ceilf(ew * (u_face + 1)) - ceilf(ew * u_face);
    ehi = ceilf(eh * (v_face + 1)) - ceilf(eh * v_face);

    uf = 0.5f * ewi * (uf + 1.f);
    vf = 0.5f * ehi * (vf + 1.f);

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (i = -1; i < 3; i++) {
        for (j = -1; j < 3; j++) {
            int new_ui = ui + j;
            int new_vi = vi + i;
            int u_shift, v_shift;
            int new_ewi, new_ehi;

            if (new_ui >= 0 && new_ui < ewi && new_vi >= 0 && new_vi < ehi) {
                face = s->in_cubemap_face_order[direction];

                u_face = face % 3;
                v_face = face / 3;
                u_shift = ceilf(ew * u_face);
                v_shift = ceilf(eh * v_face);
            } else {
                uf = 2.f * new_ui / ewi - 1.f;
                vf = 2.f * new_vi / ehi - 1.f;

                uf /= (1.f - s->in_pad);
                vf /= (1.f - s->in_pad);

                process_cube_coordinates(s, uf, vf, direction, &uf, &vf, &face);

                uf *= (1.f - s->in_pad);
                vf *= (1.f - s->in_pad);

                u_face = face % 3;
                v_face = face / 3;
                u_shift = ceilf(ew * u_face);
                v_shift = ceilf(eh * v_face);
                new_ewi = ceilf(ew * (u_face + 1)) - u_shift;
                new_ehi = ceilf(eh * (v_face + 1)) - v_shift;

                new_ui = av_clip(roundf(0.5f * new_ewi * (uf + 1.f)), 0, new_ewi - 1);
                new_vi = av_clip(roundf(0.5f * new_ehi * (vf + 1.f)), 0, new_ehi - 1);
            }


            us[i + 1][j + 1] = u_shift + new_ui;
            vs[i + 1][j + 1] = v_shift + new_vi;
        }
    }
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in cubemap6x1 format.
 *
 * @param s filter context
 * @param i horizontal position on frame [0, height)
 * @param j vertical position on frame [0, width)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static void cube6x1_to_xyz(const V360Context *s,
                           int i, int j, int width, int height,
                           float *vec)
{
    const float ew = width / 6.f;
    const float eh = height;

    const int face = floorf(i / ew);

    const int u_shift = ceilf(ew * face);
    const int ewi = ceilf(ew * (face + 1)) - u_shift;

    const float uf = 2.f * (i - u_shift) / ewi - 1.f;
    const float vf = 2.f *  j            / eh  - 1.f;

    cube_to_xyz(s, uf, vf, face, vec);
}

/**
 * Calculate frame position in cubemap6x1 format for corresponding 3D coordinates on sphere.
 *
 * @param s filter context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static void xyz_to_cube6x1(const V360Context *s,
                           const float *vec, int width, int height,
                           uint16_t us[4][4], uint16_t vs[4][4], float *du, float *dv)
{
    const float ew = width / 6.f;
    const int ehi = height;
    float uf, vf;
    int ui, vi;
    int ewi;
    int i, j;
    int direction, face;

    xyz_to_cube(s, vec, &uf, &vf, &direction);

    uf *= (1.f - s->in_pad);
    vf *= (1.f - s->in_pad);

    face = s->in_cubemap_face_order[direction];
    ewi = ceilf(ew * (face + 1)) - ceilf(ew * face);

    uf = 0.5f * ewi * (uf + 1.f);
    vf = 0.5f * ehi * (vf + 1.f);

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (i = -1; i < 3; i++) {
        for (j = -1; j < 3; j++) {
            int new_ui = ui + j;
            int new_vi = vi + i;
            int u_shift;
            int new_ewi;

            if (new_ui >= 0 && new_ui < ewi && new_vi >= 0 && new_vi < ehi) {
                face = s->in_cubemap_face_order[direction];

                u_shift = ceilf(ew * face);
            } else {
                uf = 2.f * new_ui / ewi - 1.f;
                vf = 2.f * new_vi / ehi - 1.f;

                uf /= (1.f - s->in_pad);
                vf /= (1.f - s->in_pad);

                process_cube_coordinates(s, uf, vf, direction, &uf, &vf, &face);

                uf *= (1.f - s->in_pad);
                vf *= (1.f - s->in_pad);

                u_shift = ceilf(ew * face);
                new_ewi = ceilf(ew * (face + 1)) - u_shift;

                new_ui = av_clip(roundf(0.5f * new_ewi * (uf + 1.f)), 0, new_ewi - 1);
                new_vi = av_clip(roundf(0.5f *     ehi * (vf + 1.f)), 0,     ehi - 1);
            }


            us[i + 1][j + 1] = u_shift + new_ui;
            vs[i + 1][j + 1] =           new_vi;
        }
    }
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in equirectangular format.
 *
 * @param s filter context
 * @param i horizontal position on frame [0, height)
 * @param j vertical position on frame [0, width)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static void equirect_to_xyz(const V360Context *s,
                            int i, int j, int width, int height,
                            float *vec)
{
    const float phi   = ((2.f * i) / width  - 1.f) * M_PI;
    const float theta = ((2.f * j) / height - 1.f) * M_PI_2;

    const float sin_phi   = sinf(phi);
    const float cos_phi   = cosf(phi);
    const float sin_theta = sinf(theta);
    const float cos_theta = cosf(theta);

    vec[0] =  cos_theta * sin_phi;
    vec[1] = -sin_theta;
    vec[2] = -cos_theta * cos_phi;
}

/**
 * Calculate frame position in equirectangular format for corresponding 3D coordinates on sphere.
 *
 * @param s filter context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static void xyz_to_equirect(const V360Context *s,
                            const float *vec, int width, int height,
                            uint16_t us[4][4], uint16_t vs[4][4], float *du, float *dv)
{
    const float phi   = atan2f(vec[0], -vec[2]);
    const float theta = asinf(-vec[1]);
    float uf, vf;
    int ui, vi;
    int i, j;

    uf = (phi   / M_PI   + 1.f) * width  / 2.f;
    vf = (theta / M_PI_2 + 1.f) * height / 2.f;
    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (i = -1; i < 3; i++) {
        for (j = -1; j < 3; j++) {
            us[i + 1][j + 1] = mod(ui + j, width);
            vs[i + 1][j + 1] = av_clip(vi + i, 0, height - 1);
        }
    }
}

/**
 * Prepare data for processing equi-angular cubemap input format.
 *
 * @param ctx filter context

 * @return error code
 */
static int prepare_eac_in(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->in_cubemap_face_order[RIGHT] = TOP_RIGHT;
    s->in_cubemap_face_order[LEFT]  = TOP_LEFT;
    s->in_cubemap_face_order[UP]    = BOTTOM_RIGHT;
    s->in_cubemap_face_order[DOWN]  = BOTTOM_LEFT;
    s->in_cubemap_face_order[FRONT] = TOP_MIDDLE;
    s->in_cubemap_face_order[BACK]  = BOTTOM_MIDDLE;

    s->in_cubemap_face_rotation[TOP_LEFT]      = ROT_0;
    s->in_cubemap_face_rotation[TOP_MIDDLE]    = ROT_0;
    s->in_cubemap_face_rotation[TOP_RIGHT]     = ROT_0;
    s->in_cubemap_face_rotation[BOTTOM_LEFT]   = ROT_270;
    s->in_cubemap_face_rotation[BOTTOM_MIDDLE] = ROT_90;
    s->in_cubemap_face_rotation[BOTTOM_RIGHT]  = ROT_270;

    return 0;
}

/**
 * Prepare data for processing equi-angular cubemap output format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_eac_out(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->out_cubemap_direction_order[TOP_LEFT]      = LEFT;
    s->out_cubemap_direction_order[TOP_MIDDLE]    = FRONT;
    s->out_cubemap_direction_order[TOP_RIGHT]     = RIGHT;
    s->out_cubemap_direction_order[BOTTOM_LEFT]   = DOWN;
    s->out_cubemap_direction_order[BOTTOM_MIDDLE] = BACK;
    s->out_cubemap_direction_order[BOTTOM_RIGHT]  = UP;

    s->out_cubemap_face_rotation[TOP_LEFT]      = ROT_0;
    s->out_cubemap_face_rotation[TOP_MIDDLE]    = ROT_0;
    s->out_cubemap_face_rotation[TOP_RIGHT]     = ROT_0;
    s->out_cubemap_face_rotation[BOTTOM_LEFT]   = ROT_270;
    s->out_cubemap_face_rotation[BOTTOM_MIDDLE] = ROT_90;
    s->out_cubemap_face_rotation[BOTTOM_RIGHT]  = ROT_270;

    return 0;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in equi-angular cubemap format.
 *
 * @param s filter context
 * @param i horizontal position on frame [0, height)
 * @param j vertical position on frame [0, width)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static void eac_to_xyz(const V360Context *s,
                       int i, int j, int width, int height,
                       float *vec)
{
    const float pixel_pad = 2;
    const float u_pad = pixel_pad / width;
    const float v_pad = pixel_pad / height;

    int u_face, v_face, face;

    float l_x, l_y, l_z;
    float norm;

    float uf = (float)i / width;
    float vf = (float)j / height;

    // EAC has 2-pixel padding on faces except between faces on the same row
    // Padding pixels seems not to be stretched with tangent as regular pixels
    // Formulas below approximate original padding as close as I could get experimentally

    // Horizontal padding
    uf = 3.f * (uf - u_pad) / (1.f - 2.f * u_pad);
    if (uf < 0.f) {
        u_face = 0;
        uf -= 0.5f;
    } else if (uf >= 3.f) {
        u_face = 2;
        uf -= 2.5f;
    } else {
        u_face = floorf(uf);
        uf = fmodf(uf, 1.f) - 0.5f;
    }

    // Vertical padding
    v_face = floorf(vf * 2.f);
    vf = (vf - v_pad - 0.5f * v_face) / (0.5f - 2.f * v_pad) - 0.5f;

    if (uf >= -0.5f && uf < 0.5f) {
        uf = tanf(M_PI_2 * uf);
    } else {
        uf = 2.f * uf;
    }
    if (vf >= -0.5f && vf < 0.5f) {
        vf = tanf(M_PI_2 * vf);
    } else {
        vf = 2.f * vf;
    }

    face = u_face + 3 * v_face;

    switch (face) {
    case TOP_LEFT:
        l_x = -1.f;
        l_y = -vf;
        l_z = -uf;
        break;
    case TOP_MIDDLE:
        l_x =  uf;
        l_y = -vf;
        l_z = -1.f;
        break;
    case TOP_RIGHT:
        l_x =  1.f;
        l_y = -vf;
        l_z =  uf;
        break;
    case BOTTOM_LEFT:
        l_x = -vf;
        l_y = -1.f;
        l_z =  uf;
        break;
    case BOTTOM_MIDDLE:
        l_x = -vf;
        l_y =  uf;
        l_z =  1.f;
        break;
    case BOTTOM_RIGHT:
        l_x = -vf;
        l_y =  1.f;
        l_z = -uf;
        break;
    }

    norm = sqrtf(l_x * l_x + l_y * l_y + l_z * l_z);
    vec[0] = l_x / norm;
    vec[1] = l_y / norm;
    vec[2] = l_z / norm;
}

/**
 * Calculate frame position in equi-angular cubemap format for corresponding 3D coordinates on sphere.
 *
 * @param s filter context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static void xyz_to_eac(const V360Context *s,
                       const float *vec, int width, int height,
                       uint16_t us[4][4], uint16_t vs[4][4], float *du, float *dv)
{
    const float pixel_pad = 2;
    const float u_pad = pixel_pad / width;
    const float v_pad = pixel_pad / height;

    float uf, vf;
    int ui, vi;
    int i, j;
    int direction, face;
    int u_face, v_face;

    xyz_to_cube(s, vec, &uf, &vf, &direction);

    face = s->in_cubemap_face_order[direction];
    u_face = face % 3;
    v_face = face / 3;

    uf = M_2_PI * atanf(uf) + 0.5f;
    vf = M_2_PI * atanf(vf) + 0.5f;

    // These formulas are inversed from eac_to_xyz ones
    uf = (uf + u_face) * (1.f - 2.f * u_pad) / 3.f + u_pad;
    vf = vf * (0.5f - 2.f * v_pad) + v_pad + 0.5f * v_face;

    uf *= width;
    vf *= height;

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (i = -1; i < 3; i++) {
        for (j = -1; j < 3; j++) {
            us[i + 1][j + 1] = av_clip(ui + j, 0, width  - 1);
            vs[i + 1][j + 1] = av_clip(vi + i, 0, height - 1);
        }
    }
}

/**
 * Prepare data for processing flat output format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_flat_out(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    const float h_angle = 0.5f * s->h_fov * M_PI / 180.f;
    const float v_angle = 0.5f * s->v_fov * M_PI / 180.f;

    const float sin_phi   = sinf(h_angle);
    const float cos_phi   = cosf(h_angle);
    const float sin_theta = sinf(v_angle);
    const float cos_theta = cosf(v_angle);

    s->flat_range[0] =  cos_theta * sin_phi;
    s->flat_range[1] =  sin_theta;
    s->flat_range[2] = -cos_theta * cos_phi;

    return 0;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in flat format.
 *
 * @param s filter context
 * @param i horizontal position on frame [0, height)
 * @param j vertical position on frame [0, width)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static void flat_to_xyz(const V360Context *s,
                        int i, int j, int width, int height,
                        float *vec)
{
    const float l_x =  s->flat_range[0] * (2.f * i / width  - 1.f);
    const float l_y = -s->flat_range[1] * (2.f * j / height - 1.f);
    const float l_z =  s->flat_range[2];

    const float norm = sqrtf(l_x * l_x + l_y * l_y + l_z * l_z);

    vec[0] = l_x / norm;
    vec[1] = l_y / norm;
    vec[2] = l_z / norm;
}

/**
 * Calculate frame position in dual fisheye format for corresponding 3D coordinates on sphere.
 *
 * @param s filter context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static void xyz_to_dfisheye(const V360Context *s,
                            const float *vec, int width, int height,
                            uint16_t us[4][4], uint16_t vs[4][4], float *du, float *dv)
{
    const float scale = 1.f - s->in_pad;

    const float ew = width / 2.f;
    const float eh = height;

    const float phi   = atan2f(-vec[1], -vec[0]);
    const float theta = acosf(fabsf(vec[2])) / M_PI;

    float uf = (theta * cosf(phi) * scale + 0.5f) * ew;
    float vf = (theta * sinf(phi) * scale + 0.5f) * eh;

    int ui, vi;
    int u_shift;
    int i, j;

    if (vec[2] >= 0) {
        u_shift = 0;
    } else {
        u_shift = ceilf(ew);
        uf = ew - uf;
    }

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (i = -1; i < 3; i++) {
        for (j = -1; j < 3; j++) {
            us[i + 1][j + 1] = av_clip(u_shift + ui + j, 0, width  - 1);
            vs[i + 1][j + 1] = av_clip(          vi + i, 0, height - 1);
        }
    }
}

/**
 * Calculate rotation matrix for yaw/pitch/roll angles.
 */
static inline void calculate_rotation_matrix(float yaw, float pitch, float roll,
                                             float rot_mat[3][3])
{
    const float yaw_rad   = yaw   * M_PI / 180.f;
    const float pitch_rad = pitch * M_PI / 180.f;
    const float roll_rad  = roll  * M_PI / 180.f;

    const float sin_yaw   = sinf(-yaw_rad);
    const float cos_yaw   = cosf(-yaw_rad);
    const float sin_pitch = sinf(pitch_rad);
    const float cos_pitch = cosf(pitch_rad);
    const float sin_roll  = sinf(roll_rad);
    const float cos_roll  = cosf(roll_rad);

    rot_mat[0][0] = sin_yaw * sin_pitch * sin_roll + cos_yaw * cos_roll;
    rot_mat[0][1] = sin_yaw * sin_pitch * cos_roll - cos_yaw * sin_roll;
    rot_mat[0][2] = sin_yaw * cos_pitch;

    rot_mat[1][0] = cos_pitch * sin_roll;
    rot_mat[1][1] = cos_pitch * cos_roll;
    rot_mat[1][2] = -sin_pitch;

    rot_mat[2][0] = cos_yaw * sin_pitch * sin_roll - sin_yaw * cos_roll;
    rot_mat[2][1] = cos_yaw * sin_pitch * cos_roll + sin_yaw * sin_roll;
    rot_mat[2][2] = cos_yaw * cos_pitch;
}

/**
 * Rotate vector with given rotation matrix.
 *
 * @param rot_mat rotation matrix
 * @param vec vector
 */
static inline void rotate(const float rot_mat[3][3],
                          float *vec)
{
    const float x_tmp = vec[0] * rot_mat[0][0] + vec[1] * rot_mat[0][1] + vec[2] * rot_mat[0][2];
    const float y_tmp = vec[0] * rot_mat[1][0] + vec[1] * rot_mat[1][1] + vec[2] * rot_mat[1][2];
    const float z_tmp = vec[0] * rot_mat[2][0] + vec[1] * rot_mat[2][1] + vec[2] * rot_mat[2][2];

    vec[0] = x_tmp;
    vec[1] = y_tmp;
    vec[2] = z_tmp;
}

static inline void set_mirror_modifier(int h_flip, int v_flip, int d_flip,
                                       float *modifier)
{
    modifier[0] = h_flip ? -1.f : 1.f;
    modifier[1] = v_flip ? -1.f : 1.f;
    modifier[2] = d_flip ? -1.f : 1.f;
}

static inline void mirror(const float *modifier,
                          float *vec)
{
    vec[0] *= modifier[0];
    vec[1] *= modifier[1];
    vec[2] *= modifier[2];
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    V360Context *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int depth = desc->comp[0].depth;
    float remap_data_size = 0.f;
    int sizeof_remap;
    int err;
    int p, h, w;
    float hf, wf;
    float mirror_modifier[3];
    void (*in_transform)(const V360Context *s,
                         const float *vec, int width, int height,
                         uint16_t us[4][4], uint16_t vs[4][4], float *du, float *dv);
    void (*out_transform)(const V360Context *s,
                          int i, int j, int width, int height,
                          float *vec);
    void (*calculate_kernel)(float du, float dv, int shift, const XYRemap4 *r_tmp, void *r);
    float rot_mat[3][3];

    switch (s->interp) {
    case NEAREST:
        calculate_kernel = nearest_kernel;
        s->remap_slice = depth <= 8 ? remap1_8bit_slice : remap1_16bit_slice;
        sizeof_remap = sizeof(XYRemap1);
        break;
    case BILINEAR:
        calculate_kernel = bilinear_kernel;
        s->remap_slice = depth <= 8 ? remap2_8bit_slice : remap2_16bit_slice;
        sizeof_remap = sizeof(XYRemap2);
        break;
    case BICUBIC:
        calculate_kernel = bicubic_kernel;
        s->remap_slice = depth <= 8 ? remap4_8bit_slice : remap4_16bit_slice;
        sizeof_remap = sizeof(XYRemap4);
        break;
    case LANCZOS:
        calculate_kernel = lanczos_kernel;
        s->remap_slice = depth <= 8 ? remap4_8bit_slice : remap4_16bit_slice;
        sizeof_remap = sizeof(XYRemap4);
        break;
    }

    switch (s->in) {
    case EQUIRECTANGULAR:
        in_transform = xyz_to_equirect;
        err = 0;
        wf = inlink->w;
        hf = inlink->h;
        break;
    case CUBEMAP_3_2:
        in_transform = xyz_to_cube3x2;
        err = prepare_cube_in(ctx);
        wf = inlink->w / 3.f * 4.f;
        hf = inlink->h;
        break;
    case CUBEMAP_6_1:
        in_transform = xyz_to_cube6x1;
        err = prepare_cube_in(ctx);
        wf = inlink->w / 3.f * 2.f;
        hf = inlink->h * 2.f;
        break;
    case EQUIANGULAR:
        in_transform = xyz_to_eac;
        err = prepare_eac_in(ctx);
        wf = inlink->w;
        hf = inlink->h / 9.f * 8.f;
        break;
    case FLAT:
        av_log(ctx, AV_LOG_ERROR, "Flat format is not accepted as input.\n");
        return AVERROR(EINVAL);
    case DUAL_FISHEYE:
        in_transform = xyz_to_dfisheye;
        err = 0;
        wf = inlink->w;
        hf = inlink->h;
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Specified input format is not handled.\n");
        return AVERROR_BUG;
    }

    if (err != 0) {
        return err;
    }

    switch (s->out) {
    case EQUIRECTANGULAR:
        out_transform = equirect_to_xyz;
        err = 0;
        w = roundf(wf);
        h = roundf(hf);
        break;
    case CUBEMAP_3_2:
        out_transform = cube3x2_to_xyz;
        err = prepare_cube_out(ctx);
        w = roundf(wf / 4.f * 3.f);
        h = roundf(hf);
        break;
    case CUBEMAP_6_1:
        out_transform = cube6x1_to_xyz;
        err = prepare_cube_out(ctx);
        w = roundf(wf / 2.f * 3.f);
        h = roundf(hf / 2.f);
        break;
    case EQUIANGULAR:
        out_transform = eac_to_xyz;
        err = prepare_eac_out(ctx);
        w = roundf(wf);
        h = roundf(hf / 8.f * 9.f);
        break;
    case FLAT:
        out_transform = flat_to_xyz;
        err = prepare_flat_out(ctx);
        w = roundf(wf * s->flat_range[0] / s->flat_range[1] / 2.f);
        h = roundf(hf);
        break;
    case DUAL_FISHEYE:
        av_log(ctx, AV_LOG_ERROR, "Dual fisheye format is not accepted as output.\n");
        return AVERROR(EINVAL);
    default:
        av_log(ctx, AV_LOG_ERROR, "Specified output format is not handled.\n");
        return AVERROR_BUG;
    }

    if (err != 0) {
        return err;
    }

    // Override resolution with user values if specified
    if (s->width > 0 && s->height > 0) {
        w = s->width;
        h = s->height;
    } else if (s->width > 0 || s->height > 0) {
        av_log(ctx, AV_LOG_ERROR, "Both width and height values should be specified.\n");
        return AVERROR(EINVAL);
    }

    s->planeheight[1] = s->planeheight[2] = FF_CEIL_RSHIFT(h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = h;
    s->planewidth[1]  = s->planewidth[2] = FF_CEIL_RSHIFT(w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3] = w;

    outlink->h = h;
    outlink->w = w;

    s->inplaneheight[1] = s->inplaneheight[2] = FF_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->inplaneheight[0] = s->inplaneheight[3] = inlink->h;
    s->inplanewidth[1]  = s->inplanewidth[2]  = FF_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->inplanewidth[0]  = s->inplanewidth[3]  = inlink->w;
    s->nb_planes = av_pix_fmt_count_planes(inlink->format);

    for (p = 0; p < s->nb_planes; p++) {
        remap_data_size += (float)s->planewidth[p] * s->planeheight[p] * sizeof_remap;
    }

    for (p = 0; p < s->nb_planes; p++) {
        s->remap[p] = av_calloc(s->planewidth[p] * s->planeheight[p], sizeof_remap);
        if (!s->remap[p]) {
            av_log(ctx, AV_LOG_ERROR,
                   "Not enough memory to allocate remap data. Need at least %.3f GiB.\n",
                   remap_data_size / (1024 * 1024 * 1024));
            return AVERROR(ENOMEM);
        }
    }

    calculate_rotation_matrix(s->yaw, s->pitch, s->roll, rot_mat);
    set_mirror_modifier(s->h_flip, s->v_flip, s->d_flip, mirror_modifier);

    // Calculate remap data
    for (p = 0; p < s->nb_planes; p++) {
        const int width = s->planewidth[p];
        const int height = s->planeheight[p];
        const int in_width = s->inplanewidth[p];
        const int in_height = s->inplaneheight[p];
        void *r = s->remap[p];
        float du, dv;
        float vec[3];
        XYRemap4 r_tmp;
        int i, j;

        for (i = 0; i < width; i++) {
            for (j = 0; j < height; j++) {
                out_transform(s, i, j, width, height, vec);
                rotate(rot_mat, vec);
                mirror(mirror_modifier, vec);
                in_transform(s, vec, in_width, in_height, r_tmp.u, r_tmp.v, &du, &dv);
                calculate_kernel(du, dv, j * width + i, &r_tmp, r);
            }
        }
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    V360Context *s = ctx->priv;
    AVFrame *out;
    ThreadData td;

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    td.s = s;
    td.in = in;
    td.out = out;
    td.nb_planes = s->nb_planes;

    ctx->internal->execute(ctx, s->remap_slice, &td, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;
    int p;

    for (p = 0; p < s->nb_planes; p++)
        av_freep(&s->remap[p]);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_vf_v360 = {
    .name          = "v360",
    .description   = NULL_IF_CONFIG_SMALL("Convert 360 projection of video."),
    .priv_size     = sizeof(V360Context),
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = inputs,
    .outputs       = outputs,
    .priv_class    = &v360_class,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
