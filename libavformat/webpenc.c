/*
 * webp muxer
 * Copyright (c) 2014 Michael Niedermayer
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

#include "libavutil/intreadwrite.h"
#include "libavutil/opt.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "internal.h"
#include "mux.h"

typedef struct WebpContext{
    AVClass *class;
    int frame_count;
    AVPacket *last_pkt; /* Not owned by us */
    int loop;
    int wrote_webp_header;
    int using_webp_anim_encoder;
} WebpContext;

static int webp_init(AVFormatContext *s)
{
    WebpContext *const w = s->priv_data;
    AVStream *st = s->streams[0];

    w->last_pkt = ffformatcontext(s)->pkt;

    avpriv_set_pts_info(st, 24, 1, 1000);

    return 0;
}

static int is_animated_webp_packet(AVPacket *pkt)
{
    int skip = 0;
    unsigned flags = 0;

    if (pkt->size < 4)
        return AVERROR_INVALIDDATA;
    if (AV_RL32(pkt->data) == AV_RL32("RIFF"))
        skip = 12;
    // Safe to do this as a valid WebP bitstream is >=30 bytes.
    if (pkt->size < skip + 4)
        return AVERROR_INVALIDDATA;
    if (AV_RL32(pkt->data + skip) == AV_RL32("VP8X")) {
        flags |= pkt->data[skip + 4 + 4];
    }

    if (flags & 2)  // ANIMATION_FLAG is on
        return 1;
    return 0;
}

/**
 * Returns 1 if it has written a RIFF header with a correct length field
 */
static int flush(AVFormatContext *s, int trailer, int64_t pts)
{
    WebpContext *w = s->priv_data;
    AVStream *st = s->streams[0];
    uint8_t buf[12 /* RIFF+WEBP */ + 18 /* VP8X */ +
                14 /* ANIM */ + 24 /* ANMF */], *bufp = buf;
    int writing_webp_header = 0, skip = 0;
    unsigned flags = 0;
    int vp8x = 0;

    if (!w->last_pkt->size)
        return 0;

    if (AV_RL32(w->last_pkt->data) == AV_RL32("RIFF"))
        skip = 12;

    if (AV_RL32(w->last_pkt->data + skip) == AV_RL32("VP8X")) {
        flags |= w->last_pkt->data[skip + 4 + 4];
        vp8x = 1;
        skip += AV_RL32(w->last_pkt->data + skip + 4) + 8;
    }

    if (!w->wrote_webp_header) {
        bytestream_put_le32(&bufp, MKTAG('R', 'I', 'F', 'F'));
        bytestream_put_le32(&bufp, 0); /* Size to be patched later */
        bytestream_put_le32(&bufp, MKTAG('W', 'E', 'B', 'P'));
        writing_webp_header  = 1;
        w->wrote_webp_header = 1;
        if (w->frame_count > 1)  // first non-empty packet
            w->frame_count = 1;  // so we don't count previous empty packets.
    }

    if (w->frame_count == 1) {
        if (!trailer) {
            vp8x = 1;
            flags |= 2 + 16;
        }

        if (vp8x) {
            bytestream_put_le32(&bufp, MKTAG('V', 'P', '8', 'X'));
            bytestream_put_le32(&bufp, 10);
            bytestream_put_byte(&bufp, flags);
            bytestream_put_le24(&bufp, 0);
            bytestream_put_le24(&bufp, st->codecpar->width  - 1);
            bytestream_put_le24(&bufp, st->codecpar->height - 1);
        }
        if (!trailer) {
            bytestream_put_le32(&bufp, MKTAG('A', 'N', 'I', 'M'));
            bytestream_put_le32(&bufp, 6);
            bytestream_put_le32(&bufp, 0xFFFFFFFF);
            bytestream_put_le16(&bufp, w->loop);
        }
    }

    if (w->frame_count > trailer) {
        bytestream_put_le32(&bufp, MKTAG('A', 'N', 'M', 'F'));
        bytestream_put_le32(&bufp, 16 + w->last_pkt->size - skip);
        bytestream_put_le24(&bufp, 0);
        bytestream_put_le24(&bufp, 0);
        bytestream_put_le24(&bufp, st->codecpar->width  - 1);
        bytestream_put_le24(&bufp, st->codecpar->height - 1);
        if (w->last_pkt->pts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE) {
            bytestream_put_le24(&bufp, pts - w->last_pkt->pts);
        } else
            bytestream_put_le24(&bufp, w->last_pkt->duration);
        bytestream_put_byte(&bufp, 0);
    }
    if (trailer && writing_webp_header)
        AV_WL32(buf + 4, bufp - (buf + 8) + w->last_pkt->size - skip);
    avio_write(s->pb, buf, bufp - buf);
    avio_write(s->pb, w->last_pkt->data + skip, w->last_pkt->size - skip);
    av_packet_unref(w->last_pkt);

    return trailer && writing_webp_header;
}

static int webp_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    WebpContext *w = s->priv_data;
    int ret;

    if (!pkt->size)
        return 0;
    ret = is_animated_webp_packet(pkt);
    if (ret < 0)
        return ret;
    w->using_webp_anim_encoder |= ret;

    if (w->using_webp_anim_encoder) {
        avio_write(s->pb, pkt->data, pkt->size);
        w->wrote_webp_header = 1;  // for good measure
    } else {
        int ret;
        if ((ret = flush(s, 0, pkt->pts)) < 0)
            return ret;
        av_packet_ref(w->last_pkt, pkt);
    }
    ++w->frame_count;

    return 0;
}

static int webp_write_trailer(AVFormatContext *s)
{
    unsigned filesize;
    WebpContext *w = s->priv_data;

    if (w->using_webp_anim_encoder) {
        if (w->loop) {  // Write loop count.
            if (avio_seek(s->pb, 42, SEEK_SET) == 42)
                avio_wl16(s->pb, w->loop);
        }
    } else {
        int ret;
        if ((ret = flush(s, 1, AV_NOPTS_VALUE)) < 0)
            return ret;

        if (!ret) {
            filesize = avio_tell(s->pb);
            if (filesize >= 8 && avio_seek(s->pb, 4, SEEK_SET) == 4) {
                avio_wl32(s->pb, filesize - 8);
                // Note: without the following, avio only writes 8 bytes to the file.
                avio_seek(s->pb, filesize, SEEK_SET);
            }
        }
    }

    return 0;
}

#define OFFSET(x) offsetof(WebpContext, x)
#define ENC AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "loop", "Number of times to loop the output: 0 - infinite loop", OFFSET(loop),
      AV_OPT_TYPE_INT, { .i64 = 1 }, 0, 65535, ENC },
    { NULL },
};

static const AVClass webp_muxer_class = {
    .class_name = "WebP muxer",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .option     = options,
};
const FFOutputFormat ff_webp_muxer = {
    .p.name         = "webp",
    .p.long_name    = NULL_IF_CONFIG_SMALL("WebP"),
    .p.extensions   = "webp",
    .priv_data_size = sizeof(WebpContext),
    .p.video_codec  = AV_CODEC_ID_WEBP,
    .p.audio_codec    = AV_CODEC_ID_NONE,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .init           = webp_init,
    .write_packet   = webp_write_packet,
    .write_trailer  = webp_write_trailer,
    .p.priv_class   = &webp_muxer_class,
    .p.flags        = AVFMT_VARIABLE_FPS,
    .flags_internal = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                      FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
};
