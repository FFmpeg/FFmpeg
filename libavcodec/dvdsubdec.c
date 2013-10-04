/*
 * DVD subtitle decoding
 * Copyright (c) 2005 Fabrice Bellard
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
#include "get_bits.h"
#include "dsputil.h"
#include "libavutil/attributes.h"
#include "libavutil/colorspace.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "libavutil/avstring.h"

typedef struct DVDSubContext
{
  AVClass *class;
  uint32_t palette[16];
  char    *palette_str;
  int      has_palette;
  uint8_t  colormap[4];
  uint8_t  alpha[256];
  uint8_t *buf;
  int      buf_size;
#ifdef DEBUG
  int sub_id;
#endif
} DVDSubContext;

static void yuv_a_to_rgba(const uint8_t *ycbcr, const uint8_t *alpha, uint32_t *rgba, int num_values)
{
    const uint8_t *cm = ff_cropTbl + MAX_NEG_CROP;
    uint8_t r, g, b;
    int i, y, cb, cr;
    int r_add, g_add, b_add;

    for (i = num_values; i > 0; i--) {
        y = *ycbcr++;
        cr = *ycbcr++;
        cb = *ycbcr++;
        YUV_TO_RGB1_CCIR(cb, cr);
        YUV_TO_RGB2_CCIR(r, g, b, y);
        *rgba++ = (*alpha++ << 24) | (r << 16) | (g << 8) | b;
    }
}

static int decode_run_2bit(GetBitContext *gb, int *color)
{
    unsigned int v, t;

    v = 0;
    for (t = 1; v < t && t <= 0x40; t <<= 2)
        v = (v << 4) | get_bits(gb, 4);
    *color = v & 3;
    if (v < 4) { /* Code for fill rest of line */
        return INT_MAX;
    }
    return v >> 2;
}

static int decode_run_8bit(GetBitContext *gb, int *color)
{
    int len;
    int has_run = get_bits1(gb);
    if (get_bits1(gb))
        *color = get_bits(gb, 8);
    else
        *color = get_bits(gb, 2);
    if (has_run) {
        if (get_bits1(gb)) {
            len = get_bits(gb, 7);
            if (len == 0)
                len = INT_MAX;
            else
                len += 9;
        } else
            len = get_bits(gb, 3) + 2;
    } else
        len = 1;
    return len;
}

static int decode_rle(uint8_t *bitmap, int linesize, int w, int h,
                      const uint8_t *buf, int start, int buf_size, int is_8bit)
{
    GetBitContext gb;
    int bit_len;
    int x, y, len, color;
    uint8_t *d;

    bit_len = (buf_size - start) * 8;
    init_get_bits(&gb, buf + start, bit_len);

    x = 0;
    y = 0;
    d = bitmap;
    for(;;) {
        if (get_bits_count(&gb) > bit_len)
            return -1;
        if (is_8bit)
            len = decode_run_8bit(&gb, &color);
        else
            len = decode_run_2bit(&gb, &color);
        len = FFMIN(len, w - x);
        memset(d + x, color, len);
        x += len;
        if (x >= w) {
            y++;
            if (y >= h)
                break;
            d += linesize;
            x = 0;
            /* byte align */
            align_get_bits(&gb);
        }
    }
    return 0;
}

