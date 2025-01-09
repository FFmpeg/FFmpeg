/*
 * Copyright (c) 2012-2013 Clément Bœsch
 * Copyright (c) 2013 Rudolf Polzer <divverent@xonotic.org>
 * Copyright (c) 2015 Paul B Mahol
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
 * audio to spectrum (video) transmedia filter, based on ffplay rdft showmode
 * (by Michael Niedermayer) and lavfi/avf_showwaves (by Stefano Sabatini).
 */

#include "config_components.h"

#include <float.h>
#include <math.h>

#include "libavutil/mem.h"
#include "libavutil/tx.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/cpu.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/xga_font_data.h"
#include "audio.h"
#include "formats.h"
#include "video.h"
#include "avfilter.h"
#include "filters.h"
#include "window_func.h"

enum DisplayMode  { COMBINED, SEPARATE, NB_MODES };
enum DataMode     { D_MAGNITUDE, D_PHASE, D_UPHASE, NB_DMODES };
enum FrequencyScale { F_LINEAR, F_LOG, NB_FSCALES };
enum DisplayScale { LINEAR, SQRT, CBRT, LOG, FOURTHRT, FIFTHRT, NB_SCALES };
enum ColorMode    { CHANNEL, INTENSITY, RAINBOW, MORELAND, NEBULAE, FIRE, FIERY, FRUIT, COOL, MAGMA, GREEN, VIRIDIS, PLASMA, CIVIDIS, TERRAIN, NB_CLMODES };
enum SlideMode    { REPLACE, SCROLL, FULLFRAME, RSCROLL, LREPLACE, NB_SLIDES };
enum Orientation  { VERTICAL, HORIZONTAL, NB_ORIENTATIONS };

#define DEFAULT_LENGTH 300

typedef struct ShowSpectrumContext {
    const AVClass *class;
    int w, h;
    char *rate_str;
    AVRational auto_frame_rate;
    AVRational frame_rate;
    AVFrame *outpicref;
    AVFrame *in_frame;
    int nb_display_channels;
    int orientation;
    int channel_width;
    int channel_height;
    int sliding;                ///< 1 if sliding mode, 0 otherwise
    int mode;                   ///< channel display mode
    int color_mode;             ///< display color scheme
    int scale;
    int fscale;
    float saturation;           ///< color saturation multiplier
    float rotation;             ///< color rotation
    int start, stop;            ///< zoom mode
    int data;
    int xpos;                   ///< x position (current column)
    AVTXContext **fft;          ///< Fast Fourier Transform context
    AVTXContext **ifft;         ///< Inverse Fast Fourier Transform context
    av_tx_fn tx_fn;
    av_tx_fn itx_fn;
    int fft_size;               ///< number of coeffs (FFT window size)
    AVComplexFloat **fft_in;    ///< input FFT coeffs
    AVComplexFloat **fft_data;  ///< bins holder for each (displayed) channels
    AVComplexFloat **fft_scratch;///< scratch buffers
    float *window_func_lut;     ///< Window function LUT
    float **magnitudes;
    float **phases;
    int win_func;
    int win_size;
    int buf_size;
    double win_scale;
    float overlap;
    float gain;
    int hop_size;
    float *combine_buffer;      ///< color combining buffer (4 * h items)
    float **color_buffer;       ///< color buffer (4 * h * ch items)
    int64_t pts;
    int64_t old_pts;
    int64_t in_pts;
    int old_len;
    int single_pic;
    int legend;
    int start_x, start_y;
    float drange, limit;
    float dmin, dmax;
    uint64_t samples;
    int (*plot_channel)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
    int eof;

    float opacity_factor;

    AVFrame **frames;
    unsigned int nb_frames;
    unsigned int frames_size;
} ShowSpectrumContext;

