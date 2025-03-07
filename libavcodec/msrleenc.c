/*
 * Copyright (c) 2023 Tomas Härdin
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
 * MSRLE encoder
 * @see https://wiki.multimedia.cx/index.php?title=Microsoft_RLE
 */

// TODO: pal4 mode?

#include "bytestream.h"
#include "codec_internal.h"
#include "encode.h"

typedef struct MSRLEContext {
    int curframe;
    AVFrame *last_frame;
} MSRLEContext;

static av_cold int msrle_encode_init(AVCodecContext *avctx)
{
    MSRLEContext *s = avctx->priv_data;

    avctx->bits_per_coded_sample = 8;
    s->last_frame = av_frame_alloc();
    if (!s->last_frame)
        return AVERROR(ENOMEM);

    return 0;
}

static void write_run(AVCodecContext *avctx, uint8_t **data, int len, int value)
{
    // we're allowed to write odd runs
    while (len >= 255) {
        bytestream_put_byte(data, 255);
        bytestream_put_byte(data, value);
        len -= 255;
    }
    if (len >= 1) {
        // this is wasteful when len == 1 and sometimes when len == 2
        // but sometimes we have no choice. also write_absolute()
        // relies on this
        bytestream_put_byte(data, len);
        bytestream_put_byte(data, value);
    }
}

static void write_absolute(AVCodecContext *avctx, uint8_t **data,
                           const uint8_t *line, int len)
{
    // writing 255 would be wasteful here due to the padding requirement
    while (len >= 254) {
        bytestream_put_byte(data, 0);
        bytestream_put_byte(data, 254);
        bytestream_put_buffer(data, line, 254);
        line += 254;
        len -= 254;
    }
    if (len == 1) {
        // it's less wasteful to write single pixels as runs
        // not to mention that absolute mode requires >= 3 pixels
        write_run(avctx, data, 1, line[0]);
    } else if (len == 2) {
        write_run(avctx, data, 1, line[0]);
        write_run(avctx, data, 1, line[1]);
    } else if (len > 0) {
        bytestream_put_byte(data, 0);
        bytestream_put_byte(data, len);
        bytestream_put_buffer(data, line, len);
        if (len & 1)
            bytestream_put_byte(data, 0);
    }
}

static void write_delta(AVCodecContext *avctx, uint8_t **data, int delta)
{
    // we let the yskip logic handle the case where we want to delta
    // to following lines. it's not perfect but it's easier than finding
    // the optimal combination of end-of-lines and deltas to reach any
    // following position including places where dx < 0
    while (delta >= 255) {
        bytestream_put_byte(data, 0);
        bytestream_put_byte(data, 2);
        bytestream_put_byte(data, 255);
        bytestream_put_byte(data, 0);
        delta -= 255;
    }
    if (delta > 0) {
        bytestream_put_byte(data, 0);
        bytestream_put_byte(data, 2);
        bytestream_put_byte(data, delta);
        bytestream_put_byte(data, 0);
    }
}

static void write_yskip(AVCodecContext *avctx, uint8_t **data, int yskip)
{
    if (yskip < 4)
        return;
    // we have yskip*2 nul bytess
    *data -= 2*yskip;
    // the end-of-line counts as one skip
    yskip--;
    while (yskip >= 255) {
        bytestream_put_byte(data, 0);
        bytestream_put_byte(data, 2);
        bytestream_put_byte(data, 0);
        bytestream_put_byte(data, 255);
        yskip -= 255;
    }
    if (yskip > 0) {
        bytestream_put_byte(data, 0);
        bytestream_put_byte(data, 2);
        bytestream_put_byte(data, 0);
        bytestream_put_byte(data, yskip);
    }
    bytestream_put_be16(data, 0x0000);
}

// used both to encode lines in keyframes and to encode lines between deltas
static void encode_line(AVCodecContext *avctx, uint8_t **data,
                        const uint8_t *line, int length)
{
    int run = 0, last = -1, absstart = 0;
    if (length == 0)
        return;
    for (int x = 0; x < length; x++) {
        if (last == line[x]) {
            run++;
            if (run == 3)
                write_absolute(avctx, data, &line[absstart], x - absstart - 2);
        } else {
            if (run >= 3) {
                write_run(avctx, data, run, last);
                absstart = x;
            }
            run = 1;
        }
        last = line[x];
    }
    if (run >= 3)
        write_run(avctx, data, run, last);
    else
        write_absolute(avctx, data, &line[absstart], length - absstart);
}

