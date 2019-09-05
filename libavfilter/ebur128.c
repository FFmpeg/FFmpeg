/*
 * Copyright (c) 2011 Jan Kokemüller
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
 *
 * This file is based on libebur128 which is available at
 * https://github.com/jiixyj/libebur128/
 *
 * Libebur128 has the following copyright:
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
*/

#include "ebur128.h"

#include <float.h>
#include <limits.h>
#include <math.h>               /* You may have to define _USE_MATH_DEFINES if you use MSVC */

#include "libavutil/common.h"
#include "libavutil/mem.h"
#include "libavutil/thread.h"

#define CHECK_ERROR(condition, errorcode, goto_point)                          \
    if ((condition)) {                                                         \
        errcode = (errorcode);                                                 \
        goto goto_point;                                                       \
    }

#define ALMOST_ZERO 0.000001

#define RELATIVE_GATE         (-10.0)
#define RELATIVE_GATE_FACTOR  pow(10.0, RELATIVE_GATE / 10.0)
#define MINUS_20DB            pow(10.0, -20.0 / 10.0)

struct FFEBUR128StateInternal {
    /** Filtered audio data (used as ring buffer). */
    double *audio_data;
    /** Size of audio_data array. */
    size_t audio_data_frames;
    /** Current index for audio_data. */
    size_t audio_data_index;
    /** How many frames are needed for a gating block. Will correspond to 400ms
     *  of audio at initialization, and 100ms after the first block (75% overlap
     *  as specified in the 2011 revision of BS1770). */
    unsigned long needed_frames;
    /** The channel map. Has as many elements as there are channels. */
    int *channel_map;
    /** How many samples fit in 100ms (rounded). */
    unsigned long samples_in_100ms;
    /** BS.1770 filter coefficients (nominator). */
    double b[5];
    /** BS.1770 filter coefficients (denominator). */
    double a[5];
    /** BS.1770 filter state. */
    double v[5][5];
    /** Histograms, used to calculate LRA. */
    unsigned long *block_energy_histogram;
    unsigned long *short_term_block_energy_histogram;
    /** Keeps track of when a new short term block is needed. */
    size_t short_term_frame_counter;
    /** Maximum sample peak, one per channel */
    double *sample_peak;
    /** The maximum window duration in ms. */
    unsigned long window;
    /** Data pointer array for interleaved data */
    void **data_ptrs;
};

static AVOnce histogram_init = AV_ONCE_INIT;
static DECLARE_ALIGNED(32, double, histogram_energies)[1000];
static DECLARE_ALIGNED(32, double, histogram_energy_boundaries)[1001];

static void ebur128_init_filter(FFEBUR128State * st)
{
    int i, j;

    double f0 = 1681.974450955533;
    double G = 3.999843853973347;
    double Q = 0.7071752369554196;

    double K = tan(M_PI * f0 / (double) st->samplerate);
    double Vh = pow(10.0, G / 20.0);
    double Vb = pow(Vh, 0.4996667741545416);

    double pb[3] = { 0.0, 0.0, 0.0 };
    double pa[3] = { 1.0, 0.0, 0.0 };
    double rb[3] = { 1.0, -2.0, 1.0 };
    double ra[3] = { 1.0, 0.0, 0.0 };

    double a0 = 1.0 + K / Q + K * K;
    pb[0] = (Vh + Vb * K / Q + K * K) / a0;
    pb[1] = 2.0 * (K * K - Vh) / a0;
    pb[2] = (Vh - Vb * K / Q + K * K) / a0;
    pa[1] = 2.0 * (K * K - 1.0) / a0;
    pa[2] = (1.0 - K / Q + K * K) / a0;

    f0 = 38.13547087602444;
    Q = 0.5003270373238773;
    K = tan(M_PI * f0 / (double) st->samplerate);

    ra[1] = 2.0 * (K * K - 1.0) / (1.0 + K / Q + K * K);
    ra[2] = (1.0 - K / Q + K * K) / (1.0 + K / Q + K * K);

    st->d->b[0] = pb[0] * rb[0];
    st->d->b[1] = pb[0] * rb[1] + pb[1] * rb[0];
    st->d->b[2] = pb[0] * rb[2] + pb[1] * rb[1] + pb[2] * rb[0];
    st->d->b[3] = pb[1] * rb[2] + pb[2] * rb[1];
    st->d->b[4] = pb[2] * rb[2];

    st->d->a[0] = pa[0] * ra[0];
    st->d->a[1] = pa[0] * ra[1] + pa[1] * ra[0];
    st->d->a[2] = pa[0] * ra[2] + pa[1] * ra[1] + pa[2] * ra[0];
    st->d->a[3] = pa[1] * ra[2] + pa[2] * ra[1];
    st->d->a[4] = pa[2] * ra[2];

    for (i = 0; i < 5; ++i) {
        for (j = 0; j < 5; ++j) {
            st->d->v[i][j] = 0.0;
        }
    }
}

