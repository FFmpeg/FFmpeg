/*
 * Copyright (C) 2011-2012 Michael Niedermayer (michaelni@gmx.at)
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
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"

#define TEMPLATE_REMATRIX_FLT
#include "rematrix_template.c"
#undef TEMPLATE_REMATRIX_FLT

#define TEMPLATE_REMATRIX_DBL
#include "rematrix_template.c"
#undef TEMPLATE_REMATRIX_DBL

#define TEMPLATE_REMATRIX_S16
#include "rematrix_template.c"
#undef TEMPLATE_REMATRIX_S16

#define TEMPLATE_REMATRIX_S32
#include "rematrix_template.c"
#undef TEMPLATE_REMATRIX_S32

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
#define NUM_NAMED_CHANNELS     18

int swr_set_matrix(struct SwrContext *s, const double *matrix, int stride)
{
    int nb_in, nb_out, in, out;

    if (!s || s->in_convert) // s needs to be allocated but not initialized
        return AVERROR(EINVAL);
    memset(s->matrix, 0, sizeof(s->matrix));
    nb_in  = av_get_channel_layout_nb_channels(s->in_ch_layout);
    nb_out = av_get_channel_layout_nb_channels(s->out_ch_layout);
    for (out = 0; out < nb_out; out++) {
        for (in = 0; in < nb_in; in++)
            s->matrix[out][in] = matrix[in];
        matrix += stride;
    }
    s->rematrix_custom = 1;
    return 0;
}

static int even(int64_t layout){
    if(!layout) return 1;
    if(layout&(layout-1)) return 1;
    return 0;
}

static int clean_layout(SwrContext *s, int64_t layout){
    if(layout && layout != AV_CH_FRONT_CENTER && !(layout&(layout-1))) {
        char buf[128];
        av_get_channel_layout_string(buf, sizeof(buf), -1, layout);
        av_log(s, AV_LOG_VERBOSE, "Treating %s as mono\n", buf);
        return AV_CH_FRONT_CENTER;
    }

    return layout;
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

av_cold static int auto_matrix(SwrContext *s)
{
    int i, j, out_i;
    double matrix[NUM_NAMED_CHANNELS][NUM_NAMED_CHANNELS]={{0}};
    int64_t unaccounted, in_ch_layout, out_ch_layout;
    double maxcoef=0;
    char buf[128];
    const int matrix_encoding = s->matrix_encoding;
    float maxval;

    in_ch_layout = clean_layout(s, s->in_ch_layout);
    out_ch_layout = clean_layout(s, s->out_ch_layout);

    if(   out_ch_layout == AV_CH_LAYOUT_STEREO_DOWNMIX
       && (in_ch_layout & AV_CH_LAYOUT_STEREO_DOWNMIX) == 0
    )
        out_ch_layout = AV_CH_LAYOUT_STEREO;

    if(    in_ch_layout == AV_CH_LAYOUT_STEREO_DOWNMIX
       && (out_ch_layout & AV_CH_LAYOUT_STEREO_DOWNMIX) == 0
    )
        in_ch_layout = AV_CH_LAYOUT_STEREO;

    if(!sane_layout(in_ch_layout)){
        av_get_channel_layout_string(buf, sizeof(buf), -1, s->in_ch_layout);
        av_log(s, AV_LOG_ERROR, "Input channel layout '%s' is not supported\n", buf);
        return AVERROR(EINVAL);
    }

    if(!sane_layout(out_ch_layout)){
        av_get_channel_layout_string(buf, sizeof(buf), -1, s->out_ch_layout);
        av_log(s, AV_LOG_ERROR, "Output channel layout '%s' is not supported\n", buf);
        return AVERROR(EINVAL);
    }

    memset(s->matrix, 0, sizeof(s->matrix));
    for(i=0; i<FF_ARRAY_ELEMS(matrix); i++){
        if(in_ch_layout & out_ch_layout & (1ULL<<i))
            matrix[i][i]= 1.0;
    }

    unaccounted= in_ch_layout & ~out_ch_layout;

//FIXME implement dolby surround
//FIXME implement full ac3


    if(unaccounted & AV_CH_FRONT_CENTER){
        if((out_ch_layout & AV_CH_LAYOUT_STEREO) == AV_CH_LAYOUT_STEREO){
            if(in_ch_layout & AV_CH_LAYOUT_STEREO) {
                matrix[ FRONT_LEFT][FRONT_CENTER]+= s->clev;
                matrix[FRONT_RIGHT][FRONT_CENTER]+= s->clev;
            } else {
                matrix[ FRONT_LEFT][FRONT_CENTER]+= M_SQRT1_2;
                matrix[FRONT_RIGHT][FRONT_CENTER]+= M_SQRT1_2;
            }
        }else
            av_assert0(0);
    }
    if(unaccounted & AV_CH_LAYOUT_STEREO){
        if(out_ch_layout & AV_CH_FRONT_CENTER){
            matrix[FRONT_CENTER][ FRONT_LEFT]+= M_SQRT1_2;
            matrix[FRONT_CENTER][FRONT_RIGHT]+= M_SQRT1_2;
            if(in_ch_layout & AV_CH_FRONT_CENTER)
                matrix[FRONT_CENTER][ FRONT_CENTER] = s->clev*sqrt(2);
        }else
            av_assert0(0);
    }

    if(unaccounted & AV_CH_BACK_CENTER){
        if(out_ch_layout & AV_CH_BACK_LEFT){
            matrix[ BACK_LEFT][BACK_CENTER]+= M_SQRT1_2;
            matrix[BACK_RIGHT][BACK_CENTER]+= M_SQRT1_2;
        }else if(out_ch_layout & AV_CH_SIDE_LEFT){
            matrix[ SIDE_LEFT][BACK_CENTER]+= M_SQRT1_2;
            matrix[SIDE_RIGHT][BACK_CENTER]+= M_SQRT1_2;
        }else if(out_ch_layout & AV_CH_FRONT_LEFT){
            if (matrix_encoding == AV_MATRIX_ENCODING_DOLBY ||
                matrix_encoding == AV_MATRIX_ENCODING_DPLII) {
                if (unaccounted & (AV_CH_BACK_LEFT | AV_CH_SIDE_LEFT)) {
                    matrix[FRONT_LEFT ][BACK_CENTER] -= s->slev * M_SQRT1_2;
                    matrix[FRONT_RIGHT][BACK_CENTER] += s->slev * M_SQRT1_2;
                } else {
                    matrix[FRONT_LEFT ][BACK_CENTER] -= s->slev;
                    matrix[FRONT_RIGHT][BACK_CENTER] += s->slev;
                }
            } else {
                matrix[ FRONT_LEFT][BACK_CENTER]+= s->slev*M_SQRT1_2;
                matrix[FRONT_RIGHT][BACK_CENTER]+= s->slev*M_SQRT1_2;
            }
        }else if(out_ch_layout & AV_CH_FRONT_CENTER){
            matrix[ FRONT_CENTER][BACK_CENTER]+= s->slev*M_SQRT1_2;
        }else
            av_assert0(0);
    }
    if(unaccounted & AV_CH_BACK_LEFT){
        if(out_ch_layout & AV_CH_BACK_CENTER){
            matrix[BACK_CENTER][ BACK_LEFT]+= M_SQRT1_2;
            matrix[BACK_CENTER][BACK_RIGHT]+= M_SQRT1_2;
        }else if(out_ch_layout & AV_CH_SIDE_LEFT){
            if(in_ch_layout & AV_CH_SIDE_LEFT){
                matrix[ SIDE_LEFT][ BACK_LEFT]+= M_SQRT1_2;
                matrix[SIDE_RIGHT][BACK_RIGHT]+= M_SQRT1_2;
            }else{
            matrix[ SIDE_LEFT][ BACK_LEFT]+= 1.0;
            matrix[SIDE_RIGHT][BACK_RIGHT]+= 1.0;
            }
        }else if(out_ch_layout & AV_CH_FRONT_LEFT){
            if (matrix_encoding == AV_MATRIX_ENCODING_DOLBY) {
                matrix[FRONT_LEFT ][BACK_LEFT ] -= s->slev * M_SQRT1_2;
                matrix[FRONT_LEFT ][BACK_RIGHT] -= s->slev * M_SQRT1_2;
                matrix[FRONT_RIGHT][BACK_LEFT ] += s->slev * M_SQRT1_2;
                matrix[FRONT_RIGHT][BACK_RIGHT] += s->slev * M_SQRT1_2;
            } else if (matrix_encoding == AV_MATRIX_ENCODING_DPLII) {
                matrix[FRONT_LEFT ][BACK_LEFT ] -= s->slev * SQRT3_2;
                matrix[FRONT_LEFT ][BACK_RIGHT] -= s->slev * M_SQRT1_2;
                matrix[FRONT_RIGHT][BACK_LEFT ] += s->slev * M_SQRT1_2;
                matrix[FRONT_RIGHT][BACK_RIGHT] += s->slev * SQRT3_2;
            } else {
                matrix[ FRONT_LEFT][ BACK_LEFT] += s->slev;
                matrix[FRONT_RIGHT][BACK_RIGHT] += s->slev;
            }
        }else if(out_ch_layout & AV_CH_FRONT_CENTER){
            matrix[ FRONT_CENTER][BACK_LEFT ]+= s->slev*M_SQRT1_2;
            matrix[ FRONT_CENTER][BACK_RIGHT]+= s->slev*M_SQRT1_2;
        }else
            av_assert0(0);
    }

    if(unaccounted & AV_CH_SIDE_LEFT){
        if(out_ch_layout & AV_CH_BACK_LEFT){
            /* if back channels do not exist in the input, just copy side
               channels to back channels, otherwise mix side into back */
            if (in_ch_layout & AV_CH_BACK_LEFT) {
                matrix[BACK_LEFT ][SIDE_LEFT ] += M_SQRT1_2;
                matrix[BACK_RIGHT][SIDE_RIGHT] += M_SQRT1_2;
            } else {
                matrix[BACK_LEFT ][SIDE_LEFT ] += 1.0;
                matrix[BACK_RIGHT][SIDE_RIGHT] += 1.0;
            }
        }else if(out_ch_layout & AV_CH_BACK_CENTER){
            matrix[BACK_CENTER][ SIDE_LEFT]+= M_SQRT1_2;
            matrix[BACK_CENTER][SIDE_RIGHT]+= M_SQRT1_2;
        }else if(out_ch_layout & AV_CH_FRONT_LEFT){
            if (matrix_encoding == AV_MATRIX_ENCODING_DOLBY) {
                matrix[FRONT_LEFT ][SIDE_LEFT ] -= s->slev * M_SQRT1_2;
                matrix[FRONT_LEFT ][SIDE_RIGHT] -= s->slev * M_SQRT1_2;
                matrix[FRONT_RIGHT][SIDE_LEFT ] += s->slev * M_SQRT1_2;
                matrix[FRONT_RIGHT][SIDE_RIGHT] += s->slev * M_SQRT1_2;
            } else if (matrix_encoding == AV_MATRIX_ENCODING_DPLII) {
                matrix[FRONT_LEFT ][SIDE_LEFT ] -= s->slev * SQRT3_2;
                matrix[FRONT_LEFT ][SIDE_RIGHT] -= s->slev * M_SQRT1_2;
                matrix[FRONT_RIGHT][SIDE_LEFT ] += s->slev * M_SQRT1_2;
                matrix[FRONT_RIGHT][SIDE_RIGHT] += s->slev * SQRT3_2;
            } else {
                matrix[ FRONT_LEFT][ SIDE_LEFT] += s->slev;
                matrix[FRONT_RIGHT][SIDE_RIGHT] += s->slev;
            }
        }else if(out_ch_layout & AV_CH_FRONT_CENTER){
            matrix[ FRONT_CENTER][SIDE_LEFT ]+= s->slev*M_SQRT1_2;
            matrix[ FRONT_CENTER][SIDE_RIGHT]+= s->slev*M_SQRT1_2;
        }else
            av_assert0(0);
    }

    if(unaccounted & AV_CH_FRONT_LEFT_OF_CENTER){
        if(out_ch_layout & AV_CH_FRONT_LEFT){
            matrix[ FRONT_LEFT][ FRONT_LEFT_OF_CENTER]+= 1.0;
            matrix[FRONT_RIGHT][FRONT_RIGHT_OF_CENTER]+= 1.0;
        }else if(out_ch_layout & AV_CH_FRONT_CENTER){
            matrix[ FRONT_CENTER][ FRONT_LEFT_OF_CENTER]+= M_SQRT1_2;
            matrix[ FRONT_CENTER][FRONT_RIGHT_OF_CENTER]+= M_SQRT1_2;
        }else
            av_assert0(0);
    }
    /* mix LFE into front left/right or center */
    if (unaccounted & AV_CH_LOW_FREQUENCY) {
        if (out_ch_layout & AV_CH_FRONT_CENTER) {
            matrix[FRONT_CENTER][LOW_FREQUENCY] += s->lfe_mix_level;
        } else if (out_ch_layout & AV_CH_FRONT_LEFT) {
            matrix[FRONT_LEFT ][LOW_FREQUENCY] += s->lfe_mix_level * M_SQRT1_2;
            matrix[FRONT_RIGHT][LOW_FREQUENCY] += s->lfe_mix_level * M_SQRT1_2;
        } else
            av_assert0(0);
    }

    for(out_i=i=0; i<64; i++){
        double sum=0;
        int in_i=0;
        if((out_ch_layout & (1ULL<<i)) == 0)
            continue;
        for(j=0; j<64; j++){
            if((in_ch_layout & (1ULL<<j)) == 0)
               continue;
            if (i < FF_ARRAY_ELEMS(matrix) && j < FF_ARRAY_ELEMS(matrix[0]))
                s->matrix[out_i][in_i]= matrix[i][j];
            else
                s->matrix[out_i][in_i]= i == j && (in_ch_layout & out_ch_layout & (1ULL<<i));
            sum += fabs(s->matrix[out_i][in_i]);
            in_i++;
        }
        maxcoef= FFMAX(maxcoef, sum);
        out_i++;
    }
    if(s->rematrix_volume  < 0)
        maxcoef = -s->rematrix_volume;

    if (s->rematrix_maxval > 0) {
        maxval = s->rematrix_maxval;
    } else if (   av_get_packed_sample_fmt(s->out_sample_fmt) < AV_SAMPLE_FMT_FLT
               || av_get_packed_sample_fmt(s->int_sample_fmt) < AV_SAMPLE_FMT_FLT) {
        maxval = 1.0;
    } else
        maxval = INT_MAX;

    if(maxcoef > maxval || s->rematrix_volume  < 0){
        maxcoef /= maxval;
        for(i=0; i<SWR_CH_MAX; i++)
            for(j=0; j<SWR_CH_MAX; j++){
                s->matrix[i][j] /= maxcoef;
            }
    }

    if(s->rematrix_volume > 0){
        for(i=0; i<SWR_CH_MAX; i++)
            for(j=0; j<SWR_CH_MAX; j++){
                s->matrix[i][j] *= s->rematrix_volume;
            }
    }

    for(i=0; i<av_get_channel_layout_nb_channels(out_ch_layout); i++){
        for(j=0; j<av_get_channel_layout_nb_channels(in_ch_layout); j++){
            av_log(NULL, AV_LOG_DEBUG, "%f ", s->matrix[i][j]);
        }
        av_log(NULL, AV_LOG_DEBUG, "\n");
    }
    return 0;
}

