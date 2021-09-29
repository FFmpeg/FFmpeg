/*
 * Copyright (c) 2017 Richard Ling
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

/*
 * Normalize RGB video (aka histogram stretching, contrast stretching).
 * See: https://en.wikipedia.org/wiki/Normalization_(image_processing)
 *
 * For each channel of each frame, the filter computes the input range and maps
 * it linearly to the user-specified output range. The output range defaults
 * to the full dynamic range from pure black to pure white.
 *
 * Naively maximising the dynamic range of each frame of video in isolation
 * may cause flickering (rapid changes in brightness of static objects in the
 * scene) when small dark or bright objects enter or leave the scene. This
 * filter can apply temporal smoothing to the input range to reduce flickering.
 * Temporal smoothing is similar to the auto-exposure (automatic gain control)
 * on a video camera, which performs the same function; and, like a video
 * camera, it may cause a period of over- or under-exposure of the video.
 *
 * The filter can normalize the R,G,B channels independently, which may cause
 * color shifting, or link them together as a single channel, which prevents
 * color shifting. More precisely, linked normalization preserves hue (as it's
 * defined in HSV/HSL color spaces) while independent normalization does not.
 * Independent normalization can be used to remove color casts, such as the
 * blue cast from underwater video, restoring more natural colors. The filter
 * can also combine independent and linked normalization in any ratio.
 *
 * Finally the overall strength of the filter can be adjusted, from no effect
 * to full normalization.
 *
 * The 5 AVOptions are:
 *   blackpt,   Colors which define the output range. The minimum input value
 *   whitept    is mapped to the blackpt. The maximum input value is mapped to
 *              the whitept. The defaults are black and white respectively.
 *              Specifying white for blackpt and black for whitept will give
 *              color-inverted, normalized video. Shades of grey can be used
 *              to reduce the dynamic range (contrast). Specifying saturated
 *              colors here can create some interesting effects.
 *
 *   smoothing  The amount of temporal smoothing, expressed in frames (>=0).
 *              the minimum and maximum input values of each channel are
 *              smoothed using a rolling average over the current frame and
 *              that many previous frames of video.  Defaults to 0 (no temporal
 *              smoothing).
 *
 *   independence
 *              Controls the ratio of independent (color shifting) channel
 *              normalization to linked (color preserving) normalization. 0.0
 *              is fully linked, 1.0 is fully independent. Defaults to fully
 *              independent.
 *
 *   strength   Overall strength of the filter. 1.0 is full strength. 0.0 is
 *              a rather expensive no-op. Values in between can give a gentle
 *              boost to low-contrast video without creating an artificial
 *              over-processed look. The default is full strength.
 */

#include "libavutil/imgutils.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct NormalizeHistory {
    uint16_t *history;      // History entries.
    uint64_t history_sum;   // Sum of history entries.
} NormalizeHistory;

typedef struct NormalizeLocal {
    uint16_t in;    // Original input byte value for this frame.
    float smoothed; // Smoothed input value [0,255].
    float out;      // Output value [0,255]
} NormalizeLocal;

typedef struct NormalizeContext {
    const AVClass *class;

    // Storage for the corresponding AVOptions
    uint8_t blackpt[4];
    uint8_t whitept[4];
    int smoothing;
    float independence;
    float strength;

    uint8_t co[4];      // Offsets to R,G,B,A bytes respectively in each pixel
    int depth;
    int sblackpt[4];
    int swhitept[4];
    int num_components; // Number of components in the pixel format
    int step;
    int history_len;    // Number of frames to average; based on smoothing factor
    int frame_num;      // Increments on each frame, starting from 0.

    // Per-extremum, per-channel history, for temporal smoothing.
    NormalizeHistory min[3], max[3];           // Min and max for each channel in {R,G,B}.
    uint16_t *history_mem;       // Single allocation for above history entries

    uint16_t lut[3][65536];    // Lookup table

    void (*find_min_max)(struct NormalizeContext *s, AVFrame *in, NormalizeLocal min[3], NormalizeLocal max[3]);
    void (*process)(struct NormalizeContext *s, AVFrame *in, AVFrame *out);
} NormalizeContext;

