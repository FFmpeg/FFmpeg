/*
 * RL2 Video Decoder
 * Copyright (C) 2008 Sascha Sommer (saschasommer@freenet.de)
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
 * RL2 Video Decoder
 * @author Sascha Sommer (saschasommer@freenet.de)
 * @see http://wiki.multimedia.cx/index.php?title=RL2
 */

#include <string.h>

#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "avcodec.h"
#include "codec_internal.h"
#include "decode.h"


#define EXTRADATA1_SIZE (6 + 256 * 3) ///< video base, clr count, palette

typedef struct Rl2Context {
    AVCodecContext *avctx;

    uint16_t video_base;  ///< initial drawing offset
    uint32_t clr_count;   ///< number of used colors (currently unused)
    uint8_t *back_frame;  ///< background frame
    uint32_t palette[AVPALETTE_COUNT];
} Rl2Context;

/**
 * Run Length Decode a single 320x200 frame
 * @param s rl2 context
 * @param in input buffer
 * @param size input buffer size
 * @param out output buffer
 * @param stride stride of the output buffer
 * @param video_base offset of the rle data inside the frame
 */
static void rl2_rle_decode(Rl2Context *s, const uint8_t *in, int size,
                           uint8_t *out, ptrdiff_t stride, int video_base)
{
    int base_x = video_base % s->avctx->width;
    int base_y = video_base / s->avctx->width;
    ptrdiff_t stride_adj = stride - s->avctx->width;
    const uint8_t *back_frame = s->back_frame;
    const uint8_t *in_end     = in + size;
    const uint8_t *out_end    = out + stride * s->avctx->height - stride_adj;
    uint8_t *line_end;

    /** copy start of the background frame */
    if (s->back_frame) {
        for (int i = 0; i <= base_y; i++) {
            memcpy(out, back_frame, s->avctx->width);
            out        += stride;
            back_frame += s->avctx->width;
        }
        back_frame += base_x - s->avctx->width;
    } else {
        out += stride * (base_y + 1);
    }
    line_end    = out - stride_adj;
    out        += base_x - stride;

    /** decode the variable part of the frame */
    while (in < in_end) {
        uint8_t val = *in++;
        int len     = 1;
        if (val >= 0x80) {
            if (in >= in_end)
                break;
            len = *in++;
            if (!len)
                break;
            val &= 0x7F;
        }

        if (back_frame) {
            if (!val) {
                do {
                    size_t copy = FFMIN(line_end - out, len);
                    memcpy(out, back_frame, copy);
                    out        += copy;
                    back_frame += copy;
                    len        -= copy;
                    if (out == line_end) {
                        if (out == out_end)
                            return;
                        out      += stride_adj;
                        line_end += stride;
                    }
                } while (len > 0);
                continue;
            }
            back_frame += len;
            val |= 0x80;
        }

        while (len--) {
            *out++ = val;
            if (out == line_end) {
                if (out == out_end)
                    return;
                out      += stride_adj;
                line_end += stride;
            }
        }
    }

    /** copy the rest from the background frame */
    if (s->back_frame) {
        while (1) {
            memcpy(out, back_frame, line_end - out);
            if (line_end == out_end)
                break;
            back_frame += line_end - out;
            out         = line_end + stride_adj;
            line_end   += stride;
        }
    }
}


/**
 * Initialize the decoder
 * @param avctx decoder context
 * @return 0 success, -1 on error
 */
static av_cold int rl2_decode_init(AVCodecContext *avctx)
{
    Rl2Context *s = avctx->priv_data;
    int back_size;
    int i;
    int ret;

    s->avctx       = avctx;
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    ret = ff_set_dimensions(avctx, 320, 200);
    if (ret < 0)
        return ret;

    /** parse extra data */
    if (!avctx->extradata || avctx->extradata_size < EXTRADATA1_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "invalid extradata size\n");
        return AVERROR(EINVAL);
    }

    /** get frame_offset */
    s->video_base = AV_RL16(&avctx->extradata[0]);
    s->clr_count  = AV_RL32(&avctx->extradata[2]);

    if (s->video_base >= avctx->width * avctx->height) {
        av_log(avctx, AV_LOG_ERROR, "invalid video_base\n");
        return AVERROR_INVALIDDATA;
    }

    /** initialize palette */
    for (i = 0; i < AVPALETTE_COUNT; i++)
        s->palette[i] = 0xFFU << 24 | AV_RB24(&avctx->extradata[6 + i * 3]);

    /** decode background frame if present */
    back_size = avctx->extradata_size - EXTRADATA1_SIZE;

    if (back_size > 0) {
        /* The 254 are padding to ensure that pointer arithmetic stays within
         * the buffer. */
        uint8_t *back_frame = av_mallocz(avctx->width * avctx->height + 254);
        if (!back_frame)
            return AVERROR(ENOMEM);
        rl2_rle_decode(s, avctx->extradata + EXTRADATA1_SIZE, back_size,
                       back_frame, avctx->width, 0);
        s->back_frame = back_frame;
    }
    return 0;
}


static int rl2_decode_frame(AVCodecContext *avctx, AVFrame *frame,
                            int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int ret, buf_size  = avpkt->size;
    Rl2Context *s = avctx->priv_data;

    if ((ret = ff_get_buffer(avctx, frame, 0)) < 0)
        return ret;

    /** run length decode */
    rl2_rle_decode(s, buf, buf_size, frame->data[0], frame->linesize[0],
                   s->video_base);

    /** make the palette available on the way out */
    memcpy(frame->data[1], s->palette, AVPALETTE_SIZE);

    *got_frame = 1;

    /** report that the buffer was completely consumed */
    return buf_size;
}


/**
 * Uninit decoder
 * @param avctx decoder context
 * @return 0 success, -1 on error
 */
static av_cold int rl2_decode_end(AVCodecContext *avctx)
{
    Rl2Context *s = avctx->priv_data;

    av_freep(&s->back_frame);

    return 0;
}


const FFCodec ff_rl2_decoder = {
    .p.name         = "rl2",
    CODEC_LONG_NAME("RL2 video"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_RL2,
    .priv_data_size = sizeof(Rl2Context),
    .init           = rl2_decode_init,
    .close          = rl2_decode_end,
    FF_CODEC_DECODE_CB(rl2_decode_frame),
    .p.capabilities = AV_CODEC_CAP_DR1,
};
