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

#include <math.h>

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/pixdesc.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "v360.h"

typedef struct ThreadData {
    AVFrame *in;
    AVFrame *out;
} ThreadData;

#define OFFSET(x) offsetof(V360Context, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
#define TFLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption v360_options[] = {
    {     "input", "set input projection",              OFFSET(in), AV_OPT_TYPE_INT,    {.i64=EQUIRECTANGULAR}, 0,    NB_PROJECTIONS-1, FLAGS, "in" },
    {         "e", "equirectangular",                            0, AV_OPT_TYPE_CONST,  {.i64=EQUIRECTANGULAR}, 0,                   0, FLAGS, "in" },
    {  "equirect", "equirectangular",                            0, AV_OPT_TYPE_CONST,  {.i64=EQUIRECTANGULAR}, 0,                   0, FLAGS, "in" },
    {      "c3x2", "cubemap 3x2",                                0, AV_OPT_TYPE_CONST,  {.i64=CUBEMAP_3_2},     0,                   0, FLAGS, "in" },
    {      "c6x1", "cubemap 6x1",                                0, AV_OPT_TYPE_CONST,  {.i64=CUBEMAP_6_1},     0,                   0, FLAGS, "in" },
    {       "eac", "equi-angular cubemap",                       0, AV_OPT_TYPE_CONST,  {.i64=EQUIANGULAR},     0,                   0, FLAGS, "in" },
    {  "dfisheye", "dual fisheye",                               0, AV_OPT_TYPE_CONST,  {.i64=DUAL_FISHEYE},    0,                   0, FLAGS, "in" },
    {      "flat", "regular video",                              0, AV_OPT_TYPE_CONST,  {.i64=FLAT},            0,                   0, FLAGS, "in" },
    {"rectilinear", "regular video",                             0, AV_OPT_TYPE_CONST,  {.i64=FLAT},            0,                   0, FLAGS, "in" },
    {  "gnomonic", "regular video",                              0, AV_OPT_TYPE_CONST,  {.i64=FLAT},            0,                   0, FLAGS, "in" },
    {    "barrel", "barrel facebook's 360 format",               0, AV_OPT_TYPE_CONST,  {.i64=BARREL},          0,                   0, FLAGS, "in" },
    {        "fb", "barrel facebook's 360 format",               0, AV_OPT_TYPE_CONST,  {.i64=BARREL},          0,                   0, FLAGS, "in" },
    {      "c1x6", "cubemap 1x6",                                0, AV_OPT_TYPE_CONST,  {.i64=CUBEMAP_1_6},     0,                   0, FLAGS, "in" },
    {        "sg", "stereographic",                              0, AV_OPT_TYPE_CONST,  {.i64=STEREOGRAPHIC},   0,                   0, FLAGS, "in" },
    {  "mercator", "mercator",                                   0, AV_OPT_TYPE_CONST,  {.i64=MERCATOR},        0,                   0, FLAGS, "in" },
    {      "ball", "ball",                                       0, AV_OPT_TYPE_CONST,  {.i64=BALL},            0,                   0, FLAGS, "in" },
    {    "hammer", "hammer",                                     0, AV_OPT_TYPE_CONST,  {.i64=HAMMER},          0,                   0, FLAGS, "in" },
    {"sinusoidal", "sinusoidal",                                 0, AV_OPT_TYPE_CONST,  {.i64=SINUSOIDAL},      0,                   0, FLAGS, "in" },
    {   "fisheye", "fisheye",                                    0, AV_OPT_TYPE_CONST,  {.i64=FISHEYE},         0,                   0, FLAGS, "in" },
    {   "pannini", "pannini",                                    0, AV_OPT_TYPE_CONST,  {.i64=PANNINI},         0,                   0, FLAGS, "in" },
    {"cylindrical", "cylindrical",                               0, AV_OPT_TYPE_CONST,  {.i64=CYLINDRICAL},     0,                   0, FLAGS, "in" },
    {"tetrahedron", "tetrahedron",                               0, AV_OPT_TYPE_CONST,  {.i64=TETRAHEDRON},     0,                   0, FLAGS, "in" },
    {"barrelsplit", "barrel split facebook's 360 format",        0, AV_OPT_TYPE_CONST,  {.i64=BARREL_SPLIT},    0,                   0, FLAGS, "in" },
    {       "tsp", "truncated square pyramid",                   0, AV_OPT_TYPE_CONST,  {.i64=TSPYRAMID},       0,                   0, FLAGS, "in" },
    { "hequirect", "half equirectangular",                       0, AV_OPT_TYPE_CONST,  {.i64=HEQUIRECTANGULAR},0,                   0, FLAGS, "in" },
    {        "he", "half equirectangular",                       0, AV_OPT_TYPE_CONST,  {.i64=HEQUIRECTANGULAR},0,                   0, FLAGS, "in" },
    { "equisolid", "equisolid",                                  0, AV_OPT_TYPE_CONST,  {.i64=EQUISOLID},       0,                   0, FLAGS, "in" },
    {        "og", "orthographic",                               0, AV_OPT_TYPE_CONST,  {.i64=ORTHOGRAPHIC},    0,                   0, FLAGS, "in" },
    {    "output", "set output projection",            OFFSET(out), AV_OPT_TYPE_INT,    {.i64=CUBEMAP_3_2},     0,    NB_PROJECTIONS-1, FLAGS, "out" },
    {         "e", "equirectangular",                            0, AV_OPT_TYPE_CONST,  {.i64=EQUIRECTANGULAR}, 0,                   0, FLAGS, "out" },
    {  "equirect", "equirectangular",                            0, AV_OPT_TYPE_CONST,  {.i64=EQUIRECTANGULAR}, 0,                   0, FLAGS, "out" },
    {      "c3x2", "cubemap 3x2",                                0, AV_OPT_TYPE_CONST,  {.i64=CUBEMAP_3_2},     0,                   0, FLAGS, "out" },
    {      "c6x1", "cubemap 6x1",                                0, AV_OPT_TYPE_CONST,  {.i64=CUBEMAP_6_1},     0,                   0, FLAGS, "out" },
    {       "eac", "equi-angular cubemap",                       0, AV_OPT_TYPE_CONST,  {.i64=EQUIANGULAR},     0,                   0, FLAGS, "out" },
    {  "dfisheye", "dual fisheye",                               0, AV_OPT_TYPE_CONST,  {.i64=DUAL_FISHEYE},    0,                   0, FLAGS, "out" },
    {      "flat", "regular video",                              0, AV_OPT_TYPE_CONST,  {.i64=FLAT},            0,                   0, FLAGS, "out" },
    {"rectilinear", "regular video",                             0, AV_OPT_TYPE_CONST,  {.i64=FLAT},            0,                   0, FLAGS, "out" },
    {  "gnomonic", "regular video",                              0, AV_OPT_TYPE_CONST,  {.i64=FLAT},            0,                   0, FLAGS, "out" },
    {    "barrel", "barrel facebook's 360 format",               0, AV_OPT_TYPE_CONST,  {.i64=BARREL},          0,                   0, FLAGS, "out" },
    {        "fb", "barrel facebook's 360 format",               0, AV_OPT_TYPE_CONST,  {.i64=BARREL},          0,                   0, FLAGS, "out" },
    {      "c1x6", "cubemap 1x6",                                0, AV_OPT_TYPE_CONST,  {.i64=CUBEMAP_1_6},     0,                   0, FLAGS, "out" },
    {        "sg", "stereographic",                              0, AV_OPT_TYPE_CONST,  {.i64=STEREOGRAPHIC},   0,                   0, FLAGS, "out" },
    {  "mercator", "mercator",                                   0, AV_OPT_TYPE_CONST,  {.i64=MERCATOR},        0,                   0, FLAGS, "out" },
    {      "ball", "ball",                                       0, AV_OPT_TYPE_CONST,  {.i64=BALL},            0,                   0, FLAGS, "out" },
    {    "hammer", "hammer",                                     0, AV_OPT_TYPE_CONST,  {.i64=HAMMER},          0,                   0, FLAGS, "out" },
    {"sinusoidal", "sinusoidal",                                 0, AV_OPT_TYPE_CONST,  {.i64=SINUSOIDAL},      0,                   0, FLAGS, "out" },
    {   "fisheye", "fisheye",                                    0, AV_OPT_TYPE_CONST,  {.i64=FISHEYE},         0,                   0, FLAGS, "out" },
    {   "pannini", "pannini",                                    0, AV_OPT_TYPE_CONST,  {.i64=PANNINI},         0,                   0, FLAGS, "out" },
    {"cylindrical", "cylindrical",                               0, AV_OPT_TYPE_CONST,  {.i64=CYLINDRICAL},     0,                   0, FLAGS, "out" },
    {"perspective", "perspective",                               0, AV_OPT_TYPE_CONST,  {.i64=PERSPECTIVE},     0,                   0, FLAGS, "out" },
    {"tetrahedron", "tetrahedron",                               0, AV_OPT_TYPE_CONST,  {.i64=TETRAHEDRON},     0,                   0, FLAGS, "out" },
    {"barrelsplit", "barrel split facebook's 360 format",        0, AV_OPT_TYPE_CONST,  {.i64=BARREL_SPLIT},    0,                   0, FLAGS, "out" },
    {       "tsp", "truncated square pyramid",                   0, AV_OPT_TYPE_CONST,  {.i64=TSPYRAMID},       0,                   0, FLAGS, "out" },
    { "hequirect", "half equirectangular",                       0, AV_OPT_TYPE_CONST,  {.i64=HEQUIRECTANGULAR},0,                   0, FLAGS, "out" },
    {        "he", "half equirectangular",                       0, AV_OPT_TYPE_CONST,  {.i64=HEQUIRECTANGULAR},0,                   0, FLAGS, "out" },
    { "equisolid", "equisolid",                                  0, AV_OPT_TYPE_CONST,  {.i64=EQUISOLID},       0,                   0, FLAGS, "out" },
    {        "og", "orthographic",                               0, AV_OPT_TYPE_CONST,  {.i64=ORTHOGRAPHIC},    0,                   0, FLAGS, "out" },
    {    "interp", "set interpolation method",      OFFSET(interp), AV_OPT_TYPE_INT,    {.i64=BILINEAR},        0, NB_INTERP_METHODS-1, FLAGS, "interp" },
    {      "near", "nearest neighbour",                          0, AV_OPT_TYPE_CONST,  {.i64=NEAREST},         0,                   0, FLAGS, "interp" },
    {   "nearest", "nearest neighbour",                          0, AV_OPT_TYPE_CONST,  {.i64=NEAREST},         0,                   0, FLAGS, "interp" },
    {      "line", "bilinear interpolation",                     0, AV_OPT_TYPE_CONST,  {.i64=BILINEAR},        0,                   0, FLAGS, "interp" },
    {    "linear", "bilinear interpolation",                     0, AV_OPT_TYPE_CONST,  {.i64=BILINEAR},        0,                   0, FLAGS, "interp" },
    { "lagrange9", "lagrange9 interpolation",                    0, AV_OPT_TYPE_CONST,  {.i64=LAGRANGE9},       0,                   0, FLAGS, "interp" },
    {      "cube", "bicubic interpolation",                      0, AV_OPT_TYPE_CONST,  {.i64=BICUBIC},         0,                   0, FLAGS, "interp" },
    {     "cubic", "bicubic interpolation",                      0, AV_OPT_TYPE_CONST,  {.i64=BICUBIC},         0,                   0, FLAGS, "interp" },
    {      "lanc", "lanczos interpolation",                      0, AV_OPT_TYPE_CONST,  {.i64=LANCZOS},         0,                   0, FLAGS, "interp" },
    {   "lanczos", "lanczos interpolation",                      0, AV_OPT_TYPE_CONST,  {.i64=LANCZOS},         0,                   0, FLAGS, "interp" },
    {      "sp16", "spline16 interpolation",                     0, AV_OPT_TYPE_CONST,  {.i64=SPLINE16},        0,                   0, FLAGS, "interp" },
    {  "spline16", "spline16 interpolation",                     0, AV_OPT_TYPE_CONST,  {.i64=SPLINE16},        0,                   0, FLAGS, "interp" },
    {     "gauss", "gaussian interpolation",                     0, AV_OPT_TYPE_CONST,  {.i64=GAUSSIAN},        0,                   0, FLAGS, "interp" },
    {  "gaussian", "gaussian interpolation",                     0, AV_OPT_TYPE_CONST,  {.i64=GAUSSIAN},        0,                   0, FLAGS, "interp" },
    {         "w", "output width",                   OFFSET(width), AV_OPT_TYPE_INT,    {.i64=0},               0,           INT16_MAX, FLAGS, "w"},
    {         "h", "output height",                 OFFSET(height), AV_OPT_TYPE_INT,    {.i64=0},               0,           INT16_MAX, FLAGS, "h"},
    { "in_stereo", "input stereo format",        OFFSET(in_stereo), AV_OPT_TYPE_INT,    {.i64=STEREO_2D},       0,    NB_STEREO_FMTS-1, FLAGS, "stereo" },
    {"out_stereo", "output stereo format",      OFFSET(out_stereo), AV_OPT_TYPE_INT,    {.i64=STEREO_2D},       0,    NB_STEREO_FMTS-1, FLAGS, "stereo" },
    {        "2d", "2d mono",                                    0, AV_OPT_TYPE_CONST,  {.i64=STEREO_2D},       0,                   0, FLAGS, "stereo" },
    {       "sbs", "side by side",                               0, AV_OPT_TYPE_CONST,  {.i64=STEREO_SBS},      0,                   0, FLAGS, "stereo" },
    {        "tb", "top bottom",                                 0, AV_OPT_TYPE_CONST,  {.i64=STEREO_TB},       0,                   0, FLAGS, "stereo" },
    { "in_forder", "input cubemap face order",   OFFSET(in_forder), AV_OPT_TYPE_STRING, {.str="rludfb"},        0,     NB_DIRECTIONS-1, FLAGS, "in_forder"},
    {"out_forder", "output cubemap face order", OFFSET(out_forder), AV_OPT_TYPE_STRING, {.str="rludfb"},        0,     NB_DIRECTIONS-1, FLAGS, "out_forder"},
    {   "in_frot", "input cubemap face rotation",  OFFSET(in_frot), AV_OPT_TYPE_STRING, {.str="000000"},        0,     NB_DIRECTIONS-1, FLAGS, "in_frot"},
    {  "out_frot", "output cubemap face rotation",OFFSET(out_frot), AV_OPT_TYPE_STRING, {.str="000000"},        0,     NB_DIRECTIONS-1, FLAGS, "out_frot"},
    {    "in_pad", "percent input cubemap pads",    OFFSET(in_pad), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},           0.f,                 0.1,TFLAGS, "in_pad"},
    {   "out_pad", "percent output cubemap pads",  OFFSET(out_pad), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},           0.f,                 0.1,TFLAGS, "out_pad"},
    {   "fin_pad", "fixed input cubemap pads",     OFFSET(fin_pad), AV_OPT_TYPE_INT,    {.i64=0},               0,                 100,TFLAGS, "fin_pad"},
    {  "fout_pad", "fixed output cubemap pads",   OFFSET(fout_pad), AV_OPT_TYPE_INT,    {.i64=0},               0,                 100,TFLAGS, "fout_pad"},
    {       "yaw", "yaw rotation",                     OFFSET(yaw), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},        -180.f,               180.f,TFLAGS, "yaw"},
    {     "pitch", "pitch rotation",                 OFFSET(pitch), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},        -180.f,               180.f,TFLAGS, "pitch"},
    {      "roll", "roll rotation",                   OFFSET(roll), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},        -180.f,               180.f,TFLAGS, "roll"},
    {    "rorder", "rotation order",                OFFSET(rorder), AV_OPT_TYPE_STRING, {.str="ypr"},           0,                   0,TFLAGS, "rorder"},
    {     "h_fov", "output horizontal field of view",OFFSET(h_fov), AV_OPT_TYPE_FLOAT,  {.dbl=90.f},     0.00001f,               360.f,TFLAGS, "h_fov"},
    {     "v_fov", "output vertical field of view",  OFFSET(v_fov), AV_OPT_TYPE_FLOAT,  {.dbl=45.f},     0.00001f,               360.f,TFLAGS, "v_fov"},
    {     "d_fov", "output diagonal field of view",  OFFSET(d_fov), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},           0.f,               360.f,TFLAGS, "d_fov"},
    {    "h_flip", "flip out video horizontally",   OFFSET(h_flip), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1,TFLAGS, "h_flip"},
    {    "v_flip", "flip out video vertically",     OFFSET(v_flip), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1,TFLAGS, "v_flip"},
    {    "d_flip", "flip out video indepth",        OFFSET(d_flip), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1,TFLAGS, "d_flip"},
    {   "ih_flip", "flip in video horizontally",   OFFSET(ih_flip), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1,TFLAGS, "ih_flip"},
    {   "iv_flip", "flip in video vertically",     OFFSET(iv_flip), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1,TFLAGS, "iv_flip"},
    {  "in_trans", "transpose video input",   OFFSET(in_transpose), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1, FLAGS, "in_transpose"},
    { "out_trans", "transpose video output", OFFSET(out_transpose), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1, FLAGS, "out_transpose"},
    {    "ih_fov", "input horizontal field of view",OFFSET(ih_fov), AV_OPT_TYPE_FLOAT,  {.dbl=90.f},     0.00001f,               360.f,TFLAGS, "ih_fov"},
    {    "iv_fov", "input vertical field of view",  OFFSET(iv_fov), AV_OPT_TYPE_FLOAT,  {.dbl=45.f},     0.00001f,               360.f,TFLAGS, "iv_fov"},
    {    "id_fov", "input diagonal field of view",  OFFSET(id_fov), AV_OPT_TYPE_FLOAT,  {.dbl=0.f},           0.f,               360.f,TFLAGS, "id_fov"},
    {"alpha_mask", "build mask in alpha plane",      OFFSET(alpha), AV_OPT_TYPE_BOOL,   {.i64=0},               0,                   1, FLAGS, "alpha"},
    { NULL }
};

