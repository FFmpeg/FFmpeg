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
 * SAMI subtitle decoder
 * @see http://msdn.microsoft.com/en-us/library/ms971327.aspx
 */

#include "ass.h"
#include "libavutil/avstring.h"
#include "libavutil/bprint.h"

typedef struct {
    AVBPrint source;
    AVBPrint content;
    AVBPrint full;
} SAMIContext;

static int sami_paragraph_to_ass(AVCodecContext *avctx, const char *src)
{
    SAMIContext *sami = avctx->priv_data;
    int ret = 0;
    char *tag = NULL;
    char *dupsrc = av_strdup(src);
    char *p = dupsrc;

    av_bprint_clear(&sami->content);
    for (;;) {
        char *saveptr = NULL;
        int prev_chr_is_space = 0;
        AVBPrint *dst = &sami->content;

        /* parse & extract paragraph tag */
        p = av_stristr(p, "<P");
        if (!p)
            break;
        if (p[2] != '>' && !av_isspace(p[2])) { // avoid confusion with tags such as <PRE>
            p++;
            continue;
        }
        if (dst->len) // add a separator with the previous paragraph if there was one
            av_bprintf(dst, "\\N");
        tag = av_strtok(p, ">", &saveptr);
        if (!tag || !saveptr)
            break;
        p = saveptr;

        /* check if the current paragraph is the "source" (speaker name) */
        if (av_stristr(tag, "ID=Source") || av_stristr(tag, "ID=\"Source\"")) {
            dst = &sami->source;
            av_bprint_clear(dst);
        }

        /* if empty event -> skip subtitle */
        while (av_isspace(*p))
            p++;
        if (!strncmp(p, "&nbsp;", 6)) {
            ret = -1;
            goto end;
        }

        /* extract the text, stripping most of the tags */
        while (*p) {
            if (*p == '<') {
                if (!av_strncasecmp(p, "<P", 2) && (p[2] == '>' || av_isspace(p[2])))
                    break;
                if (!av_strncasecmp(p, "<BR", 3))
                    av_bprintf(dst, "\\N");
                p++;
                while (*p && *p != '>')
                    p++;
                if (!*p)
                    break;
                if (*p == '>')
                    p++;
            }
            if (!av_isspace(*p))
                av_bprint_chars(dst, *p, 1);
            else if (!prev_chr_is_space)
                av_bprint_chars(dst, ' ', 1);
            prev_chr_is_space = av_isspace(*p);
            p++;
        }
    }

    av_bprint_clear(&sami->full);
    if (sami->source.len)
        av_bprintf(&sami->full, "{\\i1}%s{\\i0}\\N", sami->source.str);
    av_bprintf(&sami->full, "%s", sami->content.str);

end:
    av_free(dupsrc);
    return ret;
}

static int sami_decode_frame(AVCodecContext *avctx,
                             void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle *sub = data;
    const char *ptr = avpkt->data;
    SAMIContext *sami = avctx->priv_data;

    if (ptr && avpkt->size > 0 && !sami_paragraph_to_ass(avctx, ptr)) {
        int ts_start     = av_rescale_q(avpkt->pts, avctx->time_base, (AVRational){1,100});
        int ts_duration  = avpkt->duration != -1 ?
                           av_rescale_q(avpkt->duration, avctx->time_base, (AVRational){1,100}) : -1;
        int ret = ff_ass_add_rect_bprint(sub, &sami->full, ts_start, ts_duration);
        if (ret < 0)
            return ret;
    }
    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

static av_cold int sami_init(AVCodecContext *avctx)
{
    SAMIContext *sami = avctx->priv_data;
    av_bprint_init(&sami->source,  0, 2048);
    av_bprint_init(&sami->content, 0, 2048);
    av_bprint_init(&sami->full,    0, 2048);
    return ff_ass_subtitle_header_default(avctx);
}

static av_cold int sami_close(AVCodecContext *avctx)
{
    SAMIContext *sami = avctx->priv_data;
    av_bprint_finalize(&sami->source,  NULL);
    av_bprint_finalize(&sami->content, NULL);
    av_bprint_finalize(&sami->full,    NULL);
    return 0;
}

AVCodec ff_sami_decoder = {
    .name           = "sami",
    .long_name      = NULL_IF_CONFIG_SMALL("SAMI subtitle"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_SAMI,
    .priv_data_size = sizeof(SAMIContext),
    .init           = sami_init,
    .close          = sami_close,
    .decode         = sami_decode_frame,
};