static int ebur128_init_channel_map(FFEBUR128State * st)
{
    size_t i;
    st->d->channel_map =
        (int *) av_malloc_array(st->channels, sizeof(int));
    if (!st->d->channel_map)
        return AVERROR(ENOMEM);
    if (st->channels == 4) {
        st->d->channel_map[0] = FF_EBUR128_LEFT;
        st->d->channel_map[1] = FF_EBUR128_RIGHT;
        st->d->channel_map[2] = FF_EBUR128_LEFT_SURROUND;
        st->d->channel_map[3] = FF_EBUR128_RIGHT_SURROUND;
    } else if (st->channels == 5) {
        st->d->channel_map[0] = FF_EBUR128_LEFT;
        st->d->channel_map[1] = FF_EBUR128_RIGHT;
        st->d->channel_map[2] = FF_EBUR128_CENTER;
        st->d->channel_map[3] = FF_EBUR128_LEFT_SURROUND;
        st->d->channel_map[4] = FF_EBUR128_RIGHT_SURROUND;
    } else {
        for (i = 0; i < st->channels; ++i) {
            switch (i) {
            case 0:
                st->d->channel_map[i] = FF_EBUR128_LEFT;
                break;
            case 1:
                st->d->channel_map[i] = FF_EBUR128_RIGHT;
                break;
            case 2:
                st->d->channel_map[i] = FF_EBUR128_CENTER;
                break;
            case 3:
                st->d->channel_map[i] = FF_EBUR128_UNUSED;
                break;
            case 4:
                st->d->channel_map[i] = FF_EBUR128_LEFT_SURROUND;
                break;
            case 5:
                st->d->channel_map[i] = FF_EBUR128_RIGHT_SURROUND;
                break;
            default:
                st->d->channel_map[i] = FF_EBUR128_UNUSED;
                break;
            }
        }
    }
    return 0;
}

static inline void init_histogram(void)
{
    int i;
    /* initialize static constants */
    histogram_energy_boundaries[0] = pow(10.0, (-70.0 + 0.691) / 10.0);
    for (i = 0; i < 1000; ++i) {
        histogram_energies[i] =
            pow(10.0, ((double) i / 10.0 - 69.95 + 0.691) / 10.0);
    }
    for (i = 1; i < 1001; ++i) {
        histogram_energy_boundaries[i] =
            pow(10.0, ((double) i / 10.0 - 70.0 + 0.691) / 10.0);
    }
}