#define OFFSET(x) offsetof(ShowSpectrumContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showspectrum_options[] = {
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x512"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x512"}, 0, 0, FLAGS },
    { "slide", "set sliding mode", OFFSET(sliding), AV_OPT_TYPE_INT, {.i64 = 0}, 0, NB_SLIDES-1, FLAGS, .unit = "slide" },
        { "replace", "replace old columns with new", 0, AV_OPT_TYPE_CONST, {.i64=REPLACE}, 0, 0, FLAGS, .unit = "slide" },
        { "scroll", "scroll from right to left", 0, AV_OPT_TYPE_CONST, {.i64=SCROLL}, 0, 0, FLAGS, .unit = "slide" },
        { "fullframe", "return full frames", 0, AV_OPT_TYPE_CONST, {.i64=FULLFRAME}, 0, 0, FLAGS, .unit = "slide" },
        { "rscroll", "scroll from left to right", 0, AV_OPT_TYPE_CONST, {.i64=RSCROLL}, 0, 0, FLAGS, .unit = "slide" },
        { "lreplace", "replace from right to left", 0, AV_OPT_TYPE_CONST, {.i64=LREPLACE}, 0, 0, FLAGS, .unit = "slide" },
    { "mode", "set channel display mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=COMBINED}, COMBINED, NB_MODES-1, FLAGS, .unit = "mode" },
        { "combined", "combined mode", 0, AV_OPT_TYPE_CONST, {.i64=COMBINED}, 0, 0, FLAGS, .unit = "mode" },
        { "separate", "separate mode", 0, AV_OPT_TYPE_CONST, {.i64=SEPARATE}, 0, 0, FLAGS, .unit = "mode" },
    { "color", "set channel coloring", OFFSET(color_mode), AV_OPT_TYPE_INT, {.i64=CHANNEL}, CHANNEL, NB_CLMODES-1, FLAGS, .unit = "color" },
        { "channel",   "separate color for each channel", 0, AV_OPT_TYPE_CONST, {.i64=CHANNEL},   0, 0, FLAGS, .unit = "color" },
        { "intensity", "intensity based coloring",        0, AV_OPT_TYPE_CONST, {.i64=INTENSITY}, 0, 0, FLAGS, .unit = "color" },
        { "rainbow",   "rainbow based coloring",          0, AV_OPT_TYPE_CONST, {.i64=RAINBOW},   0, 0, FLAGS, .unit = "color" },
        { "moreland",  "moreland based coloring",         0, AV_OPT_TYPE_CONST, {.i64=MORELAND},  0, 0, FLAGS, .unit = "color" },
        { "nebulae",   "nebulae based coloring",          0, AV_OPT_TYPE_CONST, {.i64=NEBULAE},   0, 0, FLAGS, .unit = "color" },
        { "fire",      "fire based coloring",             0, AV_OPT_TYPE_CONST, {.i64=FIRE},      0, 0, FLAGS, .unit = "color" },
        { "fiery",     "fiery based coloring",            0, AV_OPT_TYPE_CONST, {.i64=FIERY},     0, 0, FLAGS, .unit = "color" },
        { "fruit",     "fruit based coloring",            0, AV_OPT_TYPE_CONST, {.i64=FRUIT},     0, 0, FLAGS, .unit = "color" },
        { "cool",      "cool based coloring",             0, AV_OPT_TYPE_CONST, {.i64=COOL},      0, 0, FLAGS, .unit = "color" },
        { "magma",     "magma based coloring",            0, AV_OPT_TYPE_CONST, {.i64=MAGMA},     0, 0, FLAGS, .unit = "color" },
        { "green",     "green based coloring",            0, AV_OPT_TYPE_CONST, {.i64=GREEN},     0, 0, FLAGS, .unit = "color" },
        { "viridis",   "viridis based coloring",          0, AV_OPT_TYPE_CONST, {.i64=VIRIDIS},   0, 0, FLAGS, .unit = "color" },
        { "plasma",    "plasma based coloring",           0, AV_OPT_TYPE_CONST, {.i64=PLASMA},    0, 0, FLAGS, .unit = "color" },
        { "cividis",   "cividis based coloring",          0, AV_OPT_TYPE_CONST, {.i64=CIVIDIS},   0, 0, FLAGS, .unit = "color" },
        { "terrain",   "terrain based coloring",          0, AV_OPT_TYPE_CONST, {.i64=TERRAIN},   0, 0, FLAGS, .unit = "color" },
    { "scale", "set display scale", OFFSET(scale), AV_OPT_TYPE_INT, {.i64=SQRT}, LINEAR, NB_SCALES-1, FLAGS, .unit = "scale" },
        { "lin",  "linear",      0, AV_OPT_TYPE_CONST, {.i64=LINEAR}, 0, 0, FLAGS, .unit = "scale" },
        { "sqrt", "square root", 0, AV_OPT_TYPE_CONST, {.i64=SQRT},   0, 0, FLAGS, .unit = "scale" },
        { "cbrt", "cubic root",  0, AV_OPT_TYPE_CONST, {.i64=CBRT},   0, 0, FLAGS, .unit = "scale" },
        { "log",  "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=LOG},    0, 0, FLAGS, .unit = "scale" },
        { "4thrt","4th root",    0, AV_OPT_TYPE_CONST, {.i64=FOURTHRT}, 0, 0, FLAGS, .unit = "scale" },
        { "5thrt","5th root",    0, AV_OPT_TYPE_CONST, {.i64=FIFTHRT},  0, 0, FLAGS, .unit = "scale" },
    { "fscale", "set frequency scale", OFFSET(fscale), AV_OPT_TYPE_INT, {.i64=F_LINEAR}, 0, NB_FSCALES-1, FLAGS, .unit = "fscale" },
        { "lin",  "linear",      0, AV_OPT_TYPE_CONST, {.i64=F_LINEAR}, 0, 0, FLAGS, .unit = "fscale" },
        { "log",  "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=F_LOG},    0, 0, FLAGS, .unit = "fscale" },
    { "saturation", "color saturation multiplier", OFFSET(saturation), AV_OPT_TYPE_FLOAT, {.dbl = 1}, -10, 10, FLAGS },
    WIN_FUNC_OPTION("win_func", OFFSET(win_func), FLAGS, WFUNC_HANNING),
    { "orientation", "set orientation", OFFSET(orientation), AV_OPT_TYPE_INT, {.i64=VERTICAL}, 0, NB_ORIENTATIONS-1, FLAGS, .unit = "orientation" },
        { "vertical",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=VERTICAL},   0, 0, FLAGS, .unit = "orientation" },
        { "horizontal", NULL, 0, AV_OPT_TYPE_CONST, {.i64=HORIZONTAL}, 0, 0, FLAGS, .unit = "orientation" },
    { "overlap", "set window overlap", OFFSET(overlap), AV_OPT_TYPE_FLOAT, {.dbl = 0}, 0, 1, FLAGS },
    { "gain", "set scale gain", OFFSET(gain), AV_OPT_TYPE_FLOAT, {.dbl = 1}, 0, 128, FLAGS },
    { "data", "set data mode", OFFSET(data), AV_OPT_TYPE_INT, {.i64 = 0}, 0, NB_DMODES-1, FLAGS, .unit = "data" },
        { "magnitude", NULL, 0, AV_OPT_TYPE_CONST, {.i64=D_MAGNITUDE}, 0, 0, FLAGS, .unit = "data" },
        { "phase",     NULL, 0, AV_OPT_TYPE_CONST, {.i64=D_PHASE},     0, 0, FLAGS, .unit = "data" },
        { "uphase",    NULL, 0, AV_OPT_TYPE_CONST, {.i64=D_UPHASE},    0, 0, FLAGS, .unit = "data" },
    { "rotation", "color rotation", OFFSET(rotation), AV_OPT_TYPE_FLOAT, {.dbl = 0}, -1, 1, FLAGS },
    { "start", "start frequency", OFFSET(start), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT32_MAX, FLAGS },
    { "stop",  "stop frequency",  OFFSET(stop),  AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT32_MAX, FLAGS },
    { "fps",   "set video rate",  OFFSET(rate_str), AV_OPT_TYPE_STRING, {.str = "auto"}, 0, 0, FLAGS },
    { "legend", "draw legend", OFFSET(legend), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS },
    { "drange", "set dynamic range in dBFS", OFFSET(drange), AV_OPT_TYPE_FLOAT, {.dbl = 120}, 10, 200, FLAGS },
    { "limit", "set upper limit in dBFS", OFFSET(limit), AV_OPT_TYPE_FLOAT, {.dbl = 0}, -100, 100, FLAGS },
    { "opacity", "set opacity strength", OFFSET(opacity_factor), AV_OPT_TYPE_FLOAT, {.dbl = 1}, 0, 10, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(showspectrum);

static const struct ColorTable {
    float a, y, u, v;
} color_table[][8] = {
    [INTENSITY] = {
    {    0,                  0,                  0,                   0 },
    { 0.13, .03587126228984074,  .1573300977624594, -.02548747583751842 },
    { 0.30, .18572281794568020,  .1772436246393981,  .17475554840414750 },
    { 0.60, .28184980583656130, -.1593064119945782,  .47132074554608920 },
    { 0.73, .65830621175547810, -.3716070802232764,  .24352759331252930 },
    { 0.78, .76318535758242900, -.4307467689263783,  .16866496622310430 },
    { 0.91, .95336363636363640, -.2045454545454546,  .03313636363636363 },
    {    1,                  1,                  0,                   0 }},
    [RAINBOW] = {
    {    0,                  0,                  0,                   0 },
    { 0.13,            44/256.,     (189-128)/256.,      (138-128)/256. },
    { 0.25,            29/256.,     (186-128)/256.,      (119-128)/256. },
    { 0.38,           119/256.,     (194-128)/256.,       (53-128)/256. },
    { 0.60,           111/256.,      (73-128)/256.,       (59-128)/256. },
    { 0.73,           205/256.,      (19-128)/256.,      (149-128)/256. },
    { 0.86,           135/256.,      (83-128)/256.,      (200-128)/256. },
    {    1,            73/256.,      (95-128)/256.,      (225-128)/256. }},
    [MORELAND] = {
    {    0,            44/256.,     (181-128)/256.,      (112-128)/256. },
    { 0.13,           126/256.,     (177-128)/256.,      (106-128)/256. },
    { 0.25,           164/256.,     (163-128)/256.,      (109-128)/256. },
    { 0.38,           200/256.,     (140-128)/256.,      (120-128)/256. },
    { 0.60,           201/256.,     (117-128)/256.,      (141-128)/256. },
    { 0.73,           177/256.,     (103-128)/256.,      (165-128)/256. },
    { 0.86,           136/256.,     (100-128)/256.,      (183-128)/256. },
    {    1,            68/256.,     (117-128)/256.,      (203-128)/256. }},
    [NEBULAE] = {
    {    0,            10/256.,     (134-128)/256.,      (132-128)/256. },
    { 0.23,            21/256.,     (137-128)/256.,      (130-128)/256. },
    { 0.45,            35/256.,     (134-128)/256.,      (134-128)/256. },
    { 0.57,            51/256.,     (130-128)/256.,      (139-128)/256. },
    { 0.67,           104/256.,     (116-128)/256.,      (162-128)/256. },
    { 0.77,           120/256.,     (105-128)/256.,      (188-128)/256. },
    { 0.87,           140/256.,     (105-128)/256.,      (188-128)/256. },
    {    1,                  1,                  0,                   0 }},
    [FIRE] = {
    {    0,                  0,                  0,                   0 },
    { 0.23,            44/256.,     (132-128)/256.,      (127-128)/256. },
    { 0.45,            62/256.,     (116-128)/256.,      (140-128)/256. },
    { 0.57,            75/256.,     (105-128)/256.,      (152-128)/256. },
    { 0.67,            95/256.,      (91-128)/256.,      (166-128)/256. },
    { 0.77,           126/256.,      (74-128)/256.,      (172-128)/256. },
    { 0.87,           164/256.,      (73-128)/256.,      (162-128)/256. },
    {    1,                  1,                  0,                   0 }},
    [FIERY] = {
    {    0,                  0,                  0,                   0 },
    { 0.23,            36/256.,     (116-128)/256.,      (163-128)/256. },
    { 0.45,            52/256.,     (102-128)/256.,      (200-128)/256. },
    { 0.57,           116/256.,      (84-128)/256.,      (196-128)/256. },
    { 0.67,           157/256.,      (67-128)/256.,      (181-128)/256. },
    { 0.77,           193/256.,      (40-128)/256.,      (155-128)/256. },
    { 0.87,           221/256.,     (101-128)/256.,      (134-128)/256. },
    {    1,                  1,                  0,                   0 }},
    [FRUIT] = {
    {    0,                  0,                  0,                   0 },
    { 0.20,            29/256.,     (136-128)/256.,      (119-128)/256. },
    { 0.30,            60/256.,     (119-128)/256.,       (90-128)/256. },
    { 0.40,            85/256.,      (91-128)/256.,       (85-128)/256. },
    { 0.50,           116/256.,      (70-128)/256.,      (105-128)/256. },
    { 0.60,           151/256.,      (50-128)/256.,      (146-128)/256. },
    { 0.70,           191/256.,      (63-128)/256.,      (178-128)/256. },
    {    1,            98/256.,      (80-128)/256.,      (221-128)/256. }},
    [COOL] = {
    {    0,                  0,                  0,                   0 },
    {  .15,                  0,                 .5,                 -.5 },
    {    1,                  1,                -.5,                  .5 }},
    [MAGMA] = {
    {    0,                  0,                  0,                   0 },
    { 0.10,            23/256.,     (175-128)/256.,      (120-128)/256. },
    { 0.23,            43/256.,     (158-128)/256.,      (144-128)/256. },
    { 0.35,            85/256.,     (138-128)/256.,      (179-128)/256. },
    { 0.48,            96/256.,     (128-128)/256.,      (189-128)/256. },
    { 0.64,           128/256.,     (103-128)/256.,      (214-128)/256. },
    { 0.92,           205/256.,      (80-128)/256.,      (152-128)/256. },
    {    1,                  1,                  0,                   0 }},
    [GREEN] = {
    {    0,                  0,                  0,                   0 },
    {  .75,                 .5,                  0,                 -.5 },
    {    1,                  1,                  0,                   0 }},
    [VIRIDIS] = {
    {    0,                  0,                  0,                   0 },
    { 0.10,          0x39/255.,   (0x9D -128)/255.,    (0x8F -128)/255. },
    { 0.23,          0x5C/255.,   (0x9A -128)/255.,    (0x68 -128)/255. },
    { 0.35,          0x69/255.,   (0x93 -128)/255.,    (0x57 -128)/255. },
    { 0.48,          0x76/255.,   (0x88 -128)/255.,    (0x4B -128)/255. },
    { 0.64,          0x8A/255.,   (0x72 -128)/255.,    (0x4F -128)/255. },
    { 0.80,          0xA3/255.,   (0x50 -128)/255.,    (0x66 -128)/255. },
    {    1,          0xCC/255.,   (0x2F -128)/255.,    (0x87 -128)/255. }},
    [PLASMA] = {
    {    0,                  0,                  0,                   0 },
    { 0.10,          0x27/255.,   (0xC2 -128)/255.,    (0x82 -128)/255. },
    { 0.58,          0x5B/255.,   (0x9A -128)/255.,    (0xAE -128)/255. },
    { 0.70,          0x89/255.,   (0x44 -128)/255.,    (0xAB -128)/255. },
    { 0.80,          0xB4/255.,   (0x2B -128)/255.,    (0x9E -128)/255. },
    { 0.91,          0xD2/255.,   (0x38 -128)/255.,    (0x92 -128)/255. },
    {    1,                  1,                  0,                  0. }},
    [CIVIDIS] = {
    {    0,                  0,                  0,                   0 },
    { 0.20,          0x28/255.,   (0x98 -128)/255.,    (0x6F -128)/255. },
    { 0.50,          0x48/255.,   (0x95 -128)/255.,    (0x74 -128)/255. },
    { 0.63,          0x69/255.,   (0x84 -128)/255.,    (0x7F -128)/255. },
    { 0.76,          0x89/255.,   (0x75 -128)/255.,    (0x84 -128)/255. },
    { 0.90,          0xCE/255.,   (0x35 -128)/255.,    (0x95 -128)/255. },
    {    1,                  1,                  0,                  0. }},
    [TERRAIN] = {
    {    0,                  0,                  0,                   0 },
    { 0.15,                  0,                 .5,                   0 },
    { 0.60,                  1,                -.5,                 -.5 },
    { 0.85,                  1,                -.5,                  .5 },
    {    1,                  1,                  0,                   0 }},
};

static av_cold void uninit(AVFilterContext *ctx)
{
    ShowSpectrumContext *s = ctx->priv;
    int i;

    av_freep(&s->combine_buffer);
    if (s->fft) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_tx_uninit(&s->fft[i]);
    }
    av_freep(&s->fft);
    if (s->ifft) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_tx_uninit(&s->ifft[i]);
    }
    av_freep(&s->ifft);
    if (s->fft_data) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_freep(&s->fft_data[i]);
    }
    av_freep(&s->fft_data);
    if (s->fft_in) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_freep(&s->fft_in[i]);
    }
    av_freep(&s->fft_in);
    if (s->fft_scratch) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_freep(&s->fft_scratch[i]);
    }
    av_freep(&s->fft_scratch);
    if (s->color_buffer) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_freep(&s->color_buffer[i]);
    }
    av_freep(&s->color_buffer);
    av_freep(&s->window_func_lut);
    if (s->magnitudes) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_freep(&s->magnitudes[i]);
    }
    av_freep(&s->magnitudes);
    av_frame_free(&s->outpicref);
    av_frame_free(&s->in_frame);
    if (s->phases) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_freep(&s->phases[i]);
    }
    av_freep(&s->phases);

    while (s->nb_frames > 0) {
        av_frame_free(&s->frames[s->nb_frames - 1]);
        s->nb_frames--;
    }

    av_freep(&s->frames);
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    AVFilterFormats *formats = NULL;
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVA444P, AV_PIX_FMT_NONE };
    int ret;

    /* set input audio formats */
    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &cfg_in[0]->formats)) < 0)
        return ret;

    /* set output video format */
    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &cfg_out[0]->formats)) < 0)
        return ret;

    return 0;
}

