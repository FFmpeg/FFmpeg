/*
 * Copyright (c) 2016 ReneBrals
 * Copyright (c) 2021 Paul B Mahol
 *
 * This file is part of FFmpeg.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "filters.h"
#include "framesync.h"
#include "video.h"

enum MorphModes {
    ERODE,
    DILATE,
    OPEN,
    CLOSE,
    GRADIENT,
    TOPHAT,
    BLACKHAT,
    NB_MODES
};

typedef struct IPlane {
    uint8_t **img;
    int w, h;
    int range;
    int depth;
    int type_size;

    void (*max_out_place)(uint8_t *c, const uint8_t *a, const uint8_t *b, int x);
    void (*min_out_place)(uint8_t *c, const uint8_t *a, const uint8_t *b, int x);
    void (*diff_rin_place)(uint8_t *a, const uint8_t *b, int x);
    void (*max_in_place)(uint8_t *a, const uint8_t *b, int x);
    void (*min_in_place)(uint8_t *a, const uint8_t *b, int x);
    void (*diff_in_place)(uint8_t *a, const uint8_t *b, int x);
} IPlane;

typedef struct LUT {
    /* arr is shifted from base_arr by FFMAX(min_r, 0).
     * arr != NULL means "lut completely allocated" */
    uint8_t ***arr;
    uint8_t ***base_arr;
    int min_r;
    int max_r;
    int I;
    int X;
    int pre_pad_x;
    int type_size;
} LUT;

typedef struct chord {
    int x;
    int y;
    int l;
    int i;
} chord;

typedef struct chord_set {
    chord *C;
    int size;
    int cap;

    int *R;
    int Lnum;

    int minX;
    int maxX;
    int minY;
    int maxY;
    unsigned nb_elements;
} chord_set;

#define MAX_THREADS 64

typedef struct MorphoContext {
    const AVClass *class;
    FFFrameSync fs;

    chord_set SE[4];
    IPlane SEimg[4];
    IPlane g[4], f[4], h[4];
    LUT Ty[MAX_THREADS][2][4];

    int mode;
    int planes;
    int structures;

    int planewidth[4];
    int planeheight[4];
    int splanewidth[4];
    int splaneheight[4];
    int depth;
    int type_size;
    int nb_planes;

    int got_structure[4];

    AVFrame *temp;

    int64_t *plane_f, *plane_g;
} MorphoContext;

#define OFFSET(x) offsetof(MorphoContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_FILTERING_PARAM | AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption morpho_options[] = {
    { "mode",  "set morphological transform",                 OFFSET(mode),       AV_OPT_TYPE_INT,   {.i64=0}, 0, NB_MODES-1, FLAGS, .unit = "mode" },
    { "erode",  NULL,                                         0,                  AV_OPT_TYPE_CONST, {.i64=ERODE},  0,  0, FLAGS, .unit = "mode" },
    { "dilate", NULL,                                         0,                  AV_OPT_TYPE_CONST, {.i64=DILATE}, 0,  0, FLAGS, .unit = "mode" },
    { "open",   NULL,                                         0,                  AV_OPT_TYPE_CONST, {.i64=OPEN},   0,  0, FLAGS, .unit = "mode" },
    { "close",  NULL,                                         0,                  AV_OPT_TYPE_CONST, {.i64=CLOSE},  0,  0, FLAGS, .unit = "mode" },
    { "gradient",NULL,                                        0,                  AV_OPT_TYPE_CONST, {.i64=GRADIENT},0, 0, FLAGS, .unit = "mode" },
    { "tophat",NULL,                                          0,                  AV_OPT_TYPE_CONST, {.i64=TOPHAT},  0, 0, FLAGS, .unit = "mode" },
    { "blackhat",NULL,                                        0,                  AV_OPT_TYPE_CONST, {.i64=BLACKHAT},0, 0, FLAGS, .unit = "mode" },
    { "planes",  "set planes to filter",                      OFFSET(planes),     AV_OPT_TYPE_INT,   {.i64=7}, 0, 15, FLAGS },
    { "structure", "when to process structures",              OFFSET(structures), AV_OPT_TYPE_INT,   {.i64=1}, 0,  1, FLAGS, .unit = "str" },
    {   "first", "process only first structure, ignore rest", 0,                  AV_OPT_TYPE_CONST, {.i64=0}, 0,  0, FLAGS, .unit = "str" },
    {   "all",   "process all structure",                     0,                  AV_OPT_TYPE_CONST, {.i64=1}, 0,  0, FLAGS, .unit = "str" },
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(morpho, MorphoContext, fs);

