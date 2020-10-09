/*
 * Phantom Cine demuxer
 * Copyright (c) 2010-2011 Peter Ross <pross@xvid.org>
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
 * Phantom Cine demuxer
 * @author Peter Ross <pross@xvid.org>
 */

#include "libavutil/intreadwrite.h"
#include "libavcodec/bmp.h"
#include "libavutil/intfloat.h"
#include "avformat.h"
#include "internal.h"

typedef struct {
    uint64_t pts;
} CineDemuxContext;

/** Compression */
enum {
    CC_RGB   = 0,  /**< Gray */
    CC_LEAD  = 1,  /**< LEAD (M)JPEG */
    CC_UNINT = 2   /**< Uninterpolated color image (CFA field indicates color ordering)  */
};

/** Color Filter Array */
enum {
    CFA_NONE      = 0,  /**< GRAY */
    CFA_VRI       = 1,  /**< GBRG/RGGB */
    CFA_VRIV6     = 2,  /**< BGGR/GRBG */
    CFA_BAYER     = 3,  /**< GB/RG */
    CFA_BAYERFLIP = 4,  /**< RG/GB */
};

#define CFA_TLGRAY  0x80000000U
#define CFA_TRGRAY  0x40000000U
#define CFA_BLGRAY  0x20000000U
#define CFA_BRGRAY  0x10000000U

