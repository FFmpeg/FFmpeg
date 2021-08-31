/*
 * Copyright (c) 2019 Paul B Mahol
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

#include <pocketsphinx/pocketsphinx.h>

#include "libavutil/avstring.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "internal.h"

typedef struct ASRContext {
    const AVClass *class;

    int   rate;
    char *hmm;
    char *dict;
    char *lm;
    char *lmctl;
    char *lmname;
    char *logfn;

    ps_decoder_t *ps;
    cmd_ln_t *config;

    int utt_started;
} ASRContext;

#define OFFSET(x) offsetof(ASRContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_FILTERING_PARAM
static const AVOption asr_options[] = {
    { "rate",  "set sampling rate",                             OFFSET(rate),   AV_OPT_TYPE_INT,    {.i64=16000}, 0, INT_MAX, .flags = FLAGS },
    { "hmm",   "set directory containing acoustic model files", OFFSET(hmm),    AV_OPT_TYPE_STRING, {.str=NULL},              .flags = FLAGS },
    { "dict",  "set pronunciation dictionary",                  OFFSET(dict),   AV_OPT_TYPE_STRING, {.str=NULL},              .flags = FLAGS },
    { "lm",    "set language model file",                       OFFSET(lm),     AV_OPT_TYPE_STRING, {.str=NULL},              .flags = FLAGS },
    { "lmctl", "set language model set",                        OFFSET(lmctl),  AV_OPT_TYPE_STRING, {.str=NULL},              .flags = FLAGS },
    { "lmname","set which language model to use",               OFFSET(lmname), AV_OPT_TYPE_STRING, {.str=NULL},              .flags = FLAGS },
    { "logfn", "set output for log messages",                   OFFSET(logfn),  AV_OPT_TYPE_STRING, {.str="/dev/null"},       .flags = FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(asr);

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVDictionary **metadata = &in->metadata;
    ASRContext *s = ctx->priv;
    int have_speech;
    const char *speech;

    ps_process_raw(s->ps, (const int16_t *)in->data[0], in->nb_samples, 0, 0);
    have_speech = ps_get_in_speech(s->ps);
    if (have_speech && !s->utt_started)
        s->utt_started = 1;
    if (!have_speech && s->utt_started) {
        ps_end_utt(s->ps);
        speech = ps_get_hyp(s->ps, NULL);
        if (speech != NULL)
            av_dict_set(metadata, "lavfi.asr.text", speech, 0);
        ps_start_utt(s->ps);
        s->utt_started = 0;
    }

    return ff_filter_frame(ctx->outputs[0], in);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ASRContext *s = ctx->priv;

    ps_start_utt(s->ps);

    return 0;
}

static av_cold int asr_init(AVFilterContext *ctx)
{
    ASRContext *s = ctx->priv;
    const float frate = s->rate;
    char *rate = av_asprintf("%f", frate);
    const char *argv[] = { "-logfn",    s->logfn,
                           "-hmm",      s->hmm,
                           "-lm",       s->lm,
                           "-lmctl",    s->lmctl,
                           "-lmname",   s->lmname,
                           "-dict",     s->dict,
                           "-samprate", rate,
                           NULL };

    s->config = cmd_ln_parse_r(NULL, ps_args(), 14, (char **)argv, 0);
    av_free(rate);
    if (!s->config)
        return AVERROR(ENOMEM);

    ps_default_search_args(s->config);
    s->ps = ps_init(s->config);
    if (!s->ps)
        return AVERROR(ENOMEM);

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    ASRContext *s = ctx->priv;
    int sample_rates[] = { s->rate, -1 };
    int ret;

    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layout = NULL;

    if ((ret = ff_add_format                 (&formats, AV_SAMPLE_FMT_S16                 )) < 0 ||
        (ret = ff_set_common_formats         (ctx     , formats                           )) < 0 ||
        (ret = ff_add_channel_layout         (&layout , &(AVChannelLayout)AV_CHANNEL_LAYOUT_MONO )) < 0 ||
        (ret = ff_set_common_channel_layouts (ctx     , layout                            )) < 0 ||
        (ret = ff_set_common_samplerates_from_list(ctx, sample_rates     )) < 0)
        return ret;

    return 0;
}

static av_cold void asr_uninit(AVFilterContext *ctx)
{
    ASRContext *s = ctx->priv;

    ps_free(s->ps);
    s->ps = NULL;
    cmd_ln_free_r(s->config);
    s->config = NULL;
}

static const AVFilterPad asr_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
};

static const AVFilterPad asr_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
};

const AVFilter ff_af_asr = {
    .name          = "asr",
    .description   = NULL_IF_CONFIG_SMALL("Automatic Speech Recognition."),
    .priv_size     = sizeof(ASRContext),
    .priv_class    = &asr_class,
    .init          = asr_init,
    .uninit        = asr_uninit,
    .flags         = AVFILTER_FLAG_METADATA_ONLY,
    FILTER_INPUTS(asr_inputs),
    FILTER_OUTPUTS(asr_outputs),
    FILTER_QUERY_FUNC(query_formats),
};