static int run_channel_fft(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ShowSpectrumContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const float *window_func_lut = s->window_func_lut;
    AVFrame *fin = arg;
    const int ch = jobnr;
    int n;

    /* fill FFT input with the number of samples available */
    const float *p = (float *)fin->extended_data[ch];
    float *in_frame = (float *)s->in_frame->extended_data[ch];

    memmove(in_frame, in_frame + s->hop_size, (s->fft_size - s->hop_size) * sizeof(float));
    memcpy(in_frame + s->fft_size - s->hop_size, p, fin->nb_samples * sizeof(float));

    for (int i = fin->nb_samples; i < s->hop_size; i++)
        in_frame[i + s->fft_size - s->hop_size] = 0.f;

    if (s->stop) {
        float theta, phi, psi, a, b, S, c;
        AVComplexFloat *f = s->fft_in[ch];
        AVComplexFloat *g = s->fft_data[ch];
        AVComplexFloat *h = s->fft_scratch[ch];
        int L = s->buf_size;
        int N = s->win_size;
        int M = s->win_size / 2;

        for (n = 0; n < s->win_size; n++) {
            s->fft_data[ch][n].re = in_frame[n] * window_func_lut[n];
            s->fft_data[ch][n].im = 0;
        }

        phi = 2.f * M_PI * (s->stop - s->start) / (float)inlink->sample_rate / (M - 1);
        theta = 2.f * M_PI * s->start / (float)inlink->sample_rate;

        for (int n = 0; n < M; n++) {
            h[n].re = cosf(n * n / 2.f * phi);
            h[n].im = sinf(n * n / 2.f * phi);
        }

        for (int n = M; n < L; n++) {
            h[n].re = 0.f;
            h[n].im = 0.f;
        }

        for (int n = L - N; n < L; n++) {
            h[n].re = cosf((L - n) * (L - n) / 2.f * phi);
            h[n].im = sinf((L - n) * (L - n) / 2.f * phi);
        }

        for (int n = N; n < L; n++) {
            g[n].re = 0.f;
            g[n].im = 0.f;
        }

        for (int n = 0; n < N; n++) {
            psi = n * theta + n * n / 2.f * phi;
            c =  cosf(psi);
            S = -sinf(psi);
            a = c * g[n].re - S * g[n].im;
            b = S * g[n].re + c * g[n].im;
            g[n].re = a;
            g[n].im = b;
        }

        memcpy(f, h, s->buf_size * sizeof(*f));
        s->tx_fn(s->fft[ch], h, f, sizeof(AVComplexFloat));

        memcpy(f, g, s->buf_size * sizeof(*f));
        s->tx_fn(s->fft[ch], g, f, sizeof(AVComplexFloat));

        for (int n = 0; n < L; n++) {
            c = g[n].re;
            S = g[n].im;
            a = c * h[n].re - S * h[n].im;
            b = S * h[n].re + c * h[n].im;

            g[n].re = a / L;
            g[n].im = b / L;
        }

        memcpy(f, g, s->buf_size * sizeof(*f));
        s->itx_fn(s->ifft[ch], g, f, sizeof(AVComplexFloat));

        for (int k = 0; k < M; k++) {
            psi = k * k / 2.f * phi;
            c =  cosf(psi);
            S = -sinf(psi);
            a = c * g[k].re - S * g[k].im;
            b = S * g[k].re + c * g[k].im;
            s->fft_data[ch][k].re = a;
            s->fft_data[ch][k].im = b;
        }
    } else {
        for (n = 0; n < s->win_size; n++) {
            s->fft_in[ch][n].re = in_frame[n] * window_func_lut[n];
            s->fft_in[ch][n].im = 0;
        }

        /* run FFT on each samples set */
        s->tx_fn(s->fft[ch], s->fft_data[ch], s->fft_in[ch], sizeof(AVComplexFloat));
    }

    return 0;
}

static void drawtext(AVFrame *pic, int x, int y, const char *txt, int o)
{
    const uint8_t *font;
    int font_height;

    font = avpriv_cga_font,   font_height =  8;

    for (int i = 0; txt[i]; i++) {
        int char_y, mask;

        if (o) {
            for (char_y = font_height - 1; char_y >= 0; char_y--) {
                uint8_t *p = pic->data[0] + (y + i * 10) * pic->linesize[0] + x;
                for (mask = 0x80; mask; mask >>= 1) {
                    if (font[txt[i] * font_height + font_height - 1 - char_y] & mask)
                        p[char_y] = ~p[char_y];
                    p += pic->linesize[0];
                }
            }
        } else {
            uint8_t *p = pic->data[0] + y*pic->linesize[0] + (x + i*8);
            for (char_y = 0; char_y < font_height; char_y++) {
                for (mask = 0x80; mask; mask >>= 1) {
                    if (font[txt[i] * font_height + char_y] & mask)
                        *p = ~(*p);
                    p++;
                }
                p += pic->linesize[0] - 8;
            }
        }
    }

    for (int i = 0; txt[i] && pic->data[3]; i++) {
        int char_y, mask;

        if (o) {
            for (char_y = font_height - 1; char_y >= 0; char_y--) {
                uint8_t *p = pic->data[3] + (y + i * 10) * pic->linesize[3] + x;
                for (mask = 0x80; mask; mask >>= 1) {
                    for (int k = 0; k < 8; k++)
                        p[k] = 255;
                    p += pic->linesize[3];
                }
            }
        } else {
            uint8_t *p = pic->data[3] + y*pic->linesize[3] + (x + i*8);
            for (char_y = 0; char_y < font_height; char_y++) {
                for (mask = 0x80; mask; mask >>= 1)
                    *p++ = 255;
                p += pic->linesize[3] - 8;
            }
        }
    }
}

static void color_range(ShowSpectrumContext *s, int ch,
                        float *yf, float *uf, float *vf)
{
    switch (s->mode) {
    case COMBINED:
        // reduce range by channel count
        *yf = 256.0f / s->nb_display_channels;
        switch (s->color_mode) {
        case RAINBOW:
        case MORELAND:
        case NEBULAE:
        case FIRE:
        case FIERY:
        case FRUIT:
        case COOL:
        case GREEN:
        case VIRIDIS:
        case PLASMA:
        case CIVIDIS:
        case TERRAIN:
        case MAGMA:
        case INTENSITY:
            *uf = *yf;
            *vf = *yf;
            break;
        case CHANNEL:
            /* adjust saturation for mixed UV coloring */
            /* this factor is correct for infinite channels, an approximation otherwise */
            *uf = *yf * M_PI;
            *vf = *yf * M_PI;
            break;
        default:
            av_assert0(0);
        }
        break;
    case SEPARATE:
        // full range
        *yf = 256.0f;
        *uf = 256.0f;
        *vf = 256.0f;
        break;
    default:
        av_assert0(0);
    }

    if (s->color_mode == CHANNEL) {
        if (s->nb_display_channels > 1) {
            *uf *= 0.5f * sinf((2 * M_PI * ch) / s->nb_display_channels + M_PI * s->rotation);
            *vf *= 0.5f * cosf((2 * M_PI * ch) / s->nb_display_channels + M_PI * s->rotation);
        } else {
            *uf *= 0.5f * sinf(M_PI * s->rotation);
            *vf *= 0.5f * cosf(M_PI * s->rotation + M_PI_2);
        }
    } else {
        *uf += *uf * sinf(M_PI * s->rotation);
        *vf += *vf * cosf(M_PI * s->rotation + M_PI_2);
    }

    *uf *= s->saturation;
    *vf *= s->saturation;
}

static void pick_color(ShowSpectrumContext *s,
                       float yf, float uf, float vf,
                       float a, float *out)
{
    const float af = s->opacity_factor * 255.f;

    if (s->color_mode > CHANNEL) {
        const int cm = s->color_mode;
        float y, u, v;
        int i;

        for (i = 1; i < FF_ARRAY_ELEMS(color_table[cm]) - 1; i++)
            if (color_table[cm][i].a >= a)
                break;
        // i now is the first item >= the color
        // now we know to interpolate between item i - 1 and i
        if (a <= color_table[cm][i - 1].a) {
            y = color_table[cm][i - 1].y;
            u = color_table[cm][i - 1].u;
            v = color_table[cm][i - 1].v;
        } else if (a >= color_table[cm][i].a) {
            y = color_table[cm][i].y;
            u = color_table[cm][i].u;
            v = color_table[cm][i].v;
        } else {
            float start = color_table[cm][i - 1].a;
            float end = color_table[cm][i].a;
            float lerpfrac = (a - start) / (end - start);
            y = color_table[cm][i - 1].y * (1.0f - lerpfrac)
              + color_table[cm][i].y * lerpfrac;
            u = color_table[cm][i - 1].u * (1.0f - lerpfrac)
              + color_table[cm][i].u * lerpfrac;
            v = color_table[cm][i - 1].v * (1.0f - lerpfrac)
              + color_table[cm][i].v * lerpfrac;
        }

        out[0] = y * yf;
        out[1] = u * uf;
        out[2] = v * vf;
        out[3] = a * af;
    } else {
        out[0] = a * yf;
        out[1] = a * uf;
        out[2] = a * vf;
        out[3] = a * af;
    }
}