#define OFFSET(x) offsetof(NormalizeContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM
#define FLAGSR AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption normalize_options[] = {
    { "blackpt",  "output color to which darkest input color is mapped",  OFFSET(blackpt), AV_OPT_TYPE_COLOR, { .str = "black" }, 0, 0, FLAGSR },
    { "whitept",  "output color to which brightest input color is mapped",  OFFSET(whitept), AV_OPT_TYPE_COLOR, { .str = "white" }, 0, 0, FLAGSR },
    { "smoothing",  "amount of temporal smoothing of the input range, to reduce flicker", OFFSET(smoothing), AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX/8, FLAGS },
    { "independence", "proportion of independent to linked channel normalization", OFFSET(independence), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, 1.0, FLAGSR },
    { "strength", "strength of filter, from no effect to full normalization", OFFSET(strength), AV_OPT_TYPE_FLOAT, {.dbl=1.0}, 0.0, 1.0, FLAGSR },
    { NULL }
};

AVFILTER_DEFINE_CLASS(normalize);

static void find_min_max(NormalizeContext *s, AVFrame *in, NormalizeLocal min[3], NormalizeLocal max[3])
{
    for (int c = 0; c < 3; c++)
        min[c].in = max[c].in = in->data[0][s->co[c]];
    for (int y = 0; y < in->height; y++) {
        uint8_t *inp = in->data[0] + y * in->linesize[0];
        for (int x = 0; x < in->width; x++) {
            for (int c = 0; c < 3; c++) {
                min[c].in = FFMIN(min[c].in, inp[s->co[c]]);
                max[c].in = FFMAX(max[c].in, inp[s->co[c]]);
            }
            inp += s->step;
        }
    }
}

static void process(NormalizeContext *s, AVFrame *in, AVFrame *out)
{
    for (int y = 0; y < in->height; y++) {
        uint8_t *inp = in->data[0] + y * in->linesize[0];
        uint8_t *outp = out->data[0] + y * out->linesize[0];
        for (int x = 0; x < in->width; x++) {
            for (int c = 0; c < 3; c++)
                outp[s->co[c]] = s->lut[c][inp[s->co[c]]];
            if (s->num_components == 4)
                // Copy alpha as-is.
                outp[s->co[3]] = inp[s->co[3]];
            inp += s->step;
            outp += s->step;
        }
    }
}

static void find_min_max_planar(NormalizeContext *s, AVFrame *in, NormalizeLocal min[3], NormalizeLocal max[3])
{
    min[0].in = max[0].in = in->data[2][0];
    min[1].in = max[1].in = in->data[0][0];
    min[2].in = max[2].in = in->data[1][0];
    for (int y = 0; y < in->height; y++) {
        uint8_t *inrp = in->data[2] + y * in->linesize[2];
        uint8_t *ingp = in->data[0] + y * in->linesize[0];
        uint8_t *inbp = in->data[1] + y * in->linesize[1];
        for (int x = 0; x < in->width; x++) {
            min[0].in = FFMIN(min[0].in, inrp[x]);
            max[0].in = FFMAX(max[0].in, inrp[x]);
            min[1].in = FFMIN(min[1].in, ingp[x]);
            max[1].in = FFMAX(max[1].in, ingp[x]);
            min[2].in = FFMIN(min[2].in, inbp[x]);
            max[2].in = FFMAX(max[2].in, inbp[x]);
        }
    }
}

static void process_planar(NormalizeContext *s, AVFrame *in, AVFrame *out)
{
    for (int y = 0; y < in->height; y++) {
        uint8_t *inrp = in->data[2] + y * in->linesize[2];
        uint8_t *ingp = in->data[0] + y * in->linesize[0];
        uint8_t *inbp = in->data[1] + y * in->linesize[1];
        uint8_t *inap = in->data[3] + y * in->linesize[3];
        uint8_t *outrp = out->data[2] + y * out->linesize[2];
        uint8_t *outgp = out->data[0] + y * out->linesize[0];
        uint8_t *outbp = out->data[1] + y * out->linesize[1];
        uint8_t *outap = out->data[3] + y * out->linesize[3];
        for (int x = 0; x < in->width; x++) {
            outrp[x] = s->lut[0][inrp[x]];
            outgp[x] = s->lut[1][ingp[x]];
            outbp[x] = s->lut[2][inbp[x]];
            if (s->num_components == 4)
                outap[x] = inap[x];
        }
    }
}