AVFILTER_DEFINE_CLASS(v360);

static int query_formats(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;
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
    static const enum AVPixelFormat alpha_pix_fmts[] = {
        AV_PIX_FMT_YUVA444P,   AV_PIX_FMT_YUVA444P9,
        AV_PIX_FMT_YUVA444P10, AV_PIX_FMT_YUVA444P12,
        AV_PIX_FMT_YUVA444P16,
        AV_PIX_FMT_YUVA422P,   AV_PIX_FMT_YUVA422P9,
        AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA422P12,
        AV_PIX_FMT_YUVA422P16,
        AV_PIX_FMT_YUVA420P,   AV_PIX_FMT_YUVA420P9,
        AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA420P16,
        AV_PIX_FMT_GBRAP,   AV_PIX_FMT_GBRAP10,
        AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(s->alpha ? alpha_pix_fmts : pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

#define DEFINE_REMAP1_LINE(bits, div)                                                    \
static void remap1_##bits##bit_line_c(uint8_t *dst, int width, const uint8_t *const src, \
                                      ptrdiff_t in_linesize,                             \
                                      const int16_t *const u, const int16_t *const v,    \
                                      const int16_t *const ker)                          \
{                                                                                        \
    const uint##bits##_t *const s = (const uint##bits##_t *const)src;                    \
    uint##bits##_t *d = (uint##bits##_t *)dst;                                           \
                                                                                         \
    in_linesize /= div;                                                                  \
                                                                                         \
    for (int x = 0; x < width; x++)                                                      \
        d[x] = s[v[x] * in_linesize + u[x]];                                             \
}

DEFINE_REMAP1_LINE( 8, 1)
DEFINE_REMAP1_LINE(16, 2)

/**
 * Generate remapping function with a given window size and pixel depth.
 *
 * @param ws size of interpolation window
 * @param bits number of bits per pixel
 */
#define DEFINE_REMAP(ws, bits)                                                                             \
static int remap##ws##_##bits##bit_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)          \
{                                                                                                          \
    ThreadData *td = arg;                                                                                  \
    const V360Context *s = ctx->priv;                                                                      \
    const AVFrame *in = td->in;                                                                            \
    AVFrame *out = td->out;                                                                                \
                                                                                                           \
    for (int stereo = 0; stereo < 1 + s->out_stereo > STEREO_2D; stereo++) {                               \
        for (int plane = 0; plane < s->nb_planes; plane++) {                                               \
            const unsigned map = s->map[plane];                                                            \
            const int in_linesize  = in->linesize[plane];                                                  \
            const int out_linesize = out->linesize[plane];                                                 \
            const int uv_linesize = s->uv_linesize[plane];                                                 \
            const int in_offset_w = stereo ? s->in_offset_w[plane] : 0;                                    \
            const int in_offset_h = stereo ? s->in_offset_h[plane] : 0;                                    \
            const int out_offset_w = stereo ? s->out_offset_w[plane] : 0;                                  \
            const int out_offset_h = stereo ? s->out_offset_h[plane] : 0;                                  \
            const uint8_t *const src = in->data[plane] +                                                   \
                                                   in_offset_h * in_linesize + in_offset_w * (bits >> 3);  \
            uint8_t *dst = out->data[plane] + out_offset_h * out_linesize + out_offset_w * (bits >> 3);    \
            const uint8_t *mask = plane == 3 ? s->mask : NULL;                                             \
            const int width = s->pr_width[plane];                                                          \
            const int height = s->pr_height[plane];                                                        \
                                                                                                           \
            const int slice_start = (height *  jobnr     ) / nb_jobs;                                      \
            const int slice_end   = (height * (jobnr + 1)) / nb_jobs;                                      \
                                                                                                           \
            for (int y = slice_start; y < slice_end && !mask; y++) {                                       \
                const int16_t *const u = s->u[map] + y * uv_linesize * ws * ws;                            \
                const int16_t *const v = s->v[map] + y * uv_linesize * ws * ws;                            \
                const int16_t *const ker = s->ker[map] + y * uv_linesize * ws * ws;                        \
                                                                                                           \
                s->remap_line(dst + y * out_linesize, width, src, in_linesize, u, v, ker);                 \
            }                                                                                              \
                                                                                                           \
            for (int y = slice_start; y < slice_end && mask; y++) {                                        \
                memcpy(dst + y * out_linesize, mask + y * width * (bits >> 3), width * (bits >> 3));       \
            }                                                                                              \
        }                                                                                                  \
    }                                                                                                      \
                                                                                                           \
    return 0;                                                                                              \
}

DEFINE_REMAP(1,  8)
DEFINE_REMAP(2,  8)
DEFINE_REMAP(3,  8)
DEFINE_REMAP(4,  8)
DEFINE_REMAP(1, 16)
DEFINE_REMAP(2, 16)
DEFINE_REMAP(3, 16)
DEFINE_REMAP(4, 16)

#define DEFINE_REMAP_LINE(ws, bits, div)                                                      \
static void remap##ws##_##bits##bit_line_c(uint8_t *dst, int width, const uint8_t *const src, \
                                           ptrdiff_t in_linesize,                             \
                                           const int16_t *const u, const int16_t *const v,    \
                                           const int16_t *const ker)                          \
{                                                                                             \
    const uint##bits##_t *const s = (const uint##bits##_t *const)src;                         \
    uint##bits##_t *d = (uint##bits##_t *)dst;                                                \
                                                                                              \
    in_linesize /= div;                                                                       \
                                                                                              \
    for (int x = 0; x < width; x++) {                                                         \
        const int16_t *const uu = u + x * ws * ws;                                            \
        const int16_t *const vv = v + x * ws * ws;                                            \
        const int16_t *const kker = ker + x * ws * ws;                                        \
        int tmp = 0;                                                                          \
                                                                                              \
        for (int i = 0; i < ws; i++) {                                                        \
            for (int j = 0; j < ws; j++) {                                                    \
                tmp += kker[i * ws + j] * s[vv[i * ws + j] * in_linesize + uu[i * ws + j]];   \
            }                                                                                 \
        }                                                                                     \
                                                                                              \
        d[x] = av_clip_uint##bits(tmp >> 14);                                                 \
    }                                                                                         \
}

DEFINE_REMAP_LINE(2,  8, 1)
DEFINE_REMAP_LINE(3,  8, 1)
DEFINE_REMAP_LINE(4,  8, 1)
DEFINE_REMAP_LINE(2, 16, 2)
DEFINE_REMAP_LINE(3, 16, 2)
DEFINE_REMAP_LINE(4, 16, 2)

void ff_v360_init(V360Context *s, int depth)
{
    switch (s->interp) {
    case NEAREST:
        s->remap_line = depth <= 8 ? remap1_8bit_line_c : remap1_16bit_line_c;
        break;
    case BILINEAR:
        s->remap_line = depth <= 8 ? remap2_8bit_line_c : remap2_16bit_line_c;
        break;
    case LAGRANGE9:
        s->remap_line = depth <= 8 ? remap3_8bit_line_c : remap3_16bit_line_c;
        break;
    case BICUBIC:
    case LANCZOS:
    case SPLINE16:
    case GAUSSIAN:
        s->remap_line = depth <= 8 ? remap4_8bit_line_c : remap4_16bit_line_c;
        break;
    }

    if (ARCH_X86)
        ff_v360_init_x86(s, depth);
}

/**
 * Save nearest pixel coordinates for remapping.
 *
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 * @param rmap calculated 4x4 window
 * @param u u remap data
 * @param v v remap data
 * @param ker ker remap data
 */
static void nearest_kernel(float du, float dv, const XYRemap *rmap,
                           int16_t *u, int16_t *v, int16_t *ker)
{
    const int i = lrintf(dv) + 1;
    const int j = lrintf(du) + 1;

    u[0] = rmap->u[i][j];
    v[0] = rmap->v[i][j];
}

/**
 * Calculate kernel for bilinear interpolation.
 *
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 * @param rmap calculated 4x4 window
 * @param u u remap data
 * @param v v remap data
 * @param ker ker remap data
 */
static void bilinear_kernel(float du, float dv, const XYRemap *rmap,
                            int16_t *u, int16_t *v, int16_t *ker)
{
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            u[i * 2 + j] = rmap->u[i + 1][j + 1];
            v[i * 2 + j] = rmap->v[i + 1][j + 1];
        }
    }

    ker[0] = lrintf((1.f - du) * (1.f - dv) * 16385.f);
    ker[1] = lrintf(       du  * (1.f - dv) * 16385.f);
    ker[2] = lrintf((1.f - du) *        dv  * 16385.f);
    ker[3] = lrintf(       du  *        dv  * 16385.f);
}

/**
 * Calculate 1-dimensional lagrange coefficients.
 *
 * @param t relative coordinate
 * @param coeffs coefficients
 */
static inline void calculate_lagrange_coeffs(float t, float *coeffs)
{
    coeffs[0] = (t - 1.f) * (t - 2.f) * 0.5f;
    coeffs[1] = -t * (t - 2.f);
    coeffs[2] =  t * (t - 1.f) * 0.5f;
}

/**
 * Calculate kernel for lagrange interpolation.
 *
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 * @param rmap calculated 4x4 window
 * @param u u remap data
 * @param v v remap data
 * @param ker ker remap data
 */
static void lagrange_kernel(float du, float dv, const XYRemap *rmap,
                            int16_t *u, int16_t *v, int16_t *ker)
{
    float du_coeffs[3];
    float dv_coeffs[3];

    calculate_lagrange_coeffs(du, du_coeffs);
    calculate_lagrange_coeffs(dv, dv_coeffs);

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            u[i * 3 + j] = rmap->u[i + 1][j + 1];
            v[i * 3 + j] = rmap->v[i + 1][j + 1];
            ker[i * 3 + j] = lrintf(du_coeffs[j] * dv_coeffs[i] * 16385.f);
        }
    }
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
 * @param rmap calculated 4x4 window
 * @param u u remap data
 * @param v v remap data
 * @param ker ker remap data
 */
static void bicubic_kernel(float du, float dv, const XYRemap *rmap,
                           int16_t *u, int16_t *v, int16_t *ker)
{
    float du_coeffs[4];
    float dv_coeffs[4];

    calculate_bicubic_coeffs(du, du_coeffs);
    calculate_bicubic_coeffs(dv, dv_coeffs);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            u[i * 4 + j] = rmap->u[i][j];
            v[i * 4 + j] = rmap->v[i][j];
            ker[i * 4 + j] = lrintf(du_coeffs[j] * dv_coeffs[i] * 16385.f);
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
    float sum = 0.f;

    for (int i = 0; i < 4; i++) {
        const float x = M_PI * (t - i + 1);
        if (x == 0.f) {
            coeffs[i] = 1.f;
        } else {
            coeffs[i] = sinf(x) * sinf(x / 2.f) / (x * x / 2.f);
        }
        sum += coeffs[i];
    }

    for (int i = 0; i < 4; i++) {
        coeffs[i] /= sum;
    }
}

/**
 * Calculate kernel for lanczos interpolation.
 *
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 * @param rmap calculated 4x4 window
 * @param u u remap data
 * @param v v remap data
 * @param ker ker remap data
 */
static void lanczos_kernel(float du, float dv, const XYRemap *rmap,
                           int16_t *u, int16_t *v, int16_t *ker)
{
    float du_coeffs[4];
    float dv_coeffs[4];

    calculate_lanczos_coeffs(du, du_coeffs);
    calculate_lanczos_coeffs(dv, dv_coeffs);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            u[i * 4 + j] = rmap->u[i][j];
            v[i * 4 + j] = rmap->v[i][j];
            ker[i * 4 + j] = lrintf(du_coeffs[j] * dv_coeffs[i] * 16385.f);
        }
    }
}

