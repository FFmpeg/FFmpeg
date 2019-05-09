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

#include <math.h>

#include "libavcodec/avfft.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavutil/parseutils.h"
#include "libavutil/xga_font_data.h"
#include "audio.h"
#include "video.h"
#include "avfilter.h"
#include "filters.h"
#include "internal.h"
#include "window_func.h"

enum DisplayMode  { COMBINED, SEPARATE, NB_MODES };
enum DataMode     { D_MAGNITUDE, D_PHASE, NB_DMODES };
enum FrequencyScale { F_LINEAR, F_LOG, NB_FSCALES };
enum DisplayScale { LINEAR, SQRT, CBRT, LOG, FOURTHRT, FIFTHRT, NB_SCALES };
enum ColorMode    { CHANNEL, INTENSITY, RAINBOW, MORELAND, NEBULAE, FIRE, FIERY, FRUIT, COOL, MAGMA, GREEN, VIRIDIS, PLASMA, CIVIDIS, TERRAIN, NB_CLMODES };
enum SlideMode    { REPLACE, SCROLL, FULLFRAME, RSCROLL, NB_SLIDES };
enum Orientation  { VERTICAL, HORIZONTAL, NB_ORIENTATIONS };

typedef struct ShowSpectrumContext {
    const AVClass *class;
    int w, h;
    char *rate_str;
    AVRational auto_frame_rate;
    AVRational frame_rate;
    AVFrame *outpicref;
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
    FFTContext **fft;           ///< Fast Fourier Transform context
    FFTContext **ifft;          ///< Inverse Fast Fourier Transform context
    int fft_bits;               ///< number of bits (FFT window size = 1<<fft_bits)
    FFTComplex **fft_data;      ///< bins holder for each (displayed) channels
    FFTComplex **fft_scratch;   ///< scratch buffers
    float *window_func_lut;     ///< Window function LUT
    float **magnitudes;
    float **phases;
    int win_func;
    int win_size;
    int buf_size;
    double win_scale;
    float overlap;
    float gain;
    int consumed;
    int hop_size;
    float *combine_buffer;      ///< color combining buffer (3 * h items)
    float **color_buffer;       ///< color buffer (3 * h * ch items)
    AVAudioFifo *fifo;
    int64_t pts;
    int64_t old_pts;
    int old_len;
    int single_pic;
    int legend;
    int start_x, start_y;
    int (*plot_channel)(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs);
} ShowSpectrumContext;

