/*
 * Copyright (C) 2011-2013 Michael Niedermayer (michaelni@gmx.at)
 *
 * This file is part of libswresample
 *
 * libswresample is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * libswresample is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with libswresample; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/channel_layout.h"
#include "libavutil/opt.h"
#include "swresample_internal.h"

#include <float.h>

#define  C30DB  M_SQRT2
#define  C15DB  1.189207115
#define C__0DB  1.0
#define C_15DB  0.840896415
#define C_30DB  M_SQRT1_2
#define C_45DB  0.594603558
#define C_60DB  0.5

#define OFFSET(x) offsetof(SwrContext,x)
#define PARAM AV_OPT_FLAG_AUDIO_PARAM
#define DEPREC AV_OPT_FLAG_DEPRECATED

static const AVOption options[]={
{"isr"                  , "set input sample rate"       , OFFSET( in_sample_rate), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , INT_MAX   , PARAM},
{"in_sample_rate"       , "set input sample rate"       , OFFSET( in_sample_rate), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , INT_MAX   , PARAM},
{"osr"                  , "set output sample rate"      , OFFSET(out_sample_rate), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , INT_MAX   , PARAM},
{"out_sample_rate"      , "set output sample rate"      , OFFSET(out_sample_rate), AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , INT_MAX   , PARAM},
{"isf"                  , "set input sample format"     , OFFSET( in_sample_fmt ), AV_OPT_TYPE_SAMPLE_FMT , {.i64=AV_SAMPLE_FMT_NONE}, -1   , INT_MAX, PARAM},
{"in_sample_fmt"        , "set input sample format"     , OFFSET( in_sample_fmt ), AV_OPT_TYPE_SAMPLE_FMT , {.i64=AV_SAMPLE_FMT_NONE}, -1   , INT_MAX, PARAM},
{"osf"                  , "set output sample format"    , OFFSET(out_sample_fmt ), AV_OPT_TYPE_SAMPLE_FMT , {.i64=AV_SAMPLE_FMT_NONE}, -1   , INT_MAX, PARAM},
{"out_sample_fmt"       , "set output sample format"    , OFFSET(out_sample_fmt ), AV_OPT_TYPE_SAMPLE_FMT , {.i64=AV_SAMPLE_FMT_NONE}, -1   , INT_MAX, PARAM},
{"tsf"                  , "set internal sample format"  , OFFSET(user_int_sample_fmt), AV_OPT_TYPE_SAMPLE_FMT , {.i64=AV_SAMPLE_FMT_NONE}, -1   , INT_MAX, PARAM},
{"internal_sample_fmt"  , "set internal sample format"  , OFFSET(user_int_sample_fmt), AV_OPT_TYPE_SAMPLE_FMT , {.i64=AV_SAMPLE_FMT_NONE}, -1   , INT_MAX, PARAM},
{"ichl"                 , "set input channel layout"    , OFFSET(user_in_chlayout ), AV_OPT_TYPE_CHLAYOUT, {.str=NULL             }, 0, 0 , PARAM, .unit = "chlayout"},
{"in_chlayout"          , "set input channel layout"    , OFFSET(user_in_chlayout ), AV_OPT_TYPE_CHLAYOUT, {.str=NULL             }, 0, 0 , PARAM, .unit = "chlayout"},
{"ochl"                 , "set output channel layout"   , OFFSET(user_out_chlayout), AV_OPT_TYPE_CHLAYOUT, {.str=NULL             }, 0, 0 , PARAM, .unit = "chlayout"},
{"out_chlayout"         , "set output channel layout"   , OFFSET(user_out_chlayout), AV_OPT_TYPE_CHLAYOUT, {.str=NULL             }, 0, 0 , PARAM, .unit = "chlayout"},
{"uchl"                 , "set used channel layout"     , OFFSET(user_used_chlayout), AV_OPT_TYPE_CHLAYOUT, {.str=NULL            }, 0, 0 , PARAM, .unit = "chlayout"},
{"used_chlayout"        , "set used channel layout"     , OFFSET(user_used_chlayout), AV_OPT_TYPE_CHLAYOUT, {.str=NULL            }, 0, 0 , PARAM, .unit = "chlayout"},
{"clev"                 , "set center mix level"        , OFFSET(clev           ), AV_OPT_TYPE_FLOAT, {.dbl=C_30DB                }, -32    , 32        , PARAM},
{"center_mix_level"     , "set center mix level"        , OFFSET(clev           ), AV_OPT_TYPE_FLOAT, {.dbl=C_30DB                }, -32    , 32        , PARAM},
{"slev"                 , "set surround mix level"      , OFFSET(slev           ), AV_OPT_TYPE_FLOAT, {.dbl=C_30DB                }, -32    , 32        , PARAM},
{"surround_mix_level"   , "set surround mix Level"      , OFFSET(slev           ), AV_OPT_TYPE_FLOAT, {.dbl=C_30DB                }, -32    , 32        , PARAM},
{"lfe_mix_level"        , "set LFE mix level"           , OFFSET(lfe_mix_level  ), AV_OPT_TYPE_FLOAT, {.dbl=0                     }, -32    , 32        , PARAM},
{"rmvol"                , "set rematrix volume"         , OFFSET(rematrix_volume), AV_OPT_TYPE_FLOAT, {.dbl=1.0                   }, -1000  , 1000      , PARAM},
{"rematrix_volume"      , "set rematrix volume"         , OFFSET(rematrix_volume), AV_OPT_TYPE_FLOAT, {.dbl=1.0                   }, -1000  , 1000      , PARAM},
{"rematrix_maxval"      , "set rematrix maxval"         , OFFSET(rematrix_maxval), AV_OPT_TYPE_FLOAT, {.dbl=0.0                   }, 0      , 1000      , PARAM},

{"flags"                , "set flags"                   , OFFSET(flags          ), AV_OPT_TYPE_FLAGS, {.i64=0                     }, 0      , UINT_MAX  , PARAM, .unit = "flags"},
{"swr_flags"            , "set flags"                   , OFFSET(flags          ), AV_OPT_TYPE_FLAGS, {.i64=0                     }, 0      , UINT_MAX  , PARAM, .unit = "flags"},
{"res"                  , "force resampling"            , 0                      , AV_OPT_TYPE_CONST, {.i64=SWR_FLAG_RESAMPLE     }, INT_MIN, INT_MAX   , PARAM, .unit = "flags"},

{"dither_scale"         , "set dither scale"            , OFFSET(dither.scale   ), AV_OPT_TYPE_FLOAT, {.dbl=1                     }, 0      , INT_MAX   , PARAM},

{"dither_method"        , "set dither method"           , OFFSET(user_dither_method),AV_OPT_TYPE_INT, {.i64=0                     }, 0      , SWR_DITHER_NB-1, PARAM, .unit = "dither_method"},
{"rectangular"          , "select rectangular dither"   , 0                      , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_RECTANGULAR}, INT_MIN, INT_MAX   , PARAM, .unit = "dither_method"},
{"triangular"           , "select triangular dither"    , 0                      , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_TRIANGULAR }, INT_MIN, INT_MAX   , PARAM, .unit = "dither_method"},
{"triangular_hp"        , "select triangular dither with high pass" , 0          , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_TRIANGULAR_HIGHPASS }, INT_MIN, INT_MAX, PARAM, .unit = "dither_method"},
{"lipshitz"             , "select Lipshitz noise shaping dither" , 0             , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_NS_LIPSHITZ}, INT_MIN, INT_MAX, PARAM, .unit = "dither_method"},
{"shibata"              , "select Shibata noise shaping dither" , 0              , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_NS_SHIBATA }, INT_MIN, INT_MAX, PARAM, .unit = "dither_method"},
{"low_shibata"          , "select low Shibata noise shaping dither" , 0          , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_NS_LOW_SHIBATA }, INT_MIN, INT_MAX, PARAM, .unit = "dither_method"},
{"high_shibata"         , "select high Shibata noise shaping dither" , 0         , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_NS_HIGH_SHIBATA }, INT_MIN, INT_MAX, PARAM, .unit = "dither_method"},
{"f_weighted"           , "select f-weighted noise shaping dither" , 0           , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_NS_F_WEIGHTED }, INT_MIN, INT_MAX, PARAM, .unit = "dither_method"},
{"modified_e_weighted"  , "select modified-e-weighted noise shaping dither" , 0  , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_NS_MODIFIED_E_WEIGHTED }, INT_MIN, INT_MAX, PARAM, .unit = "dither_method"},
{"improved_e_weighted"  , "select improved-e-weighted noise shaping dither" , 0  , AV_OPT_TYPE_CONST, {.i64=SWR_DITHER_NS_IMPROVED_E_WEIGHTED }, INT_MIN, INT_MAX, PARAM, .unit = "dither_method"},

{"filter_size"          , "set swr resampling filter size", OFFSET(filter_size)  , AV_OPT_TYPE_INT  , {.i64=32                    }, 0      , INT_MAX   , PARAM },
{"phase_shift"          , "set swr resampling phase shift", OFFSET(phase_shift)  , AV_OPT_TYPE_INT  , {.i64=10                    }, 0      , 24        , PARAM },
{"linear_interp"        , "enable linear interpolation" , OFFSET(linear_interp)  , AV_OPT_TYPE_BOOL , {.i64=1                     }, 0      , 1         , PARAM },
{"exact_rational"       , "enable exact rational"       , OFFSET(exact_rational) , AV_OPT_TYPE_BOOL , {.i64=1                     }, 0      , 1         , PARAM },
{"cutoff"               , "set cutoff frequency ratio"  , OFFSET(cutoff)         , AV_OPT_TYPE_DOUBLE,{.dbl=0.                    }, 0      , 1         , PARAM },

/* duplicate option in order to work with avconv */
{"resample_cutoff"      , "set cutoff frequency ratio"  , OFFSET(cutoff)         , AV_OPT_TYPE_DOUBLE,{.dbl=0.                    }, 0      , 1         , PARAM },

