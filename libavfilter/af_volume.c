/*
 * Copyright (c) 2011 Stefano Sabatini
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
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
 * audio volume filter
 */

#include "libavutil/channel_layout.h"
#include "libavutil/common.h"
#include "libavutil/eval.h"
#include "libavutil/ffmath.h"
#include "libavutil/float_dsp.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavutil/replaygain.h"

#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "af_volume.h"

static const char * const precision_str[] = {
    "fixed", "float", "double"
};

static const char *const var_names[] = {
    "n",                   ///< frame number (starting at zero)
    "nb_channels",         ///< number of channels
    "nb_consumed_samples", ///< number of samples consumed by the filter
    "nb_samples",          ///< number of samples in the current frame
#if FF_API_FRAME_PKT
    "pos",                 ///< position in the file of the frame
#endif
    "pts",                 ///< frame presentation timestamp
    "sample_rate",         ///< sample rate
    "startpts",            ///< PTS at start of stream
    "startt",              ///< time at start of stream
    "t",                   ///< time in the file of the frame
    "tb",                  ///< timebase
    "volume",              ///< last set value
    NULL
};

#define OFFSET(x) offsetof(VolumeContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM
#define T AV_OPT_FLAG_RUNTIME_PARAM

static const AVOption volume_options[] = {
    { "volume", "set volume adjustment expression",
            OFFSET(volume_expr), AV_OPT_TYPE_STRING, { .str = "1.0" }, .flags = A|F|T },
    { "precision", "select mathematical precision",
            OFFSET(precision), AV_OPT_TYPE_INT, { .i64 = PRECISION_FLOAT }, PRECISION_FIXED, PRECISION_DOUBLE, A|F, .unit = "precision" },
        { "fixed",  "select 8-bit fixed-point",     0, AV_OPT_TYPE_CONST, { .i64 = PRECISION_FIXED  }, INT_MIN, INT_MAX, A|F, .unit = "precision" },
        { "float",  "select 32-bit floating-point", 0, AV_OPT_TYPE_CONST, { .i64 = PRECISION_FLOAT  }, INT_MIN, INT_MAX, A|F, .unit = "precision" },
        { "double", "select 64-bit floating-point", 0, AV_OPT_TYPE_CONST, { .i64 = PRECISION_DOUBLE }, INT_MIN, INT_MAX, A|F, .unit = "precision" },
    { "eval", "specify when to evaluate expressions", OFFSET(eval_mode), AV_OPT_TYPE_INT, {.i64 = EVAL_MODE_ONCE}, 0, EVAL_MODE_NB-1, .flags = A|F, .unit = "eval" },
         { "once",  "eval volume expression once", 0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_ONCE},  .flags = A|F, .unit = "eval" },
         { "frame", "eval volume expression per-frame",                  0, AV_OPT_TYPE_CONST, {.i64=EVAL_MODE_FRAME}, .flags = A|F, .unit = "eval" },
    { "replaygain", "Apply replaygain side data when present",
            OFFSET(replaygain), AV_OPT_TYPE_INT, { .i64 = REPLAYGAIN_DROP }, REPLAYGAIN_DROP, REPLAYGAIN_ALBUM, A|F, .unit = "replaygain" },
        { "drop",   "replaygain side data is dropped", 0, AV_OPT_TYPE_CONST, { .i64 = REPLAYGAIN_DROP   }, 0, 0, A|F, .unit = "replaygain" },
        { "ignore", "replaygain side data is ignored", 0, AV_OPT_TYPE_CONST, { .i64 = REPLAYGAIN_IGNORE }, 0, 0, A|F, .unit = "replaygain" },
        { "track",  "track gain is preferred",         0, AV_OPT_TYPE_CONST, { .i64 = REPLAYGAIN_TRACK  }, 0, 0, A|F, .unit = "replaygain" },
        { "album",  "album gain is preferred",         0, AV_OPT_TYPE_CONST, { .i64 = REPLAYGAIN_ALBUM  }, 0, 0, A|F, .unit = "replaygain" },
    { "replaygain_preamp", "Apply replaygain pre-amplification",
            OFFSET(replaygain_preamp), AV_OPT_TYPE_DOUBLE, { .dbl = 0.0 }, -15.0, 15.0, A|F },
    { "replaygain_noclip", "Apply replaygain clipping prevention",
            OFFSET(replaygain_noclip), AV_OPT_TYPE_BOOL, { .i64 = 1 }, 0, 1, A|F },
    { NULL }
};

AVFILTER_DEFINE_CLASS(volume);

