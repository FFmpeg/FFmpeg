/*
 * copyright (c) 2008 Michael Niedermayer <michaelni@gmx.at>
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

#include "fft.h"
#include "synth_filter.h"

static void synth_filter_float(FFTContext *imdct,
                           float *synth_buf_ptr, int *synth_buf_offset,
                           float synth_buf2[32], const float window[512],
                           float out[32], const float in[32], float scale)
{
    float *synth_buf= synth_buf_ptr + *synth_buf_offset;
    int i, j;

    imdct->imdct_half(imdct, synth_buf, in);

    for (i = 0; i < 16; i++){
        float a= synth_buf2[i     ];
        float b= synth_buf2[i + 16];
        float c= 0;
        float d= 0;
        for (j = 0; j < 512 - *synth_buf_offset; j += 64){
            a += window[i + j     ]*(-synth_buf[15 - i + j      ]);
            b += window[i + j + 16]*( synth_buf[     i + j      ]);
            c += window[i + j + 32]*( synth_buf[16 + i + j      ]);
            d += window[i + j + 48]*( synth_buf[31 - i + j      ]);
        }
        for (     ; j < 512; j += 64){
            a += window[i + j     ]*(-synth_buf[15 - i + j - 512]);
            b += window[i + j + 16]*( synth_buf[     i + j - 512]);
            c += window[i + j + 32]*( synth_buf[16 + i + j - 512]);
            d += window[i + j + 48]*( synth_buf[31 - i + j - 512]);
        }
        out[i     ] = a*scale;
        out[i + 16] = b*scale;
        synth_buf2[i     ] = c;
        synth_buf2[i + 16] = d;
    }
    *synth_buf_offset= (*synth_buf_offset - 32)&511;
}

av_cold void ff_synth_filter_init(SynthFilterContext *c)
{
    c->synth_filter_float = synth_filter_float;

    if (ARCH_AARCH64)
        ff_synth_filter_init_aarch64(c);
    if (ARCH_ARM)
        ff_synth_filter_init_arm(c);
    if (ARCH_X86)
        ff_synth_filter_init_x86(c);
}