FFEBUR128State *ff_ebur128_init(unsigned int channels,
                                unsigned long samplerate,
                                unsigned long window, int mode)
{
    int errcode;
    FFEBUR128State *st;

    st = (FFEBUR128State *) av_malloc(sizeof(FFEBUR128State));
    CHECK_ERROR(!st, 0, exit)
    st->d = (struct FFEBUR128StateInternal *)
        av_malloc(sizeof(struct FFEBUR128StateInternal));
    CHECK_ERROR(!st->d, 0, free_state)
    st->channels = channels;
    errcode = ebur128_init_channel_map(st);
    CHECK_ERROR(errcode, 0, free_internal)

    st->d->sample_peak =
        (double *) av_mallocz_array(channels, sizeof(double));
    CHECK_ERROR(!st->d->sample_peak, 0, free_channel_map)

    st->samplerate = samplerate;
    st->d->samples_in_100ms = (st->samplerate + 5) / 10;
    st->mode = mode;
    if ((mode & FF_EBUR128_MODE_S) == FF_EBUR128_MODE_S) {
        st->d->window = FFMAX(window, 3000);
    } else if ((mode & FF_EBUR128_MODE_M) == FF_EBUR128_MODE_M) {
        st->d->window = FFMAX(window, 400);
    } else {
        goto free_sample_peak;
    }
    st->d->audio_data_frames = st->samplerate * st->d->window / 1000;
    if (st->d->audio_data_frames % st->d->samples_in_100ms) {
        /* round up to multiple of samples_in_100ms */
        st->d->audio_data_frames = st->d->audio_data_frames
            + st->d->samples_in_100ms
            - (st->d->audio_data_frames % st->d->samples_in_100ms);
    }
    st->d->audio_data =
        (double *) av_mallocz_array(st->d->audio_data_frames,
                                    st->channels * sizeof(double));
    CHECK_ERROR(!st->d->audio_data, 0, free_sample_peak)

    ebur128_init_filter(st);

    st->d->block_energy_histogram =
        av_mallocz(1000 * sizeof(unsigned long));
    CHECK_ERROR(!st->d->block_energy_histogram, 0, free_audio_data)
    st->d->short_term_block_energy_histogram =
        av_mallocz(1000 * sizeof(unsigned long));
    CHECK_ERROR(!st->d->short_term_block_energy_histogram, 0,
                free_block_energy_histogram)
    st->d->short_term_frame_counter = 0;

    /* the first block needs 400ms of audio data */
    st->d->needed_frames = st->d->samples_in_100ms * 4;
    /* start at the beginning of the buffer */
    st->d->audio_data_index = 0;

    if (ff_thread_once(&histogram_init, &init_histogram) != 0)
        goto free_short_term_block_energy_histogram;

    st->d->data_ptrs = av_malloc_array(channels, sizeof(void *));
    CHECK_ERROR(!st->d->data_ptrs, 0,
                free_short_term_block_energy_histogram);

    return st;

free_short_term_block_energy_histogram:
    av_free(st->d->short_term_block_energy_histogram);
free_block_energy_histogram:
    av_free(st->d->block_energy_histogram);
free_audio_data:
    av_free(st->d->audio_data);
free_sample_peak:
    av_free(st->d->sample_peak);
free_channel_map:
    av_free(st->d->channel_map);
free_internal:
    av_free(st->d);
free_state:
    av_free(st);
exit:
    return NULL;
}

void ff_ebur128_destroy(FFEBUR128State ** st)
{
    av_free((*st)->d->block_energy_histogram);
    av_free((*st)->d->short_term_block_energy_histogram);
    av_free((*st)->d->audio_data);
    av_free((*st)->d->channel_map);
    av_free((*st)->d->sample_peak);
    av_free((*st)->d->data_ptrs);
    av_free((*st)->d);
    av_free(*st);
    *st = NULL;
}

