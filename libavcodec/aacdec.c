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

/**
 * @file
 * AAC decoder
 * @author Oded Shimon  ( ods15 ods15 dyndns org )
 * @author Maxim Gavrilov ( maxim.gavrilov gmail com )
 */

#define USE_FIXED 1 // aacsbr.h breaks without this

#include "libavutil/float_dsp.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "get_bits.h"
#include "kbdwin.h"
#include "sinewin.h"

#include "aac.h"
#include "aacdec.h"
#include "aactab.h"
#include "aac/aacdec_tab.h"
#include "adts_header.h"
#include "cbrt_data.h"
#include "aacsbr.h"
#include "mpeg4audio.h"
#include "profiles.h"
#include "libavutil/intfloat.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#if ARCH_ARM
#   include "arm/aac.h"
#elif ARCH_MIPS
#   include "mips/aacdec_mips.h"
#endif

#include "aacdec_template.c"

#include "libavcodec/aac/aacdec_latm.h"

const FFCodec ff_aac_decoder = {
    .p.name          = "aac",
    CODEC_LONG_NAME("AAC (Advanced Audio Coding)"),
    .p.type          = AVMEDIA_TYPE_AUDIO,
    .p.id            = AV_CODEC_ID_AAC,
    .p.priv_class    = &ff_aac_decoder_class,
    .priv_data_size  = sizeof(AACDecContext),
    .init            = aac_decode_init,
    .close           = ff_aac_decode_close,
    FF_CODEC_DECODE_CB(aac_decode_frame),
    .p.sample_fmts   = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_NONE
    },
    .p.capabilities  = AV_CODEC_CAP_CHANNEL_CONF | AV_CODEC_CAP_DR1,
    .caps_internal   = FF_CODEC_CAP_INIT_CLEANUP,
    .p.ch_layouts    = ff_aac_ch_layout,
    .flush = flush,
    .p.profiles      = NULL_IF_CONFIG_SMALL(ff_aac_profiles),
};

const FFCodec ff_aac_fixed_decoder = {
    .p.name          = "aac_fixed",
    CODEC_LONG_NAME("AAC (Advanced Audio Coding)"),
    .p.type          = AVMEDIA_TYPE_AUDIO,
    .p.id            = AV_CODEC_ID_AAC,
    .p.priv_class    = &ff_aac_decoder_class,
    .priv_data_size  = sizeof(AACDecContext),
    .init            = aac_decode_init_fixed,
    .close           = ff_aac_decode_close,
    FF_CODEC_DECODE_CB(aac_decode_frame),
    .p.sample_fmts   = (const enum AVSampleFormat[]) {
        AV_SAMPLE_FMT_S32P, AV_SAMPLE_FMT_NONE
    },
    .p.capabilities  = AV_CODEC_CAP_CHANNEL_CONF | AV_CODEC_CAP_DR1,
    .caps_internal   = FF_CODEC_CAP_INIT_CLEANUP,
    .p.ch_layouts    = ff_aac_ch_layout,
    .p.profiles      = NULL_IF_CONFIG_SMALL(ff_aac_profiles),
    .flush = flush,
};
