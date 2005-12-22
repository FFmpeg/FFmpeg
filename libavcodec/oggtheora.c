/**
    Copyright (C) 2005  Måns Rullgård

    Permission is hereby granted, free of charge, to any person
    obtaining a copy of this software and associated documentation
    files (the "Software"), to deal in the Software without
    restriction, including without limitation the rights to use, copy,
    modify, merge, publish, distribute, sublicense, and/or sell copies
    of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
    NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
    HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
    WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
    DEALINGS IN THE SOFTWARE.
**/

#include <stdlib.h>
#include <theora/theora.h>
#include "avcodec.h"

typedef struct TheoraContext {
    theora_info info;
    theora_state state;
    theora_comment comment;
    ogg_packet op;
} TheoraContext;

static int
Theora_decode_frame(AVCodecContext *ctx, void *outdata, int *outdata_size,
                    uint8_t *buf, int buf_size)
{
    TheoraContext *thc = ctx->priv_data;
    AVFrame *frame = outdata;
    yuv_buffer yuv;

    thc->op.packet = buf;
    thc->op.bytes = buf_size;

    if(theora_decode_packetin(&thc->state, &thc->op))
        return -1;

    theora_decode_YUVout(&thc->state, &yuv);

    frame->data[0] = yuv.y;
    frame->data[1] = yuv.u;
    frame->data[2] = yuv.v;
    frame->linesize[0] = yuv.y_stride;
    frame->linesize[1] = yuv.uv_stride;
    frame->linesize[2] = yuv.uv_stride;

    *outdata_size = sizeof(*frame);
    return buf_size;
}

static int
Theora_decode_end(AVCodecContext *ctx)
{
    TheoraContext *thc = ctx->priv_data;
    theora_info_clear(&thc->info);
    theora_comment_clear(&thc->comment);
    return 0;
}

static int
Theora_decode_init(AVCodecContext *ctx)
{
    TheoraContext *thc = ctx->priv_data;
    int size, hs, i;
    ogg_packet op;
    uint8_t *cdp;

    if(ctx->extradata_size < 6)
        return -1;

    theora_info_init(&thc->info);

    memset(&op, 0, sizeof(op));
    cdp = ctx->extradata;
    size = ctx->extradata_size;

    for(i = 0; i < 3; i++){
        hs = *cdp++ << 8;
        hs += *cdp++;
        size -= 2;

        if(hs > size){
            av_log(ctx, AV_LOG_ERROR, "extradata too small: %i > %i\n",
                   hs, size);
            return -1;
        }

        op.packet = cdp;
        op.bytes = hs;
        op.b_o_s = !i;
        if(theora_decode_header(&thc->info, &thc->comment, &op))
            return -1;
        op.packetno++;

        cdp += hs;
        size -= hs;
    }

    theora_decode_init(&thc->state, &thc->info);

    ctx->width = thc->info.width;
    ctx->height = thc->info.height;
    ctx->time_base.num = thc->info.fps_denominator;
    ctx->time_base.den = thc->info.fps_numerator;
    ctx->pix_fmt = PIX_FMT_YUV420P; /* FIXME: others are possible */

    return 0;
}

AVCodec oggtheora_decoder = {
    "theora",
    CODEC_TYPE_VIDEO,
    CODEC_ID_THEORA,
    sizeof(TheoraContext),
    Theora_decode_init,
    NULL,
    Theora_decode_end,
    Theora_decode_frame,
    0,
    NULL
};
