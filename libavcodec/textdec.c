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
 * Raw subtitles decoder
 */

#include "avcodec.h"
#include "ass.h"
#include "libavutil/bprint.h"
#include "libavutil/opt.h"

typedef struct {
    AVClass *class;
    char *linebreaks;
    int keep_ass_markup;
} TextContext;

#define OFFSET(x) offsetof(TextContext, x)
#define SD AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "linebreaks",      "Extra line breaks characters",    OFFSET(linebreaks),      AV_OPT_TYPE_STRING, {.str=NULL},    .flags=SD },
    { "keep_ass_markup", "Set if ASS tags must be escaped", OFFSET(keep_ass_markup), AV_OPT_TYPE_INT,    {.i64=0}, 0, 1, .flags=SD },
    { NULL }
};

static const AVClass text_decoder_class = {
    .class_name = "text decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static int text_event_to_ass(const AVCodecContext *avctx, AVBPrint *buf,
                             const char *p, const char *p_end)
{
    const TextContext *text = avctx->priv_data;

    for (; p < p_end && *p; p++) {

        /* forced custom line breaks, not accounted as "normal" EOL */
        if (text->linebreaks && strchr(text->linebreaks, *p)) {
            av_bprintf(buf, "\\N");

        /* standard ASS escaping so random characters don't get mis-interpreted
         * as ASS */
        } else if (!text->keep_ass_markup && strchr("{}\\", *p)) {
            av_bprintf(buf, "\\%c", *p);

        /* some packets might end abruptly (no \0 at the end, like for example
         * in some cases of demuxing from a classic video container), some
         * might be terminated with \n or \r\n which we have to remove (for
         * consistency with those who haven't), and we also have to deal with
         * evil cases such as \r at the end of the buffer (and no \0 terminated
         * character) */
        } else if (p[0] == '\n') {
            /* some stuff left so we can insert a line break */
            if (p < p_end - 1)
                av_bprintf(buf, "\\N");
        } else if (p[0] == '\r' && p < p_end - 1 && p[1] == '\n') {
            /* \r followed by a \n, we can skip it. We don't insert the \N yet
             * because we don't know if it is followed by more text */
            continue;

        /* finally, a sane character */
        } else {
            av_bprint_chars(buf, *p, 1);
        }
    }
    av_bprintf(buf, "\r\n");
    return 0;
}

static int text_decode_frame(AVCodecContext *avctx, void *data,
                             int *got_sub_ptr, AVPacket *avpkt)
{
    AVBPrint buf;
    AVSubtitle *sub = data;
    const char *ptr = avpkt->data;
    const int ts_start     = av_rescale_q(avpkt->pts,      avctx->time_base, (AVRational){1,100});
    const int ts_duration  = avpkt->duration != -1 ?
                             av_rescale_q(avpkt->duration, avctx->time_base, (AVRational){1,100}) : -1;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    if (ptr && avpkt->size > 0 && *ptr &&
        !text_event_to_ass(avctx, &buf, ptr, ptr + avpkt->size)) {
        if (!av_bprint_is_complete(&buf)) {
            av_bprint_finalize(&buf, NULL);
            return AVERROR(ENOMEM);
        }
        ff_ass_add_rect(sub, buf.str, ts_start, ts_duration, 0);
    }
    *got_sub_ptr = sub->num_rects > 0;
    av_bprint_finalize(&buf, NULL);
    return avpkt->size;
}

AVCodec ff_text_decoder = {
    .name           = "text",
    .priv_data_size = sizeof(TextContext),
    .long_name      = NULL_IF_CONFIG_SMALL("Raw text subtitle"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_TEXT,
    .decode         = text_decode_frame,
    .init           = ff_ass_subtitle_header_default,
    .priv_class     = &text_decoder_class,
};
