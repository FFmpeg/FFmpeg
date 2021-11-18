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

#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

#define R 0
#define G 1
#define B 2

#define REDS     0
#define YELLOWS  1
#define GREENS   2
#define CYANS    3
#define BLUES    4
#define MAGENTAS 5

#define RED     (1 << REDS)
#define YELLOW  (1 << YELLOWS)
#define GREEN   (1 << GREENS)
#define CYAN    (1 << CYANS)
#define BLUE    (1 << BLUES)
#define MAGENTA (1 << MAGENTAS)
#define ALL      0x3F

typedef struct HueSaturationContext {
    const AVClass *class;

    float hue;
    float saturation;
    float intensity;
    float strength;
    float rlw, glw, blw;
    int lightness;
    int colors;

    int depth;
    int planewidth[4];
    int planeheight[4];

    float matrix[4][4];
    int64_t imatrix[4][4];

    int bpp;
    int step;
    uint8_t rgba_map[4];

    int (*do_slice[2])(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} HueSaturationContext;

#define DENOM 0x10000

static inline void get_triplet(int64_t m[4][4], int *r, int *g, int *b)
{
    const int ir = *r, ig = *g, ib = *b;

    *r = (ir * m[0][0] + ig * m[1][0] + ib * m[2][0] /*+ m[3][0]*/) >> 16;
    *g = (ir * m[0][1] + ig * m[1][1] + ib * m[2][1] /*+ m[3][1]*/) >> 16;
    *b = (ir * m[0][2] + ig * m[1][2] + ib * m[2][2] /*+ m[3][2]*/) >> 16;
}

#define FAST_DIV255(x) ((((x) + 128) * 257) >> 16)

static inline int lerpi8(int v0, int v1, int f, int max)
{
    return v0 + FAST_DIV255((v1 - v0) * f);
}

static inline int lerpi16(int v0, int v1, int f, int max)
{
    return v0 + (v1 - v0) * (int64_t)f / max;
}

#define HUESATURATION(name, type, clip, xall)                        \
static int do_slice_##name##_##xall(AVFilterContext *ctx,            \
                                          void *arg,                 \
                                          int jobnr, int nb_jobs)    \
{                                                                    \
    HueSaturationContext *s = ctx->priv;                             \
    AVFrame *frame = arg;                                            \
    const int imax = (1 << name) - 1;                                \
    const float strength = s->strength;                              \
    const int colors = s->colors;                                    \
    const int step = s->step;                                        \
    const int width = frame->width;                                  \
    const int process_h = frame->height;                             \
    const int slice_start = (process_h *  jobnr   ) / nb_jobs;       \
    const int slice_end   = (process_h * (jobnr+1)) / nb_jobs;       \
    const int linesize = frame->linesize[0] / sizeof(type);          \
    type *row = (type *)frame->data[0] + linesize * slice_start;     \
    const uint8_t offset_r = s->rgba_map[R];                         \
    const uint8_t offset_g = s->rgba_map[G];                         \
    const uint8_t offset_b = s->rgba_map[B];                         \
    type *dst_r = row + offset_r;                                    \
    type *dst_g = row + offset_g;                                    \
    type *dst_b = row + offset_b;                                    \
                                                                     \
    for (int y = slice_start; y < slice_end; y++) {                  \
        for (int x = 0; x < width * step; x += step) {               \
            int ir, ig, ib, ro, go, bo;                              \
                                                                     \
            ir = ro = dst_r[x];                                      \
            ig = go = dst_g[x];                                      \
            ib = bo = dst_b[x];                                      \
                                                                     \
            if (xall) {                                              \
                get_triplet(s->imatrix, &ir, &ig, &ib);              \
            } else {                                                 \
                const int min = FFMIN3(ir, ig, ib);                  \
                const int max = FFMAX3(ir, ig, ib);                  \
                const int flags = (ir == max) << REDS                \
                                | (ir == min) << CYANS               \
                                | (ig == max) << GREENS              \
                                | (ig == min) << MAGENTAS            \
                                | (ib == max) << BLUES               \
                                | (ib == min) << YELLOWS;            \
                if (colors & flags) {                                \
                    int f = 0;                                       \
                                                                     \
                    if (colors & RED)                                \
                        f = FFMAX(f, ir - FFMAX(ig, ib));            \
                    if (colors & YELLOW)                             \
                        f = FFMAX(f, FFMIN(ir, ig) - ib);            \
                    if (colors & GREEN)                              \
                        f = FFMAX(f, ig - FFMAX(ir, ib));            \
                    if (colors & CYAN)                               \
                        f = FFMAX(f, FFMIN(ig, ib) - ir);            \
                    if (colors & BLUE)                               \
                        f = FFMAX(f, ib - FFMAX(ir, ig));            \
                    if (colors & MAGENTA)                            \
                        f = FFMAX(f, FFMIN(ir, ib) - ig);            \
                    f = FFMIN(f * strength, imax);                   \
                    get_triplet(s->imatrix, &ir, &ig, &ib);          \
                    ir = lerpi##name(ro, ir, f, imax);               \
                    ig = lerpi##name(go, ig, f, imax);               \
                    ib = lerpi##name(bo, ib, f, imax);               \
                }                                                    \
            }                                                        \
                                                                     \
            dst_r[x] = clip(ir);                                     \
            dst_g[x] = clip(ig);                                     \
            dst_b[x] = clip(ib);                                     \
        }                                                            \
                                                                     \
        dst_r += linesize;                                           \
        dst_g += linesize;                                           \
        dst_b += linesize;                                           \
    }                                                                \
                                                                     \
    return 0;                                                        \
}

