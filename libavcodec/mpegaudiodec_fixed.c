/*
 * Fixed-point MPEG audio decoder
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
#include "config_components.h"
#include "libavutil/samplefmt.h"

#define USE_FLOATS 0

#include "codec_internal.h"
#include "mpegaudio.h"

#define SHR(a,b)       (((int)(a))>>(b))
/* WARNING: only correct for positive numbers */
#define FIXR_OLD(a)    ((int)((a) * FRAC_ONE + 0.5))
#define FIXR(a)        ((int)((a) * FRAC_ONE + 0.5))
#define FIXHR(a)       ((int)((a) * (1LL<<32) + 0.5))
#define MULH3(x, y, s) MULH((s)*(x), y)
#define MULLx(x, y, s) MULL((int)(x),(y),s)
#define RENAME(a)      a ## _fixed
#define OUT_FMT   AV_SAMPLE_FMT_S16
#define OUT_FMT_P AV_SAMPLE_FMT_S16P

/* Intensity stereo table. See commit b91d46614df189e7905538e7f5c4ed9c7ed0d274
 * (float based mp1/mp2/mp3 decoders.) for how they were created. */
static const int32_t is_table[2][16] = {
    { 0x000000, 0x1B0CB1, 0x2ED9EC, 0x400000, 0x512614, 0x64F34F, 0x800000 },
    { 0x800000, 0x64F34F, 0x512614, 0x400000, 0x2ED9EC, 0x1B0CB1, 0x000000 }
};

/* Antialiasing table. See commit ce4a29c066cddfc180979ed86396812f24337985
 * (optimize antialias) for how they were created. */
static const int32_t csa_table[8][4] = {
    { 0x36E129F8, 0xDF128056, 0x15F3AA4E, 0xA831565E },
    { 0x386E75F2, 0xE1CF24A5, 0x1A3D9A97, 0xA960AEB3 },
    { 0x3CC6B73A, 0xEBF19FA6, 0x28B856E0, 0xAF2AE86C },
    { 0x3EEEA054, 0xF45B88BC, 0x334A2910, 0xB56CE868 },
    { 0x3FB6905C, 0xF9F27F18, 0x39A90F74, 0xBA3BEEBC },
    { 0x3FF23F20, 0xFD60D1E4, 0x3D531104, 0xBD6E92C4 },
    { 0x3FFE5932, 0xFF175EE4, 0x3F15B816, 0xBF1905B2 },
    { 0x3FFFE34A, 0xFFC3612F, 0x3FC34479, 0xBFC37DE5 }
};

#include "mpegaudiodec_template.c"

#if CONFIG_MP1_DECODER
const FFCodec ff_mp1_decoder = {
    .p.name         = "mp1",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MP1 (MPEG audio layer 1)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_MP1,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .flush          = flush,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_MP2_DECODER
const FFCodec ff_mp2_decoder = {
    .p.name         = "mp2",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MP2 (MPEG audio layer 2)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_MP2,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .flush          = flush,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_MP3_DECODER
const FFCodec ff_mp3_decoder = {
    .p.name         = "mp3",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MP3 (MPEG audio layer 3)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_MP3,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .flush          = flush,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_MP3ADU_DECODER
const FFCodec ff_mp3adu_decoder = {
    .p.name         = "mp3adu",
    .p.long_name    = NULL_IF_CONFIG_SMALL("ADU (Application Data Unit) MP3 (MPEG audio layer 3)"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_MP3ADU,
    .priv_data_size = sizeof(MPADecodeContext),
    .init           = decode_init,
    FF_CODEC_DECODE_CB(decode_frame_adu),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .flush          = flush,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_S16,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
#endif
#if CONFIG_MP3ON4_DECODER
const FFCodec ff_mp3on4_decoder = {
    .p.name         = "mp3on4",
    .p.long_name    = NULL_IF_CONFIG_SMALL("MP3onMP4"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_MP3ON4,
    .priv_data_size = sizeof(MP3On4DecodeContext),
    .init           = decode_init_mp3on4,
    .close          = decode_close_mp3on4,
    FF_CODEC_DECODE_CB(decode_frame_mp3on4),
    .p.capabilities = AV_CODEC_CAP_CHANNEL_CONF |
                      AV_CODEC_CAP_DR1,
    .flush          = flush_mp3on4,
    .p.sample_fmts  = (const enum AVSampleFormat[]) { AV_SAMPLE_FMT_S16P,
                                                      AV_SAMPLE_FMT_NONE },
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
#endif