#define EBUR128_FILTER(type, scaling_factor)                                       \
static void ebur128_filter_##type(FFEBUR128State* st, const type** srcs,           \
                                  size_t src_index, size_t frames,                 \
                                  int stride) {                                    \
    double* audio_data = st->d->audio_data + st->d->audio_data_index;              \
    size_t i, c;                                                                   \
                                                                                   \
    if ((st->mode & FF_EBUR128_MODE_SAMPLE_PEAK) == FF_EBUR128_MODE_SAMPLE_PEAK) { \
        for (c = 0; c < st->channels; ++c) {                                       \
            double max = 0.0;                                                      \
            for (i = 0; i < frames; ++i) {                                         \
                type v = srcs[c][src_index + i * stride];                          \
                if (v > max) {                                                     \
                    max =        v;                                                \
                } else if (-v > max) {                                             \
                    max = -1.0 * v;                                                \
                }                                                                  \
            }                                                                      \
            max /= scaling_factor;                                                 \
            if (max > st->d->sample_peak[c]) st->d->sample_peak[c] = max;          \
        }                                                                          \
    }                                                                              \
    for (c = 0; c < st->channels; ++c) {                                           \
        int ci = st->d->channel_map[c] - 1;                                        \
        if (ci < 0) continue;                                                      \
        else if (ci == FF_EBUR128_DUAL_MONO - 1) ci = 0; /*dual mono */            \
        for (i = 0; i < frames; ++i) {                                             \
            st->d->v[ci][0] = (double) (srcs[c][src_index + i * stride] / scaling_factor) \
                         - st->d->a[1] * st->d->v[ci][1]                           \
                         - st->d->a[2] * st->d->v[ci][2]                           \
                         - st->d->a[3] * st->d->v[ci][3]                           \
                         - st->d->a[4] * st->d->v[ci][4];                          \
            audio_data[i * st->channels + c] =                                     \
                           st->d->b[0] * st->d->v[ci][0]                           \
                         + st->d->b[1] * st->d->v[ci][1]                           \
                         + st->d->b[2] * st->d->v[ci][2]                           \
                         + st->d->b[3] * st->d->v[ci][3]                           \
                         + st->d->b[4] * st->d->v[ci][4];                          \
            st->d->v[ci][4] = st->d->v[ci][3];                                     \
            st->d->v[ci][3] = st->d->v[ci][2];                                     \
            st->d->v[ci][2] = st->d->v[ci][1];                                     \
            st->d->v[ci][1] = st->d->v[ci][0];                                     \
        }                                                                          \
        st->d->v[ci][4] = fabs(st->d->v[ci][4]) < DBL_MIN ? 0.0 : st->d->v[ci][4]; \
        st->d->v[ci][3] = fabs(st->d->v[ci][3]) < DBL_MIN ? 0.0 : st->d->v[ci][3]; \
        st->d->v[ci][2] = fabs(st->d->v[ci][2]) < DBL_MIN ? 0.0 : st->d->v[ci][2]; \
        st->d->v[ci][1] = fabs(st->d->v[ci][1]) < DBL_MIN ? 0.0 : st->d->v[ci][1]; \
    }                                                                              \
}
EBUR128_FILTER(short, -((double)SHRT_MIN))
EBUR128_FILTER(int, -((double)INT_MIN))
EBUR128_FILTER(float,  1.0)
EBUR128_FILTER(double, 1.0)

static double ebur128_energy_to_loudness(double energy)
{
    return 10 * log10(energy) - 0.691;
}

static size_t find_histogram_index(double energy)
{
    size_t index_min = 0;
    size_t index_max = 1000;
    size_t index_mid;

    do {
        index_mid = (index_min + index_max) / 2;
        if (energy >= histogram_energy_boundaries[index_mid]) {
            index_min = index_mid;
        } else {
            index_max = index_mid;
        }
    } while (index_max - index_min != 1);

    return index_min;
}

static void ebur128_calc_gating_block(FFEBUR128State * st,
                                      size_t frames_per_block,
                                      double *optional_output)
{
    size_t i, c;
    double sum = 0.0;
    double channel_sum;
    for (c = 0; c < st->channels; ++c) {
        if (st->d->channel_map[c] == FF_EBUR128_UNUSED)
            continue;
        channel_sum = 0.0;
        if (st->d->audio_data_index < frames_per_block * st->channels) {
            for (i = 0; i < st->d->audio_data_index / st->channels; ++i) {
                channel_sum += st->d->audio_data[i * st->channels + c] *
                    st->d->audio_data[i * st->channels + c];
            }
            for (i = st->d->audio_data_frames -
                 (frames_per_block -
                  st->d->audio_data_index / st->channels);
                 i < st->d->audio_data_frames; ++i) {
                channel_sum += st->d->audio_data[i * st->channels + c] *
                    st->d->audio_data[i * st->channels + c];
            }
        } else {
            for (i =
                 st->d->audio_data_index / st->channels - frames_per_block;
                 i < st->d->audio_data_index / st->channels; ++i) {
                channel_sum +=
                    st->d->audio_data[i * st->channels +
                                      c] * st->d->audio_data[i *
                                                             st->channels +
                                                             c];
            }
        }
        if (st->d->channel_map[c] == FF_EBUR128_Mp110 ||
            st->d->channel_map[c] == FF_EBUR128_Mm110 ||
            st->d->channel_map[c] == FF_EBUR128_Mp060 ||
            st->d->channel_map[c] == FF_EBUR128_Mm060 ||
            st->d->channel_map[c] == FF_EBUR128_Mp090 ||
            st->d->channel_map[c] == FF_EBUR128_Mm090) {
            channel_sum *= 1.41;
        } else if (st->d->channel_map[c] == FF_EBUR128_DUAL_MONO) {
            channel_sum *= 2.0;
        }
        sum += channel_sum;
    }
    sum /= (double) frames_per_block;
    if (optional_output) {
        *optional_output = sum;
    } else if (sum >= histogram_energy_boundaries[0]) {
        ++st->d->block_energy_histogram[find_histogram_index(sum)];
    }
}

