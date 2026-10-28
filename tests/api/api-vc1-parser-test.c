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

#include <stdio.h>
#include <string.h>

#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"

/* Synthetic advanced-profile headers for a progressive 16x16 sequence,
 * with pulldown enabled and no optional display or HRD information.
 * Macroblock data is unnecessary: the parser only reads frame headers. */
static const uint8_t sequence[] = {
    0, 0, 1, 0x0f, 0xc2, 0, 0, 0x70, 0x07, 0x88, 0x80,
    0, 0, 1, 0x0e, 0, 0, 0x80,
};

static void parse(AVCodecParserContext *parser, AVCodecContext *ctx,
                  const char *name, int init, uint8_t header,
                  int payload_size, int trailing_zeros)
{
    uint8_t buf[128 + AV_INPUT_BUFFER_PADDING_SIZE] = { 0 };
    uint8_t *out;
    int size = 0, out_size;

    if (init) {
        memcpy(buf, sequence, sizeof(sequence));
        size += sizeof(sequence);
    }
    buf[size++] = 0;
    buf[size++] = 0;
    buf[size++] = 1;
    buf[size++] = 0x0d;
    memset(buf + size, 0xff, payload_size);
    buf[size] = header;
    if (payload_size > 1)
        buf[size + 1] = header == 0x80 ? 0x80 : 0;
    size += payload_size;
    memset(buf + size - trailing_zeros, 0, trailing_zeros);

    av_parser_parse2(parser, ctx, &out, &out_size, buf, size,
                     AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
    printf("%s: %c\n", name, av_get_picture_type_char(parser->pict_type));
}

int main(void)
{
    AVCodecParserContext *parser = av_parser_init(AV_CODEC_ID_VC1);
    AVCodecContext *ctx = avcodec_alloc_context3(NULL);

    if (!parser || !ctx) {
        av_parser_close(parser);
        avcodec_free_context(&ctx);
        return 1;
    }
    parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
    ctx->codec_id = AV_CODEC_ID_VC1;
    ctx->codec_type = AVMEDIA_TYPE_VIDEO;

    parse(parser, ctx, "initial I",         1, 0xc1, 37, 0);
    parse(parser, ctx, "skipped P",         0, 0xf2,  1, 0);
    parse(parser, ctx, "B after skipped P", 0, 0x80,  2, 0);
    parse(parser, ctx, "I ending in zeros", 0, 0xc1, 37, 2);
    parse(parser, ctx, "P starting in 01",  0, 0x01, 37, 0);
    parse(parser, ctx, "I after P",         0, 0xc1, 37, 0);

    av_parser_close(parser);
    avcodec_free_context(&ctx);
    return 0;
}
