/*
 * Copyright (c) 2012 Clément Bœsch
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
 * RealText subtitle decoder
 * @see http://service.real.com/help/library/guides/ProductionGuide/prodguide/htmfiles/realtext.htm
 */

#include "avcodec.h"
#include "ass.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"

static int rt_event_to_ass(AVBPrint *buf, const char *p)
{
    int prev_chr_is_space = 1;

    while (*p) {
        if (*p != '<') {
            if (!isspace(*p))
                av_bprint_chars(buf, *p, 1);
            else if (!prev_chr_is_space)
                av_bprint_chars(buf, ' ', 1);
            prev_chr_is_space = isspace(*p);
        } else {
            const char *end = strchr(p, '>');
            if (!end)
                break;
            if (!av_strncasecmp(p, "<br/>", 5) ||
                !av_strncasecmp(p, "<br>",  4)) {
                av_bprintf(buf, "\\N");
            }
            p = end;
        }
        p++;
    }
    av_bprintf(buf, "\r\n");
    return 0;
}

static int realtext_decode_frame(AVCodecContext *avctx,
                                 void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle *sub = data;
    const char *ptr = avpkt->data;
    AVBPrint buf;

    av_bprint_init(&buf, 0, 4096);
    // note: no need to rescale pts & duration since they are in the same
    // timebase as ASS (1/100)
    if (ptr && avpkt->size > 0 && !rt_event_to_ass(&buf, ptr))
        ff_ass_add_rect(sub, buf.str, avpkt->pts, avpkt->duration, 0);
    *got_sub_ptr = sub->num_rects > 0;
    av_bprint_finalize(&buf, NULL);
    return avpkt->size;
}

AVCodec ff_realtext_decoder = {
    .name           = "realtext",
    .long_name      = NULL_IF_CONFIG_SMALL("RealText subtitle"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_REALTEXT,
    .decode         = realtext_decode_frame,
    .init           = ff_ass_subtitle_header_default,
};