/**
 * Calculate 1-dimensional spline16 coefficients.
 *
 * @param t relative coordinate
 * @param coeffs coefficients
 */
static void calculate_spline16_coeffs(float t, float *coeffs)
{
    coeffs[0] = ((-1.f / 3.f * t + 0.8f) * t - 7.f / 15.f) * t;
    coeffs[1] = ((t - 9.f / 5.f) * t - 0.2f) * t + 1.f;
    coeffs[2] = ((6.f / 5.f - t) * t + 0.8f) * t;
    coeffs[3] = ((1.f / 3.f * t - 0.2f) * t - 2.f / 15.f) * t;
}

/**
 * Calculate kernel for spline16 interpolation.
 *
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 * @param rmap calculated 4x4 window
 * @param u u remap data
 * @param v v remap data
 * @param ker ker remap data
 */
static void spline16_kernel(float du, float dv, const XYRemap *rmap,
                            int16_t *u, int16_t *v, int16_t *ker)
{
    float du_coeffs[4];
    float dv_coeffs[4];

    calculate_spline16_coeffs(du, du_coeffs);
    calculate_spline16_coeffs(dv, dv_coeffs);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            u[i * 4 + j] = rmap->u[i][j];
            v[i * 4 + j] = rmap->v[i][j];
            ker[i * 4 + j] = lrintf(du_coeffs[j] * dv_coeffs[i] * 16385.f);
        }
    }
}

/**
 * Calculate 1-dimensional gaussian coefficients.
 *
 * @param t relative coordinate
 * @param coeffs coefficients
 */
static void calculate_gaussian_coeffs(float t, float *coeffs)
{
    float sum = 0.f;

    for (int i = 0; i < 4; i++) {
        const float x = t - (i - 1);
        if (x == 0.f) {
            coeffs[i] = 1.f;
        } else {
            coeffs[i] = expf(-2.f * x * x) * expf(-x * x / 2.f);
        }
        sum += coeffs[i];
    }

    for (int i = 0; i < 4; i++) {
        coeffs[i] /= sum;
    }
}

/**
 * Calculate kernel for gaussian interpolation.
 *
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 * @param rmap calculated 4x4 window
 * @param u u remap data
 * @param v v remap data
 * @param ker ker remap data
 */
