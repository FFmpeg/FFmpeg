/*
 * Copyright (c) 2003-2013 Loren Merritt
 * Copyright (c) 2015 Paul B Mahol
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

/* Computes the Structural Similarity Metric between two video streams.
 * original algorithm:
 * Z. Wang, A. C. Bovik, H. R. Sheikh and E. P. Simoncelli,
 *   "Image quality assessment: From error visibility to structural similarity,"
 *   IEEE Transactions on Image Processing, vol. 13, no. 4, pp. 600-612, Apr. 2004.
 *
 * To improve speed, this implementation uses the standard approximation of
 * overlapped 8x8 block sums, rather than the original gaussian weights.
 */

/*
 * @file
 * Caculate the SSIM between two input videos.
 */

#include "libavutil/avstring.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "avfilter.h"
#include "drawutils.h"
#include "formats.h"
#include "framesync.h"
#include "internal.h"
#include "ssim.h"
#include "video.h"

typedef struct SSIMContext {
    const AVClass *class;
    FFFrameSync fs;
    FILE *stats_file;
    char *stats_file_str;
    int nb_components;
    int max;
    uint64_t nb_frames;
    double ssim[4], ssim_total;
    char comps[4];
    double coefs[4];
    uint8_t rgba_map[4];
    int planewidth[4];
    int planeheight[4];
    int *temp;
    int is_rgb;
    double (*ssim_plane)(SSIMDSPContext *dsp,
                        uint8_t *main, int main_stride,
                        uint8_t *ref, int ref_stride,
                        int width, int height, void *temp,
                        int max);
    SSIMDSPContext dsp;
} SSIMContext;

#define OFFSET(x) offsetof(SSIMContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM

static const AVOption ssim_options[] = {
    {"stats_file", "Set file where to store per-frame difference information", OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    {"f",          "Set file where to store per-frame difference information", OFFSET(stats_file_str), AV_OPT_TYPE_STRING, {.str=NULL}, 0, 0, FLAGS },
    { NULL }
};

FRAMESYNC_DEFINE_CLASS(ssim, SSIMContext, fs);

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

static void ssim_4x4xn_16bit(const uint8_t *main8, ptrdiff_t main_stride,
                             const uint8_t *ref8, ptrdiff_t ref_stride,
                             int64_t (*sums)[4], int width)
{
    const uint16_t *main16 = (const uint16_t *)main8;
    const uint16_t *ref16  = (const uint16_t *)ref8;
    int x, y, z;

    main_stride >>= 1;
    ref_stride >>= 1;

    for (z = 0; z < width; z++) {
        uint64_t s1 = 0, s2 = 0, ss = 0, s12 = 0;

        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                unsigned a = main16[x + y * main_stride];
                unsigned b = ref16[x + y * ref_stride];

                s1  += a;
                s2  += b;
                ss  += a*a;
                ss  += b*b;
                s12 += a*b;
            }
        }

        sums[z][0] = s1;
        sums[z][1] = s2;
        sums[z][2] = ss;
        sums[z][3] = s12;
        main16 += 4;
        ref16 += 4;
    }
}

static void ssim_4x4xn_8bit(const uint8_t *main, ptrdiff_t main_stride,
                            const uint8_t *ref, ptrdiff_t ref_stride,
                            int (*sums)[4], int width)
{
    int x, y, z;

    for (z = 0; z < width; z++) {
        uint32_t s1 = 0, s2 = 0, ss = 0, s12 = 0;

        for (y = 0; y < 4; y++) {
            for (x = 0; x < 4; x++) {
                int a = main[x + y * main_stride];
                int b = ref[x + y * ref_stride];

                s1  += a;
                s2  += b;
                ss  += a*a;
                ss  += b*b;
                s12 += a*b;
            }
        }

        sums[z][0] = s1;
        sums[z][1] = s2;
        sums[z][2] = ss;
        sums[z][3] = s12;
        main += 4;
        ref += 4;
    }
}

