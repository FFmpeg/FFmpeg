/*
 * Float MPEG Audio decoder
 * Copyright (c) 2010 Michael Niedermayer
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
#include "libavutil/samplefmt.h"

#define USE_FLOATS 1

#include "mpegaudio.h"

#define SHR(a,b)       ((a)*(1.0f/(1<<(b))))
#define FIXR_OLD(a)    ((int)((a) * FRAC_ONE + 0.5))
#define FIXR(x)        ((float)(x))
#define FIXHR(x)       ((float)(x))
#define MULH3(x, y, s) ((s)*(y)*(x))
#define MULLx(x, y, s) ((y)*(x))
#define RENAME(a) a ## _float
#define OUT_FMT   AV_SAMPLE_FMT_FLT
#define OUT_FMT_P AV_SAMPLE_FMT_FLTP

/* Intensity stereo table. See commit b91d46614df189e7905538e7f5c4ed9c7ed0d274
 * (float based mp1/mp2/mp3 decoders.) for how they were created. */
static const float is_table[2][16] = {
    { 0.000000000000000000e+00, 2.113248705863952637e-01, 3.660253882408142090e-01,
      5.000000000000000000e-01, 6.339746117591857910e-01, 7.886751294136047363e-01,
      1.000000000000000000e+00 },
    { 1.000000000000000000e+00, 7.886751294136047363e-01, 6.339746117591857910e-01,
      5.000000000000000000e-01, 3.660253882408142090e-01, 2.113248705863952637e-01,
      0.000000000000000000e+00 }
};

/* Antialiasing table. See commit 6f1ec38ce2193d3d4cacd87edb452c6d7ba751ec
 * (mpegaudio: clean up compute_antialias() definition) for how they were
 * created. */
static const float csa_table[8][4] = {
    { 8.574929237365722656e-01, -5.144957900047302246e-01,
      3.429971337318420410e-01, -1.371988654136657715e+00 },
    { 8.817420005798339844e-01, -4.717319905757904053e-01,
      4.100100100040435791e-01, -1.353474020957946777e+00 },
    { 9.496286511421203613e-01, -3.133774697780609131e-01,
      6.362511515617370605e-01, -1.263006091117858887e+00 },
    { 9.833145737648010254e-01, -1.819131970405578613e-01,
      8.014013767242431641e-01, -1.165227770805358887e+00 },
    { 9.955177903175354004e-01, -9.457419067621231079e-02,
      9.009436368942260742e-01, -1.090092062950134277e+00 },
    { 9.991605877876281738e-01, -4.096558317542076111e-02,
      9.581949710845947266e-01, -1.040126085281372070e+00 },
    { 9.998992085456848145e-01, -1.419856864959001541e-02,
      9.857006072998046875e-01, -1.014097809791564941e+00 },
    { 9.999931454658508301e-01, -3.699974622577428818e-03,
      9.962931871414184570e-01, -1.003693103790283203e+00 }
};

#include "mpegaudiodec_template.c"

#if CONFIG_MP1FLOAT_DECODER
const AVCodec ff_mp1float_decoder = {
    .name           = "mp1float",
    .long_name      = NULL_IF_CONFIG_SMALL("MP1 (MPEG audio layer 1)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP1,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_FLT,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_MP2FLOAT_DECODER
const AVCodec ff_mp2float_decoder = {
    .name           = "mp2float",
    .long_name      = NULL_IF_CONFIG_SMALL("MP2 (MPEG audio layer 2)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP2,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_FLT,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_MP3FLOAT_DECODER
const AVCodec ff_mp3float_decoder = {
    .name           = "mp3float",
    .long_name      = NULL_IF_CONFIG_SMALL("MP3 (MPEG audio layer 3)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP3,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_FLT,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_MP3ADUFLOAT_DECODER
const AVCodec ff_mp3adufloat_decoder = {
    .name           = "mp3adufloat",
    .long_name      = NULL_IF_CONFIG_SMALL("ADU (Application Data Unit) MP3 (MPEG audio layer 3)"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP3ADU,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    .decode         = decode_frame_adu,
    .capabilities   = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .flush          = flush,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_FLT,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_MP3ON4FLOAT_DECODER
const AVCodec ff_mp3on4float_decoder = {
    .name           = "mp3on4float",
    .long_name      = NULL_IF_CONFIG_SMALL("MP3onMP4"),
    .type           = AVMEDIA_TYPE_AUDIO,
    .id             = AV_CODEC_ID_MP3ON4,
    .priv_data_size = sizeof(MP3On4DecodeContext),
    .init           = decode_init_mp3on4,
    .close          = decode_close_mp3on4,
    .decode         = decode_frame_mp3on4,
    .capabilities   = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .flush          = flush_mp3on4,
    .sample_fmts    = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_FLTP,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
