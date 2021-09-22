/*
 * Microsoft Windows ICO muxer
 * Copyright (c) 2012 Michael Bradshaw <mjbshaw gmail com>
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
 * Microsoft Windows ICO muxer
 */

#include "libavutil/intreadwrite.h"
#include "libavutil/pixdesc.h"

#include "libavcodec/codec_id.h"

#include "avformat.h"
#include "avio_internal.h"

typedef struct {
    int offset;
    int size;
    unsigned char width;
    unsigned char height;
    short bits;
} IcoImage;

typedef struct {
    int current_image;
    int nb_images;
    IcoImage *images;
} IcoMuxContext;

static int ico_check_attributes(AVFormatContext *s, const AVCodecParameters *p)
{
    if (p->codec_id == AV_CODEC_ID_BMP) {
        if (p->format == AV_PIX_FMT_PAL8 && AV_PIX_FMT_RGB32 != AV_PIX_FMT_BGRA) {
            av_log(s, AV_LOG_ERROR, "Wrong endianness for bmp pixel format\n");
            return AVERROR(EINVAL);
        } else if (p->format != AV_PIX_FMT_PAL8 &&
                   p->format != AV_PIX_FMT_RGB555LE &&
                   p->format != AV_PIX_FMT_BGR24 &&
                   p->format != AV_PIX_FMT_BGRA) {
            av_log(s, AV_LOG_ERROR, "BMP must be 1bit, 4bit, 8bit, 16bit, 24bit, or 32bit\n");
            return AVERROR(EINVAL);
        }
    } else if (p->codec_id == AV_CODEC_ID_PNG) {
        if (p->format != AV_PIX_FMT_RGBA) {
            av_log(s, AV_LOG_ERROR, "PNG in ico requires pixel format to be rgba\n");
            return AVERROR(EINVAL);
        }
    } else {
        av_log(s, AV_LOG_ERROR, "Unsupported codec %s\n", avcodec_get_name(p->codec_id));
        return AVERROR(EINVAL);
    }

    if (p->width > 256 ||
        p->height > 256) {
        av_log(s, AV_LOG_ERROR, "Unsupported dimensions %dx%d (dimensions cannot exceed 256x256)\n", p->width, p->height);
        return AVERROR(EINVAL);
    }

    return 0;
}

static int ico_write_header(AVFormatContext *s)
{
    IcoMuxContext *ico = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;
    int i;

    if (!(pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        av_log(s, AV_LOG_ERROR, "Output is not seekable\n");
        return AVERROR(EINVAL);
    }

    ico->current_image = 0;
    ico->nb_images = s->nb_streams;

    avio_wl16(pb, 0); // reserved
    avio_wl16(pb, 1); // 1 == icon
    avio_skip(pb, 2); // skip the number of images

    for (i = 0; i < s->nb_streams; i++) {
        if (ret = ico_check_attributes(s, s->streams[i]->codecpar))
            return ret;

        // Fill in later when writing trailer...
        avio_skip(pb, 16);
    }

    ico->images = av_calloc(ico->nb_images, sizeof(*ico->images));
    if (!ico->images)
        return AVERROR(ENOMEM);

    return 0;
}

static int ico_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    IcoMuxContext *ico = s->priv_data;
    IcoImage *image;
    AVIOContext *pb = s->pb;
    AVCodecParameters *par = s->streams[pkt->stream_index]->codecpar;

    if (ico->current_image >= ico->nb_images) {
        av_log(s, AV_LOG_ERROR, "ICO already contains %d images\n", ico->current_image);
        return AVERROR(EIO);
    }

    image = &ico->images[ico->current_image++];

    image->offset = avio_tell(pb);
    image->width = (par->width == 256) ? 0 : par->width;
    image->height = (par->height == 256) ? 0 : par->height;

    if (par->codec_id == AV_CODEC_ID_PNG) {
        image->bits = par->bits_per_coded_sample;
        image->size = pkt->size;

        avio_write(pb, pkt->data, pkt->size);
    } else { // BMP
        if (AV_RL32(pkt->data + 14) != 40) { // must be BITMAPINFOHEADER
            av_log(s, AV_LOG_ERROR, "Invalid BMP\n");
            return AVERROR(EINVAL);
        }

        image->bits = AV_RL16(pkt->data + 28); // allows things like 1bit and 4bit images to be preserved
        image->size = pkt->size - 14 + par->height * (par->width + 7) / 8;

        avio_write(pb, pkt->data + 14, 8); // Skip the BITMAPFILEHEADER header
        avio_wl32(pb, AV_RL32(pkt->data + 22) * 2); // rewrite height as 2 * height
        avio_write(pb, pkt->data + 26, pkt->size - 26);

        // Write bitmask (opaque)
        ffio_fill(pb, 0x00, par->height * (par->width + 7) / 8);
    }

    return 0;
}

static int ico_write_trailer(AVFormatContext *s)
{
    IcoMuxContext *ico = s->priv_data;
    AVIOContext *pb = s->pb;
    int i;

    avio_seek(pb, 4, SEEK_SET);

    avio_wl16(pb, ico->current_image);

    for (i = 0; i < ico->nb_images; i++) {
        avio_w8(pb, ico->images[i].width);
        avio_w8(pb, ico->images[i].height);

        if (s->streams[i]->codecpar->codec_id == AV_CODEC_ID_BMP &&
            s->streams[i]->codecpar->format == AV_PIX_FMT_PAL8) {
            avio_w8(pb, (ico->images[i].bits >= 8) ? 0 : 1 << ico->images[i].bits);
        } else {
            avio_w8(pb, 0);
        }

        avio_w8(pb, 0); // reserved
        avio_wl16(pb, 1); // color planes
        avio_wl16(pb, ico->images[i].bits);
        avio_wl32(pb, ico->images[i].size);
        avio_wl32(pb, ico->images[i].offset);
    }

    return 0;
}

static void ico_deinit(AVFormatContext *s)
{
    IcoMuxContext *ico = s->priv_data;

    av_freep(&ico->images);
}

const AVOutputFormat ff_ico_muxer = {
    .name           = "ico",
    .long_name      = NULL_IF_CONFIG_SMALL("Microsoft Windows ICO"),
    .priv_data_size = sizeof(IcoMuxContext),
    .mime_type      = "image/vnd.microsoft.icon",
    .extensions     = "ico",
    .audio_codec    = AV_CODEC_ID_NONE,
    .video_codec    = AV_CODEC_ID_BMP,
    .write_header   = ico_write_header,
    .write_packet   = ico_write_packet,
    .write_trailer  = ico_write_trailer,
    .deinit         = ico_deinit,
    .flags          = AVFMT_NOTIMESTAMPS,
};
