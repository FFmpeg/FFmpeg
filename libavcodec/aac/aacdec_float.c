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

#define USE_FIXED 0

#include "libavutil/thread.h"

#include "libavcodec/aac_defines.h"

#include "libavcodec/avcodec.h"
#include "aacdec.h"
#include "libavcodec/aactab.h"
#include "libavcodec/sinewin.h"
#include "libavcodec/kbdwin.h"
#include "libavcodec/cbrt_data.h"
#include "libavutil/mathematics.h"
#include "libavcodec/aacsbr.h"

DECLARE_ALIGNED(32, static float, sine_96)[96];
DECLARE_ALIGNED(32, static float, sine_120)[120];
DECLARE_ALIGNED(32, static float, sine_768)[768];
DECLARE_ALIGNED(32, static float, sine_960)[960];
DECLARE_ALIGNED(32, static float, aac_kbd_long_960)[960];
DECLARE_ALIGNED(32, static float, aac_kbd_short_120)[120];
DECLARE_ALIGNED(32, static float, aac_kbd_long_768)[768];
DECLARE_ALIGNED(32, static float, aac_kbd_short_96)[96];

static void init_tables_float_fn(void)
{
    ff_cbrt_tableinit();

    ff_kbd_window_init(ff_aac_kbd_long_1024, 4.0, 1024);
    ff_kbd_window_init(ff_aac_kbd_short_128, 6.0, 128);

    ff_kbd_window_init(aac_kbd_long_960, 4.0, 960);
    ff_kbd_window_init(aac_kbd_short_120, 6.0, 120);

    ff_sine_window_init(sine_960, 960);
    ff_sine_window_init(sine_120, 120);
    ff_init_ff_sine_windows(9);

    ff_aac_sbr_init();

    ff_aac_float_common_init();
}

static const float cce_scale[] = {
    1.09050773266525765921, //2^(1/8)
    1.18920711500272106672, //2^(1/4)
    M_SQRT2,
    2,
};

/** Dequantization-related **/
#include "aacdec_tab.h"
#include "libavutil/intfloat.h"

#include "config.h"
#if ARCH_ARM
#include "libavcodec/arm/aac.h"
#endif

#ifndef VMUL2
static inline float *VMUL2(float *dst, const float *v, unsigned idx,
                           const float *scale)
{
    float s = *scale;
    *dst++ = v[idx    & 15] * s;
    *dst++ = v[idx>>4 & 15] * s;
    return dst;
}
#endif

#ifndef VMUL4
static inline float *VMUL4(float *dst, const float *v, unsigned idx,
                           const float *scale)
{
    float s = *scale;
    *dst++ = v[idx    & 3] * s;
    *dst++ = v[idx>>2 & 3] * s;
    *dst++ = v[idx>>4 & 3] * s;
    *dst++ = v[idx>>6 & 3] * s;
    return dst;
}
#endif

#ifndef VMUL2S
static inline float *VMUL2S(float *dst, const float *v, unsigned idx,
                            unsigned sign, const float *scale)
{
    union av_intfloat32 s0, s1;

    s0.f = s1.f = *scale;
    s0.i ^= sign >> 1 << 31;
    s1.i ^= sign      << 31;

    *dst++ = v[idx    & 15] * s0.f;
    *dst++ = v[idx>>4 & 15] * s1.f;

    return dst;
}
#endif

#ifndef VMUL4S
static inline float *VMUL4S(float *dst, const float *v, unsigned idx,
                            unsigned sign, const float *scale)
{
    unsigned nz = idx >> 12;
    union av_intfloat32 s = { .f = *scale };
    union av_intfloat32 t;

    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx    & 3] * t.f;

    sign <<= nz & 1; nz >>= 1;
    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx>>2 & 3] * t.f;

    sign <<= nz & 1; nz >>= 1;
    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx>>4 & 3] * t.f;

    sign <<= nz & 1;
    t.i = s.i ^ (sign & 1U<<31);
    *dst++ = v[idx>>6 & 3] * t.f;

    return dst;
}
#endif

#include "aacdec_float_coupling.h"
#include "aacdec_float_prediction.h"
#include "aacdec_dsp_template.c"
#include "aacdec_proc_template.c"

av_cold int ff_aac_decode_init_float(AVCodecContext *avctx)
{
    static AVOnce init_float_once = AV_ONCE_INIT;
    AACDecContext *ac = avctx->priv_data;

    ac->is_fixed = 0;
    avctx->sample_fmt = AV_SAMPLE_FMT_FLTP;

    aac_dsp_init(&ac->dsp);
    aac_proc_init(&ac->proc);

    ac->fdsp = avpriv_float_dsp_alloc(avctx->flags & AV_CODEC_FLAG_BITEXACT);
    if (!ac->fdsp)
        return AVERROR(ENOMEM);

    ff_thread_once(&init_float_once, init_tables_float_fn);

    return ff_aac_decode_init(avctx);
}
