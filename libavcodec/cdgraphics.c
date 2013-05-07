/*
 * CD Graphics Video Decoder
 * Copyright (c) 2009 Michael Tison
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
#include "bytestream.h"
#include "internal.h"

/**
 * @file
 * @brief CD Graphics Video Decoder
 * @author Michael Tison
 * @see http://wiki.multimedia.cx/index.php?title=CD_Graphics
 * @see http://www.ccs.neu.edu/home/bchafy/cdb/info/cdg
 */

/// default screen sizes
#define CDG_FULL_WIDTH           300
#define CDG_FULL_HEIGHT          216
#define CDG_DISPLAY_WIDTH        294
#define CDG_DISPLAY_HEIGHT       204
#define CDG_BORDER_WIDTH           6
#define CDG_BORDER_HEIGHT         12

/// masks
#define CDG_COMMAND             0x09
#define CDG_MASK                0x3F

/// instruction codes
#define CDG_INST_MEMORY_PRESET     1
#define CDG_INST_BORDER_PRESET     2
#define CDG_INST_TILE_BLOCK        6
#define CDG_INST_SCROLL_PRESET    20
#define CDG_INST_SCROLL_COPY      24
#define CDG_INST_LOAD_PAL_LO      30
#define CDG_INST_LOAD_PAL_HIGH    31
#define CDG_INST_TILE_BLOCK_XOR   38

/// data sizes
#define CDG_PACKET_SIZE           24
#define CDG_DATA_SIZE             16
#define CDG_TILE_HEIGHT           12
#define CDG_TILE_WIDTH             6
#define CDG_MINIMUM_PKT_SIZE       6
#define CDG_MINIMUM_SCROLL_SIZE    3
#define CDG_HEADER_SIZE            8
#define CDG_PALETTE_SIZE          16

typedef struct CDGraphicsContext {
    AVFrame *frame;
    int hscroll;
    int vscroll;
} CDGraphicsContext;

static av_cold int cdg_decode_init(AVCodecContext *avctx)
{
    CDGraphicsContext *cc = avctx->priv_data;

    cc->frame = av_frame_alloc();
    if (!cc->frame)
        return AVERROR(ENOMEM);

    avctx->width   = CDG_FULL_WIDTH;
    avctx->height  = CDG_FULL_HEIGHT;
    avctx->pix_fmt = AV_PIX_FMT_PAL8;

    return 0;
}

static void cdg_border_preset(CDGraphicsContext *cc, uint8_t *data)
{
    int y;
    int lsize    = cc->frame->linesize[0];
    uint8_t *buf = cc->frame->data[0];
    int color    = data[0] & 0x0F;

    if (!(data[1] & 0x0F)) {
        /// fill the top and bottom borders
        memset(buf, color, CDG_BORDER_HEIGHT * lsize);
        memset(buf + (CDG_FULL_HEIGHT - CDG_BORDER_HEIGHT) * lsize,
               color, CDG_BORDER_HEIGHT * lsize);

        /// fill the side borders
        for (y = CDG_BORDER_HEIGHT; y < CDG_FULL_HEIGHT - CDG_BORDER_HEIGHT; y++) {
            memset(buf + y * lsize, color, CDG_BORDER_WIDTH);
            memset(buf + CDG_FULL_WIDTH - CDG_BORDER_WIDTH + y * lsize,
                   color, CDG_BORDER_WIDTH);
        }
    }
}

static void cdg_load_palette(CDGraphicsContext *cc, uint8_t *data, int low)
{
    uint8_t r, g, b;
    uint16_t color;
    int i;
    int array_offset  = low ? 0 : 8;
    uint32_t *palette = (uint32_t *) cc->frame->data[1];

    for (i = 0; i < 8; i++) {
        color = (data[2 * i] << 6) + (data[2 * i + 1] & 0x3F);
        r = ((color >> 8) & 0x000F) * 17;
        g = ((color >> 4) & 0x000F) * 17;
        b = ((color     ) & 0x000F) * 17;
        palette[i + array_offset] = 0xFFU << 24 | r << 16 | g << 8 | b;
    }
    cc->frame->palette_has_changed = 1;
}

