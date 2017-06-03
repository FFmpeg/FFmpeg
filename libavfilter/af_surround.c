/*
 * Copyright (c) 2017 Paul B Mahol
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

#include "libavutil/audio_fifo.h"
#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "libavcodec/avfft.h"
#include "avfilter.h"
#include "audio.h"
#include "formats.h"

typedef struct AudioSurroundContext {
    const AVClass *class;

    char *out_channel_layout_str;
    char *in_channel_layout_str;
    float level_in;
    float level_out;
    int output_lfe;
    int lowcutf;
    int highcutf;

    float lowcut;
    float highcut;

    uint64_t out_channel_layout;
    uint64_t in_channel_layout;
    int nb_in_channels;
    int nb_out_channels;

    AVFrame *input;
    AVFrame *output;
    AVFrame *overlap_buffer;

    int buf_size;
    int hop_size;
    AVAudioFifo *fifo;
    RDFTContext **rdft, **irdft;
    float *window_func_lut;

    int64_t pts;

    void (*filter)(AVFilterContext *ctx);
    void (*upmix_stereo)(AVFilterContext *ctx,
                         float l_phase,
                         float r_phase,
                         float c_phase,
                         float mag_total,
                         float x, float y,
                         int n);
    void (*upmix_2_1)(AVFilterContext *ctx,
                      float l_phase,
                      float r_phase,
                      float c_phase,
                      float mag_total,
                      float lfe_im,
                      float lfe_re,
                      float x, float y,
                      int n);
    void (*upmix_3_0)(AVFilterContext *ctx,
                      float l_phase,
                      float r_phase,
                      float c_mag,
                      float c_phase,
                      float mag_total,
                      float x, float y,
                      int n);
    void (*upmix_5_1)(AVFilterContext *ctx,
                      float c_re, float c_im,
                      float lfe_re, float lfe_im,
                      float mag_totall, float mag_totalr,
                      float fl_phase, float fr_phase,
                      float bl_phase, float br_phase,
                      float sl_phase, float sr_phase,
                      float xl, float yl,
                      float xr, float yr,
                      int n);
} AudioSurroundContext;

static int query_formats(AVFilterContext *ctx)
{
    AudioSurroundContext *s = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts = NULL;
    int ret;

    ret = ff_add_format(&formats, AV_SAMPLE_FMT_FLTP);
    if (ret)
        return ret;
    ret = ff_set_common_formats(ctx, formats);
    if (ret)
        return ret;

    layouts = NULL;
    ret = ff_add_channel_layout(&layouts, s->out_channel_layout);
    if (ret)
        return ret;

    ret = ff_channel_layouts_ref(layouts, &ctx->outputs[0]->in_channel_layouts);
    if (ret)
        return ret;

    layouts = NULL;
    ret = ff_add_channel_layout(&layouts, s->in_channel_layout);
    if (ret)
        return ret;

    ret = ff_channel_layouts_ref(layouts, &ctx->inputs[0]->out_channel_layouts);
    if (ret)
        return ret;

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    return ff_set_common_samplerates(ctx, formats);
}

static int config_input(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    AudioSurroundContext *s = ctx->priv;
    int ch;

    s->rdft = av_calloc(inlink->channels, sizeof(*s->rdft));
    if (!s->rdft)
        return AVERROR(ENOMEM);

    for (ch = 0; ch < inlink->channels; ch++) {
        s->rdft[ch]  = av_rdft_init(ff_log2(s->buf_size), DFT_R2C);
        if (!s->rdft[ch])
            return AVERROR(ENOMEM);
    }
    s->nb_in_channels = inlink->channels;

    s->input = ff_get_audio_buffer(inlink, s->buf_size * 2);
    if (!s->input)
        return AVERROR(ENOMEM);

    s->fifo = av_audio_fifo_alloc(inlink->format, inlink->channels, s->buf_size);
    if (!s->fifo)
        return AVERROR(ENOMEM);

    s->lowcut = 1.f * s->lowcutf / (inlink->sample_rate * 0.5) * (s->buf_size / 2);
    s->highcut = 1.f * s->highcutf / (inlink->sample_rate * 0.5) * (s->buf_size / 2);

    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    AudioSurroundContext *s = ctx->priv;
    int ch;

    s->irdft = av_calloc(outlink->channels, sizeof(*s->irdft));
    if (!s->irdft)
        return AVERROR(ENOMEM);

    for (ch = 0; ch < outlink->channels; ch++) {
        s->irdft[ch] = av_rdft_init(ff_log2(s->buf_size), IDFT_C2R);
        if (!s->irdft[ch])
            return AVERROR(ENOMEM);
    }
    s->nb_out_channels = outlink->channels;

    s->output = ff_get_audio_buffer(outlink, s->buf_size * 2);
    s->overlap_buffer = ff_get_audio_buffer(outlink, s->buf_size * 2);
    if (!s->overlap_buffer || !s->output)
        return AVERROR(ENOMEM);

    return 0;
}

static void stereo_position(float a, float p, float *x, float *y)
{
      *x = av_clipf(a+FFMAX(0, sinf(p-M_PI_2))*FFDIFFSIGN(a,0), -1, 1);
      *y = av_clipf(cosf(a*M_PI_2+M_PI)*cosf(M_PI_2-p/M_PI)*M_LN10+1, -1, 1);
}

static inline void get_lfe(int output_lfe, int n, float lowcut, float highcut,
                           float *lfe_mag, float *mag_total)
{
    if (output_lfe && n < highcut) {
        *lfe_mag    = n < lowcut ? 1.f : .5f*(1.f+cosf(M_PI*(lowcut-n)/(lowcut-highcut)));
        *lfe_mag   *= *mag_total;
        *mag_total -= *lfe_mag;
    } else {
        *lfe_mag = 0.f;
    }
}

static void upmix_1_0(AVFilterContext *ctx,
                      float l_phase,
                      float r_phase,
                      float c_phase,
                      float mag_total,
                      float x, float y,
                      int n)
{
    AudioSurroundContext *s = ctx->priv;
    float mag, *dst;

    dst = (float *)s->output->extended_data[0];

    mag = sqrtf(1.f - fabsf(x)) * ((y + 1.f) * .5f) * mag_total;

    dst[2 * n    ] = mag * cosf(c_phase);
    dst[2 * n + 1] = mag * sinf(c_phase);
}

static void upmix_stereo(AVFilterContext *ctx,
                         float l_phase,
                         float r_phase,
                         float c_phase,
                         float mag_total,
                         float x, float y,
                         int n)
{
    AudioSurroundContext *s = ctx->priv;
    float l_mag, r_mag, *dstl, *dstr;

    dstl = (float *)s->output->extended_data[0];
    dstr = (float *)s->output->extended_data[1];

    l_mag = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);
}

static void upmix_2_1(AVFilterContext *ctx,
                      float l_phase,
                      float r_phase,
                      float c_phase,
                      float mag_total,
                      float x, float y,
                      int n)
{
    AudioSurroundContext *s = ctx->priv;
    float lfe_mag, l_mag, r_mag, *dstl, *dstr, *dstlfe;

    dstl = (float *)s->output->extended_data[0];
    dstr = (float *)s->output->extended_data[1];
    dstlfe = (float *)s->output->extended_data[2];

    get_lfe(s->output_lfe, n, s->lowcut, s->highcut, &lfe_mag, &mag_total);

    l_mag = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstlfe[2 * n    ] = lfe_mag * cosf(c_phase);
    dstlfe[2 * n + 1] = lfe_mag * sinf(c_phase);
}

static void upmix_3_0(AVFilterContext *ctx,
                      float l_phase,
                      float r_phase,
                      float c_phase,
                      float mag_total,
                      float x, float y,
                      int n)
{
    AudioSurroundContext *s = ctx->priv;
    float l_mag, r_mag, c_mag, *dstc, *dstl, *dstr;

    dstl = (float *)s->output->extended_data[0];
    dstr = (float *)s->output->extended_data[1];
    dstc = (float *)s->output->extended_data[2];

    c_mag = sqrtf(1.f - fabsf(x))   * ((y + 1.f) * .5f) * mag_total;
    l_mag = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);
}

static void upmix_3_1(AVFilterContext *ctx,
                      float l_phase,
                      float r_phase,
                      float c_phase,
                      float mag_total,
                      float x, float y,
                      int n)
{
    AudioSurroundContext *s = ctx->priv;
    float lfe_mag, l_mag, r_mag, c_mag, *dstc, *dstl, *dstr, *dstlfe;

    dstl = (float *)s->output->extended_data[0];
    dstr = (float *)s->output->extended_data[1];
    dstc = (float *)s->output->extended_data[2];
    dstlfe = (float *)s->output->extended_data[3];

    get_lfe(s->output_lfe, n, s->lowcut, s->highcut, &lfe_mag, &mag_total);

    c_mag = sqrtf(1.f - fabsf(x))   * ((y + 1.f) * .5f) * mag_total;
    l_mag = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);

    dstlfe[2 * n    ] = lfe_mag * cosf(c_phase);
    dstlfe[2 * n + 1] = lfe_mag * sinf(c_phase);
}

static void upmix_3_1_surround(AVFilterContext *ctx,
                               float l_phase,
                               float r_phase,
                               float c_phase,
                               float c_mag,
                               float mag_total,
                               float x, float y,
                               int n)
{
    AudioSurroundContext *s = ctx->priv;
    float lfe_mag, l_mag, r_mag, *dstc, *dstl, *dstr, *dstlfe;

    dstl = (float *)s->output->extended_data[0];
    dstr = (float *)s->output->extended_data[1];
    dstc = (float *)s->output->extended_data[2];
    dstlfe = (float *)s->output->extended_data[3];

    get_lfe(s->output_lfe, n, s->lowcut, s->highcut, &lfe_mag, &c_mag);

    l_mag = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);

    dstlfe[2 * n    ] = lfe_mag * cosf(c_phase);
    dstlfe[2 * n + 1] = lfe_mag * sinf(c_phase);
}

static void upmix_4_0(AVFilterContext *ctx,
                      float l_phase,
                      float r_phase,
                      float c_phase,
                      float mag_total,
                      float x, float y,
                      int n)
{
    float b_mag, l_mag, r_mag, c_mag, *dstc, *dstl, *dstr, *dstb;
    AudioSurroundContext *s = ctx->priv;

    dstl = (float *)s->output->extended_data[0];
    dstr = (float *)s->output->extended_data[1];
    dstc = (float *)s->output->extended_data[2];
    dstb = (float *)s->output->extended_data[3];

    c_mag = sqrtf(1.f - fabsf(x))   * ((y + 1.f) * .5f) * mag_total;
    b_mag = sqrtf(1.f - fabsf(x))   * ((1.f - y) * .5f) * mag_total;
    l_mag = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);

    dstb[2 * n    ] = b_mag * cosf(c_phase);
    dstb[2 * n + 1] = b_mag * sinf(c_phase);
}

static void upmix_4_1(AVFilterContext *ctx,
                      float l_phase,
                      float r_phase,
                      float c_phase,
                      float mag_total,
                      float x, float y,
                      int n)
{
    float lfe_mag, b_mag, l_mag, r_mag, c_mag, *dstc, *dstl, *dstr, *dstb, *dstlfe;
    AudioSurroundContext *s = ctx->priv;

    dstl = (float *)s->output->extended_data[0];
    dstr = (float *)s->output->extended_data[1];
    dstc = (float *)s->output->extended_data[2];
    dstlfe = (float *)s->output->extended_data[3];
    dstb = (float *)s->output->extended_data[4];

    get_lfe(s->output_lfe, n, s->lowcut, s->highcut, &lfe_mag, &mag_total);

    dstlfe[2 * n    ] = lfe_mag * cosf(c_phase);
    dstlfe[2 * n + 1] = lfe_mag * sinf(c_phase);

    c_mag = sqrtf(1.f - fabsf(x))   * ((y + 1.f) * .5f) * mag_total;
    b_mag = sqrtf(1.f - fabsf(x))   * ((1.f - y) * .5f) * mag_total;
    l_mag = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);

    dstb[2 * n    ] = b_mag * cosf(c_phase);
    dstb[2 * n + 1] = b_mag * sinf(c_phase);
}

static void upmix_5_0_back(AVFilterContext *ctx,
                           float l_phase,
                           float r_phase,
                           float c_phase,
                           float mag_total,
                           float x, float y,
                           int n)
{
    float l_mag, r_mag, ls_mag, rs_mag, c_mag, *dstc, *dstl, *dstr, *dstls, *dstrs;
    AudioSurroundContext *s = ctx->priv;

    dstl  = (float *)s->output->extended_data[0];
    dstr  = (float *)s->output->extended_data[1];
    dstc  = (float *)s->output->extended_data[2];
    dstls = (float *)s->output->extended_data[3];
    dstrs = (float *)s->output->extended_data[4];

    c_mag  = sqrtf(1.f - fabsf(x))   * ((y + 1.f) * .5f) * mag_total;
    l_mag  = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag  = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    ls_mag = sqrtf(.5f * ( x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;
    rs_mag = sqrtf(.5f * (-x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);

    dstls[2 * n    ] = ls_mag * cosf(l_phase);
    dstls[2 * n + 1] = ls_mag * sinf(l_phase);

    dstrs[2 * n    ] = rs_mag * cosf(r_phase);
    dstrs[2 * n + 1] = rs_mag * sinf(r_phase);
}

static void upmix_5_1_back(AVFilterContext *ctx,
                           float l_phase,
                           float r_phase,
                           float c_phase,
                           float mag_total,
                           float x, float y,
                           int n)
{
    float lfe_mag, l_mag, r_mag, ls_mag, rs_mag, c_mag, *dstc, *dstl, *dstr, *dstls, *dstrs, *dstlfe;
    AudioSurroundContext *s = ctx->priv;

    dstl  = (float *)s->output->extended_data[0];
    dstr  = (float *)s->output->extended_data[1];
    dstc  = (float *)s->output->extended_data[2];
    dstlfe = (float *)s->output->extended_data[3];
    dstls = (float *)s->output->extended_data[4];
    dstrs = (float *)s->output->extended_data[5];

    get_lfe(s->output_lfe, n, s->lowcut, s->highcut, &lfe_mag, &mag_total);

    c_mag  = sqrtf(1.f - fabsf(x))   * ((y + 1.f) * .5f) * mag_total;
    l_mag  = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag  = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    ls_mag = sqrtf(.5f * ( x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;
    rs_mag = sqrtf(.5f * (-x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);

    dstlfe[2 * n    ] = lfe_mag * cosf(c_phase);
    dstlfe[2 * n + 1] = lfe_mag * sinf(c_phase);

    dstls[2 * n    ] = ls_mag * cosf(l_phase);
    dstls[2 * n + 1] = ls_mag * sinf(l_phase);

    dstrs[2 * n    ] = rs_mag * cosf(r_phase);
    dstrs[2 * n + 1] = rs_mag * sinf(r_phase);
}

static void upmix_5_1_back_surround(AVFilterContext *ctx,
                                    float l_phase,
                                    float r_phase,
                                    float c_phase,
                                    float c_mag,
                                    float mag_total,
                                    float x, float y,
                                    int n)
{
    AudioSurroundContext *s = ctx->priv;
    float lfe_mag, l_mag, r_mag, *dstc, *dstl, *dstr, *dstlfe;
    float ls_mag, rs_mag, *dstls, *dstrs;

    dstl = (float *)s->output->extended_data[0];
    dstr = (float *)s->output->extended_data[1];
    dstc = (float *)s->output->extended_data[2];
    dstlfe = (float *)s->output->extended_data[3];
    dstls = (float *)s->output->extended_data[4];
    dstrs = (float *)s->output->extended_data[5];

    get_lfe(s->output_lfe, n, s->lowcut, s->highcut, &lfe_mag, &c_mag);

    l_mag = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    ls_mag = sqrtf(.5f * ( x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;
    rs_mag = sqrtf(.5f * (-x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);

    dstlfe[2 * n    ] = lfe_mag * cosf(c_phase);
    dstlfe[2 * n + 1] = lfe_mag * sinf(c_phase);

    dstls[2 * n    ] = ls_mag * cosf(l_phase);
    dstls[2 * n + 1] = ls_mag * sinf(l_phase);

    dstrs[2 * n    ] = rs_mag * cosf(r_phase);
    dstrs[2 * n + 1] = rs_mag * sinf(r_phase);
}

static void upmix_5_1_back_2_1(AVFilterContext *ctx,
                               float l_phase,
                               float r_phase,
                               float c_phase,
                               float mag_total,
                               float lfe_re,
                               float lfe_im,
                               float x, float y,
                               int n)
{
    AudioSurroundContext *s = ctx->priv;
    float c_mag, l_mag, r_mag, *dstc, *dstl, *dstr, *dstlfe;
    float ls_mag, rs_mag, *dstls, *dstrs;

    dstl = (float *)s->output->extended_data[0];
    dstr = (float *)s->output->extended_data[1];
    dstc = (float *)s->output->extended_data[2];
    dstlfe = (float *)s->output->extended_data[3];
    dstls = (float *)s->output->extended_data[4];
    dstrs = (float *)s->output->extended_data[5];

    c_mag  = sqrtf(1.f - fabsf(x))   * ((y + 1.f) * .5f) * mag_total;
    l_mag  = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag  = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    ls_mag = sqrtf(.5f * ( x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;
    rs_mag = sqrtf(.5f * (-x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);

    dstlfe[2 * n    ] = lfe_re;
    dstlfe[2 * n + 1] = lfe_im;

    dstls[2 * n    ] = ls_mag * cosf(l_phase);
    dstls[2 * n + 1] = ls_mag * sinf(l_phase);

    dstrs[2 * n    ] = rs_mag * cosf(r_phase);
    dstrs[2 * n + 1] = rs_mag * sinf(r_phase);
}

static void upmix_7_0(AVFilterContext *ctx,
                      float l_phase,
                      float r_phase,
                      float c_phase,
                      float mag_total,
                      float x, float y,
                      int n)
{
    float l_mag, r_mag, ls_mag, rs_mag, c_mag, lb_mag, rb_mag;
    float *dstc, *dstl, *dstr, *dstls, *dstrs, *dstlb, *dstrb;
    AudioSurroundContext *s = ctx->priv;

    dstl  = (float *)s->output->extended_data[0];
    dstr  = (float *)s->output->extended_data[1];
    dstc  = (float *)s->output->extended_data[2];
    dstlb = (float *)s->output->extended_data[3];
    dstrb = (float *)s->output->extended_data[4];
    dstls = (float *)s->output->extended_data[5];
    dstrs = (float *)s->output->extended_data[6];

    c_mag  = sqrtf(1.f - fabsf(x))   * ((y + 1.f) * .5f) * mag_total;
    l_mag  = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag  = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    lb_mag = sqrtf(.5f * ( x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;
    rb_mag = sqrtf(.5f * (-x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;
    ls_mag = sqrtf(.5f * ( x + 1.f)) * (1.f - fabsf(y)) * mag_total;
    rs_mag = sqrtf(.5f * (-x + 1.f)) * (1.f - fabsf(y)) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);

    dstlb[2 * n    ] = lb_mag * cosf(l_phase);
    dstlb[2 * n + 1] = lb_mag * sinf(l_phase);

    dstrb[2 * n    ] = rb_mag * cosf(r_phase);
    dstrb[2 * n + 1] = rb_mag * sinf(r_phase);

    dstls[2 * n    ] = ls_mag * cosf(l_phase);
    dstls[2 * n + 1] = ls_mag * sinf(l_phase);

    dstrs[2 * n    ] = rs_mag * cosf(r_phase);
    dstrs[2 * n + 1] = rs_mag * sinf(r_phase);
}

static void upmix_7_1(AVFilterContext *ctx,
                      float l_phase,
                      float r_phase,
                      float c_phase,
                      float mag_total,
                      float x, float y,
                      int n)
{
    float lfe_mag, l_mag, r_mag, ls_mag, rs_mag, c_mag, lb_mag, rb_mag;
    float *dstc, *dstl, *dstr, *dstls, *dstrs, *dstlb, *dstrb, *dstlfe;
    AudioSurroundContext *s = ctx->priv;

    dstl  = (float *)s->output->extended_data[0];
    dstr  = (float *)s->output->extended_data[1];
    dstc  = (float *)s->output->extended_data[2];
    dstlfe = (float *)s->output->extended_data[3];
    dstlb = (float *)s->output->extended_data[4];
    dstrb = (float *)s->output->extended_data[5];
    dstls = (float *)s->output->extended_data[6];
    dstrs = (float *)s->output->extended_data[7];

    get_lfe(s->output_lfe, n, s->lowcut, s->highcut, &lfe_mag, &mag_total);

    c_mag  = sqrtf(1.f - fabsf(x))   * ((y + 1.f) * .5f) * mag_total;
    l_mag  = sqrtf(.5f * ( x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    r_mag  = sqrtf(.5f * (-x + 1.f)) * ((y + 1.f) * .5f) * mag_total;
    lb_mag = sqrtf(.5f * ( x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;
    rb_mag = sqrtf(.5f * (-x + 1.f)) * (1.f - ((y + 1.f) * .5f)) * mag_total;
    ls_mag = sqrtf(.5f * ( x + 1.f)) * (1.f - fabsf(y)) * mag_total;
    rs_mag = sqrtf(.5f * (-x + 1.f)) * (1.f - fabsf(y)) * mag_total;

    dstl[2 * n    ] = l_mag * cosf(l_phase);
    dstl[2 * n + 1] = l_mag * sinf(l_phase);

    dstr[2 * n    ] = r_mag * cosf(r_phase);
    dstr[2 * n + 1] = r_mag * sinf(r_phase);

    dstc[2 * n    ] = c_mag * cosf(c_phase);
    dstc[2 * n + 1] = c_mag * sinf(c_phase);

    dstlfe[2 * n    ] = lfe_mag * cosf(c_phase);
    dstlfe[2 * n + 1] = lfe_mag * sinf(c_phase);

    dstlb[2 * n    ] = lb_mag * cosf(l_phase);
    dstlb[2 * n + 1] = lb_mag * sinf(l_phase);

    dstrb[2 * n    ] = rb_mag * cosf(r_phase);
    dstrb[2 * n + 1] = rb_mag * sinf(r_phase);

    dstls[2 * n    ] = ls_mag * cosf(l_phase);
    dstls[2 * n + 1] = ls_mag * sinf(l_phase);

    dstrs[2 * n    ] = rs_mag * cosf(r_phase);
    dstrs[2 * n + 1] = rs_mag * sinf(r_phase);
}

static void upmix_7_1_5_1(AVFilterContext *ctx,
                          float c_re, float c_im,
                          float lfe_re, float lfe_im,
                          float mag_totall, float mag_totalr,
                          float fl_phase, float fr_phase,
                          float bl_phase, float br_phase,
                          float sl_phase, float sr_phase,
                          float xl, float yl,
                          float xr, float yr,
                          int n)
{
    float fl_mag, fr_mag, ls_mag, rs_mag, lb_mag, rb_mag;
    float *dstc, *dstl, *dstr, *dstls, *dstrs, *dstlb, *dstrb, *dstlfe;
    AudioSurroundContext *s = ctx->priv;

    dstl  = (float *)s->output->extended_data[0];
    dstr  = (float *)s->output->extended_data[1];
    dstc  = (float *)s->output->extended_data[2];
    dstlfe = (float *)s->output->extended_data[3];
    dstlb = (float *)s->output->extended_data[4];
    dstrb = (float *)s->output->extended_data[5];
    dstls = (float *)s->output->extended_data[6];
    dstrs = (float *)s->output->extended_data[7];

    fl_mag = sqrtf(.5f * (xl + 1.f)) * ((yl + 1.f) * .5f) * mag_totall;
    fr_mag = sqrtf(.5f * (xr + 1.f)) * ((yr + 1.f) * .5f) * mag_totalr;
    lb_mag = sqrtf(.5f * (-xl + 1.f)) * ((yl + 1.f) * .5f) * mag_totall;
    rb_mag = sqrtf(.5f * (-xr + 1.f)) * ((yr + 1.f) * .5f) * mag_totalr;
    ls_mag = sqrtf(1.f - fabsf(xl)) * ((yl + 1.f) * .5f) * mag_totall;
    rs_mag = sqrtf(1.f - fabsf(xr)) * ((yr + 1.f) * .5f) * mag_totalr;

    dstl[2 * n    ] = fl_mag * cosf(fl_phase);
    dstl[2 * n + 1] = fl_mag * sinf(fl_phase);

    dstr[2 * n    ] = fr_mag * cosf(fr_phase);
    dstr[2 * n + 1] = fr_mag * sinf(fr_phase);

    dstc[2 * n    ] = c_re;
    dstc[2 * n + 1] = c_im;

    dstlfe[2 * n    ] = lfe_re;
    dstlfe[2 * n + 1] = lfe_im;

    dstlb[2 * n    ] = lb_mag * cosf(bl_phase);
    dstlb[2 * n + 1] = lb_mag * sinf(bl_phase);

    dstrb[2 * n    ] = rb_mag * cosf(br_phase);
    dstrb[2 * n + 1] = rb_mag * sinf(br_phase);

    dstls[2 * n    ] = ls_mag * cosf(sl_phase);
    dstls[2 * n + 1] = ls_mag * sinf(sl_phase);

    dstrs[2 * n    ] = rs_mag * cosf(sr_phase);
    dstrs[2 * n + 1] = rs_mag * sinf(sr_phase);
}

static void filter_stereo(AVFilterContext *ctx)
{
    AudioSurroundContext *s = ctx->priv;
    float *srcl, *srcr;
    int n;

    srcl = (float *)s->input->extended_data[0];
    srcr = (float *)s->input->extended_data[1];

    for (n = 0; n < s->buf_size; n++) {
        float l_re = srcl[2 * n], r_re = srcr[2 * n];
        float l_im = srcl[2 * n + 1], r_im = srcr[2 * n + 1];
        float c_phase = atan2f(l_im + r_im, l_re + r_re);
        float l_mag = hypotf(l_re, l_im);
        float r_mag = hypotf(r_re, r_im);
        float l_phase = atan2f(l_im, l_re);
        float r_phase = atan2f(r_im, r_re);
        float phase_dif = fabsf(l_phase - r_phase);
        float mag_dif = (l_mag - r_mag) / (l_mag + r_mag);
        float mag_total = hypotf(l_mag, r_mag);
        float x, y;

        if (phase_dif > M_PI)
            phase_dif = 2 * M_PI - phase_dif;

        stereo_position(mag_dif, phase_dif, &x, &y);

        s->upmix_stereo(ctx, l_phase, r_phase, c_phase, mag_total, x, y, n);
    }
}

static void filter_surround(AVFilterContext *ctx)
{
    AudioSurroundContext *s = ctx->priv;
    float *srcl, *srcr, *srcc;
    int n;

    srcl = (float *)s->input->extended_data[0];
    srcr = (float *)s->input->extended_data[1];
    srcc = (float *)s->input->extended_data[2];

    for (n = 0; n < s->buf_size; n++) {
        float l_re = srcl[2 * n], r_re = srcr[2 * n];
        float l_im = srcl[2 * n + 1], r_im = srcr[2 * n + 1];
        float c_re = srcc[2 * n], c_im = srcc[2 * n + 1];
        float c_mag = hypotf(c_re, c_im);
        float c_phase = atan2f(c_im, c_re);
        float l_mag = hypotf(l_re, l_im);
        float r_mag = hypotf(r_re, r_im);
        float l_phase = atan2f(l_im, l_re);
        float r_phase = atan2f(r_im, r_re);
        float phase_dif = fabsf(l_phase - r_phase);
        float mag_dif = (l_mag - r_mag) / (l_mag + r_mag);
        float mag_total = hypotf(l_mag, r_mag);
        float x, y;

        if (phase_dif > M_PI)
            phase_dif = 2 * M_PI - phase_dif;

        stereo_position(mag_dif, phase_dif, &x, &y);

        s->upmix_3_0(ctx, l_phase, r_phase, c_phase, c_mag, mag_total, x, y, n);
    }
}

static void filter_2_1(AVFilterContext *ctx)
{
    AudioSurroundContext *s = ctx->priv;
    float *srcl, *srcr, *srclfe;
    int n;

    srcl = (float *)s->input->extended_data[0];
    srcr = (float *)s->input->extended_data[1];
    srclfe = (float *)s->input->extended_data[2];

    for (n = 0; n < s->buf_size; n++) {
        float l_re = srcl[2 * n], r_re = srcr[2 * n];
        float l_im = srcl[2 * n + 1], r_im = srcr[2 * n + 1];
        float lfe_re = srclfe[2 * n], lfe_im = srclfe[2 * n + 1];
        float c_phase = atan2f(l_im + r_im, l_re + r_re);
        float l_mag = hypotf(l_re, l_im);
        float r_mag = hypotf(r_re, r_im);
        float l_phase = atan2f(l_im, l_re);
        float r_phase = atan2f(r_im, r_re);
        float phase_dif = fabsf(l_phase - r_phase);
        float mag_dif = (l_mag - r_mag) / (l_mag + r_mag);
        float mag_total = hypotf(l_mag, r_mag);
        float x, y;

        if (phase_dif > M_PI)
            phase_dif = 2 * M_PI - phase_dif;

        stereo_position(mag_dif, phase_dif, &x, &y);

        s->upmix_2_1(ctx, l_phase, r_phase, c_phase, mag_total, lfe_re, lfe_im, x, y, n);
    }
}

static void filter_5_1_back(AVFilterContext *ctx)
{
    AudioSurroundContext *s = ctx->priv;
    float *srcl, *srcr, *srcc, *srclfe, *srcbl, *srcbr;
    int n;

    srcl = (float *)s->input->extended_data[0];
    srcr = (float *)s->input->extended_data[1];
    srcc = (float *)s->input->extended_data[2];
    srclfe = (float *)s->input->extended_data[3];
    srcbl = (float *)s->input->extended_data[4];
    srcbr = (float *)s->input->extended_data[5];

    for (n = 0; n < s->buf_size; n++) {
        float fl_re = srcl[2 * n], fr_re = srcr[2 * n];
        float fl_im = srcl[2 * n + 1], fr_im = srcr[2 * n + 1];
        float c_re = srcc[2 * n], c_im = srcc[2 * n + 1];
        float lfe_re = srclfe[2 * n], lfe_im = srclfe[2 * n + 1];
        float bl_re = srcbl[2 * n], bl_im = srcbl[2 * n + 1];
        float br_re = srcbr[2 * n], br_im = srcbr[2 * n + 1];
        float fl_mag = hypotf(fl_re, fl_im);
        float fr_mag = hypotf(fr_re, fr_im);
        float fl_phase = atan2f(fl_im, fl_re);
        float fr_phase = atan2f(fr_im, fr_re);
        float bl_mag = hypotf(bl_re, bl_im);
        float br_mag = hypotf(br_re, br_im);
        float bl_phase = atan2f(bl_im, bl_re);
        float br_phase = atan2f(br_im, br_re);
        float phase_difl = fabsf(fl_phase - bl_phase);
        float phase_difr = fabsf(fr_phase - br_phase);
        float mag_difl = (fl_mag - bl_mag) / (fl_mag + bl_mag);
        float mag_difr = (fr_mag - br_mag) / (fr_mag + br_mag);
        float mag_totall = hypotf(fl_mag, bl_mag);
        float mag_totalr = hypotf(fr_mag, br_mag);
        float sl_phase = atan2f(fl_im + bl_im, fl_re + bl_re);
        float sr_phase = atan2f(fr_im + br_im, fr_re + br_re);
        float xl, yl;
        float xr, yr;

        if (phase_difl > M_PI)
            phase_difl = 2 * M_PI - phase_difl;

        if (phase_difr > M_PI)
            phase_difr = 2 * M_PI - phase_difr;

        stereo_position(mag_difl, phase_difl, &xl, &yl);
        stereo_position(mag_difr, phase_difr, &xr, &yr);

        s->upmix_5_1(ctx, c_re, c_im, lfe_re, lfe_im,
                     mag_totall, mag_totalr,
                     fl_phase, fr_phase,
                     bl_phase, br_phase,
                     sl_phase, sr_phase,
                     xl, yl, xr, yr, n);
    }
}

static int init(AVFilterContext *ctx)
{
    AudioSurroundContext *s = ctx->priv;
    float overlap;
    int i;

    if (!(s->out_channel_layout = av_get_channel_layout(s->out_channel_layout_str))) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing output channel layout '%s'.\n",
               s->out_channel_layout_str);
        return AVERROR(EINVAL);
    }

    if (!(s->in_channel_layout = av_get_channel_layout(s->in_channel_layout_str))) {
        av_log(ctx, AV_LOG_ERROR, "Error parsing input channel layout '%s'.\n",
               s->in_channel_layout_str);
        return AVERROR(EINVAL);
    }

    if (s->lowcutf >= s->highcutf) {
        av_log(ctx, AV_LOG_ERROR, "Low cut-off '%d' should be less than high cut-off '%d'.\n",
               s->lowcutf, s->highcutf);
        return AVERROR(EINVAL);
    }

    switch (s->in_channel_layout) {
    case AV_CH_LAYOUT_STEREO:
        s->filter = filter_stereo;
        switch (s->out_channel_layout) {
        case AV_CH_LAYOUT_MONO:
            s->upmix_stereo = upmix_1_0;
            break;
        case AV_CH_LAYOUT_STEREO:
            s->upmix_stereo = upmix_stereo;
            break;
        case AV_CH_LAYOUT_2POINT1:
            s->upmix_stereo = upmix_2_1;
            break;
        case AV_CH_LAYOUT_SURROUND:
            s->upmix_stereo = upmix_3_0;
            break;
        case AV_CH_LAYOUT_3POINT1:
            s->upmix_stereo = upmix_3_1;
            break;
        case AV_CH_LAYOUT_4POINT0:
            s->upmix_stereo = upmix_4_0;
            break;
        case AV_CH_LAYOUT_4POINT1:
            s->upmix_stereo = upmix_4_1;
            break;
        case AV_CH_LAYOUT_5POINT0_BACK:
            s->upmix_stereo = upmix_5_0_back;
            break;
        case AV_CH_LAYOUT_5POINT1_BACK:
            s->upmix_stereo = upmix_5_1_back;
            break;
        case AV_CH_LAYOUT_7POINT0:
            s->upmix_stereo = upmix_7_0;
            break;
        case AV_CH_LAYOUT_7POINT1:
            s->upmix_stereo = upmix_7_1;
            break;
        default:
            goto fail;
        }
        break;
    case AV_CH_LAYOUT_2POINT1:
        s->filter = filter_2_1;
        switch (s->out_channel_layout) {
        case AV_CH_LAYOUT_5POINT1_BACK:
            s->upmix_2_1 = upmix_5_1_back_2_1;
            break;
        default:
            goto fail;
        }
        break;
    case AV_CH_LAYOUT_SURROUND:
        s->filter = filter_surround;
        switch (s->out_channel_layout) {
        case AV_CH_LAYOUT_3POINT1:
            s->upmix_3_0 = upmix_3_1_surround;
            break;
        case AV_CH_LAYOUT_5POINT1_BACK:
            s->upmix_3_0 = upmix_5_1_back_surround;
            break;
        default:
            goto fail;
        }
        break;
    case AV_CH_LAYOUT_5POINT1_BACK:
        s->filter = filter_5_1_back;
        switch (s->out_channel_layout) {
        case AV_CH_LAYOUT_7POINT1:
            s->upmix_5_1 = upmix_7_1_5_1;
            break;
        default:
            goto fail;
        }
        break;
    default:
fail:
        av_log(ctx, AV_LOG_ERROR, "Unsupported upmix: '%s' -> '%s'.\n",
               s->in_channel_layout_str, s->out_channel_layout_str);
        return AVERROR(EINVAL);
    }

    s->buf_size = 4096;
    s->pts = AV_NOPTS_VALUE;

    s->window_func_lut = av_calloc(s->buf_size, sizeof(*s->window_func_lut));
    if (!s->window_func_lut)
        return AVERROR(ENOMEM);

    for (i = 0; i < s->buf_size; i++)
        s->window_func_lut[i] = sqrtf(0.5 * (1 - cosf(2 * M_PI * i / s->buf_size)) / s->buf_size);
    overlap = .5;
    s->hop_size = s->buf_size * (1. - overlap);

    return 0;
}

static int fft_channel(AVFilterContext *ctx, void *arg, int ch, int nb_jobs)
{
    AudioSurroundContext *s = ctx->priv;
    const float level_in = s->level_in;
    float *dst;
    int n;

    memset(s->input->extended_data[ch] + s->buf_size * sizeof(float), 0, s->buf_size * sizeof(float));

    dst = (float *)s->input->extended_data[ch];
    for (n = 0; n < s->buf_size; n++) {
        dst[n] *= s->window_func_lut[n] * level_in;
    }

    av_rdft_calc(s->rdft[ch], (float *)s->input->extended_data[ch]);

    return 0;
}

static int ifft_channel(AVFilterContext *ctx, void *arg, int ch, int nb_jobs)
{
    AudioSurroundContext *s = ctx->priv;
    const float level_out = s->level_out;
    AVFrame *out = arg;
    float *dst, *ptr;
    int n;

    av_rdft_calc(s->irdft[ch], (float *)s->output->extended_data[ch]);

    dst = (float *)s->output->extended_data[ch];
    ptr = (float *)s->overlap_buffer->extended_data[ch];

    memmove(s->overlap_buffer->extended_data[ch],
            s->overlap_buffer->extended_data[ch] + s->hop_size * sizeof(float),
            s->buf_size * sizeof(float));
    memset(s->overlap_buffer->extended_data[ch] + s->buf_size * sizeof(float),
           0, s->hop_size * sizeof(float));

    for (n = 0; n < s->buf_size; n++) {
        ptr[n] += dst[n] * s->window_func_lut[n] * level_out;
    }

    ptr = (float *)s->overlap_buffer->extended_data[ch];
    dst = (float *)out->extended_data[ch];
    memcpy(dst, ptr, s->hop_size * sizeof(float));

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    AVFilterLink *outlink = ctx->outputs[0];
    AudioSurroundContext *s = ctx->priv;

    av_audio_fifo_write(s->fifo, (void **)in->extended_data,
                        in->nb_samples);

    if (s->pts == AV_NOPTS_VALUE)
        s->pts = in->pts;

    av_frame_free(&in);

    while (av_audio_fifo_size(s->fifo) >= s->buf_size) {
        AVFrame *out;
        int ret;

        ret = av_audio_fifo_peek(s->fifo, (void **)s->input->extended_data, s->buf_size);
        if (ret < 0)
            return ret;

        ctx->internal->execute(ctx, fft_channel, NULL, NULL, inlink->channels);

        s->filter(ctx);

        out = ff_get_audio_buffer(outlink, s->hop_size);
        if (!out)
            return AVERROR(ENOMEM);

        ctx->internal->execute(ctx, ifft_channel, out, NULL, outlink->channels);

        out->pts = s->pts;
        if (s->pts != AV_NOPTS_VALUE)
            s->pts += av_rescale_q(out->nb_samples, (AVRational){1, outlink->sample_rate}, outlink->time_base);
        av_audio_fifo_drain(s->fifo, s->hop_size);
        ret = ff_filter_frame(outlink, out);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static av_cold void uninit(AVFilterContext *ctx)
{
    AudioSurroundContext *s = ctx->priv;
    int ch;

    av_frame_free(&s->input);
    av_frame_free(&s->output);
    av_frame_free(&s->overlap_buffer);

    for (ch = 0; ch < s->nb_in_channels; ch++) {
        av_rdft_end(s->rdft[ch]);
    }
    for (ch = 0; ch < s->nb_out_channels; ch++) {
        av_rdft_end(s->irdft[ch]);
    }
    av_freep(&s->rdft);
    av_freep(&s->irdft);
    av_audio_fifo_free(s->fifo);
    av_freep(&s->window_func_lut);
}

#define OFFSET(x) offsetof(AudioSurroundContext, x)
#define FLAGS AV_OPT_FLAG_AUDIO_PARAM|AV_OPT_FLAG_FILTERING_PARAM

static const AVOption surround_options[] = {
    { "chl_out",   "set output channel layout", OFFSET(out_channel_layout_str), AV_OPT_TYPE_STRING, {.str="5.1"}, 0,   0, FLAGS },
    { "chl_in",    "set input channel layout",  OFFSET(in_channel_layout_str),  AV_OPT_TYPE_STRING, {.str="stereo"},0, 0, FLAGS },
    { "level_in",  "set input level",           OFFSET(level_in),               AV_OPT_TYPE_FLOAT,  {.dbl=1},     0,  10, FLAGS },
    { "level_out", "set output level",          OFFSET(level_out),              AV_OPT_TYPE_FLOAT,  {.dbl=1},     0,  10, FLAGS },
    { "lfe",       "output LFE",                OFFSET(output_lfe),             AV_OPT_TYPE_BOOL,   {.i64=1},     0,   1, FLAGS },
    { "lfe_low",   "LFE low cut off",           OFFSET(lowcutf),                AV_OPT_TYPE_INT,    {.i64=128},   0, 256, FLAGS },
    { "lfe_high",  "LFE high cut off",          OFFSET(highcutf),               AV_OPT_TYPE_INT,    {.i64=256},   0, 512, FLAGS },
    { NULL }
};

AVFILTER_DEFINE_CLASS(surround);

static const AVFilterPad inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .filter_frame = filter_frame,
        .config_props = config_input,
    },
    { NULL }
};

static const AVFilterPad outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter ff_af_surround = {
    .name           = "surround",
    .description    = NULL_IF_CONFIG_SMALL("Apply audio surround upmix filter."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(AudioSurroundContext),
    .priv_class     = &surround_class,
    .init           = init,
    .uninit         = uninit,
    .inputs         = inputs,
    .outputs        = outputs,
    .flags          = AVFILTER_FLAG_SLICE_THREADS,
};
