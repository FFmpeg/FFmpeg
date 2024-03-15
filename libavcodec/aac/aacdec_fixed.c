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

#include "libavcodec/aactab.h"
#include "libavcodec/sinewin_fixed_tablegen.h"
#include "libavcodec/kbdwin.h"

DECLARE_ALIGNED(32, static INTFLOAT, AAC_RENAME2(aac_kbd_long_1024))[1024];
DECLARE_ALIGNED(32, static INTFLOAT, AAC_RENAME2(aac_kbd_short_128))[128];
DECLARE_ALIGNED(32, static INTFLOAT, AAC_RENAME(aac_kbd_long_960))[960];
DECLARE_ALIGNED(32, static INTFLOAT, AAC_RENAME(aac_kbd_short_120))[120];

static void init_tables_fixed_fn(void)
{
    AAC_RENAME(ff_kbd_window_init)(AAC_RENAME2(aac_kbd_long_1024), 4.0, 1024);
    AAC_RENAME(ff_kbd_window_init)(AAC_RENAME2(aac_kbd_short_128), 6.0, 128);

    AAC_RENAME(ff_kbd_window_init)(AAC_RENAME(aac_kbd_long_960), 4.0, 960);
    AAC_RENAME(ff_kbd_window_init)(AAC_RENAME(aac_kbd_short_120), 6.0, 120);

    init_sine_windows_fixed();
}

static void init_tables_fixed(void)
{
    static AVOnce init_fixed_once = AV_ONCE_INIT;
    ff_thread_once(&init_fixed_once, init_tables_fixed_fn);
}

#include "aacdec_dsp_template.c"