static void guess_palette(DVDSubContext* ctx,
                          uint32_t *rgba_palette,
                          uint32_t subtitle_color)
{
    static const uint8_t level_map[4][4] = {
        // this configuration (full range, lowest to highest) in tests
        // seemed most common, so assume this
        {0xff},
        {0x00, 0xff},
        {0x00, 0x80, 0xff},
        {0x00, 0x55, 0xaa, 0xff},
    };
    uint8_t color_used[16] = { 0 };
    int nb_opaque_colors, i, level, j, r, g, b;
    uint8_t *colormap = ctx->colormap, *alpha = ctx->alpha;

    if(ctx->has_palette) {
        for(i = 0; i < 4; i++)
            rgba_palette[i] = (ctx->palette[colormap[i]] & 0x00ffffff)
                              | ((alpha[i] * 17U) << 24);
        return;
    }

    for(i = 0; i < 4; i++)
        rgba_palette[i] = 0;

    nb_opaque_colors = 0;
    for(i = 0; i < 4; i++) {
        if (alpha[i] != 0 && !color_used[colormap[i]]) {
            color_used[colormap[i]] = 1;
            nb_opaque_colors++;
        }
    }

    if (nb_opaque_colors == 0)
        return;

    j = 0;
    memset(color_used, 0, 16);
    for(i = 0; i < 4; i++) {
        if (alpha[i] != 0) {
            if (!color_used[colormap[i]])  {
                level = level_map[nb_opaque_colors][j];
                r = (((subtitle_color >> 16) & 0xff) * level) >> 8;
                g = (((subtitle_color >> 8) & 0xff) * level) >> 8;
                b = (((subtitle_color >> 0) & 0xff) * level) >> 8;
                rgba_palette[i] = b | (g << 8) | (r << 16) | ((alpha[i] * 17) << 24);
                color_used[colormap[i]] = (i + 1);
                j++;
            } else {
                rgba_palette[i] = (rgba_palette[color_used[colormap[i]] - 1] & 0x00ffffff) |
                                    ((alpha[i] * 17) << 24);
            }
        }
    }
}

static void reset_rects(AVSubtitle *sub_header)
{
    int i;

    if (sub_header->rects != NULL) {
        for (i = 0; i < sub_header->num_rects; i++) {
            av_freep(&sub_header->rects[i]->pict.data[0]);
            av_freep(&sub_header->rects[i]->pict.data[1]);
            av_freep(&sub_header->rects[i]);
        }
        av_freep(&sub_header->rects);
        sub_header->num_rects = 0;
    }
}

#define READ_OFFSET(a) (big_offsets ? AV_RB32(a) : AV_RB16(a))

