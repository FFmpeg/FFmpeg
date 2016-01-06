/*
 * SubRip subtitle decoder
 * Copyright (c) 2010  Aurelien Jacobs <aurel@gnuage.org>
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

#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/parseutils.h"
#include "avcodec.h"
#include "ass.h"
#include "htmlsubtitles.h"

static void srt_to_ass(AVCodecContext *avctx, AVBPrint *dst,
                       const char *in, int x1, int y1, int x2, int y2)
{
    if (x1 >= 0 && y1 >= 0) {
        /* XXX: here we rescale coordinate assuming they are in DVD resolution
         * (720x480) since we don't have anything better */

        if (x2 >= 0 && y2 >= 0 && (x2 != x1 || y2 != y1) && x2 >= x1 && y2 >= y1) {
            /* text rectangle defined, write the text at the center of the rectangle */
            const int cx = x1 + (x2 - x1)/2;
            const int cy = y1 + (y2 - y1)/2;
            const int scaled_x = cx * ASS_DEFAULT_PLAYRESX / 720;
            const int scaled_y = cy * ASS_DEFAULT_PLAYRESY / 480;
            av_bprintf(dst, "{\\an5}{\\pos(%d,%d)}", scaled_x, scaled_y);
        } else {
            /* only the top left corner, assume the text starts in that corner */
            const int scaled_x = x1 * ASS_DEFAULT_PLAYRESX / 720;
            const int scaled_y = y1 * ASS_DEFAULT_PLAYRESY / 480;
            av_bprintf(dst, "{\\an1}{\\pos(%d,%d)}", scaled_x, scaled_y);
        }
    }

    ff_htmlmarkup_to_ass(avctx, dst, in);
}

static int srt_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle *sub = data;
    AVBPrint buffer;
    int x1 = -1, y1 = -1, x2 = -1, y2 = -1;
    int size, ret;
    const uint8_t *p = av_packet_get_side_data(avpkt, AV_PKT_DATA_SUBTITLE_POSITION, &size);
    FFASSDecoderContext *s = avctx->priv_data;

    if (p && size == 16) {
        x1 = AV_RL32(p     );
        y1 = AV_RL32(p +  4);
        x2 = AV_RL32(p +  8);
        y2 = AV_RL32(p + 12);
    }

    if (avpkt->size <= 0)
        return avpkt->size;

    av_bprint_init(&buffer, 0, AV_BPRINT_SIZE_UNLIMITED);

    srt_to_ass(avctx, &buffer, avpkt->data, x1, y1, x2, y2);
    ret = ff_ass_add_rect(sub, buffer.str, s->readorder++, 0, NULL, NULL);
    av_bprint_finalize(&buffer, NULL);
    if (ret < 0)
        return ret;

    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

#if CONFIG_SRT_DECODER
/* deprecated decoder */
AVCodec ff_srt_decoder = {
    .name         = "srt",
    .long_name    = NULL_IF_CONFIG_SMALL("SubRip subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_SUBRIP,
    .init         = ff_ass_subtitle_header_default,
    .decode       = srt_decode_frame,
    .flush        = ff_ass_decoder_flush,
    .priv_data_size = sizeof(FFASSDecoderContext),
};
#endif

#if CONFIG_SUBRIP_DECODER
AVCodec ff_subrip_decoder = {
    .name         = "subrip",
    .long_name    = NULL_IF_CONFIG_SMALL("SubRip subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_SUBRIP,
    .init         = ff_ass_subtitle_header_default,
    .decode       = srt_decode_frame,
    .flush        = ff_ass_decoder_flush,
    .priv_data_size = sizeof(FFASSDecoderContext),
};
#endif
