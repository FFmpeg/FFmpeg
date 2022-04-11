/*
 * Copyright (c) 2012 Clément Bœsch
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
 * EBU R.128 implementation
 * @see http://tech.ebu.ch/loudness
 * @see https://www.youtube.com/watch?v=iuEtQqC-Sqo "EBU R128 Introduction - Florian Camerer"
 * @todo implement start/stop/reset through filter command injection
 */

#include <math.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/dict.h"
#include "libavutil/ffmath.h"
#include "libavutil/xga_font_data.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "libswresample/swresample.h"
#include "audio.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "internal.h"

#define ABS_THRES    -70            ///< silence gate: we discard anything below this absolute (LUFS) threshold
#define ABS_UP_THRES  10            ///< upper loud limit to consider (ABS_THRES being the minimum)
#define HIST_GRAIN   100            ///< defines histogram precision
#define HIST_SIZE  ((ABS_UP_THRES - ABS_THRES) * HIST_GRAIN + 1)

/**
 * A histogram is an array of HIST_SIZE hist_entry storing all the energies
 * recorded (with an accuracy of 1/HIST_GRAIN) of the loudnesses from ABS_THRES
 * (at 0) to ABS_UP_THRES (at HIST_SIZE-1).
 * This fixed-size system avoids the need of a list of energies growing
 * infinitely over the time and is thus more scalable.
 */
struct hist_entry {
    unsigned count;                 ///< how many times the corresponding value occurred
    double energy;                  ///< E = 10^((L + 0.691) / 10)
    double loudness;                ///< L = -0.691 + 10 * log10(E)
};

struct integrator {
    double **cache;                 ///< window of filtered samples (N ms)
    int cache_pos;                  ///< focus on the last added bin in the cache array
    int cache_size;
    double *sum;                    ///< sum of the last N ms filtered samples (cache content)
    int filled;                     ///< 1 if the cache is completely filled, 0 otherwise
    double rel_threshold;           ///< relative threshold
    double sum_kept_powers;         ///< sum of the powers (weighted sums) above absolute threshold
    int nb_kept_powers;             ///< number of sum above absolute threshold
    struct hist_entry *histogram;   ///< histogram of the powers, used to compute LRA and I
};

struct rect { int x, y, w, h; };

typedef struct EBUR128Context {
    const AVClass *class;           ///< AVClass context for log and options purpose

    /* peak metering */
    int peak_mode;                  ///< enabled peak modes
    double *true_peaks;             ///< true peaks per channel
    double *sample_peaks;           ///< sample peaks per channel
    double *true_peaks_per_frame;   ///< true peaks in a frame per channel
#if CONFIG_SWRESAMPLE
    SwrContext *swr_ctx;            ///< over-sampling context for true peak metering
    double *swr_buf;                ///< resampled audio data for true peak metering
    int swr_linesize;
#endif

    /* video  */
    int do_video;                   ///< 1 if video output enabled, 0 otherwise
    int w, h;                       ///< size of the video output
    struct rect text;               ///< rectangle for the LU legend on the left
    struct rect graph;              ///< rectangle for the main graph in the center
    struct rect gauge;              ///< rectangle for the gauge on the right
    AVFrame *outpicref;             ///< output picture reference, updated regularly
    int meter;                      ///< select a EBU mode between +9 and +18
    int scale_range;                ///< the range of LU values according to the meter
    int y_zero_lu;                  ///< the y value (pixel position) for 0 LU
    int y_opt_max;                  ///< the y value (pixel position) for 1 LU
    int y_opt_min;                  ///< the y value (pixel position) for -1 LU
    int *y_line_ref;                ///< y reference values for drawing the LU lines in the graph and the gauge

    /* audio */
    int nb_channels;                ///< number of channels in the input
    double *ch_weighting;           ///< channel weighting mapping
    int sample_count;               ///< sample count used for refresh frequency, reset at refresh
    int nb_samples;                 ///< number of samples to consume per single input frame
    int idx_insample;               ///< current sample position of processed samples in single input frame
    AVFrame *insamples;             ///< input samples reference, updated regularly

    /* Filter caches.
     * The mult by 3 in the following is for X[i], X[i-1] and X[i-2] */
    double *x;                      ///< 3 input samples cache for each channel
    double *y;                      ///< 3 pre-filter samples cache for each channel
    double *z;                      ///< 3 RLB-filter samples cache for each channel
    double pre_b[3];                ///< pre-filter numerator coefficients
    double pre_a[3];                ///< pre-filter denominator coefficients
    double rlb_b[3];                ///< rlb-filter numerator coefficients
    double rlb_a[3];                ///< rlb-filter denominator coefficients

    struct integrator i400;         ///< 400ms integrator, used for Momentary loudness  (M), and Integrated loudness (I)
    struct integrator i3000;        ///<    3s integrator, used for Short term loudness (S), and Loudness Range      (LRA)

    /* I and LRA specific */
    double integrated_loudness;     ///< integrated loudness in LUFS (I)
    double loudness_range;          ///< loudness range in LU (LRA)
    double lra_low, lra_high;       ///< low and high LRA values

    /* misc */
    int loglevel;                   ///< log level for frame logging
    int metadata;                   ///< whether or not to inject loudness results in frames
    int dual_mono;                  ///< whether or not to treat single channel input files as dual-mono
    double pan_law;                 ///< pan law value used to calculate dual-mono measurements
    int target;                     ///< target level in LUFS used to set relative zero LU in visualization
    int gauge_type;                 ///< whether gauge shows momentary or short
    int scale;                      ///< display scale type of statistics
} EBUR128Context;

enum {
    PEAK_MODE_NONE          = 0,
    PEAK_MODE_SAMPLES_PEAKS = 1<<1,
    PEAK_MODE_TRUE_PEAKS    = 1<<2,
};

enum {
    GAUGE_TYPE_MOMENTARY = 0,
    GAUGE_TYPE_SHORTTERM = 1,
};

enum {
    SCALE_TYPE_ABSOLUTE = 0,
    SCALE_TYPE_RELATIVE = 1,
};