{"resampler"            , "set resampling Engine"       , OFFSET(engine)         , AV_OPT_TYPE_INT  , {.i64=0                     }, 0      , SWR_ENGINE_NB-1, PARAM, .unit = "resampler"},
{"swr"                  , "select SW Resampler"         , 0                      , AV_OPT_TYPE_CONST, {.i64=SWR_ENGINE_SWR        }, INT_MIN, INT_MAX   , PARAM, .unit = "resampler"},
{"soxr"                 , "select SoX Resampler"        , 0                      , AV_OPT_TYPE_CONST, {.i64=SWR_ENGINE_SOXR       }, INT_MIN, INT_MAX   , PARAM, .unit = "resampler"},
{"precision"            , "set soxr resampling precision (in bits)"
                                                        , OFFSET(precision)      , AV_OPT_TYPE_DOUBLE,{.dbl=20.0                  }, 15.0   , 33.0      , PARAM },
{"cheby"                , "enable soxr Chebyshev passband & higher-precision irrational ratio approximation"
                                                        , OFFSET(cheby)          , AV_OPT_TYPE_BOOL , {.i64=0                     }, 0      , 1         , PARAM },
{"min_comp"             , "set minimum difference between timestamps and audio data (in seconds) below which no timestamp compensation of either kind is applied"
                                                        , OFFSET(min_compensation),AV_OPT_TYPE_FLOAT ,{.dbl=FLT_MAX               }, 0      , FLT_MAX   , PARAM },
{"min_hard_comp"        , "set minimum difference between timestamps and audio data (in seconds) to trigger padding/trimming the data."
                                                        , OFFSET(min_hard_compensation),AV_OPT_TYPE_FLOAT ,{.dbl=0.1                   }, 0      , INT_MAX   , PARAM },
{"comp_duration"        , "set duration (in seconds) over which data is stretched/squeezed to make it match the timestamps."
                                                        , OFFSET(soft_compensation_duration),AV_OPT_TYPE_FLOAT ,{.dbl=1                     }, 0      , INT_MAX   , PARAM },
{"max_soft_comp"        , "set maximum factor by which data is stretched/squeezed to make it match the timestamps."
                                                        , OFFSET(max_soft_compensation),AV_OPT_TYPE_FLOAT ,{.dbl=0                     }, INT_MIN, INT_MAX   , PARAM },
{"async"                , "simplified 1 parameter audio timestamp matching, 0(disabled), 1(filling and trimming), >1(maximum stretch/squeeze in samples per second)"
                                                        , OFFSET(async)          , AV_OPT_TYPE_FLOAT ,{.dbl=0                     }, INT_MIN, INT_MAX   , PARAM },
{"first_pts"            , "Assume the first pts should be this value (in samples)."
                                                        , OFFSET(firstpts_in_samples), AV_OPT_TYPE_INT64 ,{.i64=AV_NOPTS_VALUE    }, INT64_MIN,INT64_MAX, PARAM },

