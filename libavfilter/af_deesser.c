/*
 * Copyright (c) 2018 Chris Johnson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"

typedef struct DeesserChannel {
    double s1, s2, s3;
    double m1, m2;
    double ratioA, ratioB;
    double iirSampleA, iirSampleB;
    int flip;
} DeesserChannel;

typedef struct DeesserContext {
    const AVClass *class;

    double intensity;
    double max;
    double frequency;
    int    mode;

    DeesserChannel *chan;
} DeesserContext;

enum OutModes {
    IN_MODE,
    OUT_MODE,
    ESS_MODE,
    NB_MODES
};

#define OFFSET(x) offsetof(DeesserContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption deesser_options[] = {
    { "i", "set intensity",    OFFSET(intensity), AV_OPT_TYPE_DOUBLE, {.dbl=0.0}, 0.0, 1.0, A },
    { "m", "set max deessing", OFFSET(max),       AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0.0, 1.0, A },
    { "f", "set frequency",    OFFSET(frequency), AV_OPT_TYPE_DOUBLE, {.dbl=0.5}, 0.0, 1.0, A },
    { "s", "set output mode",  OFFSET(mode),      AV_OPT_TYPE_INT,    {.i64=OUT_MODE}, 0, NB_MODES-1, A, "mode" },
    {  "i", "input",           0,                 AV_OPT_TYPE_CONST,  {.i64=IN_MODE},  0, 0, A, "mode" },
    {  "o", "output",          0,                 AV_OPT_TYPE_CONST,  {.i64=OUT_MODE}, 0, 0, A, "mode" },
    {  "e", "ess",             0,                 AV_OPT_TYPE_CONST,  {.i64=ESS_MODE}, 0, 0, A, "mode" },
    { NULL }
};

AVFILTER_DEFINE_CLASS(deesser);

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    DeesserContext *s = ctx->priv;

    s->chan = av_calloc(inlink->channels, sizeof(*s->chan));
    if (!s->chan)
        return AVERROR(ENOMEM);

    for (int i = 0; i < inlink->channels; i++) {
        DeesserChannel *chan = &s->chan[i];

        chan->ratioA = chan->ratioB = 1.0;
    }

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    DeesserContext *s = ctx->priv;
    AVFrame *out;

    if (av_frame_is_writable(in)) {
        out = in;
    } else {
        out = ff_get_audio_buffer(outlink, in->nb_samples);
        if (!out) {
            av_frame_free(&in);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, in);
    }

    for (int ch = 0; ch < inlink->channels; ch++) {
        DeesserChannel *dec = &s->chan[ch];
        double *src = (double *)in->extended_data[ch];
        double *dst = (double *)out->extended_data[ch];
        double overallscale = inlink->sample_rate < 44100 ? 44100.0 / inlink->sample_rate : inlink->sample_rate / 44100.0;
        double intensity = pow(s->intensity, 5) * (8192 / overallscale);
        double maxdess = 1.0 / pow(10.0, ((s->max - 1.0) * 48.0) / 20);
        double iirAmount = pow(s->frequency, 2) / overallscale;
        double offset;
        double sense;
        double recovery;
        double attackspeed;

        for (int i = 0; i < in->nb_samples; i++) {
            double sample = src[i];

            dec->s3 = dec->s2;
            dec->s2 = dec->s1;
            dec->s1 = sample;
            dec->m1 = (dec->s1 - dec->s2) * ((dec->s1 - dec->s2) / 1.3);
            dec->m2 = (dec->s2 - dec->s3) * ((dec->s1 - dec->s2) / 1.3);
            sense = (dec->m1 - dec->m2) * ((dec->m1 - dec->m2) / 1.3);
            attackspeed = 7.0 + sense * 1024;

            sense = 1.0 + intensity * intensity * sense;
            sense = FFMIN(sense, intensity);
            recovery = 1.0 + (0.01 / sense);

            offset = 1.0 - fabs(sample);

            if (dec->flip) {
                dec->iirSampleA = (dec->iirSampleA * (1.0 - (offset * iirAmount))) +
                                  (sample * (offset * iirAmount));
                if (dec->ratioA < sense) {
                    dec->ratioA = ((dec->ratioA * attackspeed) + sense) / (attackspeed + 1.0);
                } else {
                    dec->ratioA = 1.0 + ((dec->ratioA - 1.0) / recovery);
                }

                dec->ratioA = FFMIN(dec->ratioA, maxdess);
                sample = dec->iirSampleA + ((sample - dec->iirSampleA) / dec->ratioA);
            } else {
                dec->iirSampleB = (dec->iirSampleB * (1.0 - (offset * iirAmount))) +
                                  (sample * (offset * iirAmount));
                if (dec->ratioB < sense) {
                    dec->ratioB = ((dec->ratioB * attackspeed) + sense) / (attackspeed + 1.0);
                } else {
                    dec->ratioB = 1.0 + ((dec->ratioB - 1.0) / recovery);
                }

                dec->ratioB = FFMIN(dec->ratioB, maxdess);
                sample = dec->iirSampleB + ((sample - dec->iirSampleB) / dec->ratioB);
            }

            dec->flip = !dec->flip;

            if (ctx->is_disabled)
                sample = src[i];

            switch (s->mode) {
            case IN_MODE:  dst[i] = src[i]; break;
            case OUT_MODE: dst[i] = sample; break;
            case ESS_MODE: dst[i] = src[i] - sample; break;
            }
        }
    }

    if (out != in)
        av_frame_free(&in);

    return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    DeesserContext *s = ctx->priv;

    av_freep(&s->chan);
}

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_deesser = {
    .name          = "deesser",
    .description   = NULL_IF_CONFIG_SMALL("Apply de-essing to the audio."),
    .priv_size     = sizeof(DeesserContext),
    .priv_class    = &deesser_class,
    .uninit        = uninit,
    FILTER_INPUTS(inputs),
    FILTER_OUTPUTS(outputs),
    FILTER_SINGLE_SAMPLEFMT(AV_SAMPLE_FMT_DBLP),
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_INTERNAL,
};