static int cine_read_probe(const AVProbeData *p)
{
    int HeaderSize;
    if (p->buf[0] == 'C' && p->buf[1] == 'I' &&  // Type
        (HeaderSize = AV_RL16(p->buf + 2)) >= 0x2C &&  // HeaderSize
        AV_RL16(p->buf + 4) <= CC_UNINT &&       // Compression
        AV_RL16(p->buf + 6) <= 1 &&              // Version
        AV_RL32(p->buf + 20) &&                  // ImageCount
        AV_RL32(p->buf + 24) >= HeaderSize &&    // OffImageHeader
        AV_RL32(p->buf + 28) >= HeaderSize &&    // OffSetup
        AV_RL32(p->buf + 32) >= HeaderSize)      // OffImageOffsets
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int set_metadata_int(AVDictionary **dict, const char *key, int value, int allow_zero)
{
    if (value || allow_zero) {
        return av_dict_set_int(dict, key, value, 0);
    }
    return 0;
}

static int set_metadata_float(AVDictionary **dict, const char *key, float value, int allow_zero)
{
    if (value != 0 || allow_zero) {
        char tmp[64];
        snprintf(tmp, sizeof(tmp), "%f", value);
        return av_dict_set(dict, key, tmp, 0);
    }
    return 0;
}

static int cine_read_header(AVFormatContext *avctx)
{
    AVIOContext *pb = avctx->pb;
    AVStream *st;
    unsigned int version, compression, offImageHeader, offSetup, offImageOffsets, biBitCount, length, CFA;
    int vflip;
    char *description;
    uint64_t i;

    st = avformat_new_stream(avctx, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->codec_tag  = 0;

    /* CINEFILEHEADER structure */
    avio_skip(pb, 4); // Type, Headersize

    compression = avio_rl16(pb);
    version     = avio_rl16(pb);
    if (version != 1) {
        avpriv_request_sample(avctx, "unknown version %i", version);
        return AVERROR_INVALIDDATA;
    }

    avio_skip(pb, 12); // FirstMovieImage, TotalImageCount, FirstImageNumber

    st->duration    = avio_rl32(pb);
    offImageHeader  = avio_rl32(pb);
    offSetup        = avio_rl32(pb);
    offImageOffsets = avio_rl32(pb);

    avio_skip(pb, 8); // TriggerTime

    /* BITMAPINFOHEADER structure */
    avio_seek(pb, offImageHeader, SEEK_SET);
    avio_skip(pb, 4); //biSize
    st->codecpar->width      = avio_rl32(pb);
    st->codecpar->height     = avio_rl32(pb);

    if (avio_rl16(pb) != 1) // biPlanes
        return AVERROR_INVALIDDATA;

    biBitCount = avio_rl16(pb);
    if (biBitCount != 8 && biBitCount != 16 && biBitCount != 24 && biBitCount != 48) {
        avpriv_request_sample(avctx, "unsupported biBitCount %i", biBitCount);
        return AVERROR_INVALIDDATA;
    }

    switch (avio_rl32(pb)) {
    case BMP_RGB:
        vflip = 0;
        break;
    case 0x100: /* BI_PACKED */
        st->codecpar->codec_tag = MKTAG('B', 'I', 'T', 0);
        vflip = 1;
        break;
    default:
        avpriv_request_sample(avctx, "unknown bitmap compression");
        return AVERROR_INVALIDDATA;
    }

    avio_skip(pb, 4); // biSizeImage

    /* parse SETUP structure */
    avio_seek(pb, offSetup, SEEK_SET);
    avio_skip(pb, 140); // FrameRatae16 .. descriptionOld
    if (avio_rl16(pb) != 0x5453)
        return AVERROR_INVALIDDATA;
    length = avio_rl16(pb);
    if (length < 0x163C) {
        avpriv_request_sample(avctx, "short SETUP header");
        return AVERROR_INVALIDDATA;
    }

    avio_skip(pb, 616); // Binning .. bFlipH
    if (!avio_rl32(pb) ^ vflip) {
        st->codecpar->extradata  = av_strdup("BottomUp");
        if (!st->codecpar->extradata) {
            st->codecpar->extradata_size = 0;
            return AVERROR(ENOMEM);
        }
        st->codecpar->extradata_size  = 9;
    }

    avio_skip(pb, 4); // Grid

    avpriv_set_pts_info(st, 64, 1, avio_rl32(pb));

    avio_skip(pb, 20); // Shutter .. bEnableColor

    set_metadata_int(&st->metadata, "camera_version", avio_rl32(pb), 0);
    set_metadata_int(&st->metadata, "firmware_version", avio_rl32(pb), 0);
    set_metadata_int(&st->metadata, "software_version", avio_rl32(pb), 0);
    set_metadata_int(&st->metadata, "recording_timezone", avio_rl32(pb), 0);

    CFA = avio_rl32(pb);

    set_metadata_int(&st->metadata, "brightness", avio_rl32(pb), 1);
    set_metadata_int(&st->metadata, "contrast", avio_rl32(pb), 1);
    set_metadata_int(&st->metadata, "gamma", avio_rl32(pb), 1);

    avio_skip(pb, 12 + 16); // Reserved1 .. AutoExpRect
    set_metadata_float(&st->metadata, "wbgain[0].r", av_int2float(avio_rl32(pb)), 1);
    set_metadata_float(&st->metadata, "wbgain[0].b", av_int2float(avio_rl32(pb)), 1);
    avio_skip(pb, 36); // WBGain[1].. WBView

    st->codecpar->bits_per_coded_sample = avio_rl32(pb);

    if (compression == CC_RGB) {
        if (biBitCount == 8) {
            st->codecpar->format = AV_PIX_FMT_GRAY8;
        } else if (biBitCount == 16) {
            st->codecpar->format = AV_PIX_FMT_GRAY16LE;
        } else if (biBitCount == 24) {
            st->codecpar->format = AV_PIX_FMT_BGR24;
        } else if (biBitCount == 48) {
            st->codecpar->format = AV_PIX_FMT_BGR48LE;
        } else {
            avpriv_request_sample(avctx, "unsupported biBitCount %i", biBitCount);
            return AVERROR_INVALIDDATA;
        }
    } else if (compression == CC_UNINT) {
        switch (CFA & 0xFFFFFF) {
        case CFA_BAYER:
            if (biBitCount == 8) {
                st->codecpar->format = AV_PIX_FMT_BAYER_GBRG8;
            } else if (biBitCount == 16) {
                st->codecpar->format = AV_PIX_FMT_BAYER_GBRG16LE;
            } else {
                avpriv_request_sample(avctx, "unsupported biBitCount %i", biBitCount);
                return AVERROR_INVALIDDATA;
            }
            break;
        case CFA_BAYERFLIP:
            if (biBitCount == 8) {
                st->codecpar->format = AV_PIX_FMT_BAYER_RGGB8;
            } else if (biBitCount == 16) {
                st->codecpar->format = AV_PIX_FMT_BAYER_RGGB16LE;
            } else {
                avpriv_request_sample(avctx, "unsupported biBitCount %i", biBitCount);
                return AVERROR_INVALIDDATA;
            }
            break;
        default:
           avpriv_request_sample(avctx, "unsupported Color Field Array (CFA) %i", CFA & 0xFFFFFF);
            return AVERROR_INVALIDDATA;
        }
    } else { //CC_LEAD
        avpriv_request_sample(avctx, "unsupported compression %i", compression);
        return AVERROR_INVALIDDATA;
    }

    avio_skip(pb, 668); // Conv8Min ... Sensor

    set_metadata_int(&st->metadata, "shutter_ns", avio_rl32(pb), 0);

    avio_skip(pb, 24); // EDRShutterNs ... ImHeightAcq

#define DESCRIPTION_SIZE 4096
    description = av_malloc(DESCRIPTION_SIZE + 1);
    if (!description)
        return AVERROR(ENOMEM);
    i = avio_get_str(pb, DESCRIPTION_SIZE, description, DESCRIPTION_SIZE + 1);
    if (i < DESCRIPTION_SIZE)
        avio_skip(pb, DESCRIPTION_SIZE - i);
    if (description[0])
        av_dict_set(&st->metadata, "description", description, AV_DICT_DONT_STRDUP_VAL);
    else
        av_free(description);

    avio_skip(pb, 1176); // RisingEdge ... cmUser

    set_metadata_int(&st->metadata, "enable_crop", avio_rl32(pb), 1);
    set_metadata_int(&st->metadata, "crop_left", avio_rl32(pb), 1);
    set_metadata_int(&st->metadata, "crop_top", avio_rl32(pb), 1);
    set_metadata_int(&st->metadata, "crop_right", avio_rl32(pb), 1);
    set_metadata_int(&st->metadata, "crop_bottom", avio_rl32(pb), 1);

    /* parse image offsets */
    avio_seek(pb, offImageOffsets, SEEK_SET);
    for (i = 0; i < st->duration; i++) {
        if (avio_feof(pb))
            return AVERROR_INVALIDDATA;

        av_add_index_entry(st, avio_rl64(pb), i, 0, 0, AVINDEX_KEYFRAME);
    }

    return 0;
}

static int cine_read_packet(AVFormatContext *avctx, AVPacket *pkt)
{
    CineDemuxContext *cine = avctx->priv_data;
    AVStream *st = avctx->streams[0];
    AVIOContext *pb = avctx->pb;
    int n, size, ret;

    if (cine->pts >= st->duration)
        return AVERROR_EOF;

    avio_seek(pb, st->internal->index_entries[cine->pts].pos, SEEK_SET);
    n = avio_rl32(pb);
    if (n < 8)
        return AVERROR_INVALIDDATA;
    avio_skip(pb, n - 8);
    size = avio_rl32(pb);

    ret = av_get_packet(pb, pkt, size);
    if (ret < 0)
        return ret;

    pkt->pts = cine->pts++;
    pkt->stream_index = 0;
    pkt->flags |= AV_PKT_FLAG_KEY;
    return 0;
}

static int cine_read_seek(AVFormatContext *avctx, int stream_index, int64_t timestamp, int flags)
{
    CineDemuxContext *cine = avctx->priv_data;

    if ((flags & AVSEEK_FLAG_FRAME) || (flags & AVSEEK_FLAG_BYTE))
        return AVERROR(ENOSYS);

    if (!(avctx->pb->seekable & AVIO_SEEKABLE_NORMAL))
        return AVERROR(EIO);

    cine->pts = timestamp;
    return 0;
}

AVInputFormat ff_cine_demuxer = {
    .name           = "cine",
    .long_name      = NULL_IF_CONFIG_SMALL("Phantom Cine"),
    .priv_data_size = sizeof(CineDemuxContext),
    .read_probe     = cine_read_probe,
    .read_header    = cine_read_header,
    .read_packet    = cine_read_packet,
    .read_seek      = cine_read_seek,
};