static int decode_dvd_subtitles(DVDSubContext *ctx, AVSubtitle *sub_header,
                                const uint8_t *buf, int buf_size)
{
    int cmd_pos, pos, cmd, x1, y1, x2, y2, offset1, offset2, next_cmd_pos;
    int big_offsets, offset_size, is_8bit = 0;
    const uint8_t *yuv_palette = 0;
    uint8_t *colormap = ctx->colormap, *alpha = ctx->alpha;
    int date;
    int i;
    int is_menu = 0;

    if (buf_size < 10)
        return -1;

    if (AV_RB16(buf) == 0) {   /* HD subpicture with 4-byte offsets */
        big_offsets = 1;
        offset_size = 4;
        cmd_pos = 6;
    } else {
        big_offsets = 0;
        offset_size = 2;
        cmd_pos = 2;
    }

    cmd_pos = READ_OFFSET(buf + cmd_pos);

    if (cmd_pos < 0 || cmd_pos > buf_size - 2 - offset_size)
        return AVERROR(EAGAIN);

    while (cmd_pos > 0 && cmd_pos < buf_size - 2 - offset_size) {
        date = AV_RB16(buf + cmd_pos);
        next_cmd_pos = READ_OFFSET(buf + cmd_pos + 2);
        av_dlog(NULL, "cmd_pos=0x%04x next=0x%04x date=%d\n",
                cmd_pos, next_cmd_pos, date);
        pos = cmd_pos + 2 + offset_size;
        offset1 = -1;
        offset2 = -1;
        x1 = y1 = x2 = y2 = 0;
        while (pos < buf_size) {
            cmd = buf[pos++];
            av_dlog(NULL, "cmd=%02x\n", cmd);
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
                /* set colormap */
                if ((buf_size - pos) < 2)
                    goto fail;
                colormap[3] = buf[pos] >> 4;
                colormap[2] = buf[pos] & 0x0f;
                colormap[1] = buf[pos + 1] >> 4;
                colormap[0] = buf[pos + 1] & 0x0f;
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
            av_dlog(NULL, "alpha=%x%x%x%x\n", alpha[0],alpha[1],alpha[2],alpha[3]);
                break;
            case 0x05:
            case 0x85:
                if ((buf_size - pos) < 6)
                    goto fail;
                x1 = (buf[pos] << 4) | (buf[pos + 1] >> 4);
                x2 = ((buf[pos + 1] & 0x0f) << 8) | buf[pos + 2];
                y1 = (buf[pos + 3] << 4) | (buf[pos + 4] >> 4);
                y2 = ((buf[pos + 4] & 0x0f) << 8) | buf[pos + 5];
                if (cmd & 0x80)
                    is_8bit = 1;
                av_dlog(NULL, "x1=%d x2=%d y1=%d y2=%d\n", x1, x2, y1, y2);
                pos += 6;
                break;
            case 0x06:
                if ((buf_size - pos) < 4)
                    goto fail;
                offset1 = AV_RB16(buf + pos);
                offset2 = AV_RB16(buf + pos + 2);
                av_dlog(NULL, "offset1=0x%04x offset2=0x%04x\n", offset1, offset2);
                pos += 4;
                break;
            case 0x86:
                if ((buf_size - pos) < 8)
                    goto fail;
                offset1 = AV_RB32(buf + pos);
                offset2 = AV_RB32(buf + pos + 4);
                av_dlog(NULL, "offset1=0x%04x offset2=0x%04x\n", offset1, offset2);
                pos += 8;
                break;

            case 0x83:
                /* HD set palette */
                if ((buf_size - pos) < 768)
                    goto fail;
                yuv_palette = buf + pos;
                pos += 768;
                break;
            case 0x84:
                /* HD set contrast (alpha) */
                if ((buf_size - pos) < 256)
                    goto fail;
                for (i = 0; i < 256; i++)
                    alpha[i] = 0xFF - buf[pos+i];
                pos += 256;
                break;

            case 0xff:
                goto the_end;
            default:
                av_dlog(NULL, "unrecognised subpicture command 0x%x\n", cmd);
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
                reset_rects(sub_header);

                bitmap = av_malloc(w * h);
                sub_header->rects = av_mallocz(sizeof(*sub_header->rects));
                sub_header->rects[0] = av_mallocz(sizeof(AVSubtitleRect));
                sub_header->num_rects = 1;
                sub_header->rects[0]->pict.data[0] = bitmap;
                decode_rle(bitmap, w * 2, w, (h + 1) / 2,
                           buf, offset1, buf_size, is_8bit);
                decode_rle(bitmap + w, w * 2, w, h / 2,
                           buf, offset2, buf_size, is_8bit);
                sub_header->rects[0]->pict.data[1] = av_mallocz(AVPALETTE_SIZE);
                if (is_8bit) {
                    if (yuv_palette == 0)
                        goto fail;
                    sub_header->rects[0]->nb_colors = 256;
                    yuv_a_to_rgba(yuv_palette, alpha, (uint32_t*)sub_header->rects[0]->pict.data[1], 256);
                } else {
                    sub_header->rects[0]->nb_colors = 4;
                    guess_palette(ctx, (uint32_t*)sub_header->rects[0]->pict.data[1],
                                  0xffff00);
                }
                sub_header->rects[0]->x = x1;
                sub_header->rects[0]->y = y1;
                sub_header->rects[0]->w = w;
                sub_header->rects[0]->h = h;
                sub_header->rects[0]->type = SUBTITLE_BITMAP;
                sub_header->rects[0]->pict.linesize[0] = w;
                sub_header->rects[0]->flags = is_menu ? AV_SUBTITLE_FLAG_FORCED : 0;
            }
        }
        if (next_cmd_pos < cmd_pos) {
            av_log(NULL, AV_LOG_ERROR, "Invalid command offset\n");
            break;
        }
        if (next_cmd_pos == cmd_pos)
            break;
        cmd_pos = next_cmd_pos;
    }
    if (sub_header->num_rects > 0)
        return is_menu;
 fail:
    reset_rects(sub_header);
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
    uint8_t transp_color[256] = { 0 };
    int y1, y2, x1, x2, y, w, h, i;
    uint8_t *bitmap;

    if (s->num_rects == 0 || s->rects == NULL || s->rects[0]->w <= 0 || s->rects[0]->h <= 0)
        return 0;

    for(i = 0; i < s->rects[0]->nb_colors; i++) {
        if ((((uint32_t*)s->rects[0]->pict.data[1])[i] >> 24) == 0)
            transp_color[i] = 1;
    }
    y1 = 0;
    while (y1 < s->rects[0]->h && is_transp(s->rects[0]->pict.data[0] + y1 * s->rects[0]->pict.linesize[0],
                                  1, s->rects[0]->w, transp_color))
        y1++;
    if (y1 == s->rects[0]->h) {
        av_freep(&s->rects[0]->pict.data[0]);
        s->rects[0]->w = s->rects[0]->h = 0;
        return 0;
    }

    y2 = s->rects[0]->h - 1;
    while (y2 > 0 && is_transp(s->rects[0]->pict.data[0] + y2 * s->rects[0]->pict.linesize[0], 1,
                               s->rects[0]->w, transp_color))
        y2--;
    x1 = 0;
    while (x1 < (s->rects[0]->w - 1) && is_transp(s->rects[0]->pict.data[0] + x1, s->rects[0]->pict.linesize[0],
                                        s->rects[0]->h, transp_color))
        x1++;
    x2 = s->rects[0]->w - 1;
    while (x2 > 0 && is_transp(s->rects[0]->pict.data[0] + x2, s->rects[0]->pict.linesize[0], s->rects[0]->h,
                                  transp_color))
        x2--;
    w = x2 - x1 + 1;
    h = y2 - y1 + 1;
    bitmap = av_malloc(w * h);
    if (!bitmap)
        return 1;
    for(y = 0; y < h; y++) {
        memcpy(bitmap + w * y, s->rects[0]->pict.data[0] + x1 + (y1 + y) * s->rects[0]->pict.linesize[0], w);
    }
    av_freep(&s->rects[0]->pict.data[0]);
    s->rects[0]->pict.data[0] = bitmap;
    s->rects[0]->pict.linesize[0] = w;
    s->rects[0]->w = w;
    s->rects[0]->h = h;
    s->rects[0]->x += x1;
    s->rects[0]->y += y1;
    return 1;
}

