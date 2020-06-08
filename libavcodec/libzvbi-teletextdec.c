/*
 * Teletext decoding for ffmpeg
 * Copyright (c) 2005-2010, 2012 Wolfram Gloger
 * Copyright (c) 2013 Marton Balint
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "avcodec.h"
#include "libavcodec/ass.h"
#include "libavcodec/dvbtxt.h"
#include "libavutil/opt.h"
#include "libavutil/bprint.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/log.h"
#include "libavutil/common.h"

#include <libzvbi.h>

#define TEXT_MAXSZ    (25 * (56 + 1) * 4 + 2)
#define VBI_NB_COLORS 40
#define VBI_TRANSPARENT_BLACK 8
#define RGBA(r,g,b,a) (((a) << 24) | ((r) << 16) | ((g) << 8) | (b))
#define VBI_R(rgba)   (((rgba) >> 0) & 0xFF)
#define VBI_G(rgba)   (((rgba) >> 8) & 0xFF)
#define VBI_B(rgba)   (((rgba) >> 16) & 0xFF)
#define VBI_A(rgba)   (((rgba) >> 24) & 0xFF)
#define MAX_BUFFERED_PAGES 25
#define BITMAP_CHAR_WIDTH  12
#define BITMAP_CHAR_HEIGHT 10
#define MAX_SLICES 64

typedef struct TeletextPage
{
    AVSubtitleRect *sub_rect;
    int pgno;
    int subno;
    int64_t pts;
} TeletextPage;

typedef struct TeletextContext
{
    AVClass        *class;
    char           *pgno;
    int             default_region;
    int             x_offset;
    int             y_offset;
    int             format_id; /* 0 = bitmap, 1 = text/ass, 2 = ass */
    int             chop_top;
    int             sub_duration; /* in msec */
    int             transparent_bg;
    int             opacity;
    int             chop_spaces;

    int             lines_processed;
    TeletextPage    *pages;
    int             nb_pages;
    int64_t         pts;
    int             handler_ret;

    vbi_decoder *   vbi;
    vbi_sliced      sliced[MAX_SLICES];

    int             readorder;
    uint8_t         subtitle_map[2048];
    int             last_pgno;
    int             last_p5;
    int             last_ass_alignment;
} TeletextContext;

static int my_ass_subtitle_header(AVCodecContext *avctx)
{
    int ret = ff_ass_subtitle_header_default(avctx);
    char *new_header;
    uint8_t *event_pos;

    if (ret < 0)
        return ret;

    event_pos = strstr(avctx->subtitle_header, "\r\n[Events]\r\n");
    if (!event_pos)
        return AVERROR_BUG;

    new_header = av_asprintf("%.*s%s%s",
        (int)(event_pos - avctx->subtitle_header), avctx->subtitle_header,
        "Style: "
        "Teletext,"            /* Name */
        "Monospace,11,"        /* Font{name,size} */
        "&Hffffff,&Hffffff,&H0,&H0," /* {Primary,Secondary,Outline,Back}Colour */
        "0,0,0,0,"             /* Bold, Italic, Underline, StrikeOut */
        "160,100,"             /* Scale{X,Y} */
        "0,0,"                 /* Spacing, Angle */
        "3,0.1,0,"             /* BorderStyle, Outline, Shadow */
        "5,1,1,1,"             /* Alignment, Margin[LRV] */
        "0\r\n"                /* Encoding */
        "Style: "
        "Subtitle,"            /* Name */
        "Monospace,16,"        /* Font{name,size} */
        "&Hffffff,&Hffffff,&H0,&H0," /* {Primary,Secondary,Outline,Back}Colour */
        "0,0,0,0,"             /* Bold, Italic, Underline, StrikeOut */
        "100,100,"             /* Scale{X,Y} */
        "0,0,"                 /* Spacing, Angle */
        "1,1,1,"               /* BorderStyle, Outline, Shadow */
        "8,48,48,20,"          /* Alignment, Margin[LRV] */
        "0\r\n"                /* Encoding */
        , event_pos);

    if (!new_header)
        return AVERROR(ENOMEM);

    av_free(avctx->subtitle_header);
    avctx->subtitle_header = new_header;
    avctx->subtitle_header_size = strlen(new_header);
    return 0;
}

