/*
 * Copyright (c) 2002 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2013 Paul B Mahol
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

/**
 * @file
 * noise generator
 */

#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/lfg.h"
#include "libavutil/parseutils.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "vf_noise.h"
#include "video.h"

typedef struct ThreadData {
    AVFrame *in, *out;
} ThreadData;

#define OFFSET(x) offsetof(NoiseContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

#define NOISE_PARAMS(name, x, param)                                                                                             \
    {#name"_seed", "set component #"#x" noise seed", OFFSET(param.seed), AV_OPT_TYPE_INT, {.i64=-1}, -1, INT_MAX, FLAGS},        \
    {#name"_strength", "set component #"#x" strength", OFFSET(param.strength), AV_OPT_TYPE_INT, {.i64=0}, 0, 100, FLAGS},        \
    {#name"s",         "set component #"#x" strength", OFFSET(param.strength), AV_OPT_TYPE_INT, {.i64=0}, 0, 100, FLAGS},        \
    {#name"_flags", "set component #"#x" flags", OFFSET(param.flags), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, 31, FLAGS, #name"_flags"}, \
    {#name"f", "set component #"#x" flags", OFFSET(param.flags), AV_OPT_TYPE_FLAGS, {.i64=0}, 0, 31, FLAGS, #name"_flags"},      \
    {"a", "averaged noise", 0, AV_OPT_TYPE_CONST, {.i64=NOISE_AVERAGED}, 0, 0, FLAGS, #name"_flags"},                            \
    {"p", "(semi)regular pattern", 0, AV_OPT_TYPE_CONST, {.i64=NOISE_PATTERN},  0, 0, FLAGS, #name"_flags"},                     \
    {"t", "temporal noise", 0, AV_OPT_TYPE_CONST, {.i64=NOISE_TEMPORAL}, 0, 0, FLAGS, #name"_flags"},                            \
    {"u", "uniform noise",  0, AV_OPT_TYPE_CONST, {.i64=NOISE_UNIFORM},  0, 0, FLAGS, #name"_flags"},

static const AVOption noise_options[] = {
    NOISE_PARAMS(all, 0, all)
    NOISE_PARAMS(c0,  0, param[0])
    NOISE_PARAMS(c1,  1, param[1])
    NOISE_PARAMS(c2,  2, param[2])
    NOISE_PARAMS(c3,  3, param[3])
    {NULL}
};

AVFILTER_DEFINE_CLASS(noise);

static const int8_t patt[4] = { -1, 0, 1, 0 };

#define RAND_N(range) ((int) ((double) range * av_lfg_get(lfg) / (UINT_MAX + 1.0)))
static av_cold int init_noise(NoiseContext *n, int comp)
{
    int8_t *noise = av_malloc(MAX_NOISE * sizeof(int8_t));
    FilterParams *fp = &n->param[comp];
    AVLFG *lfg = &n->param[comp].lfg;
    int strength = fp->strength;
    int flags = fp->flags;
    int i, j;

    if (!noise)
        return AVERROR(ENOMEM);

    av_lfg_init(&fp->lfg, fp->seed + comp*31415U);

    for (i = 0, j = 0; i < MAX_NOISE; i++, j++) {
        if (flags & NOISE_UNIFORM) {
            if (flags & NOISE_AVERAGED) {
                if (flags & NOISE_PATTERN) {
                    noise[i] = (RAND_N(strength) - strength / 2) / 6
                        + patt[j % 4] * strength * 0.25 / 3;
                } else {
                    noise[i] = (RAND_N(strength) - strength / 2) / 3;
                }
            } else {
                if (flags & NOISE_PATTERN) {
                    noise[i] = (RAND_N(strength) - strength / 2) / 2
                        + patt[j % 4] * strength * 0.25;
                } else {
                    noise[i] = RAND_N(strength) - strength / 2;
                }
            }
        } else {
            double x1, x2, w, y1;
            do {
                x1 = 2.0 * av_lfg_get(lfg) / (float)UINT_MAX - 1.0;
                x2 = 2.0 * av_lfg_get(lfg) / (float)UINT_MAX - 1.0;
                w = x1 * x1 + x2 * x2;
            } while (w >= 1.0);

            w   = sqrt((-2.0 * log(w)) / w);
            y1  = x1 * w;
            y1 *= strength / sqrt(3.0);
            if (flags & NOISE_PATTERN) {
                y1 /= 2;
                y1 += patt[j % 4] * strength * 0.35;
            }
            y1 = av_clipf(y1, -128, 127);
            if (flags & NOISE_AVERAGED)
                y1 /= 3.0;
            noise[i] = (int)y1;
        }
        if (RAND_N(6) == 0)
            j--;
    }

    for (i = 0; i < MAX_RES; i++)
        for (j = 0; j < 3; j++)
            fp->prev_shift[i][j] = noise + (av_lfg_get(lfg) & (MAX_SHIFT - 1));

    fp->noise = noise;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    AVFilterFormats *formats = NULL;
    int fmt, ret;

    for (fmt = 0; av_pix_fmt_desc_get(fmt); fmt++) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
        if (desc->flags & AV_PIX_FMT_FLAG_PLANAR && !(desc->comp[0].depth & 7)
            && (ret = ff_add_format(&formats, fmt)) < 0)
                return ret;
    }

    return ff_set_common_formats(ctx, formats);
}

static int config_input(AVFilterLink *inlink)
{
    NoiseContext *n = inlink->dst->priv;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    int ret;

    n->nb_planes = av_pix_fmt_count_planes(inlink->format);

    if ((ret = av_image_fill_linesizes(n->bytewidth, inlink->format, inlink->w)) < 0)
        return ret;

    n->height[1] = n->height[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    n->height[0] = n->height[3] = inlink->h;

    return 0;
}

void ff_line_noise_c(uint8_t *dst, const uint8_t *src, const int8_t *noise,
                     int len, int shift)
{
    int i;

    noise += shift;
    for (i = 0; i < len; i++) {
        int v = src[i] + noise[i];

        dst[i] = av_clip_uint8(v);
    }
}

void ff_line_noise_avg_c(uint8_t *dst, const uint8_t *src,
                         int len, const int8_t * const *shift)
{
    int i;
    const int8_t *src2 = (const int8_t*)src;

    for (i = 0; i < len; i++) {
        const int n = shift[0][i] + shift[1][i] + shift[2][i];
        dst[i] = src2[i] + ((n * src2[i]) >> 7);
    }
}

static void noise(uint8_t *dst, const uint8_t *src,
                  int dst_linesize, int src_linesize,
                  int width, int start, int end, NoiseContext *n, int comp)
{
    FilterParams *p = &n->param[comp];
    int8_t *noise = p->noise;
    const int flags = p->flags;
    int y;

    if (!noise) {
        if (dst != src)
            av_image_copy_plane(dst, dst_linesize, src, src_linesize, width, end - start);
        return;
    }

    for (y = start; y < end; y++) {
        const int ix = y & (MAX_RES - 1);
        int x;
        for (x=0; x < width; x+= MAX_RES) {
            int w = FFMIN(width - x, MAX_RES);
            int shift = p->rand_shift[ix];

            if (flags & NOISE_AVERAGED) {
                n->line_noise_avg(dst + x, src + x, w, (const int8_t**)p->prev_shift[ix]);
                p->prev_shift[ix][shift & 3] = noise + shift;
            } else {
                n->line_noise(dst + x, src + x, noise, w, shift);
            }
        }
        dst += dst_linesize;
        src += src_linesize;
    }
}

static int filter_slice(AVFilterContext *ctx, void *arg, int jobnr, int nb_jobs)
{
    NoiseContext *s = ctx->priv;
    ThreadData *td = arg;
    int plane;

    for (plane = 0; plane < s->nb_planes; plane++) {
        const int height = s->height[plane];
        const int start  = (height *  jobnr   ) / nb_jobs;
        const int end    = (height * (jobnr+1)) / nb_jobs;
        noise(td->out->data[plane] + start * td->out->linesize[plane],
              td->in->data[plane]  + start * td->in->linesize[plane],
              td->out->linesize[plane], td->in->linesize[plane],
              s->bytewidth[plane], start, end, s, plane);
    }
    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *inpicref)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    NoiseContext *n = ctx->priv;
    ThreadData td;
    AVFrame *out;
    int comp, i;

    if (av_frame_is_writable(inpicref)) {
        out = inpicref;
    } else {
        out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        if (!out) {
            av_frame_free(&inpicref);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, inpicref);
    }

    for (comp = 0; comp < 4; comp++) {
        FilterParams *fp = &n->param[comp];

        if ((!fp->rand_shift_init || (fp->flags & NOISE_TEMPORAL)) && fp->strength) {

            for (i = 0; i < MAX_RES; i++) {
                fp->rand_shift[i] = av_lfg_get(&fp->lfg) & (MAX_SHIFT - 1);
            }
            fp->rand_shift_init = 1;
        }
    }

    td.in = inpicref; td.out = out;
    ctx->internal->execute(ctx, filter_slice, &td, NULL, FFMIN(n->height[0], ctx->graph->nb_threads));
    emms_c();

    if (inpicref != out)
        av_frame_free(&inpicref);
    return ff_filter_frame(outlink, out);
}

static av_cold int init(AVFilterContext *ctx)
{
    NoiseContext *n = ctx->priv;
    int ret, i;

    for (i = 0; i < 4; i++) {
        if (n->all.seed >= 0)
            n->param[i].seed = n->all.seed;
        else
            n->param[i].seed = 123457;
        if (n->all.strength)
            n->param[i].strength = n->all.strength;
        if (n->all.flags)
            n->param[i].flags = n->all.flags;
    }

    for (i = 0; i < 4; i++) {
        if (n->param[i].strength && ((ret = init_noise(n, i)) < 0))
            return ret;
    }

    n->line_noise     = ff_line_noise_c;
    n->line_noise_avg = ff_line_noise_avg_c;

    if (ARCH_X86)
        ff_noise_init_x86(n);

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    NoiseContext *n = ctx->priv;
    int i;

    for (i = 0; i < 4; i++)
        av_freep(&n->param[i].noise);
}

static const AVFilterPad noise_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad noise_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_noise = {
    .name          = "noise",
    .description   = NULL_IF_CONFIG_SMALL("Add noise."),
    .priv_size     = sizeof(NoiseContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = noise_inputs,
    .outputs       = noise_outputs,
    .priv_class    = &noise_class,
    .flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC | AVFILTER_FLAG_SLICE_THREADS,
};
