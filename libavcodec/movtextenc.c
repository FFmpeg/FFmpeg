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
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavutil/common.h"
#include "ass_split.h"
#include "ass.h"
#include "bytestream.h"
#include "codec_internal.h"

#define STYLE_FLAG_BOLD         (1<<0)
#define STYLE_FLAG_ITALIC       (1<<1)
#define STYLE_FLAG_UNDERLINE    (1<<2)
#define STYLE_RECORD_SIZE       12
#define SIZE_ADD                10

#define STYL_BOX   (1<<0)
#define HLIT_BOX   (1<<1)
#define HCLR_BOX   (1<<2)

#define DEFAULT_STYLE_FONT_ID  0x01
#define DEFAULT_STYLE_FONTSIZE 0x12
#define DEFAULT_STYLE_COLOR    0xffffffff
#define DEFAULT_STYLE_FLAG     0x00

#define BGR_TO_RGB(c) (((c) & 0xff) << 16 | ((c) & 0xff00) | (((uint32_t)(c) >> 16) & 0xff))
#define FONTSIZE_SCALE(s,fs) ((fs) * (s)->font_scale_factor + 0.5)
#define av_bprint_append_any(buf, data, size)   av_bprint_append_data(buf, ((const char*)data), size)

typedef struct {
    uint16_t style_start;
    uint16_t style_end;
    uint8_t style_flag;
    uint16_t style_fontID;
    uint8_t style_fontsize;
    uint32_t style_color;
} StyleBox;

typedef struct {
    uint16_t start;
    uint16_t end;
} HighlightBox;

typedef struct {
   uint32_t color;
} HilightcolorBox;

typedef struct {
    AVClass *class;
    AVCodecContext *avctx;

    ASSSplitContext *ass_ctx;
    ASSStyle *ass_dialog_style;
    StyleBox *style_attributes;
    unsigned  count;
    unsigned  style_attributes_bytes_allocated;
    StyleBox  style_attributes_temp;
    AVBPrint buffer;
    HighlightBox hlit;
    HilightcolorBox hclr;
    uint8_t box_flags;
    StyleBox d;
    uint16_t text_pos;
    char **fonts;
    int font_count;
    double font_scale_factor;
    int frame_height;
} MovTextContext;

typedef struct {
    void (*encode)(MovTextContext *s);
} Box;

static void mov_text_cleanup(MovTextContext *s)
{
    s->count = 0;
    s->style_attributes_temp = s->d;
}

static void encode_styl(MovTextContext *s)
{
    if ((s->box_flags & STYL_BOX) && s->count) {
        uint8_t buf[12], *p = buf;

        bytestream_put_be32(&p, s->count * STYLE_RECORD_SIZE + SIZE_ADD);
        bytestream_put_be32(&p, MKBETAG('s','t','y','l'));
        bytestream_put_be16(&p, s->count);
        /*The above three attributes are hard coded for now
        but will come from ASS style in the future*/
        av_bprint_append_any(&s->buffer, buf, 10);
        for (unsigned j = 0; j < s->count; j++) {
            const StyleBox *style = &s->style_attributes[j];

            p = buf;
            bytestream_put_be16(&p, style->style_start);
            bytestream_put_be16(&p, style->style_end);
            bytestream_put_be16(&p, style->style_fontID);
            bytestream_put_byte(&p, style->style_flag);
            bytestream_put_byte(&p, style->style_fontsize);
            bytestream_put_be32(&p, style->style_color);

            av_bprint_append_any(&s->buffer, buf, 12);
        }
    }
    mov_text_cleanup(s);
}

static void encode_hlit(MovTextContext *s)
{
    if (s->box_flags & HLIT_BOX) {
        uint8_t buf[12], *p = buf;

        bytestream_put_be32(&p, 12);
        bytestream_put_be32(&p, MKBETAG('h','l','i','t'));
        bytestream_put_be16(&p, s->hlit.start);
        bytestream_put_be16(&p, s->hlit.end);

        av_bprint_append_any(&s->buffer, buf, 12);
    }
}