static float ssim_end1x(int64_t s1, int64_t s2, int64_t ss, int64_t s12, int max)
{
    int64_t ssim_c1 = (int64_t)(.01*.01*max*max*64 + .5);
    int64_t ssim_c2 = (int64_t)(.03*.03*max*max*64*63 + .5);

    int64_t fs1 = s1;
    int64_t fs2 = s2;
    int64_t fss = ss;
    int64_t fs12 = s12;
    int64_t vars = fss * 64 - fs1 * fs1 - fs2 * fs2;
    int64_t covar = fs12 * 64 - fs1 * fs2;

    return (float)(2 * fs1 * fs2 + ssim_c1) * (float)(2 * covar + ssim_c2)
         / ((float)(fs1 * fs1 + fs2 * fs2 + ssim_c1) * (float)(vars + ssim_c2));
}

static float ssim_end1(int s1, int s2, int ss, int s12)
{
    static const int ssim_c1 = (int)(.01*.01*255*255*64 + .5);
    static const int ssim_c2 = (int)(.03*.03*255*255*64*63 + .5);

    int fs1 = s1;
    int fs2 = s2;
    int fss = ss;
    int fs12 = s12;
    int vars = fss * 64 - fs1 * fs1 - fs2 * fs2;
    int covar = fs12 * 64 - fs1 * fs2;

    return (float)(2 * fs1 * fs2 + ssim_c1) * (float)(2 * covar + ssim_c2)
         / ((float)(fs1 * fs1 + fs2 * fs2 + ssim_c1) * (float)(vars + ssim_c2));
}

static float ssim_endn_16bit(const int64_t (*sum0)[4], const int64_t (*sum1)[4], int width, int max)
{
    float ssim = 0.0;
    int i;

    for (i = 0; i < width; i++)
        ssim += ssim_end1x(sum0[i][0] + sum0[i + 1][0] + sum1[i][0] + sum1[i + 1][0],
                           sum0[i][1] + sum0[i + 1][1] + sum1[i][1] + sum1[i + 1][1],
                           sum0[i][2] + sum0[i + 1][2] + sum1[i][2] + sum1[i + 1][2],
                           sum0[i][3] + sum0[i + 1][3] + sum1[i][3] + sum1[i + 1][3],
                           max);
    return ssim;
}

static double ssim_endn_8bit(const int (*sum0)[4], const int (*sum1)[4], int width)
{
    double ssim = 0.0;
    int i;

    for (i = 0; i < width; i++)
        ssim += ssim_end1(sum0[i][0] + sum0[i + 1][0] + sum1[i][0] + sum1[i + 1][0],
                          sum0[i][1] + sum0[i + 1][1] + sum1[i][1] + sum1[i + 1][1],
                          sum0[i][2] + sum0[i + 1][2] + sum1[i][2] + sum1[i + 1][2],
                          sum0[i][3] + sum0[i + 1][3] + sum1[i][3] + sum1[i + 1][3]);
    return ssim;
}

#define SUM_LEN(w) (((w) >> 2) + 3)

static double ssim_plane_16bit(SSIMDSPContext *dsp,
                              uint8_t *main, int main_stride,
                              uint8_t *ref, int ref_stride,
                              int width, int height, void *temp,
                              int max)
{
    int z = 0, y;
    double ssim = 0.0;
    int64_t (*sum0)[4] = temp;
    int64_t (*sum1)[4] = sum0 + SUM_LEN(width);

    width >>= 2;
    height >>= 2;

    for (y = 1; y < height; y++) {
        for (; z <= y; z++) {
            FFSWAP(void*, sum0, sum1);
            ssim_4x4xn_16bit(&main[4 * z * main_stride], main_stride,
                             &ref[4 * z * ref_stride], ref_stride,
                             sum0, width);
        }

        ssim += ssim_endn_16bit((const int64_t (*)[4])sum0, (const int64_t (*)[4])sum1, width - 1, max);
    }

    return ssim / ((height - 1) * (width - 1));
}

