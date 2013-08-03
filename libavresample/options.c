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
#include "libavutil/mem.h"
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

static const AVOption avresample_options[] = {
    { "in_channel_layout",      "Input Channel Layout",     OFFSET(in_channel_layout),      AV_OPT_TYPE_INT64,  { .i64 = 0              }, INT64_MIN,            INT64_MAX,              PARAM },
    { "in_sample_fmt",          "Input Sample Format",      OFFSET(in_sample_fmt),          AV_OPT_TYPE_INT,    { .i64 = AV_SAMPLE_FMT_S16 }, AV_SAMPLE_FMT_U8,     AV_SAMPLE_FMT_NB-1,     PARAM },
    { "in_sample_rate",         "Input Sample Rate",        OFFSET(in_sample_rate),         AV_OPT_TYPE_INT,    { .i64 = 48000          }, 1,                    INT_MAX,                PARAM },
    { "out_channel_layout",     "Output Channel Layout",    OFFSET(out_channel_layout),     AV_OPT_TYPE_INT64,  { .i64 = 0              }, INT64_MIN,            INT64_MAX,              PARAM },
    { "out_sample_fmt",         "Output Sample Format",     OFFSET(out_sample_fmt),         AV_OPT_TYPE_INT,    { .i64 = AV_SAMPLE_FMT_S16 }, AV_SAMPLE_FMT_U8,     AV_SAMPLE_FMT_NB-1,     PARAM },
    { "out_sample_rate",        "Output Sample Rate",       OFFSET(out_sample_rate),        AV_OPT_TYPE_INT,    { .i64 = 48000          }, 1,                    INT_MAX,                PARAM },
    { "internal_sample_fmt",    "Internal Sample Format",   OFFSET(internal_sample_fmt),    AV_OPT_TYPE_INT,    { .i64 = AV_SAMPLE_FMT_NONE }, AV_SAMPLE_FMT_NONE,   AV_SAMPLE_FMT_NB-1,     PARAM, "internal_sample_fmt" },
        {"u8" ,  "8-bit unsigned integer",        0, AV_OPT_TYPE_CONST, {.i64 = AV_SAMPLE_FMT_U8   }, INT_MIN, INT_MAX, PARAM, "internal_sample_fmt"},
        {"s16",  "16-bit signed integer",         0, AV_OPT_TYPE_CONST, {.i64 = AV_SAMPLE_FMT_S16  }, INT_MIN, INT_MAX, PARAM, "internal_sample_fmt"},
        {"s32",  "32-bit signed integer",         0, AV_OPT_TYPE_CONST, {.i64 = AV_SAMPLE_FMT_S32  }, INT_MIN, INT_MAX, PARAM, "internal_sample_fmt"},
        {"flt",  "32-bit float",                  0, AV_OPT_TYPE_CONST, {.i64 = AV_SAMPLE_FMT_FLT  }, INT_MIN, INT_MAX, PARAM, "internal_sample_fmt"},
        {"dbl",  "64-bit double",                 0, AV_OPT_TYPE_CONST, {.i64 = AV_SAMPLE_FMT_DBL  }, INT_MIN, INT_MAX, PARAM, "internal_sample_fmt"},
        {"u8p" , "8-bit unsigned integer planar", 0, AV_OPT_TYPE_CONST, {.i64 = AV_SAMPLE_FMT_U8P  }, INT_MIN, INT_MAX, PARAM, "internal_sample_fmt"},
        {"s16p", "16-bit signed integer planar",  0, AV_OPT_TYPE_CONST, {.i64 = AV_SAMPLE_FMT_S16P }, INT_MIN, INT_MAX, PARAM, "internal_sample_fmt"},
        {"s32p", "32-bit signed integer planar",  0, AV_OPT_TYPE_CONST, {.i64 = AV_SAMPLE_FMT_S32P }, INT_MIN, INT_MAX, PARAM, "internal_sample_fmt"},
        {"fltp", "32-bit float planar",           0, AV_OPT_TYPE_CONST, {.i64 = AV_SAMPLE_FMT_FLTP }, INT_MIN, INT_MAX, PARAM, "internal_sample_fmt"},
        {"dblp", "64-bit double planar",          0, AV_OPT_TYPE_CONST, {.i64 = AV_SAMPLE_FMT_DBLP }, INT_MIN, INT_MAX, PARAM, "internal_sample_fmt"},
    { "mix_coeff_type",         "Mixing Coefficient Type",  OFFSET(mix_coeff_type),         AV_OPT_TYPE_INT,    { .i64 = AV_MIX_COEFF_TYPE_FLT }, AV_MIX_COEFF_TYPE_Q8, AV_MIX_COEFF_TYPE_NB-1, PARAM, "mix_coeff_type" },
        { "q8",  "16-bit 8.8 Fixed-Point",   0, AV_OPT_TYPE_CONST, { .i64 = AV_MIX_COEFF_TYPE_Q8  }, INT_MIN, INT_MAX, PARAM, "mix_coeff_type" },
        { "q15", "32-bit 17.15 Fixed-Point", 0, AV_OPT_TYPE_CONST, { .i64 = AV_MIX_COEFF_TYPE_Q15 }, INT_MIN, INT_MAX, PARAM, "mix_coeff_type" },
        { "flt", "Floating-Point",           0, AV_OPT_TYPE_CONST, { .i64 = AV_MIX_COEFF_TYPE_FLT }, INT_MIN, INT_MAX, PARAM, "mix_coeff_type" },
    { "center_mix_level",       "Center Mix Level",         OFFSET(center_mix_level),       AV_OPT_TYPE_DOUBLE, { .dbl = M_SQRT1_2      }, -32.0,                32.0,                   PARAM },
    { "surround_mix_level",     "Surround Mix Level",       OFFSET(surround_mix_level),     AV_OPT_TYPE_DOUBLE, { .dbl = M_SQRT1_2      }, -32.0,                32.0,                   PARAM },
    { "lfe_mix_level",          "LFE Mix Level",            OFFSET(lfe_mix_level),          AV_OPT_TYPE_DOUBLE, { .dbl = 0.0            }, -32.0,                32.0,                   PARAM },
    { "normalize_mix_level",    "Normalize Mix Level",      OFFSET(normalize_mix_level),    AV_OPT_TYPE_INT,    { .i64 = 1              }, 0,                    1,                      PARAM },
    { "force_resampling",       "Force Resampling",         OFFSET(force_resampling),       AV_OPT_TYPE_INT,    { .i64 = 0              }, 0,                    1,                      PARAM },
    { "filter_size",            "Resampling Filter Size",   OFFSET(filter_size),            AV_OPT_TYPE_INT,    { .i64 = 16             }, 0,                    32, /* ??? */           PARAM },
    { "phase_shift",            "Resampling Phase Shift",   OFFSET(phase_shift),            AV_OPT_TYPE_INT,    { .i64 = 10             }, 0,                    30, /* ??? */           PARAM },
    { "linear_interp",          "Use Linear Interpolation", OFFSET(linear_interp),          AV_OPT_TYPE_INT,    { .i64 = 0              }, 0,                    1,                      PARAM },
    { "cutoff",                 "Cutoff Frequency Ratio",   OFFSET(cutoff),                 AV_OPT_TYPE_DOUBLE, { .dbl = 0.8            }, 0.0,                  1.0,                    PARAM },
    /* duplicate option in order to work with avconv */
    { "resample_cutoff",        "Cutoff Frequency Ratio",   OFFSET(cutoff),                 AV_OPT_TYPE_DOUBLE, { .dbl = 0.8            }, 0.0,                  1.0,                    PARAM },
    { "matrix_encoding",        "Matrixed Stereo Encoding", OFFSET(matrix_encoding),        AV_OPT_TYPE_INT,    {.i64 =  AV_MATRIX_ENCODING_NONE}, AV_MATRIX_ENCODING_NONE,     AV_MATRIX_ENCODING_NB-1, PARAM, "matrix_encoding" },
        { "none",  "None",               0, AV_OPT_TYPE_CONST, { .i64 = AV_MATRIX_ENCODING_NONE  }, INT_MIN, INT_MAX, PARAM, "matrix_encoding" },
        { "dolby", "Dolby",              0, AV_OPT_TYPE_CONST, { .i64 = AV_MATRIX_ENCODING_DOLBY }, INT_MIN, INT_MAX, PARAM, "matrix_encoding" },
        { "dplii", "Dolby Pro Logic II", 0, AV_OPT_TYPE_CONST, { .i64 = AV_MATRIX_ENCODING_DPLII }, INT_MIN, INT_MAX, PARAM, "matrix_encoding" },
    { "filter_type",            "Filter Type",              OFFSET(filter_type),            AV_OPT_TYPE_INT,    { .i64 = AV_RESAMPLE_FILTER_TYPE_KAISER }, AV_RESAMPLE_FILTER_TYPE_CUBIC, AV_RESAMPLE_FILTER_TYPE_KAISER,  PARAM, "filter_type" },
        { "cubic",            "Cubic",                          0, AV_OPT_TYPE_CONST, { .i64 = AV_RESAMPLE_FILTER_TYPE_CUBIC            }, INT_MIN, INT_MAX, PARAM, "filter_type" },
        { "blackman_nuttall", "Blackman Nuttall Windowed Sinc", 0, AV_OPT_TYPE_CONST, { .i64 = AV_RESAMPLE_FILTER_TYPE_BLACKMAN_NUTTALL }, INT_MIN, INT_MAX, PARAM, "filter_type" },
        { "kaiser",           "Kaiser Windowed Sinc",           0, AV_OPT_TYPE_CONST, { .i64 = AV_RESAMPLE_FILTER_TYPE_KAISER           }, INT_MIN, INT_MAX, PARAM, "filter_type" },
    { "kaiser_beta",            "Kaiser Window Beta",       OFFSET(kaiser_beta),            AV_OPT_TYPE_INT,    { .i64 = 9              }, 2,                    16,                     PARAM },
    { "dither_method",          "Dither Method",            OFFSET(dither_method),          AV_OPT_TYPE_INT,    { .i64 = AV_RESAMPLE_DITHER_NONE }, 0, AV_RESAMPLE_DITHER_NB-1, PARAM, "dither_method"},
        {"none",          "No Dithering",                         0, AV_OPT_TYPE_CONST, { .i64 = AV_RESAMPLE_DITHER_NONE          }, INT_MIN, INT_MAX, PARAM, "dither_method"},
        {"rectangular",   "Rectangular Dither",                   0, AV_OPT_TYPE_CONST, { .i64 = AV_RESAMPLE_DITHER_RECTANGULAR   }, INT_MIN, INT_MAX, PARAM, "dither_method"},
        {"triangular",    "Triangular Dither",                    0, AV_OPT_TYPE_CONST, { .i64 = AV_RESAMPLE_DITHER_TRIANGULAR    }, INT_MIN, INT_MAX, PARAM, "dither_method"},
        {"triangular_hp", "Triangular Dither With High Pass",     0, AV_OPT_TYPE_CONST, { .i64 = AV_RESAMPLE_DITHER_TRIANGULAR_HP }, INT_MIN, INT_MAX, PARAM, "dither_method"},
        {"triangular_ns", "Triangular Dither With Noise Shaping", 0, AV_OPT_TYPE_CONST, { .i64 = AV_RESAMPLE_DITHER_TRIANGULAR_NS }, INT_MIN, INT_MAX, PARAM, "dither_method"},
    { NULL },
};

static const AVClass av_resample_context_class = {
    .class_name = "AVAudioResampleContext",
    .item_name  = av_default_item_name,
    .option     = avresample_options,
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

    return avr;
}

const AVClass *avresample_get_class(void)
{
    return &av_resample_context_class;
}