static void encode_hclr(MovTextContext *s)
{
    if (s->box_flags & HCLR_BOX) {
        uint8_t buf[12], *p = buf;

        bytestream_put_be32(&p, 12);
        bytestream_put_be32(&p, MKBETAG('h','c','l','r'));
        bytestream_put_be32(&p, s->hclr.color);

        av_bprint_append_any(&s->buffer, buf, 12);
    }
}

static const Box box_types[] = {
    { encode_styl },
    { encode_hlit },
    { encode_hclr },
};

const static size_t box_count = FF_ARRAY_ELEMS(box_types);

static int mov_text_encode_close(AVCodecContext *avctx)
{
    MovTextContext *s = avctx->priv_data;

    ff_ass_split_free(s->ass_ctx);
    av_freep(&s->style_attributes);
    av_freep(&s->fonts);
    av_bprint_finalize(&s->buffer, NULL);
    return 0;
}

static int encode_sample_description(AVCodecContext *avctx)
{
    ASS *ass;
    ASSStyle *style;
    int i, j;
    uint32_t back_color = 0;
    int font_names_total_len = 0;
    MovTextContext *s = avctx->priv_data;
    uint8_t buf[30], *p = buf;

    //  0x00, 0x00, 0x00, 0x00, // uint32_t displayFlags
    //  0x01,                   // int8_t horizontal-justification
    //  0xFF,                   // int8_t vertical-justification
    //  0x00, 0x00, 0x00, 0x00, // uint8_t background-color-rgba[4]
    //     BoxRecord {
    //  0x00, 0x00,             // int16_t top
    //  0x00, 0x00,             // int16_t left
    //  0x00, 0x00,             // int16_t bottom
    //  0x00, 0x00,             // int16_t right
    //     };
    //     StyleRecord {
    //  0x00, 0x00,             // uint16_t startChar
    //  0x00, 0x00,             // uint16_t endChar
    //  0x00, 0x01,             // uint16_t font-ID
    //  0x00,                   // uint8_t face-style-flags
    //  0x12,                   // uint8_t font-size
    //  0xFF, 0xFF, 0xFF, 0xFF, // uint8_t text-color-rgba[4]
    //     };
    //     FontTableBox {
    //  0x00, 0x00, 0x00, 0x12, // uint32_t size
    //  'f', 't', 'a', 'b',     // uint8_t name[4]
    //  0x00, 0x01,             // uint16_t entry-count
    //     FontRecord {
    //  0x00, 0x01,             // uint16_t font-ID
    //  0x05,                   // uint8_t font-name-length
    //  'S', 'e', 'r', 'i', 'f',// uint8_t font[font-name-length]
    //     };
    //     };

    // Populate sample description from ASS header
    ass = (ASS*)s->ass_ctx;
    // Compute font scaling factor based on (optionally) provided
    // output video height and ASS script play_res_y
    if (s->frame_height && ass->script_info.play_res_y)
        s->font_scale_factor = (double)s->frame_height / ass->script_info.play_res_y;
    else
        s->font_scale_factor = 1;

    style = ff_ass_style_get(s->ass_ctx, "Default");
    if (!style && ass->styles_count) {
        style = &ass->styles[0];
    }
    s->d.style_fontID   = DEFAULT_STYLE_FONT_ID;
    s->d.style_fontsize = DEFAULT_STYLE_FONTSIZE;
    s->d.style_color    = DEFAULT_STYLE_COLOR;
    s->d.style_flag     = DEFAULT_STYLE_FLAG;
    if (style) {
        s->d.style_fontsize = FONTSIZE_SCALE(s, style->font_size);
        s->d.style_color = BGR_TO_RGB(style->primary_color & 0xffffff) << 8 |
                           255 - ((uint32_t)style->primary_color >> 24);
        s->d.style_flag = (!!style->bold      * STYLE_FLAG_BOLD)   |
                          (!!style->italic    * STYLE_FLAG_ITALIC) |
                          (!!style->underline * STYLE_FLAG_UNDERLINE);
        back_color = (BGR_TO_RGB(style->back_color & 0xffffff) << 8) |
                     (255 - ((uint32_t)style->back_color >> 24));
    }

    bytestream_put_be32(&p, 0); // displayFlags
    bytestream_put_be16(&p, 0x01FF); // horizontal/vertical justification (2x int8_t)
    bytestream_put_be32(&p, back_color);
    bytestream_put_be64(&p, 0); // BoxRecord - 4xint16_t: top, left, bottom, right
    //     StyleRecord {
    bytestream_put_be16(&p, s->d.style_start);
    bytestream_put_be16(&p, s->d.style_end);
    bytestream_put_be16(&p, s->d.style_fontID);
    bytestream_put_byte(&p, s->d.style_flag);
    bytestream_put_byte(&p, s->d.style_fontsize);
    bytestream_put_be32(&p, s->d.style_color);
    //     };
    av_bprint_append_any(&s->buffer, buf, 30);

    // Build font table
    // We can't build a complete font table since that would require
    // scanning all dialogs first.  But we can at least fill in what
    // is avaiable in the ASS header
    if (style && ass->styles_count) {
        // Find unique font names
        if (style->font_name) {
            av_dynarray_add(&s->fonts, &s->font_count, style->font_name);
            font_names_total_len += strlen(style->font_name);
        }
        for (i = 0; i < ass->styles_count; i++) {
            int found = 0;
            if (!ass->styles[i].font_name)
                continue;
            for (j = 0; j < s->font_count; j++) {
                if (!strcmp(s->fonts[j], ass->styles[i].font_name)) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                av_dynarray_add(&s->fonts, &s->font_count,
                                           ass->styles[i].font_name);
                font_names_total_len += strlen(ass->styles[i].font_name);
            }
        }
    } else
        av_dynarray_add(&s->fonts, &s->font_count, (char*)"Serif");

    //     FontTableBox {
    p = buf;
    bytestream_put_be32(&p, SIZE_ADD + 3 * s->font_count + font_names_total_len); // Size
    bytestream_put_be32(&p, MKBETAG('f','t','a','b'));
    bytestream_put_be16(&p, s->font_count);

    av_bprint_append_any(&s->buffer, buf, 10);
    //     FontRecord {
    for (i = 0; i < s->font_count; i++) {
        size_t len = strlen(s->fonts[i]);

        p = buf;
        bytestream_put_be16(&p, i + 1); //fontID
        bytestream_put_byte(&p, len);

        av_bprint_append_any(&s->buffer, buf, 3);
        av_bprint_append_any(&s->buffer, s->fonts[i], len);
    }
    //     };
    //     };

    if (!av_bprint_is_complete(&s->buffer)) {
        return AVERROR(ENOMEM);
    }

    avctx->extradata_size = s->buffer.len;
    avctx->extradata = av_mallocz(avctx->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!avctx->extradata) {
        return AVERROR(ENOMEM);
    }

    memcpy(avctx->extradata, s->buffer.str, avctx->extradata_size);
    av_bprint_clear(&s->buffer);

    return 0;
}

