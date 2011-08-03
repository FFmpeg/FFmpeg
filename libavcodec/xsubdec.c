/*
 * XSUB subtitle decoder
 * Copyright (c) 2007 Reimar Döffinger
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/mathematics.h"
#include "libavutil/imgutils.h"
#include "avcodec.h"
#include "get_bits.h"
#include "bytestream.h"

static av_cold int decode_init(AVCodecContext *avctx) {
    avctx->pix_fmt = PIX_FMT_PAL8;
    return 0;
}

static const uint8_t tc_offsets[9] = { 0, 1, 3, 4, 6, 7, 9, 10, 11 };
static const uint8_t tc_muls[9] = { 10, 6, 10, 6, 10, 10, 10, 10, 1 };

static int64_t parse_timecode(const uint8_t *buf, int64_t packet_time) {
    int i;
    int64_t ms = 0;
    if (buf[2] != ':' || buf[5] != ':' || buf[8] != '.')
        return AV_NOPTS_VALUE;
    for (i = 0; i < sizeof(tc_offsets); i++) {
        uint8_t c = buf[tc_offsets[i]] - '0';
        if (c > 9) return AV_NOPTS_VALUE;
        ms = (ms + c) * tc_muls[i];
    }
    return ms - packet_time;
}

static int decode_frame(AVCodecContext *avctx, void *data, int *data_size,
                        AVPacket *avpkt) {
    const uint8_t *buf = avpkt->data;
    int buf_size = avpkt->size;
    AVSubtitle *sub = data;
    const uint8_t *buf_end = buf + buf_size;
    uint8_t *bitmap;
    int w, h, x, y, rlelen, i;
    int64_t packet_time = 0;
    GetBitContext gb;

    memset(sub, 0, sizeof(*sub));

    // check that at least header fits
    if (buf_size < 27 + 7 * 2 + 4 * 3) {
        av_log(avctx, AV_LOG_ERROR, "coded frame too small\n");
        return -1;
    }

    // read start and end time
    if (buf[0] != '[' || buf[13] != '-' || buf[26] != ']') {
        av_log(avctx, AV_LOG_ERROR, "invalid time code\n");
        return -1;
    }
    if (avpkt->pts != AV_NOPTS_VALUE)
        packet_time = av_rescale_q(avpkt->pts, AV_TIME_BASE_Q, (AVRational){1, 1000});
    sub->start_display_time = parse_timecode(buf +  1, packet_time);
    sub->end_display_time   = parse_timecode(buf + 14, packet_time);
    buf += 27;

    // read header
    w = bytestream_get_le16(&buf);
    h = bytestream_get_le16(&buf);
    if (av_image_check_size(w, h, 0, avctx) < 0)
        return -1;
    x = bytestream_get_le16(&buf);
    y = bytestream_get_le16(&buf);
    // skip bottom right position, it gives no new information
    bytestream_get_le16(&buf);
    bytestream_get_le16(&buf);
    rlelen = bytestream_get_le16(&buf);

    // allocate sub and set values
    sub->rects =  av_mallocz(sizeof(*sub->rects));
    sub->rects[0] = av_mallocz(sizeof(*sub->rects[0]));
    sub->num_rects = 1;
    sub->rects[0]->x = x; sub->rects[0]->y = y;
    sub->rects[0]->w = w; sub->rects[0]->h = h;
    sub->rects[0]->type = SUBTITLE_BITMAP;
    sub->rects[0]->pict.linesize[0] = w;
    sub->rects[0]->pict.data[0] = av_malloc(w * h);
    sub->rects[0]->nb_colors = 4;
    sub->rects[0]->pict.data[1] = av_mallocz(AVPALETTE_SIZE);

    // read palette
    for (i = 0; i < sub->rects[0]->nb_colors; i++)
        ((uint32_t*)sub->rects[0]->pict.data[1])[i] = bytestream_get_be24(&buf);
    // make all except background (first entry) non-transparent
    for (i = 1; i < sub->rects[0]->nb_colors; i++)
        ((uint32_t*)sub->rects[0]->pict.data[1])[i] |= 0xff000000;

    // process RLE-compressed data
    rlelen = FFMIN(rlelen, buf_end - buf);
    init_get_bits(&gb, buf, rlelen * 8);
    bitmap = sub->rects[0]->pict.data[0];
    for (y = 0; y < h; y++) {
        // interlaced: do odd lines
        if (y == (h + 1) / 2) bitmap = sub->rects[0]->pict.data[0] + w;
        for (x = 0; x < w; ) {
            int log2 = ff_log2_tab[show_bits(&gb, 8)];
            int run = get_bits(&gb, 14 - 4 * (log2 >> 1));
            int color = get_bits(&gb, 2);
            run = FFMIN(run, w - x);
            // run length 0 means till end of row
            if (!run) run = w - x;
            memset(bitmap, color, run);
            bitmap += run;
            x += run;
        }
        // interlaced, skip every second line
        bitmap += w;
        align_get_bits(&gb);
    }
    *data_size = 1;
    return buf_size;
}

AVCodec ff_xsub_decoder = {
    .name      = "xsub",
    .type      = AVMEDIA_TYPE_SUBTITLE,
    .id        = CODEC_ID_XSUB,
    .init      = decode_init,
    .decode    = decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("XSUB"),
};
