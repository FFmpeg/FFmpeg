/*
 * ASCII/ANSI art decoder
 * Copyright (c) 2010 Peter Ross <pross@xvid.org>
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
 * ASCII/ANSI art decoder
 */

#include "libavutil/common.h"
#include "libavutil/frame.h"
#include "libavutil/lfg.h"
#include "libavutil/xga_font_data.h"
#include "avcodec.h"
#include "cga_data.h"
#include "internal.h"

#define ATTR_BOLD         0x01  /**< Bold/Bright-foreground (mode 1) */
#define ATTR_FAINT        0x02  /**< Faint (mode 2) */
#define ATTR_UNDERLINE    0x08  /**< Underline (mode 4) */
#define ATTR_BLINK        0x10  /**< Blink/Bright-background (mode 5) */
#define ATTR_REVERSE      0x40  /**< Reverse (mode 7) */
#define ATTR_CONCEALED    0x80  /**< Concealed (mode 8) */

#define DEFAULT_FG_COLOR     7  /**< CGA color index */
#define DEFAULT_BG_COLOR     0
#define DEFAULT_SCREEN_MODE  3  /**< 80x25 */

#define FONT_WIDTH           8  /**< Font width */

/** map ansi color index to cga palette index */
static const uint8_t ansi_to_cga[16] = {
    0,  4,  2,  6,  1,  5,  3, 7, 8, 12, 10, 14,  9, 13, 11, 15
};

typedef struct AnsiContext {
    AVFrame *frame;
    int x;                /**< x cursor position (pixels) */
    int y;                /**< y cursor position (pixels) */
    int sx;               /**< saved x cursor position (pixels) */
    int sy;               /**< saved y cursor position (pixels) */
    const uint8_t* font;  /**< font */
    int font_height;      /**< font height */
    int attributes;       /**< attribute flags */
    int fg;               /**< foreground color */
    int bg;               /**< background color */
    int first_frame;

    /* ansi parser state machine */
    enum {
        STATE_NORMAL = 0,
        STATE_ESCAPE,
        STATE_CODE,
        STATE_MUSIC_PREAMBLE
    } state;
#define MAX_NB_ARGS 4
    int args[MAX_NB_ARGS];
    int nb_args;          /**< number of arguments (may exceed MAX_NB_ARGS) */
} AnsiContext;