static void find_min_max_16(NormalizeContext *s, AVFrame *in, NormalizeLocal min[3], NormalizeLocal max[3])
{
    for (int c = 0; c < 3; c++)
        min[c].in = max[c].in = AV_RN16(in->data[0] + 2 * s->co[c]);
    for (int y = 0; y < in->height; y++) {
        uint16_t *inp = (uint16_t *)(in->data[0] + y * in->linesize[0]);
        for (int x = 0; x < in->width; x++) {
            for (int c = 0; c < 3; c++) {
                min[c].in = FFMIN(min[c].in, inp[s->co[c]]);
                max[c].in = FFMAX(max[c].in, inp[s->co[c]]);
            }
            inp += s->step;
        }
    }
}

static void process_16(NormalizeContext *s, AVFrame *in, AVFrame *out)
{
    for (int y = 0; y < in->height; y++) {
        uint16_t *inp  = (uint16_t *)(in->data[0] + y * in->linesize[0]);
        uint16_t *outp = (uint16_t *)(out->data[0] + y * out->linesize[0]);
        for (int x = 0; x < in->width; x++) {
            for (int c = 0; c < 3; c++)
                outp[s->co[c]] = s->lut[c][inp[s->co[c]]];
            if (s->num_components == 4)
                // Copy alpha as-is.
                outp[s->co[3]] = inp[s->co[3]];
            inp += s->step;
            outp += s->step;
        }
    }
}

static void find_min_max_planar_16(NormalizeContext *s, AVFrame *in, NormalizeLocal min[3], NormalizeLocal max[3])
{
    min[0].in = max[0].in = AV_RN16(in->data[2]);
    min[1].in = max[1].in = AV_RN16(in->data[0]);
    min[2].in = max[2].in = AV_RN16(in->data[1]);
    for (int y = 0; y < in->height; y++) {
        uint16_t *inrp = (uint16_t *)(in->data[2] + y * in->linesize[2]);
        uint16_t *ingp = (uint16_t *)(in->data[0] + y * in->linesize[0]);
        uint16_t *inbp = (uint16_t *)(in->data[1] + y * in->linesize[1]);
        for (int x = 0; x < in->width; x++) {
            min[0].in = FFMIN(min[0].in, inrp[x]);
            max[0].in = FFMAX(max[0].in, inrp[x]);
            min[1].in = FFMIN(min[1].in, ingp[x]);
            max[1].in = FFMAX(max[1].in, ingp[x]);
            min[2].in = FFMIN(min[2].in, inbp[x]);
            max[2].in = FFMAX(max[2].in, inbp[x]);
        }
    }
}

static void process_planar_16(NormalizeContext *s, AVFrame *in, AVFrame *out)
{
    for (int y = 0; y < in->height; y++) {
        uint16_t *inrp  = (uint16_t *)(in->data[2] + y * in->linesize[2]);
        uint16_t *ingp  = (uint16_t *)(in->data[0] + y * in->linesize[0]);
        uint16_t *inbp  = (uint16_t *)(in->data[1] + y * in->linesize[1]);
        uint16_t *inap  = (uint16_t *)(in->data[3] + y * in->linesize[3]);
        uint16_t *outrp = (uint16_t *)(out->data[2] + y * out->linesize[2]);
        uint16_t *outgp = (uint16_t *)(out->data[0] + y * out->linesize[0]);
        uint16_t *outbp = (uint16_t *)(out->data[1] + y * out->linesize[1]);
        uint16_t *outap = (uint16_t *)(out->data[3] + y * out->linesize[3]);
        for (int x = 0; x < in->width; x++) {
            outrp[x] = s->lut[0][inrp[x]];
            outgp[x] = s->lut[1][ingp[x]];
            outbp[x] = s->lut[2][inbp[x]];
            if (s->num_components == 4)
                outap[x] = inap[x];
        }
    }
}

