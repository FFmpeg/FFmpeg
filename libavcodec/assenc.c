/*
 * SSA/ASS encoder
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

#include <string.h>

#include "avcodec.h"
#include "ass_split.h"
#include "ass.h"
#include "libavutil/avstring.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

typedef struct {
    int id; ///< current event id, ReadOrder field
} ASSEncodeContext;

static av_cold int ass_encode_init(AVCodecContext *avctx)
{
    avctx->extradata = av_malloc(avctx->subtitle_header_size + 1);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);
    memcpy(avctx->extradata, avctx->subtitle_header, avctx->subtitle_header_size);
    avctx->extradata_size = avctx->subtitle_header_size;
    avctx->extradata[avctx->extradata_size] = 0;
    return 0;
}

static int ass_encode_frame(AVCodecContext *avctx,
                            unsigned char *buf, int bufsize,
                            const AVSubtitle *sub)
{
    ASSEncodeContext *s = avctx->priv_data;
    int i, len, total_len = 0;

    for (i=0; i<sub->num_rects; i++) {
        char ass_line[2048];
        const char *ass = sub->rects[i]->ass;

        if (sub->rects[i]->type != SUBTITLE_ASS) {
            av_log(avctx, AV_LOG_ERROR, "Only SUBTITLE_ASS type supported.\n");
            return -1;
        }

        if (strncmp(ass, "Dialogue: ", 10)) {
            av_log(avctx, AV_LOG_ERROR, "AVSubtitle rectangle ass \"%s\""
                   " does not look like a SSA markup\n", ass);
            return AVERROR_INVALIDDATA;
        }

        if (avctx->codec->id == AV_CODEC_ID_ASS) {
            long int layer;
            char *p;

            if (i > 0) {
                av_log(avctx, AV_LOG_ERROR, "ASS encoder supports only one "
                       "ASS rectangle field.\n");
                return AVERROR_INVALIDDATA;
            }

            ass += 10; // skip "Dialogue: "
            /* parse Layer field. If it's a Marked field, the content
             * will be "Marked=N" instead of the layer num, so we will
             * have layer=0, which is fine. */
            layer = strtol(ass, &p, 10);
            if (*p) p += strcspn(p, ",") + 1; // skip layer or marked
            if (*p) p += strcspn(p, ",") + 1; // skip start timestamp
            if (*p) p += strcspn(p, ",") + 1; // skip end timestamp
            snprintf(ass_line, sizeof(ass_line), "%d,%ld,%s", ++s->id, layer, p);
            ass_line[strcspn(ass_line, "\r\n")] = 0;
            ass = ass_line;
        }
        len = av_strlcpy(buf+total_len, ass, bufsize-total_len);

        if (len > bufsize-total_len-1) {
            av_log(avctx, AV_LOG_ERROR, "Buffer too small for ASS event.\n");
            return -1;
        }

        total_len += len;
    }

    return total_len;
}

#if CONFIG_SSA_ENCODER
AVCodec ff_ssa_encoder = {
    .name         = "ssa",
    .long_name    = NULL_IF_CONFIG_SMALL("SSA (SubStation Alpha) subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_SSA,
    .init         = ass_encode_init,
    .encode_sub   = ass_encode_frame,
    .priv_data_size = sizeof(ASSEncodeContext),
};
#endif

#if CONFIG_ASS_ENCODER
AVCodec ff_ass_encoder = {
    .name         = "ass",
    .long_name    = NULL_IF_CONFIG_SMALL("ASS (Advanced SubStation Alpha) subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_ASS,
    .init         = ass_encode_init,
    .encode_sub   = ass_encode_frame,
    .priv_data_size = sizeof(ASSEncodeContext),
};
#endif
