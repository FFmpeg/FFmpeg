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

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "psnr.h"
#include "video.h"

typedef struct PSNRContext {
    const AVClass *class;
    FFFrameSync fs;
    double mse, min_mse, max_mse, mse_comp[4];
    uint64_t nb_frames;
    FILE *stats_file;
    char *stats_file_str;
    int stats_version;
    int stats_header_written;
    int stats_add_max;
    int max[4], average_max;
    int is_rgb;
    uint8_t rgba_map[4];
    char comps[4];
    int nb_components;
    int planewidth[4];
    int planeheight[4];
    double planeweight[4];
    PSNRDSPContext dsp;
} PSNRContext;

#define OFFSET(x) offsetof(PSNRContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption psnr_options[] = {
    {"stats_file", "Set file where to store per-frame difference information", OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    {"f",          "Set file where to store per-frame difference information", OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    {"stats_version", "Set the format version for the stats file.",               OFFSET(stats_version),  AV_OPT_TYPE_INT,    {.i64=1},    1, 2, FLAGS },
    {"output_max",  "Add raw stats (max values) to the output log.",            OFFSET(stats_add_max), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1, FLAGS},
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(psnr, PSNRContext, fs);

static inline unsigned pow_2(unsigned base)
{
    return base*base;
}

static inline double get_psnr(double mse, uint64_t nb_frames, int max)
{
    return 10.0 * log10(pow_2(max) / (mse / nb_frames));
}

static uint64_t sse_line_8bit(const uint8_t *main_line,  const uint8_t *ref_line, int outw)
{
    int j;
    unsigned m2 = 0;

    for (j = 0; j < outw; j++)
        m2 += pow_2(main_line[j] - ref_line[j]);

    return m2;
}

static uint64_t sse_line_16bit(const uint8_t *_main_line, const uint8_t *_ref_line, int outw)
{
    int j;
    uint64_t m2 = 0;
    const uint16_t *main_line = (const uint16_t *) _main_line;
    const uint16_t *ref_line = (const uint16_t *) _ref_line;

    for (j = 0; j < outw; j++)
        m2 += pow_2(main_line[j] - ref_line[j]);

    return m2;
}

static inline
void compute_images_mse(PSNRContext *s,
                        const uint8_t *main_data[4], const int main_linesizes[4],
                        const uint8_t *ref_data[4], const int ref_linesizes[4],
                        int w, int h, double mse[4])
{
    int i, c;

    for (c = 0; c < s->nb_components; c++) {
        const int outw = s->planewidth[c];
        const int outh = s->planeheight[c];
        const uint8_t *main_line = main_data[c];
        const uint8_t *ref_line = ref_data[c];
        const int ref_linesize = ref_linesizes[c];
        const int main_linesize = main_linesizes[c];
        uint64_t m = 0;
        for (i = 0; i < outh; i++) {
            m += s->dsp.sse_line(main_line, ref_line, outw);
            ref_line += ref_linesize;
            main_line += main_linesize;
        }
        mse[c] = m / (double)(outw * outh);
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

static int do_psnr(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    PSNRContext *s = ctx->priv;
    AVFrame *master, *ref;
    double comp_mse[4], mse = 0;
    int ret, j, c;
    AVDictionary **metadata;

    ret = ff_framesync_dualinput_get(fs, &master, &ref);
    if (ret < 0)
        return ret;
    if (!ref)
        return ff_filter_frame(ctx->outputs[0], master);
    metadata = &master->metadata;

    compute_images_mse(s, (const uint8_t **)master->data, master->linesize,
                          (const uint8_t **)ref->data, ref->linesize,
                          master->width, master->height, comp_mse);

    for (j = 0; j < s->nb_components; j++)
        mse += comp_mse[j] * s->planeweight[j];

    s->min_mse = FFMIN(s->min_mse, mse);
    s->max_mse = FFMAX(s->max_mse, mse);

    s->mse += mse;
    for (j = 0; j < s->nb_components; j++)
        s->mse_comp[j] += comp_mse[j];
    s->nb_frames++;

    for (j = 0; j < s->nb_components; j++) {
        c = s->is_rgb ? s->rgba_map[j] : j;
        set_meta(metadata, "lavfi.psnr.mse.", s->comps[j], comp_mse[c]);
        set_meta(metadata, "lavfi.psnr.psnr.", s->comps[j], get_psnr(comp_mse[c], 1, s->max[c]));
    }
    set_meta(metadata, "lavfi.psnr.mse_avg", 0, mse);
    set_meta(metadata, "lavfi.psnr.psnr_avg", 0, get_psnr(mse, 1, s->average_max));

    if (s->stats_file) {
        if (s->stats_version == 2 && !s->stats_header_written) {
            fprintf(s->stats_file, "psnr_log_version:2 fields:n");
            fprintf(s->stats_file, ",mse_avg");
            for (j = 0; j < s->nb_components; j++) {
                fprintf(s->stats_file, ",mse_%c", s->comps[j]);
            }
            fprintf(s->stats_file, ",psnr_avg");
            for (j = 0; j < s->nb_components; j++) {
                fprintf(s->stats_file, ",psnr_%c", s->comps[j]);
            }
            if (s->stats_add_max) {
                fprintf(s->stats_file, ",max_avg");
                for (j = 0; j < s->nb_components; j++) {
                    fprintf(s->stats_file, ",max_%c", s->comps[j]);
                }
            }
            fprintf(s->stats_file, "\n");
            s->stats_header_written = 1;
        }
        fprintf(s->stats_file, "n:%"PRId64" mse_avg:%0.2f ", s->nb_frames, mse);
        for (j = 0; j < s->nb_components; j++) {
            c = s->is_rgb ? s->rgba_map[j] : j;
            fprintf(s->stats_file, "mse_%c:%0.2f ", s->comps[j], comp_mse[c]);
        }
        fprintf(s->stats_file, "psnr_avg:%0.2f ", get_psnr(mse, 1, s->average_max));
        for (j = 0; j < s->nb_components; j++) {
            c = s->is_rgb ? s->rgba_map[j] : j;
            fprintf(s->stats_file, "psnr_%c:%0.2f ", s->comps[j],
                    get_psnr(comp_mse[c], 1, s->max[c]));
        }
        if (s->stats_version == 2 && s->stats_add_max) {
            fprintf(s->stats_file, "max_avg:%d ", s->average_max);
            for (j = 0; j < s->nb_components; j++) {
                c = s->is_rgb ? s->rgba_map[j] : j;
                fprintf(s->stats_file, "max_%c:%d ", s->comps[j], s->max[c]);
            }
        }
        fprintf(s->stats_file, "\n");
    }

    return ff_filter_frame(ctx->outputs[0], master);
}

static av_cold int init(AVFilterContext *ctx)
{
    PSNRContext *s = ctx->priv;

    s->min_mse = +INFINITY;
    s->max_mse = -INFINITY;

    if (s->stats_file_str) {
        if (s->stats_version < 2 && s->stats_add_max) {
            av_log(ctx, AV_LOG_ERROR,
                "stats_add_max was specified but stats_version < 2.\n" );
            return AVERROR(EINVAL);
        }
        if (!strcmp(s->stats_file_str, "-")) {
            s->stats_file = stdout;
        } else {
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
    }

    s->fs.on_event = do_psnr;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10, AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY16,
#define PF_NOALPHA(suf) AV_PIX_FMT_YUV420##suf,  AV_PIX_FMT_YUV422##suf,  AV_PIX_FMT_YUV444##suf
#define PF_ALPHA(suf)   AV_PIX_FMT_YUVA420##suf, AV_PIX_FMT_YUVA422##suf, AV_PIX_FMT_YUVA444##suf
#define PF(suf)         PF_NOALPHA(suf), PF_ALPHA(suf)
        PF(P), PF(P9), PF(P10), PF_NOALPHA(P12), PF_NOALPHA(P14), PF(P16),
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_GBRP, AV_PIX_FMT_GBRP9, AV_PIX_FMT_GBRP10,
        AV_PIX_FMT_GBRP12, AV_PIX_FMT_GBRP14, AV_PIX_FMT_GBRP16,
        AV_PIX_FMT_GBRAP, AV_PIX_FMT_GBRAP10, AV_PIX_FMT_GBRAP12, AV_PIX_FMT_GBRAP16,
        AV_PIX_FMT_NONE
    };

    AVFilterFormats *fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_input_ref(AVFilterLink *inlink)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(inlink->format);
    AVFilterContext *ctx  = inlink->dst;
    PSNRContext *s = ctx->priv;
    double average_max;
    unsigned sum;
    int j;

    s->nb_components = desc->nb_components;
    if (ctx->inputs[0]->w != ctx->inputs[1]->w ||
        ctx->inputs[0]->h != ctx->inputs[1]->h) {
        av_log(ctx, AV_LOG_ERROR, "Width and height of input videos must be same.\n");
        return AVERROR(EINVAL);
    }
    if (ctx->inputs[0]->format != ctx->inputs[1]->format) {
        av_log(ctx, AV_LOG_ERROR, "Inputs must be of same pixel format.\n");
        return AVERROR(EINVAL);
    }

    s->max[0] = (1 << desc->comp[0].depth) - 1;
    s->max[1] = (1 << desc->comp[1].depth) - 1;
    s->max[2] = (1 << desc->comp[2].depth) - 1;
    s->max[3] = (1 << desc->comp[3].depth) - 1;

    s->is_rgb = ff_fill_rgba_map(s->rgba_map, inlink->format) >= 0;
    s->comps[0] = s->is_rgb ? 'r' : 'y' ;
    s->comps[1] = s->is_rgb ? 'g' : 'u' ;
    s->comps[2] = s->is_rgb ? 'b' : 'v' ;
    s->comps[3] = 'a';

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;
    sum = 0;
    for (j = 0; j < s->nb_components; j++)
        sum += s->planeheight[j] * s->planewidth[j];
    average_max = 0;
    for (j = 0; j < s->nb_components; j++) {
        s->planeweight[j] = (double) s->planeheight[j] * s->planewidth[j] / sum;
        average_max += s->max[j] * s->planeweight[j];
    }
    s->average_max = lrint(average_max);

    s->dsp.sse_line = desc->comp[0].depth > 8 ? sse_line_16bit : sse_line_8bit;
    if (ARCH_X86)
        ff_psnr_init_x86(&s->dsp, desc->comp[0].depth);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    PSNRContext *s = ctx->priv;
    AVFilterLink *mainlink = ctx->inputs[0];
    int ret;

    ret = ff_framesync_init_dualinput(&s->fs, ctx);
    if (ret < 0)
        return ret;
    outlink->w = mainlink->w;
    outlink->h = mainlink->h;
    outlink->time_base = mainlink->time_base;
    outlink->sample_aspect_ratio = mainlink->sample_aspect_ratio;
    outlink->frame_rate = mainlink->frame_rate;
    if ((ret = ff_framesync_configure(&s->fs)) < 0)
        return ret;

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    PSNRContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    PSNRContext *s = ctx->priv;

    if (s->nb_frames > 0) {
        int j;
        char buf[256];

        buf[0] = 0;
        for (j = 0; j < s->nb_components; j++) {
            int c = s->is_rgb ? s->rgba_map[j] : j;
            av_strlcatf(buf, sizeof(buf), " %c:%f", s->comps[j],
                        get_psnr(s->mse_comp[c], s->nb_frames, s->max[c]));
        }
        av_log(ctx, AV_LOG_INFO, "PSNR%s average:%f min:%f max:%f\n",
               buf,
               get_psnr(s->mse, s->nb_frames, s->average_max),
               get_psnr(s->max_mse, 1, s->average_max),
               get_psnr(s->min_mse, 1, s->average_max));
    }

    ff_framesync_uninit(&s->fs);

    if (s->stats_file && s->stats_file != stdout)
        fclose(s->stats_file);
}

static const AVFilterPad psnr_inputs[] = {
    {
        .name         = "main",
        .type         = AVMEDIA_TYPE_VIDEO,
    },{
        .name         = "reference",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_input_ref,
    },
    { NULL }
};

static const AVFilterPad psnr_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_psnr = {
    .name          = "psnr",
    .description   = NULL_IF_CONFIG_SMALL("Calculate the PSNR between two video streams."),
    .preinit       = psnr_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .priv_size     = sizeof(PSNRContext),
    .priv_class    = &psnr_class,
    .inputs        = psnr_inputs,
    .outputs       = psnr_outputs,
};