static const enum AVPixelFormat pix_fmts[] = {
    AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P,
    AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P,
    AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUVA420P, AV_PIX_FMT_YUV420P,
    AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUVJ420P,
    AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP, AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9,
    AV_PIX_FMT_YUV420P9, AV_PIX_FMT_YUV422P9, AV_PIX_FMT_YUV444P9, AV_PIX_FMT_GBRP9,
    AV_PIX_FMT_YUVA420P9, AV_PIX_FMT_YUVA422P9, AV_PIX_FMT_YUVA444P9,
    AV_PIX_FMT_YUV420P10, AV_PIX_FMT_YUV422P10, AV_PIX_FMT_YUV444P10,
    AV_PIX_FMT_YUV420P12, AV_PIX_FMT_YUV422P12, AV_PIX_FMT_YUV444P12, AV_PIX_FMT_YUV440P12,
    AV_PIX_FMT_YUV420P14, AV_PIX_FMT_YUV422P14, AV_PIX_FMT_YUV444P14,
    AV_PIX_FMT_YUV420P16, AV_PIX_FMT_YUV422P16, AV_PIX_FMT_YUV444P16,
    AV_PIX_FMT_YUVA420P10, AV_PIX_FMT_YUVA422P10, AV_PIX_FMT_YUVA444P10,
    AV_PIX_FMT_YUVA422P12, AV_PIX_FMT_YUVA444P12,
    AV_PIX_FMT_YUVA420P16, AV_PIX_FMT_YUVA422P16, AV_PIX_FMT_YUVA444P16,
    AV_PIX_FMT_GBRP10, AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
    AV_PIX_FMT_NONE
};

static void min_fun(uint8_t *c, const uint8_t *a, const uint8_t *b, int x)
{
    for (int i = 0; i < x; i++)
        c[i] = FFMIN(b[i], a[i]);
}

static void mininplace_fun(uint8_t *a, const uint8_t *b, int x)
{
    for (int i = 0; i < x; i++)
        a[i] = FFMIN(a[i], b[i]);
}

static void max_fun(uint8_t *c, const uint8_t *a, const uint8_t *b, int x)
{
    for (int i = 0; i < x; i++)
        c[i] = FFMAX(a[i], b[i]);
}

static void maxinplace_fun(uint8_t *a, const uint8_t *b, int x)
{
    for (int i = 0; i < x; i++)
        a[i] = FFMAX(a[i], b[i]);
}

static void diff_fun(uint8_t *a, const uint8_t *b, int x)
{
    for (int i = 0; i < x; i++)
        a[i] = FFMAX(b[i] - a[i], 0);
}

static void diffinplace_fun(uint8_t *a, const uint8_t *b, int x)
{
    for (int i = 0; i < x; i++)
        a[i] = FFMAX(a[i] - b[i], 0);
}

static void min16_fun(uint8_t *cc, const uint8_t *aa, const uint8_t *bb, int x)
{
    const uint16_t *a = (const uint16_t *)aa;
    const uint16_t *b = (const uint16_t *)bb;
    uint16_t *c = (uint16_t *)cc;

    for (int i = 0; i < x; i++)
        c[i] = FFMIN(b[i], a[i]);
}

static void mininplace16_fun(uint8_t *aa, const uint8_t *bb, int x)
{
    uint16_t *a = (uint16_t *)aa;
    const uint16_t *b = (const uint16_t *)bb;

    for (int i = 0; i < x; i++)
        a[i] = FFMIN(a[i], b[i]);
}

static void diff16_fun(uint8_t *aa, const uint8_t *bb, int x)
{
    const uint16_t *b = (const uint16_t *)bb;
    uint16_t *a = (uint16_t *)aa;

    for (int i = 0; i < x; i++)
        a[i] = FFMAX(b[i] - a[i], 0);
}

static void diffinplace16_fun(uint8_t *aa, const uint8_t *bb, int x)
{
    uint16_t *a = (uint16_t *)aa;
    const uint16_t *b = (const uint16_t *)bb;

    for (int i = 0; i < x; i++)
        a[i] = FFMAX(a[i] - b[i], 0);
}

static void max16_fun(uint8_t *cc, const uint8_t *aa, const uint8_t *bb, int x)
{
    const uint16_t *a = (const uint16_t *)aa;
    const uint16_t *b = (const uint16_t *)bb;
    uint16_t *c = (uint16_t *)cc;

    for (int i = 0; i < x; i++)
        c[i] = FFMAX(a[i], b[i]);
}

