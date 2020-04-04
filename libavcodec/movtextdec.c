/*
 * 3GPP TS 26.245 Timed Text decoder
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

#include "avcodec.h"
#include "ass.h"
#include "libavutil/opt.h"
#include "libavutil/avstring.h"
#include "libavutil/common.h"
#include "libavutil/bprint.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#define STYLE_FLAG_BOLD         (1<<0)
#define STYLE_FLAG_ITALIC       (1<<1)
#define STYLE_FLAG_UNDERLINE    (1<<2)

#define BOX_SIZE_INITIAL    40

#define STYL_BOX   (1<<0)
#define HLIT_BOX   (1<<1)
#define HCLR_BOX   (1<<2)
#define TWRP_BOX   (1<<3)

#define BOTTOM_LEFT     1
#define BOTTOM_CENTER   2
#define BOTTOM_RIGHT    3
#define MIDDLE_LEFT     4
#define MIDDLE_CENTER   5
#define MIDDLE_RIGHT    6
#define TOP_LEFT        7
#define TOP_CENTER      8
#define TOP_RIGHT       9

#define RGB_TO_BGR(c) (((c) & 0xff) << 16 | ((c) & 0xff00) | (((c) >> 16) & 0xff))

typedef struct {
    uint16_t fontID;
    const char *font;
    uint8_t fontsize;
    int color;
    uint8_t alpha;
    int back_color;
    uint8_t back_alpha;
    uint8_t bold;
    uint8_t italic;
    uint8_t underline;
    int alignment;
} MovTextDefault;

typedef struct {
    uint16_t fontID;
    char *font;
} FontRecord;

typedef struct {
    uint16_t style_start;
    uint16_t style_end;
    uint8_t style_flag;
    uint8_t bold;
    uint8_t italic;
    uint8_t underline;
    int color;
    uint8_t alpha;
    uint8_t fontsize;
    uint16_t style_fontID;
} StyleBox;

typedef struct {
    uint16_t hlit_start;
    uint16_t hlit_end;
} HighlightBox;

typedef struct {
   uint8_t hlit_color[4];
} HilightcolorBox;

typedef struct {
    uint8_t wrap_flag;
} TextWrapBox;

typedef struct {
    AVClass *class;
    StyleBox **s;
    StyleBox *s_temp;
    HighlightBox h;
    HilightcolorBox c;
    FontRecord **ftab;
    FontRecord *ftab_temp;
    TextWrapBox w;
    MovTextDefault d;
    uint8_t box_flags;
    uint16_t style_entries, ftab_entries;
    uint64_t tracksize;
    int size_var;
    int count_s, count_f;
    int readorder;
    int frame_width;
    int frame_height;
} MovTextContext;

typedef struct {
    uint32_t type;
    size_t base_size;
    int (*decode)(const uint8_t *tsmb, MovTextContext *m, AVPacket *avpkt);
} Box;

static void mov_text_cleanup(MovTextContext *m)
{
    int i;
    if (m->box_flags & STYL_BOX) {
        for(i = 0; i < m->count_s; i++) {
            av_freep(&m->s[i]);
        }
        av_freep(&m->s);
        m->count_s = 0;
        m->style_entries = 0;
    }
}

static void mov_text_cleanup_ftab(MovTextContext *m)
{
    int i;
    if (m->ftab_temp)
        av_freep(&m->ftab_temp->font);
    av_freep(&m->ftab_temp);
    if (m->ftab) {
        for(i = 0; i < m->count_f; i++) {
            av_freep(&m->ftab[i]->font);
            av_freep(&m->ftab[i]);
        }
    }
    av_freep(&m->ftab);
}

static int mov_text_tx3g(AVCodecContext *avctx, MovTextContext *m)
{
    uint8_t *tx3g_ptr = avctx->extradata;
    int i, box_size, font_length;
    int8_t v_align, h_align;
    StyleBox s_default;

    m->count_f = 0;
    m->ftab_entries = 0;
    box_size = BOX_SIZE_INITIAL; /* Size till ftab_entries */
    if (avctx->extradata_size < box_size)
        return -1;

    // Display Flags
    tx3g_ptr += 4;
    // Alignment
    h_align = *tx3g_ptr++;
    v_align = *tx3g_ptr++;
    if (h_align == 0) {
        if (v_align == 0)
            m->d.alignment = TOP_LEFT;
        if (v_align == 1)
            m->d.alignment = MIDDLE_LEFT;
        if (v_align == -1)
            m->d.alignment = BOTTOM_LEFT;
    }
    if (h_align == 1) {
        if (v_align == 0)
            m->d.alignment = TOP_CENTER;
        if (v_align == 1)
            m->d.alignment = MIDDLE_CENTER;
        if (v_align == -1)
            m->d.alignment = BOTTOM_CENTER;
    }
    if (h_align == -1) {
        if (v_align == 0)
            m->d.alignment = TOP_RIGHT;
        if (v_align == 1)
            m->d.alignment = MIDDLE_RIGHT;
        if (v_align == -1)
            m->d.alignment = BOTTOM_RIGHT;
    }
    // Background Color
    m->d.back_color = AV_RB24(tx3g_ptr);
    tx3g_ptr += 3;
    m->d.back_alpha = AV_RB8(tx3g_ptr);
    tx3g_ptr += 1;
    // BoxRecord
    tx3g_ptr += 8;
    // StyleRecord
    tx3g_ptr += 4;
    // fontID
    m->d.fontID = AV_RB16(tx3g_ptr);
    tx3g_ptr += 2;
    // face-style-flags
    s_default.style_flag = *tx3g_ptr++;
    m->d.bold = !!(s_default.style_flag & STYLE_FLAG_BOLD);
    m->d.italic = !!(s_default.style_flag & STYLE_FLAG_ITALIC);
    m->d.underline = !!(s_default.style_flag & STYLE_FLAG_UNDERLINE);
    // fontsize
    m->d.fontsize = *tx3g_ptr++;
    // Primary color
    m->d.color = AV_RB24(tx3g_ptr);
    tx3g_ptr += 3;
    m->d.alpha = AV_RB8(tx3g_ptr);
    tx3g_ptr += 1;
    // FontRecord
    // FontRecord Size
    tx3g_ptr += 4;
    // ftab
    tx3g_ptr += 4;

    m->ftab_entries = AV_RB16(tx3g_ptr);
    tx3g_ptr += 2;

    for (i = 0; i < m->ftab_entries; i++) {

        box_size += 3;
        if (avctx->extradata_size < box_size) {
            mov_text_cleanup_ftab(m);
            m->ftab_entries = 0;
            return -1;
        }
        m->ftab_temp = av_mallocz(sizeof(*m->ftab_temp));
        if (!m->ftab_temp) {
            mov_text_cleanup_ftab(m);
            return AVERROR(ENOMEM);
        }
        m->ftab_temp->fontID = AV_RB16(tx3g_ptr);
        tx3g_ptr += 2;
        font_length = *tx3g_ptr++;

        box_size = box_size + font_length;
        if (avctx->extradata_size < box_size) {
            mov_text_cleanup_ftab(m);
            m->ftab_entries = 0;
            return -1;
        }
        m->ftab_temp->font = av_malloc(font_length + 1);
        if (!m->ftab_temp->font) {
            mov_text_cleanup_ftab(m);
            return AVERROR(ENOMEM);
        }
        memcpy(m->ftab_temp->font, tx3g_ptr, font_length);
        m->ftab_temp->font[font_length] = '\0';
        av_dynarray_add(&m->ftab, &m->count_f, m->ftab_temp);
        if (!m->ftab) {
            mov_text_cleanup_ftab(m);
            return AVERROR(ENOMEM);
        }
        m->ftab_temp = NULL;
        tx3g_ptr = tx3g_ptr + font_length;
    }
    // In case of broken header, init default font
    m->d.font = ASS_DEFAULT_FONT;
    for (i = 0; i < m->ftab_entries; i++) {
        if (m->d.fontID == m->ftab[i]->fontID)
            m->d.font = m->ftab[i]->font;
    }
    return 0;
}

