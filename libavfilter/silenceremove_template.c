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

#undef ftype
#undef FABS
#undef FMAX
#undef SAMPLE_FORMAT
#undef SQRT
#undef ZERO
#undef ONE
#undef TMIN
#if DEPTH == 32
#define SAMPLE_FORMAT flt
#define SQRT sqrtf
#define FMAX fmaxf
#define FABS fabsf
#define ftype float
#define ZERO 0.f
#define ONE 1.f
#define TMIN -FLT_MAX
#else
#define SAMPLE_FORMAT dbl
#define SQRT sqrt
#define FMAX fmax
#define FABS fabs
#define ftype double
#define ZERO 0.0
#define ONE 1.0
#define TMIN -DBL_MAX
#endif

#define fn3(a,b)   a##_##b
#define fn2(a,b)   fn3(a,b)
#define fn(a)      fn2(a, SAMPLE_FORMAT)

static void fn(flush)(ftype *dst, const ftype *src, int src_pos,
                      int nb_channels, int count, int src_nb_samples,
                      int *out_nb_samples)
{
    int oidx, out_count = count;
    int sidx = src_pos;

    if (count <= 0)
        return;

    oidx = *out_nb_samples + out_count - 1;
    *out_nb_samples += out_count;
    while (out_count-- > 0) {
        const int spos = sidx * nb_channels;
        const int opos = oidx * nb_channels;

        for (int ch = 0; ch < nb_channels; ch++)
            dst[opos + ch] = src[spos + ch];

        oidx--;
        sidx--;
        if (sidx < 0)
            sidx = src_nb_samples - 1;
    }
}

static void fn(queue_sample)(AVFilterContext *ctx,
                             const ftype *src,
                             ftype *queue,
                             int *queue_pos,
                             int *queue_size,
                             int *window_pos,
                             int *window_size,
                             const int nb_channels,
                             const int nb_samples,
                             const int window_nb_samples)
{
    const int pos = *queue_pos * nb_channels;

    for (int ch = 0; ch < nb_channels; ch++)
        queue[pos + ch] = src[ch];

    (*queue_pos)++;
    if (*queue_pos >= nb_samples)
        *queue_pos = 0;

    if (*queue_size < nb_samples)
        (*queue_size)++;

    if (*window_size < window_nb_samples)
        (*window_size)++;

    (*window_pos)++;
    if (*window_pos >= window_nb_samples)
        *window_pos = 0;
}

static ftype fn(compute_avg)(ftype *cache, ftype x, ftype px,
                             int window_size, int *unused, int *unused2)
{
    ftype r;

    cache[0] += FABS(x);
    cache[0] -= FABS(px);
    cache[0] = r = FMAX(cache[0], ZERO);

    return r / window_size;
}

#define PEAKS(empty_value,op,sample, psample)\
    if (!empty && psample == ss[front]) {    \
        ss[front] = empty_value;             \
        if (back != front) {                 \
            front--;                         \
            if (front < 0)                   \
                front = n - 1;               \
        }                                    \
        empty = front == back;               \
    }                                        \
                                             \
    if (!empty && sample op ss[front]) {     \
        while (1) {                          \
            ss[front] = empty_value;         \
            if (back == front) {             \
                empty = 1;                   \
                break;                       \
            }                                \
            front--;                         \
            if (front < 0)                   \
                front = n - 1;               \
        }                                    \
    }                                        \
                                             \
    while (!empty && sample op ss[back]) {   \
        ss[back] = empty_value;              \
        if (back == front) {                 \
            empty = 1;                       \
            break;                           \
        }                                    \
        back++;                              \
        if (back >= n)                       \
            back = 0;                        \
    }                                        \
                                             \
    if (!empty) {                            \
        back--;                              \
        if (back < 0)                        \
            back = n - 1;                    \
    }

static ftype fn(compute_median)(ftype *ss, ftype x, ftype px,
                                int n, int *ffront, int *bback)
{
    ftype r, ax = FABS(x);
    int front = *ffront;
    int back = *bback;
    int empty = front == back && ss[front] == -ONE;
    int idx;

    PEAKS(-ONE, >, ax, FABS(px))

    ss[back] = ax;
    idx = (back <= front) ? back + (front - back + 1) / 2 : back + (n + front - back + 1) / 2;
    if (idx >= n)
        idx -= n;
    av_assert2(idx >= 0 && idx < n);
    r = ss[idx];

    *ffront = front;
    *bback = back;

    return r;
}