static int chop_spaces_utf8(const unsigned char* t, int len)
{
    t += len;
    while (len > 0) {
        if (*--t != ' ' || (len-1 > 0 && *(t-1) & 0x80))
            break;
        --len;
    }
    return len;
}

static void subtitle_rect_free(AVSubtitleRect **sub_rect)
{
    av_freep(&(*sub_rect)->data[0]);
    av_freep(&(*sub_rect)->data[1]);
    av_freep(&(*sub_rect)->ass);
    av_freep(sub_rect);
}

static char *create_ass_text(TeletextContext *ctx, const char *text)
{
    char *dialog;
    AVBPrint buf;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);
    ff_ass_bprint_text_event(&buf, text, strlen(text), "", 0);
    if (!av_bprint_is_complete(&buf)) {
        av_bprint_finalize(&buf, NULL);
        return NULL;
    }
    dialog = ff_ass_get_dialog(ctx->readorder++, 0, NULL, NULL, buf.str);
    av_bprint_finalize(&buf, NULL);
    return dialog;
}

/* Draw a page as text */
static int gen_sub_text(TeletextContext *ctx, AVSubtitleRect *sub_rect, vbi_page *page, int chop_top)
{
    const char *in;
    AVBPrint buf;
    char *vbi_text = av_malloc(TEXT_MAXSZ);
    int sz;

    if (!vbi_text)
        return AVERROR(ENOMEM);

    sz = vbi_print_page_region(page, vbi_text, TEXT_MAXSZ-1, "UTF-8",
                                   /*table mode*/ TRUE, FALSE,
                                   0,             chop_top,
                                   page->columns, page->rows-chop_top);
    if (sz <= 0) {
        av_log(ctx, AV_LOG_ERROR, "vbi_print error\n");
        av_free(vbi_text);
        return AVERROR_EXTERNAL;
    }
    vbi_text[sz] = '\0';
    in  = vbi_text;
    av_bprint_init(&buf, 0, TEXT_MAXSZ);

    if (ctx->chop_spaces) {
        for (;;) {
            int nl, sz;

            // skip leading spaces and newlines
            in += strspn(in, " \n");
            // compute end of row
            for (nl = 0; in[nl]; ++nl)
                if (in[nl] == '\n' && (nl==0 || !(in[nl-1] & 0x80)))
                    break;
            if (!in[nl])
                break;
            // skip trailing spaces
            sz = chop_spaces_utf8(in, nl);
            av_bprint_append_data(&buf, in, sz);
            av_bprintf(&buf, "\n");
            in += nl;
        }
    } else {
        av_bprintf(&buf, "%s\n", vbi_text);
    }
    av_free(vbi_text);

    if (!av_bprint_is_complete(&buf)) {
        av_bprint_finalize(&buf, NULL);
        return AVERROR(ENOMEM);
    }

    if (buf.len) {
        sub_rect->type = SUBTITLE_ASS;
        sub_rect->ass = create_ass_text(ctx, buf.str);

        if (!sub_rect->ass) {
            av_bprint_finalize(&buf, NULL);
            return AVERROR(ENOMEM);
        }
        av_log(ctx, AV_LOG_DEBUG, "subtext:%s:txetbus\n", sub_rect->ass);
    } else {
        sub_rect->type = SUBTITLE_NONE;
    }
    av_bprint_finalize(&buf, NULL);
    return 0;
}

static void bprint_color(const char *type, AVBPrint *buf, vbi_page *page, unsigned ci)
{
    int r = VBI_R(page->color_map[ci]);
    int g = VBI_G(page->color_map[ci]);
    int b = VBI_B(page->color_map[ci]);
    av_bprintf(buf, "{\\%s&H%02X%02X%02X&}", type, b, g, r);
}

#define IS_TXT_SPACE(ch) ((ch).unicode < 0x0020 || (ch).unicode >= 0xe000 || (ch).unicode == 0x00a0 ||\
                          (ch).size > VBI_DOUBLE_SIZE || (ch).opacity == VBI_TRANSPARENT_SPACE)

static void get_trim_info(vbi_page *page, vbi_char *row, int *leading, int *trailing, int *olen)
{
    int i, len = 0;
    int char_seen = 0;

    *leading = 0;

    for (i = 0; i < page->columns; i++) {
        uint16_t out = IS_TXT_SPACE(row[i]) ? 32 : row[i].unicode;

        if (out == 32 && !char_seen)
            (*leading)++;
        else if (out != 32)
            char_seen = 1, len = i - (*leading) + 1;
    }

    *olen = len;
    *trailing = len > 0 ? page->columns - *leading - len : page->columns;
}

