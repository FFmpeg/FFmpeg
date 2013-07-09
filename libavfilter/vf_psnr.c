/*
 * Copyright (c) 2011 Roger Pau Monné <roger.pau@entel.upc.edu>
 * Copyright (c) 2011 Stefano Sabatini
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
 * Caculate the PSNR between two input videos.
 */

#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "dualinput.h"
#include "drawutils.h"
#include "formats.h"
#include "internal.h"
#include "video.h"

typedef struct PSNRContext {
    const AVClass *class;
    FFDualInputContext dinput;
    double mse, min_mse, max_mse;
    uint64_t nb_frames;
    FILE *stats_file;
    char *stats_file_str;
    int max[4], average_max;
    int is_rgb;
    uint8_t rgba_map[4];
    char comps[4];
    const AVPixFmtDescriptor *desc;
} PSNRContext;

#define OFFSET(x) offsetof(PSNRContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption psnr_options[] = {
    {"stats_file", "Set file where to store per-frame difference information", OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    {"f",          "Set file where to store per-frame difference information", OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL },
};

AVFILTER_DEFINE_CLASS(psnr);

static inline int pow2(int base)
{
    return base*base;
}

static inline double get_psnr(double mse, uint64_t nb_frames, int max)
{
    return 10.0 * log(pow2(max) / (mse / nb_frames)) / log(10.0);
}

static inline
void compute_images_mse(const uint8_t *main_data[4], const int main_linesizes[4],
                        const uint8_t *ref_data[4], const int ref_linesizes[4],
                        int w, int h, const AVPixFmtDescriptor *desc,
                        double mse[4])
{
    int i, c, j;

    for (c = 0; c < desc->nb_components; c++) {
        int hsub = c == 1 || c == 2 ? desc->log2_chroma_w : 0;
        int vsub = c == 1 || c == 2 ? desc->log2_chroma_h : 0;
        const int outw = FF_CEIL_RSHIFT(w, hsub);
        const int outh = FF_CEIL_RSHIFT(h, vsub);
        const uint8_t *main_line = main_data[c];
        const uint8_t *ref_line = ref_data[c];
        const int ref_linesize = ref_linesizes[c];
        const int main_linesize = main_linesizes[c];
        int m = 0;

        for (i = 0; i < outh; i++) {
            for (j = 0; j < outw; j++)
                m += pow2(main_line[j] - ref_line[j]);
            ref_line += ref_linesize;
            main_line += main_linesize;
        }
        mse[c] = m / (outw * outh);
    }
}

static void set_meta(AVDictionary **metadata, const char *key, char comp, float d)
{
    char value[128];
    snprintf(value, sizeof(value), "%0.2f", d);
    if (comp) {
        char key2[128];
        snprintf(key2, sizeof(key2), "%s%c", key, comp);
        av_dict_set(metadata, key2, value, 0);
    } else {
        av_dict_set(metadata, key, value, 0);
    }
}

static AVFrame *do_psnr(AVFilterContext *ctx, AVFrame *main,
                        const AVFrame *ref)
{
    PSNRContext *s = ctx->priv;
    double comp_mse[4], mse = 0;
    int j, c;
    AVDictionary **metadata = avpriv_frame_get_metadatap(main);

    compute_images_mse((const uint8_t **)main->data, main->linesize,
                       (const uint8_t **)ref->data, ref->linesize,
                       main->width, main->height, s->desc, comp_mse);

    for (j = 0; j < s->desc->nb_components; j++)
        mse += comp_mse[j];
    mse /= s->desc->nb_components;

    s->min_mse = FFMIN(s->min_mse, mse);
    s->max_mse = FFMAX(s->max_mse, mse);

    s->mse += mse;
    s->nb_frames++;

    for (j = 0; j < s->desc->nb_components; j++) {
        c = s->is_rgb ? s->rgba_map[j] : j;
        set_meta(metadata, "lavfi.psnr.mse.", s->comps[j], comp_mse[c]);
        set_meta(metadata, "lavfi.psnr.mse_avg", 0, mse);
        set_meta(metadata, "lavfi.psnr.psnr.", s->comps[j], get_psnr(comp_mse[c], 1, s->max[c]));
        set_meta(metadata, "lavfi.psnr.psnr_avg", 0, get_psnr(mse, 1, s->average_max));
    }

    if (s->stats_file) {
        fprintf(s->stats_file, "n:%"PRId64" mse_avg:%0.2f ", s->nb_frames, mse);
        for (j = 0; j < s->desc->nb_components; j++) {
            c = s->is_rgb ? s->rgba_map[j] : j;
            fprintf(s->stats_file, "mse_%c:%0.2f ", s->comps[j], comp_mse[c]);
        }
        for (j = 0; j < s->desc->nb_components; j++) {
            c = s->is_rgb ? s->rgba_map[j] : j;
            fprintf(s->stats_file, "psnr_%c:%0.2f ", s->comps[j],
                    get_psnr(comp_mse[c], 1, s->max[c]));
        }
        fprintf(s->stats_file, "\n");
    }

    return main;
}