#ifdef DEBUG
#undef fprintf
#undef perror
#undef exit
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

static int append_to_cached_buf(AVCodecContext *avctx,
                                const uint8_t *buf, int buf_size)
{
    DVDSubContext *ctx = avctx->priv_data;

    if (ctx->buf_size > 0xffff - buf_size) {
        av_log(avctx, AV_LOG_WARNING, "Attempt to reconstruct "
               "too large SPU packets aborted.\n");
        av_freep(&ctx->buf);
        return AVERROR_INVALIDDATA;
    }
    ctx->buf = av_realloc(ctx->buf, ctx->buf_size + buf_size);
    if (!ctx->buf)
        return AVERROR(ENOMEM);
    memcpy(ctx->buf + ctx->buf_size, buf, buf_size);
    ctx->buf_size += buf_size;
    return 0;
}

static int dvdsub_decode(AVCodecContext *avctx,
                         void *data, int *data_size,
                         AVPacket *avpkt)
{
    DVDSubContext *ctx = avctx->priv_data;
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    AVSubtitle *sub = data;
    int is_menu;

    if (ctx->buf) {
        int ret = append_to_cached_buf(avctx, buf, buf_size);
        if (ret < 0) {
            *data_size = 0;
            return ret;
        }
        buf = ctx->buf;
        buf_size = ctx->buf_size;
    }

    is_menu = decode_dvd_subtitles(ctx, sub, buf, buf_size);
    if (is_menu == AVERROR(EAGAIN)) {
        *data_size = 0;
        return append_to_cached_buf(avctx, buf, buf_size);
    }

    if (is_menu < 0) {
    no_subtitle:
        *data_size = 0;

        return buf_size;
    }
    if (!is_menu && find_smallest_bounding_rectangle(sub) == 0)
        goto no_subtitle;

#if defined(DEBUG)
    {
    char ppm_name[32];

    snprintf(ppm_name, sizeof(ppm_name), "/tmp/%05d.ppm", ctx->sub_id++);
    av_dlog(NULL, "start=%d ms end =%d ms\n",
            sub->start_display_time,
            sub->end_display_time);
    ppm_save(ppm_name, sub->rects[0]->pict.data[0],
             sub->rects[0]->w, sub->rects[0]->h, sub->rects[0]->pict.data[1]);
    }
#endif

    av_freep(&ctx->buf);
    ctx->buf_size = 0;
    *data_size = 1;
    return buf_size;
}

