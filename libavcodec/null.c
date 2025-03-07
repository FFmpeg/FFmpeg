/*
 * Null codecs
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

#include "codec_internal.h"
#include "decode.h"
#include "encode.h"

#if CONFIG_VNULL_DECODER || CONFIG_ANULL_DECODER
static int null_decode(AVCodecContext *avctx, AVFrame *frame,
                       int *got_frame, AVPacket *avpkt)
{
    *got_frame = 0;
    return avpkt->size;
}

#if CONFIG_VNULL_DECODER
const FFCodec ff_vnull_decoder = {
    .p.name         = "vnull",
    CODEC_LONG_NAME("null video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VNULL,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_DECODE_CB(null_decode),
};
#endif

#if CONFIG_ANULL_DECODER
const FFCodec ff_anull_decoder = {
    .p.name         = "anull",
    CODEC_LONG_NAME("null audio"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_ANULL,
    .p.capabilities = AV_CODEC_CAP_DR1,
    FF_CODEC_DECODE_CB(null_decode),
};
#endif

#endif

#if CONFIG_VNULL_ENCODER || CONFIG_ANULL_ENCODER
static int null_encode(AVCodecContext *avctx, AVPacket *pkt,
                       const AVFrame *frame, int *got_packet)
{
    *got_packet = 0;
    return 0;
}

#if CONFIG_VNULL_ENCODER
const FFCodec ff_vnull_encoder = {
    .p.name         = "vnull",
    CODEC_LONG_NAME("null video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_VNULL,
    FF_CODEC_ENCODE_CB(null_encode),
};
#endif

#if CONFIG_ANULL_ENCODER
const FFCodec ff_anull_encoder = {
    .p.name         = "anull",
    CODEC_LONG_NAME("null audio"),
    .p.type         = AVMEDIA_TYPE_AUDIO,
    .p.id           = AV_CODEC_ID_ANULL,
    .p.capabilities = AV_CODEC_CAP_VARIABLE_FRAME_SIZE,
    CODEC_SAMPLEFMTS(AV_SAMPLE_FMT_U8,  AV_SAMPLE_FMT_U8P,
                     AV_SAMPLE_FMT_S16, AV_SAMPLE_FMT_S16P,
                     AV_SAMPLE_FMT_S32, AV_SAMPLE_FMT_S32P,
                     AV_SAMPLE_FMT_S64, AV_SAMPLE_FMT_S64P,
                     AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP,
                     AV_SAMPLE_FMT_DBL, AV_SAMPLE_FMT_DBLP),
    FF_CODEC_ENCODE_CB(null_encode),
};
#endif

#endif