static void maxinplace16_fun(uint8_t *aa, const uint8_t *bb, int x)
{
    uint16_t *a = (uint16_t *)aa;
    const uint16_t *b = (const uint16_t *)bb;

    for (int i = 0; i < x; i++)
        a[i] = FFMAX(a[i], b[i]);
}

static int alloc_lut(LUT *Ty, chord_set *SE, int type_size, int mode)
{
    const int min = FFMAX(Ty->min_r, 0);
    const int max = min + (Ty->max_r - Ty->min_r);
    int pre_pad_x = 0;

    if (SE->minX < 0)
        pre_pad_x = 0 - SE->minX;
    Ty->pre_pad_x = pre_pad_x;
    Ty->type_size = type_size;

    Ty->base_arr = av_calloc(max + 1, sizeof(*Ty->base_arr));
    if (!Ty->base_arr)
        return AVERROR(ENOMEM);
    for (int r = min; r <= max; r++) {
        uint8_t **arr = Ty->base_arr[r] = av_calloc(Ty->I, sizeof(uint8_t *));
        if (!Ty->base_arr[r])
            return AVERROR(ENOMEM);
        for (int i = 0; i < Ty->I; i++) {
            arr[i] = av_calloc(Ty->X + pre_pad_x, type_size);
            if (!arr[i])
                return AVERROR(ENOMEM);
            if (mode == ERODE)
                memset(arr[i], UINT8_MAX, pre_pad_x * type_size);
            /* Shifting the X index such that negative indices correspond to
             * the pre-padding.
             */
            arr[i] = &(arr[i][pre_pad_x * type_size]);
        }
    }

    Ty->arr = &(Ty->base_arr[min - Ty->min_r]);

    return 0;
}

static void free_lut(LUT *table)
{
    const int min = FFMAX(table->min_r, 0);
    const int max = min + (table->max_r - table->min_r);

    if (!table->base_arr)
        return;

    for (int r = min; r <= max; r++) {
        if (!table->base_arr[r])
            break;
        for (int i = 0; i < table->I; i++) {
            if (!table->base_arr[r][i])
                break;
            // The X index was also shifted, for padding purposes.
            av_free(table->base_arr[r][i] - table->pre_pad_x * table->type_size);
        }
        av_freep(&table->base_arr[r]);
    }
    av_freep(&table->base_arr);
    table->arr = NULL;
}