static int decode_twrp(const uint8_t *tsmb, MovTextContext *m, AVPacket *avpkt)
{
    m->box_flags |= TWRP_BOX;
    m->w.wrap_flag = *tsmb++;
    return 0;
}

static int decode_hlit(const uint8_t *tsmb, MovTextContext *m, AVPacket *avpkt)
{
    m->box_flags |= HLIT_BOX;
    m->h.hlit_start = AV_RB16(tsmb);
    tsmb += 2;
    m->h.hlit_end = AV_RB16(tsmb);
    tsmb += 2;
    return 0;
}

static int decode_hclr(const uint8_t *tsmb, MovTextContext *m, AVPacket *avpkt)
{
    m->box_flags |= HCLR_BOX;
    memcpy(m->c.hlit_color, tsmb, 4);
    tsmb += 4;
    return 0;
}

static int decode_styl(const uint8_t *tsmb, MovTextContext *m, AVPacket *avpkt)
{
    int i;
    int style_entries = AV_RB16(tsmb);
    tsmb += 2;
    // A single style record is of length 12 bytes.
    if (m->tracksize + m->size_var + 2 + style_entries * 12 > avpkt->size)
        return -1;

    m->style_entries = style_entries;

    m->box_flags |= STYL_BOX;
    for(i = 0; i < m->style_entries; i++) {
        m->s_temp = av_malloc(sizeof(*m->s_temp));
        if (!m->s_temp) {
            mov_text_cleanup(m);
            return AVERROR(ENOMEM);
        }
        m->s_temp->style_start = AV_RB16(tsmb);
        tsmb += 2;
        m->s_temp->style_end = AV_RB16(tsmb);

        if (   m->s_temp->style_end < m->s_temp->style_start
            || (m->count_s && m->s_temp->style_start < m->s[m->count_s - 1]->style_end)) {
            av_freep(&m->s_temp);
            mov_text_cleanup(m);
            return AVERROR(ENOMEM);
        }

        tsmb += 2;
        m->s_temp->style_fontID = AV_RB16(tsmb);
        tsmb += 2;
        m->s_temp->style_flag = AV_RB8(tsmb);
        m->s_temp->bold = !!(m->s_temp->style_flag & STYLE_FLAG_BOLD);
        m->s_temp->italic = !!(m->s_temp->style_flag & STYLE_FLAG_ITALIC);
        m->s_temp->underline = !!(m->s_temp->style_flag & STYLE_FLAG_UNDERLINE);
        tsmb++;
        m->s_temp->fontsize = AV_RB8(tsmb);
        tsmb++;
        m->s_temp->color = AV_RB24(tsmb);
        tsmb += 3;
        m->s_temp->alpha = AV_RB8(tsmb);
        tsmb++;
        av_dynarray_add(&m->s, &m->count_s, m->s_temp);
        if(!m->s) {
            mov_text_cleanup(m);
            return AVERROR(ENOMEM);
        }
    }
    return 0;
}