static ftype fn(compute_peak)(ftype *ss, ftype x, ftype px,
                              int n, int *ffront, int *bback)
{
    ftype r, ax = FABS(x);
    int front = *ffront;
    int back = *bback;
    int empty = front == back && ss[front] == ZERO;

    PEAKS(ZERO, >=, ax, FABS(px))

    ss[back] = ax;
    r = ss[front];

    *ffront = front;
    *bback = back;

    return r;
}

static ftype fn(compute_ptp)(ftype *ss, ftype x, ftype px,
                             int n, int *ffront, int *bback)
{
    int front = *ffront;
    int back = *bback;
    int empty = front == back && ss[front] == TMIN;
    ftype r, max, min;

    PEAKS(TMIN, >=, x, px)

    ss[back] = x;
    max = ss[front];
    min = x;
    r = FABS(min) + FABS(max - min);

    *ffront = front;
    *bback = back;

    return r;
}

static ftype fn(compute_rms)(ftype *cache, ftype x, ftype px,
                             int window_size, int *unused, int *unused2)
{
    ftype r;

    cache[0] += x * x;
    cache[0] -= px * px;
    cache[0] = r = FMAX(cache[0], ZERO);

    return SQRT(r / window_size);
}

static ftype fn(compute_dev)(ftype *ss, ftype x, ftype px,
                             int n, int *unused, int *unused2)
{
    ftype r;

    ss[0] += x;
    ss[0] -= px;

    ss[1] += x * x;
    ss[1] -= px * px;
    ss[1] = FMAX(ss[1], ZERO);

    r = FMAX(ss[1] - ss[0] * ss[0] / n, ZERO) / n;

    return SQRT(r);
}

static void fn(filter_start)(AVFilterContext *ctx,
                             const ftype *src, ftype *dst,
                             int *nb_out_samples,
                             const int nb_channels)
{
    SilenceRemoveContext *s = ctx->priv;
    const int start_periods = s->start_periods;
    int out_nb_samples = *nb_out_samples;
    const int start_window_nb_samples = s->start_window->nb_samples;
    const int start_nb_samples = s->start_queuef->nb_samples;
    const int start_wpos = s->start_window_pos * nb_channels;
    const int start_pos = s->start_queue_pos * nb_channels;
    ftype *startw = (ftype *)s->start_window->data[0];
    ftype *start = (ftype *)s->start_queuef->data[0];
    const ftype start_threshold = s->start_threshold;
    const int start_mode = s->start_mode;
    int start_thres = (start_mode == T_ANY) ? 0 : 1;
    const int start_duration = s->start_duration;
    ftype *start_cache = (ftype *)s->start_cache;
    const int start_silence = s->start_silence;
    int window_size = start_window_nb_samples;
    const int cache_size = s->cache_size;
    int *front = s->start_front;
    int *back = s->start_back;

    fn(queue_sample)(ctx, src, start,
                     &s->start_queue_pos,
                     &s->start_queue_size,
                     &s->start_window_pos,
                     &s->start_window_size,
                     nb_channels,
                     start_nb_samples,
                     start_window_nb_samples);

    if (s->start_found_periods < 0)
        goto skip;

    if (s->detection != D_PEAK && s->detection != D_MEDIAN &&
        s->detection != D_PTP)
        window_size = s->start_window_size;

    for (int ch = 0; ch < nb_channels; ch++) {
        ftype start_sample = start[start_pos + ch];
        ftype start_ow = startw[start_wpos + ch];
        ftype tstart;

        tstart = fn(s->compute)(start_cache + ch * cache_size,
                                start_sample,
                                start_ow,
                                window_size,
                                front + ch,
                                back + ch);

        startw[start_wpos + ch] = start_sample;

        if (start_mode == T_ANY) {
            start_thres |= tstart > start_threshold;
        } else {
            start_thres &= tstart > start_threshold;
        }
    }

    if (s->start_found_periods >= 0) {
        if (start_silence > 0) {
            s->start_silence_count++;
            if (s->start_silence_count > start_silence)
                s->start_silence_count = start_silence;
        }

        s->start_sample_count += start_thres;
    }

    if (s->start_sample_count > start_duration) {
        s->start_found_periods++;
        if (s->start_found_periods >= start_periods) {
            if (!ctx->is_disabled)
                fn(flush)(dst, start, s->start_queue_pos, nb_channels,
                          s->start_silence_count, start_nb_samples,
                          &out_nb_samples);
            s->start_silence_count = 0;
            s->start_found_periods = -1;
        }

        s->start_sample_count = 0;
    }

skip:
    if (s->start_found_periods < 0 || ctx->is_disabled) {
        const int dst_pos = out_nb_samples * nb_channels;
        for (int ch = 0; ch < nb_channels; ch++)
            dst[dst_pos + ch] = start[start_pos + ch];
        out_nb_samples++;
    }

    *nb_out_samples = out_nb_samples;
}

