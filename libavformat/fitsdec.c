/*
 * FITS demuxer
 * Copyright (c) 2017 Paras Chadha
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
 * FITS demuxer.
 */

#include "demux.h"
#include "internal.h"
#include "libavutil/opt.h"
#include "libavcodec/fits.h"

#define FITS_BLOCK_SIZE 2880

typedef struct FITSContext {
    const AVClass *class;
    AVRational framerate;
    int first_image;
} FITSContext;

static int fits_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    if (!memcmp(b, "SIMPLE  =                    T", 30))
        return AVPROBE_SCORE_MAX - 1;
    return 0;
}

static int fits_read_header(AVFormatContext *s)
{
    AVStream *st;
    FITSContext * fits = s->priv_data;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_FITS;

    avpriv_set_pts_info(st, 64, fits->framerate.den, fits->framerate.num);
    fits->first_image = 1;
    return 0;
}

/**
 * Parses header and checks that the current HDU contains image or not
 * It also stores the header in the avbuf and stores the size of data part in data_size
 * @param s pointer to AVFormat Context
 * @param fits pointer to FITSContext
 * @param header pointer to FITSHeader
 * @param pkt pointer to AVPacket to store the header
 * @param data_size to store the size of data part
 * @return 1 if image found, 0 if any other extension and AVERROR code otherwise
 */
static int is_image(AVFormatContext *s, FITSContext *fits, FITSHeader *header,
                    AVPacket *pkt, uint64_t *data_size)
{
    int i, ret, image = 0;
    int64_t size = 0, t;

    do {
        const uint8_t *buf, *buf_end;
        ret = av_append_packet(s->pb, pkt, FITS_BLOCK_SIZE);
        if (ret < 0) {
            return ret;
        } else if (ret < FITS_BLOCK_SIZE) {
            return AVERROR_INVALIDDATA;
        }

        ret = 0;
        buf_end = pkt->data + pkt->size;
        buf     = buf_end - FITS_BLOCK_SIZE;
        while(!ret && buf < buf_end) {
            ret = avpriv_fits_header_parse_line(s, header, buf, NULL);
            buf += 80;
        }
    } while (!ret);
    if (ret < 0)
        return ret;

    image = fits->first_image || header->image_extension;
    fits->first_image = 0;

    if (header->groups) {
        image = 0;
        if (header->naxis > 1)
            size = 1;
    } else if (header->naxis) {
        size = header->naxisn[0];
    } else {
        image = 0;
    }

    for (i = 1; i < header->naxis; i++) {
        if(size && header->naxisn[i] > UINT64_MAX / size)
            return AVERROR_INVALIDDATA;
        size *= header->naxisn[i];
    }

    if(header->pcount > UINT64_MAX - size)
        return AVERROR_INVALIDDATA;
    size += header->pcount;

    t = (abs(header->bitpix) >> 3) * ((int64_t) header->gcount);
    if(size && t > INT64_MAX / size)
        return AVERROR_INVALIDDATA;
    size *= t;

    if (!size) {
        image = 0;
    } else {
        if(FITS_BLOCK_SIZE - 1 > INT64_MAX - size)
            return AVERROR_INVALIDDATA;
        size = ((size + FITS_BLOCK_SIZE - 1) / FITS_BLOCK_SIZE) * FITS_BLOCK_SIZE;
    }
    *data_size = size;
    return image;
}

static int fits_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    uint64_t size;
    FITSContext *fits = s->priv_data;
    FITSHeader header;
    int ret;

    if (fits->first_image) {
        avpriv_fits_header_init(&header, STATE_SIMPLE);
    } else {
        avpriv_fits_header_init(&header, STATE_XTENSION);
    }

    while ((ret = is_image(s, fits, &header, pkt, &size)) == 0) {
        int64_t pos = avio_skip(s->pb, size);
        if (pos < 0)
            return pos;

        avpriv_fits_header_init(&header, STATE_XTENSION);
        av_packet_unref(pkt);
    }
    if (ret < 0)
        return ret;

    pkt->stream_index = 0;
    pkt->flags       |= AV_PKT_FLAG_KEY;
    pkt->duration     = 1;
    // Header is sent with the first line removed...
    pkt->data        += 80;
    pkt->size        -= 80;

    if (size > INT_MAX - AV_INPUT_BUFFER_PADDING_SIZE - pkt->size)
        return AVERROR(ERANGE);

    ret = av_append_packet(s->pb, pkt, size);
    if (ret < 0)
        return ret;

    return 0;
}

static const AVOption fits_options[] = {
    { "framerate", "set the framerate", offsetof(FITSContext, framerate), AV_OPT_TYPE_VIDEO_RATE, {.str = "1"}, 0, INT_MAX, AV_OPT_FLAG_DECODING_PARAM},
    { NULL },
};

static const AVClass fits_demuxer_class = {
    .class_name = "FITS demuxer",
    .item_name  = av_default_item_name,
    .option     = fits_options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

const FFInputFormat ff_fits_demuxer = {
    .p.name         = "fits",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Flexible Image Transport System"),
    .p.priv_class   = &fits_demuxer_class,
    .p.flags        = AVFMT_NOTIMESTAMPS,
    .priv_data_size = sizeof(FITSContext),
    .read_probe     = fits_probe,
    .read_header    = fits_read_header,
    .read_packet    = fits_read_packet,
};