static void gaussian_kernel(float du, float dv, const XYRemap *rmap,
                            int16_t *u, int16_t *v, int16_t *ker)
{
    float du_coeffs[4];
    float dv_coeffs[4];

    calculate_gaussian_coeffs(du, du_coeffs);
    calculate_gaussian_coeffs(dv, dv_coeffs);

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            u[i * 4 + j] = rmap->u[i][j];
            v[i * 4 + j] = rmap->v[i][j];
            ker[i * 4 + j] = lrintf(du_coeffs[j] * dv_coeffs[i] * 16385.f);
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
 * Reflect y operation.
 *
 * @param y input vertical position
 * @param h input height
 */
static inline int reflecty(int y, int h)
{
    if (y < 0) {
        return -y;
    } else if (y >= h) {
        return 2 * h - 1 - y;
    }

    return y;
}

/**
 * Reflect x operation for equirect.
 *
 * @param x input horizontal position
 * @param y input vertical position
 * @param w input width
 * @param h input height
 */
static inline int ereflectx(int x, int y, int w, int h)
{
    if (y < 0 || y >= h)
        x += w / 2;

    return mod(x, w);
}

/**
 * Reflect x operation.
 *
 * @param x input horizontal position
 * @param y input vertical position
 * @param w input width
 * @param h input height
 */
static inline int reflectx(int x, int y, int w, int h)
{
    if (y < 0 || y >= h)
        return w - 1 - x;

    return mod(x, w);
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
 * Convert char to corresponding rotation order.
 */
static int get_rorder(char c)
{
    switch (c) {
    case 'Y':
    case 'y':
        return YAW;
    case 'P':
    case 'p':
        return PITCH;
    case 'R':
    case 'r':
        return ROLL;
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
    default:
        av_assert0(0);
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
    default:
        av_assert0(0);
    }
}

/**
 * Normalize vector.
 *
 * @param vec vector
 */
static void normalize_vector(float *vec)
{
    const float norm = sqrtf(vec[0] * vec[0] + vec[1] * vec[1] + vec[2] * vec[2]);

    vec[0] /= norm;
    vec[1] /= norm;
    vec[2] /= norm;
}

/**
 * Calculate 3D coordinates on sphere for corresponding cubemap position.
 * Common operation for every cubemap.
 *
 * @param s filter private context
 * @param uf horizontal cubemap coordinate [0, 1)
 * @param vf vertical cubemap coordinate [0, 1)
 * @param face face of cubemap
 * @param vec coordinates on sphere
 * @param scalew scale for uf
 * @param scaleh scale for vf
 */
static void cube_to_xyz(const V360Context *s,
                        float uf, float vf, int face,
                        float *vec, float scalew, float scaleh)
{
    const int direction = s->out_cubemap_direction_order[face];
    float l_x, l_y, l_z;

    uf /= scalew;
    vf /= scaleh;

    rotate_cube_face_inverse(&uf, &vf, s->out_cubemap_face_rotation[face]);

    switch (direction) {
    case RIGHT:
        l_x =  1.f;
        l_y =  vf;
        l_z = -uf;
        break;
    case LEFT:
        l_x = -1.f;
        l_y =  vf;
        l_z =  uf;
        break;
    case UP:
        l_x =  uf;
        l_y = -1.f;
        l_z =  vf;
        break;
    case DOWN:
        l_x =  uf;
        l_y =  1.f;
        l_z = -vf;
        break;
    case FRONT:
        l_x =  uf;
        l_y =  vf;
        l_z =  1.f;
        break;
    case BACK:
        l_x = -uf;
        l_y =  vf;
        l_z = -1.f;
        break;
    default:
        av_assert0(0);
    }

    vec[0] = l_x;
    vec[1] = l_y;
    vec[2] = l_z;

    normalize_vector(vec);
}

/**
 * Calculate cubemap position for corresponding 3D coordinates on sphere.
 * Common operation for every cubemap.
 *
 * @param s filter private context
 * @param vec coordinated on sphere
 * @param uf horizontal cubemap coordinate [0, 1)
 * @param vf vertical cubemap coordinate [0, 1)
 * @param direction direction of view
 */
static void xyz_to_cube(const V360Context *s,
                        const float *vec,
                        float *uf, float *vf, int *direction)
{
    const float phi   = atan2f(vec[0], vec[2]);
    const float theta = asinf(vec[1]);
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
        *uf = -vec[2] / vec[0];
        *vf =  vec[1] / vec[0];
        break;
    case LEFT:
        *uf = -vec[2] / vec[0];
        *vf = -vec[1] / vec[0];
        break;
    case UP:
        *uf = -vec[0] / vec[1];
        *vf = -vec[2] / vec[1];
        break;
    case DOWN:
        *uf =  vec[0] / vec[1];
        *vf = -vec[2] / vec[1];
        break;
    case FRONT:
        *uf =  vec[0] / vec[2];
        *vf =  vec[1] / vec[2];
        break;
    case BACK:
        *uf =  vec[0] / vec[2];
        *vf = -vec[1] / vec[2];
        break;
    default:
        av_assert0(0);
    }

    face = s->in_cubemap_face_order[*direction];
    rotate_cube_face(uf, vf, s->in_cubemap_face_rotation[face]);

    (*uf) *= s->input_mirror_modifier[0];
    (*vf) *= s->input_mirror_modifier[1];
}

/**
 * Find position on another cube face in case of overflow/underflow.
 * Used for calculation of interpolation window.
 *
 * @param s filter private context
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
        default:
            av_assert0(0);
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
        default:
            av_assert0(0);
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
        default:
            av_assert0(0);
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
        default:
            av_assert0(0);
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
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int cube3x2_to_xyz(const V360Context *s,
                          int i, int j, int width, int height,
                          float *vec)
{
    const float scalew = s->fout_pad > 0 ? 1.f - s->fout_pad / (width  / 3.f) : 1.f - s->out_pad;
    const float scaleh = s->fout_pad > 0 ? 1.f - s->fout_pad / (height / 2.f) : 1.f - s->out_pad;

    const float ew = width  / 3.f;
    const float eh = height / 2.f;

    const int u_face = floorf(i / ew);
    const int v_face = floorf(j / eh);
    const int face = u_face + 3 * v_face;

    const int u_shift = ceilf(ew * u_face);
    const int v_shift = ceilf(eh * v_face);
    const int ewi = ceilf(ew * (u_face + 1)) - u_shift;
    const int ehi = ceilf(eh * (v_face + 1)) - v_shift;

    const float uf = 2.f * (i - u_shift + 0.5f) / ewi - 1.f;
    const float vf = 2.f * (j - v_shift + 0.5f) / ehi - 1.f;

    cube_to_xyz(s, uf, vf, face, vec, scalew, scaleh);

    return 1;
}

/**
 * Calculate frame position in cubemap3x2 format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_cube3x2(const V360Context *s,
                          const float *vec, int width, int height,
                          int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float scalew = s->fin_pad > 0 ? 1.f - s->fin_pad / (width  / 3.f) : 1.f - s->in_pad;
    const float scaleh = s->fin_pad > 0 ? 1.f - s->fin_pad / (height / 2.f) : 1.f - s->in_pad;
    const float ew = width  / 3.f;
    const float eh = height / 2.f;
    float uf, vf;
    int ui, vi;
    int ewi, ehi;
    int direction, face;
    int u_face, v_face;

    xyz_to_cube(s, vec, &uf, &vf, &direction);

    uf *= scalew;
    vf *= scaleh;

    face = s->in_cubemap_face_order[direction];
    u_face = face % 3;
    v_face = face / 3;
    ewi = ceilf(ew * (u_face + 1)) - ceilf(ew * u_face);
    ehi = ceilf(eh * (v_face + 1)) - ceilf(eh * v_face);

    uf = 0.5f * ewi * (uf + 1.f) - 0.5f;
    vf = 0.5f * ehi * (vf + 1.f) - 0.5f;

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int new_ui = ui + j - 1;
            int new_vi = vi + i - 1;
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

                uf /= scalew;
                vf /= scaleh;

                process_cube_coordinates(s, uf, vf, direction, &uf, &vf, &face);

                uf *= scalew;
                vf *= scaleh;

                u_face = face % 3;
                v_face = face / 3;
                u_shift = ceilf(ew * u_face);
                v_shift = ceilf(eh * v_face);
                new_ewi = ceilf(ew * (u_face + 1)) - u_shift;
                new_ehi = ceilf(eh * (v_face + 1)) - v_shift;

                new_ui = av_clip(lrintf(0.5f * new_ewi * (uf + 1.f)), 0, new_ewi - 1);
                new_vi = av_clip(lrintf(0.5f * new_ehi * (vf + 1.f)), 0, new_ehi - 1);
            }

            us[i][j] = u_shift + new_ui;
            vs[i][j] = v_shift + new_vi;
        }
    }

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in cubemap1x6 format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int cube1x6_to_xyz(const V360Context *s,
                          int i, int j, int width, int height,
                          float *vec)
{
    const float scalew = s->fout_pad > 0 ? 1.f - (float)(s->fout_pad) / width : 1.f - s->out_pad;
    const float scaleh = s->fout_pad > 0 ? 1.f - s->fout_pad / (height / 6.f) : 1.f - s->out_pad;

    const float ew = width;
    const float eh = height / 6.f;

    const int face = floorf(j / eh);

    const int v_shift = ceilf(eh * face);
    const int ehi = ceilf(eh * (face + 1)) - v_shift;

    const float uf = 2.f * (i           + 0.5f) / ew  - 1.f;
    const float vf = 2.f * (j - v_shift + 0.5f) / ehi - 1.f;

    cube_to_xyz(s, uf, vf, face, vec, scalew, scaleh);

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in cubemap6x1 format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int cube6x1_to_xyz(const V360Context *s,
                          int i, int j, int width, int height,
                          float *vec)
{
    const float scalew = s->fout_pad > 0 ? 1.f - s->fout_pad / (width / 6.f)   : 1.f - s->out_pad;
    const float scaleh = s->fout_pad > 0 ? 1.f - (float)(s->fout_pad) / height : 1.f - s->out_pad;

    const float ew = width / 6.f;
    const float eh = height;

    const int face = floorf(i / ew);

    const int u_shift = ceilf(ew * face);
    const int ewi = ceilf(ew * (face + 1)) - u_shift;

    const float uf = 2.f * (i - u_shift + 0.5f) / ewi - 1.f;
    const float vf = 2.f * (j           + 0.5f) / eh  - 1.f;

    cube_to_xyz(s, uf, vf, face, vec, scalew, scaleh);

    return 1;
}

/**
 * Calculate frame position in cubemap1x6 format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_cube1x6(const V360Context *s,
                          const float *vec, int width, int height,
                          int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float scalew = s->fin_pad > 0 ? 1.f - (float)(s->fin_pad) / width : 1.f - s->in_pad;
    const float scaleh = s->fin_pad > 0 ? 1.f - s->fin_pad / (height / 6.f) : 1.f - s->in_pad;
    const float eh = height / 6.f;
    const int ewi = width;
    float uf, vf;
    int ui, vi;
    int ehi;
    int direction, face;

    xyz_to_cube(s, vec, &uf, &vf, &direction);

    uf *= scalew;
    vf *= scaleh;

    face = s->in_cubemap_face_order[direction];
    ehi = ceilf(eh * (face + 1)) - ceilf(eh * face);

    uf = 0.5f * ewi * (uf + 1.f) - 0.5f;
    vf = 0.5f * ehi * (vf + 1.f) - 0.5f;

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int new_ui = ui + j - 1;
            int new_vi = vi + i - 1;
            int v_shift;
            int new_ehi;

            if (new_ui >= 0 && new_ui < ewi && new_vi >= 0 && new_vi < ehi) {
                face = s->in_cubemap_face_order[direction];

                v_shift = ceilf(eh * face);
            } else {
                uf = 2.f * new_ui / ewi - 1.f;
                vf = 2.f * new_vi / ehi - 1.f;

                uf /= scalew;
                vf /= scaleh;

                process_cube_coordinates(s, uf, vf, direction, &uf, &vf, &face);

                uf *= scalew;
                vf *= scaleh;

                v_shift = ceilf(eh * face);
                new_ehi = ceilf(eh * (face + 1)) - v_shift;

                new_ui = av_clip(lrintf(0.5f *     ewi * (uf + 1.f)), 0,     ewi - 1);
                new_vi = av_clip(lrintf(0.5f * new_ehi * (vf + 1.f)), 0, new_ehi - 1);
            }

            us[i][j] =           new_ui;
            vs[i][j] = v_shift + new_vi;
        }
    }

    return 1;
}

/**
 * Calculate frame position in cubemap6x1 format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_cube6x1(const V360Context *s,
                          const float *vec, int width, int height,
                          int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float scalew = s->fin_pad > 0 ? 1.f - s->fin_pad / (width / 6.f)   : 1.f - s->in_pad;
    const float scaleh = s->fin_pad > 0 ? 1.f - (float)(s->fin_pad) / height : 1.f - s->in_pad;
    const float ew = width / 6.f;
    const int ehi = height;
    float uf, vf;
    int ui, vi;
    int ewi;
    int direction, face;

    xyz_to_cube(s, vec, &uf, &vf, &direction);

    uf *= scalew;
    vf *= scaleh;

    face = s->in_cubemap_face_order[direction];
    ewi = ceilf(ew * (face + 1)) - ceilf(ew * face);

    uf = 0.5f * ewi * (uf + 1.f) - 0.5f;
    vf = 0.5f * ehi * (vf + 1.f) - 0.5f;

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int new_ui = ui + j - 1;
            int new_vi = vi + i - 1;
            int u_shift;
            int new_ewi;

            if (new_ui >= 0 && new_ui < ewi && new_vi >= 0 && new_vi < ehi) {
                face = s->in_cubemap_face_order[direction];

                u_shift = ceilf(ew * face);
            } else {
                uf = 2.f * new_ui / ewi - 1.f;
                vf = 2.f * new_vi / ehi - 1.f;

                uf /= scalew;
                vf /= scaleh;

                process_cube_coordinates(s, uf, vf, direction, &uf, &vf, &face);

                uf *= scalew;
                vf *= scaleh;

                u_shift = ceilf(ew * face);
                new_ewi = ceilf(ew * (face + 1)) - u_shift;

                new_ui = av_clip(lrintf(0.5f * new_ewi * (uf + 1.f)), 0, new_ewi - 1);
                new_vi = av_clip(lrintf(0.5f *     ehi * (vf + 1.f)), 0,     ehi - 1);
            }

            us[i][j] = u_shift + new_ui;
            vs[i][j] =           new_vi;
        }
    }

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in equirectangular format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int equirect_to_xyz(const V360Context *s,
                           int i, int j, int width, int height,
                           float *vec)
{
    const float phi   = ((2.f * i + 0.5f) / width  - 1.f) * M_PI;
    const float theta = ((2.f * j + 0.5f) / height - 1.f) * M_PI_2;

    const float sin_phi   = sinf(phi);
    const float cos_phi   = cosf(phi);
    const float sin_theta = sinf(theta);
    const float cos_theta = cosf(theta);

    vec[0] = cos_theta * sin_phi;
    vec[1] = sin_theta;
    vec[2] = cos_theta * cos_phi;

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in half equirectangular format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int hequirect_to_xyz(const V360Context *s,
                            int i, int j, int width, int height,
                            float *vec)
{
    const float phi   = ((2.f * i + 0.5f) / width  - 1.f) * M_PI_2;
    const float theta = ((2.f * j + 0.5f) / height - 1.f) * M_PI_2;

    const float sin_phi   = sinf(phi);
    const float cos_phi   = cosf(phi);
    const float sin_theta = sinf(theta);
    const float cos_theta = cosf(theta);

    vec[0] = cos_theta * sin_phi;
    vec[1] = sin_theta;
    vec[2] = cos_theta * cos_phi;

    return 1;
}

/**
 * Prepare data for processing stereographic output format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_stereographic_out(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->flat_range[0] = tanf(FFMIN(s->h_fov, 359.f) * M_PI / 720.f);
    s->flat_range[1] = tanf(FFMIN(s->v_fov, 359.f) * M_PI / 720.f);

    return 0;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in stereographic format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int stereographic_to_xyz(const V360Context *s,
                                int i, int j, int width, int height,
                                float *vec)
{
    const float x = ((2.f * i + 1.f) / width  - 1.f) * s->flat_range[0];
    const float y = ((2.f * j + 1.f) / height - 1.f) * s->flat_range[1];
    const float r = hypotf(x, y);
    const float theta = atanf(r) * 2.f;
    const float sin_theta = sinf(theta);

    vec[0] = x / r * sin_theta;
    vec[1] = y / r * sin_theta;
    vec[2] = cosf(theta);

    normalize_vector(vec);

    return 1;
}

/**
 * Prepare data for processing stereographic input format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_stereographic_in(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->iflat_range[0] = tanf(FFMIN(s->ih_fov, 359.f) * M_PI / 720.f);
    s->iflat_range[1] = tanf(FFMIN(s->iv_fov, 359.f) * M_PI / 720.f);

    return 0;
}

/**
 * Calculate frame position in stereographic format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_stereographic(const V360Context *s,
                                const float *vec, int width, int height,
                                int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float theta = acosf(vec[2]);
    const float r = tanf(theta * 0.5f);
    const float c = r / hypotf(vec[0], vec[1]);
    const float x = vec[0] * c / s->iflat_range[0] * s->input_mirror_modifier[0];
    const float y = vec[1] * c / s->iflat_range[1] * s->input_mirror_modifier[1];

    const float uf = (x + 1.f) * width  / 2.f;
    const float vf = (y + 1.f) * height / 2.f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    const int visible = isfinite(x) && isfinite(y) && vi >= 0 && vi < height && ui >= 0 && ui < width;

    *du = visible ? uf - ui : 0.f;
    *dv = visible ? vf - vi : 0.f;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = visible ? av_clip(ui + j - 1, 0, width  - 1) : 0;
            vs[i][j] = visible ? av_clip(vi + i - 1, 0, height - 1) : 0;
        }
    }

    return visible;
}

/**
 * Prepare data for processing equisolid output format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_equisolid_out(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->flat_range[0] = sinf(s->h_fov * M_PI / 720.f);
    s->flat_range[1] = sinf(s->v_fov * M_PI / 720.f);

    return 0;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in equisolid format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int equisolid_to_xyz(const V360Context *s,
                            int i, int j, int width, int height,
                            float *vec)
{
    const float x = ((2.f * i + 1.f) / width  - 1.f) * s->flat_range[0];
    const float y = ((2.f * j + 1.f) / height - 1.f) * s->flat_range[1];
    const float r = hypotf(x, y);
    const float theta = asinf(r) * 2.f;
    const float sin_theta = sinf(theta);

    vec[0] = x / r * sin_theta;
    vec[1] = y / r * sin_theta;
    vec[2] = cosf(theta);

    normalize_vector(vec);

    return 1;
}

/**
 * Prepare data for processing equisolid input format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_equisolid_in(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->iflat_range[0] = sinf(FFMIN(s->ih_fov, 359.f) * M_PI / 720.f);
    s->iflat_range[1] = sinf(FFMIN(s->iv_fov, 359.f) * M_PI / 720.f);

    return 0;
}

/**
 * Calculate frame position in equisolid format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_equisolid(const V360Context *s,
                            const float *vec, int width, int height,
                            int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float theta = acosf(vec[2]);
    const float r = sinf(theta * 0.5f);
    const float c = r / hypotf(vec[0], vec[1]);
    const float x = vec[0] * c / s->iflat_range[0] * s->input_mirror_modifier[0];
    const float y = vec[1] * c / s->iflat_range[1] * s->input_mirror_modifier[1];

    const float uf = (x + 1.f) * width  / 2.f;
    const float vf = (y + 1.f) * height / 2.f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    const int visible = isfinite(x) && isfinite(y) && vi >= 0 && vi < height && ui >= 0 && ui < width;

    *du = visible ? uf - ui : 0.f;
    *dv = visible ? vf - vi : 0.f;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = visible ? av_clip(ui + j - 1, 0, width  - 1) : 0;
            vs[i][j] = visible ? av_clip(vi + i - 1, 0, height - 1) : 0;
        }
    }

    return visible;
}

/**
 * Prepare data for processing orthographic output format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_orthographic_out(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->flat_range[0] = sinf(FFMIN(s->h_fov, 180.f) * M_PI / 360.f);
    s->flat_range[1] = sinf(FFMIN(s->v_fov, 180.f) * M_PI / 360.f);

    return 0;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in orthographic format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int orthographic_to_xyz(const V360Context *s,
                               int i, int j, int width, int height,
                               float *vec)
{
    const float x = ((2.f * i + 1.f) / width  - 1.f) * s->flat_range[0];
    const float y = ((2.f * j + 1.f) / height - 1.f) * s->flat_range[1];
    const float r = hypotf(x, y);
    const float theta = asinf(r);

    vec[0] = x;
    vec[1] = y;
    vec[2] = cosf(theta);

    normalize_vector(vec);

    return 1;
}

/**
 * Prepare data for processing orthographic input format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_orthographic_in(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->iflat_range[0] = sinf(FFMIN(s->ih_fov, 180.f) * M_PI / 360.f);
    s->iflat_range[1] = sinf(FFMIN(s->iv_fov, 180.f) * M_PI / 360.f);

    return 0;
}

/**
 * Calculate frame position in orthographic format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_orthographic(const V360Context *s,
                               const float *vec, int width, int height,
                               int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float theta = acosf(vec[2]);
    const float r = sinf(theta);
    const float c = r / hypotf(vec[0], vec[1]);
    const float x = vec[0] * c / s->iflat_range[0] * s->input_mirror_modifier[0];
    const float y = vec[1] * c / s->iflat_range[1] * s->input_mirror_modifier[1];

    const float uf = (x + 1.f) * width  / 2.f;
    const float vf = (y + 1.f) * height / 2.f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    const int visible = vec[2] >= 0.f && isfinite(x) && isfinite(y) && vi >= 0 && vi < height && ui >= 0 && ui < width;

    *du = visible ? uf - ui : 0.f;
    *dv = visible ? vf - vi : 0.f;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = visible ? av_clip(ui + j - 1, 0, width  - 1) : 0;
            vs[i][j] = visible ? av_clip(vi + i - 1, 0, height - 1) : 0;
        }
    }

    return visible;
}

/**
 * Calculate frame position in equirectangular format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_equirect(const V360Context *s,
                           const float *vec, int width, int height,
                           int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float phi   = atan2f(vec[0], vec[2]) * s->input_mirror_modifier[0];
    const float theta = asinf(vec[1]) * s->input_mirror_modifier[1];

    const float uf = (phi   / M_PI   + 1.f) * width  / 2.f;
    const float vf = (theta / M_PI_2 + 1.f) * height / 2.f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = ereflectx(ui + j - 1, vi + i - 1, width, height);
            vs[i][j] = reflecty(vi + i - 1, height);
        }
    }

    return 1;
}

/**
 * Calculate frame position in half equirectangular format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_hequirect(const V360Context *s,
                            const float *vec, int width, int height,
                            int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float phi   = atan2f(vec[0], vec[2]) * s->input_mirror_modifier[0];
    const float theta = asinf(vec[1]) * s->input_mirror_modifier[1];

    const float uf = (phi   / M_PI_2 + 1.f) * width  / 2.f;
    const float vf = (theta / M_PI_2 + 1.f) * height / 2.f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    const int visible = phi >= -M_PI_2 && phi <= M_PI_2;

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = av_clip(ui + j - 1, 0, width  - 1);
            vs[i][j] = av_clip(vi + i - 1, 0, height - 1);
        }
    }

    return visible;
}

/**
 * Prepare data for processing flat input format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_flat_in(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->iflat_range[0] = tanf(0.5f * s->ih_fov * M_PI / 180.f);
    s->iflat_range[1] = tanf(0.5f * s->iv_fov * M_PI / 180.f);

    return 0;
}

/**
 * Calculate frame position in flat format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_flat(const V360Context *s,
                       const float *vec, int width, int height,
                       int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float theta = acosf(vec[2]);
    const float r = tanf(theta);
    const float rr = fabsf(r) < 1e+6f ? r : hypotf(width, height);
    const float zf = vec[2];
    const float h = hypotf(vec[0], vec[1]);
    const float c = h <= 1e-6f ? 1.f : rr / h;
    float uf = vec[0] * c / s->iflat_range[0] * s->input_mirror_modifier[0];
    float vf = vec[1] * c / s->iflat_range[1] * s->input_mirror_modifier[1];
    int visible, ui, vi;

    uf = zf >= 0.f ? (uf + 1.f) * width  / 2.f : 0.f;
    vf = zf >= 0.f ? (vf + 1.f) * height / 2.f : 0.f;

    ui = floorf(uf);
    vi = floorf(vf);

    visible = vi >= 0 && vi < height && ui >= 0 && ui < width && zf >= 0.f;

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = visible ? av_clip(ui + j - 1, 0, width  - 1) : 0;
            vs[i][j] = visible ? av_clip(vi + i - 1, 0, height - 1) : 0;
        }
    }

    return visible;
}

/**
 * Calculate frame position in mercator format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_mercator(const V360Context *s,
                           const float *vec, int width, int height,
                           int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float phi   = atan2f(vec[0], vec[2]) * s->input_mirror_modifier[0];
    const float theta = vec[1] * s->input_mirror_modifier[1];

    const float uf = (phi / M_PI + 1.f) * width / 2.f;
    const float vf = (av_clipf(logf((1.f + theta) / (1.f - theta)) / (2.f * M_PI), -1.f, 1.f) + 1.f) * height / 2.f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = av_clip(ui + j - 1, 0, width  - 1);
            vs[i][j] = av_clip(vi + i - 1, 0, height - 1);
        }
    }

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in mercator format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int mercator_to_xyz(const V360Context *s,
                           int i, int j, int width, int height,
                           float *vec)
{
    const float phi = ((2.f * i + 1.f) / width  - 1.f) * M_PI + M_PI_2;
    const float y   = ((2.f * j + 1.f) / height - 1.f) * M_PI;
    const float div = expf(2.f * y) + 1.f;

    const float sin_phi   = sinf(phi);
    const float cos_phi   = cosf(phi);
    const float sin_theta = 2.f * expf(y) / div;
    const float cos_theta = (expf(2.f * y) - 1.f) / div;

    vec[0] = -sin_theta * cos_phi;
    vec[1] =  cos_theta;
    vec[2] =  sin_theta * sin_phi;

    return 1;
}

/**
 * Calculate frame position in ball format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_ball(const V360Context *s,
                       const float *vec, int width, int height,
                       int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float l = hypotf(vec[0], vec[1]);
    const float r = sqrtf(1.f - vec[2]) / M_SQRT2;

    const float uf = (1.f + r * vec[0] * s->input_mirror_modifier[0] / (l > 0.f ? l : 1.f)) * width  * 0.5f;
    const float vf = (1.f + r * vec[1] * s->input_mirror_modifier[1] / (l > 0.f ? l : 1.f)) * height * 0.5f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = av_clip(ui + j - 1, 0, width  - 1);
            vs[i][j] = av_clip(vi + i - 1, 0, height - 1);
        }
    }

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in ball format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int ball_to_xyz(const V360Context *s,
                       int i, int j, int width, int height,
                       float *vec)
{
    const float x = (2.f * i + 1.f) / width  - 1.f;
    const float y = (2.f * j + 1.f) / height - 1.f;
    const float l = hypotf(x, y);

    if (l <= 1.f) {
        const float z = 2.f * l * sqrtf(1.f - l * l);

        vec[0] = z * x / (l > 0.f ? l : 1.f);
        vec[1] = z * y / (l > 0.f ? l : 1.f);
        vec[2] = 1.f - 2.f * l * l;
    } else {
        vec[0] = 0.f;
        vec[1] = 1.f;
        vec[2] = 0.f;
        return 0;
    }

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in hammer format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int hammer_to_xyz(const V360Context *s,
                         int i, int j, int width, int height,
                         float *vec)
{
    const float x = ((2.f * i + 1.f) / width  - 1.f);
    const float y = ((2.f * j + 1.f) / height - 1.f);

    const float xx = x * x;
    const float yy = y * y;

    const float z = sqrtf(1.f - xx * 0.5f - yy * 0.5f);

    const float a = M_SQRT2 * x * z;
    const float b = 2.f * z * z - 1.f;

    const float aa = a * a;
    const float bb = b * b;

    const float w = sqrtf(1.f - 2.f * yy * z * z);

    vec[0] = w * 2.f * a * b / (aa + bb);
    vec[1] = M_SQRT2 * y * z;
    vec[2] = w * (bb  - aa) / (aa + bb);

    normalize_vector(vec);

    return 1;
}

/**
 * Calculate frame position in hammer format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_hammer(const V360Context *s,
                         const float *vec, int width, int height,
                         int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float theta = atan2f(vec[0], vec[2]) * s->input_mirror_modifier[0];

    const float z = sqrtf(1.f + sqrtf(1.f - vec[1] * vec[1]) * cosf(theta * 0.5f));
    const float x = sqrtf(1.f - vec[1] * vec[1]) * sinf(theta * 0.5f) / z;
    const float y = vec[1] / z * s->input_mirror_modifier[1];

    const float uf = (x + 1.f) * width  / 2.f;
    const float vf = (y + 1.f) * height / 2.f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = av_clip(ui + j - 1, 0, width  - 1);
            vs[i][j] = av_clip(vi + i - 1, 0, height - 1);
        }
    }

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in sinusoidal format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int sinusoidal_to_xyz(const V360Context *s,
                             int i, int j, int width, int height,
                             float *vec)
{
    const float theta = ((2.f * j + 1.f) / height - 1.f) * M_PI_2;
    const float phi   = ((2.f * i + 1.f) / width  - 1.f) * M_PI / cosf(theta);

    const float sin_phi   = sinf(phi);
    const float cos_phi   = cosf(phi);
    const float sin_theta = sinf(theta);
    const float cos_theta = cosf(theta);

    vec[0] = cos_theta * sin_phi;
    vec[1] = sin_theta;
    vec[2] = cos_theta * cos_phi;

    normalize_vector(vec);

    return 1;
}

/**
 * Calculate frame position in sinusoidal format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_sinusoidal(const V360Context *s,
                             const float *vec, int width, int height,
                             int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float theta = asinf(vec[1]) * s->input_mirror_modifier[1];
    const float phi   = atan2f(vec[0], vec[2]) * s->input_mirror_modifier[0] * cosf(theta);

    const float uf = (phi   / M_PI   + 1.f) * width  / 2.f;
    const float vf = (theta / M_PI_2 + 1.f) * height / 2.f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = av_clip(ui + j - 1, 0, width  - 1);
            vs[i][j] = av_clip(vi + i - 1, 0, height - 1);
        }
    }

    return 1;
}

/**
 * Prepare data for processing equi-angular cubemap input format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_eac_in(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    if (s->ih_flip && s->iv_flip) {
        s->in_cubemap_face_order[RIGHT] = BOTTOM_LEFT;
        s->in_cubemap_face_order[LEFT]  = BOTTOM_RIGHT;
        s->in_cubemap_face_order[UP]    = TOP_LEFT;
        s->in_cubemap_face_order[DOWN]  = TOP_RIGHT;
        s->in_cubemap_face_order[FRONT] = BOTTOM_MIDDLE;
        s->in_cubemap_face_order[BACK]  = TOP_MIDDLE;
    } else if (s->ih_flip) {
        s->in_cubemap_face_order[RIGHT] = TOP_LEFT;
        s->in_cubemap_face_order[LEFT]  = TOP_RIGHT;
        s->in_cubemap_face_order[UP]    = BOTTOM_LEFT;
        s->in_cubemap_face_order[DOWN]  = BOTTOM_RIGHT;
        s->in_cubemap_face_order[FRONT] = TOP_MIDDLE;
        s->in_cubemap_face_order[BACK]  = BOTTOM_MIDDLE;
    } else if (s->iv_flip) {
        s->in_cubemap_face_order[RIGHT] = BOTTOM_RIGHT;
        s->in_cubemap_face_order[LEFT]  = BOTTOM_LEFT;
        s->in_cubemap_face_order[UP]    = TOP_RIGHT;
        s->in_cubemap_face_order[DOWN]  = TOP_LEFT;
        s->in_cubemap_face_order[FRONT] = BOTTOM_MIDDLE;
        s->in_cubemap_face_order[BACK]  = TOP_MIDDLE;
    } else {
        s->in_cubemap_face_order[RIGHT] = TOP_RIGHT;
        s->in_cubemap_face_order[LEFT]  = TOP_LEFT;
        s->in_cubemap_face_order[UP]    = BOTTOM_RIGHT;
        s->in_cubemap_face_order[DOWN]  = BOTTOM_LEFT;
        s->in_cubemap_face_order[FRONT] = TOP_MIDDLE;
        s->in_cubemap_face_order[BACK]  = BOTTOM_MIDDLE;
    }

    if (s->iv_flip) {
        s->in_cubemap_face_rotation[TOP_LEFT]      = ROT_270;
        s->in_cubemap_face_rotation[TOP_MIDDLE]    = ROT_90;
        s->in_cubemap_face_rotation[TOP_RIGHT]     = ROT_270;
        s->in_cubemap_face_rotation[BOTTOM_LEFT]   = ROT_0;
        s->in_cubemap_face_rotation[BOTTOM_MIDDLE] = ROT_0;
        s->in_cubemap_face_rotation[BOTTOM_RIGHT]  = ROT_0;
    } else {
        s->in_cubemap_face_rotation[TOP_LEFT]      = ROT_0;
        s->in_cubemap_face_rotation[TOP_MIDDLE]    = ROT_0;
        s->in_cubemap_face_rotation[TOP_RIGHT]     = ROT_0;
        s->in_cubemap_face_rotation[BOTTOM_LEFT]   = ROT_270;
        s->in_cubemap_face_rotation[BOTTOM_MIDDLE] = ROT_90;
        s->in_cubemap_face_rotation[BOTTOM_RIGHT]  = ROT_270;
    }

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
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int eac_to_xyz(const V360Context *s,
                      int i, int j, int width, int height,
                      float *vec)
{
    const float pixel_pad = 2;
    const float u_pad = pixel_pad / width;
    const float v_pad = pixel_pad / height;

    int u_face, v_face, face;

    float l_x, l_y, l_z;

    float uf = (i + 0.5f) / width;
    float vf = (j + 0.5f) / height;

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
        l_y =  vf;
        l_z =  uf;
        break;
    case TOP_MIDDLE:
        l_x =  uf;
        l_y =  vf;
        l_z =  1.f;
        break;
    case TOP_RIGHT:
        l_x =  1.f;
        l_y =  vf;
        l_z = -uf;
        break;
    case BOTTOM_LEFT:
        l_x = -vf;
        l_y =  1.f;
        l_z = -uf;
        break;
    case BOTTOM_MIDDLE:
        l_x = -vf;
        l_y = -uf;
        l_z = -1.f;
        break;
    case BOTTOM_RIGHT:
        l_x = -vf;
        l_y = -1.f;
        l_z =  uf;
        break;
    default:
        av_assert0(0);
    }

    vec[0] = l_x;
    vec[1] = l_y;
    vec[2] = l_z;

    normalize_vector(vec);

    return 1;
}

/**
 * Calculate frame position in equi-angular cubemap format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_eac(const V360Context *s,
                      const float *vec, int width, int height,
                      int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float pixel_pad = 2;
    const float u_pad = pixel_pad / width;
    const float v_pad = pixel_pad / height;

    float uf, vf;
    int ui, vi;
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

    uf -= 0.5f;
    vf -= 0.5f;

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = av_clip(ui + j - 1, 0, width  - 1);
            vs[i][j] = av_clip(vi + i - 1, 0, height - 1);
        }
    }

    return 1;
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

    s->flat_range[0] = tanf(0.5f * s->h_fov * M_PI / 180.f);
    s->flat_range[1] = tanf(0.5f * s->v_fov * M_PI / 180.f);

    return 0;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in flat format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int flat_to_xyz(const V360Context *s,
                       int i, int j, int width, int height,
                       float *vec)
{
    const float l_x = s->flat_range[0] * ((2.f * i + 0.5f) / width  - 1.f);
    const float l_y = s->flat_range[1] * ((2.f * j + 0.5f) / height - 1.f);

    vec[0] = l_x;
    vec[1] = l_y;
    vec[2] = 1.f;

    normalize_vector(vec);

    return 1;
}

/**
 * Prepare data for processing fisheye output format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_fisheye_out(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->flat_range[0] = s->h_fov / 180.f;
    s->flat_range[1] = s->v_fov / 180.f;

    return 0;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in fisheye format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int fisheye_to_xyz(const V360Context *s,
                          int i, int j, int width, int height,
                          float *vec)
{
    const float uf = s->flat_range[0] * ((2.f * i) / width  - 1.f);
    const float vf = s->flat_range[1] * ((2.f * j + 1.f) / height - 1.f);

    const float phi   = atan2f(vf, uf);
    const float theta = M_PI_2 * (1.f - hypotf(uf, vf));

    const float sin_phi   = sinf(phi);
    const float cos_phi   = cosf(phi);
    const float sin_theta = sinf(theta);
    const float cos_theta = cosf(theta);

    vec[0] = cos_theta * cos_phi;
    vec[1] = cos_theta * sin_phi;
    vec[2] = sin_theta;

    normalize_vector(vec);

    return 1;
}

/**
 * Prepare data for processing fisheye input format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_fisheye_in(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->iflat_range[0] = s->ih_fov / 180.f;
    s->iflat_range[1] = s->iv_fov / 180.f;

    return 0;
}

/**
 * Calculate frame position in fisheye format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_fisheye(const V360Context *s,
                          const float *vec, int width, int height,
                          int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float h   = hypotf(vec[0], vec[1]);
    const float lh  = h > 0.f ? h : 1.f;
    const float phi = atan2f(h, vec[2]) / M_PI;

    float uf = vec[0] / lh * phi * s->input_mirror_modifier[0] / s->iflat_range[0];
    float vf = vec[1] / lh * phi * s->input_mirror_modifier[1] / s->iflat_range[1];

    const int visible = hypotf(uf, vf) <= 0.5f;
    int ui, vi;

    uf = (uf + 0.5f) * width;
    vf = (vf + 0.5f) * height;

    ui = floorf(uf);
    vi = floorf(vf);

    *du = visible ? uf - ui : 0.f;
    *dv = visible ? vf - vi : 0.f;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = visible ? av_clip(ui + j - 1, 0, width  - 1) : 0;
            vs[i][j] = visible ? av_clip(vi + i - 1, 0, height - 1) : 0;
        }
    }

    return visible;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in pannini format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int pannini_to_xyz(const V360Context *s,
                          int i, int j, int width, int height,
                          float *vec)
{
    const float uf = ((2.f * i + 1.f) / width  - 1.f);
    const float vf = ((2.f * j + 1.f) / height - 1.f);

    const float d = s->h_fov;
    const float k = uf * uf / ((d + 1.f) * (d + 1.f));
    const float dscr = k * k * d * d - (k + 1.f) * (k * d * d - 1.f);
    const float clon = (-k * d + sqrtf(dscr)) / (k + 1.f);
    const float S = (d + 1.f) / (d + clon);
    const float lon = atan2f(uf, S * clon);
    const float lat = atan2f(vf, S);

    vec[0] = sinf(lon) * cosf(lat);
    vec[1] = sinf(lat);
    vec[2] = cosf(lon) * cosf(lat);

    normalize_vector(vec);

    return 1;
}

/**
 * Calculate frame position in pannini format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_pannini(const V360Context *s,
                          const float *vec, int width, int height,
                          int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float phi   = atan2f(vec[0], vec[2]) * s->input_mirror_modifier[0];
    const float theta = asinf(vec[1]) * s->input_mirror_modifier[1];

    const float d = s->ih_fov;
    const float S = (d + 1.f) / (d + cosf(phi));

    const float x = S * sinf(phi);
    const float y = S * tanf(theta);

    const float uf = (x + 1.f) * width  / 2.f;
    const float vf = (y + 1.f) * height / 2.f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    const int visible = vi >= 0 && vi < height && ui >= 0 && ui < width && vec[2] >= 0.f;

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = visible ? av_clip(ui + j - 1, 0, width  - 1) : 0;
            vs[i][j] = visible ? av_clip(vi + i - 1, 0, height - 1) : 0;
        }
    }

    return visible;
}

/**
 * Prepare data for processing cylindrical output format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_cylindrical_out(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->flat_range[0] = M_PI * s->h_fov / 360.f;
    s->flat_range[1] = tanf(0.5f * s->v_fov * M_PI / 180.f);

    return 0;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in cylindrical format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int cylindrical_to_xyz(const V360Context *s,
                              int i, int j, int width, int height,
                              float *vec)
{
    const float uf = s->flat_range[0] * ((2.f * i + 1.f) / width  - 1.f);
    const float vf = s->flat_range[1] * ((2.f * j + 1.f) / height - 1.f);

    const float phi   = uf;
    const float theta = atanf(vf);

    const float sin_phi   = sinf(phi);
    const float cos_phi   = cosf(phi);
    const float sin_theta = sinf(theta);
    const float cos_theta = cosf(theta);

    vec[0] = cos_theta * sin_phi;
    vec[1] = sin_theta;
    vec[2] = cos_theta * cos_phi;

    normalize_vector(vec);

    return 1;
}

/**
 * Prepare data for processing cylindrical input format.
 *
 * @param ctx filter context
 *
 * @return error code
 */
static int prepare_cylindrical_in(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    s->iflat_range[0] = M_PI * s->ih_fov / 360.f;
    s->iflat_range[1] = tanf(0.5f * s->iv_fov * M_PI / 180.f);

    return 0;
}

/**
 * Calculate frame position in cylindrical format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_cylindrical(const V360Context *s,
                              const float *vec, int width, int height,
                              int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float phi   = atan2f(vec[0], vec[2]) * s->input_mirror_modifier[0] / s->iflat_range[0];
    const float theta = asinf(vec[1]) * s->input_mirror_modifier[1];

    const float uf = (phi + 1.f) * (width - 1) / 2.f;
    const float vf = (tanf(theta) / s->iflat_range[1] + 1.f) * height / 2.f;

    const int ui = floorf(uf);
    const int vi = floorf(vf);

    const int visible = vi >= 0 && vi < height && ui >= 0 && ui < width &&
                        theta <=  M_PI * s->iv_fov / 180.f &&
                        theta >= -M_PI * s->iv_fov / 180.f;

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = visible ? av_clip(ui + j - 1, 0, width  - 1) : 0;
            vs[i][j] = visible ? av_clip(vi + i - 1, 0, height - 1) : 0;
        }
    }

    return visible;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in perspective format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int perspective_to_xyz(const V360Context *s,
                              int i, int j, int width, int height,
                              float *vec)
{
    const float uf = ((2.f * i + 1.f) / width  - 1.f);
    const float vf = ((2.f * j + 1.f) / height - 1.f);
    const float rh = hypotf(uf, vf);
    const float sinzz = 1.f - rh * rh;
    const float h = 1.f + s->v_fov;
    const float sinz = (h - sqrtf(sinzz)) / (h / rh + rh / h);
    const float sinz2 = sinz * sinz;

    if (sinz2 <= 1.f) {
        const float cosz = sqrtf(1.f - sinz2);

        const float theta = asinf(cosz);
        const float phi   = atan2f(uf, vf);

        const float sin_phi   = sinf(phi);
        const float cos_phi   = cosf(phi);
        const float sin_theta = sinf(theta);
        const float cos_theta = cosf(theta);

        vec[0] = cos_theta * sin_phi;
        vec[1] = sin_theta;
        vec[2] = cos_theta * cos_phi;
    } else {
        vec[0] = 0.f;
        vec[1] = 1.f;
        vec[2] = 0.f;
        return 0;
    }

    normalize_vector(vec);
    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in tetrahedron format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int tetrahedron_to_xyz(const V360Context *s,
                              int i, int j, int width, int height,
                              float *vec)
{
    const float uf = (float)i / width;
    const float vf = (float)j / height;

    vec[0] = uf < 0.5f ? uf * 4.f - 1.f : 3.f - uf * 4.f;
    vec[1] = 1.f - vf * 2.f;
    vec[2] = 2.f * fabsf(1.f - fabsf(1.f - uf * 2.f + vf)) - 1.f;

    normalize_vector(vec);

    return 1;
}

/**
 * Calculate frame position in tetrahedron format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_tetrahedron(const V360Context *s,
                              const float *vec, int width, int height,
                              int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float d0 = vec[0] * 1.f + vec[1] * 1.f + vec[2] *-1.f;
    const float d1 = vec[0] *-1.f + vec[1] *-1.f + vec[2] *-1.f;
    const float d2 = vec[0] * 1.f + vec[1] *-1.f + vec[2] * 1.f;
    const float d3 = vec[0] *-1.f + vec[1] * 1.f + vec[2] * 1.f;
    const float d = FFMAX(d0, FFMAX3(d1, d2, d3));

    float uf, vf, x, y, z;
    int ui, vi;

    x =  vec[0] / d;
    y =  vec[1] / d;
    z = -vec[2] / d;

    vf = 0.5f - y * 0.5f * s->input_mirror_modifier[1];

    if ((x + y >= 0.f &&  y + z >= 0.f && -z - x <= 0.f) ||
        (x + y <= 0.f && -y + z >= 0.f &&  z - x >= 0.f)) {
        uf = 0.25f * x * s->input_mirror_modifier[0] + 0.25f;
    }  else {
        uf = 0.75f - 0.25f * x * s->input_mirror_modifier[0];
    }

    uf *= width;
    vf *= height;

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = reflectx(ui + j - 1, vi + i - 1, width, height);
            vs[i][j] = reflecty(vi + i - 1, height);
        }
    }

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in dual fisheye format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int dfisheye_to_xyz(const V360Context *s,
                           int i, int j, int width, int height,
                           float *vec)
{
    const float ew = width / 2.f;
    const float eh = height;

    const int ei = i >= ew ? i - ew : i;
    const float m = i >= ew ? 1.f : -1.f;

    const float uf = s->flat_range[0] * ((2.f * ei) / ew - 1.f);
    const float vf = s->flat_range[1] * ((2.f * j + 1.f) / eh - 1.f);

    const float h     = hypotf(uf, vf);
    const float lh    = h > 0.f ? h : 1.f;
    const float theta = m * M_PI_2 * (1.f - h);

    const float sin_theta = sinf(theta);
    const float cos_theta = cosf(theta);

    vec[0] = cos_theta * m * uf / lh;
    vec[1] = cos_theta *     vf / lh;
    vec[2] = sin_theta;

    normalize_vector(vec);

    return 1;
}

/**
 * Calculate frame position in dual fisheye format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_dfisheye(const V360Context *s,
                           const float *vec, int width, int height,
                           int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float ew = width / 2.f;
    const float eh = height;

    const float h     = hypotf(vec[0], vec[1]);
    const float lh    = h > 0.f ? h : 1.f;
    const float theta = acosf(fabsf(vec[2])) / M_PI;

    float uf = (theta * (vec[0] / lh) * s->input_mirror_modifier[0] / s->iflat_range[0] + 0.5f) * ew;
    float vf = (theta * (vec[1] / lh) * s->input_mirror_modifier[1] / s->iflat_range[1] + 0.5f) * eh;

    int ui, vi;
    int u_shift;

    if (vec[2] >= 0.f) {
        u_shift = ceilf(ew);
    } else {
        u_shift = 0;
        uf = ew - uf;
    }

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = av_clip(u_shift + ui + j - 1, 0, width  - 1);
            vs[i][j] = av_clip(          vi + i - 1, 0, height - 1);
        }
    }

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in barrel facebook's format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int barrel_to_xyz(const V360Context *s,
                         int i, int j, int width, int height,
                         float *vec)
{
    const float scale = 0.99f;
    float l_x, l_y, l_z;

    if (i < 4 * width / 5) {
        const float theta_range = M_PI_4;

        const int ew = 4 * width / 5;
        const int eh = height;

        const float phi   = ((2.f * i) / ew - 1.f) * M_PI        / scale;
        const float theta = ((2.f * j) / eh - 1.f) * theta_range / scale;

        const float sin_phi   = sinf(phi);
        const float cos_phi   = cosf(phi);
        const float sin_theta = sinf(theta);
        const float cos_theta = cosf(theta);

        l_x = cos_theta * sin_phi;
        l_y = sin_theta;
        l_z = cos_theta * cos_phi;
    } else {
        const int ew = width  / 5;
        const int eh = height / 2;

        float uf, vf;

        if (j < eh) {   // UP
            uf = 2.f * (i - 4 * ew) / ew - 1.f;
            vf = 2.f * (j         ) / eh - 1.f;

            uf /= scale;
            vf /= scale;

            l_x =  uf;
            l_y = -1.f;
            l_z =  vf;
        } else {            // DOWN
            uf = 2.f * (i - 4 * ew) / ew - 1.f;
            vf = 2.f * (j -     eh) / eh - 1.f;

            uf /= scale;
            vf /= scale;

            l_x =  uf;
            l_y =  1.f;
            l_z = -vf;
        }
    }

    vec[0] = l_x;
    vec[1] = l_y;
    vec[2] = l_z;

    normalize_vector(vec);

    return 1;
}

/**
 * Calculate frame position in barrel facebook's format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_barrel(const V360Context *s,
                         const float *vec, int width, int height,
                         int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float scale = 0.99f;

    const float phi   = atan2f(vec[0], vec[2]) * s->input_mirror_modifier[0];
    const float theta = asinf(vec[1]) * s->input_mirror_modifier[1];
    const float theta_range = M_PI_4;

    int ew, eh;
    int u_shift, v_shift;
    float uf, vf;
    int ui, vi;

    if (theta > -theta_range && theta < theta_range) {
        ew = 4 * width / 5;
        eh = height;

        u_shift = s->ih_flip ? width / 5 : 0;
        v_shift = 0;

        uf = (phi   / M_PI        * scale + 1.f) * ew / 2.f;
        vf = (theta / theta_range * scale + 1.f) * eh / 2.f;
    } else {
        ew = width  / 5;
        eh = height / 2;

        u_shift = s->ih_flip ? 0 : 4 * ew;

        if (theta < 0.f) {  // UP
            uf = -vec[0] / vec[1];
            vf = -vec[2] / vec[1];
            v_shift = 0;
        } else {            // DOWN
            uf =  vec[0] / vec[1];
            vf = -vec[2] / vec[1];
            v_shift = eh;
        }

        uf *= s->input_mirror_modifier[0] * s->input_mirror_modifier[1];
        vf *= s->input_mirror_modifier[1];

        uf = 0.5f * ew * (uf * scale + 1.f);
        vf = 0.5f * eh * (vf * scale + 1.f);
    }

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = u_shift + av_clip(ui + j - 1, 0, ew - 1);
            vs[i][j] = v_shift + av_clip(vi + i - 1, 0, eh - 1);
        }
    }

    return 1;
}

/**
 * Calculate frame position in barrel split facebook's format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_barrelsplit(const V360Context *s,
                              const float *vec, int width, int height,
                              int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    const float phi   = atan2f(vec[0], vec[2]) * s->input_mirror_modifier[0];
    const float theta = asinf(vec[1]) * s->input_mirror_modifier[1];

    const float theta_range = M_PI_4;

    int ew, eh;
    int u_shift, v_shift;
    float uf, vf;
    int ui, vi;

    if (theta >= -theta_range && theta <= theta_range) {
        const float scalew = s->fin_pad > 0 ? 1.f - s->fin_pad / (width * 2.f / 3.f) : 1.f - s->in_pad;
        const float scaleh = s->fin_pad > 0 ? 1.f - s->fin_pad / (height / 2.f) : 1.f - s->in_pad;

        ew = width / 3 * 2;
        eh = height / 2;

        u_shift = s->ih_flip ? width / 3 : 0;
        v_shift = phi >= M_PI_2 || phi < -M_PI_2 ? eh : 0;

        uf = fmodf(phi, M_PI_2) / M_PI_2;
        vf = theta / M_PI_4;

        if (v_shift)
            uf = uf >= 0.f ? fmodf(uf - 1.f, 1.f) : fmodf(uf + 1.f, 1.f);

        uf = (uf * scalew + 1.f) * width  / 3.f;
        vf = (vf * scaleh + 1.f) * height / 4.f;
    } else {
        const float scalew = s->fin_pad > 0 ? 1.f - s->fin_pad / (width  / 3.f) : 1.f - s->in_pad;
        const float scaleh = s->fin_pad > 0 ? 1.f - s->fin_pad / (height / 4.f) : 1.f - s->in_pad;
        int v_offset = 0;

        ew = width  / 3;
        eh = height / 4;

        u_shift = s->ih_flip ? 0 : 2 * ew;

        if (theta <= 0.f && theta >= -M_PI_2 &&
            phi <= M_PI_2 && phi >= -M_PI_2) {
            uf = -vec[0] / vec[1];
            vf = -vec[2] / vec[1];
            v_shift = 0;
            v_offset = -eh;
        } else if (theta >= 0.f && theta <= M_PI_2 &&
                   phi <= M_PI_2 && phi >= -M_PI_2) {
            uf =  vec[0] / vec[1];
            vf = -vec[2] / vec[1];
            v_shift = height * 0.25f;
        } else if (theta <= 0.f && theta >= -M_PI_2) {
            uf =  vec[0] / vec[1];
            vf =  vec[2] / vec[1];
            v_shift = height * 0.5f;
            v_offset = -eh;
        } else {
            uf = -vec[0] / vec[1];
            vf =  vec[2] / vec[1];
            v_shift = height * 0.75f;
        }

        uf *= s->input_mirror_modifier[0] * s->input_mirror_modifier[1];
        vf *= s->input_mirror_modifier[1];

        uf = 0.5f * width / 3.f * (uf * scalew + 1.f);
        vf = height * 0.25f * (vf * scaleh + 1.f) + v_offset;
    }

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = u_shift + av_clip(ui + j - 1, 0, ew - 1);
            vs[i][j] = v_shift + av_clip(vi + i - 1, 0, eh - 1);
        }
    }

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in barrel split facebook's format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int barrelsplit_to_xyz(const V360Context *s,
                              int i, int j, int width, int height,
                              float *vec)
{
    const float x = (i + 0.5f) / width;
    const float y = (j + 0.5f) / height;
    float l_x, l_y, l_z;

    if (x < 2.f / 3.f) {
        const float scalew = s->fout_pad > 0 ? 1.f - s->fout_pad / (width * 2.f / 3.f) : 1.f - s->out_pad;
        const float scaleh = s->fout_pad > 0 ? 1.f - s->fout_pad / (height / 2.f) : 1.f - s->out_pad;

        const float back = floorf(y * 2.f);

        const float phi   = ((3.f / 2.f * x - 0.5f) / scalew - back) * M_PI;
        const float theta = (y - 0.25f - 0.5f * back) / scaleh * M_PI;

        const float sin_phi   = sinf(phi);
        const float cos_phi   = cosf(phi);
        const float sin_theta = sinf(theta);
        const float cos_theta = cosf(theta);

        l_x = cos_theta * sin_phi;
        l_y = sin_theta;
        l_z = cos_theta * cos_phi;
    } else {
        const float scalew = s->fout_pad > 0 ? 1.f - s->fout_pad / (width  / 3.f) : 1.f - s->out_pad;
        const float scaleh = s->fout_pad > 0 ? 1.f - s->fout_pad / (height / 4.f) : 1.f - s->out_pad;

        const int face = floorf(y * 4.f);
        float uf, vf;

        uf = x * 3.f - 2.f;

        switch (face) {
        case 0:
            vf = y * 2.f;
            uf = 1.f - uf;
            vf = 0.5f - vf;

            l_x = (0.5f - uf) / scalew;
            l_y = -0.5f;
            l_z = (0.5f - vf) / scaleh;
            break;
        case 1:
            vf = y * 2.f;
            uf = 1.f - uf;
            vf = 1.f - (vf - 0.5f);

            l_x = (0.5f - uf) / scalew;
            l_y =  0.5f;
            l_z = (-0.5f + vf) / scaleh;
            break;
        case 2:
            vf = y * 2.f - 0.5f;
            vf = 1.f - (1.f - vf);

            l_x = (0.5f - uf) / scalew;
            l_y = -0.5f;
            l_z = (0.5f - vf) / scaleh;
            break;
        case 3:
            vf = y * 2.f - 1.5f;

            l_x = (0.5f - uf) / scalew;
            l_y =  0.5f;
            l_z = (-0.5f + vf) / scaleh;
            break;
        }
    }

    vec[0] = l_x;
    vec[1] = l_y;
    vec[2] = l_z;

    normalize_vector(vec);

    return 1;
}

/**
 * Calculate 3D coordinates on sphere for corresponding frame position in tspyramid format.
 *
 * @param s filter private context
 * @param i horizontal position on frame [0, width)
 * @param j vertical position on frame [0, height)
 * @param width frame width
 * @param height frame height
 * @param vec coordinates on sphere
 */
static int tspyramid_to_xyz(const V360Context *s,
                            int i, int j, int width, int height,
                            float *vec)
{
    const float x = (i + 0.5f) / width;
    const float y = (j + 0.5f) / height;

    if (x < 0.5f) {
        vec[0] =  x * 4.f - 1.f;
        vec[1] = (y * 2.f - 1.f);
        vec[2] = 1.f;
    } else if (x >= 0.6875f && x < 0.8125f &&
               y >= 0.375f  && y < 0.625f) {
        vec[0] = -(x - 0.6875f) * 16.f + 1.f;
        vec[1] = (y - 0.375f) * 8.f - 1.f;
        vec[2] = -1.f;
    } else if (0.5f <= x && x < 0.6875f &&
               ((0.f <= y && y < 0.375f && y >= 2.f * (x - 0.5f)) ||
                (0.375f <= y && y < 0.625f) ||
                (0.625f <= y && y < 1.f && y <= 2.f * (1.f - x)))) {
        vec[0] =  1.f;
        vec[1] =  2.f * (y - 2.f * x + 1.f) / (3.f - 4.f * x) - 1.f;
        vec[2] = -2.f * (x - 0.5f) / 0.1875f + 1.f;
    } else if (0.8125f <= x && x < 1.f &&
               ((0.f <= y && y < 0.375f && x >= (1.f - y / 2.f)) ||
                (0.375f <= y && y < 0.625f) ||
                (0.625f <= y && y < 1.f && y <= (2.f * x - 1.f)))) {
        vec[0] = -1.f;
        vec[1] =  2.f * (y + 2.f * x - 2.f) / (4.f * x - 3.f) - 1.f;
        vec[2] =  2.f * (x - 0.8125f) / 0.1875f - 1.f;
    } else if (0.f <= y && y < 0.375f &&
               ((0.5f <= x && x < 0.8125f && y < 2.f * (x - 0.5f)) ||
                (0.6875f <= x && x < 0.8125f) ||
                (0.8125f <= x && x < 1.f && x < (1.f - y / 2.f)))) {
        vec[0] =  2.f * (1.f - x - 0.5f * y) / (0.5f - y) - 1.f;
        vec[1] = -1.f;
        vec[2] =  2.f * (0.375f - y) / 0.375f - 1.f;
    } else {
        vec[0] =  2.f * (0.5f - x + 0.5f * y) / (y - 0.5f) - 1.f;
        vec[1] =  1.f;
        vec[2] = -2.f * (1.f - y) / 0.375f + 1.f;
    }

    normalize_vector(vec);

    return 1;
}

/**
 * Calculate frame position in tspyramid format for corresponding 3D coordinates on sphere.
 *
 * @param s filter private context
 * @param vec coordinates on sphere
 * @param width frame width
 * @param height frame height
 * @param us horizontal coordinates for interpolation window
 * @param vs vertical coordinates for interpolation window
 * @param du horizontal relative coordinate
 * @param dv vertical relative coordinate
 */
static int xyz_to_tspyramid(const V360Context *s,
                            const float *vec, int width, int height,
                            int16_t us[4][4], int16_t vs[4][4], float *du, float *dv)
{
    float uf, vf;
    int ui, vi;
    int face;

    xyz_to_cube(s, vec, &uf, &vf, &face);

    uf = (uf + 1.f) * 0.5f;
    vf = (vf + 1.f) * 0.5f;

    switch (face) {
    case UP:
        uf = 0.1875f * vf - 0.375f * uf * vf - 0.125f * uf + 0.8125f;
        vf = 0.375f - 0.375f * vf;
        break;
    case FRONT:
        uf = 0.5f * uf;
        break;
    case DOWN:
        uf = 1.f - 0.1875f * vf - 0.5f * uf + 0.375f * uf * vf;
        vf = 1.f - 0.375f * vf;
        break;
    case LEFT:
        vf = 0.25f * vf + 0.75f * uf * vf - 0.375f * uf + 0.375f;
        uf = 0.1875f * uf + 0.8125f;
        break;
    case RIGHT:
        vf = 0.375f * uf - 0.75f * uf * vf + vf;
        uf = 0.1875f * uf + 0.5f;
        break;
    case BACK:
        uf = 0.125f * uf + 0.6875f;
        vf = 0.25f * vf + 0.375f;
        break;
    }

    uf *= width;
    vf *= height;

    ui = floorf(uf);
    vi = floorf(vf);

    *du = uf - ui;
    *dv = vf - vi;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            us[i][j] = reflectx(ui + j - 1, vi + i - 1, width, height);
            vs[i][j] = reflecty(vi + i - 1, height);
        }
    }

    return 1;
}

