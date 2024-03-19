/*
 * Animated GIF muxer
 * Copyright (c) 2000 Fabrice Bellard
 *
 * first version by Francois Revol <revol@free.fr>
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

#include "avformat.h"
#include "internal.h"
#include "mux.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/gif.h"

typedef struct GIFContext {
    AVClass *class;
    int loop;
    int last_delay;
    int duration;
    int64_t last_pos;
    int have_end;
    AVPacket *prev_pkt;
} GIFContext;

static av_cold int gif_init(AVFormatContext *s)
{
    avpriv_set_pts_info(s->streams[0], 64, 1, 100);

    return 0;
}

static int gif_parse_packet(AVFormatContext *s, const uint8_t *data, int size)
{
    GetByteContext gb;
    int x;

    bytestream2_init(&gb, data, size);

    while (bytestream2_get_bytes_left(&gb) > 0) {
        x = bytestream2_get_byte(&gb);
        if (x != GIF_EXTENSION_INTRODUCER)
            return 0;

        x = bytestream2_get_byte(&gb);
        while (x != GIF_GCE_EXT_LABEL && bytestream2_get_bytes_left(&gb) > 0) {
            int block_size = bytestream2_get_byte(&gb);
            if (!block_size)
                break;
            bytestream2_skip(&gb, block_size);
        }

        if (x == GIF_GCE_EXT_LABEL)
            return bytestream2_tell(&gb) + 2;
    }

    return 0;
}

static int gif_get_delay(GIFContext *gif, AVPacket *prev, AVPacket *new)
{
    if (new && new->pts != AV_NOPTS_VALUE)
        gif->duration = av_clip_uint16(new->pts - prev->pts);
    else if (!new && gif->last_delay >= 0)
        gif->duration = gif->last_delay;
    else if (prev->duration)
        gif->duration = prev->duration;

    return gif->duration;
}

static int gif_write_packet(AVFormatContext *s, AVPacket *new_pkt)
{
    GIFContext *gif = s->priv_data;
    AVIOContext *pb = s->pb;
    AVPacket *pkt = gif->prev_pkt;

    if (!gif->prev_pkt) {
        gif->prev_pkt = av_packet_alloc();
        if (!gif->prev_pkt)
            return AVERROR(ENOMEM);
        return av_packet_ref(gif->prev_pkt, new_pkt);
    }

    gif->last_pos = avio_tell(pb);
    if (pkt->size > 0)
        gif->have_end = pkt->data[pkt->size - 1] == GIF_TRAILER;

    if (!gif->last_pos) {
        int delay_pos;
        int off = 13;

        if (pkt->size < 13)
            return AVERROR(EINVAL);

        if (pkt->data[10] & 0x80)
            off += 3 * (1 << ((pkt->data[10] & 0x07) + 1));

        if (pkt->size < off + 2)
            return AVERROR(EINVAL);

        avio_write(pb, pkt->data, off);

        if (pkt->data[off] == GIF_EXTENSION_INTRODUCER && pkt->data[off + 1] == 0xff)
            off += 19;

        if (pkt->size <= off)
            return AVERROR(EINVAL);

        /* "NETSCAPE EXTENSION" for looped animation GIF */
        if (gif->loop >= 0) {
            avio_w8(pb, GIF_EXTENSION_INTRODUCER); /* GIF Extension code */
            avio_w8(pb, GIF_APP_EXT_LABEL); /* Application Extension Label */
            avio_w8(pb, 0x0b); /* Length of Application Block */
            avio_write(pb, "NETSCAPE2.0", sizeof("NETSCAPE2.0") - 1);
            avio_w8(pb, 0x03); /* Length of Data Sub-Block */
            avio_w8(pb, 0x01);
            avio_wl16(pb, (uint16_t)gif->loop);
            avio_w8(pb, 0x00); /* Data Sub-block Terminator */
        }

        delay_pos = gif_parse_packet(s, pkt->data + off, pkt->size - off);
        if (delay_pos > 0 && delay_pos < pkt->size - off - 2) {
            avio_write(pb, pkt->data + off, delay_pos);
            avio_wl16(pb, gif_get_delay(gif, pkt, new_pkt));
            avio_write(pb, pkt->data + off + delay_pos + 2, pkt->size - off - delay_pos - 2);
        } else {
            avio_write(pb, pkt->data + off, pkt->size - off);
        }
    } else {
        int delay_pos = gif_parse_packet(s, pkt->data, pkt->size);

        if (delay_pos > 0 && delay_pos < pkt->size - 2) {
            avio_write(pb, pkt->data, delay_pos);
            avio_wl16(pb, gif_get_delay(gif, pkt, new_pkt));
            avio_write(pb, pkt->data + delay_pos + 2, pkt->size - delay_pos - 2);
        } else {
            avio_write(pb, pkt->data, pkt->size);
        }
    }

    av_packet_unref(gif->prev_pkt);
    if (new_pkt)
        return av_packet_ref(gif->prev_pkt, new_pkt);

    return 0;
}

static int gif_write_trailer(AVFormatContext *s)
{
    GIFContext *gif = s->priv_data;
    AVIOContext *pb = s->pb;

    if (!gif->prev_pkt)
        return AVERROR(EINVAL);

    gif_write_packet(s, NULL);

    if (!gif->have_end)
        avio_w8(pb, GIF_TRAILER);
    av_packet_free(&gif->prev_pkt);

    return 0;
}

#define OFFSET(x) offsetof(GIFContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "loop", "Number of times to loop the output: -1 - no loop, 0 - infinite loop", OFFSET(loop),
      AV_OPT_TYPE_INT, { .i64 = 0 }, -1, 65535, ENC },
    { "final_delay", "Force delay (in centiseconds) after the last frame", OFFSET(last_delay),
      AV_OPT_TYPE_INT, { .i64 = -1 }, -1, 65535, ENC },
    { NULL },
};

static const AVClass gif_muxer_class = {
    .class_name = "GIF muxer",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .option     = options,
};

const FFOutputFormat ff_gif_muxer = {
    .p.name         = "gif",
    .p.long_name    = NULL_IF_CONFIG_SMALL("CompuServe Graphics Interchange Format (GIF)"),
    .p.mime_type    = "image/gif",
    .p.extensions   = "gif",
    .priv_data_size = sizeof(GIFContext),
    .p.audio_codec  = AV_CODEC_ID_NONE,
    .p.video_codec  = AV_CODEC_ID_GIF,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .flags_internal   = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                        FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .init             = gif_init,
    .write_packet   = gif_write_packet,
    .write_trailer  = gif_write_trailer,
    .p.priv_class   = &gif_muxer_class,
    .p.flags        = AVFMT_VARIABLE_FPS,
};
