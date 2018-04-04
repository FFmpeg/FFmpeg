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
#include "libavutil/mem.h"
#include "libavutil/common.h"
#include "ass_split.h"
#include "ass.h"

#define STYLE_FLAG_BOLD         (1<<0)
#define STYLE_FLAG_ITALIC       (1<<1)
#define STYLE_FLAG_UNDERLINE    (1<<2)
#define STYLE_RECORD_SIZE       12
#define SIZE_ADD                10

#define STYL_BOX   (1<<0)
#define HLIT_BOX   (1<<1)
#define HCLR_BOX   (1<<2)

#define av_bprint_append_any(buf, data, size)   av_bprint_append_data(buf, ((const char*)data), size)

typedef struct {
    uint16_t style_start;
    uint16_t style_end;
    uint8_t style_flag;
} StyleBox;

typedef struct {
    uint16_t start;
    uint16_t end;
} HighlightBox;

typedef struct {
   uint32_t color;
} HilightcolorBox;

typedef struct {
    AVCodecContext *avctx;

    ASSSplitContext *ass_ctx;
    AVBPrint buffer;
    StyleBox **style_attributes;
    StyleBox *style_attributes_temp;
    HighlightBox hlit;
    HilightcolorBox hclr;
    int count;
    uint8_t box_flags;
    uint16_t style_entries;
    uint16_t style_fontID;
    uint8_t style_fontsize;
    uint32_t style_color;
    uint16_t text_pos;
    uint16_t byte_count;
} MovTextContext;

typedef struct {
    uint32_t type;
    void (*encode)(MovTextContext *s, uint32_t tsmb_type);
} Box;

static void mov_text_cleanup(MovTextContext *s)
{
    int j;
    if (s->box_flags & STYL_BOX) {
        for (j = 0; j < s->count; j++) {
            av_freep(&s->style_attributes[j]);
        }
        av_freep(&s->style_attributes);
    }
}

static void encode_styl(MovTextContext *s, uint32_t tsmb_type)
{
    int j;
    uint32_t tsmb_size;
    if (s->box_flags & STYL_BOX) {
        tsmb_size = s->count * STYLE_RECORD_SIZE + SIZE_ADD;
        tsmb_size = AV_RB32(&tsmb_size);
        s->style_entries = AV_RB16(&s->count);
        s->style_fontID = 0x00 | 0x01<<8;
        s->style_fontsize = 0x12;
        s->style_color = MKTAG(0xFF, 0xFF, 0xFF, 0xFF);
        /*The above three attributes are hard coded for now
        but will come from ASS style in the future*/
        av_bprint_append_any(&s->buffer, &tsmb_size, 4);
        av_bprint_append_any(&s->buffer, &tsmb_type, 4);
        av_bprint_append_any(&s->buffer, &s->style_entries, 2);
        for (j = 0; j < s->count; j++) {
            av_bprint_append_any(&s->buffer, &s->style_attributes[j]->style_start, 2);
            av_bprint_append_any(&s->buffer, &s->style_attributes[j]->style_end, 2);
            av_bprint_append_any(&s->buffer, &s->style_fontID, 2);
            av_bprint_append_any(&s->buffer, &s->style_attributes[j]->style_flag, 1);
            av_bprint_append_any(&s->buffer, &s->style_fontsize, 1);
            av_bprint_append_any(&s->buffer, &s->style_color, 4);
        }
        mov_text_cleanup(s);
    }
}

static void encode_hlit(MovTextContext *s, uint32_t tsmb_type)
{
    uint32_t tsmb_size;
    if (s->box_flags & HLIT_BOX) {
        tsmb_size = 12;
        tsmb_size = AV_RB32(&tsmb_size);
        av_bprint_append_any(&s->buffer, &tsmb_size, 4);
        av_bprint_append_any(&s->buffer, &tsmb_type, 4);
        av_bprint_append_any(&s->buffer, &s->hlit.start, 2);
        av_bprint_append_any(&s->buffer, &s->hlit.end, 2);
    }
}

static void encode_hclr(MovTextContext *s, uint32_t tsmb_type)
{
    uint32_t tsmb_size;
    if (s->box_flags & HCLR_BOX) {
        tsmb_size = 12;
        tsmb_size = AV_RB32(&tsmb_size);
        av_bprint_append_any(&s->buffer, &tsmb_size, 4);
        av_bprint_append_any(&s->buffer, &tsmb_type, 4);
        av_bprint_append_any(&s->buffer, &s->hclr.color, 4);
    }
}