#define OFFSET(x) offsetof(EBUR128Context, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define V AV_OPT_FLAG_VIDEO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption ebur128_options[] = {
    { "video", "set video output", OFFSET(do_video), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, V|F },
    { "size",  "set video size",   OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x480"}, 0, 0, V|F },
    { "meter", "set scale meter (+9 to +18)",  OFFSET(meter), AV_OPT_TYPE_INT, {.i64 = 9}, 9, 18, V|F },
    { "framelog", "force frame logging level", OFFSET(loglevel), AV_OPT_TYPE_INT, {.i64 = -1},   INT_MIN, INT_MAX, A|V|F, "level" },
        { "info",    "information logging level", 0, AV_OPT_TYPE_CONST, {.i64 = AV_LOG_INFO},    INT_MIN, INT_MAX, A|V|F, "level" },
        { "verbose", "verbose logging level",     0, AV_OPT_TYPE_CONST, {.i64 = AV_LOG_VERBOSE}, INT_MIN, INT_MAX, A|V|F, "level" },
    { "metadata", "inject metadata in the filtergraph", OFFSET(metadata), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, A|V|F },
    { "peak", "set peak mode", OFFSET(peak_mode), AV_OPT_TYPE_FLAGS, {.i64 = PEAK_MODE_NONE}, 0, INT_MAX, A|F, "mode" },
        { "none",   "disable any peak mode",   0, AV_OPT_TYPE_CONST, {.i64 = PEAK_MODE_NONE},          INT_MIN, INT_MAX, A|F, "mode" },
        { "sample", "enable peak-sample mode", 0, AV_OPT_TYPE_CONST, {.i64 = PEAK_MODE_SAMPLES_PEAKS}, INT_MIN, INT_MAX, A|F, "mode" },
        { "true",   "enable true-peak mode",   0, AV_OPT_TYPE_CONST, {.i64 = PEAK_MODE_TRUE_PEAKS},    INT_MIN, INT_MAX, A|F, "mode" },
    { "dualmono", "treat mono input files as dual-mono", OFFSET(dual_mono), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, A|F },
    { "panlaw", "set a specific pan law for dual-mono files", OFFSET(pan_law), AV_OPT_TYPE_DOUBLE, {.dbl = -3.01029995663978}, -10.0, 0.0, A|F },
    { "target", "set a specific target level in LUFS (-23 to 0)", OFFSET(target), AV_OPT_TYPE_INT, {.i64 = -23}, -23, 0, V|F },
    { "gauge", "set gauge display type", OFFSET(gauge_type), AV_OPT_TYPE_INT, {.i64 = 0 }, GAUGE_TYPE_MOMENTARY, GAUGE_TYPE_SHORTTERM, V|F, "gaugetype" },
        { "momentary",   "display momentary value",   0, AV_OPT_TYPE_CONST, {.i64 = GAUGE_TYPE_MOMENTARY}, INT_MIN, INT_MAX, V|F, "gaugetype" },
        { "m",           "display momentary value",   0, AV_OPT_TYPE_CONST, {.i64 = GAUGE_TYPE_MOMENTARY}, INT_MIN, INT_MAX, V|F, "gaugetype" },
        { "shortterm",   "display short-term value",  0, AV_OPT_TYPE_CONST, {.i64 = GAUGE_TYPE_SHORTTERM}, INT_MIN, INT_MAX, V|F, "gaugetype" },
        { "s",           "display short-term value",  0, AV_OPT_TYPE_CONST, {.i64 = GAUGE_TYPE_SHORTTERM}, INT_MIN, INT_MAX, V|F, "gaugetype" },
    { "scale", "sets display method for the stats", OFFSET(scale), AV_OPT_TYPE_INT, {.i64 = 0}, SCALE_TYPE_ABSOLUTE, SCALE_TYPE_RELATIVE, V|F, "scaletype" },
        { "absolute",   "display absolute values (LUFS)",          0, AV_OPT_TYPE_CONST, {.i64 = SCALE_TYPE_ABSOLUTE}, INT_MIN, INT_MAX, V|F, "scaletype" },
        { "LUFS",       "display absolute values (LUFS)",          0, AV_OPT_TYPE_CONST, {.i64 = SCALE_TYPE_ABSOLUTE}, INT_MIN, INT_MAX, V|F, "scaletype" },
        { "relative",   "display values relative to target (LU)",  0, AV_OPT_TYPE_CONST, {.i64 = SCALE_TYPE_RELATIVE}, INT_MIN, INT_MAX, V|F, "scaletype" },
        { "LU",         "display values relative to target (LU)",  0, AV_OPT_TYPE_CONST, {.i64 = SCALE_TYPE_RELATIVE}, INT_MIN, INT_MAX, V|F, "scaletype" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(ebur128);

static const uint8_t graph_colors[] = {
    0xdd, 0x66, 0x66,   // value above 1LU non reached below -1LU (impossible)
    0x66, 0x66, 0xdd,   // value below 1LU non reached below -1LU
    0x96, 0x33, 0x33,   // value above 1LU reached below -1LU (impossible)
    0x33, 0x33, 0x96,   // value below 1LU reached below -1LU
    0xdd, 0x96, 0x96,   // value above 1LU line non reached below -1LU (impossible)
    0x96, 0x96, 0xdd,   // value below 1LU line non reached below -1LU
    0xdd, 0x33, 0x33,   // value above 1LU line reached below -1LU (impossible)
    0x33, 0x33, 0xdd,   // value below 1LU line reached below -1LU
    0xdd, 0x66, 0x66,   // value above 1LU non reached above -1LU
    0x66, 0xdd, 0x66,   // value below 1LU non reached above -1LU
    0x96, 0x33, 0x33,   // value above 1LU reached above -1LU
    0x33, 0x96, 0x33,   // value below 1LU reached above -1LU
    0xdd, 0x96, 0x96,   // value above 1LU line non reached above -1LU
    0x96, 0xdd, 0x96,   // value below 1LU line non reached above -1LU
    0xdd, 0x33, 0x33,   // value above 1LU line reached above -1LU
    0x33, 0xdd, 0x33,   // value below 1LU line reached above -1LU
};

static const uint8_t *get_graph_color(const EBUR128Context *ebur128, int v, int y)
{
    const int above_opt_max = y > ebur128->y_opt_max;
    const int below_opt_min = y < ebur128->y_opt_min;
    const int reached = y >= v;
    const int line    = ebur128->y_line_ref[y] || y == ebur128->y_zero_lu;
    const int colorid = 8*below_opt_min+ 4*line + 2*reached + above_opt_max;
    return graph_colors + 3*colorid;
}

static inline int lu_to_y(const EBUR128Context *ebur128, double v)
{
    v += 2 * ebur128->meter;                            // make it in range [0;...]
    v  = av_clipf(v, 0, ebur128->scale_range);          // make sure it's in the graph scale
    v  = ebur128->scale_range - v;                      // invert value (y=0 is on top)
    return v * ebur128->graph.h / ebur128->scale_range; // rescale from scale range to px height
}

#define FONT8   0
#define FONT16  1

static const uint8_t font_colors[] = {
    0xdd, 0xdd, 0x00,
    0x00, 0x96, 0x96,
};

static void drawtext(AVFrame *pic, int x, int y, int ftid, const uint8_t *color, const char *fmt, ...)
{
    int i;
    char buf[128] = {0};
    const uint8_t *font;
    int font_height;
    va_list vl;

    if      (ftid == FONT16) font = avpriv_vga16_font, font_height = 16;
    else if (ftid == FONT8)  font = avpriv_cga_font,   font_height =  8;
    else return;

    va_start(vl, fmt);
    vsnprintf(buf, sizeof(buf), fmt, vl);
    va_end(vl);

    for (i = 0; buf[i]; i++) {
        int char_y, mask;
        uint8_t *p = pic->data[0] + y*pic->linesize[0] + (x + i*8)*3;

        for (char_y = 0; char_y < font_height; char_y++) {
            for (mask = 0x80; mask; mask >>= 1) {
                if (font[buf[i] * font_height + char_y] & mask)
                    memcpy(p, color, 3);
                else
                    memcpy(p, "\x00\x00\x00", 3);
                p += 3;
            }
            p += pic->linesize[0] - 8*3;
        }
    }
}

static void drawline(AVFrame *pic, int x, int y, int len, int step)
{
    int i;
    uint8_t *p = pic->data[0] + y*pic->linesize[0] + x*3;

    for (i = 0; i < len; i++) {
        memcpy(p, "\x00\xff\x00", 3);
        p += step;
    }
}

static int config_video_output(AVFilterLink *outlink)
{
    int i, x, y;
    uint8_t *p;
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *inlink = ctx->inputs[0];
    EBUR128Context *ebur128 = ctx->priv;
    AVFrame *outpicref;

    /* check if there is enough space to represent everything decently */
    if (ebur128->w < 640 || ebur128->h < 480) {
        av_log(ctx, AV_LOG_ERROR, "Video size %dx%d is too small, "
               "minimum size is 640x480\n", ebur128->w, ebur128->h);
        return AVERROR(EINVAL);
    }
    outlink->w = ebur128->w;
    outlink->h = ebur128->h;
    outlink->sample_aspect_ratio = (AVRational){1,1};
    outlink->time_base = inlink->time_base;
    outlink->frame_rate = av_make_q(10, 1);

#define PAD 8

    /* configure text area position and size */
    ebur128->text.x  = PAD;
    ebur128->text.y  = 40;
    ebur128->text.w  = 3 * 8;   // 3 characters
    ebur128->text.h  = ebur128->h - PAD - ebur128->text.y;

    /* configure gauge position and size */
    ebur128->gauge.w = 20;
    ebur128->gauge.h = ebur128->text.h;
    ebur128->gauge.x = ebur128->w - PAD - ebur128->gauge.w;
    ebur128->gauge.y = ebur128->text.y;

    /* configure graph position and size */
    ebur128->graph.x = ebur128->text.x + ebur128->text.w + PAD;
    ebur128->graph.y = ebur128->gauge.y;
    ebur128->graph.w = ebur128->gauge.x - ebur128->graph.x - PAD;
    ebur128->graph.h = ebur128->gauge.h;

    /* graph and gauge share the LU-to-pixel code */
    av_assert0(ebur128->graph.h == ebur128->gauge.h);

    /* prepare the initial picref buffer */
    av_frame_free(&ebur128->outpicref);
    ebur128->outpicref = outpicref =
        ff_get_video_buffer(outlink, outlink->w, outlink->h);
    if (!outpicref)
        return AVERROR(ENOMEM);
    outpicref->sample_aspect_ratio = (AVRational){1,1};

    /* init y references values (to draw LU lines) */
    ebur128->y_line_ref = av_calloc(ebur128->graph.h + 1, sizeof(*ebur128->y_line_ref));
    if (!ebur128->y_line_ref)
        return AVERROR(ENOMEM);

    /* black background */
    memset(outpicref->data[0], 0, ebur128->h * outpicref->linesize[0]);

    /* draw LU legends */
    drawtext(outpicref, PAD, PAD+16, FONT8, font_colors+3, " LU");
    for (i = ebur128->meter; i >= -ebur128->meter * 2; i--) {
        y = lu_to_y(ebur128, i);
        x = PAD + (i < 10 && i > -10) * 8;
        ebur128->y_line_ref[y] = i;
        y -= 4; // -4 to center vertically
        drawtext(outpicref, x, y + ebur128->graph.y, FONT8, font_colors+3,
                 "%c%d", i < 0 ? '-' : i > 0 ? '+' : ' ', FFABS(i));
    }

    /* draw graph */
    ebur128->y_zero_lu = lu_to_y(ebur128, 0);
    ebur128->y_opt_max = lu_to_y(ebur128, 1);
    ebur128->y_opt_min = lu_to_y(ebur128, -1);
    p = outpicref->data[0] + ebur128->graph.y * outpicref->linesize[0]
                           + ebur128->graph.x * 3;
    for (y = 0; y < ebur128->graph.h; y++) {
        const uint8_t *c = get_graph_color(ebur128, INT_MAX, y);

        for (x = 0; x < ebur128->graph.w; x++)
            memcpy(p + x*3, c, 3);
        p += outpicref->linesize[0];
    }

    /* draw fancy rectangles around the graph and the gauge */
#define DRAW_RECT(r) do { \
    drawline(outpicref, r.x,       r.y - 1,   r.w, 3); \
    drawline(outpicref, r.x,       r.y + r.h, r.w, 3); \
    drawline(outpicref, r.x - 1,   r.y,       r.h, outpicref->linesize[0]); \
    drawline(outpicref, r.x + r.w, r.y,       r.h, outpicref->linesize[0]); \
} while (0)
    DRAW_RECT(ebur128->graph);
    DRAW_RECT(ebur128->gauge);

    return 0;
}

static int config_audio_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    EBUR128Context *ebur128 = ctx->priv;

    /* Unofficial reversed parametrization of PRE
     * and RLB from 48kHz */

    double f0 = 1681.974450955533;
    double G = 3.999843853973347;
    double Q = 0.7071752369554196;

    double K = tan(M_PI * f0 / (double)inlink->sample_rate);
    double Vh = pow(10.0, G / 20.0);
    double Vb = pow(Vh, 0.4996667741545416);

    double a0 = 1.0 + K / Q + K * K;

    ebur128->pre_b[0] = (Vh + Vb * K / Q + K * K) / a0;
    ebur128->pre_b[1] = 2.0 * (K * K - Vh) / a0;
    ebur128->pre_b[2] = (Vh - Vb * K / Q + K * K) / a0;
    ebur128->pre_a[1] = 2.0 * (K * K - 1.0) / a0;
    ebur128->pre_a[2] = (1.0 - K / Q + K * K) / a0;

    f0 = 38.13547087602444;
    Q = 0.5003270373238773;
    K = tan(M_PI * f0 / (double)inlink->sample_rate);

    ebur128->rlb_b[0] = 1.0;
    ebur128->rlb_b[1] = -2.0;
    ebur128->rlb_b[2] = 1.0;
    ebur128->rlb_a[1] = 2.0 * (K * K - 1.0) / (1.0 + K / Q + K * K);
    ebur128->rlb_a[2] = (1.0 - K / Q + K * K) / (1.0 + K / Q + K * K);

    /* Force 100ms framing in case of metadata injection: the frames must have
     * a granularity of the window overlap to be accurately exploited.
     * As for the true peaks mode, it just simplifies the resampling buffer
     * allocation and the lookup in it (since sample buffers differ in size, it
     * can be more complex to integrate in the one-sample loop of
     * filter_frame()). */
    if (ebur128->metadata || (ebur128->peak_mode & PEAK_MODE_TRUE_PEAKS))
        ebur128->nb_samples = inlink->sample_rate / 10;
    return 0;
}