#define OFFSET(x) offsetof(ShowSpectrumContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption showspectrum_options[] = {
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x512"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x512"}, 0, 0, FLAGS },
    { "slide", "set sliding mode", OFFSET(sliding), AV_OPT_TYPE_INT, {.i64 = 0}, 0, NB_SLIDES-1, FLAGS, "slide" },
        { "replace", "replace old columns with new", 0, AV_OPT_TYPE_CONST, {.i64=REPLACE}, 0, 0, FLAGS, "slide" },
        { "scroll", "scroll from right to left", 0, AV_OPT_TYPE_CONST, {.i64=SCROLL}, 0, 0, FLAGS, "slide" },
        { "fullframe", "return full frames", 0, AV_OPT_TYPE_CONST, {.i64=FULLFRAME}, 0, 0, FLAGS, "slide" },
        { "rscroll", "scroll from left to right", 0, AV_OPT_TYPE_CONST, {.i64=RSCROLL}, 0, 0, FLAGS, "slide" },
    { "mode", "set channel display mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=COMBINED}, COMBINED, NB_MODES-1, FLAGS, "mode" },
        { "combined", "combined mode", 0, AV_OPT_TYPE_CONST, {.i64=COMBINED}, 0, 0, FLAGS, "mode" },
        { "separate", "separate mode", 0, AV_OPT_TYPE_CONST, {.i64=SEPARATE}, 0, 0, FLAGS, "mode" },
    { "color", "set channel coloring", OFFSET(color_mode), AV_OPT_TYPE_INT, {.i64=CHANNEL}, CHANNEL, NB_CLMODES-1, FLAGS, "color" },
        { "channel",   "separate color for each channel", 0, AV_OPT_TYPE_CONST, {.i64=CHANNEL},   0, 0, FLAGS, "color" },
        { "intensity", "intensity based coloring",        0, AV_OPT_TYPE_CONST, {.i64=INTENSITY}, 0, 0, FLAGS, "color" },
        { "rainbow",   "rainbow based coloring",          0, AV_OPT_TYPE_CONST, {.i64=RAINBOW},   0, 0, FLAGS, "color" },
        { "moreland",  "moreland based coloring",         0, AV_OPT_TYPE_CONST, {.i64=MORELAND},  0, 0, FLAGS, "color" },
        { "nebulae",   "nebulae based coloring",          0, AV_OPT_TYPE_CONST, {.i64=NEBULAE},   0, 0, FLAGS, "color" },
        { "fire",      "fire based coloring",             0, AV_OPT_TYPE_CONST, {.i64=FIRE},      0, 0, FLAGS, "color" },
        { "fiery",     "fiery based coloring",            0, AV_OPT_TYPE_CONST, {.i64=FIERY},     0, 0, FLAGS, "color" },
        { "fruit",     "fruit based coloring",            0, AV_OPT_TYPE_CONST, {.i64=FRUIT},     0, 0, FLAGS, "color" },
        { "cool",      "cool based coloring",             0, AV_OPT_TYPE_CONST, {.i64=COOL},      0, 0, FLAGS, "color" },
        { "magma",     "magma based coloring",            0, AV_OPT_TYPE_CONST, {.i64=MAGMA},     0, 0, FLAGS, "color" },
        { "green",     "green based coloring",            0, AV_OPT_TYPE_CONST, {.i64=GREEN},     0, 0, FLAGS, "color" },
        { "viridis",   "viridis based coloring",          0, AV_OPT_TYPE_CONST, {.i64=VIRIDIS},   0, 0, FLAGS, "color" },
        { "plasma",    "plasma based coloring",           0, AV_OPT_TYPE_CONST, {.i64=PLASMA},    0, 0, FLAGS, "color" },
        { "cividis",   "cividis based coloring",          0, AV_OPT_TYPE_CONST, {.i64=CIVIDIS},   0, 0, FLAGS, "color" },
        { "terrain",   "terrain based coloring",          0, AV_OPT_TYPE_CONST, {.i64=TERRAIN},   0, 0, FLAGS, "color" },
    { "scale", "set display scale", OFFSET(scale), AV_OPT_TYPE_INT, {.i64=SQRT}, LINEAR, NB_SCALES-1, FLAGS, "scale" },
        { "lin",  "linear",      0, AV_OPT_TYPE_CONST, {.i64=LINEAR}, 0, 0, FLAGS, "scale" },
        { "sqrt", "square root", 0, AV_OPT_TYPE_CONST, {.i64=SQRT},   0, 0, FLAGS, "scale" },
        { "cbrt", "cubic root",  0, AV_OPT_TYPE_CONST, {.i64=CBRT},   0, 0, FLAGS, "scale" },
        { "log",  "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=LOG},    0, 0, FLAGS, "scale" },
        { "4thrt","4th root",    0, AV_OPT_TYPE_CONST, {.i64=FOURTHRT}, 0, 0, FLAGS, "scale" },
        { "5thrt","5th root",    0, AV_OPT_TYPE_CONST, {.i64=FIFTHRT},  0, 0, FLAGS, "scale" },
    { "fscale", "set frequency scale", OFFSET(fscale), AV_OPT_TYPE_INT, {.i64=F_LINEAR}, 0, NB_FSCALES-1, FLAGS, "fscale" },
        { "lin",  "linear",      0, AV_OPT_TYPE_CONST, {.i64=F_LINEAR}, 0, 0, FLAGS, "fscale" },
        { "log",  "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=F_LOG},    0, 0, FLAGS, "fscale" },
    { "saturation", "color saturation multiplier", OFFSET(saturation), AV_OPT_TYPE_FLOAT, {.dbl = 1}, -10, 10, FLAGS },
    { "win_func", "set window function", OFFSET(win_func), AV_OPT_TYPE_INT, {.i64 = WFUNC_HANNING}, 0, NB_WFUNC-1, FLAGS, "win_func" },
        { "rect",     "Rectangular",      0, AV_OPT_TYPE_CONST, {.i64=WFUNC_RECT},     0, 0, FLAGS, "win_func" },
        { "bartlett", "Bartlett",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BARTLETT}, 0, 0, FLAGS, "win_func" },
        { "hann",     "Hann",             0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HANNING},  0, 0, FLAGS, "win_func" },
        { "hanning",  "Hanning",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HANNING},  0, 0, FLAGS, "win_func" },
        { "hamming",  "Hamming",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HAMMING},  0, 0, FLAGS, "win_func" },
        { "blackman", "Blackman",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BLACKMAN}, 0, 0, FLAGS, "win_func" },
        { "welch",    "Welch",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_WELCH},    0, 0, FLAGS, "win_func" },
        { "flattop",  "Flat-top",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_FLATTOP},  0, 0, FLAGS, "win_func" },
        { "bharris",  "Blackman-Harris",  0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BHARRIS},  0, 0, FLAGS, "win_func" },
        { "bnuttall", "Blackman-Nuttall", 0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BNUTTALL}, 0, 0, FLAGS, "win_func" },
        { "bhann",    "Bartlett-Hann",    0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BHANN},    0, 0, FLAGS, "win_func" },
        { "sine",     "Sine",             0, AV_OPT_TYPE_CONST, {.i64=WFUNC_SINE},     0, 0, FLAGS, "win_func" },
        { "nuttall",  "Nuttall",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_NUTTALL},  0, 0, FLAGS, "win_func" },
        { "lanczos",  "Lanczos",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_LANCZOS},  0, 0, FLAGS, "win_func" },
        { "gauss",    "Gauss",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_GAUSS},    0, 0, FLAGS, "win_func" },
        { "tukey",    "Tukey",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_TUKEY},    0, 0, FLAGS, "win_func" },
        { "dolph",    "Dolph-Chebyshev",  0, AV_OPT_TYPE_CONST, {.i64=WFUNC_DOLPH},    0, 0, FLAGS, "win_func" },
        { "cauchy",   "Cauchy",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_CAUCHY},   0, 0, FLAGS, "win_func" },
        { "parzen",   "Parzen",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_PARZEN},   0, 0, FLAGS, "win_func" },
        { "poisson",  "Poisson",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_POISSON},  0, 0, FLAGS, "win_func" },
        { "bohman",   "Bohman",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BOHMAN},   0, 0, FLAGS, "win_func" },
    { "orientation", "set orientation", OFFSET(orientation), AV_OPT_TYPE_INT, {.i64=VERTICAL}, 0, NB_ORIENTATIONS-1, FLAGS, "orientation" },
        { "vertical",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=VERTICAL},   0, 0, FLAGS, "orientation" },
        { "horizontal", NULL, 0, AV_OPT_TYPE_CONST, {.i64=HORIZONTAL}, 0, 0, FLAGS, "orientation" },
    { "overlap", "set window overlap", OFFSET(overlap), AV_OPT_TYPE_FLOAT, {.dbl = 0}, 0, 1, FLAGS },
    { "gain", "set scale gain", OFFSET(gain), AV_OPT_TYPE_FLOAT, {.dbl = 1}, 0, 128, FLAGS },
    { "data", "set data mode", OFFSET(data), AV_OPT_TYPE_INT, {.i64 = 0}, 0, NB_DMODES-1, FLAGS, "data" },
        { "magnitude", NULL, 0, AV_OPT_TYPE_CONST, {.i64=D_MAGNITUDE}, 0, 0, FLAGS, "data" },
        { "phase",     NULL, 0, AV_OPT_TYPE_CONST, {.i64=D_PHASE},     0, 0, FLAGS, "data" },
    { "rotation", "color rotation", OFFSET(rotation), AV_OPT_TYPE_FLOAT, {.dbl = 0}, -1, 1, FLAGS },
    { "start", "start frequency", OFFSET(start), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT32_MAX, FLAGS },
    { "stop",  "stop frequency",  OFFSET(stop),  AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT32_MAX, FLAGS },
    { "fps",   "set video rate",  OFFSET(rate_str), AV_OPT_TYPE_STRING, {.str = "auto"}, 0, 0, FLAGS },
    { "legend", "draw legend", OFFSET(legend), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, FLAGS },
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
            av_fft_end(s->fft[i]);
    }
    av_freep(&s->fft);
    if (s->ifft) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_fft_end(s->ifft[i]);
    }
    av_freep(&s->ifft);
    if (s->fft_data) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_freep(&s->fft_data[i]);
    }
    av_freep(&s->fft_data);
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
    av_audio_fifo_free(s->fifo);
    if (s->phases) {
        for (i = 0; i < s->nb_display_channels; i++)
            av_freep(&s->phases[i]);
    }
    av_freep(&s->phases);
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_NONE };
    int ret;

    /* set input audio formats */
    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &inlink->out_formats)) < 0)
        return ret;

    layouts = ff_all_channel_layouts();
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->out_channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->out_samplerates)) < 0)
        return ret;

    /* set output video format */
    formats = ff_make_format_list(pix_fmts);
    if ((ret = ff_formats_ref(formats, &outlink->in_formats)) < 0)
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

    for (n = 0; n < s->win_size; n++) {
        s->fft_data[ch][n].re = p[n] * window_func_lut[n];
        s->fft_data[ch][n].im = 0;
    }

    if (s->stop) {
        float theta, phi, psi, a, b, S, c;
        FFTComplex *g = s->fft_data[ch];
        FFTComplex *h = s->fft_scratch[ch];
        int L = s->buf_size;
        int N = s->win_size;
        int M = s->win_size / 2;

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

        for (int n = 0; n < N; n++) {
            g[n].re = s->fft_data[ch][n].re;
            g[n].im = s->fft_data[ch][n].im;
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

        av_fft_permute(s->fft[ch], h);
        av_fft_calc(s->fft[ch], h);

        av_fft_permute(s->fft[ch], g);
        av_fft_calc(s->fft[ch], g);

        for (int n = 0; n < L; n++) {
            c = g[n].re;
            S = g[n].im;
            a = c * h[n].re - S * h[n].im;
            b = S * h[n].re + c * h[n].im;

            g[n].re = a / L;
            g[n].im = b / L;
        }

        av_fft_permute(s->ifft[ch], g);
        av_fft_calc(s->ifft[ch], g);

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
        /* run FFT on each samples set */
        av_fft_permute(s->fft[ch], s->fft_data[ch]);
        av_fft_calc(s->fft[ch], s->fft_data[ch]);
    }

    return 0;
}