static double ssim_plane(SSIMDSPContext *dsp,
                        uint8_t *main, int main_stride,
                        uint8_t *ref, int ref_stride,
                        int width, int height, void *temp,
                        int max)
{
    int z = 0, y;
    double ssim = 0.0;
    int (*sum0)[4] = temp;
    int (*sum1)[4] = sum0 + SUM_LEN(width);

    width >>= 2;
    height >>= 2;

    for (y = 1; y < height; y++) {
        for (; z <= y; z++) {
            FFSWAP(void*, sum0, sum1);
            dsp->ssim_4x4_line(&main[4 * z * main_stride], main_stride,
                               &ref[4 * z * ref_stride], ref_stride,
                               sum0, width);
        }

        ssim += dsp->ssim_end_line((const int (*)[4])sum0, (const int (*)[4])sum1, width - 1);
    }

    return ssim / ((height - 1) * (width - 1));
}

static double ssim_db(double ssim, double weight)
{
    return (fabs(weight - ssim) > 1e-9) ? 10.0 * log10(weight / (weight - ssim)) : INFINITY;
}

static int do_ssim(FFFrameSync *fs)
{
    AVFilterContext *ctx = fs->parent;
    SSIMContext *s = ctx->priv;
    AVFrame *master, *ref;
    AVDictionary **metadata;
    double c[4] = { 0 }, ssimv = 0.0;
    int ret, i;

    ret = ff_framesync_dualinput_get(fs, &master, &ref);
    if (ret < 0)
        return ret;
    if (!ref)
        return ff_filter_frame(ctx->outputs[0], master);
    metadata = &master->metadata;

    s->nb_frames++;

    for (i = 0; i < s->nb_components; i++) {
        c[i] = s->ssim_plane(&s->dsp, master->data[i], master->linesize[i],
                             ref->data[i], ref->linesize[i],
                             s->planewidth[i], s->planeheight[i], s->temp,
                             s->max);
        ssimv += s->coefs[i] * c[i];
        s->ssim[i] += c[i];
    }
    for (i = 0; i < s->nb_components; i++) {
        int cidx = s->is_rgb ? s->rgba_map[i] : i;
        set_meta(metadata, "lavfi.ssim.", s->comps[i], c[cidx]);
    }
    s->ssim_total += ssimv;

    set_meta(metadata, "lavfi.ssim.All", 0, ssimv);
    set_meta(metadata, "lavfi.ssim.dB", 0, ssim_db(ssimv, 1.0));

    if (s->stats_file) {
        fprintf(s->stats_file, "n:%"PRId64" ", s->nb_frames);

        for (i = 0; i < s->nb_components; i++) {
            int cidx = s->is_rgb ? s->rgba_map[i] : i;
            fprintf(s->stats_file, "%c:%f ", s->comps[i], c[cidx]);
        }

        fprintf(s->stats_file, "All:%f (%f)\n", ssimv, ssim_db(ssimv, 1.0));
    }

    return ff_filter_frame(ctx->outputs[0], master);
}