HUESATURATION(8,  uint8_t,  av_clip_uint8, 0)
HUESATURATION(16, uint16_t, av_clip_uint16, 0)

HUESATURATION(8,  uint8_t,  av_clip_uint8, 1)
HUESATURATION(16, uint16_t, av_clip_uint16, 1)

static void identity_matrix(float matrix[4][4])
{
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            matrix[y][x] = y == x;
}

static void matrix_multiply(float a[4][4], float b[4][4], float c[4][4])
{
    float temp[4][4];

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++) {
            temp[y][x] = b[y][0] * a[0][x]
                       + b[y][1] * a[1][x]
                       + b[y][2] * a[2][x]
                       + b[y][3] * a[3][x];
        }
    }

    for (int y = 0; y < 4; y++) {
        for (int x = 0; x < 4; x++)
            c[y][x] = temp[y][x];
    }
}

static void colorscale_matrix(float matrix[4][4], float r, float g, float b)
{
    float temp[4][4];

    temp[0][0] = r;   temp[0][1] = 0.f; temp[0][2] = 0.f; temp[0][3] = 0.f;
    temp[1][0] = 0.f; temp[1][1] = g;   temp[1][2] = 0.f; temp[1][3] = 0.f;
    temp[2][0] = 0.f; temp[2][1] = 0.f; temp[2][2] = b;   temp[2][3] = 0.f;
    temp[3][0] = 0.f; temp[3][1] = 0.f; temp[3][2] = 0.f; temp[3][3] = 1.f;

    matrix_multiply(temp, matrix, matrix);
}

static void saturation_matrix(float matrix[4][4], float saturation,
                              float rlw, float glw, float blw)
{
    float s = 1.f - saturation;
    float a = s * rlw + saturation;
    float b = s * rlw;
    float c = s * rlw;
    float d = s * glw;
    float e = s * glw + saturation;
    float f = s * glw;
    float g = s * blw;
    float h = s * blw;
    float i = s * blw + saturation;
    float m[4][4];

    m[0][0] = a;   m[0][1] = b;   m[0][2] = c;   m[0][3] = 0.f;
    m[1][0] = d;   m[1][1] = e;   m[1][2] = f;   m[1][3] = 0.f;
    m[2][0] = g;   m[2][1] = h;   m[2][2] = i;   m[2][3] = 0.f;
    m[3][0] = 0.f; m[3][1] = 0.f; m[3][2] = 0.f; m[3][3] = 1.f;

    matrix_multiply(m, matrix, matrix);
}

static void matrix2imatrix(float matrix[4][4], int64_t imatrix[4][4])
{
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
            imatrix[y][x] = lrintf(matrix[y][x] * DENOM);
}

static void x_rotate_matrix(float matrix[4][4], float rs, float rc)
{
    float m[4][4];

    m[0][0] = 1.f; m[0][1] = 0.f; m[0][2] = 0.f; m[0][3] = 0.f;
    m[1][0] = 0.f; m[1][1] = rc;  m[1][2] = rs;  m[1][3] = 0.f;
    m[2][0] = 0.f; m[2][1] = -rs; m[2][2] = rc;  m[2][3] = 0.f;
    m[3][0] = 0.f; m[3][1] = 0.f; m[3][2] = 0.f; m[3][3] = 1.f;

    matrix_multiply(m, matrix, matrix);
}

static void y_rotate_matrix(float matrix[4][4], float rs, float rc)
{
    float m[4][4];

    m[0][0] = rc;  m[0][1] = 0.f; m[0][2] = -rs; m[0][3] = 0.f;
    m[1][0] = 0.f; m[1][1] = 1.f; m[1][2] = 0.f; m[1][3] = 0.f;
    m[2][0] = rs;  m[2][1] = 0.f; m[2][2] = rc;  m[2][3] = 0.f;
    m[3][0] = 0.f; m[3][1] = 0.f; m[3][2] = 0.f; m[3][3] = 1.f;

    matrix_multiply(m, matrix, matrix);
}

