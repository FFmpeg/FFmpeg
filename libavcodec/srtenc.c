/*
 * SubRip subtitle encoder
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

#include <stdarg.h>
#include "avcodec.h"
#include "libavutil/avstring.h"
#include "ass_split.h"
#include "ass.h"


#define SRT_STACK_SIZE 64

typedef struct {
    AVCodecContext *avctx;
    ASSSplitContext *ass_ctx;
    char buffer[2048];
    char *ptr;
    char *end;
    char *dialog_start;
    int count;
    char stack[SRT_STACK_SIZE];
    int stack_ptr;
    int alignment_applied;
} SRTContext;


#ifdef __GNUC__
__attribute__ ((__format__ (__printf__, 2, 3)))
#endif
static void srt_print(SRTContext *s, const char *str, ...)
{
    va_list vargs;
    va_start(vargs, str);
    s->ptr += vsnprintf(s->ptr, s->end - s->ptr, str, vargs);
    va_end(vargs);
}

static int srt_stack_push(SRTContext *s, const char c)
{
    if (s->stack_ptr >= SRT_STACK_SIZE)
        return -1;
    s->stack[s->stack_ptr++] = c;
    return 0;
}

static char srt_stack_pop(SRTContext *s)
{
    if (s->stack_ptr <= 0)
        return 0;
    return s->stack[--s->stack_ptr];
}

static int srt_stack_find(SRTContext *s, const char c)
{
    int i;
    for (i = s->stack_ptr-1; i >= 0; i--)
        if (s->stack[i] == c)
            break;
    return i;
}

static void srt_close_tag(SRTContext *s, char tag)
{
    srt_print(s, "</%c%s>", tag, tag == 'f' ? "ont" : "");
}

static void srt_stack_push_pop(SRTContext *s, const char c, int close)
{
    if (close) {
        int i = c ? srt_stack_find(s, c) : 0;
        if (i < 0)
            return;
        while (s->stack_ptr != i)
            srt_close_tag(s, srt_stack_pop(s));
    } else if (srt_stack_push(s, c) < 0)
        av_log(s->avctx, AV_LOG_ERROR, "tag stack overflow\n");
}

static void srt_style_apply(SRTContext *s, const char *style)
{
    ASSStyle *st = ass_style_get(s->ass_ctx, style);
    if (st) {
        int c = st->primary_color & 0xFFFFFF;
        if (st->font_name && strcmp(st->font_name, ASS_DEFAULT_FONT) ||
            st->font_size != ASS_DEFAULT_FONT_SIZE ||
            c != ASS_DEFAULT_COLOR) {
            srt_print(s, "<font");
            if (st->font_name && strcmp(st->font_name, ASS_DEFAULT_FONT))
                srt_print(s, " face=\"%s\"", st->font_name);
            if (st->font_size != ASS_DEFAULT_FONT_SIZE)
                srt_print(s, " size=\"%d\"", st->font_size);
            if (c != ASS_DEFAULT_COLOR)
                srt_print(s, " color=\"#%06x\"",
                          (c & 0xFF0000) >> 16 | c & 0xFF00 | (c & 0xFF) << 16);
            srt_print(s, ">");
            srt_stack_push(s, 'f');
        }
        if (st->bold != ASS_DEFAULT_BOLD) {
            srt_print(s, "<b>");
            srt_stack_push(s, 'b');
        }
        if (st->italic != ASS_DEFAULT_ITALIC) {
            srt_print(s, "<i>");
            srt_stack_push(s, 'i');
        }
        if (st->underline != ASS_DEFAULT_UNDERLINE) {
            srt_print(s, "<u>");
            srt_stack_push(s, 'u');
        }
        if (st->alignment != ASS_DEFAULT_ALIGNMENT) {
            srt_print(s, "{\\an%d}", st->alignment);
            s->alignment_applied = 1;
        }
    }
}


static av_cold int srt_encode_init(AVCodecContext *avctx)
{
    SRTContext *s = avctx->priv_data;
    s->avctx = avctx;
    s->ass_ctx = ff_ass_split(avctx->subtitle_header);
    return s->ass_ctx ? 0 : AVERROR_INVALIDDATA;
}

static void srt_text_cb(void *priv, const char *text, int len)
{
    SRTContext *s = priv;
    av_strlcpy(s->ptr, text, FFMIN(s->end-s->ptr, len+1));
    s->ptr += len;
}

static void srt_new_line_cb(void *priv, int forced)
{
    srt_print(priv, "\r\n");
}

static void srt_style_cb(void *priv, char style, int close)
{
    srt_stack_push_pop(priv, style, close);
    if (!close)
        srt_print(priv, "<%c>", style);
}

static void srt_color_cb(void *priv, unsigned int color, unsigned int color_id)
{
    if (color_id > 1)
        return;
    srt_stack_push_pop(priv, 'f', color == 0xFFFFFFFF);
    if (color != 0xFFFFFFFF)
        srt_print(priv, "<font color=\"#%06x\">",
              (color & 0xFF0000) >> 16 | color & 0xFF00 | (color & 0xFF) << 16);
}

static void srt_font_name_cb(void *priv, const char *name)
{
    srt_stack_push_pop(priv, 'f', !name);
    if (name)
        srt_print(priv, "<font face=\"%s\">", name);
}

static void srt_font_size_cb(void *priv, int size)
{
    srt_stack_push_pop(priv, 'f', size < 0);
    if (size >= 0)
        srt_print(priv, "<font size=\"%d\">", size);
}

static void srt_alignment_cb(void *priv, int alignment)
{
    SRTContext *s = priv;
    if (!s->alignment_applied && alignment >= 0) {
        srt_print(s, "{\\an%d}", alignment);
        s->alignment_applied = 1;
    }
}

static void srt_cancel_overrides_cb(void *priv, const char *style)
{
    srt_stack_push_pop(priv, 0, 1);
    srt_style_apply(priv, style);
}

static void srt_move_cb(void *priv, int x1, int y1, int x2, int y2,
                        int t1, int t2)
{
    SRTContext *s = priv;
    char buffer[32];
    int len = snprintf(buffer, sizeof(buffer),
                       "  X1:%03u X2:%03u Y1:%03u Y2:%03u", x1, x2, y1, y2);
    if (s->end - s->ptr > len) {
        memmove(s->dialog_start+len, s->dialog_start, s->ptr-s->dialog_start+1);
        memcpy(s->dialog_start, buffer, len);
        s->ptr += len;
    }
}

static void srt_end_cb(void *priv)
{
    srt_stack_push_pop(priv, 0, 1);
    srt_print(priv, "\r\n\r\n");
}

static const ASSCodesCallbacks srt_callbacks = {
    .text             = srt_text_cb,
    .new_line         = srt_new_line_cb,
    .style            = srt_style_cb,
    .color            = srt_color_cb,
    .font_name        = srt_font_name_cb,
    .font_size        = srt_font_size_cb,
    .alignment        = srt_alignment_cb,
    .cancel_overrides = srt_cancel_overrides_cb,
    .move             = srt_move_cb,
    .end              = srt_end_cb,
};

static int srt_encode_frame(AVCodecContext *avctx,
                            unsigned char *buf, int bufsize, void *data)
{
    SRTContext *s = avctx->priv_data;
    AVSubtitle *sub = data;
    ASSDialog *dialog;
    int i, len, num;

    s->ptr = s->buffer;
    s->end = s->ptr + sizeof(s->buffer);

    for (i=0; i<sub->num_rects; i++) {

        if (sub->rects[i]->type != SUBTITLE_ASS) {
            av_log(avctx, AV_LOG_ERROR, "Only SUBTITLE_ASS type supported.\n");
            return AVERROR(ENOSYS);
        }

        dialog = ff_ass_split_dialog(s->ass_ctx, sub->rects[i]->ass, 0, &num);
        for (; dialog && num--; dialog++) {
            int sh, sm, ss, sc = 10 * dialog->start;
            int eh, em, es, ec = 10 * dialog->end;
            sh = sc/3600000;  sc -= 3600000*sh;
            sm = sc/  60000;  sc -=   60000*sm;
            ss = sc/   1000;  sc -=    1000*ss;
            eh = ec/3600000;  ec -= 3600000*eh;
            em = ec/  60000;  ec -=   60000*em;
            es = ec/   1000;  ec -=    1000*es;
            srt_print(s,"%d\r\n%02d:%02d:%02d,%03d --> %02d:%02d:%02d,%03d\r\n",
                      ++s->count, sh, sm, ss, sc, eh, em, es, ec);
            s->alignment_applied = 0;
            s->dialog_start = s->ptr - 2;
            srt_style_apply(s, dialog->style);
            ff_ass_split_override_codes(&srt_callbacks, s, dialog->text);
        }
    }

    if (s->ptr == s->buffer)
        return 0;

    len = av_strlcpy(buf, s->buffer, bufsize);

    if (len > bufsize-1) {
        av_log(avctx, AV_LOG_ERROR, "Buffer too small for ASS event.\n");
        return -1;
    }

    return len;
}

static int srt_encode_close(AVCodecContext *avctx)
{
    SRTContext *s = avctx->priv_data;
    ff_ass_split_free(s->ass_ctx);
    return 0;
}

AVCodec ff_srt_encoder = {
    .name           = "srt",
    .long_name      = NULL_IF_CONFIG_SMALL("SubRip subtitle"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = CODEC_ID_SRT,
    .priv_data_size = sizeof(SRTContext),
    .init           = srt_encode_init,
    .encode         = srt_encode_frame,
    .close          = srt_encode_close,
};