static av_cold int mov_text_encode_init(AVCodecContext *avctx)
{
    int ret;
    MovTextContext *s = avctx->priv_data;
    s->avctx = avctx;

    av_bprint_init(&s->buffer, 0, AV_BPRINT_SIZE_UNLIMITED);

    s->ass_ctx = ff_ass_split(avctx->subtitle_header);
    if (!s->ass_ctx)
        return AVERROR_INVALIDDATA;
    ret = encode_sample_description(avctx);
    if (ret < 0)
        return ret;

    return 0;
}

// Start a new style box if needed
static int mov_text_style_start(MovTextContext *s)
{
    // there's an existing style entry
    if (s->style_attributes_temp.style_start == s->text_pos)
        // Still at same text pos, use same entry
        return 1;
    if (s->style_attributes_temp.style_flag     != s->d.style_flag   ||
        s->style_attributes_temp.style_color    != s->d.style_color  ||
        s->style_attributes_temp.style_fontID   != s->d.style_fontID ||
        s->style_attributes_temp.style_fontsize != s->d.style_fontsize) {
        StyleBox *tmp;

        // last style != defaults, end the style entry and start a new one
        if (s->count + 1 > FFMIN(SIZE_MAX / sizeof(*s->style_attributes), UINT16_MAX) ||
            !(tmp = av_fast_realloc(s->style_attributes,
                                    &s->style_attributes_bytes_allocated,
                                    (s->count + 1) * sizeof(*s->style_attributes)))) {
            mov_text_cleanup(s);
            av_bprint_clear(&s->buffer);
            s->box_flags &= ~STYL_BOX;
            return 0;
        }
        s->style_attributes = tmp;
        s->style_attributes_temp.style_end = s->text_pos;
        s->style_attributes[s->count++] = s->style_attributes_temp;
        s->box_flags |= STYL_BOX;
        s->style_attributes_temp = s->d;
        s->style_attributes_temp.style_start = s->text_pos;
    } else { // style entry matches defaults, drop entry
        s->style_attributes_temp = s->d;
        s->style_attributes_temp.style_start = s->text_pos;
    }
    return 1;
}

