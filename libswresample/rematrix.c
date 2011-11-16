/*
 * Copyright (C) 2011 Michael Niedermayer (michaelni@gmx.at)
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

#include "swresample_internal.h"
#include "libavutil/audioconvert.h"
#include "libavutil/avassert.h"

#define ONE (1.0)
#define R(x) x
#define SAMPLE float
#define COEFF float
#define RENAME(x) x ## _float
#include "rematrix_template.c"
#undef SAMPLE
#undef RENAME
#undef R
#undef ONE
#undef COEFF

#define ONE (-32768)
#define R(x) (((x) + 16384)>>15)
#define SAMPLE int16_t
#define COEFF int
#define RENAME(x) x ## _s16
#include "rematrix_template.c"


#define FRONT_LEFT             0
#define FRONT_RIGHT            1
#define FRONT_CENTER           2
#define LOW_FREQUENCY          3
#define BACK_LEFT              4
#define BACK_RIGHT             5
#define FRONT_LEFT_OF_CENTER   6
#define FRONT_RIGHT_OF_CENTER  7
#define BACK_CENTER            8
#define SIDE_LEFT              9
#define SIDE_RIGHT             10
#define TOP_CENTER             11
#define TOP_FRONT_LEFT         12
#define TOP_FRONT_CENTER       13
#define TOP_FRONT_RIGHT        14
#define TOP_BACK_LEFT          15
#define TOP_BACK_CENTER        16
#define TOP_BACK_RIGHT         17

static int even(int64_t layout){
    if(!layout) return 1;
    if(layout&(layout-1)) return 1;
    return 0;
}

static int sane_layout(int64_t layout){
    if(!(layout & AV_CH_LAYOUT_SURROUND)) // at least 1 front speaker
        return 0;
    if(!even(layout & (AV_CH_FRONT_LEFT | AV_CH_FRONT_RIGHT))) // no asymetric front
        return 0;
    if(!even(layout & (AV_CH_SIDE_LEFT | AV_CH_SIDE_RIGHT)))   // no asymetric side
        return 0;
    if(!even(layout & (AV_CH_BACK_LEFT | AV_CH_BACK_RIGHT)))
        return 0;
    if(!even(layout & (AV_CH_FRONT_LEFT_OF_CENTER | AV_CH_FRONT_RIGHT_OF_CENTER)))
        return 0;
    if(av_get_channel_layout_nb_channels(layout) >= SWR_CH_MAX)
        return 0;

    return 1;
}

int swri_rematrix_init(SwrContext *s){
    int i, j, out_i;
    double matrix[64][64]={{0}};
    int64_t unaccounted= s->in_ch_layout & ~s->out_ch_layout;
    double maxcoef=0;

    for(i=0; i<64; i++){
        if(s->in_ch_layout & s->out_ch_layout & (1LL<<i))
            matrix[i][i]= 1.0;
    }

    if(!sane_layout(s->in_ch_layout)){
        av_log(s, AV_LOG_ERROR, "Input channel layout isnt supported\n");
        return AVERROR(EINVAL);
    }
    if(!sane_layout(s->out_ch_layout)){
        av_log(s, AV_LOG_ERROR, "Output channel layout isnt supported\n");
        return AVERROR(EINVAL);
    }

//FIXME implement dolby surround
//FIXME implement full ac3


    if(unaccounted & AV_CH_FRONT_CENTER){
        if((s->out_ch_layout & AV_CH_LAYOUT_STEREO) == AV_CH_LAYOUT_STEREO){
            matrix[ FRONT_LEFT][FRONT_CENTER]+= M_SQRT1_2;
            matrix[FRONT_RIGHT][FRONT_CENTER]+= M_SQRT1_2;
        }else
            av_assert0(0);
    }
    if(unaccounted & AV_CH_LAYOUT_STEREO){
        if(s->out_ch_layout & AV_CH_FRONT_CENTER){
            matrix[FRONT_CENTER][ FRONT_LEFT]+= M_SQRT1_2;
            matrix[FRONT_CENTER][FRONT_RIGHT]+= M_SQRT1_2;
            if(s->in_ch_layout & AV_CH_FRONT_CENTER)
                matrix[FRONT_CENTER][ FRONT_CENTER] = s->clev*sqrt(2);
        }else
            av_assert0(0);
    }

    if(unaccounted & AV_CH_BACK_CENTER){
        if(s->out_ch_layout & AV_CH_BACK_LEFT){
            matrix[ BACK_LEFT][BACK_CENTER]+= M_SQRT1_2;
            matrix[BACK_RIGHT][BACK_CENTER]+= M_SQRT1_2;
        }else if(s->out_ch_layout & AV_CH_SIDE_LEFT){
            matrix[ SIDE_LEFT][BACK_CENTER]+= M_SQRT1_2;
            matrix[SIDE_RIGHT][BACK_CENTER]+= M_SQRT1_2;
        }else if(s->out_ch_layout & AV_CH_FRONT_LEFT){
            matrix[ FRONT_LEFT][BACK_CENTER]+= s->slev*M_SQRT1_2;
            matrix[FRONT_RIGHT][BACK_CENTER]+= s->slev*M_SQRT1_2;
        }else if(s->out_ch_layout & AV_CH_FRONT_CENTER){
            matrix[ FRONT_CENTER][BACK_CENTER]+= s->slev*M_SQRT1_2;
        }else
            av_assert0(0);
    }
    if(unaccounted & AV_CH_BACK_LEFT){
        if(s->out_ch_layout & AV_CH_BACK_CENTER){
            matrix[BACK_CENTER][ BACK_LEFT]+= M_SQRT1_2;
            matrix[BACK_CENTER][BACK_RIGHT]+= M_SQRT1_2;
        }else if(s->out_ch_layout & AV_CH_SIDE_LEFT){
            if(s->in_ch_layout & AV_CH_SIDE_LEFT){
                matrix[ SIDE_LEFT][ BACK_LEFT]+= M_SQRT1_2;
                matrix[SIDE_RIGHT][BACK_RIGHT]+= M_SQRT1_2;
            }else{
            matrix[ SIDE_LEFT][ BACK_LEFT]+= 1.0;
            matrix[SIDE_RIGHT][BACK_RIGHT]+= 1.0;
            }
        }else if(s->out_ch_layout & AV_CH_FRONT_LEFT){
            matrix[ FRONT_LEFT][ BACK_LEFT]+= s->slev;
            matrix[FRONT_RIGHT][BACK_RIGHT]+= s->slev;
        }else if(s->out_ch_layout & AV_CH_FRONT_CENTER){
            matrix[ FRONT_CENTER][BACK_LEFT ]+= s->slev*M_SQRT1_2;
            matrix[ FRONT_CENTER][BACK_RIGHT]+= s->slev*M_SQRT1_2;
        }else
            av_assert0(0);
    }

    if(unaccounted & AV_CH_SIDE_LEFT){
        if(s->out_ch_layout & AV_CH_BACK_LEFT){
            matrix[ BACK_LEFT][ SIDE_LEFT]+= 1.0;
            matrix[BACK_RIGHT][SIDE_RIGHT]+= 1.0;
        }else if(s->out_ch_layout & AV_CH_BACK_CENTER){
            matrix[BACK_CENTER][ SIDE_LEFT]+= M_SQRT1_2;
            matrix[BACK_CENTER][SIDE_RIGHT]+= M_SQRT1_2;
        }else if(s->out_ch_layout & AV_CH_FRONT_LEFT){
            matrix[ FRONT_LEFT][ SIDE_LEFT]+= s->slev;
            matrix[FRONT_RIGHT][SIDE_RIGHT]+= s->slev;
        }else if(s->out_ch_layout & AV_CH_FRONT_CENTER){
            matrix[ FRONT_CENTER][SIDE_LEFT ]+= s->slev*M_SQRT1_2;
            matrix[ FRONT_CENTER][SIDE_RIGHT]+= s->slev*M_SQRT1_2;
        }else
            av_assert0(0);
    }

    if(unaccounted & AV_CH_FRONT_LEFT_OF_CENTER){
        if(s->out_ch_layout & AV_CH_FRONT_LEFT){
            matrix[ FRONT_LEFT][ FRONT_LEFT_OF_CENTER]+= 1.0;
            matrix[FRONT_RIGHT][FRONT_RIGHT_OF_CENTER]+= 1.0;
        }else if(s->out_ch_layout & AV_CH_FRONT_CENTER){
            matrix[ FRONT_CENTER][ FRONT_LEFT_OF_CENTER]+= M_SQRT1_2;
            matrix[ FRONT_CENTER][FRONT_RIGHT_OF_CENTER]+= M_SQRT1_2;
        }else
            av_assert0(0);
    }

    //FIXME quantize for integeres
    for(out_i=i=0; i<64; i++){
        double sum=0;
        int in_i=0;
        int ch_in=0;
        for(j=0; j<64; j++){
            s->matrix[out_i][in_i]= matrix[i][j];
            s->matrix32[out_i][in_i]= lrintf(matrix[i][j] * 32768);
            if(matrix[i][j]){
                s->matrix_ch[out_i][++ch_in]= in_i;
                sum += fabs(matrix[i][j]);
            }
            if(s->in_ch_layout & (1ULL<<j))
                in_i++;
        }
        s->matrix_ch[out_i][0]= ch_in;
        maxcoef= FFMAX(maxcoef, sum);
        if(s->out_ch_layout & (1ULL<<i))
            out_i++;
    }
    if(s->rematrix_volume  < 0)
        maxcoef = -s->rematrix_volume;

    if((   s->out_sample_fmt < AV_SAMPLE_FMT_FLT
        || s->int_sample_fmt < AV_SAMPLE_FMT_FLT) && maxcoef > 1.0){
        for(i=0; i<SWR_CH_MAX; i++)
            for(j=0; j<SWR_CH_MAX; j++){
                s->matrix[i][j] /= maxcoef;
                s->matrix32[i][j]= lrintf(s->matrix[i][j] * 32768);
            }
    }

    if(s->rematrix_volume > 0){
        for(i=0; i<SWR_CH_MAX; i++)
            for(j=0; j<SWR_CH_MAX; j++){
                s->matrix[i][j] *= s->rematrix_volume;
                s->matrix32[i][j]= lrintf(s->matrix[i][j] * 32768);
            }
    }

    for(i=0; i<av_get_channel_layout_nb_channels(s->out_ch_layout); i++){
        for(j=0; j<av_get_channel_layout_nb_channels(s->in_ch_layout); j++){
            av_log(NULL, AV_LOG_DEBUG, "%f ", s->matrix[i][j]);
        }
        av_log(NULL, AV_LOG_DEBUG, "\n");
    }
    return 0;
}

int swri_rematrix(SwrContext *s, AudioData *out, AudioData *in, int len, int mustcopy){
    int out_i, in_i, i, j;

    av_assert0(out->ch_count == av_get_channel_layout_nb_channels(s->out_ch_layout));
    av_assert0(in ->ch_count == av_get_channel_layout_nb_channels(s-> in_ch_layout));

    for(out_i=0; out_i<out->ch_count; out_i++){
        switch(s->matrix_ch[out_i][0]){
        case 1:
            in_i= s->matrix_ch[out_i][1];
            if(mustcopy || s->matrix[out_i][in_i]!=1.0){
                if(s->int_sample_fmt == AV_SAMPLE_FMT_FLT){
                    copy_float((float  *)out->ch[out_i], (const float  *)in->ch[in_i], s->matrix  [out_i][in_i], len);
                }else
                    copy_s16  ((int16_t*)out->ch[out_i], (const int16_t*)in->ch[in_i], s->matrix32[out_i][in_i], len);
            }else{
                out->ch[out_i]= in->ch[in_i];
            }
            break;
        case 2:
            if(s->int_sample_fmt == AV_SAMPLE_FMT_FLT){
                sum2_float((float  *)out->ch[out_i], (const float  *)in->ch[ s->matrix_ch[out_i][1] ],           (const float  *)in->ch[ s->matrix_ch[out_i][2] ],
                                 s->matrix[out_i][ s->matrix_ch[out_i][1] ], s->matrix[out_i][ s->matrix_ch[out_i][2] ],
                           len);
            }else{
                sum2_s16  ((int16_t*)out->ch[out_i], (const int16_t*)in->ch[ s->matrix_ch[out_i][1] ],           (const int16_t*)in->ch[ s->matrix_ch[out_i][2] ],
                                 s->matrix32[out_i][ s->matrix_ch[out_i][1] ], s->matrix32[out_i][ s->matrix_ch[out_i][2] ],
                           len);
            }
            break;
        default:
            if(s->int_sample_fmt == AV_SAMPLE_FMT_FLT){
                for(i=0; i<len; i++){
                    float v=0;
                    for(j=0; j<s->matrix_ch[out_i][0]; j++){
                        in_i= s->matrix_ch[out_i][1+j];
                        v+= ((float*)in->ch[in_i])[i] * s->matrix[out_i][in_i];
                    }
                    ((float*)out->ch[out_i])[i]= v;
                }
            }else{
                for(i=0; i<len; i++){
                    int v=0;
                    for(j=0; j<s->matrix_ch[out_i][0]; j++){
                        in_i= s->matrix_ch[out_i][1+j];
                        v+= ((int16_t*)in->ch[in_i])[i] * s->matrix32[out_i][in_i];
                    }
                    ((int16_t*)out->ch[out_i])[i]= (v + 16384)>>15;
                }
            }
        }
    }
    return 0;
}
