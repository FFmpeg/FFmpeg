/*
 * Bethesda VID video decoder
 * Copyright (C) 2007 Nicholas Tung
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
 * @file bethsoftvideo.c
 * @brief Bethesda Softworks VID Video Decoder
 * @author Nicholas Tung [ntung (at. ntung com] (2007-03)
 * @sa http://wiki.multimedia.cx/index.php?title=Bethsoft_VID
 * @sa http://www.svatopluk.com/andux/docs/dfvid.html
 */

#include "common.h"
#include "dsputil.h"
#include "bethsoftvideo.h"
#include "bytestream.h"

typedef struct BethsoftvidContext {
    AVFrame frame;
} BethsoftvidContext;

static av_cold int bethsoftvid_decode_init(AVCodecContext *avctx)
{
    BethsoftvidContext *vid = avctx->priv_data;
    vid->frame.reference = 1;
    vid->frame.buffer_hints = FF_BUFFER_HINTS_VALID |
        FF_BUFFER_HINTS_PRESERVE | FF_BUFFER_HINTS_REUSABLE;
    avctx->pix_fmt = PIX_FMT_PAL8;
    return 0;
}

static void set_palette(AVFrame * frame, const uint8_t * palette_buffer)
{
    uint32_t * palette = (uint32_t *)frame->data[1];
    int a;
    for(a = 0; a < 256; a++){
        palette[a] = AV_RB24(&palette_buffer[a * 3]) * 4;
    }
    frame->palette_has_changed = 1;
}

static int bethsoftvid_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              const uint8_t *buf, int buf_size)
{
    BethsoftvidContext * vid = avctx->priv_data;
    char block_type;
    uint8_t * dst;
    uint8_t * frame_end;
    int remaining = avctx->width;          // number of bytes remaining on a line
    const int wrap_to_next_line = vid->frame.linesize[0] - avctx->width;
    int code;
    int yoffset;

    if (avctx->reget_buffer(avctx, &vid->frame)) {
        av_log(avctx, AV_LOG_ERROR, "reget_buffer() failed\n");
        return -1;
    }
    dst = vid->frame.data[0];
    frame_end = vid->frame.data[0] + vid->frame.linesize[0] * avctx->height;

    switch(block_type = *buf++){
        case PALETTE_BLOCK:
            set_palette(&vid->frame, buf);
            return 0;
        case VIDEO_YOFF_P_FRAME:
            yoffset = bytestream_get_le16(&buf);
            if(yoffset >= avctx->height)
                return -1;
            dst += vid->frame.linesize[0] * yoffset;
    }

    // main code
    while((code = *buf++)){
        int length = code & 0x7f;

        // copy any bytes starting at the current position, and ending at the frame width
        while(length > remaining){
            if(code < 0x80)
                bytestream_get_buffer(&buf, dst, remaining);
            else if(block_type == VIDEO_I_FRAME)
                memset(dst, buf[0], remaining);
            length -= remaining;      // decrement the number of bytes to be copied
            dst += remaining + wrap_to_next_line;    // skip over extra bytes at end of frame
            remaining = avctx->width;
            if(dst == frame_end)
                goto end;
        }

        // copy any remaining bytes after / if line overflows
        if(code < 0x80)
            bytestream_get_buffer(&buf, dst, length);
        else if(block_type == VIDEO_I_FRAME)
            memset(dst, *buf++, length);
        remaining -= length;
        dst += length;
    }
    end:

    *data_size = sizeof(AVFrame);
    *(AVFrame*)data = vid->frame;

    return buf_size;
}

static av_cold int bethsoftvid_decode_end(AVCodecContext *avctx)
{
    BethsoftvidContext * vid = avctx->priv_data;
    if(vid->frame.data[0])
        avctx->release_buffer(avctx, &vid->frame);
    return 0;
}

AVCodec bethsoftvid_decoder = {
    .name = "bethsoftvid",
    .type = CODEC_TYPE_VIDEO,
    .id = CODEC_ID_BETHSOFTVID,
    .priv_data_size = sizeof(BethsoftvidContext),
    .init = bethsoftvid_decode_init,
    .close = bethsoftvid_decode_end,
    .decode = bethsoftvid_decode_frame,
};
