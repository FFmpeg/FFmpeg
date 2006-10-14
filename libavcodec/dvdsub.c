/*
 * DVD subtitle decoding for ffmpeg
 * Copyright (c) 2005 Fabrice Bellard.
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

//#define DEBUG

static int dvdsub_init_decoder(AVCodecContext *avctx)
{
    return 0;
}

static uint16_t getbe16(const uint8_t *p)
{
    return (p[0] << 8) | p[1];
}

static int get_nibble(const uint8_t *buf, int nibble_offset)
{
    return (buf[nibble_offset >> 1] >> ((1 - (nibble_offset & 1)) << 2)) & 0xf;
}

static int decode_rle(uint8_t *bitmap, int linesize, int w, int h,
                      const uint8_t *buf, int nibble_offset, int buf_size)
{
    unsigned int v;
    int x, y, len, color, nibble_end;
    uint8_t *d;

    nibble_end = buf_size * 2;
    x = 0;
    y = 0;
    d = bitmap;
    for(;;) {
        if (nibble_offset >= nibble_end)
            return -1;
        v = get_nibble(buf, nibble_offset++);
        if (v < 0x4) {
            v = (v << 4) | get_nibble(buf, nibble_offset++);
            if (v < 0x10) {
                v = (v << 4) | get_nibble(buf, nibble_offset++);
                if (v < 0x040) {
                    v = (v << 4) | get_nibble(buf, nibble_offset++);
                    if (v < 4) {
                        v |= (w - x) << 2;
                    }
                }
            }
        }
        len = v >> 2;
        if (len > (w - x))
            len = (w - x);
        color = v & 0x03;
        memset(d + x, color, len);
        x += len;
        if (x >= w) {
            y++;
            if (y >= h)
                break;
            d += linesize;
            x = 0;
            /* byte align */
            nibble_offset += (nibble_offset & 1);
        }
    }
    return 0;
}

static void guess_palette(uint32_t *rgba_palette,
                          uint8_t *palette,
                          uint8_t *alpha,
                          uint32_t subtitle_color)
{
    uint8_t color_used[16];
    int nb_opaque_colors, i, level, j, r, g, b;

    for(i = 0; i < 4; i++)
        rgba_palette[i] = 0;

    memset(color_used, 0, 16);
    nb_opaque_colors = 0;
    for(i = 0; i < 4; i++) {
        if (alpha[i] != 0 && !color_used[palette[i]]) {
            color_used[palette[i]] = 1;
            nb_opaque_colors++;
        }
    }

    if (nb_opaque_colors == 0)
        return;

    j = nb_opaque_colors;
    memset(color_used, 0, 16);
    for(i = 0; i < 4; i++) {
        if (alpha[i] != 0) {
            if (!color_used[palette[i]])  {
                level = (0xff * j) / nb_opaque_colors;
                r = (((subtitle_color >> 16) & 0xff) * level) >> 8;
                g = (((subtitle_color >> 8) & 0xff) * level) >> 8;
                b = (((subtitle_color >> 0) & 0xff) * level) >> 8;
                rgba_palette[i] = b | (g << 8) | (r << 16) | ((alpha[i] * 17) << 24);
                color_used[palette[i]] = (i + 1);
                j--;
            } else {
                rgba_palette[i] = (rgba_palette[color_used[palette[i]] - 1] & 0x00ffffff) |
                                    ((alpha[i] * 17) << 24);
            }
        }
    }
}