static uint8_t mov_text_style_to_flag(const char style)
{
    uint8_t style_flag = 0;

    switch (style){
    case 'b':
        style_flag = STYLE_FLAG_BOLD;
        break;
    case 'i':
        style_flag = STYLE_FLAG_ITALIC;
        break;
    case 'u':
        style_flag = STYLE_FLAG_UNDERLINE;
        break;
    }
    return style_flag;
}

static void mov_text_style_set(MovTextContext *s, uint8_t style_flags)
{
    if (!((s->style_attributes_temp.style_flag & style_flags) ^ style_flags)) {
        // setting flags that that are already set
        return;
    }
    if (mov_text_style_start(s))
        s->style_attributes_temp.style_flag |= style_flags;
}

static void mov_text_style_cb(void *priv, const char style, int close)
{
    MovTextContext *s = priv;
    uint8_t style_flag = mov_text_style_to_flag(style);

    if (!!(s->style_attributes_temp.style_flag & style_flag) != close) {
        // setting flag that is already set
        return;
    }
    if (mov_text_style_start(s)) {
        if (!close)
            s->style_attributes_temp.style_flag |= style_flag;
        else
            s->style_attributes_temp.style_flag &= ~style_flag;
    }
}

static void mov_text_color_set(MovTextContext *s, uint32_t color)
{
    if ((s->style_attributes_temp.style_color & 0xffffff00) == color) {
        // color hasn't changed
        return;
    }
    if (mov_text_style_start(s))
        s->style_attributes_temp.style_color = (color & 0xffffff00) |
                            (s->style_attributes_temp.style_color & 0xff);
}

static void mov_text_color_cb(void *priv, unsigned int color, unsigned int color_id)
{
    MovTextContext *s = priv;

    color = BGR_TO_RGB(color) << 8;
    if (color_id == 1) {    //primary color changes
        mov_text_color_set(s, color);
    } else if (color_id == 2) {    //secondary color changes
        if (!(s->box_flags & HCLR_BOX))
            // Highlight alpha not set yet, use current primary alpha
            s->hclr.color = s->style_attributes_temp.style_color;
        if (!(s->box_flags & HLIT_BOX) || s->hlit.start == s->text_pos) {
            s->box_flags |= HCLR_BOX;
            s->box_flags |= HLIT_BOX;
            s->hlit.start = s->text_pos;
            s->hclr.color = color | (s->hclr.color & 0xFF);
        }
        else //close tag
            s->hlit.end = s->text_pos;
        /* If there are more than one secondary color changes in ASS,
           take start of first section and end of last section. Movtext
           allows only one highlight box per sample.
         */
    }
    // Movtext does not support changes to other color_id (outline, background)
}