static char *get_time(AVFilterContext *ctx, float seconds, int x)
{
    char *units;

    if (x == 0)
        units = av_asprintf("0");
    else if (log10(seconds) > 6)
        units = av_asprintf("%.2fh", seconds / (60 * 60));
    else if (log10(seconds) > 3)
        units = av_asprintf("%.2fm", seconds / 60);
    else
        units = av_asprintf("%.2fs", seconds);
    return units;
}

static float log_scale(const float bin,
                       const float bmin, const float bmax,
                       const float min, const float max)
{
    return exp2f(((bin - bmin) / (bmax - bmin)) * (log2f(max) - log2f(min)) + log2f(min));
}

static float get_hz(const float bin, const float bmax,
                    const float min, const float max,
                    int fscale)
{
    switch (fscale) {
    case F_LINEAR:
        return min + (bin / bmax) * (max - min);
    case F_LOG:
        return min + log_scale(bin, 0, bmax, 20.f, max - min);
    default:
        return 0.f;
    }
}

static float inv_log_scale(float bin,
                           float bmin, float bmax,
                           float min, float max)
{
    return (min * exp2f((bin * (log2f(max) - log2f(20.f))) / bmax) + min) * bmax / max;
}

static float bin_pos(const int bin, const int num_bins, const float min, const float max)
{
    return inv_log_scale(bin, 0.f, num_bins, 20.f, max - min);
}

static float get_scale(AVFilterContext *ctx, int scale, float a)
{
    ShowSpectrumContext *s = ctx->priv;
    const float dmin = s->dmin;
    const float dmax = s->dmax;

    a = av_clipf(a, dmin, dmax);
    if (scale != LOG)
        a = (a - dmin) / (dmax - dmin);

    switch (scale) {
    case LINEAR:
        break;
    case SQRT:
        a = sqrtf(a);
        break;
    case CBRT:
        a = cbrtf(a);
        break;
    case FOURTHRT:
        a = sqrtf(sqrtf(a));
        break;
    case FIFTHRT:
        a = powf(a, 0.2f);
        break;
    case LOG:
        a = (s->drange - s->limit + log10f(a) * 20.f) / s->drange;
        break;
    default:
        av_assert0(0);
    }

    return a;
}

static float get_iscale(AVFilterContext *ctx, int scale, float a)
{
    ShowSpectrumContext *s = ctx->priv;
    const float dmin = s->dmin;
    const float dmax = s->dmax;

    switch (scale) {
    case LINEAR:
        break;
    case SQRT:
        a = a * a;
        break;
    case CBRT:
        a = a * a * a;
        break;
    case FOURTHRT:
        a = a * a * a * a;
        break;
    case FIFTHRT:
        a = a * a * a * a * a;
        break;
    case LOG:
        a = expf(M_LN10 * (a * s->drange - s->drange + s->limit) / 20.f);
        break;
    default:
        av_assert0(0);
    }

    if (scale != LOG)
        a = a * (dmax - dmin) + dmin;

    return a;
}

static int draw_legend(AVFilterContext *ctx, uint64_t samples)
{
    ShowSpectrumContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    int ch, y, x = 0, sz = s->orientation == VERTICAL ? s->w : s->h;
    int multi = (s->mode == SEPARATE && s->color_mode == CHANNEL);
    float spp = samples / (float)sz;
    char *text;
    uint8_t *dst;
    char chlayout_str[128];

    av_channel_layout_describe(&inlink->ch_layout, chlayout_str, sizeof(chlayout_str));

    text = av_asprintf("%d Hz | %s", inlink->sample_rate, chlayout_str);
    if (!text)
        return AVERROR(ENOMEM);

    drawtext(s->outpicref, 2, outlink->h - 10, "CREATED BY LIBAVFILTER", 0);
    drawtext(s->outpicref, outlink->w - 2 - strlen(text) * 10, outlink->h - 10, text, 0);
    av_freep(&text);
    if (s->stop) {
        text = av_asprintf("Zoom: %d Hz - %d Hz", s->start, s->stop);
        if (!text)
            return AVERROR(ENOMEM);
        drawtext(s->outpicref, outlink->w - 2 - strlen(text) * 10, 3, text, 0);
        av_freep(&text);
    }

    dst = s->outpicref->data[0] + (s->start_y - 1) * s->outpicref->linesize[0] + s->start_x - 1;
    for (x = 0; x < s->w + 1; x++)
        dst[x] = 200;
    dst = s->outpicref->data[0] + (s->start_y + s->h) * s->outpicref->linesize[0] + s->start_x - 1;
    for (x = 0; x < s->w + 1; x++)
        dst[x] = 200;
    for (y = 0; y < s->h + 2; y++) {
        dst = s->outpicref->data[0] + (y + s->start_y - 1) * s->outpicref->linesize[0];
        dst[s->start_x - 1] = 200;
        dst[s->start_x + s->w] = 200;
    }
    if (s->orientation == VERTICAL) {
        int h = s->mode == SEPARATE ? s->h / s->nb_display_channels : s->h;
        int hh = s->mode == SEPARATE ? -(s->h % s->nb_display_channels) + 1 : 1;
        for (ch = 0; ch < (s->mode == SEPARATE ? s->nb_display_channels : 1); ch++) {
            for (y = 0; y < h; y += 20) {
                dst = s->outpicref->data[0] + (s->start_y + h * (ch + 1) - y - hh) * s->outpicref->linesize[0];
                dst[s->start_x - 2] = 200;
                dst[s->start_x + s->w + 1] = 200;
            }
            for (y = 0; y < h; y += 40) {
                dst = s->outpicref->data[0] + (s->start_y + h * (ch + 1) - y - hh) * s->outpicref->linesize[0];
                dst[s->start_x - 3] = 200;
                dst[s->start_x + s->w + 2] = 200;
            }
            dst = s->outpicref->data[0] + (s->start_y - 2) * s->outpicref->linesize[0] + s->start_x;
            for (x = 0; x < s->w; x+=40)
                dst[x] = 200;
            dst = s->outpicref->data[0] + (s->start_y - 3) * s->outpicref->linesize[0] + s->start_x;
            for (x = 0; x < s->w; x+=80)
                dst[x] = 200;
            dst = s->outpicref->data[0] + (s->h + s->start_y + 1) * s->outpicref->linesize[0] + s->start_x;
            for (x = 0; x < s->w; x+=40) {
                dst[x] = 200;
            }
            dst = s->outpicref->data[0] + (s->h + s->start_y + 2) * s->outpicref->linesize[0] + s->start_x;
            for (x = 0; x < s->w; x+=80) {
                dst[x] = 200;
            }
            for (y = 0; y < h; y += 40) {
                float range = s->stop ? s->stop - s->start : inlink->sample_rate / 2;
                float hertz = get_hz(y, h, s->start, s->start + range, s->fscale);
                char *units;

                if (hertz == 0)
                    units = av_asprintf("DC");
                else
                    units = av_asprintf("%.2f", hertz);
                if (!units)
                    return AVERROR(ENOMEM);

                drawtext(s->outpicref, s->start_x - 8 * strlen(units) - 4, h * (ch + 1) + s->start_y - y - 4 - hh, units, 0);
                av_free(units);
            }
        }

        for (x = 0; x < s->w && s->single_pic; x+=80) {
            float seconds = x * spp / inlink->sample_rate;
            char *units = get_time(ctx, seconds, x);
            if (!units)
                return AVERROR(ENOMEM);

            drawtext(s->outpicref, s->start_x + x - 4 * strlen(units), s->h + s->start_y + 6, units, 0);
            drawtext(s->outpicref, s->start_x + x - 4 * strlen(units), s->start_y - 12, units, 0);
            av_free(units);
        }

        drawtext(s->outpicref, outlink->w / 2 - 4 * 4, outlink->h - s->start_y / 2, "TIME", 0);
        drawtext(s->outpicref, s->start_x / 7, outlink->h / 2 - 14 * 4, "FREQUENCY (Hz)", 1);
    } else {
        int w = s->mode == SEPARATE ? s->w / s->nb_display_channels : s->w;
        for (y = 0; y < s->h; y += 20) {
            dst = s->outpicref->data[0] + (s->start_y + y) * s->outpicref->linesize[0];
            dst[s->start_x - 2] = 200;
            dst[s->start_x + s->w + 1] = 200;
        }
        for (y = 0; y < s->h; y += 40) {
            dst = s->outpicref->data[0] + (s->start_y + y) * s->outpicref->linesize[0];
            dst[s->start_x - 3] = 200;
            dst[s->start_x + s->w + 2] = 200;
        }
        for (ch = 0; ch < (s->mode == SEPARATE ? s->nb_display_channels : 1); ch++) {
            dst = s->outpicref->data[0] + (s->start_y - 2) * s->outpicref->linesize[0] + s->start_x + w * ch;
            for (x = 0; x < w; x+=40)
                dst[x] = 200;
            dst = s->outpicref->data[0] + (s->start_y - 3) * s->outpicref->linesize[0] + s->start_x + w * ch;
            for (x = 0; x < w; x+=80)
                dst[x] = 200;
            dst = s->outpicref->data[0] + (s->h + s->start_y + 1) * s->outpicref->linesize[0] + s->start_x + w * ch;
            for (x = 0; x < w; x+=40) {
                dst[x] = 200;
            }
            dst = s->outpicref->data[0] + (s->h + s->start_y + 2) * s->outpicref->linesize[0] + s->start_x + w * ch;
            for (x = 0; x < w; x+=80) {
                dst[x] = 200;
            }
            for (x = 0; x < w - 79; x += 80) {
                float range = s->stop ? s->stop - s->start : inlink->sample_rate / 2;
                float hertz = get_hz(x, w, s->start, s->start + range, s->fscale);
                char *units;

                if (hertz == 0)
                    units = av_asprintf("DC");
                else
                    units = av_asprintf("%.2f", hertz);
                if (!units)
                    return AVERROR(ENOMEM);

                drawtext(s->outpicref, s->start_x - 4 * strlen(units) + x + w * ch, s->start_y - 12, units, 0);
                drawtext(s->outpicref, s->start_x - 4 * strlen(units) + x + w * ch, s->h + s->start_y + 6, units, 0);
                av_free(units);
            }
        }
        for (y = 0; y < s->h && s->single_pic; y+=40) {
            float seconds = y * spp / inlink->sample_rate;
            char *units = get_time(ctx, seconds, x);
            if (!units)
                return AVERROR(ENOMEM);

            drawtext(s->outpicref, s->start_x - 8 * strlen(units) - 4, s->start_y + y - 4, units, 0);
            av_free(units);
        }
        drawtext(s->outpicref, s->start_x / 7, outlink->h / 2 - 4 * 4, "TIME", 1);
        drawtext(s->outpicref, outlink->w / 2 - 14 * 4, outlink->h - s->start_y / 2, "FREQUENCY (Hz)", 0);
    }

    for (ch = 0; ch < (multi ? s->nb_display_channels : 1); ch++) {
        int h = multi ? s->h / s->nb_display_channels : s->h;

        for (y = 0; y < h; y++) {
            float out[4] = { 0., 127.5, 127.5, 0.f};
            int chn;

            for (chn = 0; chn < (s->mode == SEPARATE ? 1 : s->nb_display_channels); chn++) {
                float yf, uf, vf;
                int channel = (multi) ? s->nb_display_channels - ch - 1 : chn;
                float lout[4];

                color_range(s, channel, &yf, &uf, &vf);
                pick_color(s, yf, uf, vf, y / (float)h, lout);
                out[0] += lout[0];
                out[1] += lout[1];
                out[2] += lout[2];
                out[3] += lout[3];
            }
            memset(s->outpicref->data[0]+(s->start_y + h * (ch + 1) - y - 1) * s->outpicref->linesize[0] + s->w + s->start_x + 20, av_clip_uint8(out[0]), 10);
            memset(s->outpicref->data[1]+(s->start_y + h * (ch + 1) - y - 1) * s->outpicref->linesize[1] + s->w + s->start_x + 20, av_clip_uint8(out[1]), 10);
            memset(s->outpicref->data[2]+(s->start_y + h * (ch + 1) - y - 1) * s->outpicref->linesize[2] + s->w + s->start_x + 20, av_clip_uint8(out[2]), 10);
            if (s->outpicref->data[3])
                memset(s->outpicref->data[3]+(s->start_y + h * (ch + 1) - y - 1) * s->outpicref->linesize[3] + s->w + s->start_x + 20, av_clip_uint8(out[3]), 10);
        }

        for (y = 0; ch == 0 && y < h + 5; y += 25) {
            static const char *log_fmt = "%.0f";
            static const char *lin_fmt = "%.3f";
            const float a = av_clipf(1.f - y / (float)(h - 1), 0.f, 1.f);
            const float value = s->scale == LOG ? log10f(get_iscale(ctx, s->scale, a)) * 20.f : get_iscale(ctx, s->scale, a);
            char *text;

            text = av_asprintf(s->scale == LOG ? log_fmt : lin_fmt, value);
            if (!text)
                continue;
            drawtext(s->outpicref, s->w + s->start_x + 35, s->start_y + y - 3, text, 0);
            av_free(text);
        }
    }

    if (s->scale == LOG)
        drawtext(s->outpicref, s->w + s->start_x + 22, s->start_y + s->h + 20, "dBFS", 0);

    return 0;
}