static const Box box_types[] = {
    { MKTAG('s','t','y','l'), encode_styl },
    { MKTAG('h','l','i','t'), encode_hlit },
    { MKTAG('h','c','l','r'), encode_hclr },
};

const static size_t box_count = FF_ARRAY_ELEMS(box_types);

static av_cold int mov_text_encode_init(AVCodecContext *avctx)
{
    /*
     * For now, we'll use a fixed default style. When we add styling
     * support, this will be generated from the ASS style.
     */
    static const uint8_t text_sample_entry[] = {
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
    s->avctx = avctx;

    avctx->extradata_size = sizeof text_sample_entry;
    avctx->extradata = av_mallocz(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata)
        return AVERROR(ENOMEM);

    av_bprint_init(&s->buffer, 0, AV_BPRINT_SIZE_UNLIMITED);

    memcpy(avctx->extradata, text_sample_entry, avctx->extradata_size);

    s->ass_ctx = ff_ass_split(avctx->subtitle_header);
    return s->ass_ctx ? 0 : AVERROR_INVALIDDATA;
}

static void mov_text_style_cb(void *priv, const char style, int close)
{
    MovTextContext *s = priv;
    if (!close) {
        if (!(s->box_flags & STYL_BOX)) {   //first style entry

            s->style_attributes_temp = av_malloc(sizeof(*s->style_attributes_temp));

            if (!s->style_attributes_temp) {
                av_bprint_clear(&s->buffer);
                s->box_flags &= ~STYL_BOX;
                return;
            }

            s->style_attributes_temp->style_flag = 0;
            s->style_attributes_temp->style_start = AV_RB16(&s->text_pos);
        } else {
            if (s->style_attributes_temp->style_flag) { //break the style record here and start a new one
                s->style_attributes_temp->style_end = AV_RB16(&s->text_pos);
                av_dynarray_add(&s->style_attributes, &s->count, s->style_attributes_temp);
                s->style_attributes_temp = av_malloc(sizeof(*s->style_attributes_temp));
                if (!s->style_attributes_temp) {
                    mov_text_cleanup(s);
                    av_bprint_clear(&s->buffer);
                    s->box_flags &= ~STYL_BOX;
                    return;
                }

                s->style_attributes_temp->style_flag = s->style_attributes[s->count - 1]->style_flag;
                s->style_attributes_temp->style_start = AV_RB16(&s->text_pos);
            } else {
                s->style_attributes_temp->style_flag = 0;
                s->style_attributes_temp->style_start = AV_RB16(&s->text_pos);
            }
        }
        switch (style){
        case 'b':
            s->style_attributes_temp->style_flag |= STYLE_FLAG_BOLD;
            break;
        case 'i':
            s->style_attributes_temp->style_flag |= STYLE_FLAG_ITALIC;
            break;
        case 'u':
            s->style_attributes_temp->style_flag |= STYLE_FLAG_UNDERLINE;
            break;
        }
    } else if (!s->style_attributes_temp) {
        av_log(s->avctx, AV_LOG_WARNING, "Ignoring unmatched close tag\n");
        return;
    } else {
        s->style_attributes_temp->style_end = AV_RB16(&s->text_pos);
        av_dynarray_add(&s->style_attributes, &s->count, s->style_attributes_temp);

        s->style_attributes_temp = av_malloc(sizeof(*s->style_attributes_temp));

        if (!s->style_attributes_temp) {
            mov_text_cleanup(s);
            av_bprint_clear(&s->buffer);
            s->box_flags &= ~STYL_BOX;
            return;
        }

        s->style_attributes_temp->style_flag = s->style_attributes[s->count - 1]->style_flag;
        switch (style){
        case 'b':
            s->style_attributes_temp->style_flag &= ~STYLE_FLAG_BOLD;
            break;
        case 'i':
            s->style_attributes_temp->style_flag &= ~STYLE_FLAG_ITALIC;
            break;
        case 'u':
            s->style_attributes_temp->style_flag &= ~STYLE_FLAG_UNDERLINE;
            break;
        }
        if (s->style_attributes_temp->style_flag) { //start of new style record
            s->style_attributes_temp->style_start = AV_RB16(&s->text_pos);
        }
    }
    s->box_flags |= STYL_BOX;
}

static void mov_text_color_cb(void *priv, unsigned int color, unsigned int color_id)
{
    MovTextContext *s = priv;
    if (color_id == 2) {    //secondary color changes
        if (s->box_flags & HLIT_BOX) {  //close tag
            s->hlit.end = AV_RB16(&s->text_pos);
        } else {
            s->box_flags |= HCLR_BOX;
            s->box_flags |= HLIT_BOX;
            s->hlit.start = AV_RB16(&s->text_pos);
            s->hclr.color = color | (0xFF << 24);  //set alpha value to FF
        }
    }
    /* If there are more than one secondary color changes in ASS, take start of
       first section and end of last section. Movtext allows only one
       highlight box per sample.
     */
}

static uint16_t utf8_strlen(const char *text, int len)
{
    uint16_t i = 0, ret = 0;
    while (i < len) {
        char c = text[i];
        if ((c & 0x80) == 0)
            i += 1;
        else if ((c & 0xE0) == 0xC0)
            i += 2;
        else if ((c & 0xF0) == 0xE0)
            i += 3;
        else if ((c & 0xF8) == 0xF0)
            i += 4;
        else
            return 0;
        ret++;
    }
    return ret;
}

static void mov_text_text_cb(void *priv, const char *text, int len)
{
    uint16_t utf8_len = utf8_strlen(text, len);
    MovTextContext *s = priv;
    av_bprint_append_data(&s->buffer, text, len);
    // If it's not utf-8, just use the byte length
    s->text_pos += utf8_len ? utf8_len : len;
    s->byte_count += len;
}

static void mov_text_new_line_cb(void *priv, int forced)
{
    MovTextContext *s = priv;
    av_bprint_append_data(&s->buffer, "\n", 1);
    s->text_pos += 1;
    s->byte_count += 1;
}

static const ASSCodesCallbacks mov_text_callbacks = {
    .text     = mov_text_text_cb,
    .new_line = mov_text_new_line_cb,
    .style    = mov_text_style_cb,
    .color    = mov_text_color_cb,
};

static int mov_text_encode_frame(AVCodecContext *avctx, unsigned char *buf,
                                 int bufsize, const AVSubtitle *sub)
{
    MovTextContext *s = avctx->priv_data;
    ASSDialog *dialog;
    int i, length;
    size_t j;

    s->byte_count = 0;
    s->text_pos = 0;
    s->count = 0;
    s->box_flags = 0;
    s->style_entries = 0;
    for (i = 0; i < sub->num_rects; i++) {
        const char *ass = sub->rects[i]->ass;

        if (sub->rects[i]->type != SUBTITLE_ASS) {
            av_log(avctx, AV_LOG_ERROR, "Only SUBTITLE_ASS type supported.\n");
            return AVERROR(ENOSYS);
        }

#if FF_API_ASS_TIMING
        if (!strncmp(ass, "Dialogue: ", 10)) {
            int num;
            dialog = ff_ass_split_dialog(s->ass_ctx, ass, 0, &num);
            for (; dialog && num--; dialog++) {
                ff_ass_split_override_codes(&mov_text_callbacks, s, dialog->text);
            }
        } else {
#endif
            dialog = ff_ass_split_dialog2(s->ass_ctx, ass);
            if (!dialog)
                return AVERROR(ENOMEM);
            ff_ass_split_override_codes(&mov_text_callbacks, s, dialog->text);
            ff_ass_free_dialog(&dialog);
#if FF_API_ASS_TIMING
        }
#endif

        for (j = 0; j < box_count; j++) {
            box_types[j].encode(s, box_types[j].type);
        }
    }

    AV_WB16(buf, s->byte_count);
    buf += 2;

    if (!av_bprint_is_complete(&s->buffer)) {
        length = AVERROR(ENOMEM);
        goto exit;
    }

    if (!s->buffer.len) {
        length = 0;
        goto exit;
    }

    if (s->buffer.len > bufsize - 3) {
        av_log(avctx, AV_LOG_ERROR, "Buffer too small for ASS event.\n");
        length = AVERROR(EINVAL);
        goto exit;
    }

    memcpy(buf, s->buffer.str, s->buffer.len);
    length = s->buffer.len + 2;

exit:
    av_bprint_clear(&s->buffer);
    return length;
}

static int mov_text_encode_close(AVCodecContext *avctx)
{
    MovTextContext *s = avctx->priv_data;
    ff_ass_split_free(s->ass_ctx);
    av_bprint_finalize(&s->buffer, NULL);
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
