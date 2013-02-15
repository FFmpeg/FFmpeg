/*
 * Copyright (c) 2012 Clément Bœsch
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/**
 * @file
 * EBU R.128 implementation
 * @see http://tech.ebu.ch/loudness
 * @see https://www.youtube.com/watch?v=iuEtQqC-Sqo "EBU R128 Introduction - Florian Camerer"
 * @todo True Peak
 * @todo implement start/stop/reset through filter command injection
 * @todo support other frequencies to avoid resampling
 */

#include <math.h>

#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/xga_font_data.h"
#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

#define MAX_CHANNELS 63

/* pre-filter coefficients */
#define PRE_B0  1.53512485958697
#define PRE_B1 -2.69169618940638
#define PRE_B2  1.19839281085285
#define PRE_A1 -1.69065929318241
#define PRE_A2  0.73248077421585

/* RLB-filter coefficients */
#define RLB_B0  1.0
#define RLB_B1 -2.0
#define RLB_B2  1.0
#define RLB_A1 -1.99004745483398
#define RLB_A2  0.99007225036621

#define ABS_THRES    -70            ///< silence gate: we discard anything below this absolute (LUFS) threshold
#define ABS_UP_THRES  10            ///< upper loud limit to consider (ABS_THRES being the minimum)
#define HIST_GRAIN   100            ///< defines histogram precision
#define HIST_SIZE  ((ABS_UP_THRES - ABS_THRES) * HIST_GRAIN + 1)

/**
 * An histogram is an array of HIST_SIZE hist_entry storing all the energies
 * recorded (with an accuracy of 1/HIST_GRAIN) of the loudnesses from ABS_THRES
 * (at 0) to ABS_UP_THRES (at HIST_SIZE-1).
 * This fixed-size system avoids the need of a list of energies growing
 * infinitely over the time and is thus more scalable.
 */
struct hist_entry {
    int count;                      ///< how many times the corresponding value occurred
    double energy;                  ///< E = 10^((L + 0.691) / 10)
    double loudness;                ///< L = -0.691 + 10 * log10(E)
};

struct integrator {
    double *cache[MAX_CHANNELS];    ///< window of filtered samples (N ms)
    int cache_pos;                  ///< focus on the last added bin in the cache array
    double sum[MAX_CHANNELS];       ///< sum of the last N ms filtered samples (cache content)
    int filled;                     ///< 1 if the cache is completely filled, 0 otherwise
    double rel_threshold;           ///< relative threshold
    double sum_kept_powers;         ///< sum of the powers (weighted sums) above absolute threshold
    int nb_kept_powers;             ///< number of sum above absolute threshold
    struct hist_entry *histogram;   ///< histogram of the powers, used to compute LRA and I
};

struct rect { int x, y, w, h; };

typedef struct {
    const AVClass *class;           ///< AVClass context for log and options purpose

    /* video  */
    int do_video;                   ///< 1 if video output enabled, 0 otherwise
    int w, h;                       ///< size of the video output
    struct rect text;               ///< rectangle for the LU legend on the left
    struct rect graph;              ///< rectangle for the main graph in the center
    struct rect gauge;              ///< rectangle for the gauge on the right
    AVFilterBufferRef *outpicref;   ///< output picture reference, updated regularly
    int meter;                      ///< select a EBU mode between +9 and +18
    int scale_range;                ///< the range of LU values according to the meter
    int y_zero_lu;                  ///< the y value (pixel position) for 0 LU
    int *y_line_ref;                ///< y reference values for drawing the LU lines in the graph and the gauge

    /* audio */
    int nb_channels;                ///< number of channels in the input
    double *ch_weighting;           ///< channel weighting mapping
    int sample_count;               ///< sample count used for refresh frequency, reset at refresh

    /* Filter caches.
     * The mult by 3 in the following is for X[i], X[i-1] and X[i-2] */
    double x[MAX_CHANNELS * 3];     ///< 3 input samples cache for each channel
    double y[MAX_CHANNELS * 3];     ///< 3 pre-filter samples cache for each channel
    double z[MAX_CHANNELS * 3];     ///< 3 RLB-filter samples cache for each channel

#define I400_BINS  (48000 * 4 / 10)
#define I3000_BINS (48000 * 3)
    struct integrator i400;         ///< 400ms integrator, used for Momentary loudness  (M), and Integrated loudness (I)
    struct integrator i3000;        ///<    3s integrator, used for Short term loudness (S), and Loudness Range      (LRA)

    /* I and LRA specific */
    double integrated_loudness;     ///< integrated loudness in LUFS (I)
    double loudness_range;          ///< loudness range in LU (LRA)
    double lra_low, lra_high;       ///< low and high LRA values
} EBUR128Context;