int ff_ebur128_set_channel(FFEBUR128State * st,
                           unsigned int channel_number, int value)
{
    if (channel_number >= st->channels) {
        return 1;
    }
    if (value == FF_EBUR128_DUAL_MONO &&
        (st->channels != 1 || channel_number != 0)) {
        return 1;
    }
    st->d->channel_map[channel_number] = value;
    return 0;
}

static int ebur128_energy_shortterm(FFEBUR128State * st, double *out);
#define FF_EBUR128_ADD_FRAMES_PLANAR(type)                                             \
void ff_ebur128_add_frames_planar_##type(FFEBUR128State* st, const type** srcs,        \
                                 size_t frames, int stride) {                          \
    size_t src_index = 0;                                                              \
    while (frames > 0) {                                                               \
        if (frames >= st->d->needed_frames) {                                          \
            ebur128_filter_##type(st, srcs, src_index, st->d->needed_frames, stride);  \
            src_index += st->d->needed_frames * stride;                                \
            frames -= st->d->needed_frames;                                            \
            st->d->audio_data_index += st->d->needed_frames * st->channels;            \
            /* calculate the new gating block */                                       \
            if ((st->mode & FF_EBUR128_MODE_I) == FF_EBUR128_MODE_I) {                 \
                ebur128_calc_gating_block(st, st->d->samples_in_100ms * 4, NULL);      \
            }                                                                          \
            if ((st->mode & FF_EBUR128_MODE_LRA) == FF_EBUR128_MODE_LRA) {             \
                st->d->short_term_frame_counter += st->d->needed_frames;               \
                if (st->d->short_term_frame_counter == st->d->samples_in_100ms * 30) { \
                    double st_energy;                                                  \
                    ebur128_energy_shortterm(st, &st_energy);                          \
                    if (st_energy >= histogram_energy_boundaries[0]) {                 \
                        ++st->d->short_term_block_energy_histogram[                    \
                                                    find_histogram_index(st_energy)];  \
                    }                                                                  \
                    st->d->short_term_frame_counter = st->d->samples_in_100ms * 20;    \
                }                                                                      \
            }                                                                          \
            /* 100ms are needed for all blocks besides the first one */                \
            st->d->needed_frames = st->d->samples_in_100ms;                            \
            /* reset audio_data_index when buffer full */                              \
            if (st->d->audio_data_index == st->d->audio_data_frames * st->channels) {  \
                st->d->audio_data_index = 0;                                           \
            }                                                                          \
        } else {                                                                       \
            ebur128_filter_##type(st, srcs, src_index, frames, stride);                \
            st->d->audio_data_index += frames * st->channels;                          \
            if ((st->mode & FF_EBUR128_MODE_LRA) == FF_EBUR128_MODE_LRA) {             \
                st->d->short_term_frame_counter += frames;                             \
            }                                                                          \
            st->d->needed_frames -= frames;                                            \
            frames = 0;                                                                \
        }                                                                              \
    }                                                                                  \
}
FF_EBUR128_ADD_FRAMES_PLANAR(short)
FF_EBUR128_ADD_FRAMES_PLANAR(int)
FF_EBUR128_ADD_FRAMES_PLANAR(float)
FF_EBUR128_ADD_FRAMES_PLANAR(double)
#define FF_EBUR128_ADD_FRAMES(type)                                            \
void ff_ebur128_add_frames_##type(FFEBUR128State* st, const type* src,         \
                                    size_t frames) {                           \
  int i;                                                                       \
  const type **buf = (const type**)st->d->data_ptrs;                           \
  for (i = 0; i < st->channels; i++)                                           \
    buf[i] = src + i;                                                          \
  ff_ebur128_add_frames_planar_##type(st, buf, frames, st->channels);          \
}
FF_EBUR128_ADD_FRAMES(short)
FF_EBUR128_ADD_FRAMES(int)
FF_EBUR128_ADD_FRAMES(float)
FF_EBUR128_ADD_FRAMES(double)

