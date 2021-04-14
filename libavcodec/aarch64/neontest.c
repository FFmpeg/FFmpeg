/*
 * check NEON registers for clobbers
 * Copyright (c) 2013 Martin Storsjo
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

#include "libavcodec/avcodec.h"
#include "libavutil/aarch64/neontest.h"

wrap(avcodec_open2(AVCodecContext *avctx,
                   const AVCodec *codec,
                   AVDictionary **options))
{
    testneonclobbers(avcodec_open2, avctx, codec, options);
}

wrap(avcodec_decode_subtitle2(AVCodecContext *avctx,
                              AVSubtitle *sub,
                              int *got_sub_ptr,
                              AVPacket *avpkt))
{
    testneonclobbers(avcodec_decode_subtitle2, avctx, sub,
                     got_sub_ptr, avpkt);
}

wrap(avcodec_encode_subtitle(AVCodecContext *avctx,
                             uint8_t *buf, int buf_size,
                             const AVSubtitle *sub))
{
    testneonclobbers(avcodec_encode_subtitle, avctx, buf, buf_size, sub);
}

wrap(avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt))
{
    testneonclobbers(avcodec_send_packet, avctx, avpkt);
}

wrap(avcodec_receive_packet(AVCodecContext *avctx, AVPacket *avpkt))
{
    testneonclobbers(avcodec_receive_packet, avctx, avpkt);
}

wrap(avcodec_send_frame(AVCodecContext *avctx, const AVFrame *frame))
{
    testneonclobbers(avcodec_send_frame, avctx, frame);
}

wrap(avcodec_receive_frame(AVCodecContext *avctx, AVFrame *frame))
{
    testneonclobbers(avcodec_receive_frame, avctx, frame);
}