static int set_expr(AVExpr **pexpr, const char *expr, void *log_ctx)
{
    int ret;
    AVExpr *old = NULL;

    if (*pexpr)
        old = *pexpr;
    ret = av_expr_parse(pexpr, expr, var_names,
                        NULL, NULL, NULL, NULL, 0, log_ctx);
    if (ret < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Error when evaluating the volume expression '%s'\n", expr);
        *pexpr = old;
        return ret;
    }

    av_expr_free(old);
    return 0;
}

static av_cold int init(AVFilterContext *ctx)
{
    VolumeContext *vol = ctx->priv;

    vol->fdsp = avpriv_float_dsp_alloc(0);
    if (!vol->fdsp)
        return AVERROR(ENOMEM);

    return set_expr(&vol->volume_pexpr, vol->volume_expr, ctx);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    VolumeContext *vol = ctx->priv;
    av_expr_free(vol->volume_pexpr);
    av_opt_free(vol);
    av_freep(&vol->fdsp);
}

static int query_formats(AVFilterContext *ctx)
{
    VolumeContext *vol = ctx->priv;
    static const enum AVSampleFormat sample_fmts[][7] = {
        [PRECISION_FIXED] = {
            AV_SAMPLE_FMT_U8,
            AV_SAMPLE_FMT_U8P,
            AV_SAMPLE_FMT_S16,
            AV_SAMPLE_FMT_S16P,
            AV_SAMPLE_FMT_S32,
            AV_SAMPLE_FMT_S32P,
            AV_SAMPLE_FMT_NONE
        },
        [PRECISION_FLOAT] = {
            AV_SAMPLE_FMT_FLT,
            AV_SAMPLE_FMT_FLTP,
            AV_SAMPLE_FMT_NONE
        },
        [PRECISION_DOUBLE] = {
            AV_SAMPLE_FMT_DBL,
            AV_SAMPLE_FMT_DBLP,
            AV_SAMPLE_FMT_NONE
        }
    };
    int ret = ff_set_common_all_channel_counts(ctx);
    if (ret < 0)
        return ret;

    ret = ff_set_common_formats_from_list(ctx, sample_fmts[vol->precision]);
    if (ret < 0)
        return ret;

    return ff_set_common_all_samplerates(ctx);
}

static inline void scale_samples_u8(uint8_t *dst, const uint8_t *src,
                                    int nb_samples, int volume)
{
    int i;
    for (i = 0; i < nb_samples; i++)
        dst[i] = av_clip_uint8(((((int64_t)src[i] - 128) * volume + 128) >> 8) + 128);
}

static inline void scale_samples_u8_small(uint8_t *dst, const uint8_t *src,
                                          int nb_samples, int volume)
{
    int i;
    for (i = 0; i < nb_samples; i++)
        dst[i] = av_clip_uint8((((src[i] - 128) * volume + 128) >> 8) + 128);
}

static inline void scale_samples_s16(uint8_t *dst, const uint8_t *src,
                                     int nb_samples, int volume)
{
    int i;
    int16_t *smp_dst       = (int16_t *)dst;
    const int16_t *smp_src = (const int16_t *)src;
    for (i = 0; i < nb_samples; i++)
        smp_dst[i] = av_clip_int16(((int64_t)smp_src[i] * volume + 128) >> 8);
}

static inline void scale_samples_s16_small(uint8_t *dst, const uint8_t *src,
                                           int nb_samples, int volume)
{
    int i;
    int16_t *smp_dst       = (int16_t *)dst;
    const int16_t *smp_src = (const int16_t *)src;
    for (i = 0; i < nb_samples; i++)
        smp_dst[i] = av_clip_int16((smp_src[i] * volume + 128) >> 8);
}

static inline void scale_samples_s32(uint8_t *dst, const uint8_t *src,
                                     int nb_samples, int volume)
{
    int i;
    int32_t *smp_dst       = (int32_t *)dst;
    const int32_t *smp_src = (const int32_t *)src;
    for (i = 0; i < nb_samples; i++)
        smp_dst[i] = av_clipl_int32((((int64_t)smp_src[i] * volume + 128) >> 8));
}

static av_cold void volume_init(VolumeContext *vol)
{
    vol->samples_align = 1;

    switch (av_get_packed_sample_fmt(vol->sample_fmt)) {
    case AV_SAMPLE_FMT_U8:
        if (vol->volume_i < 0x1000000)
            vol->scale_samples = scale_samples_u8_small;
        else
            vol->scale_samples = scale_samples_u8;
        break;
    case AV_SAMPLE_FMT_S16:
        if (vol->volume_i < 0x10000)
            vol->scale_samples = scale_samples_s16_small;
        else
            vol->scale_samples = scale_samples_s16;
        break;
    case AV_SAMPLE_FMT_S32:
        vol->scale_samples = scale_samples_s32;
        break;
    case AV_SAMPLE_FMT_FLT:
        vol->samples_align = 4;
        break;
    case AV_SAMPLE_FMT_DBL:
        vol->samples_align = 8;
        break;
    }

#if ARCH_X86
    ff_volume_init_x86(vol);
#endif
}