static void decode_string(vbi_page *page, vbi_char *row, AVBPrint *buf,
                          int start, int end, vbi_color *cur_color, vbi_color *cur_back_color)
{
    int i;

    for (i = start; i < end; i++) {
        uint16_t out = IS_TXT_SPACE(row[i]) ? 32 : row[i].unicode;

        if (*cur_color != row[i].foreground) {
            bprint_color("c", buf, page, row[i].foreground);
            *cur_color = row[i].foreground;
        }
        if (*cur_back_color != row[i].background) {
            bprint_color("3c", buf, page, row[i].background);
            *cur_back_color = row[i].background;
        }

        if (out == 32) {
            av_bprintf(buf, "\\h");
        } else if (out == '\\' || out == '{' || out == '}') {
            av_bprintf(buf, "\\%c", (char)out);
        } else {
            char tmp;
            /* convert to utf-8 */
            PUT_UTF8(out, tmp, av_bprint_chars(buf, tmp, 1););
        }
    }
}

/* Draw a page as ass formatted text */
static int gen_sub_ass(TeletextContext *ctx, AVSubtitleRect *sub_rect, vbi_page *page, int chop_top)
{
    int i;
    int leading, trailing, len;
    int last_trailing = -1, last_leading = -1;
    int min_trailing = page->columns, min_leading = page->columns;
    int alignment = 2;
    int vertical_align = -1;
    int can_align_left = 1, can_align_right = 1, can_align_center = 1;
    int is_subtitle_page = ctx->subtitle_map[page->pgno & 0x7ff];
    int empty_lines = 0;
    vbi_color cur_color = VBI_WHITE;
    vbi_color cur_back_color = VBI_BLACK;
    AVBPrint buf;

    av_bprint_init(&buf, 0, AV_BPRINT_SIZE_UNLIMITED);

    for (i = chop_top; i < page->rows; i++) {
        vbi_char *row = page->text + i * page->columns;

        get_trim_info(page, row, &leading, &trailing, &len);

        if (len) {
            if (last_leading != -1 && last_leading != leading || leading > 5)
                can_align_left = 0;
            if (last_trailing != -1 && last_trailing != trailing || trailing > 2)
                can_align_right = 0;
            if (last_trailing != -1 && (FFABS((trailing - leading) - (last_trailing - last_leading)) > 1) || trailing - leading > 4)
                can_align_center = 0;
            last_leading = leading;
            last_trailing = trailing;
            min_leading = FFMIN(leading, min_leading);
            min_trailing = FFMIN(trailing, min_trailing);
        }
    }

    if (!can_align_right && can_align_left && !can_align_center) {
        ctx->last_ass_alignment = alignment = 1;
    } else if (!can_align_right && !can_align_left && can_align_center) {
        ctx->last_ass_alignment = alignment = 2;
    } else if (can_align_right && !can_align_left && !can_align_center) {
        ctx->last_ass_alignment = alignment = 3;
    } else {
        if (ctx->last_ass_alignment == 1 && can_align_left ||
            ctx->last_ass_alignment == 2 && can_align_center ||
            ctx->last_ass_alignment == 3 && can_align_right)
            alignment = ctx->last_ass_alignment;
    }

    for (i = chop_top; i < page->rows; i++) {
        int j;
        vbi_char *row = page->text + i * page->columns;
        int is_transparent_line;

        for (j = 0; j < page->columns; j++)
            if (row[j].opacity != VBI_TRANSPARENT_SPACE)
                break;
        is_transparent_line = (j == page->columns);

        len = is_transparent_line ? 0 : page->columns;
        leading = trailing = is_transparent_line ? page->columns : 0;

        if (is_subtitle_page) {
            if (!is_transparent_line)
                get_trim_info(page, row, &leading, &trailing, &len);

            if (vertical_align == -1 && len) {
                vertical_align = (2 - (av_clip(i + 1, 0, 23) / 8));
                av_bprintf(&buf, "{\\an%d}", alignment + vertical_align * 3);
                if (vertical_align != 2)
                    empty_lines = 0;
            }

            if (len && empty_lines > 1)
                for (empty_lines /= 2; empty_lines > 0; empty_lines--)
                    av_bprintf(&buf, " \\N");

            if (alignment == 1 || alignment == 2 && !can_align_center)
                leading = min_leading;
            if (alignment == 3 || alignment == 2 && !can_align_center)
                trailing = min_trailing;
        }

        if (len || !is_subtitle_page) {
            decode_string(page, row, &buf, leading, page->columns - trailing, &cur_color, &cur_back_color);
            av_bprintf(&buf, " \\N");
            empty_lines = 0;
        } else {
            empty_lines++;
        }
    }

    if (vertical_align == 0)
        for (empty_lines = (empty_lines - 1) / 2; empty_lines > 0; empty_lines--)
            av_bprintf(&buf, " \\N");

    if (!av_bprint_is_complete(&buf)) {
        av_bprint_finalize(&buf, NULL);
        return AVERROR(ENOMEM);
    }

    if (buf.len) {
        sub_rect->type = SUBTITLE_ASS;
        sub_rect->ass = ff_ass_get_dialog(ctx->readorder++, 0, is_subtitle_page ? "Subtitle" : "Teletext", NULL, buf.str);

        if (!sub_rect->ass) {
            av_bprint_finalize(&buf, NULL);
            return AVERROR(ENOMEM);
        }
        av_log(ctx, AV_LOG_DEBUG, "subtext:%s:txetbus\n", sub_rect->ass);
    } else {
        sub_rect->type = SUBTITLE_NONE;
    }
    av_bprint_finalize(&buf, NULL);
    return 0;
}