static float get_value(AVFilterContext *ctx, int ch, int y)
{
    ShowSpectrumContext *s = ctx->priv;
    float *magnitudes = s->magnitudes[ch];
    float *phases = s->phases[ch];
    float a;

    switch (s->data) {
    case D_MAGNITUDE:
        /* get magnitude */
        a = magnitudes[y];
        break;
    case D_UPHASE:
    case D_PHASE:
        /* get phase */
        a = phases[y];
        break;
    default:
        av_assert0(0);
    }

    return av_clipf(get_scale(ctx, s->scale, a), 0.f, 1.f);
}

static int plot_channel_lin(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ShowSpectrumContext *s = ctx->priv;
    const int h = s->orientation == VERTICAL ? s->channel_height : s->channel_width;
    const int ch = jobnr;
    float yf, uf, vf;
    int y;

    /* decide color range */
    color_range(s, ch, &yf, &uf, &vf);

    /* draw the channel */
    for (y = 0; y < h; y++) {
        int row = (s->mode == COMBINED) ? y : ch * h + y;
        float *out = &s->color_buffer[ch][4 * row];
        float a = get_value(ctx, ch, y);

        pick_color(s, yf, uf, vf, a, out);
    }

    return 0;
}

static int plot_channel_log(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ShowSpectrumContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    const int h = s->orientation == VERTICAL ? s->channel_height : s->channel_width;
    const int ch = jobnr;
    float yf, uf, vf;

    /* decide color range */
    color_range(s, ch, &yf, &uf, &vf);

    /* draw the channel */
    for (int yy = 0; yy < h; yy++) {
        float range = s->stop ? s->stop - s->start : inlink->sample_rate / 2;
        float pos = bin_pos(yy, h, s->start, s->start + range);
        float delta = pos - floorf(pos);
        float a0, a1;

        a0 = get_value(ctx, ch, av_clip(pos, 0, h-1));
        a1 = get_value(ctx, ch, av_clip(pos+1, 0, h-1));
        {
            int row = (s->mode == COMBINED) ? yy : ch * h + yy;
            float *out = &s->color_buffer[ch][4 * row];

            pick_color(s, yf, uf, vf, delta * a1 + (1.f - delta) * a0, out);
        }
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    FilterLink *l = ff_filter_link(outlink);
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowSpectrumContext *s = ctx->priv;
    int i, fft_size, h, w, ret;
    float overlap;

    s->old_pts = AV_NOPTS_VALUE;
    s->dmax = expf(s->limit * M_LN10 / 20.f);
    s->dmin = expf((s->limit - s->drange) * M_LN10 / 20.f);

    switch (s->fscale) {
    case F_LINEAR: s->plot_channel = plot_channel_lin; break;
    case F_LOG:    s->plot_channel = plot_channel_log; break;
    default: return AVERROR_BUG;
    }

    s->stop = FFMIN(s->stop, inlink->sample_rate / 2);
    if ((s->stop || s->start) && s->stop <= s->start) {
        av_log(ctx, AV_LOG_ERROR, "Stop frequency should be greater than start.\n");
        return AVERROR(EINVAL);
    }

    if (!strcmp(ctx->filter->name, "showspectrumpic"))
        s->single_pic = 1;

    outlink->w = s->w;
    outlink->h = s->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};

    if (s->legend) {
        s->start_x = (log10(inlink->sample_rate) + 1) * 25;
        s->start_y = 64;
        outlink->w += s->start_x * 2;
        outlink->h += s->start_y * 2;
    }

    h = (s->mode == COMBINED || s->orientation == HORIZONTAL) ? s->h : s->h / inlink->ch_layout.nb_channels;
    w = (s->mode == COMBINED || s->orientation == VERTICAL)   ? s->w : s->w / inlink->ch_layout.nb_channels;
    s->channel_height = h;
    s->channel_width  = w;

    if (s->orientation == VERTICAL) {
        /* FFT window size (precision) according to the requested output frame height */
        fft_size = h * 2;
    } else {
        /* FFT window size (precision) according to the requested output frame width */
        fft_size = w * 2;
    }

    s->win_size = fft_size;
    s->buf_size = FFALIGN(s->win_size << (!!s->stop), av_cpu_max_align());

    if (!s->fft) {
        s->fft = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->fft));
        if (!s->fft)
            return AVERROR(ENOMEM);
    }

    if (s->stop) {
        if (!s->ifft) {
            s->ifft = av_calloc(inlink->ch_layout.nb_channels, sizeof(*s->ifft));
            if (!s->ifft)
                return AVERROR(ENOMEM);
        }
    }

    /* (re-)configuration if the video output changed (or first init) */
    if (fft_size != s->fft_size) {
        AVFrame *outpicref;

        s->fft_size = fft_size;

        /* FFT buffers: x2 for each (display) channel buffer.
         * Note: we use free and malloc instead of a realloc-like function to
         * make sure the buffer is aligned in memory for the FFT functions. */
        for (i = 0; i < s->nb_display_channels; i++) {
            if (s->stop) {
                av_tx_uninit(&s->ifft[i]);
                av_freep(&s->fft_scratch[i]);
            }
            av_tx_uninit(&s->fft[i]);
            av_freep(&s->fft_in[i]);
            av_freep(&s->fft_data[i]);
        }
        av_freep(&s->fft_data);

        s->nb_display_channels = inlink->ch_layout.nb_channels;
        for (i = 0; i < s->nb_display_channels; i++) {
            float scale = 1.f;

            ret = av_tx_init(&s->fft[i], &s->tx_fn, AV_TX_FLOAT_FFT, 0, fft_size << (!!s->stop), &scale, 0);
            if (s->stop) {
                ret = av_tx_init(&s->ifft[i], &s->itx_fn, AV_TX_FLOAT_FFT, 1, fft_size << (!!s->stop), &scale, 0);
                if (ret < 0) {
                    av_log(ctx, AV_LOG_ERROR, "Unable to create Inverse FFT context. "
                           "The window size might be too high.\n");
                    return ret;
                }
            }
            if (ret < 0) {
                av_log(ctx, AV_LOG_ERROR, "Unable to create FFT context. "
                       "The window size might be too high.\n");
                return ret;
            }
        }

        s->magnitudes = av_calloc(s->nb_display_channels, sizeof(*s->magnitudes));
        if (!s->magnitudes)
            return AVERROR(ENOMEM);
        for (i = 0; i < s->nb_display_channels; i++) {
            s->magnitudes[i] = av_calloc(s->orientation == VERTICAL ? s->h : s->w, sizeof(**s->magnitudes));
            if (!s->magnitudes[i])
                return AVERROR(ENOMEM);
        }

        s->phases = av_calloc(s->nb_display_channels, sizeof(*s->phases));
        if (!s->phases)
            return AVERROR(ENOMEM);
        for (i = 0; i < s->nb_display_channels; i++) {
            s->phases[i] = av_calloc(s->orientation == VERTICAL ? s->h : s->w, sizeof(**s->phases));
            if (!s->phases[i])
                return AVERROR(ENOMEM);
        }

        av_freep(&s->color_buffer);
        s->color_buffer = av_calloc(s->nb_display_channels, sizeof(*s->color_buffer));
        if (!s->color_buffer)
            return AVERROR(ENOMEM);
        for (i = 0; i < s->nb_display_channels; i++) {
            s->color_buffer[i] = av_calloc(s->orientation == VERTICAL ? s->h * 4 : s->w * 4, sizeof(**s->color_buffer));
            if (!s->color_buffer[i])
                return AVERROR(ENOMEM);
        }

        s->fft_in = av_calloc(s->nb_display_channels, sizeof(*s->fft_in));
        if (!s->fft_in)
            return AVERROR(ENOMEM);
        s->fft_data = av_calloc(s->nb_display_channels, sizeof(*s->fft_data));
        if (!s->fft_data)
            return AVERROR(ENOMEM);
        s->fft_scratch = av_calloc(s->nb_display_channels, sizeof(*s->fft_scratch));
        if (!s->fft_scratch)
            return AVERROR(ENOMEM);
        for (i = 0; i < s->nb_display_channels; i++) {
            s->fft_in[i] = av_calloc(s->buf_size, sizeof(**s->fft_in));
            if (!s->fft_in[i])
                return AVERROR(ENOMEM);

            s->fft_data[i] = av_calloc(s->buf_size, sizeof(**s->fft_data));
            if (!s->fft_data[i])
                return AVERROR(ENOMEM);

            s->fft_scratch[i] = av_calloc(s->buf_size, sizeof(**s->fft_scratch));
            if (!s->fft_scratch[i])
                return AVERROR(ENOMEM);
        }

        /* pre-calc windowing function */
        s->window_func_lut =
            av_realloc_f(s->window_func_lut, s->win_size,
                         sizeof(*s->window_func_lut));
        if (!s->window_func_lut)
            return AVERROR(ENOMEM);
        generate_window_func(s->window_func_lut, s->win_size, s->win_func, &overlap);
        if (s->overlap == 1)
            s->overlap = overlap;
        s->hop_size = (1.f - s->overlap) * s->win_size;
        if (s->hop_size < 1) {
            av_log(ctx, AV_LOG_ERROR, "overlap %f too big\n", s->overlap);
            return AVERROR(EINVAL);
        }

        for (s->win_scale = 0, i = 0; i < s->win_size; i++) {
            s->win_scale += s->window_func_lut[i] * s->window_func_lut[i];
        }
        s->win_scale = 1.f / sqrtf(s->win_scale);

        /* prepare the initial picref buffer (black frame) */
        av_frame_free(&s->outpicref);
        s->outpicref = outpicref =
            ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!outpicref)
            return AVERROR(ENOMEM);
        outpicref->sample_aspect_ratio = (AVRational){1,1};
        for (i = 0; i < outlink->h; i++) {
            memset(outpicref->data[0] + i * outpicref->linesize[0],   0, outlink->w);
            memset(outpicref->data[1] + i * outpicref->linesize[1], 128, outlink->w);
            memset(outpicref->data[2] + i * outpicref->linesize[2], 128, outlink->w);
            if (outpicref->data[3])
                memset(outpicref->data[3] + i * outpicref->linesize[3], 0, outlink->w);
        }
        outpicref->color_range = AVCOL_RANGE_JPEG;

        if (!s->single_pic && s->legend)
            draw_legend(ctx, 0);
    }

    if ((s->orientation == VERTICAL   && s->xpos >= s->w) ||
        (s->orientation == HORIZONTAL && s->xpos >= s->h))
        s->xpos = 0;

    if (s->sliding == LREPLACE) {
        if (s->orientation == VERTICAL)
            s->xpos = s->w - 1;
        if (s->orientation == HORIZONTAL)
            s->xpos = s->h - 1;
    }

    s->auto_frame_rate = av_make_q(inlink->sample_rate, s->hop_size);
    if (s->orientation == VERTICAL && s->sliding == FULLFRAME)
        s->auto_frame_rate = av_mul_q(s->auto_frame_rate, av_make_q(1, s->w));
    if (s->orientation == HORIZONTAL && s->sliding == FULLFRAME)
        s->auto_frame_rate = av_mul_q(s->auto_frame_rate, av_make_q(1, s->h));
    if (!s->single_pic && strcmp(s->rate_str, "auto")) {
        int ret = av_parse_video_rate(&s->frame_rate, s->rate_str);
        if (ret < 0)
            return ret;
    } else if (s->single_pic) {
        s->frame_rate = av_make_q(1, 1);
    } else {
        s->frame_rate = s->auto_frame_rate;
    }
    l->frame_rate = s->frame_rate;
    outlink->time_base = av_inv_q(l->frame_rate);

    if (s->orientation == VERTICAL) {
        s->combine_buffer =
            av_realloc_f(s->combine_buffer, s->h * 4,
                         sizeof(*s->combine_buffer));
    } else {
        s->combine_buffer =
            av_realloc_f(s->combine_buffer, s->w * 4,
                         sizeof(*s->combine_buffer));
    }
    if (!s->combine_buffer)
        return AVERROR(ENOMEM);

    av_log(ctx, AV_LOG_VERBOSE, "s:%dx%d FFT window size:%d\n",
           s->w, s->h, s->win_size);

    s->in_frame = ff_get_audio_buffer(inlink, s->win_size);
    if (!s->in_frame)
        return AVERROR(ENOMEM);

    s->frames = av_fast_realloc(NULL, &s->frames_size,
                                DEFAULT_LENGTH * sizeof(*(s->frames)));
    if (!s->frames)
        return AVERROR(ENOMEM);

    return 0;
}