static av_cold int init(AVFilterContext *ctx)
{
    SSIMContext *s = ctx->priv;

    if (s->stats_file_str) {
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

    s->fs.on_event = do_ssim;
    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    static const enum AVPixelFormat pix_fmts[] = {
        AV_PIX_FMT_GRAY8, AV_PIX_FMT_GRAY9, AV_PIX_FMT_GRAY10,
        AV_PIX_FMT_GRAY12, AV_PIX_FMT_GRAY14, AV_PIX_FMT_GRAY16,
        AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P, AV_PIX_FMT_YUV444P,
        AV_PIX_FMT_YUV440P, AV_PIX_FMT_YUV411P, AV_PIX_FMT_YUV410P,
        AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUVJ422P,
        AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUVJ444P,
        AV_PIX_FMT_GBRP,
#define PF(suf) AV_PIX_FMT_YUV420##suf,  AV_PIX_FMT_YUV422##suf,  AV_PIX_FMT_YUV444##suf, AV_PIX_FMT_GBR##suf
        PF(P9), PF(P10), PF(P12), PF(P14), PF(P16),
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
    SSIMContext *s = ctx->priv;
    int sum = 0, i;

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

    s->is_rgb = ff_fill_rgba_map(s->rgba_map, inlink->format) >= 0;
    s->comps[0] = s->is_rgb ? 'R' : 'Y';
    s->comps[1] = s->is_rgb ? 'G' : 'U';
    s->comps[2] = s->is_rgb ? 'B' : 'V';
    s->comps[3] = 'A';

    s->planeheight[1] = s->planeheight[2] = AV_CEIL_RSHIFT(inlink->h, desc->log2_chroma_h);
    s->planeheight[0] = s->planeheight[3] = inlink->h;
    s->planewidth[1]  = s->planewidth[2]  = AV_CEIL_RSHIFT(inlink->w, desc->log2_chroma_w);
    s->planewidth[0]  = s->planewidth[3]  = inlink->w;
    for (i = 0; i < s->nb_components; i++)
        sum += s->planeheight[i] * s->planewidth[i];
    for (i = 0; i < s->nb_components; i++)
        s->coefs[i] = (double) s->planeheight[i] * s->planewidth[i] / sum;

    s->temp = av_mallocz_array(2 * SUM_LEN(inlink->w), (desc->comp[0].depth > 8) ? sizeof(int64_t[4]) : sizeof(int[4]));
    if (!s->temp)
        return AVERROR(ENOMEM);
    s->max = (1 << desc->comp[0].depth) - 1;

    s->ssim_plane = desc->comp[0].depth > 8 ? ssim_plane_16bit : ssim_plane;
    s->dsp.ssim_4x4_line = ssim_4x4xn_8bit;
    s->dsp.ssim_end_line = ssim_endn_8bit;
    if (ARCH_X86)
        ff_ssim_init_x86(&s->dsp);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    SSIMContext *s = ctx->priv;
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

    outlink->time_base = s->fs.time_base;

    if (av_cmp_q(mainlink->time_base, outlink->time_base) ||
        av_cmp_q(ctx->inputs[1]->time_base, outlink->time_base))
        av_log(ctx, AV_LOG_WARNING, "not matching timebases found between first input: %d/%d and second input %d/%d, results may be incorrect!\n",
               mainlink->time_base.num, mainlink->time_base.den,
               ctx->inputs[1]->time_base.num, ctx->inputs[1]->time_base.den);

    return 0;
}

static int activate(AVFilterContext *ctx)
{
    SSIMContext *s = ctx->priv;
    return ff_framesync_activate(&s->fs);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    SSIMContext *s = ctx->priv;

    if (s->nb_frames > 0) {
        char buf[256];
        int i;
        buf[0] = 0;
        for (i = 0; i < s->nb_components; i++) {
            int c = s->is_rgb ? s->rgba_map[i] : i;
            av_strlcatf(buf, sizeof(buf), " %c:%f (%f)", s->comps[i], s->ssim[c] / s->nb_frames,
                        ssim_db(s->ssim[c], s->nb_frames));
        }
        av_log(ctx, AV_LOG_INFO, "SSIM%s All:%f (%f)\n", buf,
               s->ssim_total / s->nb_frames, ssim_db(s->ssim_total, s->nb_frames));
    }

    ff_framesync_uninit(&s->fs);

    if (s->stats_file && s->stats_file != stdout)
        fclose(s->stats_file);

    av_freep(&s->temp);
}

static const AVFilterPad ssim_inputs[] = {
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

static const AVFilterPad ssim_outputs[] = {
    {
        .name          = "default",
        .type          = AVMEDIA_TYPE_VIDEO,
        .config_props  = config_output,
    },
    { NULL }
};

AVFilter ff_vf_ssim = {
    .name          = "ssim",
    .description   = NULL_IF_CONFIG_SMALL("Calculate the SSIM between two video streams."),
    .preinit       = ssim_framesync_preinit,
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .activate      = activate,
    .priv_size     = sizeof(SSIMContext),
    .priv_class    = &ssim_class,
    .inputs        = ssim_inputs,
    .outputs       = ssim_outputs,
};