static av_cold int decode_init(AVCodecContext *avctx)
{
    AnsiContext *s = avctx->priv_data;
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);

    /* defaults */
    s->font        = avpriv_vga16_font;
    s->font_height = 16;
    s->fg          = DEFAULT_FG_COLOR;
    s->bg          = DEFAULT_BG_COLOR;

    if (!avctx->width || !avctx->height) {
        int ret = ff_set_dimensions(avctx, 80 << 3, 25 << 4);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static void set_palette(uint32_t *pal)
{
    int r, g, b;
    memcpy(pal, ff_cga_palette, 16 * 4);
    pal += 16;
#define COLOR(x) ((x) * 40 + 55)
    for (r = 0; r < 6; r++)
        for (g = 0; g < 6; g++)
            for (b = 0; b < 6; b++)
                *pal++ = 0xFF000000 | (COLOR(r) << 16) | (COLOR(g) << 8) | COLOR(b);
#define GRAY(x) ((x) * 10 + 8)
    for (g = 0; g < 24; g++)
        *pal++ = 0xFF000000 | (GRAY(g) << 16) | (GRAY(g) << 8) | GRAY(g);
}

static void hscroll(AVCodecContext *avctx)
{
    AnsiContext *s = avctx->priv_data;
    int i;

    if (s->y <= avctx->height - 2*s->font_height) {
        s->y += s->font_height;
        return;
    }

    i = 0;
    for (; i < avctx->height - s->font_height; i++)
        memcpy(s->frame->data[0] + i * s->frame->linesize[0],
               s->frame->data[0] + (i + s->font_height) * s->frame->linesize[0],
               avctx->width);
    for (; i < avctx->height; i++)
        memset(s->frame->data[0] + i * s->frame->linesize[0],
            DEFAULT_BG_COLOR, avctx->width);
}

static void erase_line(AVCodecContext * avctx, int xoffset, int xlength)
{
    AnsiContext *s = avctx->priv_data;
    int i;
    for (i = 0; i < s->font_height; i++)
        memset(s->frame->data[0] + (s->y + i)*s->frame->linesize[0] + xoffset,
            DEFAULT_BG_COLOR, xlength);
}

static void erase_screen(AVCodecContext *avctx)
{
    AnsiContext *s = avctx->priv_data;
    int i;
    for (i = 0; i < avctx->height; i++)
        memset(s->frame->data[0] + i * s->frame->linesize[0], DEFAULT_BG_COLOR, avctx->width);
    s->x = s->y = 0;
}

/**
 * Draw character to screen
 */
static void draw_char(AVCodecContext *avctx, int c)
{
    AnsiContext *s = avctx->priv_data;
    int fg = s->fg;
    int bg = s->bg;

    if ((s->attributes & ATTR_BOLD))
        fg += 8;
    if ((s->attributes & ATTR_BLINK))
        bg += 8;
    if ((s->attributes & ATTR_REVERSE))
        FFSWAP(int, fg, bg);
    if ((s->attributes & ATTR_CONCEALED))
        fg = bg;
    ff_draw_pc_font(s->frame->data[0] + s->y * s->frame->linesize[0] + s->x,
                    s->frame->linesize[0], s->font, s->font_height, c, fg, bg);
    s->x += FONT_WIDTH;
    if (s->x > avctx->width - FONT_WIDTH) {
        s->x = 0;
        hscroll(avctx);
    }
}

/**
 * Execute ANSI escape code
 * @return 0 on success, negative on error
 */
static int execute_code(AVCodecContext * avctx, int c)
{
    AnsiContext *s = avctx->priv_data;
    int ret, i;
    int width  = avctx->width;
    int height = avctx->height;

    switch(c) {
    case 'A': //Cursor Up
        s->y = FFMAX(s->y - (s->nb_args > 0 ? s->args[0]*s->font_height : s->font_height), 0);
        break;
    case 'B': //Cursor Down
        s->y = FFMIN(s->y + (s->nb_args > 0 ? s->args[0]*s->font_height : s->font_height), avctx->height - s->font_height);
        break;
    case 'C': //Cursor Right
        s->x = FFMIN(s->x + (s->nb_args > 0 ? s->args[0]*FONT_WIDTH : FONT_WIDTH), avctx->width  - FONT_WIDTH);
        break;
    case 'D': //Cursor Left
        s->x = FFMAX(s->x - (s->nb_args > 0 ? s->args[0]*FONT_WIDTH : FONT_WIDTH), 0);
        break;
    case 'H': //Cursor Position
    case 'f': //Horizontal and Vertical Position
        s->y = s->nb_args > 0 ? av_clip((s->args[0] - 1)*s->font_height, 0, avctx->height - s->font_height) : 0;
        s->x = s->nb_args > 1 ? av_clip((s->args[1] - 1)*FONT_WIDTH,     0, avctx->width  - FONT_WIDTH) : 0;
        break;
    case 'h': //set creen mode
    case 'l': //reset screen mode
        if (s->nb_args < 2)
            s->args[0] = DEFAULT_SCREEN_MODE;
        switch(s->args[0]) {
        case 0: case 1: case 4: case 5: case 13: case 19: //320x200 (25 rows)
            s->font = avpriv_cga_font;
            s->font_height = 8;
            width  = 40<<3;
            height = 25<<3;
            break;
        case 2: case 3: //640x400 (25 rows)
            s->font = avpriv_vga16_font;
            s->font_height = 16;
            width  = 80<<3;
            height = 25<<4;
            break;
        case 6: case 14: //640x200 (25 rows)
            s->font = avpriv_cga_font;
            s->font_height = 8;
            width  = 80<<3;
            height = 25<<3;
            break;
        case 7: //set line wrapping
            break;
        case 15: case 16: //640x350 (43 rows)
            s->font = avpriv_cga_font;
            s->font_height = 8;
            width  = 80<<3;
            height = 43<<3;
            break;
        case 17: case 18: //640x480 (60 rows)
            s->font = avpriv_cga_font;
            s->font_height = 8;
            width  = 80<<3;
            height = 60<<4;
            break;
        default:
            avpriv_request_sample(avctx, "Unsupported screen mode");
        }
        s->x = av_clip(s->x, 0, width  - FONT_WIDTH);
        s->y = av_clip(s->y, 0, height - s->font_height);
        if (width != avctx->width || height != avctx->height) {
            av_frame_unref(s->frame);
            ret = ff_set_dimensions(avctx, width, height);
            if (ret < 0)
                return ret;
            if ((ret = ff_get_buffer(avctx, s->frame,
                                     AV_GET_BUFFER_FLAG_REF)) < 0)
                return ret;
            s->frame->pict_type           = AV_PICTURE_TYPE_I;
            s->frame->palette_has_changed = 1;
            set_palette((uint32_t *)s->frame->data[1]);
            erase_screen(avctx);
        } else if (c == 'l') {
            erase_screen(avctx);
        }
        break;
    case 'J': //Erase in Page
        switch (s->args[0]) {
        case 0:
            erase_line(avctx, s->x, avctx->width - s->x);
            if (s->y < avctx->height - s->font_height)
                memset(s->frame->data[0] + (s->y + s->font_height)*s->frame->linesize[0],
                    DEFAULT_BG_COLOR, (avctx->height - s->y - s->font_height)*s->frame->linesize[0]);
            break;
        case 1:
            erase_line(avctx, 0, s->x);
            if (s->y > 0)
                memset(s->frame->data[0], DEFAULT_BG_COLOR, s->y * s->frame->linesize[0]);
            break;
        case 2:
            erase_screen(avctx);
        }
        break;
    case 'K': //Erase in Line
        switch(s->args[0]) {
        case 0:
            erase_line(avctx, s->x, avctx->width - s->x);
            break;
        case 1:
            erase_line(avctx, 0, s->x);
            break;
        case 2:
            erase_line(avctx, 0, avctx->width);
        }
        break;
    case 'm': //Select Graphics Rendition
        if (s->nb_args == 0) {
            s->nb_args = 1;
            s->args[0] = 0;
        }
        for (i = 0; i < FFMIN(s->nb_args, MAX_NB_ARGS); i++) {
            int m = s->args[i];
            if (m == 0) {
                s->attributes = 0;
                s->fg = DEFAULT_FG_COLOR;
                s->bg = DEFAULT_BG_COLOR;
            } else if (m == 1 || m == 2 || m == 4 || m == 5 || m == 7 || m == 8) {
                s->attributes |= 1 << (m - 1);
            } else if (m >= 30 && m <= 37) {
                s->fg = ansi_to_cga[m - 30];
            } else if (m == 38 && i + 2 < FFMIN(s->nb_args, MAX_NB_ARGS) && s->args[i + 1] == 5 && s->args[i + 2] < 256) {
                int index = s->args[i + 2];
                s->fg = index < 16 ? ansi_to_cga[index] : index;
                i += 2;
            } else if (m == 39) {
                s->fg = ansi_to_cga[DEFAULT_FG_COLOR];
            } else if (m >= 40 && m <= 47) {
                s->bg = ansi_to_cga[m - 40];
            } else if (m == 48 && i + 2 < FFMIN(s->nb_args, MAX_NB_ARGS) && s->args[i + 1] == 5 && s->args[i + 2] < 256) {
                int index = s->args[i + 2];
                s->bg = index < 16 ? ansi_to_cga[index] : index;
                i += 2;
            } else if (m == 49) {
                s->fg = ansi_to_cga[DEFAULT_BG_COLOR];
            } else {
                avpriv_request_sample(avctx, "Unsupported rendition parameter");
            }
        }
        break;
    case 'n': //Device Status Report
    case 'R': //report current line and column
        /* ignore */
        break;
    case 's': //Save Cursor Position
        s->sx = s->x;
        s->sy = s->y;
        break;
    case 'u': //Restore Cursor Position
        s->x = av_clip(s->sx, 0, avctx->width  - FONT_WIDTH);
        s->y = av_clip(s->sy, 0, avctx->height - s->font_height);
        break;
    default:
        avpriv_request_sample(avctx, "Unknown escape code");
        break;
    }
    s->x = av_clip(s->x, 0, avctx->width  - FONT_WIDTH);
    s->y = av_clip(s->y, 0, avctx->height - s->font_height);
    return 0;
}

static int decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame,
                            AVPacket *avpkt)
{
    AnsiContext *s = avctx->priv_data;
    uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    const uint8_t *buf_end   = buf+buf_size;
    int ret, i, count;

    if ((ret = ff_reget_buffer(avctx, s->frame)) < 0)
        return ret;
    if (!avctx->frame_number) {
        for (i=0; i<avctx->height; i++)
            memset(s->frame->data[0]+ i*s->frame->linesize[0], 0, avctx->width);
        memset(s->frame->data[1], 0, AVPALETTE_SIZE);
    }

    s->frame->pict_type           = AV_PICTURE_TYPE_I;
    s->frame->palette_has_changed = 1;
    set_palette((uint32_t *)s->frame->data[1]);
    if (!s->first_frame) {
        erase_screen(avctx);
        s->first_frame = 1;
    }

    while(buf < buf_end) {
        switch(s->state) {
        case STATE_NORMAL:
            switch (buf[0]) {
            case 0x00: //NUL
            case 0x07: //BEL
            case 0x1A: //SUB
                /* ignore */
                break;
            case 0x08: //BS
                s->x = FFMAX(s->x - 1, 0);
                break;
            case 0x09: //HT
                i = s->x / FONT_WIDTH;
                count = ((i + 8) & ~7) - i;
                for (i = 0; i < count; i++)
                    draw_char(avctx, ' ');
                break;
            case 0x0A: //LF
                hscroll(avctx);
            case 0x0D: //CR
                s->x = 0;
                break;
            case 0x0C: //FF
                erase_screen(avctx);
                break;
            case 0x1B: //ESC
                s->state = STATE_ESCAPE;
                break;
            default:
                draw_char(avctx, buf[0]);
            }
            break;
        case STATE_ESCAPE:
            if (buf[0] == '[') {
                s->state   = STATE_CODE;
                s->nb_args = 0;
                s->args[0] = -1;
            } else {
                s->state = STATE_NORMAL;
                draw_char(avctx, 0x1B);
                continue;
            }
            break;
        case STATE_CODE:
            switch(buf[0]) {
            case '0': case '1': case '2': case '3': case '4':
            case '5': case '6': case '7': case '8': case '9':
                if (s->nb_args < MAX_NB_ARGS && s->args[s->nb_args] < 6553)
                    s->args[s->nb_args] = FFMAX(s->args[s->nb_args], 0) * 10 + buf[0] - '0';
                break;
            case ';':
                s->nb_args++;
                if (s->nb_args < MAX_NB_ARGS)
                    s->args[s->nb_args] = 0;
                break;
            case 'M':
                s->state = STATE_MUSIC_PREAMBLE;
                break;
            case '=': case '?':
                /* ignore */
                break;
            default:
                if (s->nb_args > MAX_NB_ARGS)
                    av_log(avctx, AV_LOG_WARNING, "args overflow (%i)\n", s->nb_args);
                if (s->nb_args < MAX_NB_ARGS && s->args[s->nb_args] >= 0)
                    s->nb_args++;
                if ((ret = execute_code(avctx, buf[0])) < 0)
                    return ret;
                s->state = STATE_NORMAL;
            }
            break;
        case STATE_MUSIC_PREAMBLE:
            if (buf[0] == 0x0E || buf[0] == 0x1B)
                s->state = STATE_NORMAL;
            /* ignore music data */
            break;
        }
        buf++;
    }

    *got_frame = 1;
    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;
    return buf_size;
}

static av_cold int decode_close(AVCodecContext *avctx)
{
    AnsiContext *s = avctx->priv_data;

    av_frame_free(&s->frame);
    return 0;
}

AVCodec ff_ansi_decoder = {
    .name           = "ansi",
    .long_name      = NULL_IF_CONFIG_SMALL("ASCII/ANSI art"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_ANSI,
    .priv_data_size = sizeof(AnsiContext),
    .init           = decode_init,
    .close          = decode_close,
    .decode         = decode_frame,
    .capabilities   = AV_CODEC_CAP_DR1,
};