static const Box box_types[] = {
    { MKBETAG('s','t','y','l'), 2, decode_styl },
    { MKBETAG('h','l','i','t'), 4, decode_hlit },
    { MKBETAG('h','c','l','r'), 4, decode_hclr },
    { MKBETAG('t','w','r','p'), 1, decode_twrp }
};

const static size_t box_count = FF_ARRAY_ELEMS(box_types);

// Return byte length of the UTF-8 sequence starting at text[0]. 0 on error.
static int get_utf8_length_at(const char *text, const char *text_end)
{
    const char *start = text;
    int err = 0;
    uint32_t c;
    GET_UTF8(c, text < text_end ? (uint8_t)*text++ : (err = 1, 0), goto error;);
    if (err)
        goto error;
    return text - start;
error:
    return 0;
}

static int text_to_ass(AVBPrint *buf, const char *text, const char *text_end,
                       AVCodecContext *avctx)
{
    MovTextContext *m = avctx->priv_data;
    int i = 0;
    int text_pos = 0;
    int style_active = 0;
    int entry = 0;
    int color = m->d.color;

    if (text < text_end && m->box_flags & TWRP_BOX) {
        if (m->w.wrap_flag == 1) {
            av_bprintf(buf, "{\\q1}"); /* End of line wrap */
        } else {
            av_bprintf(buf, "{\\q2}"); /* No wrap */
        }
    }

    while (text < text_end) {
        int len;

        if ((m->box_flags & STYL_BOX) && entry < m->style_entries) {
            if (text_pos == m->s[entry]->style_start) {
                style_active = 1;
                if (m->s[entry]->bold ^ m->d.bold)
                    av_bprintf(buf, "{\\b%d}", m->s[entry]->bold);
                if (m->s[entry]->italic ^ m->d.italic)
                    av_bprintf(buf, "{\\i%d}", m->s[entry]->italic);
                if (m->s[entry]->underline ^ m->d.underline)
                    av_bprintf(buf, "{\\u%d}", m->s[entry]->underline);
                if (m->s[entry]->fontsize != m->d.fontsize)
                    av_bprintf(buf, "{\\fs%d}", m->s[entry]->fontsize);
                if (m->s[entry]->style_fontID != m->d.fontID)
                    for (i = 0; i < m->ftab_entries; i++) {
                        if (m->s[entry]->style_fontID == m->ftab[i]->fontID)
                            av_bprintf(buf, "{\\fn%s}", m->ftab[i]->font);
                    }
                if (m->d.color != m->s[entry]->color) {
                    color = m->s[entry]->color;
                    av_bprintf(buf, "{\\1c&H%X&}", RGB_TO_BGR(color));
                }
                if (m->d.alpha != m->s[entry]->alpha)
                    av_bprintf(buf, "{\\1a&H%02X&}", 255 - m->s[entry]->alpha);
            }
            if (text_pos == m->s[entry]->style_end) {
                if (style_active) {
                    av_bprintf(buf, "{\\r}");
                    style_active = 0;
                    color = m->d.color;
                }
                entry++;
            }
        }
        if (m->box_flags & HLIT_BOX) {
            if (text_pos == m->h.hlit_start) {
                /* If hclr box is present, set the secondary color to the color
                 * specified. Otherwise, set primary color to white and secondary
                 * color to black. These colors will come from TextSampleModifier
                 * boxes in future and inverse video technique for highlight will
                 * be implemented.
                 */
                if (m->box_flags & HCLR_BOX) {
                    av_bprintf(buf, "{\\2c&H%02x%02x%02x&}", m->c.hlit_color[2],
                                m->c.hlit_color[1], m->c.hlit_color[0]);
                } else {
                    av_bprintf(buf, "{\\1c&H000000&}{\\2c&HFFFFFF&}");
                }
            }
            if (text_pos == m->h.hlit_end) {
                if (m->box_flags & HCLR_BOX) {
                    av_bprintf(buf, "{\\2c&H%X&}", RGB_TO_BGR(m->d.color));
                } else {
                    av_bprintf(buf, "{\\1c&H%X&}{\\2c&H%X&}",
                               RGB_TO_BGR(color), RGB_TO_BGR(m->d.color));
                }
            }
        }

        len = get_utf8_length_at(text, text_end);
        if (len < 1) {
            av_log(avctx, AV_LOG_ERROR, "invalid UTF-8 byte in subtitle\n");
            len = 1;
        }
        for (i = 0; i < len; i++) {
            switch (*text) {
            case '\r':
                break;
            case '\n':
                av_bprintf(buf, "\\N");
                break;
            default:
                av_bprint_chars(buf, *text, 1);
                break;
            }
            text++;
        }
        text_pos++;
    }

    return 0;
}