#define OFFSET(x) offsetof(EBUR128Context, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define V AV_OPT_FLAG_VIDEO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption ebur128_options[] = {
    { "video", "set video output", OFFSET(do_video), AV_OPT_TYPE_INT, {.i64 = 0}, 0, 1, V|F },
    { "size",  "set video size",   OFFSET(w), AV_OPT_TYPE_IMAGE_SIZE, {.str = "640x480"}, 0, 0, V|F },
    { "meter", "set scale meter (+9 to +18)",  OFFSET(meter), AV_OPT_TYPE_INT, {.i64 = 9}, 9, 18, V|F },
    { NULL },
};

AVFILTER_DEFINE_CLASS(ebur128);

static const uint8_t graph_colors[] = {
    0xdd, 0x66, 0x66,   // value above 0LU non reached
    0x66, 0x66, 0xdd,   // value below 0LU non reached
    0x96, 0x33, 0x33,   // value above 0LU reached
    0x33, 0x33, 0x96,   // value below 0LU reached
    0xdd, 0x96, 0x96,   // value above 0LU line non reached
    0x96, 0x96, 0xdd,   // value below 0LU line non reached
    0xdd, 0x33, 0x33,   // value above 0LU line reached
    0x33, 0x33, 0xdd,   // value below 0LU line reached
};

static const uint8_t *get_graph_color(const EBUR128Context *ebur128, int v, int y)
{
    const int below0  = y > ebur128->y_zero_lu;
    const int reached = y >= v;
    const int line    = ebur128->y_line_ref[y] || y == ebur128->y_zero_lu;
    const int colorid = 4*line + 2*reached + below0;
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

static void drawtext(AVFilterBufferRef *pic, int x, int y, int ftid, const uint8_t *color, const char *fmt, ...)
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

static void drawline(AVFilterBufferRef *pic, int x, int y, int len, int step)
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
    EBUR128Context *ebur128 = ctx->priv;
    AVFilterBufferRef *outpicref;

    /* check if there is enough space to represent everything decently */
    if (ebur128->w < 640 || ebur128->h < 480) {
        av_log(ctx, AV_LOG_ERROR, "Video size %dx%d is too small, "
               "minimum size is 640x480\n", ebur128->w, ebur128->h);
        return AVERROR(EINVAL);
    }
    outlink->w = ebur128->w;
    outlink->h = ebur128->h;

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
    avfilter_unref_bufferp(&ebur128->outpicref);
    ebur128->outpicref = outpicref =
        ff_get_video_buffer(outlink, AV_PERM_WRITE|AV_PERM_PRESERVE|AV_PERM_REUSE2,
                            outlink->w, outlink->h);
    if (!outpicref)
        return AVERROR(ENOMEM);
    outlink->sample_aspect_ratio = (AVRational){1,1};

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

static int config_audio_output(AVFilterLink *outlink)
{
    int i;
    int idx_bitposn = 0;
    AVFilterContext *ctx = outlink->src;
    EBUR128Context *ebur128 = ctx->priv;
    const int nb_channels = av_get_channel_layout_nb_channels(outlink->channel_layout);

#define BACK_MASK (AV_CH_BACK_LEFT    |AV_CH_BACK_CENTER    |AV_CH_BACK_RIGHT| \
                   AV_CH_TOP_BACK_LEFT|AV_CH_TOP_BACK_CENTER|AV_CH_TOP_BACK_RIGHT| \
                   AV_CH_SIDE_LEFT                          |AV_CH_SIDE_RIGHT| \
                   AV_CH_SURROUND_DIRECT_LEFT               |AV_CH_SURROUND_DIRECT_RIGHT)

    ebur128->nb_channels  = nb_channels;
    ebur128->ch_weighting = av_calloc(nb_channels, sizeof(*ebur128->ch_weighting));
    if (!ebur128->ch_weighting)
        return AVERROR(ENOMEM);

    for (i = 0; i < nb_channels; i++) {

        /* find the next bit that is set starting from the right */
        while ((outlink->channel_layout & 1ULL<<idx_bitposn) == 0 && idx_bitposn < 63)
            idx_bitposn++;

        /* channel weighting */
        if ((1ULL<<idx_bitposn & AV_CH_LOW_FREQUENCY) ||
            (1ULL<<idx_bitposn & AV_CH_LOW_FREQUENCY_2)) {
            ebur128->ch_weighting[i] = 0;
        } else if (1ULL<<idx_bitposn & BACK_MASK) {
            ebur128->ch_weighting[i] = 1.41;
        } else {
            ebur128->ch_weighting[i] = 1.0;
        }

        idx_bitposn++;

        if (!ebur128->ch_weighting[i])
            continue;

        /* bins buffer for the two integration window (400ms and 3s) */
        ebur128->i400.cache[i]  = av_calloc(I400_BINS,  sizeof(*ebur128->i400.cache[0]));
        ebur128->i3000.cache[i] = av_calloc(I3000_BINS, sizeof(*ebur128->i3000.cache[0]));
        if (!ebur128->i400.cache[i] || !ebur128->i3000.cache[i])
            return AVERROR(ENOMEM);
    }

    return 0;
}

