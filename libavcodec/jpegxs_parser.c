/*
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

#include "libavutil/mem.h"

#include "bytestream.h"
#include "get_bits.h"
#include "jpegxs.h"
#include "parser.h"
#include "parser_internal.h"

typedef struct JPEGXSParseContext {
    ParseContext pc;

    int eoc_found;
} JPEGXSParseContext;

/**
 * Find the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int jpegxs_find_frame_end(JPEGXSParseContext *jpegxs, const uint8_t *buf,
                                 int buf_size)
{
    ParseContext *pc = &jpegxs->pc;
    int pic_found, i = 0;
    uint32_t state;

    pic_found = pc->frame_start_found;
    state = pc->state;

    if (!pic_found) {
        for (i = 0; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if ((uint16_t)state == JPEGXS_MARKER_SOC) {
                i++;
                pic_found = 1;
                break;
            }
        }
    }

    if (buf_size == 0) {
        if (jpegxs->eoc_found) {
            pc->frame_start_found = jpegxs->eoc_found = 0;
            pc->state = -1;
        }
        return 0;
    }

    while (pic_found && i < buf_size) {
        if (jpegxs->eoc_found) {
            for(; i < buf_size; i++) {
                state = (state << 8) | buf[i];
                if ((state >> 16) == JPEGXS_MARKER_EOC) {
                    if ((uint16_t)state == JPEGXS_MARKER_SOC) {
                        // New image
                        pc->frame_start_found = jpegxs->eoc_found = 0;
                        pc->state = -1;
                        return i - 1;
                    } else {
                        // False positive
                        i++;
                        jpegxs->eoc_found = 0;
                        break;
                    }
                }
            }
        }

        for(; i < buf_size; i++) {
            state = (state << 8) | buf[i];
            if ((uint16_t)state == JPEGXS_MARKER_EOC) {
                // EOC candidate
                i++;
                jpegxs->eoc_found = 1;
                break;
            }
        }
    }

    pc->frame_start_found = pic_found;
    pc->state = state;
    return END_NOT_FOUND;
}

static int jpegxs_parse_frame(AVCodecParserContext *s, AVCodecContext *avctx,
                              const uint8_t *buf, int buf_size)
{
    GetByteContext gbc;
    GetBitContext gb;
    int8_t bpc[3], log2_chroma_w[3], log2_chroma_h[3];
    int size, marker, components;

    s->key_frame = 1;
    s->pict_type = AV_PICTURE_TYPE_I;

    if (buf_size < 4)
        return 0;

    bytestream2_init(&gbc, buf, buf_size);
    marker = bytestream2_get_be16(&gbc);
    if (marker != JPEGXS_MARKER_SOC)
        return 0;

    marker = bytestream2_get_be16(&gbc);
    if (marker != JPEGXS_MARKER_CAP)
        return 0;
    size = bytestream2_get_be16(&gbc);
    bytestream2_skip(&gbc, FFMAX(size - 2, 0));

    marker = bytestream2_get_be16(&gbc);
    if (marker != JPEGXS_MARKER_PIH)
        return 0;
    size = bytestream2_get_be16(&gbc);
    bytestream2_skip(&gbc, 4); // Lcod
    bytestream2_skip(&gbc, 2); // Ppih
    bytestream2_skip(&gbc, 2); // Plev
    size -= 8;

    s->width  = bytestream2_get_be16(&gbc);
    s->height = bytestream2_get_be16(&gbc);
    size -= 4;

    bytestream2_skip(&gbc, 2); // Cw
    bytestream2_skip(&gbc, 2); // Hsl
    size -= 4;

    components = bytestream2_get_byte(&gbc);
    if (components != 1 && components != 3)
        return 0;
    size--;

    bytestream2_skip(&gbc, FFMAX(size - 2, 0));

    while (bytestream2_get_bytes_left(&gbc) > 0) {
        marker = bytestream2_get_be16(&gbc);

        switch(marker) {
        case JPEGXS_MARKER_CDT:
            size = bytestream2_get_be16(&gbc);
            init_get_bits8(&gb, gbc.buffer, FFMIN(FFMAX(size - 2, 0), bytestream2_get_bytes_left(&gbc)));

            for (int i = 0; i < components; i++) {
                bpc[i] = get_bits(&gb, 8);
                if (i && bpc[i] != bpc[i-1])
                    return 0;

                log2_chroma_w[i] = get_bits(&gb, 4);
                log2_chroma_h[i] = get_bits(&gb, 4);

                if (log2_chroma_h[i] > log2_chroma_w[i])
                    return 0;
                if (i == 2 && (log2_chroma_h[2] != log2_chroma_h[1] ||
                               log2_chroma_w[2] != log2_chroma_w[1]))
                    return 0;
            }

            switch (bpc[0]) {
            case 8:
                if (components == 1)                                     s->format = AV_PIX_FMT_GRAY8;
                else if (log2_chroma_w[1] == 1 && log2_chroma_h[1] == 1) s->format = AV_PIX_FMT_YUV444P;
                else if (log2_chroma_w[1] == 2 && log2_chroma_h[1] == 1) s->format = AV_PIX_FMT_YUV422P;
                else                                                     s->format = AV_PIX_FMT_YUV420P;
                break;
            case 10:
                if (components == 1)                                     s->format = AV_PIX_FMT_GRAY10;
                else if (log2_chroma_w[1] == 1 && log2_chroma_h[1] == 1) s->format = AV_PIX_FMT_YUV444P10;
                else if (log2_chroma_w[1] == 2 && log2_chroma_h[1] == 1) s->format = AV_PIX_FMT_YUV422P10;
                else                                                     s->format = AV_PIX_FMT_YUV420P10;
                break;
            case 12:
                if (components == 1)                                     s->format = AV_PIX_FMT_GRAY12;
                else if (log2_chroma_w[1] == 1 && log2_chroma_h[1] == 1) s->format = AV_PIX_FMT_YUV444P12;
                else if (log2_chroma_w[1] == 2 && log2_chroma_h[1] == 1) s->format = AV_PIX_FMT_YUV422P12;
                else                                                     s->format = AV_PIX_FMT_YUV420P12;
                break;
            case 14:
                if (components == 1)                                     s->format = AV_PIX_FMT_GRAY14;
                else if (log2_chroma_w[1] == 1 && log2_chroma_h[1] == 1) s->format = AV_PIX_FMT_YUV444P14;
                else if (log2_chroma_w[1] == 2 && log2_chroma_h[1] == 1) s->format = AV_PIX_FMT_YUV422P14;
                else                                                     s->format = AV_PIX_FMT_YUV420P14;
                break;
            default:
                s->format = AV_PIX_FMT_NONE;
                break;
            }
            return 0;
        default:
            size = bytestream2_get_be16(&gbc);
            bytestream2_skip(&gbc, FFMAX(size - 2, 0));
            break;
        }
    }

    return 0;
}

static int jpegxsvideo_parse(AVCodecParserContext *s,
                             AVCodecContext *avctx,
                             const uint8_t **poutbuf, int *poutbuf_size,
                             const uint8_t *buf, int buf_size)
{
    JPEGXSParseContext *jpegxs = s->priv_data;
    ParseContext *pc = &jpegxs->pc;
    int next;

    next = jpegxs_find_frame_end(jpegxs, buf, buf_size);

    if (ff_combine_frame(pc, next, &buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    jpegxs_parse_frame(s, avctx, buf, buf_size);

    *poutbuf = buf;
    *poutbuf_size = buf_size;
    return next;
}

static av_cold void jpegxsparse_close(AVCodecParserContext *s)
{
    JPEGXSParseContext *jpegxs = s->priv_data;
    ParseContext *pc = &jpegxs->pc;

    av_freep(&pc->buffer);
}

const FFCodecParser ff_jpegxs_parser = {
    PARSER_CODEC_LIST(AV_CODEC_ID_JPEGXS),
    .priv_data_size = sizeof(JPEGXSParseContext),
    .parse          = jpegxsvideo_parse,
    .close          = jpegxsparse_close,
};