static int config_audio_output(AVFilterLink *outlink)
{
    int i;
    AVFilterContext *ctx = outlink->src;
    EBUR128Context *ebur128 = ctx->priv;
    const int nb_channels = outlink->ch_layout.nb_channels;

#define BACK_MASK (AV_CH_BACK_LEFT    |AV_CH_BACK_CENTER    |AV_CH_BACK_RIGHT| \
                   AV_CH_TOP_BACK_LEFT|AV_CH_TOP_BACK_CENTER|AV_CH_TOP_BACK_RIGHT| \
                   AV_CH_SIDE_LEFT                          |AV_CH_SIDE_RIGHT| \
                   AV_CH_SURROUND_DIRECT_LEFT               |AV_CH_SURROUND_DIRECT_RIGHT)

    ebur128->nb_channels  = nb_channels;
    ebur128->x            = av_calloc(nb_channels, 3 * sizeof(*ebur128->x));
    ebur128->y            = av_calloc(nb_channels, 3 * sizeof(*ebur128->y));
    ebur128->z            = av_calloc(nb_channels, 3 * sizeof(*ebur128->z));
    ebur128->ch_weighting = av_calloc(nb_channels, sizeof(*ebur128->ch_weighting));
    if (!ebur128->ch_weighting || !ebur128->x || !ebur128->y || !ebur128->z)
        return AVERROR(ENOMEM);

