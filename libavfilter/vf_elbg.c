/*
 * Copyright (c) 2013 Stefano Sabatini
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
 * video quantizer filter based on ELBG
 */

#include "libavcodec/elbg.h"
#include "libavutil/mem.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/random_seed.h"

#include "avfilter.h"
#include "drawutils.h"
#include "filters.h"
#include "formats.h"
#include "video.h"

typedef struct ELBGFilterContext {
    const AVClass *class;
    struct ELBGContext *ctx;
    AVLFG lfg;
    int64_t lfg_seed;
    int max_steps_nb;
    int *codeword;
    int codeword_length;
    int *codeword_closest_codebook_idxs;
    int *codebook;
    int codebook_length;
    const AVPixFmtDescriptor *pix_desc;
    uint8_t rgba_map[4];
    int use_alpha;
    int pal8;
} ELBGFilterContext;

#define OFFSET(x) offsetof(ELBGFilterContext, x)
#define FLAGS AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption elbg_options[] = {
    { "codebook_length", "set codebook length", OFFSET(codebook_length), AV_OPT_TYPE_INT, { .i64 = 256 }, 1, INT_MAX, FLAGS },
    { "l",               "set codebook length", OFFSET(codebook_length), AV_OPT_TYPE_INT, { .i64 = 256 }, 1, INT_MAX, FLAGS },
    { "nb_steps", "set max number of steps used to compute the mapping", OFFSET(max_steps_nb), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, INT_MAX, FLAGS },
    { "n",        "set max number of steps used to compute the mapping", OFFSET(max_steps_nb), AV_OPT_TYPE_INT, { .i64 = 1 }, 1, INT_MAX, FLAGS },
    { "seed", "set the random seed", OFFSET(lfg_seed), AV_OPT_TYPE_INT64, {.i64 = -1}, -1, UINT32_MAX, FLAGS },
    { "s",    "set the random seed", OFFSET(lfg_seed), AV_OPT_TYPE_INT64, { .i64 = -1 }, -1, UINT32_MAX, FLAGS },
    { "pal8", "set the pal8 output", OFFSET(pal8), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, FLAGS },
    { "use_alpha", "use alpha channel for mapping", OFFSET(use_alpha), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(elbg);

static av_cold int init(AVFilterContext *ctx)
{
    ELBGFilterContext *const elbg = ctx->priv;

    if (elbg->pal8 && elbg->codebook_length > 256) {
        av_log(ctx, AV_LOG_ERROR, "pal8 output allows max 256 codebook length.\n");
        return AVERROR(EINVAL);
    }

    if (elbg->lfg_seed == -1)
        elbg->lfg_seed = av_get_random_seed();

    av_lfg_init(&elbg->lfg, elbg->lfg_seed);
    return 0;
}

static int query_formats(const AVFilterContext *ctx,
                         AVFilterFormatsConfig **cfg_in,
                         AVFilterFormatsConfig **cfg_out)
{
    const ELBGFilterContext *const elbg = ctx->priv;
    int ret;

    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_ARGB, AV_PIX_FMT_RGBA, AV_PIX_FMT_ABGR, AV_PIX_FMT_BGRA,
        AV_PIX_FMT_RGB24, AV_PIX_FMT_BGR24,
        AV_PIX_FMT_NONE
    };
    if (!elbg->pal8) {
        return ff_set_common_formats_from_list2(ctx, cfg_in, cfg_out, pix_fmts);
    } else {
        static const enum AVPixelFormat pal8_fmt[] = {
            AV_PIX_FMT_PAL8,
            AV_PIX_FMT_NONE
        };
        if ((ret = ff_formats_ref(ff_make_format_list(pix_fmts), &cfg_in[0]->formats)) < 0 ||
            (ret = ff_formats_ref(ff_make_format_list(pal8_fmt), &cfg_out[0]->formats)) < 0)
            return ret;
    }
    return 0;
}

#define NB_COMPONENTS 4

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    ELBGFilterContext *const elbg = ctx->priv;

    elbg->pix_desc = av_pix_fmt_desc_get(inlink->format);
    elbg->codeword_length = inlink->w * inlink->h;
    elbg->codeword = av_realloc_f(elbg->codeword, elbg->codeword_length,
                                  NB_COMPONENTS * sizeof(*elbg->codeword));
    if (!elbg->codeword)
        return AVERROR(ENOMEM);

    elbg->codeword_closest_codebook_idxs =
        av_realloc_f(elbg->codeword_closest_codebook_idxs, elbg->codeword_length,
                     sizeof(*elbg->codeword_closest_codebook_idxs));
    if (!elbg->codeword_closest_codebook_idxs)
        return AVERROR(ENOMEM);

    elbg->codebook = av_realloc_f(elbg->codebook, elbg->codebook_length,
                                  NB_COMPONENTS * sizeof(*elbg->codebook));
    if (!elbg->codebook)
        return AVERROR(ENOMEM);

    ff_fill_rgba_map(elbg->rgba_map, inlink->format);

    return 0;
}