#define RE(y, ch) s->fft_data[ch][y].re
#define IM(y, ch) s->fft_data[ch][y].im
#define MAGNITUDE(y, ch) hypotf(RE(y, ch), IM(y, ch))
#define PHASE(y, ch) atan2f(IM(y, ch), RE(y, ch))

static int calc_channel_magnitudes(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ShowSpectrumContext *s = ctx->priv;
    const double w = s->win_scale * (s->scale == LOG ? s->win_scale : 1);
    int y, h = s->orientation == VERTICAL ? s->h : s->w;
    const float f = s->gain * w;
    const int ch = jobnr;
    float *magnitudes = s->magnitudes[ch];

    for (y = 0; y < h; y++)
        magnitudes[y] = MAGNITUDE(y, ch) * f;

    return 0;
}

static int calc_channel_phases(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ShowSpectrumContext *s = ctx->priv;
    const int h = s->orientation == VERTICAL ? s->h : s->w;
    const int ch = jobnr;
    float *phases = s->phases[ch];
    int y;

    for (y = 0; y < h; y++)
        phases[y] = (PHASE(y, ch) / M_PI + 1) / 2;

    return 0;
}

static void unwrap(float *x, int N, float tol, float *mi, float *ma)
{
    const float rng = 2.f * M_PI;
    float prev_p = 0.f;
    float max = -FLT_MAX;
    float min = FLT_MAX;

    for (int i = 0; i < N; i++) {
        const float d = x[FFMIN(i + 1, N)] - x[i];
        const float p = ceilf(fabsf(d) / rng) * rng * (((d < tol) > 0.f) - ((d > -tol) > 0.f));

        x[i] += p + prev_p;
        prev_p += p;
        max = fmaxf(x[i], max);
        min = fminf(x[i], min);
    }

    *mi = min;
    *ma = max;
}

static int calc_channel_uphases(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    ShowSpectrumContext *s = ctx->priv;
    const int h = s->orientation == VERTICAL ? s->h : s->w;
    const int ch = jobnr;
    float *phases = s->phases[ch];
    float min, max, scale;
    int y;

    for (y = 0; y < h; y++)
        phases[y] = PHASE(y, ch);
    unwrap(phases, h, M_PI, &min, &max);
    scale = 1.f / (max - min + FLT_MIN);
    for (y = 0; y < h; y++)
        phases[y] = fabsf((phases[y] - min) * scale);

    return 0;
}

static void acalc_magnitudes(ShowSpectrumContext *s)
{
    const double w = s->win_scale * (s->scale == LOG ? s->win_scale : 1);
    int ch, y, h = s->orientation == VERTICAL ? s->h : s->w;
    const float f = s->gain * w;

    for (ch = 0; ch < s->nb_display_channels; ch++) {
        float *magnitudes = s->magnitudes[ch];

        for (y = 0; y < h; y++)
            magnitudes[y] += MAGNITUDE(y, ch) * f;
    }
}

static void scale_magnitudes(ShowSpectrumContext *s, float scale)
{
    int ch, y, h = s->orientation == VERTICAL ? s->h : s->w;

    for (ch = 0; ch < s->nb_display_channels; ch++) {
        float *magnitudes = s->magnitudes[ch];

        for (y = 0; y < h; y++)
            magnitudes[y] *= scale;
    }
}

static void clear_combine_buffer(ShowSpectrumContext *s, int size)
{
    int y;

    for (y = 0; y < size; y++) {
        s->combine_buffer[4 * y    ] = 0;
        s->combine_buffer[4 * y + 1] = 127.5;
        s->combine_buffer[4 * y + 2] = 127.5;
        s->combine_buffer[4 * y + 3] = 0;
    }
}