#define I400_BINS(x)  ((x) * 4 / 10)
#define I3000_BINS(x) ((x) * 3)

    ebur128->i400.sum = av_calloc(nb_channels, sizeof(*ebur128->i400.sum));
    ebur128->i3000.sum = av_calloc(nb_channels, sizeof(*ebur128->i3000.sum));
    ebur128->i400.cache = av_calloc(nb_channels, sizeof(*ebur128->i400.cache));
    ebur128->i3000.cache = av_calloc(nb_channels, sizeof(*ebur128->i3000.cache));
    if (!ebur128->i400.sum || !ebur128->i3000.sum ||
        !ebur128->i400.cache || !ebur128->i3000.cache)
        return AVERROR(ENOMEM);

    for (i = 0; i < nb_channels; i++) {
        /* channel weighting */
        const enum AVChannel chl = av_channel_layout_channel_from_index(&outlink->ch_layout, i);
        if (chl == AV_CHAN_LOW_FREQUENCY || chl == AV_CHAN_LOW_FREQUENCY_2) {
            ebur128->ch_weighting[i] = 0;
        } else if (chl < 64 && (1ULL << chl) & BACK_MASK) {
            ebur128->ch_weighting[i] = 1.41;
        } else {
            ebur128->ch_weighting[i] = 1.0;
        }

        if (!ebur128->ch_weighting[i])
            continue;

        /* bins buffer for the two integration window (400ms and 3s) */
        ebur128->i400.cache_size = I400_BINS(outlink->sample_rate);
        ebur128->i3000.cache_size = I3000_BINS(outlink->sample_rate);
        ebur128->i400.cache[i]  = av_calloc(ebur128->i400.cache_size,  sizeof(*ebur128->i400.cache[0]));
        ebur128->i3000.cache[i] = av_calloc(ebur128->i3000.cache_size, sizeof(*ebur128->i3000.cache[0]));
        if (!ebur128->i400.cache[i] || !ebur128->i3000.cache[i])
            return AVERROR(ENOMEM);
    }

#if CONFIG_SWRESAMPLE
    if (ebur128->peak_mode & PEAK_MODE_TRUE_PEAKS) {
        int ret;

        ebur128->swr_buf    = av_malloc_array(nb_channels, 19200 * sizeof(double));
        ebur128->true_peaks = av_calloc(nb_channels, sizeof(*ebur128->true_peaks));
        ebur128->true_peaks_per_frame = av_calloc(nb_channels, sizeof(*ebur128->true_peaks_per_frame));
        ebur128->swr_ctx    = swr_alloc();
        if (!ebur128->swr_buf || !ebur128->true_peaks ||
            !ebur128->true_peaks_per_frame || !ebur128->swr_ctx)
            return AVERROR(ENOMEM);

        av_opt_set_chlayout(ebur128->swr_ctx, "in_chlayout",    &outlink->ch_layout, 0);
        av_opt_set_int(ebur128->swr_ctx, "in_sample_rate",       outlink->sample_rate, 0);
        av_opt_set_sample_fmt(ebur128->swr_ctx, "in_sample_fmt", outlink->format, 0);

        av_opt_set_chlayout(ebur128->swr_ctx, "out_chlayout",    &outlink->ch_layout, 0);
        av_opt_set_int(ebur128->swr_ctx, "out_sample_rate",       192000, 0);
        av_opt_set_sample_fmt(ebur128->swr_ctx, "out_sample_fmt", outlink->format, 0);

        ret = swr_init(ebur128->swr_ctx);
        if (ret < 0)
            return ret;
    }