static void fix_transparency(TeletextContext *ctx, AVSubtitleRect *sub_rect, vbi_page *page,
                             int chop_top, int resx, int resy)
{
    int iy;

    // Hack for transparency, inspired by VLC code...
    for (iy = 0; iy < resy; iy++) {
        uint8_t *pixel = sub_rect->data[0] + iy * sub_rect->linesize[0];
        vbi_char *vc = page->text + (iy / BITMAP_CHAR_HEIGHT + chop_top) * page->columns;
        vbi_char *vcnext = vc + page->columns;
        for (; vc < vcnext; vc++) {
            uint8_t *pixelnext = pixel + BITMAP_CHAR_WIDTH;
            switch (vc->opacity) {
                case VBI_TRANSPARENT_SPACE:
                    memset(pixel, VBI_TRANSPARENT_BLACK, BITMAP_CHAR_WIDTH);
                    break;
                case VBI_OPAQUE:
                    if (!ctx->transparent_bg)
                        break;
                case VBI_SEMI_TRANSPARENT:
                    if (ctx->opacity > 0) {
                        if (ctx->opacity < 255)
                            for(; pixel < pixelnext; pixel++)
                                if (*pixel == vc->background)
                                    *pixel += VBI_NB_COLORS;
                        break;
                    }
                case VBI_TRANSPARENT_FULL:
                    for(; pixel < pixelnext; pixel++)
                        if (*pixel == vc->background)
                            *pixel = VBI_TRANSPARENT_BLACK;
                    break;
            }
            pixel = pixelnext;
        }
    }
}