static int ebur128_calc_relative_threshold(FFEBUR128State **sts, size_t size,
                                           double *relative_threshold)
{
    size_t i, j;
    int above_thresh_counter = 0;
    *relative_threshold = 0.0;

    for (i = 0; i < size; i++) {
        unsigned long *block_energy_histogram = sts[i]->d->block_energy_histogram;
        for (j = 0; j < 1000; ++j) {
            *relative_threshold += block_energy_histogram[j] * histogram_energies[j];
            above_thresh_counter += block_energy_histogram[j];
        }
    }

    if (above_thresh_counter != 0) {
        *relative_threshold /= (double)above_thresh_counter;
        *relative_threshold *= RELATIVE_GATE_FACTOR;
    }

    return above_thresh_counter;
}

static int ebur128_gated_loudness(FFEBUR128State ** sts, size_t size,
                                  double *out)
{
    double gated_loudness = 0.0;
    double relative_threshold;
    size_t above_thresh_counter;
    size_t i, j, start_index;

    for (i = 0; i < size; i++)
        if ((sts[i]->mode & FF_EBUR128_MODE_I) != FF_EBUR128_MODE_I)
            return AVERROR(EINVAL);

    if (!ebur128_calc_relative_threshold(sts, size, &relative_threshold)) {
        *out = -HUGE_VAL;
        return 0;
    }

    above_thresh_counter = 0;
    if (relative_threshold < histogram_energy_boundaries[0]) {
        start_index = 0;
    } else {
        start_index = find_histogram_index(relative_threshold);
        if (relative_threshold > histogram_energies[start_index]) {
            ++start_index;
        }
    }
    for (i = 0; i < size; i++) {
        for (j = start_index; j < 1000; ++j) {
            gated_loudness += sts[i]->d->block_energy_histogram[j] *
                histogram_energies[j];
            above_thresh_counter += sts[i]->d->block_energy_histogram[j];
        }
    }
    if (!above_thresh_counter) {
        *out = -HUGE_VAL;
        return 0;
    }
    gated_loudness /= (double) above_thresh_counter;
    *out = ebur128_energy_to_loudness(gated_loudness);
    return 0;
}

int ff_ebur128_relative_threshold(FFEBUR128State * st, double *out)
{
    double relative_threshold;

    if ((st->mode & FF_EBUR128_MODE_I) != FF_EBUR128_MODE_I)
        return AVERROR(EINVAL);

    if (!ebur128_calc_relative_threshold(&st, 1, &relative_threshold)) {
        *out = -70.0;
        return 0;
    }

    *out = ebur128_energy_to_loudness(relative_threshold);
    return 0;
}

int ff_ebur128_loudness_global(FFEBUR128State * st, double *out)
{
    return ebur128_gated_loudness(&st, 1, out);
}

int ff_ebur128_loudness_global_multiple(FFEBUR128State ** sts, size_t size,
                                        double *out)
{
    return ebur128_gated_loudness(sts, size, out);
}

static int ebur128_energy_in_interval(FFEBUR128State * st,
                                      size_t interval_frames, double *out)
{
    if (interval_frames > st->d->audio_data_frames) {
        return AVERROR(EINVAL);
    }
    ebur128_calc_gating_block(st, interval_frames, out);
    return 0;
}

static int ebur128_energy_shortterm(FFEBUR128State * st, double *out)
{
    return ebur128_energy_in_interval(st, st->d->samples_in_100ms * 30,
                                      out);
}

