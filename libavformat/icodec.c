/*
 * Microsoft Windows ICO demuxer
 * Copyright (c) 2011 Peter Ross (pross@xvid.org)
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
 * Microsoft Windows ICO demuxer
 */

#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/png.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

typedef struct {
    int offset;
    int size;
    int nb_pal;
} IcoImage;

typedef struct {
    int current_image;
    int nb_images;
    IcoImage * images;
} IcoDemuxContext;

static int probe(const AVProbeData *p)
{
    unsigned i, frames, checked = 0;

    if (p->buf_size < 22 || AV_RL16(p->buf) || AV_RL16(p->buf + 2) != 1)
        return 0;
    frames = AV_RL16(p->buf + 4);
    if (!frames)
        return 0;
    for (i = 0; i < frames && i * 16 + 22 <= p->buf_size; i++) {
        unsigned offset;
        if (AV_RL16(p->buf + 10 + i * 16) & ~1)
            return FFMIN(i, AVPROBE_SCORE_MAX / 4);
        if (p->buf[13 + i * 16])
            return FFMIN(i, AVPROBE_SCORE_MAX / 4);
        if (AV_RL32(p->buf + 14 + i * 16) < 40)
            return FFMIN(i, AVPROBE_SCORE_MAX / 4);
        offset = AV_RL32(p->buf + 18 + i * 16);
        if (offset < 22)
            return FFMIN(i, AVPROBE_SCORE_MAX / 4);
        if (offset > p->buf_size - 8)
            continue;
        if (p->buf[offset] != 40 && AV_RB64(p->buf + offset) != PNGSIG)
            return FFMIN(i, AVPROBE_SCORE_MAX / 4);
        checked++;
    }

    if (checked < frames)
        return AVPROBE_SCORE_MAX / 4 + FFMIN(checked, 1);
    return AVPROBE_SCORE_MAX / 2 + 1;
}

static int read_header(AVFormatContext *s)
{
    IcoDemuxContext *ico = s->priv_data;
    AVIOContext *pb = s->pb;
    int i, codec;

    avio_skip(pb, 4);
    ico->nb_images = avio_rl16(pb);

    if (!ico->nb_images)
        return AVERROR_INVALIDDATA;

    ico->images = av_malloc_array(ico->nb_images, sizeof(IcoImage));
    if (!ico->images)
        return AVERROR(ENOMEM);

    for (i = 0; i < ico->nb_images; i++) {
        AVStream *st;
        int tmp;

        if (avio_seek(pb, 6 + i * 16, SEEK_SET) < 0)
            return AVERROR_INVALIDDATA;

        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->width      = avio_r8(pb);
        st->codecpar->height     = avio_r8(pb);
        ico->images[i].nb_pal = avio_r8(pb);
        if (ico->images[i].nb_pal == 255)
            ico->images[i].nb_pal = 0;

        avio_skip(pb, 5);

        ico->images[i].size   = avio_rl32(pb);
        if (ico->images[i].size <= 0) {
            av_log(s, AV_LOG_ERROR, "Invalid image size %d\n", ico->images[i].size);
            return AVERROR_INVALIDDATA;
        }
        ico->images[i].offset = avio_rl32(pb);

        if (avio_seek(pb, ico->images[i].offset, SEEK_SET) < 0)
            return AVERROR_INVALIDDATA;

        codec = avio_rl32(pb);
        switch (codec) {
        case MKTAG(0x89, 'P', 'N', 'G'):
            st->codecpar->codec_id = AV_CODEC_ID_PNG;
            st->codecpar->width    = 0;
            st->codecpar->height   = 0;
            break;
        case 40:
            if (ico->images[i].size < 40)
                return AVERROR_INVALIDDATA;
            st->codecpar->codec_id = AV_CODEC_ID_BMP;
            tmp = avio_rl32(pb);
            if (tmp)
                st->codecpar->width = tmp;
            tmp = avio_rl32(pb);
            if (tmp)
                st->codecpar->height = tmp / 2;
            break;
        default:
            avpriv_request_sample(s, "codec %d", codec);
            return AVERROR_INVALIDDATA;
        }
    }

    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    IcoDemuxContext *ico = s->priv_data;
    IcoImage *image;
    AVIOContext *pb = s->pb;
    AVStream *st;
    int ret;

    if (ico->current_image >= ico->nb_images)
        return AVERROR_EOF;

    st = s->streams[0];

    image = &ico->images[ico->current_image];

    if ((ret = avio_seek(pb, image->offset, SEEK_SET)) < 0)
        return ret;

    if (s->streams[ico->current_image]->codecpar->codec_id == AV_CODEC_ID_PNG) {
        if ((ret = av_get_packet(pb, pkt, image->size)) < 0)
            return ret;
    } else {
        uint8_t *buf;
        if ((ret = av_new_packet(pkt, 14 + image->size)) < 0)
            return ret;
        buf = pkt->data;

        /* add BMP header */
        bytestream_put_byte(&buf, 'B');
        bytestream_put_byte(&buf, 'M');
        bytestream_put_le32(&buf, pkt->size);
        bytestream_put_le16(&buf, 0);
        bytestream_put_le16(&buf, 0);
        bytestream_put_le32(&buf, 0);

        if ((ret = avio_read(pb, buf, image->size)) != image->size) {
            return ret < 0 ? ret : AVERROR_INVALIDDATA;
        }

        st->codecpar->bits_per_coded_sample = AV_RL16(buf + 14);

        if (AV_RL32(buf + 32))
            image->nb_pal = AV_RL32(buf + 32);

        if (st->codecpar->bits_per_coded_sample <= 8 && !image->nb_pal) {
            image->nb_pal = 1 << st->codecpar->bits_per_coded_sample;
            AV_WL32(buf + 32, image->nb_pal);
        }

        if (image->nb_pal > INT_MAX / 4 - 14 - 40)
            return AVERROR_INVALIDDATA;

        AV_WL32(buf - 4, 14 + 40 + image->nb_pal * 4);
        AV_WL32(buf + 8, AV_RL32(buf + 8) / 2);
    }

    pkt->stream_index = ico->current_image++;
    pkt->flags |= AV_PKT_FLAG_KEY;

    return 0;
}

static int ico_read_close(AVFormatContext * s)
{
    IcoDemuxContext *ico = s->priv_data;
    av_freep(&ico->images);
    return 0;
}

const FFInputFormat ff_ico_demuxer = {
    .p.name         = "ico",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Microsoft Windows ICO"),
    .p.flags        = AVFMT_NOTIMESTAMPS,
    .priv_data_size = sizeof(IcoDemuxContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .read_close     = ico_read_close,
};
