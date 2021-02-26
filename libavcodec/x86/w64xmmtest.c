/*
 * check XMM registers for clobbers on Win64
 * Copyright (c) 2012 Ronald S. Bultje <rsbultje@gmail.com>
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
#include "libavutil/x86/w64xmmtest.h"

wrap(avcodec_open2(AVCodecContext *avctx,
                   const AVCodec *codec,
                   AVDictionary **options))
{
    testxmmclobbers(avcodec_open2, avctx, codec, options);
}

#if FF_API_OLD_ENCDEC
wrap(avcodec_decode_audio4(AVCodecContext *avctx,
                           AVFrame *frame,
                           int *got_frame_ptr,
                           AVPacket *avpkt))
{
    testxmmclobbers(avcodec_decode_audio4, avctx, frame,
                    got_frame_ptr, avpkt);
}

wrap(avcodec_decode_video2(AVCodecContext *avctx,
                           AVFrame *picture,
                           int *got_picture_ptr,
                           AVPacket *avpkt))
{
    testxmmclobbers(avcodec_decode_video2, avctx, picture,
                    got_picture_ptr, avpkt);
}

wrap(avcodec_encode_audio2(AVCodecContext *avctx,
                           AVPacket *avpkt,
                           const AVFrame *frame,
                           int *got_packet_ptr))
{
    testxmmclobbers(avcodec_encode_audio2, avctx, avpkt, frame,
                    got_packet_ptr);
}

wrap(avcodec_encode_video2(AVCodecContext *avctx, AVPacket *avpkt,
                           const AVFrame *frame, int *got_packet_ptr))
{
    testxmmclobbers(avcodec_encode_video2, avctx, avpkt, frame, got_packet_ptr);
}
#endif

wrap(avcodec_decode_subtitle2(AVCodecContext *avctx,
                              AVSubtitle *sub,
                              int *got_sub_ptr,
                              AVPacket *avpkt))
{
    testxmmclobbers(avcodec_decode_subtitle2, avctx, sub,
                    got_sub_ptr, avpkt);
}

wrap(avcodec_encode_subtitle(AVCodecContext *avctx,
                             uint8_t *buf, int buf_size,
                             const AVSubtitle *sub))
{
    testxmmclobbers(avcodec_encode_subtitle, avctx, buf, buf_size, sub);
}

wrap(avcodec_send_packet(AVCodecContext *avctx, const AVPacket *avpkt))
{
    testxmmclobbers(avcodec_send_packet, avctx, avpkt);
}

wrap(avcodec_receive_packet(AVCodecContext *avctx, AVPacket *avpkt))
{
    testxmmclobbers(avcodec_receive_packet, avctx, avpkt);
}

wrap(avcodec_send_frame(AVCodecContext *avctx, const AVFrame *frame))
{
    testxmmclobbers(avcodec_send_frame, avctx, frame);
}

wrap(avcodec_receive_frame(AVCodecContext *avctx, AVFrame *frame))
{
    testxmmclobbers(avcodec_receive_frame, avctx, frame);
}
