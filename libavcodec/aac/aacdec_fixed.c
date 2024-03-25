/*
 * AAC decoder
 * Copyright (c) 2005-2006 Oded Shimon ( ods15 ods15 dyndns org )
 * Copyright (c) 2006-2007 Maxim Gavrilov ( maxim.gavrilov gmail com )
 * Copyright (c) 2008-2013 Alex Converse <alex.converse@gmail.com>
 *
 * AAC LATM decoder
 * Copyright (c) 2008-2010 Paul Kendall <paul@kcbbs.gen.nz>
 * Copyright (c) 2010      Janne Grunau <janne-libav@jannau.net>
 *
 * AAC decoder fixed-point implementation
 * Copyright (c) 2013
 *      MIPS Technologies, Inc., California.
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

#define USE_FIXED 1

#include "libavutil/thread.h"

#include "libavcodec/aac_defines.h"

#include "libavcodec/avcodec.h"
#include "aacdec.h"
#include "libavcodec/aactab.h"
#include "libavcodec/sinewin_fixed_tablegen.h"
#include "libavcodec/kbdwin.h"
#include "libavcodec/cbrt_data.h"
#include "libavcodec/aacsbr.h"

DECLARE_ALIGNED(32, static int, aac_kbd_long_1024_fixed)[1024];
DECLARE_ALIGNED(32, static int, aac_kbd_short_128_fixed)[128];
DECLARE_ALIGNED(32, static int, aac_kbd_long_960_fixed)[960];
DECLARE_ALIGNED(32, static int, aac_kbd_short_120_fixed)[120];
DECLARE_ALIGNED(32, static int, aac_kbd_long_768_fixed)[768];
DECLARE_ALIGNED(32, static int, aac_kbd_short_96_fixed)[96];

static void init_tables_fixed_fn(void)
{
    ff_cbrt_tableinit_fixed();

    ff_kbd_window_init_fixed(aac_kbd_long_1024_fixed, 4.0, 1024);
    ff_kbd_window_init_fixed(aac_kbd_short_128_fixed, 6.0, 128);

    ff_kbd_window_init_fixed(aac_kbd_long_960_fixed, 4.0, 960);
    ff_kbd_window_init_fixed(aac_kbd_short_120_fixed, 6.0, 120);

    ff_aac_sbr_init_fixed();

    init_sine_windows_fixed();
}

static const int cce_scale_fixed[8] = {
    Q30(1.0),          //2^(0/8)
    Q30(1.0905077327), //2^(1/8)
    Q30(1.1892071150), //2^(2/8)
    Q30(1.2968395547), //2^(3/8)
    Q30(1.4142135624), //2^(4/8)
    Q30(1.5422108254), //2^(5/8)
    Q30(1.6817928305), //2^(6/8)
    Q30(1.8340080864), //2^(7/8)
};

/** Dequantization-related */
#include "aacdec_fixed_dequant.h"

#include "aacdec_fixed_coupling.h"
#include "aacdec_fixed_prediction.h"
#include "aacdec_dsp_template.c"
#include "aacdec_proc_template.c"

av_cold int ff_aac_decode_init_fixed(AVCodecContext *avctx)
{
    static AVOnce init_fixed_once = AV_ONCE_INIT;
    AACDecContext *ac = avctx->priv_data;

    ac->is_fixed = 1;
    avctx->sample_fmt = AV_SAMPLE_FMT_S32P;

    aac_dsp_init_fixed(&ac->dsp);
    aac_proc_init_fixed(&ac->proc);

    ac->fdsp = avpriv_alloc_fixed_dsp(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!ac->fdsp)
        return AVERROR(ENOMEM);

    ff_thread_once(&init_fixed_once, init_tables_fixed_fn);

    return ff_aac_decode_init(avctx);
}
