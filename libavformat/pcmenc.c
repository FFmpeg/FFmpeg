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

#include "avformat.h"
#include "rawenc.h"

#define PCMDEF(name_, long_name_, ext, codec)               \
AVOutputFormat ff_pcm_ ## name_ ## _muxer = {               \
    .name         = #name_,                                 \
    .long_name    = NULL_IF_CONFIG_SMALL(long_name_),       \
    .extensions   = ext,                                    \
    .audio_codec  = codec,                                  \
    .video_codec  = AV_CODEC_ID_NONE,                          \
    .write_packet = ff_raw_write_packet,                    \
    .flags        = AVFMT_NOTIMESTAMPS,                     \
};

PCMDEF(f64be, "PCM 64-bit floating-point big-endian",
       NULL, AV_CODEC_ID_PCM_F64BE)

PCMDEF(f64le, "PCM 64-bit floating-point little-endian",
       NULL, AV_CODEC_ID_PCM_F64LE)

PCMDEF(f32be, "PCM 32-bit floating-point big-endian",
       NULL, AV_CODEC_ID_PCM_F32BE)

PCMDEF(f32le, "PCM 32-bit floating-point little-endian",
       NULL, AV_CODEC_ID_PCM_F32LE)

PCMDEF(s32be, "PCM signed 32-bit big-endian",
       NULL, AV_CODEC_ID_PCM_S32BE)

PCMDEF(s32le, "PCM signed 32-bit little-endian",
       NULL, AV_CODEC_ID_PCM_S32LE)

PCMDEF(s24be, "PCM signed 24-bit big-endian",
       NULL, AV_CODEC_ID_PCM_S24BE)

PCMDEF(s24le, "PCM signed 24-bit little-endian",
       NULL, AV_CODEC_ID_PCM_S24LE)

PCMDEF(s16be, "PCM signed 16-bit big-endian",
       AV_NE("sw", NULL), AV_CODEC_ID_PCM_S16BE)

PCMDEF(s16le, "PCM signed 16-bit little-endian",
       AV_NE(NULL, "sw"), AV_CODEC_ID_PCM_S16LE)

PCMDEF(s8, "PCM signed 8-bit",
       "sb", AV_CODEC_ID_PCM_S8)

PCMDEF(u32be, "PCM unsigned 32-bit big-endian",
       NULL, AV_CODEC_ID_PCM_U32BE)

PCMDEF(u32le, "PCM unsigned 32-bit little-endian",
       NULL, AV_CODEC_ID_PCM_U32LE)

PCMDEF(u24be, "PCM unsigned 24-bit big-endian",
       NULL, AV_CODEC_ID_PCM_U24BE)

PCMDEF(u24le, "PCM unsigned 24-bit little-endian",
       NULL, AV_CODEC_ID_PCM_U24LE)

PCMDEF(u16be, "PCM unsigned 16-bit big-endian",
       AV_NE("uw", NULL), AV_CODEC_ID_PCM_U16BE)

PCMDEF(u16le, "PCM unsigned 16-bit little-endian",
       AV_NE(NULL, "uw"), AV_CODEC_ID_PCM_U16LE)

PCMDEF(u8, "PCM unsigned 8-bit",
       "ub", AV_CODEC_ID_PCM_U8)

PCMDEF(alaw, "PCM A-law",
       "al", AV_CODEC_ID_PCM_ALAW)

PCMDEF(mulaw, "PCM mu-law",
       "ul", AV_CODEC_ID_PCM_MULAW)

PCMDEF(vidc, "PCM Archimedes VIDC",
       NULL, AV_CODEC_ID_PCM_VIDC)
