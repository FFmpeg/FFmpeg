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
#include "libavutil/mem.h"

/**
 * Print the sample description for comparison with the FATE reference.
 */
static int dump_sample_description(const AVCodec *codec, const char *header)
{
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    int ret = 1;

    if (!ctx)
        return 1;

    ctx->subtitle_header = (uint8_t *)av_strdup(header);
    if (!ctx->subtitle_header)
        goto end;
    ctx->subtitle_header_size = strlen(header);
    ctx->time_base = (AVRational){ 1, 1000 };

    if (avcodec_open2(ctx, codec, NULL) < 0)
        goto end;

    printf("extradata_size: %d\n", ctx->extradata_size);
    for (int i = 0; i < ctx->extradata_size; i++)
        printf("%02x", ctx->extradata[i]);
    printf("\n");

    ret = 0;
end:
    avcodec_free_context(&ctx);
    return ret;
}

int main(void)
{
    const AVCodec *codec = avcodec_find_encoder(AV_CODEC_ID_MOV_TEXT);
    int ret;

    if (!codec)
        return 1;

    printf("Fallback font\n");
    ret = dump_sample_description(codec,
                                  "[Script Info]\n"
                                  "ScriptType: v4.00+\n");
    printf("Explicit Serif style\n");
    ret |= dump_sample_description(codec,
                                   "[Script Info]\n"
                                   "ScriptType: v4.00+\n"
                                   "[V4+ Styles]\n"
                                   "Format: Name, Fontname, Fontsize\n"
                                   "Style: Default,Serif,18\n");
    return ret;
}