/* Draw a page as bitmap */
static int gen_sub_bitmap(TeletextContext *ctx, AVSubtitleRect *sub_rect, vbi_page *page, int chop_top)
{
    int resx = page->columns * BITMAP_CHAR_WIDTH;
    int resy = (page->rows - chop_top) * BITMAP_CHAR_HEIGHT;
    uint8_t ci;
    vbi_char *vc = page->text + (chop_top * page->columns);
    vbi_char *vcend = page->text + (page->rows * page->columns);

    for (; vc < vcend; vc++) {
        if (vc->opacity != VBI_TRANSPARENT_SPACE)
            break;
    }

    if (vc >= vcend) {
        av_log(ctx, AV_LOG_DEBUG, "dropping empty page %3x\n", page->pgno);
        sub_rect->type = SUBTITLE_NONE;
        return 0;
    }

    sub_rect->data[0] = av_mallocz(resx * resy);
    sub_rect->linesize[0] = resx;
    if (!sub_rect->data[0])
        return AVERROR(ENOMEM);

    vbi_draw_vt_page_region(page, VBI_PIXFMT_PAL8,
                            sub_rect->data[0], sub_rect->linesize[0],
                            0, chop_top, page->columns, page->rows - chop_top,
                            /*reveal*/ 1, /*flash*/ 1);

    fix_transparency(ctx, sub_rect, page, chop_top, resx, resy);
    sub_rect->x = ctx->x_offset;
    sub_rect->y = ctx->y_offset + chop_top * BITMAP_CHAR_HEIGHT;
    sub_rect->w = resx;
    sub_rect->h = resy;
    sub_rect->nb_colors = ctx->opacity > 0 && ctx->opacity < 255 ? 2 * VBI_NB_COLORS : VBI_NB_COLORS;
    sub_rect->data[1] = av_mallocz(AVPALETTE_SIZE);
    if (!sub_rect->data[1]) {
        av_freep(&sub_rect->data[0]);
        return AVERROR(ENOMEM);
    }
    for (ci = 0; ci < VBI_NB_COLORS; ci++) {
        int r, g, b, a;

        r = VBI_R(page->color_map[ci]);
        g = VBI_G(page->color_map[ci]);
        b = VBI_B(page->color_map[ci]);
        a = VBI_A(page->color_map[ci]);
        ((uint32_t *)sub_rect->data[1])[ci] = RGBA(r, g, b, a);
        ((uint32_t *)sub_rect->data[1])[ci + VBI_NB_COLORS] = RGBA(r, g, b, ctx->opacity);
        ff_dlog(ctx, "palette %0x\n", ((uint32_t *)sub_rect->data[1])[ci]);
    }
    ((uint32_t *)sub_rect->data[1])[VBI_TRANSPARENT_BLACK] = RGBA(0, 0, 0, 0);
    ((uint32_t *)sub_rect->data[1])[VBI_TRANSPARENT_BLACK + VBI_NB_COLORS] = RGBA(0, 0, 0, 0);
    sub_rect->type = SUBTITLE_BITMAP;
    return 0;
}

static void handler(vbi_event *ev, void *user_data)
{
    TeletextContext *ctx = user_data;
    TeletextPage *new_pages;
    vbi_page page;
    int res;
    char pgno_str[12];
    int chop_top;
    int is_subtitle_page = ctx->subtitle_map[ev->ev.ttx_page.pgno & 0x7ff];

    snprintf(pgno_str, sizeof pgno_str, "%03x", ev->ev.ttx_page.pgno);
    av_log(ctx, AV_LOG_DEBUG, "decoded page %s.%02x\n",
           pgno_str, ev->ev.ttx_page.subno & 0xFF);

    if (strcmp(ctx->pgno, "*") && (strcmp(ctx->pgno, "subtitle") || !is_subtitle_page) && !strstr(ctx->pgno, pgno_str))
        return;
    if (ctx->handler_ret < 0)
        return;

    res = vbi_fetch_vt_page(ctx->vbi, &page,
                            ev->ev.ttx_page.pgno,
                            ev->ev.ttx_page.subno,
                            VBI_WST_LEVEL_3p5, 25, TRUE);

    if (!res)
        return;

    chop_top = ctx->chop_top || ((page.rows > 1) && is_subtitle_page);

    av_log(ctx, AV_LOG_DEBUG, "%d x %d page chop:%d\n",
           page.columns, page.rows, chop_top);

    if (ctx->nb_pages < MAX_BUFFERED_PAGES) {
        if ((new_pages = av_realloc_array(ctx->pages, ctx->nb_pages + 1, sizeof(TeletextPage)))) {
            TeletextPage *cur_page = new_pages + ctx->nb_pages;
            ctx->pages = new_pages;
            cur_page->sub_rect = av_mallocz(sizeof(*cur_page->sub_rect));
            cur_page->pts = ctx->pts;
            cur_page->pgno = ev->ev.ttx_page.pgno;
            cur_page->subno = ev->ev.ttx_page.subno;
            if (cur_page->sub_rect) {
                switch (ctx->format_id) {
                    case 0:
                        res = gen_sub_bitmap(ctx, cur_page->sub_rect, &page, chop_top);
                        break;
                    case 1:
                        res = gen_sub_text(ctx, cur_page->sub_rect, &page, chop_top);
                        break;
                    case 2:
                        res = gen_sub_ass(ctx, cur_page->sub_rect, &page, chop_top);
                        break;
                    default:
                        res = AVERROR_BUG;
                        break;
                }
                if (res < 0) {
                    av_freep(&cur_page->sub_rect);
                    ctx->handler_ret = res;
                } else {
                    ctx->pages[ctx->nb_pages++] = *cur_page;
                }
            } else {
                ctx->handler_ret = AVERROR(ENOMEM);
            }
        } else {
            ctx->handler_ret = AVERROR(ENOMEM);
        }
    } else {
        //TODO: If multiple packets contain more than one page, pages may got queued up, and this may happen...
        av_log(ctx, AV_LOG_ERROR, "Buffered too many pages, dropping page %s.\n", pgno_str);
        ctx->handler_ret = AVERROR(ENOSYS);
    }

    vbi_unref_page(&page);
}