#endif

    if (ebur128->peak_mode & PEAK_MODE_SAMPLES_PEAKS) {
        ebur128->sample_peaks = av_calloc(nb_channels, sizeof(*ebur128->sample_peaks));
        if (!ebur128->sample_peaks)
            return AVERROR(ENOMEM);
    }

    return 0;
}

#define ENERGY(loudness) (ff_exp10(((loudness) + 0.691) / 10.))
#define LOUDNESS(energy) (-0.691 + 10 * log10(energy))
#define DBFS(energy) (20 * log10(energy))

static struct hist_entry *get_histogram(void)
{
    int i;
    struct hist_entry *h = av_calloc(HIST_SIZE, sizeof(*h));

    if (!h)
        return NULL;
    for (i = 0; i < HIST_SIZE; i++) {
        h[i].loudness = i / (double)HIST_GRAIN + ABS_THRES;
        h[i].energy   = ENERGY(h[i].loudness);
    }
    return h;
}

static av_cold int init(AVFilterContext *ctx)
{
    EBUR128Context *ebur128 = ctx->priv;
    AVFilterPad pad;
    int ret;

    if (ebur128->loglevel != AV_LOG_INFO &&
        ebur128->loglevel != AV_LOG_VERBOSE) {
        if (ebur128->do_video || ebur128->metadata)
            ebur128->loglevel = AV_LOG_VERBOSE;
        else
            ebur128->loglevel = AV_LOG_INFO;
    }

    if (!CONFIG_SWRESAMPLE && (ebur128->peak_mode & PEAK_MODE_TRUE_PEAKS)) {
        av_log(ctx, AV_LOG_ERROR,
               "True-peak mode requires libswresample to be performed\n");
        return AVERROR(EINVAL);
    }

    // if meter is  +9 scale, scale range is from -18 LU to  +9 LU (or 3*9)
    // if meter is +18 scale, scale range is from -36 LU to +18 LU (or 3*18)
    ebur128->scale_range = 3 * ebur128->meter;

    ebur128->i400.histogram  = get_histogram();
    ebur128->i3000.histogram = get_histogram();
    if (!ebur128->i400.histogram || !ebur128->i3000.histogram)
        return AVERROR(ENOMEM);

    ebur128->integrated_loudness = ABS_THRES;
    ebur128->loudness_range = 0;

    /* insert output pads */
    if (ebur128->do_video) {
        pad = (AVFilterPad){
            .name         = "out0",
            .type         = AVMEDIA_TYPE_VIDEO,
            .config_props = config_video_output,
        };
        ret = ff_append_outpad(ctx, &pad);
        if (ret < 0)
            return ret;
    }
    pad = (AVFilterPad){
        .name         = ebur128->do_video ? "out1" : "out0",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_audio_output,
    };
    ret = ff_append_outpad(ctx, &pad);
    if (ret < 0)
        return ret;

    /* summary */
    av_log(ctx, AV_LOG_VERBOSE, "EBU +%d scale\n", ebur128->meter);

    return 0;
}

#define HIST_POS(power) (int)(((power) - ABS_THRES) * HIST_GRAIN)

/* loudness and power should be set such as loudness = -0.691 +
 * 10*log10(power), we just avoid doing that calculus two times */