static void multiply_matrix(float c[3][3], const float a[3][3], const float b[3][3])
{
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            float sum = 0.f;

            for (int k = 0; k < 3; k++)
                sum += a[i][k] * b[k][j];

            c[i][j] = sum;
        }
    }
}

/**
 * Calculate rotation matrix for yaw/pitch/roll angles.
 */
static inline void calculate_rotation_matrix(float yaw, float pitch, float roll,
                                             float rot_mat[3][3],
                                             const int rotation_order[3])
{
    const float yaw_rad   = yaw   * M_PI / 180.f;
    const float pitch_rad = pitch * M_PI / 180.f;
    const float roll_rad  = roll  * M_PI / 180.f;

    const float sin_yaw   = sinf(yaw_rad);
    const float cos_yaw   = cosf(yaw_rad);
    const float sin_pitch = sinf(pitch_rad);
    const float cos_pitch = cosf(pitch_rad);
    const float sin_roll  = sinf(roll_rad);
    const float cos_roll  = cosf(roll_rad);

    float m[3][3][3];
    float temp[3][3];

    m[0][0][0] =  cos_yaw;  m[0][0][1] = 0;          m[0][0][2] =  sin_yaw;
    m[0][1][0] =  0;        m[0][1][1] = 1;          m[0][1][2] =  0;
    m[0][2][0] = -sin_yaw;  m[0][2][1] = 0;          m[0][2][2] =  cos_yaw;

    m[1][0][0] = 1;         m[1][0][1] = 0;          m[1][0][2] =  0;
    m[1][1][0] = 0;         m[1][1][1] = cos_pitch;  m[1][1][2] = -sin_pitch;
    m[1][2][0] = 0;         m[1][2][1] = sin_pitch;  m[1][2][2] =  cos_pitch;

    m[2][0][0] = cos_roll;  m[2][0][1] = -sin_roll;  m[2][0][2] =  0;
    m[2][1][0] = sin_roll;  m[2][1][1] =  cos_roll;  m[2][1][2] =  0;
    m[2][2][0] = 0;         m[2][2][1] =  0;         m[2][2][2] =  1;

    multiply_matrix(temp, m[rotation_order[0]], m[rotation_order[1]]);
    multiply_matrix(rot_mat, temp, m[rotation_order[2]]);
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

static inline void mirror(const float *modifier, float *vec)
{
    vec[0] *= modifier[0];
    vec[1] *= modifier[1];
    vec[2] *= modifier[2];
}

static int allocate_plane(V360Context *s, int sizeof_uv, int sizeof_ker, int sizeof_mask, int p)
{
    if (!s->u[p])
        s->u[p] = av_calloc(s->uv_linesize[p] * s->pr_height[p], sizeof_uv);
    if (!s->v[p])
        s->v[p] = av_calloc(s->uv_linesize[p] * s->pr_height[p], sizeof_uv);
    if (!s->u[p] || !s->v[p])
        return AVERROR(ENOMEM);
    if (sizeof_ker) {
        if (!s->ker[p])
            s->ker[p] = av_calloc(s->uv_linesize[p] * s->pr_height[p], sizeof_ker);
        if (!s->ker[p])
            return AVERROR(ENOMEM);
    }

    if (sizeof_mask && !p) {
        if (!s->mask)
            s->mask = av_calloc(s->pr_width[p] * s->pr_height[p], sizeof_mask);
        if (!s->mask)
            return AVERROR(ENOMEM);
    }

    return 0;
}

static void fov_from_dfov(int format, float d_fov, float w, float h, float *h_fov, float *v_fov)
{
    switch (format) {
    case ORTHOGRAPHIC:
        {
            const float d = 0.5f * hypotf(w, h);
            const float l = sinf(d_fov * M_PI / 360.f) / d;

            *h_fov = asinf(w * 0.5 * l) * 360.f / M_PI;
            *v_fov = asinf(h * 0.5 * l) * 360.f / M_PI;

            if (d_fov > 180.f) {
                *h_fov = 180.f - *h_fov;
                *v_fov = 180.f - *v_fov;
            }
        }
        break;
    case EQUISOLID:
        {
            const float d = 0.5f * hypotf(w, h);
            const float l = d / (sinf(d_fov * M_PI / 720.f));

            *h_fov = 2.f * asinf(w * 0.5f / l) * 360.f / M_PI;
            *v_fov = 2.f * asinf(h * 0.5f / l) * 360.f / M_PI;
        }
        break;
    case STEREOGRAPHIC:
        {
            const float d = 0.5f * hypotf(w, h);
            const float l = d / (tanf(d_fov * M_PI / 720.f));

            *h_fov = 2.f * atan2f(w * 0.5f, l) * 360.f / M_PI;
            *v_fov = 2.f * atan2f(h * 0.5f, l) * 360.f / M_PI;
        }
        break;
    case DUAL_FISHEYE:
        {
            const float d = 0.5f * hypotf(w * 0.5f, h);

            *h_fov = d / w * 2.f * d_fov;
            *v_fov = d / h * d_fov;
        }
        break;
    case FISHEYE:
        {
            const float d = 0.5f * hypotf(w, h);

            *h_fov = d / w * d_fov;
            *v_fov = d / h * d_fov;
        }
        break;
    case FLAT:
    default:
        {
            const float da = tanf(0.5f * FFMIN(d_fov, 359.f) * M_PI / 180.f);
            const float d = hypotf(w, h);

            *h_fov = atan2f(da * w, d) * 360.f / M_PI;
            *v_fov = atan2f(da * h, d) * 360.f / M_PI;

            if (*h_fov < 0.f)
                *h_fov += 360.f;
            if (*v_fov < 0.f)
                *v_fov += 360.f;
        }
        break;
    }
}

static void set_dimensions(int *outw, int *outh, int w, int h, const AVPixFmtDescriptor *desc)
{
    outw[1] = outw[2] = FF_CEIL_RSHIFT(w, desc->log2_chroma_w);
    outw[0] = outw[3] = w;
    outh[1] = outh[2] = FF_CEIL_RSHIFT(h, desc->log2_chroma_h);
    outh[0] = outh[3] = h;
}

// Calculate remap data
static av_always_inline int v360_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    V360Context *s = ctx->priv;

    for (int p = 0; p < s->nb_allocated; p++) {
        const int max_value = s->max_value;
        const int width = s->pr_width[p];
        const int uv_linesize = s->uv_linesize[p];
        const int height = s->pr_height[p];
        const int in_width = s->inplanewidth[p];
        const int in_height = s->inplaneheight[p];
        const int slice_start = (height *  jobnr     ) / nb_jobs;
        const int slice_end   = (height * (jobnr + 1)) / nb_jobs;
        float du, dv;
        float vec[3];
        XYRemap rmap;

        for (int j = slice_start; j < slice_end; j++) {
            for (int i = 0; i < width; i++) {
                int16_t *u = s->u[p] + (j * uv_linesize + i) * s->elements;
                int16_t *v = s->v[p] + (j * uv_linesize + i) * s->elements;
                int16_t *ker = s->ker[p] + (j * uv_linesize + i) * s->elements;
                uint8_t *mask8 = p ? NULL : s->mask + (j * s->pr_width[0] + i);
                uint16_t *mask16 = p ? NULL : (uint16_t *)s->mask + (j * s->pr_width[0] + i);
                int in_mask, out_mask;

                if (s->out_transpose)
                    out_mask = s->out_transform(s, j, i, height, width, vec);
                else
                    out_mask = s->out_transform(s, i, j, width, height, vec);
                av_assert1(!isnan(vec[0]) && !isnan(vec[1]) && !isnan(vec[2]));
                rotate(s->rot_mat, vec);
                av_assert1(!isnan(vec[0]) && !isnan(vec[1]) && !isnan(vec[2]));
                normalize_vector(vec);
                mirror(s->output_mirror_modifier, vec);
                if (s->in_transpose)
                    in_mask = s->in_transform(s, vec, in_height, in_width, rmap.v, rmap.u, &du, &dv);
                else
                    in_mask = s->in_transform(s, vec, in_width, in_height, rmap.u, rmap.v, &du, &dv);
                av_assert1(!isnan(du) && !isnan(dv));
                s->calculate_kernel(du, dv, &rmap, u, v, ker);

                if (!p && s->mask) {
                    if (s->mask_size == 1) {
                        mask8[0] = 255 * (out_mask & in_mask);
                    } else {
                        mask16[0] = max_value * (out_mask & in_mask);
                    }
                }
            }
        }
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    V360Context *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    const int depth = desc->comp[0].depth;
    const int sizeof_mask = s->mask_size = (depth + 7) >> 3;
    int sizeof_uv;
    int sizeof_ker;
    int err;
    int h, w;
    int in_offset_h, in_offset_w;
    int out_offset_h, out_offset_w;
    float hf, wf;
    int (*prepare_out)(AVFilterContext *ctx);
    int have_alpha;

    s->max_value = (1 << depth) - 1;
    s->input_mirror_modifier[0] = s->ih_flip ? -1.f : 1.f;
    s->input_mirror_modifier[1] = s->iv_flip ? -1.f : 1.f;

    switch (s->interp) {
    case NEAREST:
        s->calculate_kernel = nearest_kernel;
        s->remap_slice = depth <= 8 ? remap1_8bit_slice : remap1_16bit_slice;
        s->elements = 1;
        sizeof_uv = sizeof(int16_t) * s->elements;
        sizeof_ker = 0;
        break;
    case BILINEAR:
        s->calculate_kernel = bilinear_kernel;
        s->remap_slice = depth <= 8 ? remap2_8bit_slice : remap2_16bit_slice;
        s->elements = 2 * 2;
        sizeof_uv = sizeof(int16_t) * s->elements;
        sizeof_ker = sizeof(int16_t) * s->elements;
        break;
    case LAGRANGE9:
        s->calculate_kernel = lagrange_kernel;
        s->remap_slice = depth <= 8 ? remap3_8bit_slice : remap3_16bit_slice;
        s->elements = 3 * 3;
        sizeof_uv = sizeof(int16_t) * s->elements;
        sizeof_ker = sizeof(int16_t) * s->elements;
        break;
    case BICUBIC:
        s->calculate_kernel = bicubic_kernel;
        s->remap_slice = depth <= 8 ? remap4_8bit_slice : remap4_16bit_slice;
        s->elements = 4 * 4;
        sizeof_uv = sizeof(int16_t) * s->elements;
        sizeof_ker = sizeof(int16_t) * s->elements;
        break;
    case LANCZOS:
        s->calculate_kernel = lanczos_kernel;
        s->remap_slice = depth <= 8 ? remap4_8bit_slice : remap4_16bit_slice;
        s->elements = 4 * 4;
        sizeof_uv = sizeof(int16_t) * s->elements;
        sizeof_ker = sizeof(int16_t) * s->elements;
        break;
    case SPLINE16:
        s->calculate_kernel = spline16_kernel;
        s->remap_slice = depth <= 8 ? remap4_8bit_slice : remap4_16bit_slice;
        s->elements = 4 * 4;
        sizeof_uv = sizeof(int16_t) * s->elements;
        sizeof_ker = sizeof(int16_t) * s->elements;
        break;
    case GAUSSIAN:
        s->calculate_kernel = gaussian_kernel;
        s->remap_slice = depth <= 8 ? remap4_8bit_slice : remap4_16bit_slice;
        s->elements = 4 * 4;
        sizeof_uv = sizeof(int16_t) * s->elements;
        sizeof_ker = sizeof(int16_t) * s->elements;
        break;
    default:
        av_assert0(0);
    }

    ff_v360_init(s, depth);

    for (int order = 0; order < NB_RORDERS; order++) {
        const char c = s->rorder[order];
        int rorder;

        if (c == '\0') {
            av_log(ctx, AV_LOG_WARNING,
                   "Incomplete rorder option. Direction for all 3 rotation orders should be specified. Switching to default rorder.\n");
            s->rotation_order[0] = YAW;
            s->rotation_order[1] = PITCH;
            s->rotation_order[2] = ROLL;
            break;
        }

        rorder = get_rorder(c);
        if (rorder == -1) {
            av_log(ctx, AV_LOG_WARNING,
                   "Incorrect rotation order symbol '%c' in rorder option. Switching to default rorder.\n", c);
            s->rotation_order[0] = YAW;
            s->rotation_order[1] = PITCH;
            s->rotation_order[2] = ROLL;
            break;
        }

        s->rotation_order[order] = rorder;
    }

    switch (s->in_stereo) {
    case STEREO_2D:
        w = inlink->w;
        h = inlink->h;
        in_offset_w = in_offset_h = 0;
        break;
    case STEREO_SBS:
        w = inlink->w / 2;
        h = inlink->h;
        in_offset_w = w;
        in_offset_h = 0;
        break;
    case STEREO_TB:
        w = inlink->w;
        h = inlink->h / 2;
        in_offset_w = 0;
        in_offset_h = h;
        break;
    default:
        av_assert0(0);
    }

    set_dimensions(s->inplanewidth, s->inplaneheight, w, h, desc);
    set_dimensions(s->in_offset_w, s->in_offset_h, in_offset_w, in_offset_h, desc);

    s->in_width = s->inplanewidth[0];
    s->in_height = s->inplaneheight[0];

    if (s->id_fov > 0.f)
        fov_from_dfov(s->in, s->id_fov, w, h, &s->ih_fov, &s->iv_fov);

    if (s->in_transpose)
        FFSWAP(int, s->in_width, s->in_height);

    switch (s->in) {
    case EQUIRECTANGULAR:
        s->in_transform = xyz_to_equirect;
        err = 0;
        wf = w;
        hf = h;
        break;
    case CUBEMAP_3_2:
        s->in_transform = xyz_to_cube3x2;
        err = prepare_cube_in(ctx);
        wf = w / 3.f * 4.f;
        hf = h;
        break;
    case CUBEMAP_1_6:
        s->in_transform = xyz_to_cube1x6;
        err = prepare_cube_in(ctx);
        wf = w * 4.f;
        hf = h / 3.f;
        break;
    case CUBEMAP_6_1:
        s->in_transform = xyz_to_cube6x1;
        err = prepare_cube_in(ctx);
        wf = w / 3.f * 2.f;
        hf = h * 2.f;
        break;
    case EQUIANGULAR:
        s->in_transform = xyz_to_eac;
        err = prepare_eac_in(ctx);
        wf = w;
        hf = h / 9.f * 8.f;
        break;
    case FLAT:
        s->in_transform = xyz_to_flat;
        err = prepare_flat_in(ctx);
        wf = w;
        hf = h;
        break;
    case PERSPECTIVE:
        av_log(ctx, AV_LOG_ERROR, "Supplied format is not accepted as input.\n");
        return AVERROR(EINVAL);
    case DUAL_FISHEYE:
        s->in_transform = xyz_to_dfisheye;
        err = prepare_fisheye_in(ctx);
        wf = w;
        hf = h;
        break;
    case BARREL:
        s->in_transform = xyz_to_barrel;
        err = 0;
        wf = w / 5.f * 4.f;
        hf = h;
        break;
    case STEREOGRAPHIC:
        s->in_transform = xyz_to_stereographic;
        err = prepare_stereographic_in(ctx);
        wf = w;
        hf = h / 2.f;
        break;
    case MERCATOR:
        s->in_transform = xyz_to_mercator;
        err = 0;
        wf = w;
        hf = h / 2.f;
        break;
    case BALL:
        s->in_transform = xyz_to_ball;
        err = 0;
        wf = w;
        hf = h / 2.f;
        break;
    case HAMMER:
        s->in_transform = xyz_to_hammer;
        err = 0;
        wf = w;
        hf = h;
        break;
    case SINUSOIDAL:
        s->in_transform = xyz_to_sinusoidal;
        err = 0;
        wf = w;
        hf = h;
        break;
    case FISHEYE:
        s->in_transform = xyz_to_fisheye;
        err = prepare_fisheye_in(ctx);
        wf = w * 2;
        hf = h;
        break;
    case PANNINI:
        s->in_transform = xyz_to_pannini;
        err = 0;
        wf = w;
        hf = h;
        break;
    case CYLINDRICAL:
        s->in_transform = xyz_to_cylindrical;
        err = prepare_cylindrical_in(ctx);
        wf = w;
        hf = h * 2.f;
        break;
    case TETRAHEDRON:
        s->in_transform = xyz_to_tetrahedron;
        err = 0;
        wf = w;
        hf = h;
        break;
    case BARREL_SPLIT:
        s->in_transform = xyz_to_barrelsplit;
        err = 0;
        wf = w * 4.f / 3.f;
        hf = h;
        break;
    case TSPYRAMID:
        s->in_transform = xyz_to_tspyramid;
        err = 0;
        wf = w;
        hf = h;
        break;
    case HEQUIRECTANGULAR:
        s->in_transform = xyz_to_hequirect;
        err = 0;
        wf = w * 2.f;
        hf = h;
        break;
    case EQUISOLID:
        s->in_transform = xyz_to_equisolid;
        err = prepare_equisolid_in(ctx);
        wf = w;
        hf = h / 2.f;
        break;
    case ORTHOGRAPHIC:
        s->in_transform = xyz_to_orthographic;
        err = prepare_orthographic_in(ctx);
        wf = w;
        hf = h / 2.f;
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
        s->out_transform = equirect_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf);
        h = lrintf(hf);
        break;
    case CUBEMAP_3_2:
        s->out_transform = cube3x2_to_xyz;
        prepare_out = prepare_cube_out;
        w = lrintf(wf / 4.f * 3.f);
        h = lrintf(hf);
        break;
    case CUBEMAP_1_6:
        s->out_transform = cube1x6_to_xyz;
        prepare_out = prepare_cube_out;
        w = lrintf(wf / 4.f);
        h = lrintf(hf * 3.f);
        break;
    case CUBEMAP_6_1:
        s->out_transform = cube6x1_to_xyz;
        prepare_out = prepare_cube_out;
        w = lrintf(wf / 2.f * 3.f);
        h = lrintf(hf / 2.f);
        break;
    case EQUIANGULAR:
        s->out_transform = eac_to_xyz;
        prepare_out = prepare_eac_out;
        w = lrintf(wf);
        h = lrintf(hf / 8.f * 9.f);
        break;
    case FLAT:
        s->out_transform = flat_to_xyz;
        prepare_out = prepare_flat_out;
        w = lrintf(wf);
        h = lrintf(hf);
        break;
    case DUAL_FISHEYE:
        s->out_transform = dfisheye_to_xyz;
        prepare_out = prepare_fisheye_out;
        w = lrintf(wf);
        h = lrintf(hf);
        break;
    case BARREL:
        s->out_transform = barrel_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf / 4.f * 5.f);
        h = lrintf(hf);
        break;
    case STEREOGRAPHIC:
        s->out_transform = stereographic_to_xyz;
        prepare_out = prepare_stereographic_out;
        w = lrintf(wf);
        h = lrintf(hf * 2.f);
        break;
    case MERCATOR:
        s->out_transform = mercator_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf);
        h = lrintf(hf * 2.f);
        break;
    case BALL:
        s->out_transform = ball_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf);
        h = lrintf(hf * 2.f);
        break;
    case HAMMER:
        s->out_transform = hammer_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf);
        h = lrintf(hf);
        break;
    case SINUSOIDAL:
        s->out_transform = sinusoidal_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf);
        h = lrintf(hf);
        break;
    case FISHEYE:
        s->out_transform = fisheye_to_xyz;
        prepare_out = prepare_fisheye_out;
        w = lrintf(wf * 0.5f);
        h = lrintf(hf);
        break;
    case PANNINI:
        s->out_transform = pannini_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf);
        h = lrintf(hf);
        break;
    case CYLINDRICAL:
        s->out_transform = cylindrical_to_xyz;
        prepare_out = prepare_cylindrical_out;
        w = lrintf(wf);
        h = lrintf(hf * 0.5f);
        break;
    case PERSPECTIVE:
        s->out_transform = perspective_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf / 2.f);
        h = lrintf(hf);
        break;
    case TETRAHEDRON:
        s->out_transform = tetrahedron_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf);
        h = lrintf(hf);
        break;
    case BARREL_SPLIT:
        s->out_transform = barrelsplit_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf / 4.f * 3.f);
        h = lrintf(hf);
        break;
    case TSPYRAMID:
        s->out_transform = tspyramid_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf);
        h = lrintf(hf);
        break;
    case HEQUIRECTANGULAR:
        s->out_transform = hequirect_to_xyz;
        prepare_out = NULL;
        w = lrintf(wf / 2.f);
        h = lrintf(hf);
        break;
    case EQUISOLID:
        s->out_transform = equisolid_to_xyz;
        prepare_out = prepare_equisolid_out;
        w = lrintf(wf);
        h = lrintf(hf * 2.f);
        break;
    case ORTHOGRAPHIC:
        s->out_transform = orthographic_to_xyz;
        prepare_out = prepare_orthographic_out;
        w = lrintf(wf);
        h = lrintf(hf * 2.f);
        break;
    default:
        av_log(ctx, AV_LOG_ERROR, "Specified output format is not handled.\n");
        return AVERROR_BUG;
    }

    // Override resolution with user values if specified
    if (s->width > 0 && s->height <= 0 && s->h_fov > 0.f && s->v_fov > 0.f &&
        s->out == FLAT && s->d_fov == 0.f) {
        w = s->width;
        h = w / tanf(s->h_fov * M_PI / 360.f) * tanf(s->v_fov * M_PI / 360.f);
    } else if (s->width <= 0 && s->height > 0 && s->h_fov > 0.f && s->v_fov > 0.f &&
        s->out == FLAT && s->d_fov == 0.f) {
        h = s->height;
        w = h / tanf(s->v_fov * M_PI / 360.f) * tanf(s->h_fov * M_PI / 360.f);
    } else if (s->width > 0 && s->height > 0) {
        w = s->width;
        h = s->height;
    } else if (s->width > 0 || s->height > 0) {
        av_log(ctx, AV_LOG_ERROR, "Both width and height values should be specified.\n");
        return AVERROR(EINVAL);
    } else {
        if (s->out_transpose)
            FFSWAP(int, w, h);

        if (s->in_transpose)
            FFSWAP(int, w, h);
    }

    s->width  = w;
    s->height = h;

    if (s->d_fov > 0.f)
        fov_from_dfov(s->out, s->d_fov, w, h, &s->h_fov, &s->v_fov);

    if (prepare_out) {
        err = prepare_out(ctx);
        if (err != 0)
            return err;
    }

    set_dimensions(s->pr_width, s->pr_height, w, h, desc);

    switch (s->out_stereo) {
    case STEREO_2D:
        out_offset_w = out_offset_h = 0;
        break;
    case STEREO_SBS:
        out_offset_w = w;
        out_offset_h = 0;
        w *= 2;
        break;
    case STEREO_TB:
        out_offset_w = 0;
        out_offset_h = h;
        h *= 2;
        break;
    default:
        av_assert0(0);
    }

    set_dimensions(s->out_offset_w, s->out_offset_h, out_offset_w, out_offset_h, desc);
    set_dimensions(s->planewidth, s->planeheight, w, h, desc);

    for (int i = 0; i < 4; i++)
        s->uv_linesize[i] = FFALIGN(s->pr_width[i], 8);

    outlink->h = h;
    outlink->w = w;

    s->nb_planes = av_pix_fmt_count_planes(inlink->format);
    have_alpha   = !!(desc->flags & AV_PIX_FMT_FLAG_ALPHA);

    if (desc->log2_chroma_h == desc->log2_chroma_w && desc->log2_chroma_h == 0) {
        s->nb_allocated = 1;
        s->map[0] = s->map[1] = s->map[2] = s->map[3] = 0;
    } else {
        s->nb_allocated = 2;
        s->map[0] = s->map[3] = 0;
        s->map[1] = s->map[2] = 1;
    }

    for (int i = 0; i < s->nb_allocated; i++) {
        err = allocate_plane(s, sizeof_uv, sizeof_ker, sizeof_mask * have_alpha * s->alpha, i);
        if (err < 0)
            return err;
    }

    calculate_rotation_matrix(s->yaw, s->pitch, s->roll, s->rot_mat, s->rotation_order);
    set_mirror_modifier(s->h_flip, s->v_flip, s->d_flip, s->output_mirror_modifier);

    ctx->internal->execute(ctx, v360_slice, NULL, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

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

    td.in = in;
    td.out = out;

    ctx->internal->execute(ctx, s->remap_slice, &td, NULL, FFMIN(outlink->h, ff_filter_get_nb_threads(ctx)));

    av_frame_free(&in);
    return ff_filter_frame(outlink, out);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    int ret;

    ret = ff_filter_process_command(ctx, cmd, args, res, res_len, flags);
    if (ret < 0)
        return ret;

    return config_output(ctx->outputs[0]);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    V360Context *s = ctx->priv;

    for (int p = 0; p < s->nb_allocated; p++) {
        av_freep(&s->u[p]);
        av_freep(&s->v[p]);
        av_freep(&s->ker[p]);
    }
    av_freep(&s->mask);
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
    .process_command = process_command,
};