static int slice_to_vbi_lines(TeletextContext *ctx, uint8_t* buf, int size)
{
    int lines = 0;
    while (size >= 2 && lines < MAX_SLICES) {
        int data_unit_id     = buf[0];
        int data_unit_length = buf[1];
        if (data_unit_length + 2 > size)
            return AVERROR_INVALIDDATA;
        if (ff_data_unit_id_is_teletext(data_unit_id)) {
            if (data_unit_length != 0x2c)
                return AVERROR_INVALIDDATA;
            else {
                int line_offset  = buf[2] & 0x1f;
                int field_parity = buf[2] & 0x20;
                uint8_t *p = ctx->sliced[lines].data;
                int i, pmag;
                ctx->sliced[lines].id = VBI_SLICED_TELETEXT_B;
                ctx->sliced[lines].line = (line_offset > 0 ? (line_offset + (field_parity ? 0 : 313)) : 0);
                for (i = 0; i < 42; i++)
                    p[i] = vbi_rev8(buf[4 + i]);
                /* Unfortunately libzvbi does not expose page flags, and
                 * vbi_classify_page only checks MIP, so we have to manually
                 * decode the page flags and store the results. */
                pmag = vbi_unham16p(p);
                if (pmag >= 0 && pmag >> 3 == 0) {   // We found a row 0 header
                    int page = vbi_unham16p(p + 2);
                    int flags1 = vbi_unham16p(p + 6);
                    int flags2 = vbi_unham16p(p + 8);
                    if (page >= 0 && flags1 >= 0 && flags2 >= 0) {
                        int pgno = ((pmag & 7) << 8) + page;
                        // Check for disabled NEWSFLASH flag and enabled SUBTITLE and SUPRESS_HEADER flags
                        ctx->subtitle_map[pgno] = (!(flags1 & 0x40) && flags1 & 0x80 && flags2 & 0x01);
                        // Propagate ERASE_PAGE flag for repeated page headers to work around a libzvbi bug
                        if (ctx->subtitle_map[pgno] && pgno == ctx->last_pgno) {
                            int last_byte9 = vbi_unham8(ctx->last_p5);
                            if (last_byte9 >= 0 && last_byte9 & 0x8) {
                                int byte9 = vbi_unham8(p[5]);
                                if (byte9 >= 0)
                                    p[5] = vbi_ham8(byte9 | 0x8);
                            }
                        }
                        ctx->last_pgno = pgno;
                        ctx->last_p5 = p[5];
                    }
                }
                lines++;
            }
        }
        size -= data_unit_length + 2;
        buf += data_unit_length + 2;
    }
    if (size)
        av_log(ctx, AV_LOG_WARNING, "%d bytes remained after slicing data\n", size);
    return lines;
}