static int alloc_lut_if_necessary(LUT *Ty, IPlane *f, chord_set *SE,
                                  int num, enum MorphModes mode)
{
    if (!Ty->arr || Ty->I != SE->Lnum ||
        Ty->X != f->w ||
        SE->minX < 0 && -SE->minX > Ty->pre_pad_x ||
        Ty->min_r != SE->minY ||
        Ty->max_r != SE->maxY + num - 1) {
        int ret;

        free_lut(Ty);

        Ty->I = SE->Lnum;
        Ty->X = f->w;
        Ty->min_r = SE->minY;
        Ty->max_r = SE->maxY + num - 1;
        ret = alloc_lut(Ty, SE, f->type_size, mode);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static void circular_swap(LUT *Ty)
{
    /*
     * Swap the pointers to r-indices in a circle. This is useful because
     * Ty(r,i,x) = Ty-1(r+1,i,x) for r < ymax.
     */
    if (Ty->max_r - Ty->min_r > 0) {
        uint8_t **Ty0 = Ty->arr[Ty->min_r];

        for (int r = Ty->min_r; r < Ty->max_r; r++)
            Ty->arr[r] = Ty->arr[r + 1];

        Ty->arr[Ty->max_r] = Ty0;
    }
}

static void compute_min_row(IPlane *f, LUT *Ty, chord_set *SE, int r, int y)
{
    if (y + r >= 0 && y + r < f->h) {
        memcpy(Ty->arr[r][0], f->img[y + r], Ty->X * Ty->type_size);
    } else {
        memset(Ty->arr[r][0], UINT8_MAX, Ty->X * Ty->type_size);
    }

    for (int i = 1; i < SE->Lnum; i++) {
        int d = SE->R[i] - SE->R[i - 1];

        f->min_out_place(Ty->arr[r][i] - Ty->pre_pad_x * f->type_size,
            Ty->arr[r][i - 1] - Ty->pre_pad_x * f->type_size,
            Ty->arr[r][i - 1] + (d - Ty->pre_pad_x) * f->type_size,
            Ty->X + Ty->pre_pad_x - d);
        memcpy(Ty->arr[r][i] + (Ty->X - d) * f->type_size,
               Ty->arr[r][i - 1] + (Ty->X - d) * f->type_size,
               d * f->type_size);
    }
}

static void update_min_lut(IPlane *f, LUT *Ty, chord_set *SE, int y, int tid, int num)
{
    for (int i = 0; i < num; i++)
        circular_swap(Ty);

    compute_min_row(f, Ty, SE, Ty->max_r - tid, y);
}

static int compute_min_lut(LUT *Ty, IPlane *f, chord_set *SE, int y, int num)
{
    int ret = alloc_lut_if_necessary(Ty, f, SE, num, ERODE);
    if (ret < 0)
        return ret;

    for (int r = Ty->min_r; r <= Ty->max_r; r++)
        compute_min_row(f, Ty, SE, r, y);

    return 0;
}

static void compute_max_row(IPlane *f, LUT *Ty, chord_set *SE, int r, int y)
{
    if (y + r >= 0 && y + r < f->h) {
        memcpy(Ty->arr[r][0], f->img[y + r], Ty->X * Ty->type_size);
    } else {
        memset(Ty->arr[r][0], 0, Ty->X * Ty->type_size);
    }

    for (int i = 1; i < SE->Lnum; i++) {
        int d = SE->R[i] - SE->R[i - 1];

        f->max_out_place(Ty->arr[r][i] - Ty->pre_pad_x * f->type_size,
            Ty->arr[r][i - 1] - Ty->pre_pad_x * f->type_size,
            Ty->arr[r][i - 1] + (d - Ty->pre_pad_x) * f->type_size,
            Ty->X + Ty->pre_pad_x - d);
        memcpy(Ty->arr[r][i] + (Ty->X - d) * f->type_size,
               Ty->arr[r][i - 1] + (Ty->X - d) * f->type_size,
               d * f->type_size);
    }
}

static void update_max_lut(IPlane *f, LUT *Ty, chord_set *SE, int y, int tid, int num)
{
    for (int i = 0; i < num; i++)
        circular_swap(Ty);

    compute_max_row(f, Ty, SE, Ty->max_r - tid, y);
}

static int compute_max_lut(LUT *Ty, IPlane *f, chord_set *SE, int y, int num)
{
    int ret = alloc_lut_if_necessary(Ty, f, SE, num, DILATE);
    if (ret < 0)
        return ret;

    for (int r = Ty->min_r; r <= Ty->max_r; r++)
        compute_max_row(f, Ty, SE, r, y);

    return 0;
}

static void line_dilate(IPlane *g, LUT *Ty, chord_set *SE, int y, int tid)
{
    memset(g->img[y], 0, g->w * g->type_size);

    for (int c = 0; c < SE->size; c++) {
        g->max_in_place(g->img[y],
            Ty->arr[SE->C[c].y + tid][SE->C[c].i] + SE->C[c].x * Ty->type_size,
            av_clip(g->w - SE->C[c].x, 0, g->w));
    }
}

static void line_erode(IPlane *g, LUT *Ty, chord_set *SE, int y, int tid)
{
    memset(g->img[y], UINT8_MAX, g->w * g->type_size);

    for (int c = 0; c < SE->size; c++) {
        g->min_in_place(g->img[y],
            Ty->arr[SE->C[c].y + tid][SE->C[c].i] + SE->C[c].x * Ty->type_size,
            av_clip(g->w - SE->C[c].x, 0, g->w));
    }
}

static int dilate(IPlane *g, IPlane *f, chord_set *SE, LUT *Ty, int y0, int y1)
{
    int ret = compute_max_lut(Ty, f, SE, y0, 1);
    if (ret < 0)
        return ret;

    line_dilate(g, Ty, SE, y0, 0);
    for (int y = y0 + 1; y < y1; y++) {
        update_max_lut(f, Ty, SE, y, 0, 1);
        line_dilate(g, Ty, SE, y, 0);
    }

    return 0;
}

static int erode(IPlane *g, IPlane *f, chord_set *SE, LUT *Ty, int y0, int y1)
{
    int ret = compute_min_lut(Ty, f, SE, y0, 1);
    if (ret < 0)
        return ret;

    line_erode(g, Ty, SE, y0, 0);
    for (int y = y0 + 1; y < y1; y++) {
        update_min_lut(f, Ty, SE, y, 0, 1);
        line_erode(g, Ty, SE, y, 0);
    }

    return 0;
}

static void difference(IPlane *g, IPlane *f, int y0, int y1)
{
    for (int y = y0; y < y1; y++)
        f->diff_in_place(g->img[y], f->img[y], f->w);
}

static void difference2(IPlane *g, IPlane *f, int y0, int y1)
{
    for (int y = y0; y < y1; y++)
        f->diff_rin_place(g->img[y], f->img[y], f->w);
}

static int insert_chord_set(chord_set *chords, chord c)
{
    // Checking if chord fits in dynamic array, resize if not.
    if (chords->size == chords->cap) {
        chords->C = av_realloc_f(chords->C, chords->cap * 2, sizeof(chord));
        if (!chords->C)
            return AVERROR(ENOMEM);
        chords->cap *= 2;
    }

    // Add the chord to the dynamic array.
    chords->C[chords->size].x = c.x;
    chords->C[chords->size].y = c.y;
    chords->C[chords->size++].l = c.l;

    // Update minimum/maximum x/y offsets of the chord set.
    chords->minX = FFMIN(chords->minX, c.x);
    chords->maxX = FFMAX(chords->maxX, c.x);

    chords->minY = FFMIN(chords->minY, c.y);
    chords->maxY = FFMAX(chords->maxY, c.y);

    return 0;
}

static void free_chord_set(chord_set *SE)
{
    av_freep(&SE->C);
    SE->size = 0;
    SE->cap = 0;

    av_freep(&SE->R);
    SE->Lnum = 0;
}

static int init_chordset(chord_set *chords)
{
    chords->nb_elements = 0;
    chords->size = 0;
    chords->C = av_calloc(1, sizeof(chord));
    if (!chords->C)
        return AVERROR(ENOMEM);

    chords->cap = 1;
    chords->minX = INT16_MAX;
    chords->maxX = INT16_MIN;
    chords->minY = INT16_MAX;
    chords->maxY = INT16_MIN;

    return 0;
}

static int comp_chord_length(const void *p, const void *q)
{
    chord a, b;
    a = *((chord *)p);
    b = *((chord *)q);

    return (a.l > b.l) - (a.l < b.l);
}

static int comp_chord(const void *p, const void *q)
{
    chord a, b;
    a = *((chord *)p);
    b = *((chord *)q);

    return (a.y > b.y) - (a.y < b.y);
}

static int build_chord_set(IPlane *SE, chord_set *chords)
{
    const int mid = 1 << (SE->depth - 1);
    int chord_length_index;
    int chord_start, val, ret;
    int centerX, centerY;
    int r_cap = 1;
    chord c;

    ret = init_chordset(chords);
    if (ret < 0)
        return ret;
    /*
     * In erosion/dilation, the center of the IPlane has S.E. offset (0,0).
     * Otherwise, the resulting IPlane would be shifted to the top-left.
     */
    centerX = (SE->w - 1) / 2;
    centerY = (SE->h - 1) / 2;

    /*
     * Computing the set of chords C.
     */
    for (int y = 0; y < SE->h; y++) {
        int x;

        chord_start = -1;
        for (x = 0; x < SE->w; x++) {
            if (SE->type_size == 1) {
                chords->nb_elements += (SE->img[y][x] >= mid);
                //A chord is a run of non-zero pixels.
                if (SE->img[y][x] >= mid && chord_start == -1) {
                    // Chord starts.
                    chord_start = x;
                } else if (SE->img[y][x] < mid && chord_start != -1) {
                    // Chord ends before end of line.
                    c.x = chord_start - centerX;
                    c.y = y - centerY;
                    c.l = x - chord_start;
                    ret = insert_chord_set(chords, c);
                    if (ret < 0)
                        return AVERROR(ENOMEM);
                    chord_start = -1;
                }
            } else {
                chords->nb_elements += (AV_RN16(&SE->img[y][x * 2]) >= mid);
                //A chord is a run of non-zero pixels.
                if (AV_RN16(&SE->img[y][x * 2]) >= mid && chord_start == -1) {
                    // Chord starts.
                    chord_start = x;
                } else if (AV_RN16(&SE->img[y][x * 2]) < mid && chord_start != -1) {
                    // Chord ends before end of line.
                    c.x = chord_start - centerX;
                    c.y = y - centerY;
                    c.l = x - chord_start;
                    ret = insert_chord_set(chords, c);
                    if (ret < 0)
                        return AVERROR(ENOMEM);
                    chord_start = -1;
                }
            }
        }
        if (chord_start != -1) {
            // Chord ends at end of line.
            c.x = chord_start - centerX;
            c.y = y - centerY;
            c.l = x - chord_start;
            ret = insert_chord_set(chords, c);
            if (ret < 0)
                return AVERROR(ENOMEM);
        }
    }

    /*
     * Computing the array of chord lengths R(i).
     * This is needed because the lookup table will contain a row for each
     * length index i.
     */
    qsort(chords->C, chords->size, sizeof(chord), comp_chord_length);
    chords->R = av_calloc(1, sizeof(*chords->R));
    if (!chords->R)
        return AVERROR(ENOMEM);
    chords->Lnum = 0;
    val = 0;
    r_cap = 1;

    if (chords->size > 0) {
        val = 1;
        if (chords->Lnum >= r_cap) {
            chords->R = av_realloc_f(chords->R, r_cap * 2, sizeof(*chords->R));
            if (!chords->R)
                return AVERROR(ENOMEM);
            r_cap *= 2;
        }
        chords->R[chords->Lnum++] = 1;
        val = 1;
    }

    for (int i = 0; i < chords->size; i++) {
        if (val != chords->C[i].l) {
            while (2 * val < chords->C[i].l && val != 0) {
                if (chords->Lnum >= r_cap) {
                    chords->R = av_realloc_f(chords->R, r_cap * 2, sizeof(*chords->R));
                    if (!chords->R)
                        return AVERROR(ENOMEM);
                    r_cap *= 2;
                }

                chords->R[chords->Lnum++] = 2 * val;
                val *= 2;
            }
            val = chords->C[i].l;

            if (chords->Lnum >= r_cap) {
                chords->R = av_realloc_f(chords->R, r_cap * 2, sizeof(*chords->R));
                if (!chords->R)
                    return AVERROR(ENOMEM);
                r_cap *= 2;
            }
            chords->R[chords->Lnum++] = val;
        }
    }

    /*
     * Setting the length indices of chords.
     * These are needed so that the algorithm can, for each chord,
     * access the lookup table at the correct length in constant time.
     */
    chord_length_index = 0;
    for (int i = 0; i < chords->size; i++) {
        while (chords->R[chord_length_index] < chords->C[i].l)
            chord_length_index++;
        chords->C[i].i = chord_length_index;
    }

    /*
     * Chords are sorted on Y. This way, when a row of the lookup table or IPlane
     * is cached, the next chord offset has a better chance of being on the
     * same cache line.
     */
    qsort(chords->C, chords->size, sizeof(chord), comp_chord);

    return 0;
}

static void free_iplane(IPlane *imp)
{
    av_freep(&imp->img);
}

static int read_iplane(IPlane *imp, const uint8_t *dst, int dst_linesize,
                       int w, int h, int R, int type_size, int depth)
{
    if (!imp->img)
        imp->img = av_calloc(h, sizeof(*imp->img));
    if (!imp->img)
        return AVERROR(ENOMEM);

    imp->w = w;
    imp->h = h;
    imp->range = R;
    imp->depth = depth;
    imp->type_size = type_size;
    imp->max_out_place = type_size == 1 ? max_fun : max16_fun;
    imp->min_out_place = type_size == 1 ? min_fun : min16_fun;
    imp->diff_rin_place = type_size == 1 ? diff_fun : diff16_fun;
    imp->max_in_place = type_size == 1 ? maxinplace_fun : maxinplace16_fun;
    imp->min_in_place = type_size == 1 ? mininplace_fun : mininplace16_fun;
    imp->diff_in_place = type_size == 1 ? diffinplace_fun : diffinplace16_fun;

    for (int y = 0; y < h; y++)
        imp->img[y] = (uint8_t *)dst + y * dst_linesize;

    return 0;
}

static int config_input(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    MorphoContext *s = inlink->dst->priv;

    s->depth = desc->comp[0].depth;
    s->type_size = (s->depth + 7) / 8;
    s->nb_planes = desc->nb_components;
    s->planewidth[1] = s->planewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0] = s->planewidth[3] = inlink->w;
    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;

    return 0;
}

static int config_input_structure(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx = inlink->dst;
    MorphoContext *s = inlink->dst->priv;

    av_assert0(ctx->inputs[0]->format == ctx->inputs[1]->format);

    s->splanewidth[1] = s->splanewidth[2] = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->splanewidth[0] = s->splanewidth[3] = inlink->w;
    s->splaneheight[1] = s->splaneheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->splaneheight[0] = s->splaneheight[3] = inlink->h;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    MorphoContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

static int morpho_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MorphoContext *s = ctx->priv;
    ThreadData *td = arg;
    AVFrame *out = td->out;
    AVFrame *in = td->in;
    int ret;

    for (int p = 0; p < s->nb_planes; p++) {
        const int width = s->planewidth[p];
        const int height = s->planeheight[p];
        const int y0 = (height *  jobnr   ) / nb_jobs;
        const int y1 = (height * (jobnr+1)) / nb_jobs;
        const int depth = s->depth;

        if (ctx->is_disabled || !(s->planes & (1 << p))) {
copy:
            av_image_copy_plane(out->data[p] + y0 * out->linesize[p],
                out->linesize[p],
                in->data[p] + y0 * in->linesize[p],
                in->linesize[p],
                width * ((depth + 7) / 8),
                y1 - y0);
            continue;
        }

        if (s->SE[p].minX == INT16_MAX ||
            s->SE[p].minY == INT16_MAX ||
            s->SE[p].maxX == INT16_MIN ||
            s->SE[p].maxY == INT16_MIN)
            goto copy;

        switch (s->mode) {
        case ERODE:
            ret = erode(&s->g[p], &s->f[p], &s->SE[p], &s->Ty[jobnr][0][p], y0, y1);
            break;
        case DILATE:
        case GRADIENT:
            ret = dilate(&s->g[p], &s->f[p], &s->SE[p], &s->Ty[jobnr][0][p], y0, y1);
            break;
        case OPEN:
        case TOPHAT:
            ret = erode(&s->h[p], &s->f[p], &s->SE[p], &s->Ty[jobnr][0][p], y0, y1);
            break;
        case CLOSE:
        case BLACKHAT:
            ret = dilate(&s->h[p], &s->f[p], &s->SE[p], &s->Ty[jobnr][0][p], y0, y1);
            break;
        default:
            av_assert0(0);
        }

        if (ret < 0)
            return ret;
    }

    return 0;
}

static int morpho_sliceX(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    MorphoContext *s = ctx->priv;
    int ret;

    for (int p = 0; p < s->nb_planes; p++) {
        const int height = s->planeheight[p];
        const int y0 = (height *  jobnr   ) / nb_jobs;
        const int y1 = (height * (jobnr+1)) / nb_jobs;

        if (ctx->is_disabled || !(s->planes & (1 << p))) {
copy:
            continue;
        }

        if (s->SE[p].minX == INT16_MAX ||
            s->SE[p].minY == INT16_MAX ||
            s->SE[p].maxX == INT16_MIN ||
            s->SE[p].maxY == INT16_MIN)
            goto copy;

        switch (s->mode) {
        case OPEN:
            ret = dilate(&s->g[p], &s->h[p], &s->SE[p], &s->Ty[jobnr][1][p], y0, y1);
            break;
        case CLOSE:
            ret = erode(&s->g[p], &s->h[p], &s->SE[p], &s->Ty[jobnr][1][p], y0, y1);
            break;
        case GRADIENT:
            ret = erode(&s->h[p], &s->f[p], &s->SE[p], &s->Ty[jobnr][1][p], y0, y1);
            if (ret < 0)
                break;
            difference(&s->g[p], &s->h[p], y0, y1);
            break;
        case TOPHAT:
            ret = dilate(&s->g[p], &s->h[p], &s->SE[p], &s->Ty[jobnr][1][p], y0, y1);
            if (ret < 0)
                break;
            difference2(&s->g[p], &s->f[p], y0, y1);
            break;
        case BLACKHAT:
            ret = erode(&s->g[p], &s->h[p], &s->SE[p], &s->Ty[jobnr][1][p], y0, y1);
            if (ret < 0)
                break;
            difference(&s->g[p], &s->f[p], y0, y1);
            break;
        default:
            av_assert0(0);
        }

        if (ret < 0)
            return ret;
    }

    return 0;
}

static int do_morpho(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    AVFilterLink *outlink = ctx->outputs[0];
    MorphoContext *s = ctx->priv;
    AVFrame *in = NULL, *structurepic = NULL;
    ThreadData td;
    AVFrame *out;
    int ret;

    ret = ff_framesync_dualinput_get(fs, &in, &structurepic);
    if (ret < 0)
        return ret;
    if (!structurepic)
        return ff_filter_frame(outlink, in);

    out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!out) {
        av_frame_free(&in);
        return AVERROR(ENOMEM);
    }
    av_frame_copy_props(out, in);

    for (int p = 0; p < s->nb_planes; p++) {
        const uint8_t *ssrc = structurepic->data[p];
        const int ssrc_linesize = structurepic->linesize[p];
        const int swidth = s->splanewidth[p];
        const int sheight = s->splaneheight[p];
        const uint8_t *src = in->data[p];
        int src_linesize = in->linesize[p];
        uint8_t *dst = out->data[p];
        int dst_linesize = out->linesize[p];
        const int width = s->planewidth[p];
        const int height = s->planeheight[p];
        const int depth = s->depth;
        int type_size = s->type_size;

        if (!s->got_structure[p] || s->structures) {
            free_chord_set(&s->SE[p]);

            ret = read_iplane(&s->SEimg[p], ssrc, ssrc_linesize, swidth, sheight, 1, type_size, depth);
            if (ret < 0)
                goto fail;
            ret = build_chord_set(&s->SEimg[p], &s->SE[p]);
            if (ret < 0)
                goto fail;
            s->got_structure[p] = 1;
        }

        ret = read_iplane(&s->f[p], src, src_linesize, width, height, 1, type_size, depth);
        if (ret < 0)
            goto fail;

        ret = read_iplane(&s->g[p], dst, dst_linesize, s->f[p].w, s->f[p].h, s->f[p].range, type_size, depth);
        if (ret < 0)
            goto fail;

        switch (s->mode) {
        case OPEN:
        case CLOSE:
        case GRADIENT:
        case TOPHAT:
        case BLACKHAT:
            ret = read_iplane(&s->h[p], s->temp->data[p], s->temp->linesize[p], width, height, 1, type_size, depth);
            break;
        }

        if (ret < 0)
            goto fail;
    }

    td.in = in; td.out = out;
    ret = ff_filter_execute(ctx, morpho_slice, &td, NULL,
                            FFMIN3(s->planeheight[1], s->planeheight[2],
                                   FFMIN(MAX_THREADS, ff_filter_get_nb_threads(ctx))));
    if (ret == 0 && (s->mode != ERODE && s->mode != DILATE)) {
        ff_filter_execute(ctx, morpho_sliceX, NULL, NULL,
                          FFMIN3(s->planeheight[1], s->planeheight[2],
                                 FFMIN(MAX_THREADS, ff_filter_get_nb_threads(ctx))));
    }

    av_frame_free(&in);
    out->pts = av_rescale_q(s->fs.pts, s->fs.time_base, outlink->time_base);
    return ff_filter_frame(outlink, out);
fail:
    av_frame_free(&out);
    av_frame_free(&in);
    return ret;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    MorphoContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    FilterLink *il = ff_filter_link(mainlink);
    FilterLink *ol = ff_filter_link(outlink);
    int ret;

    s->fs.on_event = do_morpho;
    ret = ff_framesync_init_dualinput(&s->fs, ctx);
    if (ret < 0)
        return ret;
    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    ol->frame_rate = il->frame_rate;

    if ((ret = ff_framesync_configure(&s->fs)) < 0)
        return ret;
    outlink->time_base = s->fs.time_base;

    s->temp = ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!s->temp)
        return AVERROR(ENOMEM);

    s->plane_f = av_calloc(outlink->w * outlink->h, sizeof(*s->plane_f));
    s->plane_g = av_calloc(outlink->w * outlink->h, sizeof(*s->plane_g));
    if (!s->plane_f || !s->plane_g)
        return AVERROR(ENOMEM);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    MorphoContext *s = ctx->priv;

    for (int p = 0; p < 4; p++) {
        free_iplane(&s->SEimg[p]);
        free_iplane(&s->f[p]);
        free_iplane(&s->g[p]);
        free_iplane(&s->h[p]);
        free_chord_set(&s->SE[p]);
        for (int n = 0; n < MAX_THREADS; n++) {
            free_lut(&s->Ty[n][0][p]);
            free_lut(&s->Ty[n][1][p]);
        }
    }

    ff_framesync_uninit(&s->fs);

    av_frame_free(&s->temp);
    av_freep(&s->plane_f);
    av_freep(&s->plane_g);
}

static const AVFilterPad morpho_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input,
    },
    {
        .name         = "structure",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_structure,
    },
};

static const AVFilterPad morpho_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_output,
    },
};

const FFFilter ff_vf_morpho = {
    .p.name          = "morpho",
    .p.description   = NULL_IF_CONFIG_SMALL("Apply Morphological filter."),
    .p.priv_class    = &morpho_class,
    .p.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL |
                       AVFILTER_FLAG_SLICE_THREADS,
    .preinit         = morpho_framesync_preinit,
    .priv_size       = sizeof(MorphoContext),
    .activate        = activate,
    .uninit          = uninit,
    FILTER_INPUTS(morpho_inputs),
    FILTER_OUTPUTS(morpho_outputs),
    FILTER_PIXFMTS_ARRAY(pix_fmts),
    .process_command = ff_filter_process_command,
};
