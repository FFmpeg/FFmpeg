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

#include "libavutil/intreadwrite.h"
#include "internal.h"
#include "libavutil/opt.h"
#include "libavcodec/fits.h"
#include "libavutil/bprint.h"

#define FITS_BLOCK_SIZE 2880

typedef struct FITSContext {
    const AVClass *class;
    AVRational framerate;
    int first_image;
    int64_t pts;
} FITSContext;

static int fits_probe(AVProbeData *p)
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
    fits->pts = 0;
    fits->first_image = 1;
    return 0;
}

/**
 * Parses header and checks that the current HDU contains image or not
 * It also stores the header in the avbuf and stores the size of data part in data_size
 * @param s pointer to AVFormat Context
 * @param fits pointer to FITSContext
 * @param header pointer to FITSHeader
 * @param avbuf pointer to AVBPrint to store the header
 * @param data_size to store the size of data part
 * @return 1 if image found, 0 if any other extension and AVERROR_INVALIDDATA otherwise
 */
static int64_t is_image(AVFormatContext *s, FITSContext *fits, FITSHeader *header,
                         AVBPrint *avbuf, uint64_t *data_size)
{
    int i, ret, image = 0;
    char buf[FITS_BLOCK_SIZE] = { 0 };
    int64_t buf_size = 0, size = 0, t;

    do {
        ret = avio_read(s->pb, buf, FITS_BLOCK_SIZE);
        if (ret < 0) {
            return ret;
        } else if (ret < FITS_BLOCK_SIZE) {
            return AVERROR_INVALIDDATA;
        }

        av_bprint_append_data(avbuf, buf, FITS_BLOCK_SIZE);
        ret = 0;
        buf_size = 0;
        while(!ret && buf_size < FITS_BLOCK_SIZE) {
            ret = avpriv_fits_header_parse_line(s, header, buf + buf_size, NULL);
            buf_size += 80;
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
    if(size && t > UINT64_MAX / size)
        return AVERROR_INVALIDDATA;
    size *= t;

    if (!size) {
        image = 0;
    } else {
        if(FITS_BLOCK_SIZE - 1 > UINT64_MAX - size)
            return AVERROR_INVALIDDATA;
        size = ((size + FITS_BLOCK_SIZE - 1) / FITS_BLOCK_SIZE) * FITS_BLOCK_SIZE;
    }
    *data_size = size;
    return image;
}

static int fits_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int64_t pos, ret;
    uint64_t size;
    FITSContext *fits = s->priv_data;
    FITSHeader header;
    AVBPrint avbuf;
    char *buf;

    if (fits->first_image) {
        avpriv_fits_header_init(&header, STATE_SIMPLE);
    } else {
        avpriv_fits_header_init(&header, STATE_XTENSION);
    }

    av_bprint_init(&avbuf, FITS_BLOCK_SIZE, AV_BPRINT_SIZE_UNLIMITED);
    while ((ret = is_image(s, fits, &header, &avbuf, &size)) == 0) {
        pos = avio_skip(s->pb, size);
        if (pos < 0)
            return pos;

        av_bprint_finalize(&avbuf, NULL);
        av_bprint_init(&avbuf, FITS_BLOCK_SIZE, AV_BPRINT_SIZE_UNLIMITED);
        avpriv_fits_header_init(&header, STATE_XTENSION);
    }
    if (ret < 0)
        goto fail;

    if (!av_bprint_is_complete(&avbuf)) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    // Header is sent with the first line removed...
    ret = av_new_packet(pkt, avbuf.len - 80 + size);
    if (ret < 0)
        goto fail;

    pkt->stream_index = 0;
    pkt->flags |= AV_PKT_FLAG_KEY;

    ret = av_bprint_finalize(&avbuf, &buf);
    if (ret < 0) {
        av_packet_unref(pkt);
        return ret;
    }

    memcpy(pkt->data, buf + 80, avbuf.len - 80);
    pkt->size = avbuf.len - 80;
    av_freep(&buf);
    ret = avio_read(s->pb, pkt->data + pkt->size, size);
    if (ret < 0) {
        av_packet_unref(pkt);
        return ret;
    }

    pkt->size += ret;
    pkt->pts = fits->pts;
    fits->pts++;

    return 0;

fail:
    av_bprint_finalize(&avbuf, NULL);
    return ret;
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
};

AVInputFormat ff_fits_demuxer = {
    .name           = "fits",
    .long_name      = NULL_IF_CONFIG_SMALL("Flexible Image Transport System"),
    .priv_data_size = sizeof(FITSContext),
    .read_probe     = fits_probe,
    .read_header    = fits_read_header,
    .read_packet    = fits_read_packet,
    .priv_class     = &fits_demuxer_class,
    .raw_codec_id   = AV_CODEC_ID_FITS,
};