static int teletext_decode_frame(AVCodecContext *avctx, void *data, int *got_sub_ptr, AVPacket *pkt)
{
    TeletextContext *ctx = avctx->priv_data;
    AVSubtitle      *sub = data;
    int             ret = 0;
    int j;

    if (!ctx->vbi) {
        if (!(ctx->vbi = vbi_decoder_new()))
            return AVERROR(ENOMEM);
        if (ctx->default_region != -1) {
            av_log(avctx, AV_LOG_INFO, "Setting default zvbi region to %i\n", ctx->default_region);
            vbi_teletext_set_default_region(ctx->vbi, ctx->default_region);
        }
        if (!vbi_event_handler_register(ctx->vbi, VBI_EVENT_TTX_PAGE, handler, ctx)) {
            vbi_decoder_delete(ctx->vbi);
            ctx->vbi = NULL;
            return AVERROR(ENOMEM);
        }
    }

    if (avctx->pkt_timebase.num && pkt->pts != AV_NOPTS_VALUE)
        ctx->pts = av_rescale_q(pkt->pts, avctx->pkt_timebase, AV_TIME_BASE_Q);

    if (pkt->size) {
        int lines;
        const int full_pes_size = pkt->size + 45; /* PES header is 45 bytes */

        // We allow unreasonably big packets, even if the standard only allows a max size of 1472
        if (full_pes_size < 184 || full_pes_size > 65504 || full_pes_size % 184 != 0)
            return AVERROR_INVALIDDATA;

        ctx->handler_ret = pkt->size;

        if (ff_data_identifier_is_teletext(*pkt->data)) {
            if ((lines = slice_to_vbi_lines(ctx, pkt->data + 1, pkt->size - 1)) < 0)
                return lines;
            ff_dlog(avctx, "ctx=%p buf_size=%d lines=%u pkt_pts=%7.3f\n",
                    ctx, pkt->size, lines, (double)pkt->pts/90000.0);
            if (lines > 0) {
                vbi_decode(ctx->vbi, ctx->sliced, lines, 0.0);
                ctx->lines_processed += lines;
            }
        }
        ctx->pts = AV_NOPTS_VALUE;
        ret = ctx->handler_ret;
    }

    if (ret < 0)
        return ret;

    // is there a subtitle to pass?
    if (ctx->nb_pages) {
        int i;
        sub->format = !!ctx->format_id;
        sub->start_display_time = 0;
        sub->end_display_time = ctx->sub_duration;
        sub->num_rects = 0;
        sub->pts = ctx->pages->pts;

        if (ctx->pages->sub_rect->type != SUBTITLE_NONE) {
            sub->rects = av_malloc(sizeof(*sub->rects));
            if (sub->rects) {
                sub->num_rects = 1;
                sub->rects[0] = ctx->pages->sub_rect;
#if FF_API_AVPICTURE
FF_DISABLE_DEPRECATION_WARNINGS
                for (j = 0; j < 4; j++) {
                    sub->rects[0]->pict.data[j] = sub->rects[0]->data[j];
                    sub->rects[0]->pict.linesize[j] = sub->rects[0]->linesize[j];
                }
FF_ENABLE_DEPRECATION_WARNINGS
#endif
            } else {
                ret = AVERROR(ENOMEM);
            }
        } else {
            av_log(avctx, AV_LOG_DEBUG, "sending empty sub\n");
            sub->rects = NULL;
        }
        if (!sub->rects) // no rect was passed
            subtitle_rect_free(&ctx->pages->sub_rect);

        for (i = 0; i < ctx->nb_pages - 1; i++)
            ctx->pages[i] = ctx->pages[i + 1];
        ctx->nb_pages--;

        if (ret >= 0)
            *got_sub_ptr = 1;
    } else
        *got_sub_ptr = 0;

    return ret;
}

static int teletext_init_decoder(AVCodecContext *avctx)
{
    TeletextContext *ctx = avctx->priv_data;
    unsigned int maj, min, rev;

    vbi_version(&maj, &min, &rev);
    if (!(maj > 0 || min > 2 || min == 2 && rev >= 26)) {
        av_log(avctx, AV_LOG_ERROR, "decoder needs zvbi version >= 0.2.26.\n");
        return AVERROR_EXTERNAL;
    }

    if (ctx->format_id == 0) {
        avctx->width  = 41 * BITMAP_CHAR_WIDTH;
        avctx->height = 25 * BITMAP_CHAR_HEIGHT;
    }

    ctx->vbi = NULL;
    ctx->pts = AV_NOPTS_VALUE;
    ctx->last_pgno = -1;
    ctx->last_ass_alignment = 2;

    if (ctx->opacity == -1)
        ctx->opacity = ctx->transparent_bg ? 0 : 255;

    av_log(avctx, AV_LOG_VERBOSE, "page filter: %s\n", ctx->pgno);

    switch (ctx->format_id) {
        case 0:
            return 0;
        case 1:
            return ff_ass_subtitle_header_default(avctx);
        case 2:
            return my_ass_subtitle_header(avctx);
    }
    return AVERROR_BUG;
}

