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
 * SubViewer subtitle decoder
 * @see https://en.wikipedia.org/wiki/SubViewer
 */

#include "avcodec.h"
#include "ass.h"
#include "libavutil/bprint.h"

static int subviewer_event_to_ass(AVBPrint *buf, const char *p)
{
    while (*p) {
        char c;

        if (sscanf(p, "%*u:%*u:%*u.%*u,%*u:%*u:%*u.%*u%c", &c) == 1)
            p += strcspn(p, "\n") + 1;
        if (!strncmp(p, "[br]", 4)) {
            av_bprintf(buf, "\\N");
            p += 4;
        } else {
            if (p[0] == '\n' && p[1])
                av_bprintf(buf, "\\N");
            else if (*p != '\r')
                av_bprint_chars(buf, *p, 1);
            p++;
        }
    }

    av_bprintf(buf, "\r\n");
    return 0;
}

static int subviewer_decode_frame(AVCodecContext *avctx,
                                  void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle *sub = data;
    const char *ptr = avpkt->data;
    AVBPrint buf;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    // note: no need to rescale pts & duration since they are in the same
    // timebase as ASS (1/100)
    if (ptr && avpkt->size > 0 && !subviewer_event_to_ass(&buf, ptr))
        ff_ass_add_rect(sub, buf.str, avpkt->pts, avpkt->duration, 0);
    *got_sub_ptr = sub->num_rects > 0;
    av_bprint_finalize(&buf, NULL);
    return avpkt->size;
}

AVCodec ff_subviewer_decoder = {
    .name           = "subviewer",
    .long_name      = NULL_IF_CONFIG_SMALL("SubViewer subtitle"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_SUBVIEWER,
    .decode         = subviewer_decode_frame,
    .init           = ff_ass_subtitle_header_default,
};