static void fn(filter_stop)(AVFilterContext *ctx,
                            const ftype *src, ftype *dst,
                            int *nb_out_samples,
                            const int nb_channels)
{
    SilenceRemoveContext *s = ctx->priv;
    const int stop_periods = s->stop_periods;
    int out_nb_samples = *nb_out_samples;
    const int stop_window_nb_samples = s->stop_window->nb_samples;
    const int stop_nb_samples = s->stop_queuef->nb_samples;
    const int stop_wpos = s->stop_window_pos * nb_channels;
    const int stop_pos = s->stop_queue_pos * nb_channels;
    ftype *stopw = (ftype *)s->stop_window->data[0];
    const ftype stop_threshold = s->stop_threshold;
    ftype *stop = (ftype *)s->stop_queuef->data[0];
    const int stop_mode = s->stop_mode;
    int stop_thres = (stop_mode == T_ANY) ? 0 : 1;
    const int stop_duration = s->stop_duration;
    ftype *stop_cache = (ftype *)s->stop_cache;
    const int stop_silence = s->stop_silence;
    int window_size = stop_window_nb_samples;
    const int cache_size = s->cache_size;
    const int restart = s->restart;
    int *front = s->stop_front;
    int *back = s->stop_back;

    fn(queue_sample)(ctx, src, stop,
                     &s->stop_queue_pos,
                     &s->stop_queue_size,
                     &s->stop_window_pos,
                     &s->stop_window_size,
                     nb_channels,
                     stop_nb_samples,
                     stop_window_nb_samples);

    if (s->detection != D_PEAK && s->detection != D_MEDIAN &&
        s->detection != D_PTP)
        window_size = s->stop_window_size;

    for (int ch = 0; ch < nb_channels; ch++) {
        ftype stop_sample = stop[stop_pos + ch];
        ftype stop_ow = stopw[stop_wpos + ch];
        ftype tstop;

        tstop = fn(s->compute)(stop_cache + ch * cache_size,
                               stop_sample,
                               stop_ow,
                               window_size,
                               front + ch,
                               back + ch);

        stopw[stop_wpos + ch] = stop_sample;

        if (stop_mode == T_ANY) {
            stop_thres |= tstop <= stop_threshold;
        } else {
            stop_thres &= tstop <= stop_threshold;
        }
    }

    s->found_nonsilence = FFMAX(s->found_nonsilence, !stop_thres);
    if (restart && !stop_thres)
        s->stop_found_periods = 0;

    if (s->stop_found_periods >= 0 || ctx->is_disabled) {
        if (s->found_nonsilence) {
            s->stop_sample_count += stop_thres;
            s->stop_sample_count *= stop_thres;
        }
    } else if (s->stop_silence_count > 0) {
        const int dst_pos = out_nb_samples * nb_channels;
        for (int ch = 0; ch < nb_channels; ch++)
            dst[dst_pos + ch] = stop[stop_pos + ch];
        s->stop_silence_count--;
        out_nb_samples++;
    }

    if (s->stop_sample_count > stop_duration) {
        s->stop_found_periods++;
        if (s->stop_found_periods >= stop_periods) {
            s->stop_found_periods = -1;
            s->stop_silence_count = stop_silence;
        }

        s->stop_sample_count = 0;
    }

    if (s->stop_found_periods >= 0 || ctx->is_disabled) {
        const int dst_pos = out_nb_samples * nb_channels;
        for (int ch = 0; ch < nb_channels; ch++)
            dst[dst_pos + ch] = stop[stop_pos + ch];
        out_nb_samples++;
    }

    *nb_out_samples = out_nb_samples;
}
