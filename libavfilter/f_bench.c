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

#include "libavutil/opt.h"
#include "libavutil/time.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"

enum BenchAction {
    ACTION_START,
    ACTION_STOP,
    NB_ACTION
};

typedef struct {
    const AVClass *class;
    int action;
    int64_t max, min;
    int64_t sum;
    int n;
} BenchContext;

#define OFFSET(x) offsetof(BenchContext, x)
#define DEFINE_OPTIONS(filt_name, FLAGS)                                                                                \
static const AVOption filt_name##_options[] = {                                                                         \
    { "action", "set action", OFFSET(action), AV_OPT_TYPE_INT, {.i64=ACTION_START}, 0, NB_ACTION-1, FLAGS, "action" },  \
        { "start", "start timer",  0, AV_OPT_TYPE_CONST, {.i64=ACTION_START}, INT_MIN, INT_MAX, FLAGS, "action" },      \
        { "stop",  "stop timer",   0, AV_OPT_TYPE_CONST, {.i64=ACTION_STOP},  INT_MIN, INT_MAX, FLAGS, "action" },      \
    { NULL }                                                                                                            \
}

#define START_TIME_KEY "lavfi.bench.start_time"
#define T2F(v) ((v) / 1000000.)

static av_cold int init(AVFilterContext *ctx)
{
    BenchContext *s = ctx->priv;
    s->min = INT64_MAX;
    s->max = INT64_MIN;
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    BenchContext *s = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];
    const int64_t t = av_gettime();

    if (t < 0)
        return ff_filter_frame(outlink, in);

    if (s->action == ACTION_START) {
        av_dict_set_int(&in->metadata, START_TIME_KEY, t, 0);
    } else if (s->action == ACTION_STOP) {
        AVDictionaryEntry *e = av_dict_get(in->metadata, START_TIME_KEY, NULL, 0);
        if (e) {
            const int64_t start = strtoll(e->value, NULL, 0);
            const int64_t diff = t - start;
            s->sum += diff;
            s->n++;
            s->min = FFMIN(s->min, diff);
            s->max = FFMAX(s->max, diff);
            av_log(s, AV_LOG_INFO, "t:%f avg:%f max:%f min:%f\n",
                   T2F(diff), T2F(s->sum / s->n), T2F(s->max), T2F(s->min));
        }
        av_dict_set(&in->metadata, START_TIME_KEY, NULL, 0);
    }

    return ff_filter_frame(outlink, in);
}

#if CONFIG_BENCH_FILTER
DEFINE_OPTIONS(bench, AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM);
AVFILTER_DEFINE_CLASS(bench);

static const AVFilterPad bench_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad bench_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_bench = {
    .name          = "bench",
    .description   = NULL_IF_CONFIG_SMALL("Benchmark part of a filtergraph."),
    .priv_size     = sizeof(BenchContext),
    .init          = init,
    .inputs        = bench_inputs,
    .outputs       = bench_outputs,
    .priv_class    = &bench_class,
};
#endif /* CONFIG_BENCH_FILTER */

#if CONFIG_ABENCH_FILTER
DEFINE_OPTIONS(abench, AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_AUDIO_PARAM);
AVFILTER_DEFINE_CLASS(abench);

static const AVFilterPad abench_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad abench_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_AUDIO,
    },
    { NULL }
};

AVFilter ff_af_abench = {
    .name          = "abench",
    .description   = NULL_IF_CONFIG_SMALL("Benchmark part of a filtergraph."),
    .priv_size     = sizeof(BenchContext),
    .init          = init,
    .inputs        = abench_inputs,
    .outputs       = abench_outputs,
    .priv_class    = &abench_class,
};
#endif /* CONFIG_ABENCH_FILTER */