static int cdg_tile_block(CDGraphicsContext *cc, uint8_t *data, int b)
{
    unsigned ci, ri;
    int color;
    int x, y;
    int ai;
    int stride   = cc->frame->linesize[0];
    uint8_t *buf = cc->frame->data[0];

    ri = (data[2] & 0x1F) * CDG_TILE_HEIGHT + cc->vscroll;
    ci = (data[3] & 0x3F) * CDG_TILE_WIDTH  + cc->hscroll;

    if (ri > (CDG_FULL_HEIGHT - CDG_TILE_HEIGHT))
        return AVERROR(EINVAL);
    if (ci > (CDG_FULL_WIDTH - CDG_TILE_WIDTH))
        return AVERROR(EINVAL);

    for (y = 0; y < CDG_TILE_HEIGHT; y++) {
        for (x = 0; x < CDG_TILE_WIDTH; x++) {
            if (!((data[4 + y] >> (5 - x)) & 0x01))
                color = data[0] & 0x0F;
            else
                color = data[1] & 0x0F;

            ai = ci + x + (stride * (ri + y));
            if (b)
                color ^= buf[ai];
            buf[ai] = color;
        }
    }

    return 0;
}

#define UP    2
#define DOWN  1
#define LEFT  2
#define RIGHT 1

static void cdg_copy_rect_buf(int out_tl_x, int out_tl_y, uint8_t *out,
                              int in_tl_x, int in_tl_y, uint8_t *in,
                              int w, int h, int stride)
{
    int y;

    in  += in_tl_x  + in_tl_y  * stride;
    out += out_tl_x + out_tl_y * stride;
    for (y = 0; y < h; y++)
        memcpy(out + y * stride, in + y * stride, w);
}

static void cdg_fill_rect_preset(int tl_x, int tl_y, uint8_t *out,
                                 int color, int w, int h, int stride)
{
    int y;

    for (y = tl_y; y < tl_y + h; y++)
        memset(out + tl_x + y * stride, color, w);
}

static void cdg_fill_wrapper(int out_tl_x, int out_tl_y, uint8_t *out,
                             int in_tl_x, int in_tl_y, uint8_t *in,
                             int color, int w, int h, int stride, int roll)
{
    if (roll) {
        cdg_copy_rect_buf(out_tl_x, out_tl_y, out, in_tl_x, in_tl_y,
                          in, w, h, stride);
    } else {
        cdg_fill_rect_preset(out_tl_x, out_tl_y, out, color, w, h, stride);
    }
}

static void cdg_scroll(CDGraphicsContext *cc, uint8_t *data,
                       AVFrame *new_frame, int roll_over)
{
    int color;
    int hscmd, h_off, hinc, vscmd, v_off, vinc;
    int y;
    int stride   = cc->frame->linesize[0];
    uint8_t *in  = cc->frame->data[0];
    uint8_t *out = new_frame->data[0];

    color =  data[0] & 0x0F;
    hscmd = (data[1] & 0x30) >> 4;
    vscmd = (data[2] & 0x30) >> 4;

    h_off =  FFMIN(data[1] & 0x07, CDG_BORDER_WIDTH  - 1);
    v_off =  FFMIN(data[2] & 0x0F, CDG_BORDER_HEIGHT - 1);

    /// find the difference and save the offset for cdg_tile_block usage
    hinc = h_off - cc->hscroll;
    vinc = v_off - cc->vscroll;
    cc->hscroll = h_off;
    cc->vscroll = v_off;

    if (vscmd == UP)
        vinc -= 12;
    if (vscmd == DOWN)
        vinc += 12;
    if (hscmd == LEFT)
        hinc -= 6;
    if (hscmd == RIGHT)
        hinc += 6;

    if (!hinc && !vinc)
        return;

    memcpy(new_frame->data[1], cc->frame->data[1], CDG_PALETTE_SIZE * 4);

    for (y = FFMAX(0, vinc); y < FFMIN(CDG_FULL_HEIGHT + vinc, CDG_FULL_HEIGHT); y++)
        memcpy(out + FFMAX(0, hinc) + stride * y,
               in + FFMAX(0, hinc) - hinc + (y - vinc) * stride,
               FFMIN(stride + hinc, stride));

    if (vinc > 0)
        cdg_fill_wrapper(0, 0, out,
                         0, CDG_FULL_HEIGHT - vinc, in, color,
                         stride, vinc, stride, roll_over);
    else if (vinc < 0)
        cdg_fill_wrapper(0, CDG_FULL_HEIGHT + vinc, out,
                         0, 0, in, color,
                         stride, -1 * vinc, stride, roll_over);

    if (hinc > 0)
        cdg_fill_wrapper(0, 0, out,
                         CDG_FULL_WIDTH - hinc, 0, in, color,
                         hinc, CDG_FULL_HEIGHT, stride, roll_over);
    else if (hinc < 0)
        cdg_fill_wrapper(CDG_FULL_WIDTH + hinc, 0, out,
                         0, 0, in, color,
                         -1 * hinc, CDG_FULL_HEIGHT, stride, roll_over);

}

