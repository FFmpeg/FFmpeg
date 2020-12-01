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
 * WebVTT subtitle decoder
 * @see http://dev.w3.org/html5/webvtt/
 * @todo need to support extended markups and cue settings
 */

#include "avcodec.h"
#include "ass.h"
#include "internal.h"
#include "libavutil/bprint.h"

static const struct {
    const char *from;
    const char *to;
} webvtt_tag_replace[] = {
    {"<i>", "{\\i1}"}, {"</i>", "{\\i0}"},
    {"<b>", "{\\b1}"}, {"</b>", "{\\b0}"},
    {"<u>", "{\\u1}"}, {"</u>", "{\\u0}"},
    {"{", "\\{"}, {"}", "\\}"}, // escape to avoid ASS markup conflicts
    {"&gt;", ">"}, {"&lt;", "<"},
    {"&lrm;", ""}, {"&rlm;", ""}, // FIXME: properly honor bidi marks
    {"&amp;", "&"}, {"&nbsp;", "\\h"},
};

static int webvtt_event_to_ass(AVBPrint *buf, const char *p)
{
    int i, again = 0, skip = 0;

    while (*p) {

        for (i = 0; i < FF_ARRAY_ELEMS(webvtt_tag_replace); i++) {
            const char *from = webvtt_tag_replace[i].from;
            const size_t len = strlen(from);
            if (!strncmp(p, from, len)) {
                av_bprintf(buf, "%s", webvtt_tag_replace[i].to);
                p += len;
                again = 1;
                break;
            }
        }
        if (!*p)
            break;

        if (again) {
            again = 0;
            skip = 0;
            continue;
        }
        if (*p == '<')
            skip = 1;
        else if (*p == '>')
            skip = 0;
        else if (p[0] == '\n' && p[1])
            av_bprintf(buf, "\\N");
        else if (!skip && *p != '\r')
            av_bprint_chars(buf, *p, 1);
        p++;
    }
    return 0;
}

static int webvtt_decode_frame(AVCodecContext *avctx,
                               void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    int ret = 0;
    AVSubtitle *sub = data;
    const char *ptr = avpkt->data;
    FFASSDecoderContext *s = avctx->priv_data;
    AVBPrint buf;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    if (ptr && avpkt->size > 0 && !webvtt_event_to_ass(&buf, ptr))
        ret = ff_ass_add_rect(sub, buf.str, s->readorder++, 0, NULL, NULL);
    av_bprint_finalize(&buf, NULL);
    if (ret < 0)
        return ret;
    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

const AVCodec ff_webvtt_decoder = {
    .name           = "webvtt",
    .long_name      = NULL_IF_CONFIG_SMALL("WebVTT subtitle"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_WEBVTT,
    .decode         = webvtt_decode_frame,
    .init           = ff_ass_subtitle_header_default,
    .flush          = ff_ass_decoder_flush,
    .priv_data_size = sizeof(FFASSDecoderContext),
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE,
};
