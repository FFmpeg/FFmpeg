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
 * MPL2 subtitles decoder
 *
 * @see http://web.archive.org/web/20090328040233/http://napisy.ussbrowarek.org/mpl2-eng.html
 */

#include "avcodec.h"
#include "ass.h"
#include "codec_internal.h"
#include "libavutil/bprint.h"

static int mpl2_event_to_ass(AVBPrint *buf, const char *p)
{
    if (*p == ' ')
        p++;

    while (*p) {
        int got_style = 0;

        while (*p && strchr("/\\_", *p)) {
            if      (*p == '/')  av_bprintf(buf, "{\\i1}");
            else if (*p == '\\') av_bprintf(buf, "{\\b1}");
            else if (*p == '_')  av_bprintf(buf, "{\\u1}");
            got_style = 1;
            p++;
        }

        while (*p && *p != '|') {
            if (*p != '\r' && *p != '\n')
                av_bprint_chars(buf, *p, 1);
            p++;
        }

        if (*p == '|') {
            if (got_style)
                av_bprintf(buf, "{\\r}");
            av_bprintf(buf, "\\N");
            p++;
        }
    }

    return 0;
}

static int mpl2_decode_frame(AVCodecContext *avctx, AVSubtitle *sub,
                             int *got_sub_ptr, const AVPacket *avpkt)
{
    int ret = 0;
    AVBPrint buf;
    const char *ptr = avpkt->data;
    FFASSDecoderContext *s = avctx->priv_data;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    if (ptr && avpkt->size > 0 && *ptr && !mpl2_event_to_ass(&buf, ptr))
        ret = ff_ass_add_rect(sub, buf.str, s->readorder++, 0, NULL, NULL);
    av_bprint_finalize(&buf, NULL);
    if (ret < 0)
        return ret;
    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

const FFCodec ff_mpl2_decoder = {
    .p.name         = "mpl2",
    CODEC_LONG_NAME("MPL2 subtitle"),
    .p.type         = AVMEDIA_TYPE_SUBTITLE,
    .p.id           = AV_CODEC_ID_MPL2,
    FF_CODEC_DECODE_SUB_CB(mpl2_decode_frame),
    .init           = ff_ass_subtitle_header_default,
    .flush          = ff_ass_decoder_flush,
    .priv_data_size = sizeof(FFASSDecoderContext),
};