static void z_rotate_matrix(float matrix[4][4], float rs, float rc)
{
    float m[4][4];

    m[0][0] = rc;  m[0][1] = rs;  m[0][2] = 0.f; m[0][3] = 0.f;
    m[1][0] = -rs; m[1][1] = rc;  m[1][2] = 0.f; m[1][3] = 0.f;
    m[2][0] = 0.f; m[2][1] = 0.f; m[2][2] = 1.f; m[2][3] = 0.f;
    m[3][0] = 0.f; m[3][1] = 0.f; m[3][2] = 0.f; m[3][3] = 1.f;

    matrix_multiply(m, matrix, matrix);
}

static void z_shear_matrix(float matrix[4][4], float dx, float dy)
{
    float m[4][4];

    m[0][0] = 1.f; m[0][1] = 0.f; m[0][2] = dx;  m[0][3] = 0.f;
    m[1][0] = 0.f; m[1][1] = 1.f; m[1][2] = dy;  m[1][3] = 0.f;
    m[2][0] = 0.f; m[2][1] = 0.f; m[2][2] = 1.f; m[2][3] = 0.f;
    m[3][0] = 0.f; m[3][1] = 0.f; m[3][2] = 0.f; m[3][3] = 1.f;

    matrix_multiply(m, matrix, matrix);
}

static void transform_point(float matrix[4][4],
                            float x, float y, float z,
                            float *tx, float *ty, float *tz)
{
    x = y;
    *tx = x * matrix[0][0] + y * matrix[1][0] + z * matrix[2][0] + matrix[3][0];
    *ty = x * matrix[0][1] + y * matrix[1][1] + z * matrix[2][1] + matrix[3][1];
    *tz = x * matrix[0][2] + y * matrix[1][2] + z * matrix[2][2] + matrix[3][2];
}

static void hue_rotate_matrix(float matrix[4][4], float rotation,
                              float rlw, float glw, float blw)
{
    float mag, lx, ly, lz;
    float xrs, xrc;
    float yrs, yrc;
    float zrs, zrc;
    float zsx, zsy;

    mag = M_SQRT2;
    xrs = 1.f / mag;
    xrc = 1.f / mag;
    x_rotate_matrix(matrix, xrs, xrc);

    mag = sqrtf(3.f);
    yrs = -1.f / mag;
    yrc = M_SQRT2 / mag;
    y_rotate_matrix(matrix, yrs, yrc);

    transform_point(matrix, rlw, glw, blw, &lx, &ly, &lz);
    zsx = lx / lz;
    zsy = ly / lz;
    z_shear_matrix(matrix, zsx, zsy);

    zrs = sinf(rotation * M_PI / 180.f);
    zrc = cosf(rotation * M_PI / 180.f);
    z_rotate_matrix(matrix, zrs, zrc);

    z_shear_matrix(matrix, -zsx, -zsy);

    y_rotate_matrix(matrix, -yrs, yrc);
    x_rotate_matrix(matrix, -xrs, xrc);
}

static void shue_rotate_matrix(float m[4][4], float rotation)
{
    float xrs, xrc, yrs, yrc, zrs, zrc, mag;

    mag = M_SQRT2;
    xrs = 1.f / mag;
    xrc = 1.f / mag;
    x_rotate_matrix(m, xrs, xrc);

    mag = sqrtf(3.f);
    yrs = -1.f / mag;
    yrc = M_SQRT2 / mag;
    y_rotate_matrix(m, yrs, yrc);

    zrs = sinf(rotation * M_PI / 180.f);
    zrc = cosf(rotation * M_PI / 180.f);
    z_rotate_matrix(m, zrs, zrc);

    y_rotate_matrix(m, -yrs, yrc);
    x_rotate_matrix(m, -xrs, xrc);
}

static void init_matrix(HueSaturationContext *s)
{
    float i = 1.f + s->intensity;
    float saturation = 1.f + s->saturation;
    float hue = s->hue;

    identity_matrix(s->matrix);
    colorscale_matrix(s->matrix, i, i, i);
    saturation_matrix(s->matrix, saturation,
                      s->rlw, s->glw, s->blw);

    if (s->lightness)
        hue_rotate_matrix(s->matrix, hue,
                          s->rlw, s->glw, s->blw);
    else
        shue_rotate_matrix(s->matrix, hue);

    matrix2imatrix(s->matrix, s->imatrix);
}

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    AVFilterContext *ctx = inlink->dst;
    HueSaturationContext *s = ctx->priv;

    init_matrix(s);

    ff_filter_execute(ctx, s->do_slice[(s->strength >= 99.f) && (s->colors == ALL)], frame, NULL,
                      FFMIN(s->planeheight[1], ff_filter_get_nb_threads(ctx)));

    return ff_filter_frame(ctx->outputs[0], frame);
}

