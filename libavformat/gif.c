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
#include "libavutil/avassert.h"
#include "libavutil/imgutils.h"
#include "libavutil/log.h"
#include "libavutil/opt.h"

static int gif_image_write_header(AVFormatContext *s, int width, int height,
                                  int loop_count, uint32_t *palette)
{
    AVIOContext *pb = s->pb;
    AVRational sar = s->streams[0]->codec->sample_aspect_ratio;
    int i, aspect = 0;

    if (sar.num > 0 && sar.den > 0) {
        aspect = sar.num * 64 / sar.den - 15;
        if (aspect < 0 || aspect > 255)
            aspect = 0;
    }

    avio_write(pb, "GIF", 3);
    avio_write(pb, "89a", 3);
    avio_wl16(pb, width);
    avio_wl16(pb, height);

    if (palette) {
        avio_w8(pb, 0xf7); /* flags: global clut, 256 entries */
        avio_w8(pb, 0x1f); /* background color index */
        avio_w8(pb, aspect);
        for (i = 0; i < 256; i++) {
            const uint32_t v = palette[i] & 0xffffff;
            avio_wb24(pb, v);
        }
    } else {
        avio_w8(pb, 0); /* flags */
        avio_w8(pb, 0); /* background color index */
        avio_w8(pb, aspect);
    }


    if (loop_count >= 0 ) {
        /* "NETSCAPE EXTENSION" for looped animation GIF */
        avio_w8(pb, 0x21); /* GIF Extension code */
        avio_w8(pb, 0xff); /* Application Extension Label */
        avio_w8(pb, 0x0b); /* Length of Application Block */
        avio_write(pb, "NETSCAPE2.0", sizeof("NETSCAPE2.0") - 1);
        avio_w8(pb, 0x03); /* Length of Data Sub-Block */
        avio_w8(pb, 0x01);
        avio_wl16(pb, (uint16_t)loop_count);
        avio_w8(pb, 0x00); /* Data Sub-block Terminator */
    }

    return 0;
}

typedef struct {
    AVClass *class;
    int loop;
    int last_delay;
    AVPacket *prev_pkt;
    int duration;
} GIFContext;

static int gif_write_header(AVFormatContext *s)
{
    GIFContext *gif = s->priv_data;
    AVCodecContext *video_enc;
    int width, height;
    uint32_t palette[AVPALETTE_COUNT];

    if (s->nb_streams != 1 ||
        s->streams[0]->codec->codec_type != AVMEDIA_TYPE_VIDEO ||
        s->streams[0]->codec->codec_id   != AV_CODEC_ID_GIF) {
        av_log(s, AV_LOG_ERROR,
               "GIF muxer supports only a single video GIF stream.\n");
        return AVERROR(EINVAL);
    }

    video_enc = s->streams[0]->codec;
    width  = video_enc->width;
    height = video_enc->height;

    avpriv_set_pts_info(s->streams[0], 64, 1, 100);
    if (avpriv_set_systematic_pal2(palette, video_enc->pix_fmt) < 0) {
        av_assert0(video_enc->pix_fmt == AV_PIX_FMT_PAL8);
        gif_image_write_header(s, width, height, gif->loop, NULL);
    } else {
        gif_image_write_header(s, width, height, gif->loop, palette);
    }

    avio_flush(s->pb);
    return 0;
}

static int flush_packet(AVFormatContext *s, AVPacket *new)
{
    GIFContext *gif = s->priv_data;
    int size;
    AVIOContext *pb = s->pb;
    uint8_t flags = 0x4, transparent_color_index = 0x1f;
    const uint32_t *palette;
    AVPacket *pkt = gif->prev_pkt;

    if (!pkt)
        return 0;

    /* Mark one colour as transparent if the input palette contains at least
     * one colour that is more than 50% transparent. */
    palette = (uint32_t*)av_packet_get_side_data(pkt, AV_PKT_DATA_PALETTE, &size);
    if (palette && size != AVPALETTE_SIZE) {
        av_log(s, AV_LOG_ERROR, "Invalid palette extradata\n");
        return AVERROR_INVALIDDATA;
    }
    if (palette) {
        unsigned i, smallest_alpha = 0xff;

        for (i = 0; i < AVPALETTE_COUNT; i++) {
            const uint32_t v = palette[i];
            if (v >> 24 < smallest_alpha) {
                smallest_alpha = v >> 24;
                transparent_color_index = i;
            }
        }
        if (smallest_alpha < 128)
            flags |= 0x1; /* Transparent Color Flag */
    }

    if (new && new->pts != AV_NOPTS_VALUE)
        gif->duration = av_clip_uint16(new->pts - gif->prev_pkt->pts);
    else if (!new && gif->last_delay >= 0)
        gif->duration = gif->last_delay;

    /* graphic control extension block */
    avio_w8(pb, 0x21);
    avio_w8(pb, 0xf9);
    avio_w8(pb, 0x04); /* block size */
    avio_w8(pb, flags);
    avio_wl16(pb, gif->duration);
    avio_w8(pb, transparent_color_index);
    avio_w8(pb, 0x00);

    avio_write(pb, pkt->data, pkt->size);

    av_free_packet(gif->prev_pkt);
    if (new)
        av_copy_packet(gif->prev_pkt, new);

    return 0;
}

static int gif_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    GIFContext *gif = s->priv_data;

    if (!gif->prev_pkt) {
        gif->prev_pkt = av_malloc(sizeof(*gif->prev_pkt));
        if (!gif->prev_pkt)
            return AVERROR(ENOMEM);
        return av_copy_packet(gif->prev_pkt, pkt);
    }
    return flush_packet(s, pkt);
}

static int gif_write_trailer(AVFormatContext *s)
{
    GIFContext *gif = s->priv_data;
    AVIOContext *pb = s->pb;

    flush_packet(s, NULL);
    av_freep(&gif->prev_pkt);
    avio_w8(pb, 0x3b);

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

AVOutputFormat ff_gif_muxer = {
    .name           = "gif",
    .long_name      = NULL_IF_CONFIG_SMALL("GIF Animation"),
    .mime_type      = "image/gif",
    .extensions     = "gif",
    .priv_data_size = sizeof(GIFContext),
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_GIF,
    .write_header   = gif_write_header,
    .write_packet   = gif_write_packet,
    .write_trailer  = gif_write_trailer,
    .priv_class     = &gif_muxer_class,
    .flags          = AVFMT_VARIABLE_FPS,
};