#define ENERGY(loudness) (pow(10, ((loudness) + 0.691) / 10.))
#define LOUDNESS(energy) (-0.691 + 10 * log10(energy))

static struct hist_entry *get_histogram(void)
{
    int i;
    struct hist_entry *h = av_calloc(HIST_SIZE, sizeof(*h));

    for (i = 0; i < HIST_SIZE; i++) {
        h[i].loudness = i / (double)HIST_GRAIN + ABS_THRES;
        h[i].energy   = ENERGY(h[i].loudness);
    }
    return h;
}

static av_cold int init(AVFilterContext *ctx, const char *args)
{
    int ret;
    EBUR128Context *ebur128 = ctx->priv;
    AVFilterPad pad;

    ebur128->class = &ebur128_class;
    av_opt_set_defaults(ebur128);

    if ((ret = av_set_options_string(ebur128, args, "=", ":")) < 0)
        return ret;

    // if meter is  +9 scale, scale range is from -18 LU to  +9 LU (or 3*9)
    // if meter is +18 scale, scale range is from -36 LU to +18 LU (or 3*18)
    ebur128->scale_range = 3 * ebur128->meter;

    ebur128->i400.histogram  = get_histogram();
    ebur128->i3000.histogram = get_histogram();

    ebur128->integrated_loudness = ABS_THRES;
    ebur128->loudness_range = 0;

    /* insert output pads */
    if (ebur128->do_video) {
        pad = (AVFilterPad){
            .name         = av_strdup("out0"),
            .type         = AVMEDIA_TYPE_VIDEO,
            .config_props = config_video_output,
        };
        if (!pad.name)
            return AVERROR(ENOMEM);
        ff_insert_outpad(ctx, 0, &pad);
    }
    pad = (AVFilterPad){
        .name         = av_asprintf("out%d", ebur128->do_video),
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_audio_output,
    };
    if (!pad.name)
        return AVERROR(ENOMEM);
    ff_insert_outpad(ctx, ebur128->do_video, &pad);

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

static int filter_frame(AVFilterLink *inlink, AVFilterBufferRef *insamples)
{
    int i, ch, idx_insample;
    AVFilterContext *ctx = inlink->dst;
    EBUR128Context *ebur128 = ctx->priv;
    const int nb_channels = ebur128->nb_channels;
    const int nb_samples  = insamples->audio->nb_samples;
    const double *samples = (double *)insamples->data[0];
    AVFilterBufferRef *pic = ebur128->outpicref;

    for (idx_insample = 0; idx_insample < nb_samples; idx_insample++) {
        const int bin_id_400  = ebur128->i400.cache_pos;
        const int bin_id_3000 = ebur128->i3000.cache_pos;

#define MOVE_TO_NEXT_CACHED_ENTRY(time) do {                \
    ebur128->i##time.cache_pos++;                           \
    if (ebur128->i##time.cache_pos == I##time##_BINS) {     \
        ebur128->i##time.filled    = 1;                     \
        ebur128->i##time.cache_pos = 0;                     \
    }                                                       \
} while (0)

        MOVE_TO_NEXT_CACHED_ENTRY(400);
        MOVE_TO_NEXT_CACHED_ENTRY(3000);

        for (ch = 0; ch < nb_channels; ch++) {
            double bin;

            ebur128->x[ch * 3] = *samples++; // set X[i]

            if (!ebur128->ch_weighting[ch])
                continue;

            /* Y[i] = X[i]*b0 + X[i-1]*b1 + X[i-2]*b2 - Y[i-1]*a1 - Y[i-2]*a2 */
#define FILTER(Y, X, name) do {                                                 \
            double *dst = ebur128->Y + ch*3;                                    \
            double *src = ebur128->X + ch*3;                                    \
            dst[2] = dst[1];                                                    \
            dst[1] = dst[0];                                                    \
            dst[0] = src[0]*name##_B0 + src[1]*name##_B1 + src[2]*name##_B2     \
                                      - dst[1]*name##_A1 - dst[2]*name##_A2;    \
} while (0)

            // TODO: merge both filters in one?
            FILTER(y, x, PRE);  // apply pre-filter
            ebur128->x[ch * 3 + 2] = ebur128->x[ch * 3 + 1];
            ebur128->x[ch * 3 + 1] = ebur128->x[ch * 3    ];
            FILTER(z, y, RLB);  // apply RLB-filter

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
        if (++ebur128->sample_count == 4800) {
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
        power_##time /= I##time##_BINS;                                             \
    }                                                                               \
    loudness_##time = LOUDNESS(power_##time);                                       \
} while (0)

            COMPUTE_LOUDNESS(M,  400);
            COMPUTE_LOUDNESS(S, 3000);

            /* Integrated loudness */
#define I_GATE_THRES -10  // initially defined to -8 LU in the first EBU standard

            if (loudness_400 >= ABS_THRES) {
                double integrated_sum = 0;
                int nb_integrated = 0;
                int gate_hist_pos = gate_update(&ebur128->i400, power_400,
                                                loudness_400, I_GATE_THRES);

                /* compute integrated loudness by summing the histogram values
                 * above the relative threshold */
                for (i = gate_hist_pos; i < HIST_SIZE; i++) {
                    const int nb_v = ebur128->i400.histogram[i].count;
                    nb_integrated  += nb_v;
                    integrated_sum += nb_v * ebur128->i400.histogram[i].energy;
                }
                if (nb_integrated)
                    ebur128->integrated_loudness = LOUDNESS(integrated_sum / nb_integrated);
            }

            /* LRA */
#define LRA_GATE_THRES -20
#define LRA_LOWER_PRC   10
#define LRA_HIGHER_PRC  95

            /* XXX: example code in EBU 3342 is ">=" but formula in BS.1770
             * specs is ">" */
            if (loudness_3000 >= ABS_THRES) {
                int nb_powers = 0;
                int gate_hist_pos = gate_update(&ebur128->i3000, power_3000,
                                                loudness_3000, LRA_GATE_THRES);

                for (i = gate_hist_pos; i < HIST_SIZE; i++)
                    nb_powers += ebur128->i3000.histogram[i].count;
                if (nb_powers) {
                    int n, nb_pow;

                    /* get lower loudness to consider */
                    n = 0;
                    nb_pow = LRA_LOWER_PRC  * nb_powers / 100. + 0.5;
                    for (i = gate_hist_pos; i < HIST_SIZE; i++) {
                        n += ebur128->i3000.histogram[i].count;
                        if (n >= nb_pow) {
                            ebur128->lra_low = ebur128->i3000.histogram[i].loudness;
                            break;
                        }
                    }

                    /* get higher loudness to consider */
                    n = nb_powers;
                    nb_pow = LRA_HIGHER_PRC * nb_powers / 100. + 0.5;
                    for (i = HIST_SIZE - 1; i >= 0; i--) {
                        n -= ebur128->i3000.histogram[i].count;
                        if (n < nb_pow) {
                            ebur128->lra_high = ebur128->i3000.histogram[i].loudness;
                            break;
                        }
                    }

                    // XXX: show low & high on the graph?
                    ebur128->loudness_range = ebur128->lra_high - ebur128->lra_low;
                }
            }

#define LOG_FMT "M:%6.1f S:%6.1f     I:%6.1f LUFS     LRA:%6.1f LU"

            /* push one video frame */
            if (ebur128->do_video) {
                int x, y, ret;
                uint8_t *p;

                const int y_loudness_lu_graph = lu_to_y(ebur128, loudness_3000 + 23);
                const int y_loudness_lu_gauge = lu_to_y(ebur128, loudness_400  + 23);

                /* draw the graph using the short-term loudness */
                p = pic->data[0] + ebur128->graph.y*pic->linesize[0] + ebur128->graph.x*3;
                for (y = 0; y < ebur128->graph.h; y++) {
                    const uint8_t *c = get_graph_color(ebur128, y_loudness_lu_graph, y);

                    memmove(p, p + 3, (ebur128->graph.w - 1) * 3);
                    memcpy(p + (ebur128->graph.w - 1) * 3, c, 3);
                    p += pic->linesize[0];
                }

                /* draw the gauge using the momentary loudness */
                p = pic->data[0] + ebur128->gauge.y*pic->linesize[0] + ebur128->gauge.x*3;
                for (y = 0; y < ebur128->gauge.h; y++) {
                    const uint8_t *c = get_graph_color(ebur128, y_loudness_lu_gauge, y);

                    for (x = 0; x < ebur128->gauge.w; x++)
                        memcpy(p + x*3, c, 3);
                    p += pic->linesize[0];
                }

                /* draw textual info */
                drawtext(pic, PAD, PAD - PAD/2, FONT16, font_colors,
                         LOG_FMT "     ", // padding to erase trailing characters
                         loudness_400, loudness_3000,
                         ebur128->integrated_loudness, ebur128->loudness_range);

                /* set pts and push frame */
                pic->pts = pts;
                ret = ff_filter_frame(outlink, avfilter_ref_buffer(pic, ~AV_PERM_WRITE));
                if (ret < 0)
                    return ret;
            }

            av_log(ctx, ebur128->do_video ? AV_LOG_VERBOSE : AV_LOG_INFO,
                   "t: %-10s " LOG_FMT "\n", av_ts2timestr(pts, &outlink->time_base),
                   loudness_400, loudness_3000,
                   ebur128->integrated_loudness, ebur128->loudness_range);
        }
    }

    return ff_filter_frame(ctx->outputs[ebur128->do_video], insamples);
}