static void mov_text_alpha_set(MovTextContext *s, uint8_t alpha)
{
    if ((s->style_attributes_temp.style_color & 0xff) == alpha) {
        // color hasn't changed
        return;
    }
    if (mov_text_style_start(s))
        s->style_attributes_temp.style_color =
                (s->style_attributes_temp.style_color & 0xffffff00) | alpha;
}

static void mov_text_alpha_cb(void *priv, int alpha, int alpha_id)
{
    MovTextContext *s = priv;

    alpha = 255 - alpha;
    if (alpha_id == 1) // primary alpha changes
        mov_text_alpha_set(s, alpha);
    else if (alpha_id == 2) {    //secondary alpha changes
        if (!(s->box_flags & HCLR_BOX))
            // Highlight color not set yet, use current primary color
            s->hclr.color = s->style_attributes_temp.style_color;
        if (!(s->box_flags & HLIT_BOX) || s->hlit.start == s->text_pos) {
            s->box_flags |= HCLR_BOX;
            s->box_flags |= HLIT_BOX;
            s->hlit.start = s->text_pos;
            s->hclr.color = (s->hclr.color & 0xffffff00) | alpha;
        }
        else //close tag
            s->hlit.end = s->text_pos;
    }
    // Movtext does not support changes to other alpha_id (outline, background)
}

static uint16_t find_font_id(MovTextContext *s, const char *name)
{
    if (!name)
        return 1;

    for (int i = 0; i < s->font_count; i++) {
        if (!strcmp(name, s->fonts[i]))
            return i + 1;
    }
    return 1;
}

static void mov_text_font_name_set(MovTextContext *s, const char *name)
{
    int fontID = find_font_id(s, name);
    if (s->style_attributes_temp.style_fontID == fontID) {
        // color hasn't changed
        return;
    }
    if (mov_text_style_start(s))
        s->style_attributes_temp.style_fontID = fontID;
}

static void mov_text_font_name_cb(void *priv, const char *name)
{
    mov_text_font_name_set((MovTextContext*)priv, name);
}

static void mov_text_font_size_set(MovTextContext *s, int size)
{
    size = FONTSIZE_SCALE(s, size);
    if (s->style_attributes_temp.style_fontsize == size) {
        // color hasn't changed
        return;
    }
    if (mov_text_style_start(s))
        s->style_attributes_temp.style_fontsize = size;
}

static void mov_text_font_size_cb(void *priv, int size)
{
    mov_text_font_size_set((MovTextContext*)priv, size);
}

static void mov_text_end_cb(void *priv)
{
    // End of text, close any open style record
    mov_text_style_start((MovTextContext*)priv);
}

static void mov_text_ass_style_set(MovTextContext *s, ASSStyle *style)
{
    uint8_t    style_flags, alpha;
    uint32_t   color;

    if (style) {
        style_flags = (!!style->bold      * STYLE_FLAG_BOLD)   |
                      (!!style->italic    * STYLE_FLAG_ITALIC) |
                      (!!style->underline * STYLE_FLAG_UNDERLINE);
        mov_text_style_set(s, style_flags);
        color = BGR_TO_RGB(style->primary_color & 0xffffff) << 8;
        mov_text_color_set(s, color);
        alpha = 255 - ((uint32_t)style->primary_color >> 24);
        mov_text_alpha_set(s, alpha);
        mov_text_font_size_set(s, style->font_size);
        mov_text_font_name_set(s, style->font_name);
    } else {
        // End current style record, go back to defaults
        mov_text_style_start(s);
    }
}

static void mov_text_dialog(MovTextContext *s, ASSDialog *dialog)
{
    ASSStyle *style = ff_ass_style_get(s->ass_ctx, dialog->style);

    s->ass_dialog_style = style;
    mov_text_ass_style_set(s, style);
}

static void mov_text_cancel_overrides_cb(void *priv, const char *style_name)
{
    MovTextContext *s = priv;
    ASSStyle *style;

    if (!style_name || !*style_name)
        style = s->ass_dialog_style;
    else
        style= ff_ass_style_get(s->ass_ctx, style_name);

    mov_text_ass_style_set(s, style);
}