static int teletext_close_decoder(AVCodecContext *avctx)
{
    TeletextContext *ctx = avctx->priv_data;

    ff_dlog(avctx, "lines_total=%u\n", ctx->lines_processed);
    while (ctx->nb_pages)
        subtitle_rect_free(&ctx->pages[--ctx->nb_pages].sub_rect);
    av_freep(&ctx->pages);

    vbi_decoder_delete(ctx->vbi);
    ctx->vbi = NULL;
    ctx->pts = AV_NOPTS_VALUE;
    ctx->last_pgno = -1;
    ctx->last_ass_alignment = 2;
    memset(ctx->subtitle_map, 0, sizeof(ctx->subtitle_map));
    if (!(avctx->flags2 & AV_CODEC_FLAG2_RO_FLUSH_NOOP))
        ctx->readorder = 0;
    return 0;
}

static void teletext_flush(AVCodecContext *avctx)
{
    teletext_close_decoder(avctx);
}

#define OFFSET(x) offsetof(TeletextContext, x)
#define SD AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    {"txt_page",        "page numbers to decode, subtitle for subtitles, * for all", OFFSET(pgno),   AV_OPT_TYPE_STRING, {.str = "*"},      0, 0,        SD},
    {"txt_default_region", "default G0 character set used for decoding",     OFFSET(default_region), AV_OPT_TYPE_INT,    {.i64 = -1},      -1, 87,       SD},
    {"txt_chop_top",    "discards the top teletext line",                    OFFSET(chop_top),       AV_OPT_TYPE_INT,    {.i64 = 1},        0, 1,        SD},
    {"txt_format",      "format of the subtitles (bitmap or text or ass)",   OFFSET(format_id),      AV_OPT_TYPE_INT,    {.i64 = 0},        0, 2,        SD,  "txt_format"},
    {"bitmap",          NULL,                                                0,                      AV_OPT_TYPE_CONST,  {.i64 = 0},        0, 0,        SD,  "txt_format"},
    {"text",            NULL,                                                0,                      AV_OPT_TYPE_CONST,  {.i64 = 1},        0, 0,        SD,  "txt_format"},
    {"ass",             NULL,                                                0,                      AV_OPT_TYPE_CONST,  {.i64 = 2},        0, 0,        SD,  "txt_format"},
    {"txt_left",        "x offset of generated bitmaps",                     OFFSET(x_offset),       AV_OPT_TYPE_INT,    {.i64 = 0},        0, 65535,    SD},
    {"txt_top",         "y offset of generated bitmaps",                     OFFSET(y_offset),       AV_OPT_TYPE_INT,    {.i64 = 0},        0, 65535,    SD},
    {"txt_chop_spaces", "chops leading and trailing spaces from text",       OFFSET(chop_spaces),    AV_OPT_TYPE_INT,    {.i64 = 1},        0, 1,        SD},
    {"txt_duration",    "display duration of teletext pages in msecs",       OFFSET(sub_duration),   AV_OPT_TYPE_INT,    {.i64 = -1},      -1, 86400000, SD},
    {"txt_transparent", "force transparent background of the teletext",      OFFSET(transparent_bg), AV_OPT_TYPE_INT,    {.i64 = 0},        0, 1,        SD},
    {"txt_opacity",     "set opacity of the transparent background",         OFFSET(opacity),        AV_OPT_TYPE_INT,    {.i64 = -1},      -1, 255,      SD},
    { NULL },
};

static const AVClass teletext_class = {
    .class_name = "libzvbi_teletextdec",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_libzvbi_teletext_decoder = {
    .name      = "libzvbi_teletextdec",
    .long_name = NULL_IF_CONFIG_SMALL("Libzvbi DVB teletext decoder"),
    .type      = AVMEDIA_TYPE_SUBTITLE,
    .id        = AV_CODEC_ID_DVB_TELETEXT,
    .priv_data_size = sizeof(TeletextContext),
    .init      = teletext_init_decoder,
    .close     = teletext_close_decoder,
    .decode    = teletext_decode_frame,
    .capabilities = AV_CODEC_CAP_DELAY,
    .flush     = teletext_flush,
    .priv_class= &teletext_class,
    .wrapper_name = "libzvbi",
};