static av_cold int init(AVFilterContext *ctx)
{
    PSNRContext *s = ctx->priv;

    s->min_mse = +INFINITY;
    s->max_mse = -INFINITY;

    if (s->stats_file_str) {
        s->stats_file = fopen(s->stats_file_str, "w");
        if (!s->stats_file) {
            int err = AVERROR(errno);
            char buf[128];
            av_strerror(err, buf, sizeof(buf));
            av_log(ctx, AV_LOG_ERROR, "Could not open stats file %s: %s\n",
                   s->stats_file_str, buf);
            return err;
        }
    }

    s->dinput.process = do_psnr;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum PixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRAP,
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV422P,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ411P,
        AV_PIX_FMT_YUVA444P, AV_PIX_FMT_YUVA422P, AV_PIX_FMT_YUVA420P,
        AV_PIX_FMT_NONE
    };

    ff_set_common_formats(ctx, ff_make_format_list(pix_fmts));
    return 0;
}

static int config_input_ref(AVFilterLink *inlink)
{
    AVFilterContext *ctx  = inlink->dst;
    PSNRContext *s = ctx->priv;
    int j;

    s->desc = av_pix_fmt_desc_get(inlink->format);
    if (ctx->inputs[0]->w != ctx->inputs[1]->w ||
        ctx->inputs[0]->h != ctx->inputs[1]->h) {
        av_log(ctx, AV_LOG_ERROR, "Width and heigth of input videos must be same.\n");
        return AVERROR(EINVAL);
    }
    if (ctx->inputs[0]->format != ctx->inputs[1]->format) {
        av_log(ctx, AV_LOG_ERROR, "Inputs must be of same pixel format.\n");
        return AVERROR(EINVAL);
    }

    switch (inlink->format) {
    case AV_PIX_FMT_YUV410P:
    case AV_PIX_FMT_YUV411P:
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUV440P:
    case AV_PIX_FMT_YUV444P:
    case AV_PIX_FMT_YUVA420P:
    case AV_PIX_FMT_YUVA422P:
    case AV_PIX_FMT_YUVA444P:
        s->max[0] = 235;
        s->max[3] = 255;
        s->max[1] = s->max[2] = 240;
        break;
    default:
        s->max[0] = s->max[1] = s->max[2] = s->max[3] = 255;
    }

    s->is_rgb = ff_fill_rgba_map(s->rgba_map, inlink->format) >= 0;
    s->comps[0] = s->is_rgb ? 'r' : 'y' ;
    s->comps[1] = s->is_rgb ? 'g' : 'u' ;
    s->comps[2] = s->is_rgb ? 'b' : 'v' ;
    s->comps[3] = 'a';

    for (j = 0; j < s->desc->nb_components; j++)
        s->average_max += s->max[j];
    s->average_max /= s->desc->nb_components;

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AVFilterLink *mainlink = ctx->inputs[0];

    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    outlink->frame_rate = mainlink->frame_rate;

    return 0;
}

static int filter_frame_main(AVFilterLink *inlink, AVFrame *inpicref)
{
    PSNRContext *s = inlink->dst->priv;
    return ff_dualinput_filter_frame_main(&s->dinput, inlink, inpicref);
}

static int filter_frame_ref(AVFilterLink *inlink, AVFrame *inpicref)
{
    PSNRContext *s = inlink->dst->priv;
    return ff_dualinput_filter_frame_second(&s->dinput, inlink, inpicref);
}

static int request_frame(AVFilterLink *outlink)
{
    PSNRContext *s = outlink->src->priv;
    return ff_dualinput_request_frame(&s->dinput, outlink);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PSNRContext *s = ctx->priv;

    if (s->nb_frames > 0) {
        av_log(ctx, AV_LOG_INFO, "PSNR average:%0.2f min:%0.2f max:%0.2f\n",
               get_psnr(s->mse, s->nb_frames, s->average_max),
               get_psnr(s->max_mse, 1, s->average_max),
               get_psnr(s->min_mse, 1, s->average_max));
    }

    ff_dualinput_uninit(&s->dinput);

    if (s->stats_file)
        fclose(s->stats_file);
}

static const AVFilterPad psnr_inputs[] = {
    {
        .name             = "main",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame_main,
    },{
        .name             = "reference",
        .type             = AVMEDIA_TYPE_VIDEO,
        .filter_frame     = filter_frame_ref,
        .config_props     = config_input_ref,
    },
    { NULL }
};

static const AVFilterPad psnr_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
        .request_frame = request_frame,
    },
    { NULL }
};

AVFilter avfilter_vf_psnr = {
    .name           = "psnr",
    .description    = NULL_IF_CONFIG_SMALL("Calculate the PSNR between two video streams."),
    .init           = init,
    .uninit         = uninit,
    .query_formats  = query_formats,
    .priv_size      = sizeof(PSNRContext),
    .priv_class     = &psnr_class,
    .inputs         = psnr_inputs,
    .outputs        = psnr_outputs,
};