static int mov_text_init(AVCodecContext *avctx) {
    /*
     * TODO: Handle the default text style.
     * NB: Most players ignore styles completely, with the result that
     * it's very common to find files where the default style is broken
     * and respecting it results in a worse experience than ignoring it.
     */
    int ret;
    MovTextContext *m = avctx->priv_data;
    ret = mov_text_tx3g(avctx, m);
    if (ret == 0) {
        if (!m->frame_width || !m->frame_height) {
            m->frame_width = ASS_DEFAULT_PLAYRESX;
            m->frame_height = ASS_DEFAULT_PLAYRESY;
        }
        return ff_ass_subtitle_header_full(avctx,
                    m->frame_width, m->frame_height,
                    m->d.font, m->d.fontsize,
                    (255 - m->d.alpha) << 24 | RGB_TO_BGR(m->d.color),
                    (255 - m->d.alpha) << 24 | RGB_TO_BGR(m->d.color),
                    (255 - m->d.back_alpha) << 24 | RGB_TO_BGR(m->d.back_color),
                    (255 - m->d.back_alpha) << 24 | RGB_TO_BGR(m->d.back_color),
                    m->d.bold, m->d.italic, m->d.underline,
                    ASS_DEFAULT_BORDERSTYLE, m->d.alignment);
    } else
        return ff_ass_subtitle_header_default(avctx);
}

