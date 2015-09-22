/*
 * Copyright (c) 2001 Heikki Leinonen
 * Copyright (c) 2001 Chris Bagwell
 * Copyright (c) 2003 Donnie Smith
 * Copyright (c) 2014 Paul B Mahol
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

#include <float.h> /* DBL_MAX */

#include "libavutil/opt.h"
#include "libavutil/timestamp.h"
#include "audio.h"
#include "formats.h"
#include "avfilter.h"
#include "internal.h"

enum SilenceMode {
    SILENCE_TRIM,
    SILENCE_TRIM_FLUSH,
    SILENCE_COPY,
    SILENCE_COPY_FLUSH,
    SILENCE_STOP
};

typedef struct SilenceRemoveContext {
    const AVClass *class;

    enum SilenceMode mode;

    int start_periods;
    int64_t start_duration;
    double start_threshold;

    int stop_periods;
    int64_t stop_duration;
    double stop_threshold;

    double *start_holdoff;
    size_t start_holdoff_offset;
    size_t start_holdoff_end;
    int    start_found_periods;

    double *stop_holdoff;
    size_t stop_holdoff_offset;
    size_t stop_holdoff_end;
    int    stop_found_periods;

    double *window;
    double *window_current;
    double *window_end;
    int window_size;
    double rms_sum;

    int leave_silence;
    int restart;
    int64_t next_pts;
} SilenceRemoveContext;