static int gate_update(struct integrator *integ, double power,
                       double loudness, int gate_thres)
{
    int ipower;
    double relative_threshold;
    int gate_hist_pos;

    /* update powers histograms by incrementing current power count */
    ipower = av_clip(HIST_POS(loudness), 0, HIST_SIZE - 1);
    integ->histogram[ipower].count++;

    /* compute relative threshold and get its position in the histogram */
    integ->sum_kept_powers += power;
    integ->nb_kept_powers++;
    relative_threshold = integ->sum_kept_powers / integ->nb_kept_powers;
    if (!relative_threshold)
        relative_threshold = 1e-12;
    integ->rel_threshold = LOUDNESS(relative_threshold) + gate_thres;
    gate_hist_pos = av_clip(HIST_POS(integ->rel_threshold), 0, HIST_SIZE - 1);

    return gate_hist_pos;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *insamples)
{
    int i, ch, idx_insample;
    AVFilterContext *ctx = inlink->dst;
    EBUR128Context *ebur128 = ctx->priv;
    const int nb_channels = ebur128->nb_channels;
    const int nb_samples  = insamples->nb_samples;
    const double *samples = (double *)insamples->data[0];
    AVFrame *pic = ebur128->outpicref;

#if CONFIG_SWRESAMPLE
    if (ebur128->peak_mode & PEAK_MODE_TRUE_PEAKS && ebur128->idx_insample == 0) {
        const double *swr_samples = ebur128->swr_buf;
        int ret = swr_convert(ebur128->swr_ctx, (uint8_t**)&ebur128->swr_buf, 19200,
                              (const uint8_t **)insamples->data, nb_samples);
        if (ret < 0)
            return ret;
        for (ch = 0; ch < nb_channels; ch++)
            ebur128->true_peaks_per_frame[ch] = 0.0;
        for (idx_insample = 0; idx_insample < ret; idx_insample++) {
            for (ch = 0; ch < nb_channels; ch++) {
                ebur128->true_peaks[ch] = FFMAX(ebur128->true_peaks[ch], fabs(*swr_samples));
                ebur128->true_peaks_per_frame[ch] = FFMAX(ebur128->true_peaks_per_frame[ch],
                                                          fabs(*swr_samples));
                swr_samples++;
            }
        }
    }
#endif

    for (idx_insample = ebur128->idx_insample; idx_insample < nb_samples; idx_insample++) {
        const int bin_id_400  = ebur128->i400.cache_pos;
        const int bin_id_3000 = ebur128->i3000.cache_pos;

#define MOVE_TO_NEXT_CACHED_ENTRY(time) do {                \
    ebur128->i##time.cache_pos++;                           \
    if (ebur128->i##time.cache_pos ==                       \
        ebur128->i##time.cache_size) {                      \
        ebur128->i##time.filled    = 1;                     \
        ebur128->i##time.cache_pos = 0;                     \
    }                                                       \
} while (0)

        MOVE_TO_NEXT_CACHED_ENTRY(400);
        MOVE_TO_NEXT_CACHED_ENTRY(3000);

        for (ch = 0; ch < nb_channels; ch++) {
            double bin;

            if (ebur128->peak_mode & PEAK_MODE_SAMPLES_PEAKS)
                ebur128->sample_peaks[ch] = FFMAX(ebur128->sample_peaks[ch], fabs(samples[idx_insample * nb_channels + ch]));

            ebur128->x[ch * 3] = samples[idx_insample * nb_channels + ch]; // set X[i]

            if (!ebur128->ch_weighting[ch])
                continue;

            /* Y[i] = X[i]*b0 + X[i-1]*b1 + X[i-2]*b2 - Y[i-1]*a1 - Y[i-2]*a2 */
#define FILTER(Y, X, NUM, DEN) do {                                             \
            double *dst = ebur128->Y + ch*3;                                    \
            double *src = ebur128->X + ch*3;                                    \
            dst[2] = dst[1];                                                    \
            dst[1] = dst[0];                                                    \
            dst[0] = src[0]*NUM[0] + src[1]*NUM[1] + src[2]*NUM[2]              \
                                   - dst[1]*DEN[1] - dst[2]*DEN[2];             \
} while (0)

            // TODO: merge both filters in one?
            FILTER(y, x, ebur128->pre_b, ebur128->pre_a);  // apply pre-filter
            ebur128->x[ch * 3 + 2] = ebur128->x[ch * 3 + 1];
            ebur128->x[ch * 3 + 1] = ebur128->x[ch * 3    ];
            FILTER(z, y, ebur128->rlb_b, ebur128->rlb_a);  // apply RLB-filter

            bin = ebur128->z[ch * 3] * ebur128->z[ch * 3];

            /* add the new value, and limit the sum to the cache size (400ms or 3s)
             * by removing the oldest one */
            ebur128->i400.sum [ch] = ebur128->i400.sum [ch] + bin - ebur128->i400.cache [ch][bin_id_400];
            ebur128->i3000.sum[ch] = ebur128->i3000.sum[ch] + bin - ebur128->i3000.cache[ch][bin_id_3000];

            /* override old cache entry with the new value */
            ebur128->i400.cache [ch][bin_id_400 ] = bin;
            ebur128->i3000.cache[ch][bin_id_3000] = bin;
        }

        /* For integrated loudness, gating blocks are 400ms long with 75%
         * overlap (see BS.1770-2 p5), so a re-computation is needed each 100ms
         * (4800 samples at 48kHz). */
        if (++ebur128->sample_count == inlink->sample_rate / 10) {
            double loudness_400, loudness_3000;
            double power_400 = 1e-12, power_3000 = 1e-12;
            AVFilterLink *outlink = ctx->outputs[0];
            const int64_t pts = insamples->pts +
                av_rescale_q(idx_insample, (AVRational){ 1, inlink->sample_rate },
                             outlink->time_base);

            ebur128->sample_count = 0;

#define COMPUTE_LOUDNESS(m, time) do {                                              \
    if (ebur128->i##time.filled) {                                                  \
        /* weighting sum of the last <time> ms */                                   \
        for (ch = 0; ch < nb_channels; ch++)                                        \
            power_##time += ebur128->ch_weighting[ch] * ebur128->i##time.sum[ch];   \
        power_##time /= I##time##_BINS(inlink->sample_rate);                        \
    }                                                                               \
    loudness_##time = LOUDNESS(power_##time);                                       \
} while (0)

            COMPUTE_LOUDNESS(M,  400);
            COMPUTE_LOUDNESS(S, 3000);

            /* Integrated loudness */
#define I_GATE_THRES -10  // initially defined to -8 LU in the first EBU standard

            if (loudness_400 >= ABS_THRES) {
                double integrated_sum = 0.0;
                uint64_t nb_integrated = 0;
                int gate_hist_pos = gate_update(&ebur128->i400, power_400,
                                                loudness_400, I_GATE_THRES);

                /* compute integrated loudness by summing the histogram values
                 * above the relative threshold */
                for (i = gate_hist_pos; i < HIST_SIZE; i++) {
                    const unsigned nb_v = ebur128->i400.histogram[i].count;
                    nb_integrated  += nb_v;
                    integrated_sum += nb_v * ebur128->i400.histogram[i].energy;
                }
                if (nb_integrated) {
                    ebur128->integrated_loudness = LOUDNESS(integrated_sum / nb_integrated);
                    /* dual-mono correction */
                    if (nb_channels == 1 && ebur128->dual_mono) {
                        ebur128->integrated_loudness -= ebur128->pan_law;
                    }
                }
            }

            /* LRA */
#define LRA_GATE_THRES -20
#define LRA_LOWER_PRC   10
#define LRA_HIGHER_PRC  95

            /* XXX: example code in EBU 3342 is ">=" but formula in BS.1770
             * specs is ">" */
            if (loudness_3000 >= ABS_THRES) {
                uint64_t nb_powers = 0;
                int gate_hist_pos = gate_update(&ebur128->i3000, power_3000,
                                                loudness_3000, LRA_GATE_THRES);

                for (i = gate_hist_pos; i < HIST_SIZE; i++)
                    nb_powers += ebur128->i3000.histogram[i].count;
                if (nb_powers) {
                    uint64_t n, nb_pow;

                    /* get lower loudness to consider */
                    n = 0;
                    nb_pow = LRA_LOWER_PRC * nb_powers * 0.01 + 0.5;
                    for (i = gate_hist_pos; i < HIST_SIZE; i++) {
                        n += ebur128->i3000.histogram[i].count;
                        if (n >= nb_pow) {
                            ebur128->lra_low = ebur128->i3000.histogram[i].loudness;
                            break;
                        }
                    }

                    /* get higher loudness to consider */
                    n = nb_powers;
                    nb_pow = LRA_HIGHER_PRC * nb_powers * 0.01 + 0.5;
                    for (i = HIST_SIZE - 1; i >= 0; i--) {
                        n -= FFMIN(n, ebur128->i3000.histogram[i].count);
                        if (n < nb_pow) {
                            ebur128->lra_high = ebur128->i3000.histogram[i].loudness;
                            break;
                        }
                    }

                    // XXX: show low & high on the graph?
                    ebur128->loudness_range = ebur128->lra_high - ebur128->lra_low;
                }
            }

            /* dual-mono correction */
            if (nb_channels == 1 && ebur128->dual_mono) {
                loudness_400 -= ebur128->pan_law;
                loudness_3000 -= ebur128->pan_law;
            }