av_cold int swri_rematrix_init(SwrContext *s){
    int i, j;
    int nb_in  = av_get_channel_layout_nb_channels(s->in_ch_layout);
    int nb_out = av_get_channel_layout_nb_channels(s->out_ch_layout);

    s->mix_any_f = NULL;

    if (!s->rematrix_custom) {
        int r = auto_matrix(s);
        if (r)
            return r;
    }
    if (s->midbuf.fmt == AV_SAMPLE_FMT_S16P){
        s->native_matrix = av_calloc(nb_in * nb_out, sizeof(int));
        s->native_one    = av_mallocz(sizeof(int));
        for (i = 0; i < nb_out; i++)
            for (j = 0; j < nb_in; j++)
                ((int*)s->native_matrix)[i * nb_in + j] = lrintf(s->matrix[i][j] * 32768);
        *((int*)s->native_one) = 32768;
        s->mix_1_1_f = (mix_1_1_func_type*)copy_s16;
        s->mix_2_1_f = (mix_2_1_func_type*)sum2_s16;
        s->mix_any_f = (mix_any_func_type*)get_mix_any_func_s16(s);
    }else if(s->midbuf.fmt == AV_SAMPLE_FMT_FLTP){
        s->native_matrix = av_calloc(nb_in * nb_out, sizeof(float));
        s->native_one    = av_mallocz(sizeof(float));
        for (i = 0; i < nb_out; i++)
            for (j = 0; j < nb_in; j++)
                ((float*)s->native_matrix)[i * nb_in + j] = s->matrix[i][j];
        *((float*)s->native_one) = 1.0;
        s->mix_1_1_f = (mix_1_1_func_type*)copy_float;
        s->mix_2_1_f = (mix_2_1_func_type*)sum2_float;
        s->mix_any_f = (mix_any_func_type*)get_mix_any_func_float(s);
    }else if(s->midbuf.fmt == AV_SAMPLE_FMT_DBLP){
        s->native_matrix = av_calloc(nb_in * nb_out, sizeof(double));
        s->native_one    = av_mallocz(sizeof(double));
        for (i = 0; i < nb_out; i++)
            for (j = 0; j < nb_in; j++)
                ((double*)s->native_matrix)[i * nb_in + j] = s->matrix[i][j];
        *((double*)s->native_one) = 1.0;
        s->mix_1_1_f = (mix_1_1_func_type*)copy_double;
        s->mix_2_1_f = (mix_2_1_func_type*)sum2_double;
        s->mix_any_f = (mix_any_func_type*)get_mix_any_func_double(s);
    }else if(s->midbuf.fmt == AV_SAMPLE_FMT_S32P){
        // Only for dithering currently
//         s->native_matrix = av_calloc(nb_in * nb_out, sizeof(double));
        s->native_one    = av_mallocz(sizeof(int));
//         for (i = 0; i < nb_out; i++)
//             for (j = 0; j < nb_in; j++)
//                 ((double*)s->native_matrix)[i * nb_in + j] = s->matrix[i][j];
        *((int*)s->native_one) = 32768;
        s->mix_1_1_f = (mix_1_1_func_type*)copy_s32;
        s->mix_2_1_f = (mix_2_1_func_type*)sum2_s32;
        s->mix_any_f = (mix_any_func_type*)get_mix_any_func_s32(s);
    }else
        av_assert0(0);
    //FIXME quantize for integeres
    for (i = 0; i < SWR_CH_MAX; i++) {
        int ch_in=0;
        for (j = 0; j < SWR_CH_MAX; j++) {
            s->matrix32[i][j]= lrintf(s->matrix[i][j] * 32768);
            if(s->matrix[i][j])
                s->matrix_ch[i][++ch_in]= j;
        }
        s->matrix_ch[i][0]= ch_in;
    }

    if(HAVE_YASM && HAVE_MMX) swri_rematrix_init_x86(s);

    return 0;
}

