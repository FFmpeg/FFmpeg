/*
 * GIF parser
 * Copyright (c) 2018 Paul B Mahol
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
 * GIF parser
 */

#include "gif.h"
#include "parser.h"

typedef enum GIFParseStates {
    GIF_HEADER = 1,
    GIF_EXTENSION,
    GIF_EXTENSION_BLOCK,
    GIF_IMAGE,
    GIF_IMAGE_BLOCK,
} gif_states;

typedef struct GIFParseContext {
    ParseContext pc;
    unsigned found_sig;
    int found_start;
    int found_end;
    int index;
    int state;
    int gct_flag;
    int gct_size;
    int block_size;
    int etype;
    int delay;
    int keyframe;
} GIFParseContext;

static int gif_find_frame_end(GIFParseContext *g, const uint8_t *buf,
                              int buf_size, void *logctx)
{
    ParseContext *pc = &g->pc;
    int index, next = END_NOT_FOUND;

    for (index = 0; index < buf_size; index++) {
        if (!g->state) {
            if (!memcmp(buf + index, gif87a_sig, 6) ||
                !memcmp(buf + index, gif89a_sig, 6)) {
                g->state = GIF_HEADER;
                g->found_sig++;
                g->keyframe = 1;
            } else if (buf[index] == GIF_EXTENSION_INTRODUCER) {
                g->state = GIF_EXTENSION;
                g->found_start = pc->frame_start_found = 1;
            } else if (buf[index] == GIF_IMAGE_SEPARATOR) {
                if (g->state != GIF_EXTENSION_BLOCK && g->found_start &&
                    g->found_end && g->found_sig) {
                    next = index;
                    g->found_start = pc->frame_start_found = 1;
                    g->found_end = 0;
                    g->index = 0;
                    g->gct_flag = 0;
                    g->gct_size = 0;
                    g->state = GIF_IMAGE;
                    break;
                }
                g->state = GIF_IMAGE;
            } else if (buf[index] == GIF_TRAILER) {
                g->state = 0;
                g->found_end = 1;
                g->found_sig = 0;
            } else {
                g->found_sig = 0;
            }
        }

        if (g->state == GIF_HEADER) {
            if (g->index == 10) {
                g->gct_flag = !!(buf[index] & 0x80);
                g->gct_size = 3 * (1 << ((buf[index] & 0x07) + 1));
            }
            if (g->index >= 12 + g->gct_flag * g->gct_size) {
                g->state = 0;
                g->index = 0;
                g->gct_flag = 0;
                g->gct_size = 0;
                continue;
            }
            g->index++;
        } else if (g->state == GIF_EXTENSION) {
            if (g->found_start && g->found_end && g->found_sig) {
                next = index;
                g->found_start = pc->frame_start_found = 0;
                g->found_end = 0;
                g->index = 0;
                g->gct_flag = 0;
                g->gct_size = 0;
                g->state = 0;
                break;
            }
            if (g->index == 1) {
                g->etype = buf[index];
            }
            if (g->index >= 2) {
                g->block_size = buf[index];
                g->index = 0;
                g->state = GIF_EXTENSION_BLOCK;
                continue;
            }
            g->index++;
        } else if (g->state == GIF_IMAGE_BLOCK) {
            if (!g->index)
                g->block_size = buf[index];
            if (g->index >= g->block_size) {
                g->index = 0;
                if (!g->block_size) {
                    g->state = 0;
                    g->found_end = 1;
                }
                continue;
            }
            g->index++;
        } else if (g->state == GIF_EXTENSION_BLOCK) {
            if (g->etype == GIF_GCE_EXT_LABEL) {
                if (g->index == 0)
                    g->delay = 0;
                if (g->index >= 1 && g->index <= 2) {
                    g->delay |= buf[index] << (8 * (g->index - 1));
                }
            }
            if (g->index >= g->block_size) {
                g->block_size = buf[index];
                g->index = 0;
                if (!g->block_size)
                    g->state = 0;
                continue;
            }
            g->index++;
        } else if (g->state == GIF_IMAGE) {
            if (g->index == 9) {
                g->gct_flag = !!(buf[index] & 0x80);
                g->gct_size = 3 * (1 << ((buf[index] & 0x07) + 1));
            }
            if (g->index >= 10 + g->gct_flag * g->gct_size) {
                g->state = GIF_IMAGE_BLOCK;
                g->index = 0;
                g->gct_flag = 0;
                g->gct_size = 0;
                continue;
            }
            g->index++;
        }
    }

    return next;
}

static int gif_parse(AVCodecParserContext *s, AVCodecContext *avctx,
                     const uint8_t **poutbuf, int *poutbuf_size,
                     const uint8_t *buf, int buf_size)
{
    GIFParseContext *g = s->priv_data;
    int next;

    *poutbuf_size = 0;
    *poutbuf = NULL;

    if (s->flags & PARSER_FLAG_COMPLETE_FRAMES) {
        next = buf_size;
    } else {
        next = gif_find_frame_end(g, buf, buf_size, avctx);
        if (ff_combine_frame(&g->pc, next, &buf, &buf_size) < 0) {
            *poutbuf      = NULL;
            *poutbuf_size = 0;
            return buf_size;
        }
    }

    s->duration  = g->delay ? g->delay : 10;
    s->key_frame = g->keyframe;
    s->pict_type = g->keyframe ? AV_PICTURE_TYPE_I : AV_PICTURE_TYPE_P;
    g->keyframe  = 0;

    *poutbuf      = buf;
    *poutbuf_size = buf_size;
    return next;
}

const AVCodecParser ff_gif_parser = {
    .codec_ids      = { AV_CODEC_ID_GIF },
    .priv_data_size = sizeof(GIFParseContext),
    .parser_parse   = gif_parse,
    .parser_close   = ff_parse_close,
};
