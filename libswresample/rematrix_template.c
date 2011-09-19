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


static void RENAME(sum2)(SAMPLE *out, const SAMPLE *in1, const SAMPLE *in2, float coeff1, float coeff2, int len){
    int i;

    for(i=0; i<len; i++)
        out[i] = coeff1*in1[i] + coeff2*in2[i]; //FIXME better int16
}

static void RENAME(copy)(SAMPLE *out, const SAMPLE *in, float coeff, int len){
    if(coeff == 1.0){
        memcpy(out, in, sizeof(SAMPLE)*len);
    }else{
        int i;
        for(i=0; i<len; i++)
            out[i] = coeff*in[i]; //FIXME better int16
    }
}