#define OFFSET(x) offsetof(SilenceRemoveContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM
static const AVOption silenceremove_options[] = {
    { "start_periods",   NULL, OFFSET(start_periods),   AV_OPT_TYPE_INT,      {.i64=0},     0,    9000, FLAGS },
    { "start_duration",  NULL, OFFSET(start_duration),  AV_OPT_TYPE_DURATION, {.i64=0},     0,    9000, FLAGS },
    { "start_threshold", NULL, OFFSET(start_threshold), AV_OPT_TYPE_DOUBLE,   {.dbl=0},     0, DBL_MAX, FLAGS },
    { "stop_periods",    NULL, OFFSET(stop_periods),    AV_OPT_TYPE_INT,      {.i64=0}, -9000,    9000, FLAGS },
    { "stop_duration",   NULL, OFFSET(stop_duration),   AV_OPT_TYPE_DURATION, {.i64=0},     0,    9000, FLAGS },
    { "stop_threshold",  NULL, OFFSET(stop_threshold),  AV_OPT_TYPE_DOUBLE,   {.dbl=0},     0, DBL_MAX, FLAGS },
    { "leave_silence",   NULL, OFFSET(leave_silence),   AV_OPT_TYPE_BOOL,     {.i64=0},     0,       1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(silenceremove);

static av_cold int init(AVFilterContext *ctx)
{
    SilenceRemoveContext *s = ctx->priv;

    if (s->stop_periods < 0) {
        s->stop_periods = -s->stop_periods;
        s->restart = 1;
    }

    return 0;
}

static void clear_rms(SilenceRemoveContext *s)
{
    memset(s->window, 0, s->window_size * sizeof(*s->window));

    s->window_current = s->window;
    s->window_end = s->window + s->window_size;
    s->rms_sum = 0;
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    SilenceRemoveContext *s = ctx->priv;

    s->window_size = (inlink->sample_rate / 50) * inlink->channels;
    s->window = av_malloc_array(s->window_size, sizeof(*s->window));
    if (!s->window)
        return AVERROR(ENOMEM);

    clear_rms(s);

    s->start_duration = av_rescale(s->start_duration, inlink->sample_rate,
                                   AV_TIME_BASE);
    s->stop_duration  = av_rescale(s->stop_duration, inlink->sample_rate,
                                   AV_TIME_BASE);

    s->start_holdoff = av_malloc_array(FFMAX(s->start_duration, 1),
                                       sizeof(*s->start_holdoff) *
                                       inlink->channels);
    if (!s->start_holdoff)
        return AVERROR(ENOMEM);

    s->start_holdoff_offset = 0;
    s->start_holdoff_end    = 0;
    s->start_found_periods  = 0;

    s->stop_holdoff = av_malloc_array(FFMAX(s->stop_duration, 1),
                                      sizeof(*s->stop_holdoff) *
                                      inlink->channels);
    if (!s->stop_holdoff)
        return AVERROR(ENOMEM);

    s->stop_holdoff_offset = 0;
    s->stop_holdoff_end    = 0;
    s->stop_found_periods  = 0;

    if (s->start_periods)
        s->mode = SILENCE_TRIM;
    else
        s->mode = SILENCE_COPY;

    return 0;
}

static double compute_rms(SilenceRemoveContext *s, double sample)
{
    double new_sum;

    new_sum  = s->rms_sum;
    new_sum -= *s->window_current;
    new_sum += sample * sample;

    return sqrt(new_sum / s->window_size);
}

static void update_rms(SilenceRemoveContext *s, double sample)
{
    s->rms_sum -= *s->window_current;
    *s->window_current = sample * sample;
    s->rms_sum += *s->window_current;

    s->window_current++;
    if (s->window_current >= s->window_end)
        s->window_current = s->window;
}

static void flush(AVFrame *out, AVFilterLink *outlink,
                  int *nb_samples_written, int *ret)
{
    if (*nb_samples_written) {
        out->nb_samples = *nb_samples_written / outlink->channels;
        *ret = ff_filter_frame(outlink, out);
        *nb_samples_written = 0;
    } else {
        av_frame_free(&out);
    }
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    SilenceRemoveContext *s = ctx->priv;
    int i, j, threshold, ret = 0;
    int nbs, nb_samples_read, nb_samples_written;
    double *obuf, *ibuf = (double *)in->data[0];
    AVFrame *out;

    nb_samples_read = nb_samples_written = 0;

    switch (s->mode) {
    case SILENCE_TRIM:
silence_trim:
        nbs = in->nb_samples - nb_samples_read / inlink->channels;
        if (!nbs)
            break;

        for (i = 0; i < nbs; i++) {
            threshold = 0;
            for (j = 0; j < inlink->channels; j++) {
                threshold |= compute_rms(s, ibuf[j]) > s->start_threshold;
            }

            if (threshold) {
                for (j = 0; j < inlink->channels; j++) {
                    update_rms(s, *ibuf);
                    s->start_holdoff[s->start_holdoff_end++] = *ibuf++;
                    nb_samples_read++;
                }

                if (s->start_holdoff_end >= s->start_duration * inlink->channels) {
                    if (++s->start_found_periods >= s->start_periods) {
                        s->mode = SILENCE_TRIM_FLUSH;
                        goto silence_trim_flush;
                    }

                    s->start_holdoff_offset = 0;
                    s->start_holdoff_end = 0;
                }
            } else {
                s->start_holdoff_end = 0;

                for (j = 0; j < inlink->channels; j++)
                    update_rms(s, ibuf[j]);

                ibuf += inlink->channels;
                nb_samples_read += inlink->channels;
            }
        }
        break;

    case SILENCE_TRIM_FLUSH:
silence_trim_flush:
        nbs  = s->start_holdoff_end - s->start_holdoff_offset;
        nbs -= nbs % inlink->channels;
        if (!nbs)
            break;

        out = ff_get_audio_buffer(inlink, nbs / inlink->channels);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        memcpy(out->data[0], &s->start_holdoff[s->start_holdoff_offset],
               nbs * sizeof(double));
        s->start_holdoff_offset += nbs;

        ret = ff_filter_frame(outlink, out);

        if (s->start_holdoff_offset == s->start_holdoff_end) {
            s->start_holdoff_offset = 0;
            s->start_holdoff_end = 0;
            s->mode = SILENCE_COPY;
            goto silence_copy;
        }
        break;

    case SILENCE_COPY:
silence_copy:
        nbs = in->nb_samples - nb_samples_read / inlink->channels;
        if (!nbs)
            break;

        out = ff_get_audio_buffer(inlink, nbs);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        obuf = (double *)out->data[0];

        if (s->stop_periods) {
            for (i = 0; i < nbs; i++) {
                threshold = 1;
                for (j = 0; j < inlink->channels; j++)
                    threshold &= compute_rms(s, ibuf[j]) > s->stop_threshold;

                if (threshold && s->stop_holdoff_end && !s->leave_silence) {
                    s->mode = SILENCE_COPY_FLUSH;
                    flush(out, outlink, &nb_samples_written, &ret);
                    goto silence_copy_flush;
                } else if (threshold) {
                    for (j = 0; j < inlink->channels; j++) {
                        update_rms(s, *ibuf);
                        *obuf++ = *ibuf++;
                        nb_samples_read++;
                        nb_samples_written++;
                    }
                } else if (!threshold) {
                    for (j = 0; j < inlink->channels; j++) {
                        update_rms(s, *ibuf);
                        if (s->leave_silence) {
                            *obuf++ = *ibuf;
                            nb_samples_written++;
                        }

                        s->stop_holdoff[s->stop_holdoff_end++] = *ibuf++;
                        nb_samples_read++;
                    }

                    if (s->stop_holdoff_end >= s->stop_duration * inlink->channels) {
                        if (++s->stop_found_periods >= s->stop_periods) {
                            s->stop_holdoff_offset = 0;
                            s->stop_holdoff_end = 0;

                            if (!s->restart) {
                                s->mode = SILENCE_STOP;
                                flush(out, outlink, &nb_samples_written, &ret);
                                goto silence_stop;
                            } else {
                                s->stop_found_periods = 0;
                                s->start_found_periods = 0;
                                s->start_holdoff_offset = 0;
                                s->start_holdoff_end = 0;
                                clear_rms(s);
                                s->mode = SILENCE_TRIM;
                                flush(out, outlink, &nb_samples_written, &ret);
                                goto silence_trim;
                            }
                        }
                        s->mode = SILENCE_COPY_FLUSH;
                        flush(out, outlink, &nb_samples_written, &ret);
                        goto silence_copy_flush;
                    }
                }
            }
            flush(out, outlink, &nb_samples_written, &ret);
        } else {
            memcpy(obuf, ibuf, sizeof(double) * nbs * inlink->channels);
            ret = ff_filter_frame(outlink, out);
        }
        break;

    case SILENCE_COPY_FLUSH:
silence_copy_flush:
        nbs  = s->stop_holdoff_end - s->stop_holdoff_offset;
        nbs -= nbs % inlink->channels;
        if (!nbs)
            break;

        out = ff_get_audio_buffer(inlink, nbs / inlink->channels);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }

        memcpy(out->data[0], &s->stop_holdoff[s->stop_holdoff_offset],
               nbs * sizeof(double));
        s->stop_holdoff_offset += nbs;

        ret = ff_filter_frame(outlink, out);

        if (s->stop_holdoff_offset == s->stop_holdoff_end) {
            s->stop_holdoff_offset = 0;
            s->stop_holdoff_end = 0;
            s->mode = SILENCE_COPY;
            goto silence_copy;
        }
        break;
    case SILENCE_STOP:
silence_stop:
        break;
    }

    av_frame_free(&in);

    return ret;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SilenceRemoveContext *s = ctx->priv;
    int ret;

    ret = ff_request_frame(ctx->inputs[0]);
    if (ret == AVERROR_EOF && (s->mode == SILENCE_COPY_FLUSH ||
                               s->mode == SILENCE_COPY)) {
        int nbs = s->stop_holdoff_end - s->stop_holdoff_offset;
        if (nbs) {
            AVFrame *frame;

            frame = ff_get_audio_buffer(outlink, nbs / outlink->channels);
            if (!frame)
                return AVERROR(ENOMEM);

            memcpy(frame->data[0], &s->stop_holdoff[s->stop_holdoff_offset],
                   nbs * sizeof(double));
            ret = ff_filter_frame(ctx->inputs[0], frame);
        }
        s->mode = SILENCE_STOP;
    }
    return ret;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    static const enum AVSampleFormat sample_fmts[] = {
        AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_NONE
    };
    int ret;

    layouts = ff_all_channel_counts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ret = ff_set_common_channel_layouts(ctx, layouts);
    if (ret < 0)
        return ret;

    formats = ff_make_format_list(sample_fmts);
    if (!formats)
        return AVERROR(ENOMEM);
    ret = ff_set_common_formats(ctx, formats);
    if (ret < 0)
        return ret;

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SilenceRemoveContext *s = ctx->priv;

    av_freep(&s->start_holdoff);
    av_freep(&s->stop_holdoff);
    av_freep(&s->window);
}

static const AVFilterPad silenceremove_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_input,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad silenceremove_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter ff_af_silenceremove = {
    .name          = "silenceremove",
    .description   = NULL_IF_CONFIG_SMALL("Remove silence."),
    .priv_size     = sizeof(SilenceRemoveContext),
    .priv_class    = &silenceremove_class,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = silenceremove_inputs,
    .outputs       = silenceremove_outputs,
};