static int query_formats(AVFilterContext *ctx)
{
    EBUR128Context *ebur128 = ctx->priv;
    AVFilterFormats *formats;
    AVFilterChannelLayouts *layouts;
    AVFilterLink *inlink = ctx->inputs[0];
    AVFilterLink *outlink = ctx->outputs[0];

    static const enum AVSampleFormat sample_fmts[] = { AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_NONE };
    static const int input_srate[] = {48000, -1}; // ITU-R BS.1770 provides coeff only for 48kHz
    static const enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGB24, AV_PIX_FMT_NONE };

    /* set input audio formats */
    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &inlink->out_formats);

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_channel_layouts_ref(layouts, &inlink->out_channel_layouts);

    formats = ff_make_format_list(input_srate);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &inlink->out_samplerates);

    /* set optional output video format */
    if (ebur128->do_video) {
        formats = ff_make_format_list(pix_fmts);
        if (!formats)
            return AVERROR(ENOMEM);
        ff_formats_ref(formats, &outlink->in_formats);
        outlink = ctx->outputs[1];
    }

    /* set audio output formats (same as input since it's just a passthrough) */
    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &outlink->in_formats);

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_channel_layouts_ref(layouts, &outlink->in_channel_layouts);

    formats = ff_make_format_list(input_srate);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_formats_ref(formats, &outlink->in_samplerates);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    int i;
    EBUR128Context *ebur128 = ctx->priv;

    av_log(ctx, AV_LOG_INFO, "Summary:\n\n"
           "  Integrated loudness:\n"
           "    I:         %5.1f LUFS\n"
           "    Threshold: %5.1f LUFS\n\n"
           "  Loudness range:\n"
           "    LRA:       %5.1f LU\n"
           "    Threshold: %5.1f LUFS\n"
           "    LRA low:   %5.1f LUFS\n"
           "    LRA high:  %5.1f LUFS\n",
           ebur128->integrated_loudness, ebur128->i400.rel_threshold,
           ebur128->loudness_range,      ebur128->i3000.rel_threshold,
           ebur128->lra_low, ebur128->lra_high);

    av_freep(&ebur128->y_line_ref);
    av_freep(&ebur128->ch_weighting);
    av_freep(&ebur128->i400.histogram);
    av_freep(&ebur128->i3000.histogram);
    for (i = 0; i < ebur128->nb_channels; i++) {
        av_freep(&ebur128->i400.cache[i]);
        av_freep(&ebur128->i3000.cache[i]);
    }
    for (i = 0; i < ctx->nb_outputs; i++)
        av_freep(&ctx->output_pads[i].name);
    avfilter_unref_bufferp(&ebur128->outpicref);
}

static const AVFilterPad ebur128_inputs[] = {
    {
        .name             = "default",
        .type             = AVMEDIA_TYPE_AUDIO,
        .get_audio_buffer = ff_null_get_audio_buffer,
        .filter_frame     = filter_frame,
    },
    { NULL }
};

AVFilter avfilter_af_ebur128 = {
    .name          = "ebur128",
    .description   = NULL_IF_CONFIG_SMALL("EBU R128 scanner."),
    .priv_size     = sizeof(EBUR128Context),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = ebur128_inputs,
    .outputs       = NULL,
    .priv_class    = &ebur128_class,
};
