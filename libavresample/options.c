/*
 * Copyright (c) 2012 Justin Ruggles <justin.ruggles@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/mathematics.h"
#include "libavutil/opt.h"
#include "avresample.h"
#include "internal.h"
#include "audio_mix.h"

/**
 * @file
 * Options definition for AVAudioResampleContext.
 */

#define OFFSET(x) offsetof(AVAudioResampleContext, x)
#define PARAM AV_OPT_FLAG_AUDIO_PARAM

static const AVOption options[] = {
    { "in_channel_layout",      "Input Channel Layout",     OFFSET(in_channel_layout),      AV_OPT_TYPE_INT64,  { 0                     }, INT64_MIN,            INT64_MAX,              PARAM },
    { "in_sample_fmt",          "Input Sample Format",      OFFSET(in_sample_fmt),          AV_OPT_TYPE_INT,    { AV_SAMPLE_FMT_S16     }, AV_SAMPLE_FMT_U8,     AV_SAMPLE_FMT_NB-1,     PARAM },
    { "in_sample_rate",         "Input Sample Rate",        OFFSET(in_sample_rate),         AV_OPT_TYPE_INT,    { 48000                 }, 1,                    INT_MAX,                PARAM },
    { "out_channel_layout",     "Output Channel Layout",    OFFSET(out_channel_layout),     AV_OPT_TYPE_INT64,  { 0                     }, INT64_MIN,            INT64_MAX,              PARAM },
    { "out_sample_fmt",         "Output Sample Format",     OFFSET(out_sample_fmt),         AV_OPT_TYPE_INT,    { AV_SAMPLE_FMT_S16     }, AV_SAMPLE_FMT_U8,     AV_SAMPLE_FMT_NB-1,     PARAM },
    { "out_sample_rate",        "Output Sample Rate",       OFFSET(out_sample_rate),        AV_OPT_TYPE_INT,    { 48000                 }, 1,                    INT_MAX,                PARAM },
    { "internal_sample_fmt",    "Internal Sample Format",   OFFSET(internal_sample_fmt),    AV_OPT_TYPE_INT,    { AV_SAMPLE_FMT_FLTP    }, AV_SAMPLE_FMT_NONE,   AV_SAMPLE_FMT_NB-1,     PARAM },
    { "mix_coeff_type",         "Mixing Coefficient Type",  OFFSET(mix_coeff_type),         AV_OPT_TYPE_INT,    { AV_MIX_COEFF_TYPE_FLT }, AV_MIX_COEFF_TYPE_Q8, AV_MIX_COEFF_TYPE_NB-1, PARAM, "mix_coeff_type" },
        { "q8",  "16-bit 8.8 Fixed-Point",   0, AV_OPT_TYPE_CONST, { AV_MIX_COEFF_TYPE_Q8  }, INT_MIN, INT_MAX, PARAM, "mix_coeff_type" },
        { "q15", "32-bit 17.15 Fixed-Point", 0, AV_OPT_TYPE_CONST, { AV_MIX_COEFF_TYPE_Q15 }, INT_MIN, INT_MAX, PARAM, "mix_coeff_type" },
        { "flt", "Floating-Point",           0, AV_OPT_TYPE_CONST, { AV_MIX_COEFF_TYPE_FLT }, INT_MIN, INT_MAX, PARAM, "mix_coeff_type" },
    { "center_mix_level",       "Center Mix Level",         OFFSET(center_mix_level),       AV_OPT_TYPE_DOUBLE, { M_SQRT1_2             }, -32.0,                32.0,                   PARAM },
    { "surround_mix_level",     "Surround Mix Level",       OFFSET(surround_mix_level),     AV_OPT_TYPE_DOUBLE, { M_SQRT1_2             }, -32.0,                32.0,                   PARAM },
    { "lfe_mix_level",          "LFE Mix Level",            OFFSET(lfe_mix_level),          AV_OPT_TYPE_DOUBLE, { 0.0                   }, -32.0,                32.0,                   PARAM },
    { "force_resampling",       "Force Resampling",         OFFSET(force_resampling),       AV_OPT_TYPE_INT,    { 0                     }, 0,                    1,                      PARAM },
    { "filter_size",            "Resampling Filter Size",   OFFSET(filter_size),            AV_OPT_TYPE_INT,    { 16                    }, 0,                    32, /* ??? */           PARAM },
    { "phase_shift",            "Resampling Phase Shift",   OFFSET(phase_shift),            AV_OPT_TYPE_INT,    { 10                    }, 0,                    30, /* ??? */           PARAM },
    { "linear_interp",          "Use Linear Interpolation", OFFSET(linear_interp),          AV_OPT_TYPE_INT,    { 0                     }, 0,                    1,                      PARAM },
    { "cutoff",                 "Cutoff Frequency Ratio",   OFFSET(cutoff),                 AV_OPT_TYPE_DOUBLE, { 0.8                   }, 0.0,                  1.0,                    PARAM },
    { NULL },
};

static const AVClass av_resample_context_class = {
    .class_name = "AVAudioResampleContext",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVAudioResampleContext *avresample_alloc_context(void)
{
    AVAudioResampleContext *avr;

    avr = av_mallocz(sizeof(*avr));
    if (!avr)
        return NULL;

    avr->av_class = &av_resample_context_class;
    av_opt_set_defaults(avr);

    avr->am = av_mallocz(sizeof(*avr->am));
    if (!avr->am) {
        av_free(avr);
        return NULL;
    }
    avr->am->avr = avr;

    return avr;
}

const AVClass *avresample_get_class(void)
{
    return &av_resample_context_class;
}
