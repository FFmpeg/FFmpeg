/*
 * Copyright (c) 2011 Mans Rullgard
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

#include "config.h"
#include "libavutil/attributes.h"
#include "libavutil/thread.h"
#include "mpegaudio.h"
#include "mpegaudiodsp.h"
#include "dct.h"
#include "dct32.h"

static AVOnce mpadsp_table_init = AV_ONCE_INIT;

static av_cold void mpadsp_init_tabs(void)
{
    int i, j;
    /* compute mdct windows */
    for (i = 0; i < 36; i++) {
        for (j = 0; j < 4; j++) {
            double d;

            if (j == 2 && i % 3 != 1)
                continue;

            d = sin(M_PI * (i + 0.5) / 36.0);
            if (j == 1) {
                if      (i >= 30) d = 0;
                else if (i >= 24) d = sin(M_PI * (i - 18 + 0.5) / 12.0);
                else if (i >= 18) d = 1;
            } else if (j == 3) {
                if      (i <   6) d = 0;
                else if (i <  12) d = sin(M_PI * (i -  6 + 0.5) / 12.0);
                else if (i <  18) d = 1;
            }
            //merge last stage of imdct into the window coefficients
            d *= 0.5 * IMDCT_SCALAR / cos(M_PI * (2 * i + 19) / 72);

            if (j == 2) {
                ff_mdct_win_float[j][i/3] = d / (1 << 5);
                ff_mdct_win_fixed[j][i/3] = d / (1 << 5) * (1LL << 32) + 0.5;
            } else {
                int idx = i < 18 ? i : i + (MDCT_BUF_SIZE/2 - 18);
                ff_mdct_win_float[j][idx] = d / (1 << 5);
                ff_mdct_win_fixed[j][idx] = d / (1 << 5) * (1LL << 32) + 0.5;
            }
        }
    }

    /* NOTE: we do frequency inversion after the MDCT by changing
        the sign of the right window coefs */
    for (j = 0; j < 4; j++) {
        for (i = 0; i < MDCT_BUF_SIZE; i += 2) {
            ff_mdct_win_float[j + 4][i    ] =  ff_mdct_win_float[j][i    ];
            ff_mdct_win_float[j + 4][i + 1] = -ff_mdct_win_float[j][i + 1];
            ff_mdct_win_fixed[j + 4][i    ] =  ff_mdct_win_fixed[j][i    ];
            ff_mdct_win_fixed[j + 4][i + 1] = -ff_mdct_win_fixed[j][i + 1];
        }
    }

#if ARCH_X86
    ff_mpadsp_init_x86_tabs();
#endif
}

av_cold void ff_mpadsp_init(MPADSPContext *s)
{
    DCTContext dct;

    ff_dct_init(&dct, 5, DCT_II);
    ff_thread_once(&mpadsp_table_init, &mpadsp_init_tabs);

    s->apply_window_float = ff_mpadsp_apply_window_float;
    s->apply_window_fixed = ff_mpadsp_apply_window_fixed;

    s->dct32_float = dct.dct32;
    s->dct32_fixed = ff_dct32_fixed;

    s->imdct36_blocks_float = ff_imdct36_blocks_float;
    s->imdct36_blocks_fixed = ff_imdct36_blocks_fixed;

#if ARCH_AARCH64
    ff_mpadsp_init_aarch64(s);
#elif ARCH_ARM
    ff_mpadsp_init_arm(s);
#elif ARCH_PPC
    ff_mpadsp_init_ppc(s);
#elif ARCH_X86
    ff_mpadsp_init_x86(s);
#endif
#if HAVE_MIPSFPU
    ff_mpadsp_init_mipsfpu(s);
#endif
#if HAVE_MIPSDSP
    ff_mpadsp_init_mipsdsp(s);
#endif
}