static int set_volume(AVFilterContext *ctx)
{
    VolumeContext *vol = ctx->priv;

    vol->volume = av_expr_eval(vol->volume_pexpr, vol->var_values, NULL);
    if (isnan(vol->volume)) {
        if (vol->eval_mode == EVAL_MODE_ONCE) {
            av_log(ctx, AV_LOG_ERROR, "Invalid value NaN for volume\n");
            return AVERROR(EINVAL);
        } else {
            av_log(ctx, AV_LOG_WARNING, "Invalid value NaN for volume, setting to 0\n");
            vol->volume = 0;
        }
    }
    vol->var_values[VAR_VOLUME] = vol->volume;

    av_log(ctx, AV_LOG_VERBOSE, "n:%f t:%f pts:%f precision:%s ",
           vol->var_values[VAR_N], vol->var_values[VAR_T], vol->var_values[VAR_PTS],
           precision_str[vol->precision]);

    if (vol->precision == PRECISION_FIXED) {
        vol->volume_i = (int)(vol->volume * 256 + 0.5);
        vol->volume   = vol->volume_i / 256.0;
        av_log(ctx, AV_LOG_VERBOSE, "volume_i:%d/255 ", vol->volume_i);
    }
    av_log(ctx, AV_LOG_VERBOSE, "volume:%f volume_dB:%f\n",
           vol->volume, 20.0*log10(vol->volume));

    volume_init(vol);
    return 0;
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    VolumeContext *vol   = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    vol->sample_fmt = inlink->format;
    vol->channels   = inlink->ch_layout.nb_channels;
    vol->planes     = av_sample_fmt_is_planar(inlink->format) ? vol->channels : 1;

    vol->var_values[VAR_N] =
    vol->var_values[VAR_NB_CONSUMED_SAMPLES] =
    vol->var_values[VAR_NB_SAMPLES] =
#if FF_API_FRAME_PKT
    vol->var_values[VAR_POS] =
#endif
    vol->var_values[VAR_PTS] =
    vol->var_values[VAR_STARTPTS] =
    vol->var_values[VAR_STARTT] =
    vol->var_values[VAR_T] =
    vol->var_values[VAR_VOLUME] = NAN;

    vol->var_values[VAR_NB_CHANNELS] = inlink->ch_layout.nb_channels;
    vol->var_values[VAR_TB]          = av_q2d(inlink->time_base);
    vol->var_values[VAR_SAMPLE_RATE] = inlink->sample_rate;

    av_log(inlink->src, AV_LOG_VERBOSE, "tb:%f sample_rate:%f nb_channels:%f\n",
           vol->var_values[VAR_TB],
           vol->var_values[VAR_SAMPLE_RATE],
           vol->var_values[VAR_NB_CHANNELS]);

    return set_volume(ctx);
}

