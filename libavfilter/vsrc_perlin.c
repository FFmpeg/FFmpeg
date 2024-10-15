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

/**
 * @file
 * Perlin noise generator
 */

#include <float.h>

#include "perlin.h"
#include "libavutil/lfg.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

typedef struct PerlinContext {
    const AVClass *class;

    int w, h;
    AVRational frame_rate;

    FFPerlin perlin;
    int octaves;
    double persistence;
    unsigned int random_seed;
    enum FFPerlinRandomMode random_mode;

    double xscale, yscale, tscale;
    uint64_t pts;
} PerlinContext;

#define OFFSET(x) offsetof(PerlinContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption perlin_options[] = {
    { "size",     "set video size", OFFSET(w),          AV_OPT_TYPE_IMAGE_SIZE, {.str="320x240"}, 0, 0, FLAGS },
    { "s",        "set video size", OFFSET(w),          AV_OPT_TYPE_IMAGE_SIZE, {.str="320x240"}, 0, 0, FLAGS },
    { "rate",     "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "r",        "set video rate", OFFSET(frame_rate), AV_OPT_TYPE_VIDEO_RATE, {.str="25"}, 0, INT_MAX, FLAGS },
    { "octaves", "set the number of components to use to generate the noise", OFFSET(octaves), AV_OPT_TYPE_INT, {.i64=1}, 1, INT_MAX, FLAGS },
    { "persistence", "set the octaves persistence", OFFSET(persistence), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.0, DBL_MAX, FLAGS },

    { "xscale", "set x-scale factor", OFFSET(xscale), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.0, DBL_MAX, FLAGS },
    { "yscale", "set y-scale factor", OFFSET(yscale), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.0, DBL_MAX, FLAGS },
    { "tscale", "set t-scale factor", OFFSET(tscale), AV_OPT_TYPE_DOUBLE, {.dbl=1}, 0.0, DBL_MAX, FLAGS },

    { "random_mode", "set random mode used to compute initial pattern", OFFSET(random_mode), AV_OPT_TYPE_INT, {.i64=FF_PERLIN_RANDOM_MODE_RANDOM}, 0, FF_PERLIN_RANDOM_MODE_NB-1, FLAGS, .unit = "random_mode" },
    { "random", "compute and use random seed", 0, AV_OPT_TYPE_CONST, {.i64=FF_PERLIN_RANDOM_MODE_RANDOM},   0, 0, FLAGS, .unit = "random_mode" },
    { "ken", "use the predefined initial pattern defined by Ken Perlin in the original article", 0, AV_OPT_TYPE_CONST, {.i64=FF_PERLIN_RANDOM_MODE_KEN}, 0, 0, FLAGS, .unit = "random_mode" },
    { "seed", "use the value specified by random_seed", 0, AV_OPT_TYPE_CONST, {.i64=FF_PERLIN_RANDOM_MODE_SEED}, 0, 0, FLAGS, .unit="random_mode" },

    { "random_seed", "set the seed for filling the initial pattern", OFFSET(random_seed), AV_OPT_TYPE_UINT, {.i64=0}, 0, UINT_MAX, FLAGS },
    { "seed",        "set the seed for filling the initial pattern", OFFSET(random_seed), AV_OPT_TYPE_UINT, {.i64=0}, 0, UINT_MAX, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(perlin);

static av_cold int init(AVFilterContext *ctx)
{
    PerlinContext *perlin = ctx->priv;
    int ret;

    if (ret = ff_perlin_init(&perlin->perlin, -1, perlin->octaves, perlin->persistence,
                             perlin->random_mode, perlin->random_seed)) {
        return ret;
    }

    av_log(ctx, AV_LOG_VERBOSE,
           "s:%dx%d r:%d/%d octaves:%d persistence:%f xscale:%f yscale:%f tscale:%f\n",
           perlin->w, perlin->h, perlin->frame_rate.num, perlin->frame_rate.den,
           perlin->octaves, perlin->persistence,
           perlin->xscale, perlin->yscale, perlin->tscale);
    return 0;
}

static int config_props(AVFilterLink *outlink)
{
    PerlinContext *perlin = outlink->src->priv;
    FilterLink *l = ff_filter_link(outlink);

    outlink->w = perlin->w;
    outlink->h = perlin->h;
    outlink->time_base = av_inv_q(perlin->frame_rate);
    l->frame_rate = perlin->frame_rate;

    return 0;
}

static int request_frame(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    PerlinContext *perlin = ctx->priv;
    AVFrame *picref = ff_get_video_buffer(outlink, perlin->w, perlin->h);
    int i, j;
    uint8_t *data0, *data;
    double x, y, t;

    if (!picref)
        return AVERROR(ENOMEM);

    picref->sample_aspect_ratio = (AVRational) {1, 1};
    picref->pts = perlin->pts++;
    picref->duration = 1;

    t = perlin->tscale * (perlin->pts * av_q2d(outlink->time_base));
    data0 = picref->data[0];

    for (i = 0; i < perlin->h; i++) {
        y = perlin->yscale * (double)i / perlin->h;

        data = data0;

        for (j = 0; j < perlin->w; j++) {
            double res;
            x = perlin->xscale * (double)j / perlin->w;
            res = ff_perlin_get(&perlin->perlin, x, y, t);
            av_log(ctx, AV_LOG_DEBUG, "x:%f y:%f t:%f => %f\n", x, y, t, res);
            *data++ = res * 255;
        }
        data0 += picref->linesize[0];
    }

    return ff_filter_frame(outlink, picref);
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };

    return ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, pix_fmts);
}

static const AVFilterPad perlin_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .request_frame = request_frame,
        .config_props  = config_props,
    },
};

const AVFilter ff_vsrc_perlin = {
    .name          = "perlin",
    .description   = NULL_IF_CONFIG_SMALL("Generate Perlin noise"),
    .priv_size     = sizeof(PerlinContext),
    .priv_class    = &perlin_class,
    .init          = init,
    .inputs        = NULL,
    FILTER_OUTPUTS(perlin_outputs),
    FILTER_QUERY_FUNC2(query_formats),
};