#define R 0
#define G 1
#define B 2
#define A 3

static int filter_frame(AVFilterLink *inlink, AVFrame *frame)
{
    ELBGFilterContext *const elbg = inlink->dst->priv;
    int i, j, k, ret;
    uint8_t *p, *p0;

    const uint8_t r_idx  = elbg->rgba_map[R];
    const uint8_t g_idx  = elbg->rgba_map[G];
    const uint8_t b_idx  = elbg->rgba_map[B];
    const uint8_t a_idx  = elbg->rgba_map[A];

    /* build the codeword */
    p0 = frame->data[0];
    k = 0;
    for (i = 0; i < inlink->h; i++) {
        p = p0;
        for (j = 0; j < inlink->w; j++) {
            elbg->codeword[k++] = p[b_idx];
            elbg->codeword[k++] = p[g_idx];
            elbg->codeword[k++] = p[r_idx];
            elbg->codeword[k++] = elbg->use_alpha ? p[a_idx] : 0xff;
            p += elbg->pix_desc->nb_components;
        }
        p0 += frame->linesize[0];
    }

    /* compute the codebook */
    ret = avpriv_elbg_do(&elbg->ctx, elbg->codeword, NB_COMPONENTS,
                         elbg->codeword_length, elbg->codebook,
                         elbg->codebook_length, elbg->max_steps_nb,
                         elbg->codeword_closest_codebook_idxs, &elbg->lfg, 0);
    if (ret < 0) {
        av_frame_free(&frame);
        return ret;
    }

    if (elbg->pal8) {
        AVFilterLink *outlink = inlink->dst->outputs[0];
        AVFrame *out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
        uint32_t *pal;

        if (!out) {
            av_frame_free(&frame);
            return AVERROR(ENOMEM);
        }
        av_frame_copy_props(out, frame);
        av_frame_free(&frame);
        pal = (uint32_t *)out->data[1];
        p0 = (uint8_t *)out->data[0];

        for (i = 0; i < elbg->codebook_length; i++) {
            const unsigned al =  elbg->use_alpha ? elbg->codebook[i*4+3] : 0xff;
            pal[i] =  al                    << 24  |
                     (elbg->codebook[i*4+2] << 16) |
                     (elbg->codebook[i*4+1] <<  8) |
                      elbg->codebook[i*4  ];
        }

        k = 0;
        for (i = 0; i < inlink->h; i++) {
            p = p0;
            for (j = 0; j < inlink->w; j++, p++) {
                p[0] = elbg->codeword_closest_codebook_idxs[k++];
            }
            p0 += out->linesize[0];
        }

        return ff_filter_frame(outlink, out);
    }

    /* fill the output with the codebook values */
    p0 = frame->data[0];

    k = 0;
    for (i = 0; i < inlink->h; i++) {
        p = p0;
        for (j = 0; j < inlink->w; j++) {
            int cb_idx = NB_COMPONENTS * elbg->codeword_closest_codebook_idxs[k++];
            p[b_idx] = elbg->codebook[cb_idx];
            p[g_idx] = elbg->codebook[cb_idx+1];
            p[r_idx] = elbg->codebook[cb_idx+2];
            p[a_idx] = elbg->use_alpha ? elbg->codebook[cb_idx+3] : 0xFFu;
            p += elbg->pix_desc->nb_components;
        }
        p0 += frame->linesize[0];
    }

    return ff_filter_frame(inlink->dst->outputs[0], frame);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    ELBGFilterContext *const elbg = ctx->priv;

    avpriv_elbg_free(&elbg->ctx);

    av_freep(&elbg->codebook);
    av_freep(&elbg->codeword);
    av_freep(&elbg->codeword_closest_codebook_idxs);
}

static const AVFilterPad elbg_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_VIDEO,
        .flags          = AVFILTERPAD_FLAG_NEEDS_WRITABLE,
        .config_props   = config_input,
        .filter_frame   = filter_frame,
    },
};

const FFFilter ff_vf_elbg = {
    .p.name        = "elbg",
    .p.description = NULL_IF_CONFIG_SMALL("Apply posterize effect, using the ELBG algorithm."),
    .p.priv_class  = &elbg_class,
    .priv_size     = sizeof(ELBGFilterContext),
    .init          = init,
    .uninit        = uninit,
    FILTER_INPUTS(elbg_inputs),
    FILTER_OUTPUTS(ff_video_default_filterpad),
    FILTER_QUERY_FUNC2(query_formats),
};