#define LOG_FMT "TARGET:%d LUFS    M:%6.1f S:%6.1f     I:%6.1f %s       LRA:%6.1f LU"

            /* push one video frame */
            if (ebur128->do_video) {
                AVFrame *clone;
                int x, y;
                uint8_t *p;
                double gauge_value;
                int y_loudness_lu_graph, y_loudness_lu_gauge;

                if (ebur128->gauge_type == GAUGE_TYPE_MOMENTARY) {
                    gauge_value = loudness_400 - ebur128->target;
                } else {
                    gauge_value = loudness_3000 - ebur128->target;
                }

                y_loudness_lu_graph = lu_to_y(ebur128, loudness_3000 - ebur128->target);
                y_loudness_lu_gauge = lu_to_y(ebur128, gauge_value);

                av_frame_make_writable(pic);
                /* draw the graph using the short-term loudness */
                p = pic->data[0] + ebur128->graph.y*pic->linesize[0] + ebur128->graph.x*3;
                for (y = 0; y < ebur128->graph.h; y++) {
                    const uint8_t *c = get_graph_color(ebur128, y_loudness_lu_graph, y);

                    memmove(p, p + 3, (ebur128->graph.w - 1) * 3);
                    memcpy(p + (ebur128->graph.w - 1) * 3, c, 3);
                    p += pic->linesize[0];
                }

                /* draw the gauge using either momentary or short-term loudness */
                p = pic->data[0] + ebur128->gauge.y*pic->linesize[0] + ebur128->gauge.x*3;
                for (y = 0; y < ebur128->gauge.h; y++) {
                    const uint8_t *c = get_graph_color(ebur128, y_loudness_lu_gauge, y);

                    for (x = 0; x < ebur128->gauge.w; x++)
                        memcpy(p + x*3, c, 3);
                    p += pic->linesize[0];
                }

                /* draw textual info */
                if (ebur128->scale == SCALE_TYPE_ABSOLUTE) {
                    drawtext(pic, PAD, PAD - PAD/2, FONT16, font_colors,
                             LOG_FMT "     ", // padding to erase trailing characters
                             ebur128->target, loudness_400, loudness_3000,
                             ebur128->integrated_loudness, "LUFS", ebur128->loudness_range);
                } else {
                    drawtext(pic, PAD, PAD - PAD/2, FONT16, font_colors,
                             LOG_FMT "     ", // padding to erase trailing characters
                             ebur128->target, loudness_400-ebur128->target, loudness_3000-ebur128->target,
                             ebur128->integrated_loudness-ebur128->target, "LU", ebur128->loudness_range);
                }

                /* set pts and push frame */
                pic->pts = pts;
                clone = av_frame_clone(pic);
                if (!clone)
                    return AVERROR(ENOMEM);
                ebur128->idx_insample = idx_insample + 1;
                ff_filter_set_ready(ctx, 100);
                return ff_filter_frame(outlink, clone);
            }

            if (ebur128->metadata) { /* happens only once per filter_frame call */
                char metabuf[128];
#define META_PREFIX "lavfi.r128."

#define SET_META(name, var) do {                                            \
    snprintf(metabuf, sizeof(metabuf), "%.3f", var);                        \
    av_dict_set(&insamples->metadata, name, metabuf, 0);                    \
} while (0)

#define SET_META_PEAK(name, ptype) do {                                     \
    if (ebur128->peak_mode & PEAK_MODE_ ## ptype ## _PEAKS) {               \
        double max_peak = 0.0;                                              \
        char key[64];                                                       \
        for (ch = 0; ch < nb_channels; ch++) {                              \
            snprintf(key, sizeof(key),                                      \
                     META_PREFIX AV_STRINGIFY(name) "_peaks_ch%d", ch);     \
            max_peak = fmax(max_peak, ebur128->name##_peaks[ch]);           \
            SET_META(key, ebur128->name##_peaks[ch]);                       \
        }                                                                   \
        snprintf(key, sizeof(key),                                          \
                 META_PREFIX AV_STRINGIFY(name) "_peak");                   \
        SET_META(key, max_peak);                                            \
    }                                                                       \
} while (0)

                SET_META(META_PREFIX "M",        loudness_400);
                SET_META(META_PREFIX "S",        loudness_3000);
                SET_META(META_PREFIX "I",        ebur128->integrated_loudness);
                SET_META(META_PREFIX "LRA",      ebur128->loudness_range);
                SET_META(META_PREFIX "LRA.low",  ebur128->lra_low);
                SET_META(META_PREFIX "LRA.high", ebur128->lra_high);

                SET_META_PEAK(sample, SAMPLES);
                SET_META_PEAK(true,   TRUE);
            }

            if (ebur128->scale == SCALE_TYPE_ABSOLUTE) {
                av_log(ctx, ebur128->loglevel, "t: %-10s " LOG_FMT,
                       av_ts2timestr(pts, &outlink->time_base),
                       ebur128->target, loudness_400, loudness_3000,
                       ebur128->integrated_loudness, "LUFS", ebur128->loudness_range);
            } else {
                av_log(ctx, ebur128->loglevel, "t: %-10s " LOG_FMT,
                       av_ts2timestr(pts, &outlink->time_base),
                       ebur128->target, loudness_400-ebur128->target, loudness_3000-ebur128->target,
                       ebur128->integrated_loudness-ebur128->target, "LU", ebur128->loudness_range);
            }

#define PRINT_PEAKS(str, sp, ptype) do {                            \
    if (ebur128->peak_mode & PEAK_MODE_ ## ptype ## _PEAKS) {       \
        av_log(ctx, ebur128->loglevel, "  " str ":");               \
        for (ch = 0; ch < nb_channels; ch++)                        \
            av_log(ctx, ebur128->loglevel, " %5.1f", DBFS(sp[ch])); \
        av_log(ctx, ebur128->loglevel, " dBFS");                    \
    }                                                               \
} while (0)

            PRINT_PEAKS("SPK", ebur128->sample_peaks, SAMPLES);
            PRINT_PEAKS("FTPK", ebur128->true_peaks_per_frame, TRUE);
            PRINT_PEAKS("TPK", ebur128->true_peaks,   TRUE);
            av_log(ctx, ebur128->loglevel, "\n");

        }
    }

    ebur128->idx_insample = 0;
    ebur128->insamples = NULL;

    return ff_filter_frame(ctx->outputs[ebur128->do_video], insamples);
}

