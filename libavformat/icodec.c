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
#include "libavcodec/bytestream.h"
#include "libavcodec/bmp.h"
#include "avformat.h"
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

static int probe(AVProbeData *p)
{
    if (AV_RL16(p->buf) == 0 && AV_RL16(p->buf + 2) == 1 && AV_RL16(p->buf + 4))
        return AVPROBE_SCORE_MAX / 3;
    return 0;
}

static int read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    IcoDemuxContext *ico = s->priv_data;
    AVIOContext *pb = s->pb;
    int i;

    avio_skip(pb, 4);
    ico->nb_images = avio_rl16(pb);

    ico->images = av_malloc(ico->nb_images * sizeof(IcoImage));
    if (!ico->images)
        return AVERROR(ENOMEM);

    for (i = 0; i < ico->nb_images; i++) {
        AVStream *st;
        int tmp;

        if (avio_seek(pb, 6 + i * 16, SEEK_SET) < 0)
            break;

        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);

        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->width      = avio_r8(pb);
        st->codec->height     = avio_r8(pb);
        ico->images[i].nb_pal = avio_r8(pb);
        if (ico->images[i].nb_pal == 255)
            ico->images[i].nb_pal = 0;

        avio_skip(pb, 5);

        ico->images[i].size   = avio_rl32(pb);
        ico->images[i].offset = avio_rl32(pb);

        if (avio_seek(pb, ico->images[i].offset, SEEK_SET) < 0)
            break;

        switch(avio_rl32(pb)) {
        case MKTAG(0x89, 'P', 'N', 'G'):
            st->codec->codec_id = CODEC_ID_PNG;
            st->codec->width    = 0;
            st->codec->height   = 0;
            break;
        case 40:
            if (ico->images[i].size < 40)
                return AVERROR_INVALIDDATA;
            st->codec->codec_id = CODEC_ID_BMP;
            tmp = avio_rl32(pb);
            if (tmp)
                st->codec->width = tmp;
            tmp = avio_rl32(pb);
            if (tmp)
                st->codec->height = tmp / 2;
            break;
        default:
            av_log_ask_for_sample(s, "unsupported codec\n");
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
    AVStream *st = s->streams[0];
    int ret;

    if (ico->current_image >= ico->nb_images)
        return AVERROR(EIO);

    image = &ico->images[ico->current_image];

    if ((ret = avio_seek(pb, image->offset, SEEK_SET)) < 0)
        return ret;

    if (s->streams[ico->current_image]->codec->codec_id == CODEC_ID_PNG) {
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

        if ((ret = avio_read(pb, buf, image->size)) < 0)
            return ret;

        st->codec->bits_per_coded_sample = AV_RL16(buf + 14);

        if (AV_RL32(buf + 32))
            image->nb_pal = AV_RL32(buf + 32);

        if (st->codec->bits_per_coded_sample <= 8 && !image->nb_pal) {
            image->nb_pal = 1 << st->codec->bits_per_coded_sample;
            AV_WL32(buf + 32, image->nb_pal);
        }

        AV_WL32(buf - 4, 14 + 40 + image->nb_pal * 4);
        AV_WL32(buf + 8, AV_RL32(buf + 8) / 2);
    }

    pkt->stream_index = ico->current_image++;
    pkt->flags |= AV_PKT_FLAG_KEY;

    return 0;
}

AVInputFormat ff_ico_demuxer = {
    .name           = "ico",
    .long_name      = NULL_IF_CONFIG_SMALL("Microsoft Windows ICO"),
    .priv_data_size = sizeof(IcoDemuxContext),
    .read_probe     = probe,
    .read_header    = read_header,
    .read_packet    = read_packet,
    .flags          = AVFMT_NOTIMESTAMPS,
};