static int encode(AVCodecContext *avctx, AVPacket *pkt,
                  const AVFrame *pict, int keyframe, int *got_keyframe)
{
    MSRLEContext *s = avctx->priv_data;
    uint8_t *data = pkt->data;

    /*  Compare the current frame to the last frame, or code the entire frame
        if keyframe != 0. We're continually outputting pairs of bytes:

            00 00           end of line
            00 01           end of bitmap
            00 02 dx dy     delta. move pointer to x+dx, y+dy
            00 ll dd dd ..  absolute (verbatim) mode. ll >= 3
            rr dd           run. rr >= 1

        For keyframes we only have absolute mode and runs at our disposal, and
        we are not allowed to end a line early. If this happens when keyframe == 0
        then *got_keyframe is set to 1 and s->curframe is reset.
    */
    *got_keyframe = 1;  // set to zero whenever we use a feature that makes this a not-keyframe

    if (keyframe) {
        for (int y = avctx->height-1; y >= 0; y--) {
            uint8_t *line = &pict->data[0][y*pict->linesize[0]];
            encode_line(avctx, &data, line, avctx->width);
            bytestream_put_be16(&data, 0x0000); // end of line
        }
    } else {
        // compare to previous frame
        int yskip = 0; // we can encode large skips using deltas
        for (int y = avctx->height-1; y >= 0; y--) {
            const uint8_t *line = &pict->data[0][y*pict->linesize[0]];
            const uint8_t *prev = &s->last_frame->data[0][y*s->last_frame->linesize[0]];
            // we need at least 5 pixels in a row for a delta to be worthwhile
            int delta = 0, linestart = 0, encoded = 0;
            for (int x = 0; x < avctx->width; x++) {
                if (line[x] == prev[x]) {
                    delta++;
                    if (delta == 5) {
                        int len = x - linestart - 4;
                        if (len > 0) {
                            write_yskip(avctx, &data, yskip);
                            yskip = 0;
                            encode_line(avctx, &data, &line[linestart], len);
                            encoded = 1;
                        }
                        linestart = -1;
                    }
                } else {
                    if (delta >= 5) {
                        write_yskip(avctx, &data, yskip);
                        yskip = 0;
                        write_delta(avctx, &data, delta);
                        *got_keyframe = 0;
                        encoded = 1;
                    }
                    delta = 0;
                    if (linestart == -1)
                        linestart = x;
                }
            }
            if (delta < 5) {
                write_yskip(avctx, &data, yskip);
                yskip = 0;
                encode_line(avctx, &data, &line[linestart], avctx->width - linestart);
                encoded  = 1;
            } else
                *got_keyframe = 0;
            bytestream_put_be16(&data, 0x0000); // end of line
            if (!encoded)
                yskip++;
            else
                yskip = 0;
        }
        write_yskip(avctx, &data, yskip);
    }
    bytestream_put_be16(&data, 0x0001); // end of bitmap
    pkt->size = data - pkt->data;
    return 0;
}

static int msrle_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                              const AVFrame *pict, int *got_packet)
{
    MSRLEContext *s = avctx->priv_data;
    int ret, got_keyframe;

    if ((ret = ff_alloc_packet(avctx, pkt, (
                avctx->width*2 /* worst case = rle every pixel */ + 2 /*end of line */
            ) * avctx->height + 2 /* end of bitmap */ + FF_INPUT_BUFFER_MIN_SIZE)))
        return ret;

    if (pict->data[1]) {
        uint8_t *side_data = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE, AVPALETTE_SIZE);
        if (!side_data)
            return AVERROR(ENOMEM);
        memcpy(side_data, pict->data[1], AVPALETTE_SIZE);
    }

    if ((ret = encode(avctx, pkt, pict, s->curframe == 0, &got_keyframe)))
        return ret;

    if (got_keyframe) {
        pkt->flags |= AV_PKT_FLAG_KEY;
        s->curframe = 0;
    }
    if (++s->curframe >= avctx->gop_size)
        s->curframe = 0;
    *got_packet = 1;

    return av_frame_replace(s->last_frame, pict);
}

static int msrle_encode_close(AVCodecContext *avctx)
{
    MSRLEContext *s = avctx->priv_data;
    av_frame_free(&s->last_frame);
    return 0;
}

const FFCodec ff_msrle_encoder = {
    .p.name         = "msrle",
    CODEC_LONG_NAME("Microsoft RLE"),
    .p.type         = AVMEDIA_TYPE_VIDEO,
    .p.id           = AV_CODEC_ID_MSRLE,
    .p.capabilities = AV_CODEC_CAP_DR1,
    .priv_data_size = sizeof(MSRLEContext),
    .init           = msrle_encode_init,
    FF_CODEC_ENCODE_CB(msrle_encode_frame),
    .close          = msrle_encode_close,
    CODEC_PIXFMTS(AV_PIX_FMT_PAL8),
    .caps_internal  = FF_CODEC_CAP_INIT_CLEANUP,
};
