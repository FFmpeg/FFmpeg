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

typedef void (RENAME(mix_any_func_type))(SAMPLE **out, const SAMPLE **in1, COEFF *coeffp, int len);

static void RENAME(sum2)(SAMPLE *out, const SAMPLE *in1, const SAMPLE *in2, COEFF *coeffp, int index1, int index2, int len){
    int i;
    COEFF coeff1 = coeffp[index1];
    COEFF coeff2 = coeffp[index2];

    for(i=0; i<len; i++)
        out[i] = R(coeff1*in1[i] + coeff2*in2[i]);
}

static void RENAME(copy)(SAMPLE *out, const SAMPLE *in, COEFF *coeffp, int index, int len){
    int i;
    COEFF coeff = coeffp[index];
    for(i=0; i<len; i++)
        out[i] = R(coeff*in[i]);
}

static void RENAME(mix6to2)(SAMPLE **out, const SAMPLE **in, COEFF *coeffp, int len){
    int i;

    for(i=0; i<len; i++) {
        INTER t = in[2][i]*coeffp[0*6+2] + in[3][i]*coeffp[0*6+3];
        out[0][i] = R(t + in[0][i]*coeffp[0*6+0] + in[4][i]*coeffp[0*6+4]);
        out[1][i] = R(t + in[1][i]*coeffp[1*6+1] + in[5][i]*coeffp[1*6+5]);
    }
}

static void RENAME(mix8to2)(SAMPLE **out, const SAMPLE **in, COEFF *coeffp, int len){
    int i;

    for(i=0; i<len; i++) {
        INTER t = in[2][i]*coeffp[0*8+2] + in[3][i]*coeffp[0*8+3];
        out[0][i] = R(t + in[0][i]*coeffp[0*8+0] + in[4][i]*coeffp[0*8+4] + in[6][i]*coeffp[0*8+6]);
        out[1][i] = R(t + in[1][i]*coeffp[1*8+1] + in[5][i]*coeffp[1*8+5] + in[7][i]*coeffp[1*8+7]);
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