static int plot_spectrum_column(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ShowSpectrumContext *s = ctx->priv;
    AVFrame *outpicref = s->outpicref;
    int ret, plane, x, y, z = s->orientation == VERTICAL ? s->h : s->w;
    const int alpha = outpicref->data[3] != NULL;

    /* fill a new spectrum column */
    /* initialize buffer for combining to black */
    clear_combine_buffer(s, z);

    ff_filter_execute(ctx, s->plot_channel, NULL, NULL, s->nb_display_channels);

    for (y = 0; y < z * 4; y++) {
        for (x = 0; x < s->nb_display_channels; x++) {
            s->combine_buffer[y] += s->color_buffer[x][y];
        }
    }

    ret = ff_inlink_make_frame_writable(outlink, &s->outpicref);
    if (ret < 0)
        return ret;
    outpicref = s->outpicref;
    /* copy to output */
    if (s->orientation == VERTICAL) {
        if (s->sliding == SCROLL) {
            for (plane = 0; plane < 3 + alpha; plane++) {
                for (y = 0; y < s->h; y++) {
                    uint8_t *p = outpicref->data[plane] + s->start_x +
                                 (y + s->start_y) * outpicref->linesize[plane];
                    memmove(p, p + 1, s->w - 1);
                }
            }
            s->xpos = s->w - 1;
        } else if (s->sliding == RSCROLL) {
            for (plane = 0; plane < 3 + alpha; plane++) {
                for (y = 0; y < s->h; y++) {
                    uint8_t *p = outpicref->data[plane] + s->start_x +
                                 (y + s->start_y) * outpicref->linesize[plane];
                    memmove(p + 1, p, s->w - 1);
                }
            }
            s->xpos = 0;
        }
        for (plane = 0; plane < 3; plane++) {
            uint8_t *p = outpicref->data[plane] + s->start_x +
                         (outlink->h - 1 - s->start_y) * outpicref->linesize[plane] +
                         s->xpos;
            for (y = 0; y < s->h; y++) {
                *p = lrintf(av_clipf(s->combine_buffer[4 * y + plane], 0, 255));
                p -= outpicref->linesize[plane];
            }
        }
        if (alpha) {
            uint8_t *p = outpicref->data[3] + s->start_x +
                         (outlink->h - 1 - s->start_y) * outpicref->linesize[3] +
                         s->xpos;
            for (y = 0; y < s->h; y++) {
                *p = lrintf(av_clipf(s->combine_buffer[4 * y + 3], 0, 255));
                p -= outpicref->linesize[3];
            }
        }
    } else {
        if (s->sliding == SCROLL) {
            for (plane = 0; plane < 3 + alpha; plane++) {
                for (y = 1; y < s->h; y++) {
                    memmove(outpicref->data[plane] + (y-1 + s->start_y) * outpicref->linesize[plane] + s->start_x,
                            outpicref->data[plane] + (y   + s->start_y) * outpicref->linesize[plane] + s->start_x,
                            s->w);
                }
            }
            s->xpos = s->h - 1;
        } else if (s->sliding == RSCROLL) {
            for (plane = 0; plane < 3 + alpha; plane++) {
                for (y = s->h - 1; y >= 1; y--) {
                    memmove(outpicref->data[plane] + (y   + s->start_y) * outpicref->linesize[plane] + s->start_x,
                            outpicref->data[plane] + (y-1 + s->start_y) * outpicref->linesize[plane] + s->start_x,
                            s->w);
                }
            }
            s->xpos = 0;
        }
        for (plane = 0; plane < 3; plane++) {
            uint8_t *p = outpicref->data[plane] + s->start_x +
                         (s->xpos + s->start_y) * outpicref->linesize[plane];
            for (x = 0; x < s->w; x++) {
                *p = lrintf(av_clipf(s->combine_buffer[4 * x + plane], 0, 255));
                p++;
            }
        }
        if (alpha) {
            uint8_t *p = outpicref->data[3] + s->start_x +
                         (s->xpos + s->start_y) * outpicref->linesize[3];
            for (x = 0; x < s->w; x++) {
                *p = lrintf(av_clipf(s->combine_buffer[4 * x + 3], 0, 255));
                p++;
            }
        }
    }

    if (s->sliding != FULLFRAME || s->xpos == 0)
        s->pts = outpicref->pts = av_rescale_q(s->in_pts, inlink->time_base, outlink->time_base);

    if (s->sliding == LREPLACE) {
        s->xpos--;
        if (s->orientation == VERTICAL && s->xpos < 0)
            s->xpos = s->w - 1;
        if (s->orientation == HORIZONTAL && s->xpos < 0)
            s->xpos = s->h - 1;
    } else {
        s->xpos++;
        if (s->orientation == VERTICAL && s->xpos >= s->w)
            s->xpos = 0;
        if (s->orientation == HORIZONTAL && s->xpos >= s->h)
            s->xpos = 0;
    }

    if (!s->single_pic && (s->sliding != FULLFRAME || s->xpos == 0)) {
        if (s->old_pts < outpicref->pts || s->sliding == FULLFRAME ||
            (s->eof && ff_inlink_queued_samples(inlink) <= s->hop_size)) {
            AVFrame *clone;

            if (s->legend) {
                char *units = get_time(ctx, insamples->pts /(float)inlink->sample_rate, x);
                if (!units)
                    return AVERROR(ENOMEM);

                if (s->orientation == VERTICAL) {
                    for (y = 0; y < 10; y++) {
                        memset(s->outpicref->data[0] + outlink->w / 2 - 4 * s->old_len +
                               (outlink->h - s->start_y / 2 - 20 + y) * s->outpicref->linesize[0], 0, 10 * s->old_len);
                    }
                    drawtext(s->outpicref,
                             outlink->w / 2 - 4 * strlen(units),
                             outlink->h - s->start_y / 2 - 20,
                             units, 0);
                } else  {
                    for (y = 0; y < 10 * s->old_len; y++) {
                        memset(s->outpicref->data[0] + s->start_x / 7 + 20 +
                               (outlink->h / 2 - 4 * s->old_len + y) * s->outpicref->linesize[0], 0, 10);
                    }
                    drawtext(s->outpicref,
                             s->start_x / 7 + 20,
                             outlink->h / 2 - 4 * strlen(units),
                             units, 1);
                }
                s->old_len = strlen(units);
                av_free(units);
            }
            s->old_pts = outpicref->pts;
            clone = av_frame_clone(s->outpicref);
            if (!clone)
                return AVERROR(ENOMEM);
            ret = ff_filter_frame(outlink, clone);
            if (ret < 0)
                return ret;
            return 0;
        }
    }

    return 1;
}

#if CONFIG_SHOWSPECTRUM_FILTER

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    ShowSpectrumContext *s = ctx->priv;
    int ret, status;
    int64_t pts;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (s->outpicref && ff_inlink_queued_samples(inlink) > 0) {
        AVFrame *fin;

        ret = ff_inlink_consume_samples(inlink, s->hop_size, s->hop_size, &fin);
        if (ret < 0)
            return ret;
        if (ret > 0) {
            ff_filter_execute(ctx, run_channel_fft, fin, NULL, s->nb_display_channels);

            if (s->data == D_MAGNITUDE)
                ff_filter_execute(ctx, calc_channel_magnitudes, NULL, NULL, s->nb_display_channels);

            if (s->data == D_PHASE)
                ff_filter_execute(ctx, calc_channel_phases, NULL, NULL, s->nb_display_channels);

            if (s->data == D_UPHASE)
                ff_filter_execute(ctx, calc_channel_uphases, NULL, NULL, s->nb_display_channels);

            if (s->sliding != FULLFRAME || s->xpos == 0)
                s->in_pts = fin->pts;
            ret = plot_spectrum_column(inlink, fin);
            av_frame_free(&fin);
            if (ret <= 0)
                return ret;
        }
    }

    if (s->eof && s->sliding == FULLFRAME &&
        s->xpos > 0 && s->outpicref) {

        if (s->orientation == VERTICAL) {
            for (int i = 0; i < outlink->h; i++) {
                memset(s->outpicref->data[0] + i * s->outpicref->linesize[0] + s->xpos,   0, outlink->w - s->xpos);
                memset(s->outpicref->data[1] + i * s->outpicref->linesize[1] + s->xpos, 128, outlink->w - s->xpos);
                memset(s->outpicref->data[2] + i * s->outpicref->linesize[2] + s->xpos, 128, outlink->w - s->xpos);
                if (s->outpicref->data[3])
                    memset(s->outpicref->data[3] + i * s->outpicref->linesize[3] + s->xpos, 0, outlink->w - s->xpos);
            }
        } else {
            for (int i = s->xpos; i < outlink->h; i++) {
                memset(s->outpicref->data[0] + i * s->outpicref->linesize[0],   0, outlink->w);
                memset(s->outpicref->data[1] + i * s->outpicref->linesize[1], 128, outlink->w);
                memset(s->outpicref->data[2] + i * s->outpicref->linesize[2], 128, outlink->w);
                if (s->outpicref->data[3])
                    memset(s->outpicref->data[3] + i * s->outpicref->linesize[3], 0, outlink->w);
            }
        }
        s->outpicref->pts = av_rescale_q(s->in_pts, inlink->time_base, outlink->time_base);
        pts = s->outpicref->pts;
        ret = ff_filter_frame(outlink, s->outpicref);
        s->outpicref = NULL;
        ff_outlink_set_status(outlink, AVERROR_EOF, pts);
        return 0;
    }

    if (!s->eof && ff_inlink_acknowledge_status(inlink, &status, &pts)) {
        s->eof = status == AVERROR_EOF;
        ff_filter_set_ready(ctx, 100);
        return 0;
    }

    if (s->eof) {
        ff_outlink_set_status(outlink, AVERROR_EOF, s->pts);
        return 0;
    }

    if (ff_inlink_queued_samples(inlink) >= s->hop_size) {
        ff_filter_set_ready(ctx, 10);
        return 0;
    }

    if (ff_outlink_frame_wanted(outlink)) {
        ff_inlink_request_frame(inlink);
        return 0;
    }

    return FFERROR_NOT_READY;
}

static const AVFilterPad showspectrum_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
};

const FFFilter ff_avf_showspectrum = {
    .p.name        = "showspectrum",
    .p.description = NULL_IF_CONFIG_SMALL("Convert input audio to a spectrum video output."),
    .p.priv_class  = &showspectrum_class,
    .p.flags       = AVFILTER_FLAG_SLICE_THREADS,
    .uninit        = uninit,
    .priv_size     = sizeof(ShowSpectrumContext),
    FILTER_INPUTS(ff_audio_default_filterpad),
    FILTER_OUTPUTS(showspectrum_outputs),
    FILTER_QUERY_FUNC2(query_formats),
    .activate      = activate,
};
#endif // CONFIG_SHOWSPECTRUM_FILTER

#if CONFIG_SHOWSPECTRUMPIC_FILTER

