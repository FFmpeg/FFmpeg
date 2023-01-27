/*
 * RAW PCM muxers
 * Copyright (c) 2002 Fabrice Bellard
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

#include "config_components.h"

#include "avformat.h"
#include "mux.h"
#include "rawenc.h"

#define PCMDEF_0(name_, long_name_, ext, codec)
#define PCMDEF_1(name_, long_name_, ext, codec)             \
const FFOutputFormat ff_pcm_ ## name_ ## _muxer = {         \
    .p.name        = #name_,                                \
    .p.long_name   = NULL_IF_CONFIG_SMALL(long_name_),      \
    .p.extensions  = ext,                                   \
    .p.audio_codec = codec,                                 \
    .p.video_codec = AV_CODEC_ID_NONE,                      \
    .p.flags       = AVFMT_NOTIMESTAMPS,                    \
    .write_packet = ff_raw_write_packet,                    \
};
#define PCMDEF_2(name, long_name, ext, codec, enabled)      \
    PCMDEF_ ## enabled(name, long_name, ext, codec)
#define PCMDEF_3(name, long_name, ext, codec, config)       \
    PCMDEF_2(name, long_name, ext, codec, config)
#define PCMDEF(name, long_name, ext, uppercase)             \
    PCMDEF_3(name, long_name, ext, AV_CODEC_ID_PCM_ ## uppercase, \
             CONFIG_PCM_ ## uppercase ## _MUXER)

PCMDEF(f64be, "PCM 64-bit floating-point big-endian",           NULL, F64BE)
PCMDEF(f64le, "PCM 64-bit floating-point little-endian",        NULL, F64LE)
PCMDEF(f32be, "PCM 32-bit floating-point big-endian",           NULL, F32BE)
PCMDEF(f32le, "PCM 32-bit floating-point little-endian",        NULL, F32LE)
PCMDEF(s32be, "PCM signed 32-bit big-endian",                   NULL, S32BE)
PCMDEF(s32le, "PCM signed 32-bit little-endian",                NULL, S32LE)
PCMDEF(s24be, "PCM signed 24-bit big-endian",                   NULL, S24BE)
PCMDEF(s24le, "PCM signed 24-bit little-endian",                NULL, S24LE)
PCMDEF(s16be, "PCM signed 16-bit big-endian",      AV_NE("sw", NULL), S16BE)
PCMDEF(s16le, "PCM signed 16-bit little-endian",   AV_NE(NULL, "sw"), S16LE)
PCMDEF(s8,    "PCM signed 8-bit",                               "sb",    S8)
PCMDEF(u32be, "PCM unsigned 32-bit big-endian",                 NULL, U32BE)
PCMDEF(u32le, "PCM unsigned 32-bit little-endian",              NULL, U32LE)
PCMDEF(u24be, "PCM unsigned 24-bit big-endian",                 NULL, U24BE)
PCMDEF(u24le, "PCM unsigned 24-bit little-endian",              NULL, U24LE)
PCMDEF(u16be, "PCM unsigned 16-bit big-endian",    AV_NE("uw", NULL), U16BE)
PCMDEF(u16le, "PCM unsigned 16-bit little-endian", AV_NE(NULL, "uw"), U16LE)
PCMDEF(u8,    "PCM unsigned 8-bit",                             "ub",    U8)
PCMDEF(alaw,  "PCM A-law",                                      "al",  ALAW)
PCMDEF(mulaw, "PCM mu-law",                                     "ul", MULAW)
PCMDEF(vidc,  "PCM Archimedes VIDC",                            NULL,  VIDC)