// This function is the main guts of the filter. Normalizes the input frame
// into the output frame. The frames are known to have the same dimensions
// and pixel format.
static void normalize(NormalizeContext *s, AVFrame *in, AVFrame *out)
{
    // Per-extremum, per-channel local variables.
    NormalizeLocal min[3], max[3];   // Min and max for each channel in {R,G,B}.

    float rgb_min_smoothed; // Min input range for linked normalization
    float rgb_max_smoothed; // Max input range for linked normalization
    int c;

    // First, scan the input frame to find, for each channel, the minimum
    // (min.in) and maximum (max.in) values present in the channel.
    s->find_min_max(s, in, min, max);

    // Next, for each channel, push min.in and max.in into their respective
    // histories, to determine the min.smoothed and max.smoothed for this frame.
    {
        int history_idx = s->frame_num % s->history_len;
        // Assume the history is not yet full; num_history_vals is the number
        // of frames received so far including the current frame.
        int num_history_vals = s->frame_num + 1;
        if (s->frame_num >= s->history_len) {
            //The history is full; drop oldest value and cap num_history_vals.
            for (c = 0; c < 3; c++) {
                s->min[c].history_sum -= s->min[c].history[history_idx];
                s->max[c].history_sum -= s->max[c].history[history_idx];
            }
            num_history_vals = s->history_len;
        }
        // For each extremum, update history_sum and calculate smoothed value
        // as the rolling average of the history entries.
        for (c = 0; c < 3; c++) {
            s->min[c].history_sum += (s->min[c].history[history_idx] = min[c].in);
            min[c].smoothed = s->min[c].history_sum / (float)num_history_vals;
            s->max[c].history_sum += (s->max[c].history[history_idx] = max[c].in);
            max[c].smoothed = s->max[c].history_sum / (float)num_history_vals;
        }
    }

    // Determine the input range for linked normalization. This is simply the
    // minimum of the per-channel minimums, and the maximum of the per-channel
    // maximums.
    rgb_min_smoothed = FFMIN3(min[0].smoothed, min[1].smoothed, min[2].smoothed);
    rgb_max_smoothed = FFMAX3(max[0].smoothed, max[1].smoothed, max[2].smoothed);

    // Now, process each channel to determine the input and output range and
    // build the lookup tables.
    for (c = 0; c < 3; c++) {
        int in_val;
        // Adjust the input range for this channel [min.smoothed,max.smoothed]
        // by mixing in the correct proportion of the linked normalization
        // input range [rgb_min_smoothed,rgb_max_smoothed].
        min[c].smoothed = (min[c].smoothed  *         s->independence)
                        + (rgb_min_smoothed * (1.0f - s->independence));
        max[c].smoothed = (max[c].smoothed  *         s->independence)
                        + (rgb_max_smoothed * (1.0f - s->independence));

        // Calculate the output range [min.out,max.out] as a ratio of the full-
        // strength output range [blackpt,whitept] and the original input range
        // [min.in,max.in], based on the user-specified filter strength.
        min[c].out = (s->sblackpt[c] *        s->strength)
                   + (min[c].in     * (1.0f - s->strength));
        max[c].out = (s->swhitept[c] *        s->strength)
                   + (max[c].in     * (1.0f - s->strength));

        // Now, build a lookup table which linearly maps the adjusted input range
        // [min.smoothed,max.smoothed] to the output range [min.out,max.out].
        // Perform the linear interpolation for each x:
        //     lut[x] = (int)(float(x - min.smoothed) * scale + max.out + 0.5)
        // where scale = (max.out - min.out) / (max.smoothed - min.smoothed)
        if (min[c].smoothed == max[c].smoothed) {
            // There is no dynamic range to expand. No mapping for this channel.
            for (in_val = min[c].in; in_val <= max[c].in; in_val++)
                s->lut[c][in_val] = min[c].out;
        } else {
            // We must set lookup values for all values in the original input
            // range [min.in,max.in]. Since the original input range may be
            // larger than [min.smoothed,max.smoothed], some output values may
            // fall outside the [0,255] dynamic range. We need to clamp them.
            float scale = (max[c].out - min[c].out) / (max[c].smoothed - min[c].smoothed);
            for (in_val = min[c].in; in_val <= max[c].in; in_val++) {
                int out_val = (in_val - min[c].smoothed) * scale + min[c].out + 0.5f;
                out_val = av_clip_uintp2_c(out_val, s->depth);
                s->lut[c][in_val] = out_val;
            }
        }
    }

    // Finally, process the pixels of the input frame using the lookup tables.
    s->process(s, in, out);

    s->frame_num++;
}

// Now we define all the functions accessible from the ff_vf_normalize class,
// which is ffmpeg's interface to our filter.  See doc/filter_design.txt and
// doc/writing_filters.txt for descriptions of what these interface functions
// are expected to do.