static int activate(AVFilterContext *ctx)
{
    AVFilterLink *inlink = ctx->inputs[0];
    EBUR128Context *ebur128 = ctx->priv;
    AVFilterLink *voutlink = ctx->outputs[0];
    AVFilterLink *outlink = ctx->outputs[ebur128->do_video];
    int ret;

    FF_FILTER_FORWARD_STATUS_BACK(outlink, inlink);
    if (ebur128->do_video)
        FF_FILTER_FORWARD_STATUS_BACK(voutlink, inlink);

    if (!ebur128->insamples) {
        AVFrame *in;

        if (ebur128->nb_samples > 0) {
            ret = ff_inlink_consume_samples(inlink, ebur128->nb_samples, ebur128->nb_samples, &in);
        } else {
            ret = ff_inlink_consume_frame(inlink, &in);
        }
        if (ret < 0)
            return ret;
        if (ret > 0)
            ebur128->insamples = in;
    }

    if (ebur128->insamples)
        ret = filter_frame(inlink, ebur128->insamples);

    FF_FILTER_FORWARD_STATUS_ALL(inlink, ctx);
    FF_FILTER_FORWARD_WANTED(outlink, inlink);
    if (ebur128->do_video)
        FF_FILTER_FORWARD_WANTED(voutlink, inlink);

    return ret;
}

static int query_formats(AVFilterContext *ctx)
{
    EBUR128Context *ebur128 = ctx->priv;
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];
    int ret;

    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_NONE };
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE };

    /* set optional output video format */
    if (ebur128->do_video) {
        formats = ff_make_format_list(pix_fmts);
        if ((ret = ff_formats_ref(formats, &outlink->incfg.formats)) < 0)
            return ret;
        outlink = ctx->outputs[1];
    }

    /* set input and output audio formats
     * Note: ff_set_common_* functions are not used because they affect all the
     * links, and thus break the video format negotiation */
    formats = ff_make_format_list(sample_fmts);
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.formats)) < 0 ||
        (ret = ff_formats_ref(formats, &outlink->incfg.formats)) < 0)
        return ret;

    layouts = ff_all_channel_layouts();
    if ((ret = ff_channel_layouts_ref(layouts, &inlink->outcfg.channel_layouts)) < 0 ||
        (ret = ff_channel_layouts_ref(layouts, &outlink->incfg.channel_layouts)) < 0)
        return ret;

    formats = ff_all_samplerates();
    if ((ret = ff_formats_ref(formats, &inlink->outcfg.samplerates)) < 0 ||
        (ret = ff_formats_ref(formats, &outlink->incfg.samplerates)) < 0)
        return ret;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int i;
    EBUR128Context *ebur128 = ctx->priv;

    /* dual-mono correction */
    if (ebur128->nb_channels == 1 && ebur128->dual_mono) {
        ebur128->i400.rel_threshold -= ebur128->pan_law;
        ebur128->i3000.rel_threshold -= ebur128->pan_law;
        ebur128->lra_low -= ebur128->pan_law;
        ebur128->lra_high -= ebur128->pan_law;
    }

    av_log(ctx, AV_LOG_INFO, "Summary:\n\n"
           "  Integrated loudness:\n"
           "    I:         %5.1f LUFS\n"
           "    Threshold: %5.1f LUFS\n\n"
           "  Loudness range:\n"
           "    LRA:       %5.1f LU\n"
           "    Threshold: %5.1f LUFS\n"
           "    LRA low:   %5.1f LUFS\n"
           "    LRA high:  %5.1f LUFS",
           ebur128->integrated_loudness, ebur128->i400.rel_threshold,
           ebur128->loudness_range,      ebur128->i3000.rel_threshold,
           ebur128->lra_low, ebur128->lra_high);

#define PRINT_PEAK_SUMMARY(str, sp, ptype) do {                  \
    int ch;                                                      \
    double maxpeak;                                              \
    maxpeak = 0.0;                                               \
    if (ebur128->peak_mode & PEAK_MODE_ ## ptype ## _PEAKS) {    \
        for (ch = 0; ch < ebur128->nb_channels; ch++)            \
            maxpeak = FFMAX(maxpeak, sp[ch]);                    \
        av_log(ctx, AV_LOG_INFO, "\n\n  " str " peak:\n"         \
               "    Peak:      %5.1f dBFS",                      \
               DBFS(maxpeak));                                   \
    }                                                            \
} while (0)

    PRINT_PEAK_SUMMARY("Sample", ebur128->sample_peaks, SAMPLES);
    PRINT_PEAK_SUMMARY("True",   ebur128->true_peaks,   TRUE);
    av_log(ctx, AV_LOG_INFO, "\n");

    av_freep(&ebur128->y_line_ref);
    av_freep(&ebur128->x);
    av_freep(&ebur128->y);
    av_freep(&ebur128->z);
    av_freep(&ebur128->ch_weighting);
    av_freep(&ebur128->true_peaks);
    av_freep(&ebur128->sample_peaks);
    av_freep(&ebur128->true_peaks_per_frame);
    av_freep(&ebur128->i400.sum);
    av_freep(&ebur128->i3000.sum);
    av_freep(&ebur128->i400.histogram);
    av_freep(&ebur128->i3000.histogram);
    for (i = 0; i < ebur128->nb_channels; i++) {
        if (ebur128->i400.cache)
            av_freep(&ebur128->i400.cache[i]);
        if (ebur128->i3000.cache)
            av_freep(&ebur128->i3000.cache[i]);
    }
    av_freep(&ebur128->i400.cache);
    av_freep(&ebur128->i3000.cache);
    av_frame_free(&ebur128->outpicref);
#if CONFIG_SWRESAMPLE
    av_freep(&ebur128->swr_buf);
    swr_free(&ebur128->swr_ctx);
#endif
}

static const AVFilterPad ebur128_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_audio_input,
    },
};

const AVFilter ff_af_ebur128 = {
    .name          = "ebur128",
    .description   = NULL_IF_CONFIG_SMALL("EBU R128 scanner."),
    .priv_size     = sizeof(EBUR128Context),
    .init          = init,
    .uninit        = uninit,
    .activate      = activate,
    FILTER_INPUTS(ebur128_inputs),
    .outputs       = NULL,
    FILTER_QUERY_FUNC(query_formats),
    .priv_class    = &ebur128_class,
    .flags         = AVFILTER_FLAG_DYNAMIC_OUTPUTS,
};
