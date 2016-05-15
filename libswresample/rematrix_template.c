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

#if defined(TEMPLATE_REMATRIX_FLT)
#    define R(x) x
#    define SAMPLE float
#    define COEFF float
#    define INTER float
#    define RENAME(x) x ## _float
#elif defined(TEMPLATE_REMATRIX_DBL)
#    define R(x) x
#    define SAMPLE double
#    define COEFF double
#    define INTER double
#    define RENAME(x) x ## _double
#elif defined(TEMPLATE_REMATRIX_S16)
#    define SAMPLE int16_t
#    define COEFF int
#    define INTER int
#  ifdef TEMPLATE_CLIP
#    define R(x) av_clip_int16(((x) + 16384)>>15)
#    define RENAME(x) x ## _clip_s16
#  else
#    define R(x) (((x) + 16384)>>15)
#    define RENAME(x) x ## _s16
#  endif
#elif defined(TEMPLATE_REMATRIX_S32)
#    define R(x) (((x) + 16384)>>15)
#    define SAMPLE int32_t
#    define COEFF int
#    define INTER int64_t
#    define RENAME(x) x ## _s32
#endif

typedef void (RENAME(mix_any_func_type))(SAMPLE **out, const SAMPLE **in1, COEFF *coeffp, integer len);

static void RENAME(sum2)(SAMPLE *out, const SAMPLE *in1, const SAMPLE *in2, COEFF *coeffp, integer index1, integer index2, integer len){
    int i;
    INTER coeff1 = coeffp[index1];
    INTER coeff2 = coeffp[index2];

    for(i=0; i<len; i++)
        out[i] = R(coeff1*in1[i] + coeff2*in2[i]);
}

static void RENAME(copy)(SAMPLE *out, const SAMPLE *in, COEFF *coeffp, integer index, integer len){
    int i;
    INTER coeff = coeffp[index];
    for(i=0; i<len; i++)
        out[i] = R(coeff*in[i]);
}

static void RENAME(mix6to2)(SAMPLE **out, const SAMPLE **in, COEFF *coeffp, integer len){
    int i;

    for(i=0; i<len; i++) {
        INTER t = in[2][i]*(INTER)coeffp[0*6+2] + in[3][i]*(INTER)coeffp[0*6+3];
        out[0][i] = R(t + in[0][i]*(INTER)coeffp[0*6+0] + in[4][i]*(INTER)coeffp[0*6+4]);
        out[1][i] = R(t + in[1][i]*(INTER)coeffp[1*6+1] + in[5][i]*(INTER)coeffp[1*6+5]);
    }
}

static void RENAME(mix8to2)(SAMPLE **out, const SAMPLE **in, COEFF *coeffp, integer len){
    int i;

    for(i=0; i<len; i++) {
        INTER t = in[2][i]*(INTER)coeffp[0*8+2] + in[3][i]*(INTER)coeffp[0*8+3];
        out[0][i] = R(t + in[0][i]*(INTER)coeffp[0*8+0] + in[4][i]*(INTER)coeffp[0*8+4] + in[6][i]*(INTER)coeffp[0*8+6]);
        out[1][i] = R(t + in[1][i]*(INTER)coeffp[1*8+1] + in[5][i]*(INTER)coeffp[1*8+5] + in[7][i]*(INTER)coeffp[1*8+7]);
    }
}

static RENAME(mix_any_func_type) *RENAME(get_mix_any_func)(SwrContext *s){
    if(   s->out_ch_layout == AV_CH_LAYOUT_STEREO && (s->in_ch_layout == AV_CH_LAYOUT_5POINT1 || s->in_ch_layout == AV_CH_LAYOUT_5POINT1_BACK)
       && s->matrix[0][2] == s->matrix[1][2] && s->matrix[0][3] == s->matrix[1][3]
       && !s->matrix[0][1] && !s->matrix[0][5] && !s->matrix[1][0] && !s->matrix[1][4]
    )
        return RENAME(mix6to2);

    if(   s->out_ch_layout == AV_CH_LAYOUT_STEREO && s->in_ch_layout == AV_CH_LAYOUT_7POINT1
       && s->matrix[0][2] == s->matrix[1][2] && s->matrix[0][3] == s->matrix[1][3]
       && !s->matrix[0][1] && !s->matrix[0][5] && !s->matrix[1][0] && !s->matrix[1][4]
       && !s->matrix[0][7] && !s->matrix[1][6]
    )
        return RENAME(mix8to2);

    return NULL;
}

#undef R
#undef SAMPLE
#undef COEFF
#undef INTER
#undef RENAME