static void drawtext(AVFrame *pic, int x, int y, const char *txt, int o)
{
    const uint8_t *font;
    int font_height;
    int i;

    font = avpriv_cga_font,   font_height =  8;

    for (i = 0; txt[i]; i++) {
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
    } else {
        out[0] = a * yf;
        out[1] = a * uf;
        out[2] = a * vf;
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

static float log_scale(const float value, const float min, const float max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;

    {
        const float b = logf(max / min) / (max - min);
        const float a = max / expf(max * b);

        return expf(value * b) * a;
    }
}

static float get_log_hz(const int bin, const int num_bins, const float sample_rate)
{
    const float max_freq = sample_rate / 2;
    const float hz_per_bin = max_freq / num_bins;
    const float freq = hz_per_bin * bin;
    const float scaled_freq = log_scale(freq + 1, 21, max_freq) - 1;

    return num_bins * scaled_freq / max_freq;
}

static float inv_log_scale(const float value, const float min, const float max)
{
    if (value < min)
        return min;
    if (value > max)
        return max;

    {
        const float b = logf(max / min) / (max - min);
        const float a = max / expf(max * b);

        return logf(value / a) / b;
    }
}

static float bin_pos(const int bin, const int num_bins, const float sample_rate)
{
    const float max_freq = sample_rate / 2;
    const float hz_per_bin = max_freq / num_bins;
    const float freq = hz_per_bin * bin;
    const float scaled_freq = inv_log_scale(freq + 1, 21, max_freq) - 1;

    return num_bins * scaled_freq / max_freq;
}

static int draw_legend(AVFilterContext *ctx, int samples)
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

    av_get_channel_layout_string(chlayout_str, sizeof(chlayout_str), inlink->channels,
                                 inlink->channel_layout);

    text = av_asprintf("%d Hz | %s", inlink->sample_rate, chlayout_str);

    drawtext(s->outpicref, 2, outlink->h - 10, "CREATED BY LIBAVFILTER", 0);
    drawtext(s->outpicref, outlink->w - 2 - strlen(text) * 10, outlink->h - 10, text, 0);
    if (s->stop) {
        char *text = av_asprintf("Zoom: %d Hz - %d Hz", s->start, s->stop);
        drawtext(s->outpicref, outlink->w - 2 - strlen(text) * 10, 3, text, 0);
        av_freep(&text);
    }

    av_freep(&text);

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
                float bin = s->fscale == F_LINEAR ? y : get_log_hz(y, h, inlink->sample_rate);
                float hertz = s->start + bin * range / (float)(1 << (int)ceil(log2(h)));
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
                float bin = s->fscale == F_LINEAR ? x : get_log_hz(x, w, inlink->sample_rate);
                float hertz = s->start + bin * range / (float)(1 << (int)ceil(log2(w)));
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

            drawtext(s->outpicref, s->start_x - 8 * strlen(units) - 4, s->start_y + y - 4, units, 0);
            av_free(units);
        }
        drawtext(s->outpicref, s->start_x / 7, outlink->h / 2 - 4 * 4, "TIME", 1);
        drawtext(s->outpicref, outlink->w / 2 - 14 * 4, outlink->h - s->start_y / 2, "FREQUENCY (Hz)", 0);
    }

    for (ch = 0; ch < (multi ? s->nb_display_channels : 1); ch++) {
        int h = multi ? s->h / s->nb_display_channels : s->h;

        for (y = 0; y < h; y++) {
            float out[3] = { 0., 127.5, 127.5};
            int chn;

            for (chn = 0; chn < (s->mode == SEPARATE ? 1 : s->nb_display_channels); chn++) {
                float yf, uf, vf;
                int channel = (multi) ? s->nb_display_channels - ch - 1 : chn;
                float lout[3];

                color_range(s, channel, &yf, &uf, &vf);
                pick_color(s, yf, uf, vf, y / (float)h, lout);
                out[0] += lout[0];
                out[1] += lout[1];
                out[2] += lout[2];
            }
            memset(s->outpicref->data[0]+(s->start_y + h * (ch + 1) - y - 1) * s->outpicref->linesize[0] + s->w + s->start_x + 20, av_clip_uint8(out[0]), 10);
            memset(s->outpicref->data[1]+(s->start_y + h * (ch + 1) - y - 1) * s->outpicref->linesize[1] + s->w + s->start_x + 20, av_clip_uint8(out[1]), 10);
            memset(s->outpicref->data[2]+(s->start_y + h * (ch + 1) - y - 1) * s->outpicref->linesize[2] + s->w + s->start_x + 20, av_clip_uint8(out[2]), 10);
        }

        for (y = 0; ch == 0 && y < h; y += h / 10) {
            float value = 120.f * log10f(1.f - y / (float)h);
            char *text;

            if (value < -120)
                break;
            text = av_asprintf("%.0f dB", value);
            if (!text)
                continue;
            drawtext(s->outpicref, s->w + s->start_x + 35, s->start_y + y - 5, text, 0);
            av_free(text);
        }
    }

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
    case D_PHASE:
        /* get phase */
        a = phases[y];
        break;
    default:
        av_assert0(0);
    }

    /* apply scale */
    switch (s->scale) {
    case LINEAR:
        a = av_clipf(a, 0, 1);
        break;
    case SQRT:
        a = av_clipf(sqrtf(a), 0, 1);
        break;
    case CBRT:
        a = av_clipf(cbrtf(a), 0, 1);
        break;
    case FOURTHRT:
        a = av_clipf(sqrtf(sqrtf(a)), 0, 1);
        break;
    case FIFTHRT:
        a = av_clipf(powf(a, 0.20), 0, 1);
        break;
    case LOG:
        a = 1.f + log10f(av_clipf(a, 1e-6, 1)) / 6.f; // zero = -120dBFS
        break;
    default:
        av_assert0(0);
    }

    return a;
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
        float *out = &s->color_buffer[ch][3 * row];
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
    float y, yf, uf, vf;
    int yy = 0;

    /* decide color range */
    color_range(s, ch, &yf, &uf, &vf);

    /* draw the channel */
    for (y = 0; y < h && yy < h; yy++) {
        float pos0 = bin_pos(yy+0, h, inlink->sample_rate);
        float pos1 = bin_pos(yy+1, h, inlink->sample_rate);
        float delta = pos1 - pos0;
        float a0, a1;

        a0 = get_value(ctx, ch, yy+0);
        a1 = get_value(ctx, ch, FFMIN(yy+1, h-1));
        for (float j = pos0; j < pos1 && y + j - pos0 < h; j++) {
            float row = (s->mode == COMBINED) ? y + j - pos0 : ch * h + y + j - pos0;
            float *out = &s->color_buffer[ch][3 * FFMIN(lrintf(row), h-1)];
            float lerpfrac = (j - pos0) / delta;

            pick_color(s, yf, uf, vf, lerpfrac * a1 + (1.f-lerpfrac) * a0, out);
        }
        y += delta;
    }

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    ShowSpectrumContext *s = ctx->priv;
    int i, fft_bits, h, w;
    float overlap;

    switch (s->fscale) {
    case F_LINEAR: s->plot_channel = plot_channel_lin; break;
    case F_LOG:    s->plot_channel = plot_channel_log; break;
    default: return AVERROR_BUG;
    }

    s->stop = FFMIN(s->stop, inlink->sample_rate / 2);
    if (s->stop && s->stop <= s->start) {
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

    h = (s->mode == COMBINED || s->orientation == HORIZONTAL) ? s->h : s->h / inlink->channels;
    w = (s->mode == COMBINED || s->orientation == VERTICAL)   ? s->w : s->w / inlink->channels;
    s->channel_height = h;
    s->channel_width  = w;

    if (s->orientation == VERTICAL) {
        /* FFT window size (precision) according to the requested output frame height */
        for (fft_bits = 1; 1 << fft_bits < 2 * h; fft_bits++);
    } else {
        /* FFT window size (precision) according to the requested output frame width */
        for (fft_bits = 1; 1 << fft_bits < 2 * w; fft_bits++);
    }

    s->win_size = 1 << fft_bits;
    s->buf_size = s->win_size << !!s->stop;

    if (!s->fft) {
        s->fft = av_calloc(inlink->channels, sizeof(*s->fft));
        if (!s->fft)
            return AVERROR(ENOMEM);
    }

    if (s->stop) {
        if (!s->ifft) {
            s->ifft = av_calloc(inlink->channels, sizeof(*s->ifft));
            if (!s->ifft)
                return AVERROR(ENOMEM);
        }
    }

    /* (re-)configuration if the video output changed (or first init) */
    if (fft_bits != s->fft_bits) {
        AVFrame *outpicref;

        s->fft_bits = fft_bits;

        /* FFT buffers: x2 for each (display) channel buffer.
         * Note: we use free and malloc instead of a realloc-like function to
         * make sure the buffer is aligned in memory for the FFT functions. */
        for (i = 0; i < s->nb_display_channels; i++) {
            if (s->stop) {
                av_fft_end(s->ifft[i]);
                av_freep(&s->fft_scratch[i]);
            }
            av_fft_end(s->fft[i]);
            av_freep(&s->fft_data[i]);
        }
        av_freep(&s->fft_data);

        s->nb_display_channels = inlink->channels;
        for (i = 0; i < s->nb_display_channels; i++) {
            s->fft[i] = av_fft_init(fft_bits + !!s->stop, 0);
            if (s->stop) {
                s->ifft[i] = av_fft_init(fft_bits + !!s->stop, 1);
                if (!s->ifft[i]) {
                    av_log(ctx, AV_LOG_ERROR, "Unable to create Inverse FFT context. "
                           "The window size might be too high.\n");
                    return AVERROR(EINVAL);
                }
            }
            if (!s->fft[i]) {
                av_log(ctx, AV_LOG_ERROR, "Unable to create FFT context. "
                       "The window size might be too high.\n");
                return AVERROR(EINVAL);
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
            s->color_buffer[i] = av_calloc(s->orientation == VERTICAL ? s->h * 3 : s->w * 3, sizeof(**s->color_buffer));
            if (!s->color_buffer[i])
                return AVERROR(ENOMEM);
        }

        s->fft_data = av_calloc(s->nb_display_channels, sizeof(*s->fft_data));
        if (!s->fft_data)
            return AVERROR(ENOMEM);
        s->fft_scratch = av_calloc(s->nb_display_channels, sizeof(*s->fft_scratch));
        if (!s->fft_scratch)
            return AVERROR(ENOMEM);
        for (i = 0; i < s->nb_display_channels; i++) {
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
        }
        outpicref->color_range = AVCOL_RANGE_JPEG;

        if (!s->single_pic && s->legend)
            draw_legend(ctx, 0);
    }

    if ((s->orientation == VERTICAL   && s->xpos >= s->w) ||
        (s->orientation == HORIZONTAL && s->xpos >= s->h))
        s->xpos = 0;

    s->auto_frame_rate = av_make_q(inlink->sample_rate, s->hop_size);
    if (s->orientation == VERTICAL && s->sliding == FULLFRAME)
        s->auto_frame_rate.den *= s->w;
    if (s->orientation == HORIZONTAL && s->sliding == FULLFRAME)
        s->auto_frame_rate.den *= s->h;
    if (!s->single_pic && strcmp(s->rate_str, "auto")) {
        int ret = av_parse_video_rate(&s->frame_rate, s->rate_str);
        if (ret < 0)
            return ret;
    } else {
        s->frame_rate = s->auto_frame_rate;
    }
    outlink->frame_rate = s->frame_rate;
    outlink->time_base = av_inv_q(outlink->frame_rate);

    if (s->orientation == VERTICAL) {
        s->combine_buffer =
            av_realloc_f(s->combine_buffer, s->h * 3,
                         sizeof(*s->combine_buffer));
    } else {
        s->combine_buffer =
            av_realloc_f(s->combine_buffer, s->w * 3,
                         sizeof(*s->combine_buffer));
    }

    av_log(ctx, AV_LOG_VERBOSE, "s:%dx%d FFT window size:%d\n",
           s->w, s->h, s->win_size);

    av_audio_fifo_free(s->fifo);
    s->fifo = av_audio_fifo_alloc(inlink->format, inlink->channels, s->win_size);
    if (!s->fifo)
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
        s->combine_buffer[3 * y    ] = 0;
        s->combine_buffer[3 * y + 1] = 127.5;
        s->combine_buffer[3 * y + 2] = 127.5;
    }
}

