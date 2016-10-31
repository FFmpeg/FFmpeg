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

#include <stdint.h>

#include "libavresample/avresample.h"
#include "libavutil/attributes.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/common.h"
#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "libavutil/samplefmt.h"

#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct ASyncContext {
    const AVClass *class;

    AVAudioResampleContext *avr;
    int64_t pts;            ///< timestamp in samples of the first sample in fifo
    int min_delta;          ///< pad/trim min threshold in samples
    int first_frame;        ///< 1 until filter_frame() has processed at least 1 frame with a pts != AV_NOPTS_VALUE
    int64_t first_pts;      ///< user-specified first expected pts, in samples
    int comp;               ///< current resample compensation

    /* options */
    int resample;
    float min_delta_sec;
    int max_comp;

    /* set by filter_frame() to signal an output frame to request_frame() */
    int got_output;
} ASyncContext;

#define OFFSET(x) offsetof(ASyncContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
static const AVOption asyncts_options[] = {
    { "compensate", "Stretch/squeeze the data to make it match the timestamps", OFFSET(resample),      AV_OPT_TYPE_BOOL,  { .i64 = 0 },   0, 1,       A|F },
    { "min_delta",  "Minimum difference between timestamps and audio data "
                    "(in seconds) to trigger padding/trimmin the data.",        OFFSET(min_delta_sec), AV_OPT_TYPE_FLOAT, { .dbl = 0.1 }, 0, INT_MAX, A|F },
    { "max_comp",   "Maximum compensation in samples per second.",              OFFSET(max_comp),      AV_OPT_TYPE_INT,   { .i64 = 500 }, 0, INT_MAX, A|F },
    { "first_pts",  "Assume the first pts should be this value.",               OFFSET(first_pts),     AV_OPT_TYPE_INT64, { .i64 = AV_NOPTS_VALUE }, INT64_MIN, INT64_MAX, A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(asyncts);

static av_cold int init(AVFilterContext *ctx)
{
    ASyncContext *s = ctx->priv;

    s->pts         = AV_NOPTS_VALUE;
    s->first_frame = 1;

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ASyncContext *s = ctx->priv;

    if (s->avr) {
        avresample_close(s->avr);
        avresample_free(&s->avr);
    }
}

static int config_props(AVFilterLink *link)
{
    ASyncContext *s = link->src->priv;
    int ret;

    s->min_delta = s->min_delta_sec * link->sample_rate;
    link->time_base = (AVRational){1, link->sample_rate};

    s->avr = avresample_alloc_context();
    if (!s->avr)
        return AVERROR(ENOMEM);

    av_opt_set_int(s->avr,  "in_channel_layout", link->channel_layout, 0);
    av_opt_set_int(s->avr, "out_channel_layout", link->channel_layout, 0);
    av_opt_set_int(s->avr,  "in_sample_fmt",     link->format,         0);
    av_opt_set_int(s->avr, "out_sample_fmt",     link->format,         0);
    av_opt_set_int(s->avr,  "in_sample_rate",    link->sample_rate,    0);
    av_opt_set_int(s->avr, "out_sample_rate",    link->sample_rate,    0);

    if (s->resample)
        av_opt_set_int(s->avr, "force_resampling", 1, 0);

    if ((ret = avresample_open(s->avr)) < 0)
        return ret;

    return 0;
}

/* get amount of data currently buffered, in samples */
static int64_t get_delay(ASyncContext *s)
{
    return avresample_available(s->avr) + avresample_get_delay(s->avr);
}

static void handle_trimming(AVFilterContext *ctx)
{
    ASyncContext *s = ctx->priv;

    if (s->pts < s->first_pts) {
        int delta = FFMIN(s->first_pts - s->pts, avresample_available(s->avr));
        av_log(ctx, AV_LOG_VERBOSE, "Trimming %d samples from start\n",
               delta);
        avresample_read(s->avr, NULL, delta);
        s->pts += delta;
    } else if (s->first_frame)
        s->pts = s->first_pts;
}

static int request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    ASyncContext      *s = ctx->priv;
    int ret = 0;
    int nb_samples;

    s->got_output = 0;
    ret = ff_request_frame(ctx->inputs[0]);

    /* flush the fifo */
    if (ret == AVERROR_EOF) {
        if (s->first_pts != AV_NOPTS_VALUE)
            handle_trimming(ctx);

        if (nb_samples = get_delay(s)) {
            AVFrame *buf = ff_get_audio_buffer(link, nb_samples);
            if (!buf)
                return AVERROR(ENOMEM);
            ret = avresample_convert(s->avr, buf->extended_data,
                                     buf->linesize[0], nb_samples, NULL, 0, 0);
            if (ret <= 0) {
                av_frame_free(&buf);
                return (ret < 0) ? ret : AVERROR_EOF;
            }

            buf->pts = s->pts;
            return ff_filter_frame(link, buf);
        }
    }

    return ret;
}

static int write_to_fifo(ASyncContext *s, AVFrame *buf)
{
    int ret = avresample_convert(s->avr, NULL, 0, 0, buf->extended_data,
                                 buf->linesize[0], buf->nb_samples);
    av_frame_free(&buf);
    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext  *ctx = inlink->dst;
    ASyncContext       *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int nb_channels = av_get_channel_layout_nb_channels(buf->channel_layout);
    int64_t pts = (buf->pts == AV_NOPTS_VALUE) ? buf->pts :
                  av_rescale_q(buf->pts, inlink->time_base, outlink->time_base);
    int out_size, ret;
    int64_t delta;
    int64_t new_pts;

    /* buffer data until we get the next timestamp */
    if (s->pts == AV_NOPTS_VALUE || pts == AV_NOPTS_VALUE) {
        if (pts != AV_NOPTS_VALUE) {
            s->pts = pts - get_delay(s);
        }
        return write_to_fifo(s, buf);
    }

    if (s->first_pts != AV_NOPTS_VALUE) {
        handle_trimming(ctx);
        if (!avresample_available(s->avr))
            return write_to_fifo(s, buf);
    }

    /* when we have two timestamps, compute how many samples would we have
     * to add/remove to get proper sync between data and timestamps */
    delta    = pts - s->pts - get_delay(s);
    out_size = avresample_available(s->avr);

    if (llabs(delta) > s->min_delta ||
        (s->first_frame && delta && s->first_pts != AV_NOPTS_VALUE)) {
        av_log(ctx, AV_LOG_VERBOSE, "Discontinuity - %"PRId64" samples.\n", delta);
        out_size = av_clipl_int32((int64_t)out_size + delta);
    } else {
        if (s->resample) {
            // adjust the compensation if delta is non-zero
            int delay = get_delay(s);
            int comp = s->comp + av_clip(delta * inlink->sample_rate / delay,
                                         -s->max_comp, s->max_comp);
            if (comp != s->comp) {
                av_log(ctx, AV_LOG_VERBOSE, "Compensating %d samples per second.\n", comp);
                if (avresample_set_compensation(s->avr, comp, inlink->sample_rate) == 0) {
                    s->comp = comp;
                }
            }
        }
        // adjust PTS to avoid monotonicity errors with input PTS jitter
        pts -= delta;
        delta = 0;
    }

    if (out_size > 0) {
        AVFrame *buf_out = ff_get_audio_buffer(outlink, out_size);
        if (!buf_out) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        if (s->first_frame && delta > 0) {
            int planar = av_sample_fmt_is_planar(buf_out->format);
            int planes = planar ?  nb_channels : 1;
            int block_size = av_get_bytes_per_sample(buf_out->format) *
                             (planar ? 1 : nb_channels);

            int ch;

            av_samples_set_silence(buf_out->extended_data, 0, delta,
                                   nb_channels, buf->format);

            for (ch = 0; ch < planes; ch++)
                buf_out->extended_data[ch] += delta * block_size;

            avresample_read(s->avr, buf_out->extended_data, out_size);

            for (ch = 0; ch < planes; ch++)
                buf_out->extended_data[ch] -= delta * block_size;
        } else {
            avresample_read(s->avr, buf_out->extended_data, out_size);

            if (delta > 0) {
                av_samples_set_silence(buf_out->extended_data, out_size - delta,
                                       delta, nb_channels, buf->format);
            }
        }
        buf_out->pts = s->pts;
        ret = ff_filter_frame(outlink, buf_out);
        if (ret < 0)
            goto fail;
        s->got_output = 1;
    } else if (avresample_available(s->avr)) {
        av_log(ctx, AV_LOG_WARNING, "Non-monotonous timestamps, dropping "
               "whole buffer.\n");
    }

    /* drain any remaining buffered data */
    avresample_read(s->avr, NULL, avresample_available(s->avr));

    new_pts = pts - avresample_get_delay(s->avr);
    /* check for s->pts monotonicity */
    if (new_pts > s->pts) {
        s->pts = new_pts;
        ret = avresample_convert(s->avr, NULL, 0, 0, buf->extended_data,
                                 buf->linesize[0], buf->nb_samples);
    } else {
        av_log(ctx, AV_LOG_WARNING, "Non-monotonous timestamps, dropping "
               "whole buffer.\n");
        ret = 0;
    }

    s->first_frame = 0;
fail:
    av_frame_free(&buf);

    return ret;
}

static const AVFilterPad avfilter_af_asyncts_inputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .filter_frame  = filter_frame
    },
    { NULL }
};

static const AVFilterPad avfilter_af_asyncts_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_AUDIO,
        .config_props  = config_props,
        .request_frame = request_frame
    },
    { NULL }
};

AVFilter ff_af_asyncts = {
    .name        = "asyncts",
    .description = NULL_IF_CONFIG_SMALL("Sync audio data to timestamps."),
    .init        = init,
    .uninit      = uninit,
    .priv_size   = sizeof(ASyncContext),
    .priv_class  = &asyncts_class,
    .inputs      = avfilter_af_asyncts_inputs,
    .outputs     = avfilter_af_asyncts_outputs,
};
