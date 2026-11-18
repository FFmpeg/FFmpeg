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

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "libavcodec/avcodec.h"

static void parse(AVCodecParserContext *parser, AVCodecContext *ctx,
                  const char *name, const uint8_t *input, int input_size)
{
    uint8_t padded[2 * 2 * 18 + AV_INPUT_BUFFER_PADDING_SIZE] = { 0 };
    uint8_t *output;
    int output_size;
    int ret;

    if (input_size)
        memcpy(padded, input, input_size);
    ret = av_parser_parse2(parser, ctx, &output, &output_size,
                           input_size ? padded : NULL, input_size,
                           AV_NOPTS_VALUE, AV_NOPTS_VALUE, -1);
    printf("%s: input %d, consumed %d, output %d", name, input_size, ret, output_size);
    if (output_size) {
        printf(", duration %d, bitrate %" PRId64 ", ",
               parser->duration, ctx->bit_rate);
        for (int i = 0; i < output_size; i++)
            printf("%02x", output[i]);
    }
    printf("\n");
}

static int test_parser(enum AVCodecID codec, int channels, int frame_size)
{
    AVCodecParserContext *parser = av_parser_init(codec);
    AVCodecContext *ctx = avcodec_alloc_context3(NULL);
    uint8_t blocks[2 * 2 * 18] = { 0 };
    const int block_size = channels * frame_size;
    int ret = 1;

    if (!parser || !ctx)
        goto end;
    ctx->codec_id = codec;
    av_channel_layout_default(&ctx->ch_layout, channels);

    /* Mode 0 headers, with distinct payloads in each speech frame. */
    for (int i = 0; i < 2 * channels; i++)
        blocks[i * frame_size + 1] = i + 1;

    printf("%s, channels %d\n", avcodec_get_name(codec), channels);
    parse(parser, ctx, "exact", blocks, block_size);
    parse(parser, ctx, "split start", blocks, block_size - 1);
    parse(parser, ctx, "split end", blocks + block_size - 1, 1);
    if (channels > 1) {
        parse(parser, ctx, "first channel", blocks, frame_size);
        parse(parser, ctx, "last channel", blocks + frame_size, frame_size);
    }
    parse(parser, ctx, "combined", blocks, 2 * block_size);
    parse(parser, ctx, "remainder", blocks + block_size, block_size);
    if (channels == 1)
        parse(parser, ctx, "truncated block", blocks, block_size - 1);
    parse(parser, ctx, "eof", NULL, 0);

    /* New input after EOF needs a new parser. */
    av_parser_close(parser);
    parser = av_parser_init(codec);
    if (!parser)
        goto end;
    parser->flags |= PARSER_FLAG_COMPLETE_FRAMES;
    ctx->bit_rate = 0;
    parse(parser, ctx, "complete frames", blocks, 2 * block_size);
    parse(parser, ctx, "complete frames eof", NULL, 0);
    ret = 0;

end:
    av_parser_close(parser);
    avcodec_free_context(&ctx);
    return ret;
}

int main(void)
{
    int ret = test_parser(AV_CODEC_ID_AMR_NB, 1, 13);
    ret |= test_parser(AV_CODEC_ID_AMR_NB, 2, 13);
    ret |= test_parser(AV_CODEC_ID_AMR_WB, 1, 18);
    ret |= test_parser(AV_CODEC_ID_AMR_WB, 2, 18);
    return ret;
}