static int plot_spectrum_column(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    ShowSpectrumContext *s = ctx->priv;
    AVFrame *outpicref = s->outpicref;
    int ret, plane, x, y, z = s->orientation == VERTICAL ? s->h : s->w;

    /* fill a new spectrum column */
    /* initialize buffer for combining to black */
    clear_combine_buffer(s, z);

    ctx->internal->execute(ctx, s->plot_channel, NULL, NULL, s->nb_display_channels);

    for (y = 0; y < z * 3; y++) {
        for (x = 0; x < s->nb_display_channels; x++) {
            s->combine_buffer[y] += s->color_buffer[x][y];
        }
    }

    av_frame_make_writable(s->outpicref);
    /* copy to output */
    if (s->orientation == VERTICAL) {
        if (s->sliding == SCROLL) {
            for (plane = 0; plane < 3; plane++) {
                for (y = 0; y < s->h; y++) {
                    uint8_t *p = outpicref->data[plane] + s->start_x +
                                 (y + s->start_y) * outpicref->linesize[plane];
                    memmove(p, p + 1, s->w - 1);
                }
            }
            s->xpos = s->w - 1;
        } else if (s->sliding == RSCROLL) {
            for (plane = 0; plane < 3; plane++) {
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
                *p = lrintf(av_clipf(s->combine_buffer[3 * y + plane], 0, 255));
                p -= outpicref->linesize[plane];
            }
        }
    } else {
        if (s->sliding == SCROLL) {
            for (plane = 0; plane < 3; plane++) {
                for (y = 1; y < s->h; y++) {
                    memmove(outpicref->data[plane] + (y-1 + s->start_y) * outpicref->linesize[plane] + s->start_x,
                            outpicref->data[plane] + (y   + s->start_y) * outpicref->linesize[plane] + s->start_x,
                            s->w);
                }
            }
            s->xpos = s->h - 1;
        } else if (s->sliding == RSCROLL) {
            for (plane = 0; plane < 3; plane++) {
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
                *p = lrintf(av_clipf(s->combine_buffer[3 * x + plane], 0, 255));
                p++;
            }
        }
    }

    if (s->sliding != FULLFRAME || s->xpos == 0)
        outpicref->pts = av_rescale_q(insamples->pts, inlink->time_base, outlink->time_base);

    s->xpos++;
    if (s->orientation == VERTICAL && s->xpos >= s->w)
        s->xpos = 0;
    if (s->orientation == HORIZONTAL && s->xpos >= s->h)
        s->xpos = 0;
    if (!s->single_pic && (s->sliding != FULLFRAME || s->xpos == 0)) {
        if (s->old_pts < outpicref->pts) {
            if (s->legend) {
                char *units = get_time(ctx, insamples->pts /(float)inlink->sample_rate, x);

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
            ret = ff_filter_frame(outlink, av_frame_clone(s->outpicref));
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
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);

    if (av_audio_fifo_size(s->fifo) < s->win_size) {
        AVFrame *frame = NULL;

        ret = ff_inlink_consume_frame(inlink, &frame);
        if (ret < 0)
            return ret;
        if (ret > 0) {
            s->pts = frame->pts;
            s->consumed = 0;

            av_audio_fifo_write(s->fifo, (void **)frame->extended_data, frame->nb_samples);
            av_frame_free(&frame);
        }
    }

    if (s->outpicref && av_audio_fifo_size(s->fifo) >= s->win_size) {
        AVFrame *fin = ff_get_audio_buffer(inlink, s->win_size);
        if (!fin)
            return AVERROR(ENOMEM);

        fin->pts = s->pts + s->consumed;
        s->consumed += s->hop_size;
        ret = av_audio_fifo_peek(s->fifo, (void **)fin->extended_data,
                                 FFMIN(s->win_size, av_audio_fifo_size(s->fifo)));
        if (ret < 0) {
            av_frame_free(&fin);
            return ret;
        }

        av_assert0(fin->nb_samples == s->win_size);

        ctx->internal->execute(ctx, run_channel_fft, fin, NULL, s->nb_display_channels);

        if (s->data == D_MAGNITUDE)
            ctx->internal->execute(ctx, calc_channel_magnitudes, NULL, NULL, s->nb_display_channels);

        if (s->data == D_PHASE)
            ctx->internal->execute(ctx, calc_channel_phases, NULL, NULL, s->nb_display_channels);

        ret = plot_spectrum_column(inlink, fin);

        av_frame_free(&fin);
        av_audio_fifo_drain(s->fifo, s->hop_size);
        if (ret <= 0)
            return ret;
    }

    if (ff_outlink_get_status(inlink) == AVERROR_EOF &&
        s->sliding == FULLFRAME &&
        s->xpos > 0 && s->outpicref) {
        int64_t pts;

        if (s->orientation == VERTICAL) {
            for (int i = 0; i < outlink->h; i++) {
                memset(s->outpicref->data[0] + i * s->outpicref->linesize[0] + s->xpos,   0, outlink->w - s->xpos);
                memset(s->outpicref->data[1] + i * s->outpicref->linesize[1] + s->xpos, 128, outlink->w - s->xpos);
                memset(s->outpicref->data[2] + i * s->outpicref->linesize[2] + s->xpos, 128, outlink->w - s->xpos);
            }
        } else {
            for (int i = s->xpos; i < outlink->h; i++) {
                memset(s->outpicref->data[0] + i * s->outpicref->linesize[0],   0, outlink->w);
                memset(s->outpicref->data[1] + i * s->outpicref->linesize[1], 128, outlink->w);
                memset(s->outpicref->data[2] + i * s->outpicref->linesize[2], 128, outlink->w);
            }
        }
        s->outpicref->pts += s->consumed;
        pts = s->outpicref->pts;
        ret = ff_filter_frame(outlink, s->outpicref);
        s->outpicref = NULL;
        ff_outlink_set_status(outlink, AVERROR_EOF, pts);
        return 0;
    }

    FF_FILTER_FORWARD_STATUS(inlink, outlink);
    if (ff_outlink_frame_wanted(outlink) && av_audio_fifo_size(s->fifo) < s->win_size) {
        ff_inlink_request_frame(inlink);
        return 0;
    }

    if (av_audio_fifo_size(s->fifo) >= s->win_size) {
        ff_filter_set_ready(ctx, 10);
        return 0;
    }
    return FFERROR_NOT_READY;
}

static const AVFilterPad showspectrum_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

static const AVFilterPad showspectrum_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_avf_showspectrum = {
    .name          = "showspectrum",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to a spectrum video output."),
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(ShowSpectrumContext),
    .inputs        = showspectrum_inputs,
    .outputs       = showspectrum_outputs,
    .activate      = activate,
    .priv_class    = &showspectrum_class,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};
#endif // CONFIG_SHOWSPECTRUM_FILTER

#if CONFIG_SHOWSPECTRUMPIC_FILTER

static const AVOption showspectrumpic_options[] = {
    { "size", "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "4096x2048"}, 0, 0, FLAGS },
    { "s",    "set video size", OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "4096x2048"}, 0, 0, FLAGS },
    { "mode", "set channel display mode", OFFSET(mode), AV_OPT_TYPE_INT, {.i64=COMBINED}, 0, NB_MODES-1, FLAGS, "mode" },
        { "combined", "combined mode", 0, AV_OPT_TYPE_CONST, {.i64=COMBINED}, 0, 0, FLAGS, "mode" },
        { "separate", "separate mode", 0, AV_OPT_TYPE_CONST, {.i64=SEPARATE}, 0, 0, FLAGS, "mode" },
    { "color", "set channel coloring", OFFSET(color_mode), AV_OPT_TYPE_INT, {.i64=INTENSITY}, 0, NB_CLMODES-1, FLAGS, "color" },
        { "channel",   "separate color for each channel", 0, AV_OPT_TYPE_CONST, {.i64=CHANNEL},   0, 0, FLAGS, "color" },
        { "intensity", "intensity based coloring",        0, AV_OPT_TYPE_CONST, {.i64=INTENSITY}, 0, 0, FLAGS, "color" },
        { "rainbow",   "rainbow based coloring",          0, AV_OPT_TYPE_CONST, {.i64=RAINBOW},   0, 0, FLAGS, "color" },
        { "moreland",  "moreland based coloring",         0, AV_OPT_TYPE_CONST, {.i64=MORELAND},  0, 0, FLAGS, "color" },
        { "nebulae",   "nebulae based coloring",          0, AV_OPT_TYPE_CONST, {.i64=NEBULAE},   0, 0, FLAGS, "color" },
        { "fire",      "fire based coloring",             0, AV_OPT_TYPE_CONST, {.i64=FIRE},      0, 0, FLAGS, "color" },
        { "fiery",     "fiery based coloring",            0, AV_OPT_TYPE_CONST, {.i64=FIERY},     0, 0, FLAGS, "color" },
        { "fruit",     "fruit based coloring",            0, AV_OPT_TYPE_CONST, {.i64=FRUIT},     0, 0, FLAGS, "color" },
        { "cool",      "cool based coloring",             0, AV_OPT_TYPE_CONST, {.i64=COOL},      0, 0, FLAGS, "color" },
        { "magma",     "magma based coloring",            0, AV_OPT_TYPE_CONST, {.i64=MAGMA},     0, 0, FLAGS, "color" },
        { "green",     "green based coloring",            0, AV_OPT_TYPE_CONST, {.i64=GREEN},     0, 0, FLAGS, "color" },
        { "viridis",   "viridis based coloring",          0, AV_OPT_TYPE_CONST, {.i64=VIRIDIS},   0, 0, FLAGS, "color" },
        { "plasma",    "plasma based coloring",           0, AV_OPT_TYPE_CONST, {.i64=PLASMA},    0, 0, FLAGS, "color" },
        { "cividis",   "cividis based coloring",          0, AV_OPT_TYPE_CONST, {.i64=CIVIDIS},   0, 0, FLAGS, "color" },
        { "terrain",   "terrain based coloring",          0, AV_OPT_TYPE_CONST, {.i64=TERRAIN},   0, 0, FLAGS, "color" },
    { "scale", "set display scale", OFFSET(scale), AV_OPT_TYPE_INT, {.i64=LOG}, 0, NB_SCALES-1, FLAGS, "scale" },
        { "lin",  "linear",      0, AV_OPT_TYPE_CONST, {.i64=LINEAR}, 0, 0, FLAGS, "scale" },
        { "sqrt", "square root", 0, AV_OPT_TYPE_CONST, {.i64=SQRT},   0, 0, FLAGS, "scale" },
        { "cbrt", "cubic root",  0, AV_OPT_TYPE_CONST, {.i64=CBRT},   0, 0, FLAGS, "scale" },
        { "log",  "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=LOG},    0, 0, FLAGS, "scale" },
        { "4thrt","4th root",    0, AV_OPT_TYPE_CONST, {.i64=FOURTHRT}, 0, 0, FLAGS, "scale" },
        { "5thrt","5th root",    0, AV_OPT_TYPE_CONST, {.i64=FIFTHRT},  0, 0, FLAGS, "scale" },
    { "fscale", "set frequency scale", OFFSET(fscale), AV_OPT_TYPE_INT, {.i64=F_LINEAR}, 0, NB_FSCALES-1, FLAGS, "fscale" },
        { "lin",  "linear",      0, AV_OPT_TYPE_CONST, {.i64=F_LINEAR}, 0, 0, FLAGS, "fscale" },
        { "log",  "logarithmic", 0, AV_OPT_TYPE_CONST, {.i64=F_LOG},    0, 0, FLAGS, "fscale" },
    { "saturation", "color saturation multiplier", OFFSET(saturation), AV_OPT_TYPE_FLOAT, {.dbl = 1}, -10, 10, FLAGS },
    { "win_func", "set window function", OFFSET(win_func), AV_OPT_TYPE_INT, {.i64 = WFUNC_HANNING}, 0, NB_WFUNC-1, FLAGS, "win_func" },
        { "rect",     "Rectangular",      0, AV_OPT_TYPE_CONST, {.i64=WFUNC_RECT},     0, 0, FLAGS, "win_func" },
        { "bartlett", "Bartlett",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BARTLETT}, 0, 0, FLAGS, "win_func" },
        { "hann",     "Hann",             0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HANNING},  0, 0, FLAGS, "win_func" },
        { "hanning",  "Hanning",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HANNING},  0, 0, FLAGS, "win_func" },
        { "hamming",  "Hamming",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_HAMMING},  0, 0, FLAGS, "win_func" },
        { "blackman", "Blackman",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BLACKMAN}, 0, 0, FLAGS, "win_func" },
        { "welch",    "Welch",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_WELCH},    0, 0, FLAGS, "win_func" },
        { "flattop",  "Flat-top",         0, AV_OPT_TYPE_CONST, {.i64=WFUNC_FLATTOP},  0, 0, FLAGS, "win_func" },
        { "bharris",  "Blackman-Harris",  0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BHARRIS},  0, 0, FLAGS, "win_func" },
        { "bnuttall", "Blackman-Nuttall", 0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BNUTTALL}, 0, 0, FLAGS, "win_func" },
        { "bhann",    "Bartlett-Hann",    0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BHANN},    0, 0, FLAGS, "win_func" },
        { "sine",     "Sine",             0, AV_OPT_TYPE_CONST, {.i64=WFUNC_SINE},     0, 0, FLAGS, "win_func" },
        { "nuttall",  "Nuttall",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_NUTTALL},  0, 0, FLAGS, "win_func" },
        { "lanczos",  "Lanczos",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_LANCZOS},  0, 0, FLAGS, "win_func" },
        { "gauss",    "Gauss",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_GAUSS},    0, 0, FLAGS, "win_func" },
        { "tukey",    "Tukey",            0, AV_OPT_TYPE_CONST, {.i64=WFUNC_TUKEY},    0, 0, FLAGS, "win_func" },
        { "dolph",    "Dolph-Chebyshev",  0, AV_OPT_TYPE_CONST, {.i64=WFUNC_DOLPH},    0, 0, FLAGS, "win_func" },
        { "cauchy",   "Cauchy",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_CAUCHY},   0, 0, FLAGS, "win_func" },
        { "parzen",   "Parzen",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_PARZEN},   0, 0, FLAGS, "win_func" },
        { "poisson",  "Poisson",          0, AV_OPT_TYPE_CONST, {.i64=WFUNC_POISSON},  0, 0, FLAGS, "win_func" },
        { "bohman",   "Bohman",           0, AV_OPT_TYPE_CONST, {.i64=WFUNC_BOHMAN},   0, 0, FLAGS, "win_func" },
    { "orientation", "set orientation", OFFSET(orientation), AV_OPT_TYPE_INT, {.i64=VERTICAL}, 0, NB_ORIENTATIONS-1, FLAGS, "orientation" },
        { "vertical",   NULL, 0, AV_OPT_TYPE_CONST, {.i64=VERTICAL},   0, 0, FLAGS, "orientation" },
        { "horizontal", NULL, 0, AV_OPT_TYPE_CONST, {.i64=HORIZONTAL}, 0, 0, FLAGS, "orientation" },
    { "gain", "set scale gain", OFFSET(gain), AV_OPT_TYPE_FLOAT, {.dbl = 1}, 0, 128, FLAGS },
    { "legend", "draw legend", OFFSET(legend), AV_OPT_TYPE_BOOL, {.i64 = 1}, 0, 1, FLAGS },
    { "rotation", "color rotation", OFFSET(rotation), AV_OPT_TYPE_FLOAT, {.dbl = 0}, -1, 1, FLAGS },
    { "start", "start frequency", OFFSET(start), AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT32_MAX, FLAGS },
    { "stop",  "stop frequency",  OFFSET(stop),  AV_OPT_TYPE_INT, {.i64 = 0}, 0, INT32_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(showspectrumpic);

static int showspectrumpic_request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    ShowSpectrumContext *s = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];
    int ret, samples;

    ret = ff_request_frame(inlink);
    samples = av_audio_fifo_size(s->fifo);
    if (ret == AVERROR_EOF && s->outpicref && samples > 0) {
        int consumed = 0;
        int x = 0, sz = s->orientation == VERTICAL ? s->w : s->h;
        int ch, spf, spb;
        AVFrame *fin;

        spf = s->win_size * (samples / ((s->win_size * sz) * ceil(samples / (float)(s->win_size * sz))));
        spf = FFMAX(1, spf);

        spb = (samples / (spf * sz)) * spf;

        fin = ff_get_audio_buffer(inlink, s->win_size);
        if (!fin)
            return AVERROR(ENOMEM);

        while (x < sz) {
            ret = av_audio_fifo_peek(s->fifo, (void **)fin->extended_data, s->win_size);
            if (ret < 0) {
                av_frame_free(&fin);
                return ret;
            }

            av_audio_fifo_drain(s->fifo, spf);

            if (ret < s->win_size) {
                for (ch = 0; ch < s->nb_display_channels; ch++) {
                    memset(fin->extended_data[ch] + ret * sizeof(float), 0,
                           (s->win_size - ret) * sizeof(float));
                }
            }

            ctx->internal->execute(ctx, run_channel_fft, fin, NULL, s->nb_display_channels);
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
            draw_legend(ctx, samples);

        ret = ff_filter_frame(outlink, s->outpicref);
        s->outpicref = NULL;
    }

    return ret;
}

static int showspectrumpic_filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    AVFilterContext *ctx = inlink->dst;
    ShowSpectrumContext *s = ctx->priv;
    int ret;

    ret = av_audio_fifo_write(s->fifo, (void **)insamples->extended_data, insamples->nb_samples);
    av_frame_free(&insamples);
    return ret;
}

static const AVFilterPad showspectrumpic_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = showspectrumpic_filter_frame,
    },
    { NULL }
};

static const AVFilterPad showspectrumpic_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = showspectrumpic_request_frame,
    },
    { NULL }
};

AVFilter ff_avf_showspectrumpic = {
    .name          = "showspectrumpic",
    .description   = NULL_IF_CONFIG_SMALL("Convert input audio to a spectrum video output single picture."),
    .uninit        = uninit,
    .query_formats = query_formats,
    .priv_size     = sizeof(ShowSpectrumContext),
    .inputs        = showspectrumpic_inputs,
    .outputs       = showspectrumpic_outputs,
    .priv_class    = &showspectrumpic_class,
    .flags         = AVFILTER_FLAG_SLICE_THREADS,
};

#endif // CONFIG_SHOWSPECTRUMPIC_FILTER