int ff_ebur128_loudness_momentary(FFEBUR128State * st, double *out)
{
    double energy;
    int error = ebur128_energy_in_interval(st, st->d->samples_in_100ms * 4,
                                           &energy);
    if (error) {
        return error;
    } else if (energy <= 0.0) {
        *out = -HUGE_VAL;
        return 0;
    }
    *out = ebur128_energy_to_loudness(energy);
    return 0;
}

int ff_ebur128_loudness_shortterm(FFEBUR128State * st, double *out)
{
    double energy;
    int error = ebur128_energy_shortterm(st, &energy);
    if (error) {
        return error;
    } else if (energy <= 0.0) {
        *out = -HUGE_VAL;
        return 0;
    }
    *out = ebur128_energy_to_loudness(energy);
    return 0;
}

int ff_ebur128_loudness_window(FFEBUR128State * st,
                               unsigned long window, double *out)
{
    double energy;
    size_t interval_frames = st->samplerate * window / 1000;
    int error = ebur128_energy_in_interval(st, interval_frames, &energy);
    if (error) {
        return error;
    } else if (energy <= 0.0) {
        *out = -HUGE_VAL;
        return 0;
    }
    *out = ebur128_energy_to_loudness(energy);
    return 0;
}

/* EBU - TECH 3342 */
int ff_ebur128_loudness_range_multiple(FFEBUR128State ** sts, size_t size,
                                       double *out)
{
    size_t i, j;
    size_t stl_size;
    double stl_power, stl_integrated;
    /* High and low percentile energy */
    double h_en, l_en;
    unsigned long hist[1000] = { 0 };
    size_t percentile_low, percentile_high;
    size_t index;

    for (i = 0; i < size; ++i) {
        if (sts[i]) {
            if ((sts[i]->mode & FF_EBUR128_MODE_LRA) !=
                FF_EBUR128_MODE_LRA) {
                return AVERROR(EINVAL);
            }
        }
    }

    stl_size = 0;
    stl_power = 0.0;
    for (i = 0; i < size; ++i) {
        if (!sts[i])
            continue;
        for (j = 0; j < 1000; ++j) {
            hist[j] += sts[i]->d->short_term_block_energy_histogram[j];
            stl_size += sts[i]->d->short_term_block_energy_histogram[j];
            stl_power += sts[i]->d->short_term_block_energy_histogram[j]
                * histogram_energies[j];
        }
    }
    if (!stl_size) {
        *out = 0.0;
        return 0;
    }

    stl_power /= stl_size;
    stl_integrated = MINUS_20DB * stl_power;

    if (stl_integrated < histogram_energy_boundaries[0]) {
        index = 0;
    } else {
        index = find_histogram_index(stl_integrated);
        if (stl_integrated > histogram_energies[index]) {
            ++index;
        }
    }
    stl_size = 0;
    for (j = index; j < 1000; ++j) {
        stl_size += hist[j];
    }
    if (!stl_size) {
        *out = 0.0;
        return 0;
    }

    percentile_low = (size_t) ((stl_size - 1) * 0.1 + 0.5);
    percentile_high = (size_t) ((stl_size - 1) * 0.95 + 0.5);

    stl_size = 0;
    j = index;
    while (stl_size <= percentile_low) {
        stl_size += hist[j++];
    }
    l_en = histogram_energies[j - 1];
    while (stl_size <= percentile_high) {
        stl_size += hist[j++];
    }
    h_en = histogram_energies[j - 1];
    *out =
        ebur128_energy_to_loudness(h_en) -
        ebur128_energy_to_loudness(l_en);
    return 0;
}

int ff_ebur128_loudness_range(FFEBUR128State * st, double *out)
{
    return ff_ebur128_loudness_range_multiple(&st, 1, out);
}

int ff_ebur128_sample_peak(FFEBUR128State * st,
                           unsigned int channel_number, double *out)
{
    if ((st->mode & FF_EBUR128_MODE_SAMPLE_PEAK) !=
        FF_EBUR128_MODE_SAMPLE_PEAK) {
        return AVERROR(EINVAL);
    } else if (channel_number >= st->channels) {
        return AVERROR(EINVAL);
    }
    *out = st->d->sample_peak[channel_number];
    return 0;
}