static int cdg_decode_frame(AVCodecContext *avctx,
                            void *data, int *got_frame, AVPacket *avpkt)
{
    const uint8_t *buf = avpkt->data;
    int buf_size       = avpkt->size;
    int ret;
    uint8_t command, inst;
    uint8_t cdg_data[CDG_DATA_SIZE];
    AVFrame *frame = data;
    CDGraphicsContext *cc = avctx->priv_data;

    if (buf_size < CDG_MINIMUM_PKT_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "buffer too small for decoder\n");
        return AVERROR(EINVAL);
    }
    if (buf_size > CDG_HEADER_SIZE + CDG_DATA_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "buffer too big for decoder\n");
        return AVERROR(EINVAL);
    }

    if ((ret = ff_reget_buffer(avctx, cc->frame)) < 0)
        return ret;
    if (!avctx->frame_number) {
        memset(cc->frame->data[0], 0, cc->frame->linesize[0] * avctx->height);
        memset(cc->frame->data[1], 0, AVPALETTE_SIZE);
    }

    command = bytestream_get_byte(&buf);
    inst    = bytestream_get_byte(&buf);
    inst    &= CDG_MASK;
    buf += 2;  /// skipping 2 unneeded bytes

    if (buf_size > CDG_HEADER_SIZE)
        bytestream_get_buffer(&buf, cdg_data, buf_size - CDG_HEADER_SIZE);

    if ((command & CDG_MASK) == CDG_COMMAND) {
        switch (inst) {
        case CDG_INST_MEMORY_PRESET:
            if (!(cdg_data[1] & 0x0F))
                memset(cc->frame->data[0], cdg_data[0] & 0x0F,
                       cc->frame->linesize[0] * CDG_FULL_HEIGHT);
            break;
        case CDG_INST_LOAD_PAL_LO:
        case CDG_INST_LOAD_PAL_HIGH:
            if (buf_size - CDG_HEADER_SIZE < CDG_DATA_SIZE) {
                av_log(avctx, AV_LOG_ERROR, "buffer too small for loading palette\n");
                return AVERROR(EINVAL);
            }

            cdg_load_palette(cc, cdg_data, inst == CDG_INST_LOAD_PAL_LO);
            break;
        case CDG_INST_BORDER_PRESET:
            cdg_border_preset(cc, cdg_data);
            break;
        case CDG_INST_TILE_BLOCK_XOR:
        case CDG_INST_TILE_BLOCK:
            if (buf_size - CDG_HEADER_SIZE < CDG_DATA_SIZE) {
                av_log(avctx, AV_LOG_ERROR, "buffer too small for drawing tile\n");
                return AVERROR(EINVAL);
            }

            ret = cdg_tile_block(cc, cdg_data, inst == CDG_INST_TILE_BLOCK_XOR);
            if (ret) {
                av_log(avctx, AV_LOG_ERROR, "tile is out of range\n");
                return ret;
            }
            break;
        case CDG_INST_SCROLL_PRESET:
        case CDG_INST_SCROLL_COPY:
            if (buf_size - CDG_HEADER_SIZE < CDG_MINIMUM_SCROLL_SIZE) {
                av_log(avctx, AV_LOG_ERROR, "buffer too small for scrolling\n");
                return AVERROR(EINVAL);
            }

            if ((ret = ff_get_buffer(avctx, frame, AV_GET_BUFFER_FLAG_REF)) < 0)
                return ret;

            cdg_scroll(cc, cdg_data, frame, inst == CDG_INST_SCROLL_COPY);
            av_frame_unref(cc->frame);
            ret = av_frame_ref(cc->frame, frame);
            if (ret < 0)
                return ret;
            break;
        default:
            break;
        }

        if (!frame->data[0]) {
            ret = av_frame_ref(frame, cc->frame);
            if (ret < 0)
                return ret;
        }
        *got_frame = 1;
    } else {
        *got_frame = 0;
        buf_size   = 0;
    }

    return buf_size;
}

static av_cold int cdg_decode_end(AVCodecContext *avctx)
{
    CDGraphicsContext *cc = avctx->priv_data;

    av_frame_free(&cc->frame);

    return 0;
}

AVCodec ff_cdgraphics_decoder = {
    .name           = "cdgraphics",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_CDGRAPHICS,
    .priv_data_size = sizeof(CDGraphicsContext),
    .init           = cdg_decode_init,
    .close          = cdg_decode_end,
    .decode         = cdg_decode_frame,
    .capabilities   = CODEC_CAP_DR1,
    .long_name      = NULL_IF_CONFIG_SMALL("CD Graphics video"),
};