static const AVOption showspectrumpic_options[] = {
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "4096x2048"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "4096x2048"}, 0, 0, FLAGS },
    { "mode", "set channel display mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=COMBINED}, 0, NB_MODES-1, FLAGS, .unit = "mode" },
        { "combined", "combined mode", 0, AV_OPT_TYPE_CONST, {.i64=COMBINED}, 0, 0, FLAGS, .unit = "mode" },
        { "separate", "separate mode", 0, AV_OPT_TYPE_CONST, {.i64=SEPARATE}, 0, 0, FLAGS, .unit = "mode" },
    { "color", "set channel coloring", OFFSET(color_mode), AV_OPT_TYPE_INT, {.i64=INTENSITY}, 0, NB_CLMODES-1, FLAGS, .unit = "color" },
        { "channel",   "separate color for each channel", 0, AV_OPT_TYPE_CONST, {.i64=CHANNEL},   0, 0, FLAGS, .unit = "color" },
        { "intensity", "intensity based coloring",        0, AV_OPT_TYPE_CONST, {.i64=INTENSITY}, 0, 0, FLAGS, .unit = "color" },
        { "rainbow",   "rainbow based coloring",          0, AV_OPT_TYPE_CONST, {.i64=RAINBOW},   0, 0, FLAGS, .unit = "color" },
        { "moreland",  "moreland based coloring",         0, AV_OPT_TYPE_CONST, {.i64=MORELAND},  0, 0, FLAGS, .unit = "color" },
        { "nebulae",   "nebulae based coloring",          0, AV_OPT_TYPE_CONST, {.i64=NEBULAE},   0, 0, FLAGS, .unit = "color" },
        { "fire",      "fire based coloring",             0, AV_OPT_TYPE_CONST, {.i64=FIRE},      0, 0, FLAGS, .unit = "color" },
        { "fiery",     "fiery based coloring",            0, AV_OPT_TYPE_CONST, {.i64=FIERY},     0, 0, FLAGS, .unit = "color" },
        { "fruit",     "fruit based coloring",            0, AV_OPT_TYPE_CONST, {.i64=FRUIT},     0, 0, FLAGS, .unit = "color" },
        { "cool",      "cool based coloring",             0, AV_OPT_TYPE_CONST, {.i64=COOL},      0, 0, FLAGS, .unit = "color" },
        { "magma",     "magma based coloring",            0, AV_OPT_TYPE_CONST, {.i64=MAGMA},     0, 0, FLAGS, .unit = "color" },
        { "green",     "green based coloring",            0, AV_OPT_TYPE_CONST, {.i64=GREEN},     0, 0, FLAGS, .unit = "color" },
        { "viridis",   "viridis based coloring",          0, AV_OPT_TYPE_CONST, {.i64=VIRIDIS},   0, 0, FLAGS, .unit = "color" },
        { "plasma",    "plasma based coloring",           0, AV_OPT_TYPE_CONST, {.i64=PLASMA},    0, 0, FLAGS, .unit = "color" },
        { "cividis",   "cividis based coloring",          0, AV_OPT_TYPE_CONST, {.i64=CIVIDIS},   0, 0, FLAGS, .unit = "color" },
        { "terrain",   "terrain based coloring",          0, AV_OPT_TYPE_CONST, {.i64=TERRAIN},   0, 0, FLAGS, .unit = "color" },
    { "scale", "set display scale", OFFSET(scale), AV_OPT_TYPE_INT, {.i64=LOG}, 0, NB_SCALES-1, FLAGS, .unit = "scale" },
        { "lin",  "linear",      0, AV_OPT_TYPE_CONST, {.i64=LINEAR}, 0, 0, FLAGS, .unit = "scale" },
        { "sqrt", "square root", 0, AV_OPT_TYPE_CONST, {.i64=SQRT},   0, 0, FLAGS, .unit = "scale" },
        { "cbrt", "cubic root",  0, AV_OPT_TYPE_CONST, {.i64=CBRT},   0, 0, FLAGS, .unit = "scale" },
        { "log",  "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=LOG},    0, 0, FLAGS, .unit = "scale" },
        { "4thrt","4th root",    0, AV_OPT_TYPE_CONST, {.i64=FOURTHRT}, 0, 0, FLAGS, .unit = "scale" },
        { "5thrt","5th root",    0, AV_OPT_TYPE_CONST, {.i64=FIFTHRT},  0, 0, FLAGS, .unit = "scale" },
    { "fscale", "set frequency scale", OFFSET(fscale), AV_OPT_TYPE_INT, {.i64=F_LINEAR}, 0, NB_FSCALES-1, FLAGS, .unit = "fscale" },
        { "lin",  "linear",      0, AV_OPT_TYPE_CONST, {.i64=F_LINEAR}, 0, 0, FLAGS, .unit = "fscale" },
        { "log",  "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=F_LOG},    0, 0, FLAGS, .unit = "fscale" },
    { "saturation", "color saturation multiplier", OFFSET(saturation), AV_OPT_TYPE_FLOAT, {.dbl = 1}, -10, 10, FLAGS },
    WIN_FUNC_OPTION("win_func", OFFSET(win_func), FLAGS, WFUNC_HANNING),
    { "orientation", "set orientation", OFFSET(orientation), AV_OPT_TYPE_INT, {.i64=VERTICAL}, 0, NB_ORIENTATIONS-1, FLAGS, .unit = "orientation" },
        { "vertical",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=VERTICAL},   0, 0, FLAGS, .unit = "orientation" },
        { "horizontal", NULL, 0, AV_OPT_TYPE_CONST, {.i64=HORIZONTAL}, 0, 0, FLAGS, .unit = "orientation" },
    { "gain", "set scale gain", OFFSET(gain), AV_OPT_TYPE_FLOAT, {.dbl = 1}, 0, 128, FLAGS },
    { "legend", "draw legend", OFFSET(legend), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
    { "rotation", "color rotation", OFFSET(rotation), AV_OPT_TYPE_FLOAT, {.dbl = 0}, -1, 1, FLAGS },
    { "start", "start frequency", OFFSET(start), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT32_MAX, FLAGS },
    { "stop",  "stop frequency",  OFFSET(stop),  AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT32_MAX, FLAGS },
    { "drange", "set dynamic range in dBFS", OFFSET(drange), AV_OPT_TYPE_FLOAT, {.dbl = 120}, 10, 200, FLAGS },
    { "limit", "set upper limit in dBFS", OFFSET(limit), AV_OPT_TYPE_FLOAT, {.dbl = 0}, -100, 100, FLAGS },
    { "opacity", "set opacity strength", OFFSET(opacity_factor), AV_OPT_TYPE_FLOAT, {.dbl = 1}, 0, 10, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(showspectrumpic);

static int showspectrumpic_request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ShowSpectrumContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret;

    ret = ff_request_frame(inlink);
    if (ret == AVERROR_EOF && s->outpicref && s->samples > 0) {
        int consumed = 0;
        int x = 0, sz = s->orientation == VERTICAL ? s->w : s->h;
        unsigned int nb_frame = 0;
        int ch, spf, spb;
        int src_offset = 0;
        AVFrame *fin;

        spf = s->win_size * (s->samples / ((s->win_size * sz) * ceil(s->samples / (float)(s->win_size * sz))));
        spf = FFMAX(1, spf);
        s->hop_size = spf;

        spb = (s->samples / (spf * sz)) * spf;

        fin = ff_get_audio_buffer(inlink, spf);
        if (!fin)
            return AVERROR(ENOMEM);

        while (x < sz) {
            int acc_samples = 0;
            int dst_offset = 0;

            while (nb_frame < s->nb_frames) {
                AVFrame *cur_frame = s->frames[nb_frame];
                int cur_frame_samples = cur_frame->nb_samples;
                int nb_samples = 0;

                if (acc_samples < spf) {
                    nb_samples = FFMIN(spf - acc_samples, cur_frame_samples - src_offset);
                    acc_samples += nb_samples;
                    av_samples_copy(fin->extended_data, cur_frame->extended_data,
                                    dst_offset, src_offset, nb_samples,
                                    cur_frame->ch_layout.nb_channels, AV_SAMPLE_FMT_FLTP);
                }

                src_offset += nb_samples;
                dst_offset += nb_samples;
                if (cur_frame_samples <= src_offset) {
                    av_frame_free(&s->frames[nb_frame]);
                    nb_frame++;
                    src_offset = 0;
                }

                if (acc_samples == spf)
                    break;
            }

            ff_filter_execute(ctx, run_channel_fft, fin, NULL, s->nb_display_channels);
            acalc_magnitudes(s);

            consumed += spf;
            if (consumed >= spb) {
                int h = s->orientation == VERTICAL ? s->h : s->w;

                scale_magnitudes(s, 1.f / (consumed / spf));
                plot_spectrum_column(inlink, fin);
                consumed = 0;
                x++;
                for (ch = 0; ch < s->nb_display_channels; ch++)
                    memset(s->magnitudes[ch], 0, h * sizeof(float));
            }
        }

        av_frame_free(&fin);
        s->outpicref->pts = 0;

        if (s->legend)
            draw_legend(ctx, s->samples);

        ret = ff_filter_frame(outlink, s->outpicref);
        s->outpicref = NULL;
    }

    return ret;
}

static int showspectrumpic_filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    ShowSpectrumContext *s = ctx->priv;
    void *ptr;

    if (s->nb_frames + 1ULL > s->frames_size / sizeof(*(s->frames))) {
        ptr = av_fast_realloc(s->frames, &s->frames_size, s->frames_size * 2);
        if (!ptr)
            return AVERROR(ENOMEM);
        s->frames = ptr;
    }

    s->frames[s->nb_frames] = insamples;
    s->samples += insamples->nb_samples;
    s->nb_frames++;

    return 0;
}

static const AVFilterPad showspectrumpic_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = showspectrumpic_filter_frame,
    },
};

static const AVFilterPad showspectrumpic_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = showspectrumpic_request_frame,
    },
};

const FFFilter ff_avf_showspectrumpic = {
    .p.name        = "showspectrumpic",
    .p.description = NULL_IF_CONFIG_SMALL("Convert input audio to a spectrum video output single picture."),
    .p.priv_class  = &showspectrumpic_class,
    .p.flags       = AVFILTER_FLAG_SLICE_THREADS,
    .uninit        = uninit,
    .priv_size     = sizeof(ShowSpectrumContext),
    FILTER_INPUTS(showspectrumpic_inputs),
    FILTER_OUTPUTS(showspectrumpic_outputs),
    FILTER_QUERY_FUNC2(query_formats),
};

#endif // CONFIG_SHOWSPECTRUMPIC_FILTER
