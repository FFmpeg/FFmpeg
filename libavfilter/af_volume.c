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
#include "libavutil/float_dsp.h"
#include "libavutil/opt.h"
#include "audio.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "af_volume.h"

static const char *precision_str[] = {
    "fixed", "float", "double"
};

#define OFFSET(x) offsetof(VolumeContext, x)
#define A AV_OPT_FLAG_AUDIO_PARAM
#define F AV_OPT_FLAG_FILTERING_PARAM

static const AVOption volume_options[] = {
    { "volume", "set volume adjustment",
            OFFSET(volume), AV_OPT_TYPE_DOUBLE, { .dbl = 1.0 }, 0, 0x7fffff, A|F },
    { "precision", "select mathematical precision",
            OFFSET(precision), AV_OPT_TYPE_INT, { .i64 = PRECISION_FLOAT }, PRECISION_FIXED, PRECISION_DOUBLE, A|F, "precision" },
        { "fixed",  "select 8-bit fixed-point",     0, AV_OPT_TYPE_CONST, { .i64 = PRECISION_FIXED  }, INT_MIN, INT_MAX, A|F, "precision" },
        { "float",  "select 32-bit floating-point", 0, AV_OPT_TYPE_CONST, { .i64 = PRECISION_FLOAT  }, INT_MIN, INT_MAX, A|F, "precision" },
        { "double", "select 64-bit floating-point", 0, AV_OPT_TYPE_CONST, { .i64 = PRECISION_DOUBLE }, INT_MIN, INT_MAX, A|F, "precision" },
    { NULL },
};

AVFILTER_DEFINE_CLASS(volume);

static av_cold int init(AVFilterContext *ctx)
{
    VolumeContext *vol = ctx->priv;

    if (vol->precision == PRECISION_FIXED) {
        vol->volume_i = (int)(vol->volume * 256 + 0.5);
        vol->volume   = vol->volume_i / 256.0;
        av_log(ctx, AV_LOG_VERBOSE, "volume:(%d/256)(%f)(%1.2fdB) precision:fixed\n",
               vol->volume_i, vol->volume, 20.0*log(vol->volume)/M_LN10);
    } else {
        av_log(ctx, AV_LOG_VERBOSE, "volume:(%f)(%1.2fdB) precision:%s\n",
               vol->volume, 20.0*log(vol->volume)/M_LN10,
               precision_str[vol->precision]);
    }

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    VolumeContext *vol = ctx->priv;
    AVFilterFormats *formats = NULL;
    AVFilterChannelLayouts *layouts;
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

    layouts = ff_all_channel_layouts();
    if (!layouts)
        return AVERROR(ENOMEM);
    ff_set_common_channel_layouts(ctx, layouts);

    formats = ff_make_format_list(sample_fmts[vol->precision]);
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_formats(ctx, formats);

    formats = ff_all_samplerates();
    if (!formats)
        return AVERROR(ENOMEM);
    ff_set_common_samplerates(ctx, formats);

    return 0;
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
        avpriv_float_dsp_init(&vol->fdsp, 0);
        vol->samples_align = 4;
        break;
    case AV_SAMPLE_FMT_DBL:
        avpriv_float_dsp_init(&vol->fdsp, 0);
        vol->samples_align = 8;
        break;
    }

    if (ARCH_X86)
        ff_volume_init_x86(vol);
}

static int config_output(AVFilterLink *outlink)
{
    AVFilterContext *ctx = outlink->src;
    VolumeContext *vol   = ctx->priv;
    AVFilterLink *inlink = ctx->inputs[0];

    vol->sample_fmt = inlink->format;
    vol->channels   = av_get_channel_layout_nb_channels(inlink->channel_layout);
    vol->planes     = av_sample_fmt_is_planar(inlink->format) ? vol->channels : 1;

    volume_init(vol);

    return 0;
}

static int filter_frame(AVFilterLink *inlink, AVFrame *buf)
{
    VolumeContext *vol    = inlink->dst->priv;
    AVFilterLink *outlink = inlink->dst->outputs[0];
    int nb_samples        = buf->nb_samples;
    AVFrame *out_buf;

    if (vol->volume == 1.0 || vol->volume_i == 256)
        return ff_filter_frame(outlink, buf);

    /* do volume scaling in-place if input buffer is writable */
    if (av_frame_is_writable(buf)) {
        out_buf = buf;
    } else {
        out_buf = ff_get_audio_buffer(inlink, nb_samples);
        if (!out_buf)
            return AVERROR(ENOMEM);
        av_frame_copy_props(out_buf, buf);
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
                vol->fdsp.vector_fmul_scalar((float *)out_buf->extended_data[p],
                                             (const float *)buf->extended_data[p],
                                             vol->volume, plane_samples);
            }
        } else {
            for (p = 0; p < vol->planes; p++) {
                vol->fdsp.vector_dmul_scalar((double *)out_buf->extended_data[p],
                                             (const double *)buf->extended_data[p],
                                             vol->volume, plane_samples);
            }
        }
    }

    if (buf != out_buf)
        av_frame_free(&buf);

    return ff_filter_frame(outlink, out_buf);
}

static const AVFilterPad avfilter_af_volume_inputs[] = {
    {
        .name           = "default",
        .type           = AVMEDIA_TYPE_AUDIO,
        .filter_frame   = filter_frame,
    },
    { NULL }
};

static const AVFilterPad avfilter_af_volume_outputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_AUDIO,
        .config_props = config_output,
    },
    { NULL }
};

AVFilter avfilter_af_volume = {
    .name           = "volume",
    .description    = NULL_IF_CONFIG_SMALL("Change input volume."),
    .query_formats  = query_formats,
    .priv_size      = sizeof(VolumeContext),
    .priv_class     = &volume_class,
    .init           = init,
    .inputs         = avfilter_af_volume_inputs,
    .outputs        = avfilter_af_volume_outputs,
    .flags          = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