static int process_command(AVFilterContext *ctx, const char *cmd, const char *args,
                           char *res, int res_len, int flags)
{
    VolumeContext *vol = ctx->priv;
    int ret = AVERROR(ENOSYS);

    if (!strcmp(cmd, "volume")) {
        if ((ret = set_expr(&vol->volume_pexpr, args, ctx)) < 0)
            return ret;
        if (vol->eval_mode == EVAL_MODE_ONCE)
            set_volume(ctx);
    }

    return ret;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    AVFilterContext *ctx = inlink->dst;
    VolumeContext *vol    = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    int nb_samples        = buf->nb_samples;
    AVFrame *out_buf;
    AVFrameSideData *sd = av_frame_get_side_data(buf, AV_FRAME_DATA_REPLAYGAIN);
    int ret;

    if (sd && vol->replaygain != REPLAYGAIN_IGNORE) {
        if (vol->replaygain != REPLAYGAIN_DROP) {
            AVReplayGain *replaygain = (AVReplayGain*)sd->data;
            int32_t gain  = 100000;
            uint32_t peak = 100000;
            float g, p;

            if (vol->replaygain == REPLAYGAIN_TRACK &&
                replaygain->track_gain != INT32_MIN) {
                gain = replaygain->track_gain;

                if (replaygain->track_peak != 0)
                    peak = replaygain->track_peak;
            } else if (replaygain->album_gain != INT32_MIN) {
                gain = replaygain->album_gain;

                if (replaygain->album_peak != 0)
                    peak = replaygain->album_peak;
            } else {
                av_log(inlink->dst, AV_LOG_WARNING, "Both ReplayGain gain "
                       "values are unknown.\n");
            }
            g = gain / 100000.0f;
            p = peak / 100000.0f;

            av_log(inlink->dst, AV_LOG_VERBOSE,
                   "Using gain %f dB from replaygain side data.\n", g);

            vol->volume   = ff_exp10((g + vol->replaygain_preamp) / 20);
            if (vol->replaygain_noclip)
                vol->volume = FFMIN(vol->volume, 1.0 / p);
            vol->volume_i = (int)(vol->volume * 256 + 0.5);

            volume_init(vol);
        }
        av_frame_remove_side_data(buf, AV_FRAME_DATA_REPLAYGAIN);
    }

    if (isnan(vol->var_values[VAR_STARTPTS])) {
        vol->var_values[VAR_STARTPTS] = TS2D(buf->pts);
        vol->var_values[VAR_STARTT  ] = TS2T(buf->pts, inlink->time_base);
    }
    vol->var_values[VAR_PTS] = TS2D(buf->pts);
    vol->var_values[VAR_T  ] = TS2T(buf->pts, inlink->time_base);
    vol->var_values[VAR_N  ] = inlink->frame_count_out;

#if FF_API_FRAME_PKT
FF_DISABLE_DEPRECATION_WARNINGS
    {
        int64_t pos;
        pos = buf->pkt_pos;
        vol->var_values[VAR_POS] = pos == -1 ? NAN : pos;
    }
FF_ENABLE_DEPRECATION_WARNINGS
#endif
    if (vol->eval_mode == EVAL_MODE_FRAME)
        set_volume(ctx);

    if (vol->volume == 1.0 || vol->volume_i == 256) {
        out_buf = buf;
        goto end;
    }

    /* do volume scaling in-place if input buffer is writable */
    if (av_frame_is_writable(buf)
            && (vol->precision != PRECISION_FIXED || vol->volume_i > 0)) {
        out_buf = buf;
    } else {
        out_buf = ff_get_audio_buffer(outlink, nb_samples);
        if (!out_buf) {
            av_frame_free(&buf);
            return AVERROR(ENOMEM);
        }
        ret = av_frame_copy_props(out_buf, buf);
        if (ret < 0) {
            av_frame_free(&out_buf);
            av_frame_free(&buf);
            return ret;
        }
    }

    if (vol->precision != PRECISION_FIXED || vol->volume_i > 0) {
        int p, plane_samples;

        if (av_sample_fmt_is_planar(buf->format))
            plane_samples = FFALIGN(nb_samples, vol->samples_align);
        else
            plane_samples = FFALIGN(nb_samples * vol->channels, vol->samples_align);

        if (vol->precision == PRECISION_FIXED) {
            for (p = 0; p < vol->planes; p++) {
                vol->scale_samples(out_buf->extended_data[p],
                                   buf->extended_data[p], plane_samples,
                                   vol->volume_i);
            }
        } else if (av_get_packed_sample_fmt(vol->sample_fmt) == AV_SAMPLE_FMT_FLT) {
            for (p = 0; p < vol->planes; p++) {
                vol->fdsp->vector_fmul_scalar((float *)out_buf->extended_data[p],
                                             (const float *)buf->extended_data[p],
                                             vol->volume, plane_samples);
            }
        } else {
            for (p = 0; p < vol->planes; p++) {
                vol->fdsp->vector_dmul_scalar((double *)out_buf->extended_data[p],
                                             (const double *)buf->extended_data[p],
                                             vol->volume, plane_samples);
            }
        }
    }

    if (buf != out_buf)
        av_frame_free(&buf);

end:
    vol->var_values[VAR_NB_CONSUMED_SAMPLES] += out_buf->nb_samples;
    return ff_filter_frame(outlink, out_buf);
}

static const AVFilterPad avfilter_af_volume_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
    },
};

static const AVFilterPad avfilter_af_volume_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
};

const AVFilter ff_af_volume = {
    .name           = "volume",
    .description    = NULL_IF_CONFIG_SMALL("Change input volume."),
    .priv_size      = sizeof(VolumeContext),
    .priv_class     = &volume_class,
    .init           = init,
    .uninit         = uninit,
    FILTER_INPUTS(avfilter_af_volume_inputs),
    FILTER_OUTPUTS(avfilter_af_volume_outputs),
    FILTER_QUERY_FUNC(query_formats),
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
    .process_command = process_command,
};