static void parse_palette(DVDSubContext *ctx, char *p)
{
    int i;

    ctx->has_palette = 1;
    for(i=0;i<16;i++) {
        ctx->palette[i] = strtoul(p, &p, 16);
        while(*p == ',' || av_isspace(*p))
            p++;
    }
}

static int dvdsub_parse_extradata(AVCodecContext *avctx)
{
    DVDSubContext *ctx = (DVDSubContext*) avctx->priv_data;
    char *dataorig, *data;

    if (!avctx->extradata || !avctx->extradata_size)
        return 1;

    dataorig = data = av_malloc(avctx->extradata_size+1);
    if (!data)
        return AVERROR(ENOMEM);
    memcpy(data, avctx->extradata, avctx->extradata_size);
    data[avctx->extradata_size] = '\0';

    for(;;) {
        int pos = strcspn(data, "\n\r");
        if (pos==0 && *data==0)
            break;

        if (strncmp("palette:", data, 8) == 0) {
            parse_palette(ctx, data + 8);
        } else if (strncmp("size:", data, 5) == 0) {
            int w, h;
            if (sscanf(data + 5, "%dx%d", &w, &h) == 2 &&
                av_image_check_size(w, h, 0, avctx) >= 0)
                avcodec_set_dimensions(avctx, w, h);
        }

        data += pos;
        data += strspn(data, "\n\r");
    }

    av_free(dataorig);
    return 1;
}

static av_cold int dvdsub_init(AVCodecContext *avctx)
{
    DVDSubContext *ctx = avctx->priv_data;
    int ret;

    if ((ret = dvdsub_parse_extradata(avctx)) < 0)
        return ret;

    if (ctx->palette_str)
        parse_palette(ctx, ctx->palette_str);
    if (ctx->has_palette) {
        int i;
        av_log(avctx, AV_LOG_DEBUG, "palette:");
        for(i=0;i<16;i++)
            av_log(avctx, AV_LOG_DEBUG, " 0x%06x", ctx->palette[i]);
        av_log(avctx, AV_LOG_DEBUG, "\n");
    }

    return 1;
}

static av_cold int dvdsub_close(AVCodecContext *avctx)
{
    DVDSubContext *ctx = avctx->priv_data;
    av_freep(&ctx->buf);
    ctx->buf_size = 0;
    return 0;
}

#define OFFSET(field) offsetof(DVDSubContext, field)
#define VD AV_OPT_FLAG_SUBTITLE_PARAM | AV_OPT_FLAG_DECODING_PARAM
static const AVOption options[] = {
    { "palette", "set the global palette", OFFSET(palette_str), AV_OPT_TYPE_STRING, { .str = NULL }, 0, 0, VD },
    { NULL }
};
static const AVClass dvdsub_class = {
    .class_name = "dvdsubdec",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_dvdsub_decoder = {
    .name           = "dvdsub",
    .long_name      = NULL_IF_CONFIG_SMALL("DVD subtitles"),
    .type           = AVMEDIA_TYPE_SUBTITLE,
    .id             = AV_CODEC_ID_DVD_SUBTITLE,
    .priv_data_size = sizeof(DVDSubContext),
    .init           = dvdsub_init,
    .decode         = dvdsub_decode,
    .close          = dvdsub_close,
    .priv_class     = &dvdsub_class,
};