static unsigned utf8_strlen(const char *text, int len)
{
    unsigned i = 0, ret = 0;
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
    unsigned utf8_len = utf8_strlen(text, len);
    MovTextContext *s = priv;
    av_bprint_append_data(&s->buffer, text, len);
    // If it's not utf-8, just use the byte length
    s->text_pos += utf8_len ? utf8_len : len;
}

static void mov_text_new_line_cb(void *priv, int forced)
{
    MovTextContext *s = priv;
    s->text_pos += 1;
    av_bprint_chars(&s->buffer, '\n', 1);
}

static const ASSCodesCallbacks mov_text_callbacks = {
    .text             = mov_text_text_cb,
    .new_line         = mov_text_new_line_cb,
    .style            = mov_text_style_cb,
    .color            = mov_text_color_cb,
    .alpha            = mov_text_alpha_cb,
    .font_name        = mov_text_font_name_cb,
    .font_size        = mov_text_font_size_cb,
    .cancel_overrides = mov_text_cancel_overrides_cb,
    .end              = mov_text_end_cb,
};

static int mov_text_encode_frame(AVCodecContext *avctx, unsigned char *buf,
                                 int bufsize, const AVSubtitle *sub)
{
    MovTextContext *s = avctx->priv_data;
    ASSDialog *dialog;
    int i, length;

    s->text_pos = 0;
    s->count = 0;
    s->box_flags = 0;
    av_bprint_clear(&s->buffer);
    for (i = 0; i < sub->num_rects; i++) {
        const char *ass = sub->rects[i]->ass;

        if (sub->rects[i]->type != SUBTITLE_ASS) {
            av_log(avctx, AV_LOG_ERROR, "Only SUBTITLE_ASS type supported.\n");
            return AVERROR(EINVAL);
        }

        dialog = ff_ass_split_dialog(s->ass_ctx, ass);
        if (!dialog)
            return AVERROR(ENOMEM);
        mov_text_dialog(s, dialog);
        ff_ass_split_override_codes(&mov_text_callbacks, s, dialog->text);
        ff_ass_free_dialog(&dialog);
    }

    if (s->buffer.len > UINT16_MAX)
        return AVERROR(ERANGE);
    AV_WB16(buf, s->buffer.len);
    buf += 2;

    for (size_t j = 0; j < box_count; j++)
        box_types[j].encode(s);

    if (!av_bprint_is_complete(&s->buffer))
        return AVERROR(ENOMEM);

    if (!s->buffer.len)
        return 0;

    if (s->buffer.len > bufsize - 3) {
        av_log(avctx, AV_LOG_ERROR, "Buffer too small for ASS event.\n");
        return AVERROR_BUFFER_TOO_SMALL;
    }

    memcpy(buf, s->buffer.str, s->buffer.len);
    length = s->buffer.len + 2;

    return length;
}

#define OFFSET(x) offsetof(MovTextContext, x)
#define FLAGS AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_SUBTITLE_PARAM
static const AVOption options[] = {
    { "height", "Frame height, usually video height", OFFSET(frame_height), AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass mov_text_encoder_class = {
    .class_name = "MOV text enoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_movtext_encoder = {
    .p.name         = "mov_text",
    .p.long_name    = NULL_IF_CONFIG_SMALL("3GPP Timed Text subtitle"),
    .p.type         = AVMEDIA_TYPE_SUBTITLE,
    .p.id           = AV_CODEC_ID_MOV_TEXT,
    .priv_data_size = sizeof(MovTextContext),
    .p.priv_class   = &mov_text_encoder_class,
    .init           = mov_text_encode_init,
    FF_CODEC_ENCODE_SUB_CB(mov_text_encode_frame),
    .close          = mov_text_encode_close,
    .caps_internal  = FF_CODEC_CAP_INIT_THREADSAFE | FF_CODEC_CAP_INIT_CLEANUP,
};