static int decode_dvd_subtitles(AVSubtitle *sub_header,
                                const uint8_t *buf, int buf_size)
{
    int cmd_pos, pos, cmd, x1, y1, x2, y2, offset1, offset2, next_cmd_pos;
    uint8_t palette[4], alpha[4];
    int date;
    int i;
    int is_menu = 0;

    if (buf_size < 4)
        return -1;
    sub_header->rects = NULL;
    sub_header->num_rects = 0;
    sub_header->start_display_time = 0;
    sub_header->end_display_time = 0;

    cmd_pos = getbe16(buf + 2);
    while ((cmd_pos + 4) < buf_size) {
        date = getbe16(buf + cmd_pos);
        next_cmd_pos = getbe16(buf + cmd_pos + 2);
#ifdef DEBUG
        av_log(NULL, AV_LOG_INFO, "cmd_pos=0x%04x next=0x%04x date=%d\n",
               cmd_pos, next_cmd_pos, date);
#endif
        pos = cmd_pos + 4;
        offset1 = -1;
        offset2 = -1;
        x1 = y1 = x2 = y2 = 0;
        while (pos < buf_size) {
            cmd = buf[pos++];
#ifdef DEBUG
            av_log(NULL, AV_LOG_INFO, "cmd=%02x\n", cmd);
#endif
            switch(cmd) {
            case 0x00:
                /* menu subpicture */
                is_menu = 1;
                break;
            case 0x01:
                /* set start date */
                sub_header->start_display_time = (date << 10) / 90;
                break;
            case 0x02:
                /* set end date */
                sub_header->end_display_time = (date << 10) / 90;
                break;
            case 0x03:
                /* set palette */
                if ((buf_size - pos) < 2)
                    goto fail;
                palette[3] = buf[pos] >> 4;
                palette[2] = buf[pos] & 0x0f;
                palette[1] = buf[pos + 1] >> 4;
                palette[0] = buf[pos + 1] & 0x0f;
                pos += 2;
                break;
            case 0x04:
                /* set alpha */
                if ((buf_size - pos) < 2)
                    goto fail;
                alpha[3] = buf[pos] >> 4;
                alpha[2] = buf[pos] & 0x0f;
                alpha[1] = buf[pos + 1] >> 4;
                alpha[0] = buf[pos + 1] & 0x0f;
                pos += 2;
#ifdef DEBUG
            av_log(NULL, AV_LOG_INFO, "alpha=%x%x%x%x\n", alpha[0],alpha[1],alpha[2],alpha[3]);
#endif
                break;
            case 0x05:
                if ((buf_size - pos) < 6)
                    goto fail;
                x1 = (buf[pos] << 4) | (buf[pos + 1] >> 4);
                x2 = ((buf[pos + 1] & 0x0f) << 8) | buf[pos + 2];
                y1 = (buf[pos + 3] << 4) | (buf[pos + 4] >> 4);
                y2 = ((buf[pos + 4] & 0x0f) << 8) | buf[pos + 5];
#ifdef DEBUG
                av_log(NULL, AV_LOG_INFO, "x1=%d x2=%d y1=%d y2=%d\n",
                       x1, x2, y1, y2);
#endif
                pos += 6;
                break;
            case 0x06:
                if ((buf_size - pos) < 4)
                    goto fail;
                offset1 = getbe16(buf + pos);
                offset2 = getbe16(buf + pos + 2);
#ifdef DEBUG
                av_log(NULL, AV_LOG_INFO, "offset1=0x%04x offset2=0x%04x\n", offset1, offset2);
#endif
                pos += 4;
                break;
            case 0xff:
            default:
                goto the_end;
            }
        }
    the_end:
        if (offset1 >= 0) {
            int w, h;
            uint8_t *bitmap;

            /* decode the bitmap */
            w = x2 - x1 + 1;
            if (w < 0)
                w = 0;
            h = y2 - y1;
            if (h < 0)
                h = 0;
            if (w > 0 && h > 0) {
                if (sub_header->rects != NULL) {
                    for (i = 0; i < sub_header->num_rects; i++) {
                        av_free(sub_header->rects[i].bitmap);
                        av_free(sub_header->rects[i].rgba_palette);
                    }
                    av_freep(&sub_header->rects);
                    sub_header->num_rects = 0;
                }

                bitmap = av_malloc(w * h);
                sub_header->rects = av_mallocz(sizeof(AVSubtitleRect));
                sub_header->num_rects = 1;
                sub_header->rects[0].rgba_palette = av_malloc(4 * 4);
                decode_rle(bitmap, w * 2, w, h / 2,
                           buf, offset1 * 2, buf_size);
                decode_rle(bitmap + w, w * 2, w, h / 2,
                           buf, offset2 * 2, buf_size);
                guess_palette(sub_header->rects[0].rgba_palette,
                              palette, alpha, 0xffff00);
                sub_header->rects[0].x = x1;
                sub_header->rects[0].y = y1;
                sub_header->rects[0].w = w;
                sub_header->rects[0].h = h;
                sub_header->rects[0].nb_colors = 4;
                sub_header->rects[0].linesize = w;
                sub_header->rects[0].bitmap = bitmap;
            }
        }
        if (next_cmd_pos == cmd_pos)
            break;
        cmd_pos = next_cmd_pos;
    }
    if (sub_header->num_rects > 0)
        return is_menu;
 fail:
    return -1;
}

static int is_transp(const uint8_t *buf, int pitch, int n,
                     const uint8_t *transp_color)
{
    int i;
    for(i = 0; i < n; i++) {
        if (!transp_color[*buf])
            return 0;
        buf += pitch;
    }
    return 1;
}