av_cold void swri_rematrix_free(SwrContext *s){
    av_freep(&s->native_matrix);
    av_freep(&s->native_one);
    av_freep(&s->native_simd_matrix);
    av_freep(&s->native_simd_one);
}

int swri_rematrix(SwrContext *s, AudioData *out, AudioData *in, int len, int mustcopy){
    int out_i, in_i, i, j;
    int len1 = 0;
    int off = 0;

    if(s->mix_any_f) {
        s->mix_any_f(out->ch, (const uint8_t **)in->ch, s->native_matrix, len);
        return 0;
    }

    if(s->mix_2_1_simd || s->mix_1_1_simd){
        len1= len&~15;
        off = len1 * out->bps;
    }

    av_assert0(!s->out_ch_layout || out->ch_count == av_get_channel_layout_nb_channels(s->out_ch_layout));
    av_assert0(!s-> in_ch_layout || in ->ch_count == av_get_channel_layout_nb_channels(s-> in_ch_layout));

    for(out_i=0; out_i<out->ch_count; out_i++){
        switch(s->matrix_ch[out_i][0]){
        case 0:
            if(mustcopy)
                memset(out->ch[out_i], 0, len * av_get_bytes_per_sample(s->int_sample_fmt));
            break;
        case 1:
            in_i= s->matrix_ch[out_i][1];
            if(s->matrix[out_i][in_i]!=1.0){
                if(s->mix_1_1_simd && len1)
                    s->mix_1_1_simd(out->ch[out_i]    , in->ch[in_i]    , s->native_simd_matrix, in->ch_count*out_i + in_i, len1);
                if(len != len1)
                    s->mix_1_1_f   (out->ch[out_i]+off, in->ch[in_i]+off, s->native_matrix, in->ch_count*out_i + in_i, len-len1);
            }else if(mustcopy){
                memcpy(out->ch[out_i], in->ch[in_i], len*out->bps);
            }else{
                out->ch[out_i]= in->ch[in_i];
            }
            break;
        case 2: {
            int in_i1 = s->matrix_ch[out_i][1];
            int in_i2 = s->matrix_ch[out_i][2];
            if(s->mix_2_1_simd && len1)
                s->mix_2_1_simd(out->ch[out_i]    , in->ch[in_i1]    , in->ch[in_i2]    , s->native_simd_matrix, in->ch_count*out_i + in_i1, in->ch_count*out_i + in_i2, len1);
            else
                s->mix_2_1_f   (out->ch[out_i]    , in->ch[in_i1]    , in->ch[in_i2]    , s->native_matrix, in->ch_count*out_i + in_i1, in->ch_count*out_i + in_i2, len1);
            if(len != len1)
                s->mix_2_1_f   (out->ch[out_i]+off, in->ch[in_i1]+off, in->ch[in_i2]+off, s->native_matrix, in->ch_count*out_i + in_i1, in->ch_count*out_i + in_i2, len-len1);
            break;}
        default:
            if(s->int_sample_fmt == AV_SAMPLE_FMT_FLTP){
                for(i=0; i<len; i++){
                    float v=0;
                    for(j=0; j<s->matrix_ch[out_i][0]; j++){
                        in_i= s->matrix_ch[out_i][1+j];
                        v+= ((float*)in->ch[in_i])[i] * s->matrix[out_i][in_i];
                    }
                    ((float*)out->ch[out_i])[i]= v;
                }
            }else if(s->int_sample_fmt == AV_SAMPLE_FMT_DBLP){
                for(i=0; i<len; i++){
                    double v=0;
                    for(j=0; j<s->matrix_ch[out_i][0]; j++){
                        in_i= s->matrix_ch[out_i][1+j];
                        v+= ((double*)in->ch[in_i])[i] * s->matrix[out_i][in_i];
                    }
                    ((double*)out->ch[out_i])[i]= v;
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