// The pixel formats that our filter supports. We should be able to process
// any 8-bit RGB formats. 16-bit support might be useful one day.
static const enum AVPixelFormat pixel_fmts[] = {
    AV_PIX_FMT_RGB24,
    AV_PIX_FMT_BGR24,
    AV_PIX_FMT_ARGB,
    AV_PIX_FMT_RGBA,
    AV_PIX_FMT_ABGR,
    AV_PIX_FMT_BGRA,
    AV_PIX_FMT_0RGB,
    AV_PIX_FMT_RGB0,
    AV_PIX_FMT_0BGR,
    AV_PIX_FMT_BGR0,
    AV_PIX_FMT_RGB48,  AV_PIX_FMT_BGR48,
    AV_PIX_FMT_RGBA64, AV_PIX_FMT_BGRA64,
    AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
    AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
    AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
    AV_PIX_FMT_NONE
};

// At this point we know the pixel format used for both input and output.  We
// can also access the frame rate of the input video and allocate some memory
// appropriately
static int config_input(AVFilterLink *inlink)
{
    NormalizeContext *s = inlink->dst->priv;
    // Store offsets to R,G,B,A bytes respectively in each pixel
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int c, planar, scale;

    ff_fill_rgba_map(s->co, inlink->format);
    s->depth = desc->comp[0].depth;
    scale = 1 << (s->depth - 8);
    s->num_components = desc->nb_components;
    s->step = av_get_padded_bits_per_pixel(desc) >> (3 + (s->depth > 8));
    // Convert smoothing value to history_len (a count of frames to average,
    // must be at least 1).  Currently this is a direct assignment, but the
    // smoothing value was originally envisaged as a number of seconds.  In
    // future it would be nice to set history_len using a number of seconds,
    // but VFR video is currently an obstacle to doing so.
    s->history_len = s->smoothing + 1;
    // Allocate the history buffers -- there are 6 -- one for each extrema.
    // s->smoothing is limited to INT_MAX/8, so that (s->history_len * 6)
    // can't overflow on 32bit causing a too-small allocation.
    s->history_mem = av_malloc(s->history_len * 6 * sizeof(*s->history_mem));
    if (s->history_mem == NULL)
        return AVERROR(ENOMEM);

    for (c = 0; c < 3; c++) {
        s->min[c].history = s->history_mem + (c*2)   * s->history_len;
        s->max[c].history = s->history_mem + (c*2+1) * s->history_len;
        s->sblackpt[c] = scale * s->blackpt[c] + (s->blackpt[c] & (1 << (s->depth - 8)));
        s->swhitept[c] = scale * s->whitept[c] + (s->whitept[c] & (1 << (s->depth - 8)));
    }

    planar = desc->flags & AV_PIX_FMT_FLAG_PLANAR;

    if (s->depth <= 8) {
        s->find_min_max = planar ? find_min_max_planar : find_min_max;
        s->process = planar? process_planar : process;
    } else {
        s->find_min_max = planar ? find_min_max_planar_16 : find_min_max_16;
        s->process = planar? process_planar_16 : process_16;
    }

    return 0;
}

// Free any memory allocations here
static av_cold void uninit(AVFilterContext *ctx)
{
    NormalizeContext *s = ctx->priv;

    av_freep(&s->history_mem);
}

// This function is pretty much standard from doc/writing_filters.txt.  It
// tries to do in-place filtering where possible, only allocating a new output
// frame when absolutely necessary.
static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    NormalizeContext *s = ctx->priv;
    AVFrame *out;
    // Set 'direct' if we can modify the input frame in-place.  Otherwise we
    // need to retrieve a new frame from the output link.
    int direct = av_frame_is_writable(in) && !ctx->is_disabled;

    if (direct) {
        out = in;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    // Now we've got the input and output frames (which may be the same frame)
    // perform the filtering with our custom function.
    normalize(s, in, out);

    if (ctx->is_disabled) {
        av_frame_free(&out);
        return ff_filter_frame(outlink, in);
    }

    if (!direct)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
};

const AVFilter ff_vf_normalize = {
    .name          = "normalize",
    .description   = NULL_IF_CONFIG_SMALL("Normalize RGB video."),
    .priv_size     = sizeof(NormalizeContext),
    .priv_class    = &normalize_class,
    .uninit        = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_PIXFMTS_ARRAY(pixel_fmts),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
    .process_command = ff_filter_process_command,
};