static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
    AV_PIX_FMT_RGBA,  AV_PIX_FMT_BGRA,
    AV_PIX_FMT_ABGR,  AV_PIX_FMT_ARGB,
    AV_PIX_FMT_0BGR,  AV_PIX_FMT_0RGB,
    AV_PIX_FMT_RGB0,  AV_PIX_FMT_BGR0,
    AV_PIX_FMT_RGB48,  AV_PIX_FMT_BGR48,
    AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
    AV_PIX_FMT_NONE
};

static av_cold int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    HueSaturationContext *s = ctx->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);

    s->depth = desc->comp[0].depth;
    s->bpp = s->depth >> 3;
    s->step = av_get_padded_bits_per_pixel(desc) >> (3 + (s->bpp == 2));
    ff_fill_rgba_map(s->rgba_map, inlink->format);

    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    s->do_slice[0] = s->depth <= 8 ? do_slice_8_0 : do_slice_16_0;
    s->do_slice[1] = s->depth <= 8 ? do_slice_8_1 : do_slice_16_1;

    return 0;
}

static const AVFilterPad huesaturation_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .filter_frame   = filter_frame,
        .config_props   = config_input,
    },
};

static const AVFilterPad huesaturation_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

#define OFFSET(x) offsetof(HueSaturationContext, x)
#define VF AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption huesaturation_options[] = {
    { "hue",        "set the hue shift",               OFFSET(hue),        AV_OPT_TYPE_FLOAT, {.dbl=0},-180, 180, VF },
    { "saturation", "set the saturation shift",        OFFSET(saturation), AV_OPT_TYPE_FLOAT, {.dbl=0},  -1,   1, VF },
    { "intensity",  "set the intensity shift",         OFFSET(intensity),  AV_OPT_TYPE_FLOAT, {.dbl=0},  -1,   1, VF },
    { "colors",     "set colors range",                OFFSET(colors),     AV_OPT_TYPE_FLAGS, {.i64=ALL},     0,ALL,VF, "colors" },
    {  "r",         "set reds",                        0,                  AV_OPT_TYPE_CONST, {.i64=RED},     0, 0, VF, "colors" },
    {  "y",         "set yellows",                     0,                  AV_OPT_TYPE_CONST, {.i64=YELLOW},  0, 0, VF, "colors" },
    {  "g",         "set greens",                      0,                  AV_OPT_TYPE_CONST, {.i64=GREEN},   0, 0, VF, "colors" },
    {  "c",         "set cyans",                       0,                  AV_OPT_TYPE_CONST, {.i64=CYAN},    0, 0, VF, "colors" },
    {  "b",         "set blues",                       0,                  AV_OPT_TYPE_CONST, {.i64=BLUE},    0, 0, VF, "colors" },
    {  "m",         "set magentas",                    0,                  AV_OPT_TYPE_CONST, {.i64=MAGENTA}, 0, 0, VF, "colors" },
    {  "a",         "set all colors",                  0,                  AV_OPT_TYPE_CONST, {.i64=ALL},     0, 0, VF, "colors" },
    { "strength",   "set the filtering strength",      OFFSET(strength),   AV_OPT_TYPE_FLOAT, {.dbl=1},       0,100,VF },
    { "rw",         "set the red weight",              OFFSET(rlw),        AV_OPT_TYPE_FLOAT, {.dbl=.333},    0, 1, VF },
    { "gw",         "set the green weight",            OFFSET(glw),        AV_OPT_TYPE_FLOAT, {.dbl=.334},    0, 1, VF },
    { "bw",         "set the blue weight",             OFFSET(blw),        AV_OPT_TYPE_FLOAT, {.dbl=.333},    0, 1, VF },
    { "lightness",  "set the preserve lightness",      OFFSET(lightness),  AV_OPT_TYPE_BOOL,  {.i64=0},       0, 1, VF },
    { NULL }
};

AVFILTER_DEFINE_CLASS(huesaturation);

const AVFilter ff_vf_huesaturation = {
    .name            = "huesaturation",
    .description     = NULL_IF_CONFIG_SMALL("Apply hue-saturation-intensity adjustments."),
    .priv_size       = sizeof(HueSaturationContext),
    .priv_class      = &huesaturation_class,
    FILTER_INPUTS(huesaturation_inputs),
    FILTER_OUTPUTS(huesaturation_outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .flags           = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
    .process_command = ff_filter_process_command,
};