{ "matrix_encoding"     , "set matrixed stereo encoding" , OFFSET(matrix_encoding), AV_OPT_TYPE_INT   ,{.i64 = AV_MATRIX_ENCODING_NONE}, AV_MATRIX_ENCODING_NONE,     AV_MATRIX_ENCODING_NB-1, PARAM, .unit = "matrix_encoding" },
    { "none",  "select none",               0, AV_OPT_TYPE_CONST, { .i64 = AV_MATRIX_ENCODING_NONE  }, INT_MIN, INT_MAX, PARAM, .unit = "matrix_encoding" },
    { "dolby", "select Dolby",              0, AV_OPT_TYPE_CONST, { .i64 = AV_MATRIX_ENCODING_DOLBY }, INT_MIN, INT_MAX, PARAM, .unit = "matrix_encoding" },
    { "dplii", "select Dolby Pro Logic II", 0, AV_OPT_TYPE_CONST, { .i64 = AV_MATRIX_ENCODING_DPLII }, INT_MIN, INT_MAX, PARAM, .unit = "matrix_encoding" },

{ "filter_type"         , "select swr filter type"      , OFFSET(filter_type)    , AV_OPT_TYPE_INT  , { .i64 = SWR_FILTER_TYPE_KAISER }, SWR_FILTER_TYPE_CUBIC, SWR_FILTER_TYPE_KAISER, PARAM, .unit = "filter_type" },
    { "cubic"           , "select cubic"                , 0                      , AV_OPT_TYPE_CONST, { .i64 = SWR_FILTER_TYPE_CUBIC            }, INT_MIN, INT_MAX, PARAM, .unit = "filter_type" },
    { "blackman_nuttall", "select Blackman Nuttall windowed sinc", 0             , AV_OPT_TYPE_CONST, { .i64 = SWR_FILTER_TYPE_BLACKMAN_NUTTALL }, INT_MIN, INT_MAX, PARAM, .unit = "filter_type" },
    { "kaiser"          , "select Kaiser windowed sinc" , 0                      , AV_OPT_TYPE_CONST, { .i64 = SWR_FILTER_TYPE_KAISER           }, INT_MIN, INT_MAX, PARAM, .unit = "filter_type" },

{ "kaiser_beta"         , "set swr Kaiser window beta"  , OFFSET(kaiser_beta)    , AV_OPT_TYPE_DOUBLE  , {.dbl=9                     }, 2      , 16        , PARAM },

{ "output_sample_bits"  , "set swr number of output sample bits", OFFSET(dither.output_sample_bits), AV_OPT_TYPE_INT  , {.i64=0   }, 0      , 64        , PARAM },
{0}
};

static const char* context_to_name(void* ptr) {
    return "SWR";
}

static const AVClass av_class = {
    .class_name                = "SWResampler",
    .item_name                 = context_to_name,
    .option                    = options,
    .version                   = LIBAVUTIL_VERSION_INT,
    .log_level_offset_offset   = OFFSET(log_level_offset),
    .parent_log_context_offset = OFFSET(log_ctx),
    .category                  = AV_CLASS_CATEGORY_SWRESAMPLER,
};

const AVClass *swr_get_class(void)
{
    return &av_class;
}

av_cold struct SwrContext *swr_alloc(void){
    SwrContext *s= av_mallocz(sizeof(SwrContext));
    if(s){
        s->av_class= &av_class;
        av_opt_set_defaults(s);
        s->firstpts = AV_NOPTS_VALUE;
    }
    return s;
}