/* return 0 if empty rectangle, 1 if non empty */
static int find_smallest_bounding_rectangle(AVSubtitle *s)
{
    uint8_t transp_color[256];
    int y1, y2, x1, x2, y, w, h, i;
    uint8_t *bitmap;

    if (s->num_rects == 0 || s->rects == NULL || s->rects[0].w <= 0 || s->rects[0].h <= 0)
        return 0;

    memset(transp_color, 0, 256);
    for(i = 0; i < s->rects[0].nb_colors; i++) {
        if ((s->rects[0].rgba_palette[i] >> 24) == 0)
            transp_color[i] = 1;
    }
    y1 = 0;
    while (y1 < s->rects[0].h && is_transp(s->rects[0].bitmap + y1 * s->rects[0].linesize,
                                  1, s->rects[0].w, transp_color))
        y1++;
    if (y1 == s->rects[0].h) {
        av_freep(&s->rects[0].bitmap);
        s->rects[0].w = s->rects[0].h = 0;
        return 0;
    }

    y2 = s->rects[0].h - 1;
    while (y2 > 0 && is_transp(s->rects[0].bitmap + y2 * s->rects[0].linesize, 1,
                               s->rects[0].w, transp_color))
        y2--;
    x1 = 0;
    while (x1 < (s->rects[0].w - 1) && is_transp(s->rects[0].bitmap + x1, s->rects[0].linesize,
                                        s->rects[0].h, transp_color))
        x1++;
    x2 = s->rects[0].w - 1;
    while (x2 > 0 && is_transp(s->rects[0].bitmap + x2, s->rects[0].linesize, s->rects[0].h,
                                  transp_color))
        x2--;
    w = x2 - x1 + 1;
    h = y2 - y1 + 1;
    bitmap = av_malloc(w * h);
    if (!bitmap)
        return 1;
    for(y = 0; y < h; y++) {
        memcpy(bitmap + w * y, s->rects[0].bitmap + x1 + (y1 + y) * s->rects[0].linesize, w);
    }
    av_freep(&s->rects[0].bitmap);
    s->rects[0].bitmap = bitmap;
    s->rects[0].linesize = w;
    s->rects[0].w = w;
    s->rects[0].h = h;
    s->rects[0].x += x1;
    s->rects[0].y += y1;
    return 1;
}

static int dvdsub_close_decoder(AVCodecContext *avctx)
{
    return 0;
}

#ifdef DEBUG
#undef fprintf
static void ppm_save(const char *filename, uint8_t *bitmap, int w, int h,
                     uint32_t *rgba_palette)
{
    int x, y, v;
    FILE *f;

    f = fopen(filename, "w");
    if (!f) {
        perror(filename);
        exit(1);
    }
    fprintf(f, "P6\n"
            "%d %d\n"
            "%d\n",
            w, h, 255);
    for(y = 0; y < h; y++) {
        for(x = 0; x < w; x++) {
            v = rgba_palette[bitmap[y * w + x]];
            putc((v >> 16) & 0xff, f);
            putc((v >> 8) & 0xff, f);
            putc((v >> 0) & 0xff, f);
        }
    }
    fclose(f);
}
#endif

static int dvdsub_decode(AVCodecContext *avctx,
                         void *data, int *data_size,
                         uint8_t *buf, int buf_size)
{
    AVSubtitle *sub = (void *)data;
    int is_menu;

    is_menu = decode_dvd_subtitles(sub, buf, buf_size);

    if (is_menu < 0) {
    no_subtitle:
        *data_size = 0;

        return buf_size;
    }
    if (!is_menu && find_smallest_bounding_rectangle(sub) == 0)
        goto no_subtitle;

#if defined(DEBUG)
    av_log(NULL, AV_LOG_INFO, "start=%d ms end =%d ms\n",
           sub->start_display_time,
           sub->end_display_time);
    ppm_save("/tmp/a.ppm", sub->rects[0].bitmap,
             sub->rects[0].w, sub->rects[0].h, sub->rects[0].rgba_palette);
#endif

    *data_size = 1;
    return buf_size;
}

AVCodec dvdsub_decoder = {
    "dvdsub",
    CODEC_TYPE_SUBTITLE,
    CODEC_ID_DVD_SUBTITLE,
    0,
    dvdsub_init_decoder,
    NULL,
    dvdsub_close_decoder,
    dvdsub_decode,
};

/* parser definition */
typedef struct DVDSubParseContext {
    uint8_t *packet;
    int packet_len;
    int packet_index;
} DVDSubParseContext;

static int dvdsub_parse_init(AVCodecParserContext *s)
{
    return 0;
}

static int dvdsub_parse(AVCodecParserContext *s,
                        AVCodecContext *avctx,
                        uint8_t **poutbuf, int *poutbuf_size,
                        const uint8_t *buf, int buf_size)
{
    DVDSubParseContext *pc = s->priv_data;

    if (pc->packet_index == 0) {
        if (buf_size < 2)
            return 0;
        pc->packet_len = (buf[0] << 8) | buf[1];
        av_freep(&pc->packet);
        pc->packet = av_malloc(pc->packet_len);
    }
    if (pc->packet) {
        if (pc->packet_index + buf_size <= pc->packet_len) {
            memcpy(pc->packet + pc->packet_index, buf, buf_size);
            pc->packet_index += buf_size;
            if (pc->packet_index >= pc->packet_len) {
                *poutbuf = pc->packet;
                *poutbuf_size = pc->packet_len;
                pc->packet_index = 0;
                return buf_size;
            }
        } else {
            /* erroneous size */
            pc->packet_index = 0;
        }
    }
    *poutbuf = NULL;
    *poutbuf_size = 0;
    return buf_size;
}

static void dvdsub_parse_close(AVCodecParserContext *s)
{
    DVDSubParseContext *pc = s->priv_data;
    av_freep(&pc->packet);
}

AVCodecParser dvdsub_parser = {
    { CODEC_ID_DVD_SUBTITLE },
    sizeof(DVDSubParseContext),
    dvdsub_parse_init,
    dvdsub_parse,
    dvdsub_parse_close,
};
