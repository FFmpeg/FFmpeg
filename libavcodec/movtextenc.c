/*
 * 3GPP TS 26.245 Timed Text encoder
 * Copyright (c) 2012  Philip Langdale <philipl@overt.org>
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

#include <stdarg.h>
#include "avcodec.h"
#include "libavutil/avassert.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "ass_split.h"
#include "ass.h"

typedef struct {
    ASSSplitContext *ass_ctx;
    char buffer[2048];
    char *ptr;
    char *end;
} MovTextContext;


static av_cold int mov_text_encode_init(AVCodecContext *avctx)
{
    /*
     * For now, we'll use a fixed default style. When we add styling
     * support, this will be generated from the ASS style.
     */
    static uint8_t text_sample_entry[] = {
        0x00, 0x00, 0x00, 0x00, // uint32_t displayFlags
        0x01,                   // int8_t horizontal-justification
        0xFF,                   // int8_t vertical-justification
        0x00, 0x00, 0x00, 0x00, // uint8_t background-color-rgba[4]
        // BoxRecord {
        0x00, 0x00,             // int16_t top
        0x00, 0x00,             // int16_t left
        0x00, 0x00,             // int16_t bottom
        0x00, 0x00,             // int16_t right
        // };
        // StyleRecord {
        0x00, 0x00,             // uint16_t startChar
        0x00, 0x00,             // uint16_t endChar
        0x00, 0x01,             // uint16_t font-ID
        0x00,                   // uint8_t face-style-flags
        0x12,                   // uint8_t font-size
        0xFF, 0xFF, 0xFF, 0xFF, // uint8_t text-color-rgba[4]
        // };
        // FontTableBox {
        0x00, 0x00, 0x00, 0x12, // uint32_t size
        'f', 't', 'a', 'b',     // uint8_t name[4]
        0x00, 0x01,             // uint16_t entry-count
        // FontRecord {
        0x00, 0x01,             // uint16_t font-ID
        0x05,                   // uint8_t font-name-length
        'S', 'e', 'r', 'i', 'f',// uint8_t font[font-name-length]
        // };
        // };
    };

    MovTextContext *s = avctx->priv_data;

    avctx->extradata_size = sizeof text_sample_entry;
    avctx->extradata = av_mallocz(avctx->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    memcpy(avctx->extradata, text_sample_entry, avctx->extradata_size);

    s->ass_ctx = ff_ass_split(avctx->subtitle_header);
    return s->ass_ctx ? 0 : AVERROR_INVALIDDATA;
}

static void mov_text_text_cb(void *priv, const char *text, int len)
{
    MovTextContext *s = priv;
    av_assert0(s->end >= s->ptr);
    av_strlcpy(s->ptr, text, FFMIN(s->end - s->ptr, len + 1));
    s->ptr += FFMIN(s->end - s->ptr, len);
}

static void mov_text_new_line_cb(void *priv, int forced)
{
    MovTextContext *s = priv;
    av_assert0(s->end >= s->ptr);
    av_strlcpy(s->ptr, "\n", FFMIN(s->end - s->ptr, 2));
    if (s->end > s->ptr)
        s->ptr++;
}

static const ASSCodesCallbacks mov_text_callbacks = {
    .text     = mov_text_text_cb,
    .new_line = mov_text_new_line_cb,
};

static int mov_text_encode_frame(AVCodecContext *avctx, unsigned char *buf,
                                 int bufsize, const AVSubtitle *sub)
{
    MovTextContext *s = avctx->priv_data;
    ASSDialog *dialog;
    int i, len, num;

    s->ptr = s->buffer;
    s->end = s->ptr + sizeof(s->buffer);

    for (i = 0; i < sub->num_rects; i++) {

        if (sub->rects[i]->type != SUBTITLE_ASS) {
            av_log(avctx, AV_LOG_ERROR, "Only SUBTITLE_ASS type supported.\n");
            return AVERROR(ENOSYS);
        }

        dialog = ff_ass_split_dialog(s->ass_ctx, sub->rects[i]->ass, 0, &num);
        for (; dialog && num--; dialog++) {
            ff_ass_split_override_codes(&mov_text_callbacks, s, dialog->text);
        }
    }

    if (s->ptr == s->buffer)
        return 0;

    AV_WB16(buf, strlen(s->buffer));
    buf += 2;

    len = av_strlcpy(buf, s->buffer, bufsize - 2);

    if (len > bufsize-3) {
        av_log(avctx, AV_LOG_ERROR, "Buffer too small for ASS event.\n");
        return AVERROR(EINVAL);
    }

    return len + 2;
}

static int mov_text_encode_close(AVCodecContext *avctx)
{
    MovTextContext *s = avctx->priv_data;
    ff_ass_split_free(s->ass_ctx);
    return 0;
}

AVCodec ff_movtext_encoder = {
    .name           = "mov_text",
    .long_name      = NULL_IF_CONFIG_SMALL("3GPP Timed Text subtitle"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_MOV_TEXT,
    .priv_data_size = sizeof(MovTextContext),
    .init           = mov_text_encode_init,
    .encode_sub     = mov_text_encode_frame,
    .close          = mov_text_encode_close,
};