static int mov_text_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_sub_ptr, AVPacket *avpkt)
{
    AVSubtitle *sub = data;
    MovTextContext *m = avctx->priv_data;
    int ret;
    AVBPrint buf;
    char *ptr = avpkt->data;
    char *end;
    int text_length, tsmb_type, ret_tsmb;
    uint64_t tsmb_size;
    const uint8_t *tsmb;
    size_t i;

    if (!ptr || avpkt->size < 2)
        return AVERROR_INVALIDDATA;

    /*
     * A packet of size two with value zero is an empty subtitle
     * used to mark the end of the previous non-empty subtitle.
     * We can just drop them here as we have duration information
     * already. If the value is non-zero, then it's technically a
     * bad packet.
     */
    if (avpkt->size == 2)
        return AV_RB16(ptr) == 0 ? 0 : AVERROR_INVALIDDATA;

    /*
     * The first two bytes of the packet are the length of the text string
     * In complex cases, there are style descriptors appended to the string
     * so we can't just assume the packet size is the string size.
     */
    text_length = AV_RB16(ptr);
    end = ptr + FFMIN(2 + text_length, avpkt->size);
    ptr += 2;

    mov_text_cleanup(m);

    tsmb_size = 0;
    m->tracksize = 2 + text_length;
    m->style_entries = 0;
    m->box_flags = 0;
    m->count_s = 0;
    // Note that the spec recommends lines be no longer than 2048 characters.
    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    if (text_length + 2 != avpkt->size) {
        while (m->tracksize + 8 <= avpkt->size) {
            // A box is a minimum of 8 bytes.
            tsmb = ptr + m->tracksize - 2;
            tsmb_size = AV_RB32(tsmb);
            tsmb += 4;
            tsmb_type = AV_RB32(tsmb);
            tsmb += 4;

            if (tsmb_size == 1) {
                if (m->tracksize + 16 > avpkt->size)
                    break;
                tsmb_size = AV_RB64(tsmb);
                tsmb += 8;
                m->size_var = 16;
            } else
                m->size_var = 8;
            //size_var is equal to 8 or 16 depending on the size of box

            if (tsmb_size == 0) {
                av_log(avctx, AV_LOG_ERROR, "tsmb_size is 0\n");
                return AVERROR_INVALIDDATA;
            }

            if (tsmb_size > avpkt->size - m->tracksize)
                break;

            for (i = 0; i < box_count; i++) {
                if (tsmb_type == box_types[i].type) {
                    if (m->tracksize + m->size_var + box_types[i].base_size > avpkt->size)
                        break;
                    ret_tsmb = box_types[i].decode(tsmb, m, avpkt);
                    if (ret_tsmb == -1)
                        break;
                }
            }
            m->tracksize = m->tracksize + tsmb_size;
        }
        text_to_ass(&buf, ptr, end, avctx);
        mov_text_cleanup(m);
    } else
        text_to_ass(&buf, ptr, end, avctx);

    ret = ff_ass_add_rect(sub, buf.str, m->readorder++, 0, NULL, NULL);
    av_bprint_finalize(&buf, NULL);
    if (ret < 0)
        return ret;
    *got_sub_ptr = sub->num_rects > 0;
    return avpkt->size;
}

static int mov_text_decode_close(AVCodecContext *avctx)
{
    MovTextContext *m = avctx->priv_data;
    mov_text_cleanup_ftab(m);
    mov_text_cleanup(m);
    return 0;
}

static void mov_text_flush(AVCodecContext *avctx)
{
    MovTextContext *m = avctx->priv_data;
    if (!(avctx->flags2 & AV_CODEC_FLAG2_RO_FLUSH_NOOP))
        m->readorder = 0;
}

#define OFFSET(x) offsetof(MovTextContext, x)
#define FLAGS AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_SUBTITLE_PARAM
static const AVOption options[] = {
    { "width", "Frame width, usually video width", OFFSET(frame_width), AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGS },
    { "height", "Frame height, usually video height", OFFSET(frame_height), AV_OPT_TYPE_INT, {.i64=0}, 0, INT_MAX, FLAGS },
    { NULL },
};

static const AVClass mov_text_decoder_class = {
    .class_name = "MOV text decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_movtext_decoder = {
    .name         = "mov_text",
    .long_name    = NULL_IF_CONFIG_SMALL("3GPP Timed Text subtitle"),
    .type         = AVMEDIA_TYPE_SUBTITLE,
    .id           = AV_CODEC_ID_MOV_TEXT,
    .priv_data_size = sizeof(MovTextContext),
    .priv_class   = &mov_text_decoder_class,
    .init         = mov_text_init,
    .decode       = mov_text_decode_frame,
    .close        = mov_text_decode_close,
    .flush        = mov_text_flush,
};
