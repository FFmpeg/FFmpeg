/*
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavresample/avresample.h"
#include "libavutil/audio_fifo.h"
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

    /* options */
    int resample;
    float min_delta_sec;
    int max_comp;
} ASyncContext;

#define OFFSET(x) offsetof(ASyncContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
static const AVOption options[] = {
    { "compensate", "Stretch/squeeze the data to make it match the timestamps", OFFSET(resample),      AV_OPT_TYPE_INT,   { 0 },   0, 1,       A },
    { "min_delta",  "Minimum difference between timestamps and audio data "
                    "(in seconds) to trigger padding/trimmin the data.",        OFFSET(min_delta_sec), AV_OPT_TYPE_FLOAT, { 0.1 }, 0, INT_MAX, A },
    { "max_comp",   "Maximum compensation in samples per second.",              OFFSET(max_comp),      AV_OPT_TYPE_INT,   { 500 }, 0, INT_MAX, A },
    { NULL },
};

static const AVClass async_class = {
    .class_name = "asyncts filter",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int init(AVFilterContext *ctx, const char *args, void *opaque)
{
    ASyncContext *s = ctx->priv;
    int ret;

    s->class = &async_class;
    av_opt_set_defaults(s);

    if ((ret = av_set_options_string(s, args, "=", ":")) < 0) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing options string '%s'.\n", args);
        return ret;
    }
    av_opt_free(s);

    s->pts = AV_NOPTS_VALUE;

    return 0;
}

static void uninit(AVFilterContext *ctx)
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

static int request_frame(AVFilterLink *link)
{
    AVFilterContext *ctx = link->src;
    ASyncContext      *s = ctx->priv;
    int ret = ff_request_frame(ctx->inputs[0]);
    int nb_samples;

    /* flush the fifo */
    if (ret == AVERROR_EOF && (nb_samples = avresample_get_delay(s->avr))) {
        AVFilterBufferRef *buf = ff_get_audio_buffer(link, AV_PERM_WRITE,
                                                     nb_samples);
        if (!buf)
            return AVERROR(ENOMEM);
        avresample_convert(s->avr, (void**)buf->extended_data, buf->linesize[0],
                           nb_samples, NULL, 0, 0);
        buf->pts = s->pts;
        ff_filter_samples(link, buf);
        return 0;
    }

    return ret;
}

static void write_to_fifo(ASyncContext *s, AVFilterBufferRef *buf)
{
    avresample_convert(s->avr, NULL, 0, 0, (void**)buf->extended_data,
                       buf->linesize[0], buf->audio->nb_samples);
    avfilter_unref_buffer(buf);
}

/* get amount of data currently buffered, in samples */
static int64_t get_delay(ASyncContext *s)
{
    return avresample_available(s->avr) + avresample_get_delay(s->avr);
}

static void filter_samples(AVFilterLink *inlink, AVFilterBufferRef *buf)
{
    AVFilterContext  *ctx = inlink->dst;
    ASyncContext       *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    int nb_channels = av_get_channel_layout_nb_channels(buf->audio->channel_layout);
    int64_t pts = (buf->pts == AV_NOPTS_VALUE) ? buf->pts :
                  av_rescale_q(buf->pts, inlink->time_base, outlink->time_base);
    int out_size;
    int64_t delta;

    /* buffer data until we get the first timestamp */
    if (s->pts == AV_NOPTS_VALUE) {
        if (pts != AV_NOPTS_VALUE) {
            s->pts = pts - get_delay(s);
        }
        write_to_fifo(s, buf);
        return;
    }

    /* now wait for the next timestamp */
    if (pts == AV_NOPTS_VALUE) {
        write_to_fifo(s, buf);
        return;
    }

    /* when we have two timestamps, compute how many samples would we have
     * to add/remove to get proper sync between data and timestamps */
    delta    = pts - s->pts - get_delay(s);
    out_size = avresample_available(s->avr);

    if (labs(delta) > s->min_delta) {
        av_log(ctx, AV_LOG_VERBOSE, "Discontinuity - %"PRId64" samples.\n", delta);
        out_size += delta;
    } else {
        if (s->resample) {
            int comp = av_clip(delta, -s->max_comp, s->max_comp);
            av_log(ctx, AV_LOG_VERBOSE, "Compensating %d samples per second.\n", comp);
            avresample_set_compensation(s->avr, delta, inlink->sample_rate);
        }
        delta = 0;
    }

    if (out_size > 0) {
        AVFilterBufferRef *buf_out = ff_get_audio_buffer(outlink, AV_PERM_WRITE,
                                                         out_size);
        if (!buf_out)
            return;

        avresample_read(s->avr, (void**)buf_out->extended_data, out_size);
        buf_out->pts = s->pts;

        if (delta > 0) {
            av_samples_set_silence(buf_out->extended_data, out_size - delta,
                                   delta, nb_channels, buf->format);
        }
        ff_filter_samples(outlink, buf_out);
    } else {
        av_log(ctx, AV_LOG_WARNING, "Non-monotonous timestamps, dropping "
               "whole buffer.\n");
    }

    /* drain any remaining buffered data */
    avresample_read(s->avr, NULL, avresample_available(s->avr));

    s->pts = pts - avresample_get_delay(s->avr);
    avresample_convert(s->avr, NULL, 0, 0, (void**)buf->extended_data,
                       buf->linesize[0], buf->audio->nb_samples);
    avfilter_unref_buffer(buf);
}

AVFilter avfilter_af_asyncts = {
    .name        = "asyncts",
    .description = NULL_IF_CONFIG_SMALL("Sync audio data to timestamps"),

    .init        = init,
    .uninit      = uninit,

    .priv_size   = sizeof(ASyncContext),

    .inputs      = (const AVFilterPad[]) {{ .name           = "default",
                                            .type           = AVMEDIA_TYPE_AUDIO,
                                            .filter_samples = filter_samples },
                                          { NULL }},
    .outputs     = (const AVFilterPad[]) {{ .name           = "default",
                                            .type           = AVMEDIA_TYPE_AUDIO,
                                            .config_props   = config_props,
                                            .request_frame  = request_frame },
                                          { NULL }},
};
