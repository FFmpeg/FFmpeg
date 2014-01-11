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
#include "libavutil/arm/neontest.h"

wrap(avcodec_open2(AVCodecContext *avctx,
                   AVCodec *codec,
                   AVDictionary **options))
{
    testneonclobbers(avcodec_open2, avctx, codec, options);
}

wrap(avcodec_decode_audio4(AVCodecContext *avctx,
                           AVFrame *frame,
                           int *got_frame_ptr,
                           AVPacket *avpkt))
{
    testneonclobbers(avcodec_decode_audio4, avctx, frame,
                     got_frame_ptr, avpkt);
}

wrap(avcodec_decode_video2(AVCodecContext *avctx,
                           AVFrame *picture,
                           int *got_picture_ptr,
                           AVPacket *avpkt))
{
    testneonclobbers(avcodec_decode_video2, avctx, picture,
                     got_picture_ptr, avpkt);
}

wrap(avcodec_decode_subtitle2(AVCodecContext *avctx,
                              AVSubtitle *sub,
                              int *got_sub_ptr,
                              AVPacket *avpkt))
{
    testneonclobbers(avcodec_decode_subtitle2, avctx, sub,
                     got_sub_ptr, avpkt);
}

wrap(avcodec_encode_audio2(AVCodecContext *avctx,
                           AVPacket *avpkt,
                           const AVFrame *frame,
                           int *got_packet_ptr))
{
    testneonclobbers(avcodec_encode_audio2, avctx, avpkt, frame,
                     got_packet_ptr);
}

wrap(avcodec_encode_subtitle(AVCodecContext *avctx,
                             uint8_t *buf, int buf_size,
                             const AVSubtitle *sub))
{
    testneonclobbers(avcodec_encode_subtitle, avctx, buf, buf_size, sub);
}

wrap(avcodec_encode_video2(AVCodecContext *avctx, AVPacket *avpkt,
                           const AVFrame *frame, int *got_packet_ptr))
{
    testneonclobbers(avcodec_encode_video2, avctx, avpkt, frame, got_packet_ptr);
}
