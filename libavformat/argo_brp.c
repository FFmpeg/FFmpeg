/*
 * Argonaut Games BRP Demuxer
 *
 * Copyright (C) 2020 Zane van Iperen (zane@zanevaniperen.com)
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
#include "libavutil/intreadwrite.h"
#include "libavutil/avassert.h"
#include "libavutil/internal.h"
#include "argo_asf.h"

#define BRP_TAG                 MKTAG('B', 'R', 'P', 'P')
#define BRP_FILE_HEADER_SIZE    12
#define BRP_BLOCK_HEADER_SIZE   12
#define BRP_STREAM_HEADER_SIZE  20
#define BRP_MAX_STREAMS         32 /* Soft cap, but even this is overkill. */
#define BVID_HEADER_SIZE        16
#define BRP_MIN_BUFFER_SIZE     FFMAX(FFMAX3(BRP_FILE_HEADER_SIZE,    \
                                             BRP_BLOCK_HEADER_SIZE,   \
                                             BRP_STREAM_HEADER_SIZE), \
                                      BVID_HEADER_SIZE)

#define BRP_CODEC_ID_BVID       MKTAG('B', 'V', 'I', 'D')
#define BRP_CODEC_ID_BASF       MKTAG('B', 'A', 'S', 'F')

typedef struct ArgoBRPFileHeader {
    uint32_t magic;
    uint32_t num_streams;
    uint32_t byte_rate;
} ArgoBRPFileHeader;

typedef struct ArgoBRPBlockHeader {
    int32_t  stream_id;
    uint32_t start_ms;
    uint32_t size;
} ArgoBRPBlockHeader;

typedef struct ArgoBVIDHeader {
    uint32_t num_frames;
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} ArgoBVIDHeader;

typedef struct ArgoBRPStreamHeader {
    uint32_t codec_id;
    uint32_t id;
    uint32_t duration_ms;
    uint32_t byte_rate;
    uint32_t extradata_size;
    union
    {
        /* If codec_id == BRP_CODEC_ID_BVID */
        ArgoBVIDHeader    bvid;
        /* If codec_id == BRP_CODEC_ID_BASF */
        ArgoASFFileHeader basf;
    } extradata;
} ArgoBRPStreamHeader;

typedef struct ArgoBRPDemuxContext {
    ArgoBRPFileHeader   fhdr;
    ArgoBRPStreamHeader *streams;
    /* To know how much of a BASF to give. */
    int64_t             lastpts;
    int                 hit_eof;

    /* BASF-specific fields. */
    struct {
        int                 index;
        ArgoASFChunkHeader  ckhdr;
        int64_t             blocks_read;
        int64_t             offset;
        /* ms, not samples. */
        int64_t             lastpts;
    } basf;
} ArgoBRPDemuxContext;

static int argo_brp_probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) != BRP_TAG)
        return 0;

    return AVPROBE_SCORE_EXTENSION + 1;
}

static int argo_brp_read_close(AVFormatContext *s)
{
    ArgoBRPDemuxContext *brp = s->priv_data;

    if (brp->streams != NULL)
        av_freep(&brp->streams);

    return 0;
}

static int read_extradata(AVFormatContext *s, const ArgoBRPStreamHeader *hdr,
                          void *buf, size_t bufsz)
{
    const char *name;
    uint32_t size;
    int64_t ret;

    if (hdr->codec_id == BRP_CODEC_ID_BVID) {
        name = "BVID";
        size = BVID_HEADER_SIZE;
    } else if (hdr->codec_id == BRP_CODEC_ID_BASF) {
        name = "BASF";
        size = ASF_FILE_HEADER_SIZE;
    } else {
        avpriv_request_sample(s, "BRP codec id 0x%x", hdr->codec_id);

        if ((ret = avio_skip(s->pb, hdr->extradata_size)) < 0)
            return ret;

        return 1;
    }

    if (hdr->extradata_size != size) {
        av_log(s, AV_LOG_ERROR, "Invalid %s extradata size %u, expected %u\n",
               name, hdr->extradata_size, size);
        return AVERROR_INVALIDDATA;
    }

    av_assert0(bufsz >= size);

    if ((ret = avio_read(s->pb, buf, size)) < 0)
        return ret;

    if (ret != size)
        return AVERROR(EIO);

    return 0;
}

static int argo_brp_read_header(AVFormatContext *s)
{
    int64_t ret;
    AVIOContext *pb = s->pb;
    ArgoBRPDemuxContext *brp = s->priv_data;
    uint8_t buf[FFMAX(BRP_MIN_BUFFER_SIZE, ASF_MIN_BUFFER_SIZE)];

    if ((ret = avio_read(pb, buf, BRP_FILE_HEADER_SIZE)) < 0)
        return ret;
    else if (ret != BRP_FILE_HEADER_SIZE)
        return AVERROR(EIO);

    brp->fhdr.magic       = AV_RL32(buf + 0);
    brp->fhdr.num_streams = AV_RL32(buf + 4);
    brp->fhdr.byte_rate   = AV_RL32(buf + 8);

    if (brp->fhdr.magic != BRP_TAG)
        return AVERROR_INVALIDDATA;

    if (brp->fhdr.num_streams > BRP_MAX_STREAMS) {
        avpriv_request_sample(s, ">%d streams", BRP_MAX_STREAMS);
        return AVERROR_PATCHWELCOME;
    }

    if ((brp->streams = av_mallocz_array(brp->fhdr.num_streams, sizeof(ArgoBRPStreamHeader))) == NULL)
        return AVERROR(ENOMEM);

    /* Build the stream info. */
    brp->basf.index = -1;
    for (uint32_t i = 0; i < brp->fhdr.num_streams; i++) {
        ArgoBRPStreamHeader *hdr = brp->streams + i;
        AVStream *st;

        if (!(st = avformat_new_stream(s, NULL))) {
            ret = AVERROR(ENOMEM);
            goto fail;
        }

        if ((ret = avio_read(pb, buf, BRP_STREAM_HEADER_SIZE)) < 0) {
            goto fail;
        } else if (ret != BRP_STREAM_HEADER_SIZE) {
            ret = AVERROR(EIO);
            goto fail;
        }

        hdr->codec_id       = AV_RL32(buf + 0);
        hdr->id             = AV_RL32(buf + 4);
        hdr->duration_ms    = AV_RL32(buf + 8);
        hdr->byte_rate      = AV_RL32(buf + 12);
        hdr->extradata_size = AV_RL32(buf + 16);

        /* Timestamps are in milliseconds. */
        avpriv_set_pts_info(st, 64, 1, 1000);
        st->duration           = hdr->duration_ms;
        st->codecpar->bit_rate = hdr->byte_rate * 8;

        if ((ret = read_extradata(s, hdr, buf, sizeof(buf))) < 0) {
            goto fail;
        } else if (ret > 0) {
            st->codecpar->codec_type = AVMEDIA_TYPE_UNKNOWN;
            continue;
        }

        if (hdr->codec_id == BRP_CODEC_ID_BVID) {
            ArgoBVIDHeader *bvid = &hdr->extradata.bvid;

            st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;

            /* No decoder for this yet. */
            st->codecpar->codec_id   = AV_CODEC_ID_NONE;

            bvid->num_frames = AV_RL32(buf +  0);
            bvid->width      = AV_RL32(buf +  4);
            bvid->height     = AV_RL32(buf +  8);
            bvid->depth      = AV_RL32(buf + 12);

            /* These are from 1990's games, sanity check this. */
            if (bvid->width >= 65536 || bvid->height >= 65536 ||
                bvid->depth > 24     || bvid->depth % 8 != 0) {
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }

            st->codecpar->width  = bvid->width;
            st->codecpar->height = bvid->height;

            if (bvid->depth == 8) {
                st->codecpar->format = AV_PIX_FMT_PAL8;
            } else if (bvid->depth == 24) {
                st->codecpar->format = AV_PIX_FMT_RGB24;
            } else {
                avpriv_request_sample(s, "depth == %u", bvid->depth);
                ret = AVERROR_PATCHWELCOME;
                goto fail;
            }
        } else if (hdr->codec_id == BRP_CODEC_ID_BASF) {
            /*
             * It would make the demuxer significantly more complicated
             * to support multiple BASF streams. I've never seen a file
             * with more than one.
             */
            if (brp->basf.index >= 0) {
                avpriv_request_sample(s, "Multiple BASF streams");
                ret = AVERROR_PATCHWELCOME;
                goto fail;
            }

            st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            st->codecpar->codec_id   = AV_CODEC_ID_ADPCM_ARGO;
            brp->basf.index          = i;
            ff_argo_asf_parse_file_header(&hdr->extradata.basf, buf);

            if ((ret = ff_argo_asf_validate_file_header(s, &hdr->extradata.basf)) < 0)
                goto fail;

        } else {
            av_assert0(0); /* Caught above, should never happen. */
        }
    }

    /*
     * This is nasty. BASF streams only have one (huge) block.
     * It should be the first one. It contains the chunk header, so
     * it needs to be read here.
     */
    if (brp->basf.index >= 0) {
        AVStream *st = s->streams[brp->basf.index];
        ArgoBRPStreamHeader *hdr = brp->streams + brp->basf.index;
        ArgoBRPBlockHeader blk;

        av_assert0(st->codecpar->codec_id == AV_CODEC_ID_ADPCM_ARGO);
        av_assert0(brp->streams[brp->basf.index].extradata_size == ASF_FILE_HEADER_SIZE);

        if ((ret = avio_read(pb, buf, BRP_BLOCK_HEADER_SIZE)) < 0)
            goto fail;
        else if (ret != BRP_BLOCK_HEADER_SIZE) {
            ret = AVERROR(EIO);
            goto fail;
        }

        blk.stream_id = AV_RL32(buf + 0);
        blk.start_ms  = AV_RL32(buf + 4);
        blk.size      = AV_RL32(buf + 8);

        if (blk.stream_id != brp->basf.index) {
            avpriv_request_sample(st, "first block not BASF");
            ret = AVERROR_PATCHWELCOME;
            goto fail;
        }

        if (blk.size < ASF_CHUNK_HEADER_SIZE) {
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }

        if ((ret = avio_read(pb, buf, ASF_CHUNK_HEADER_SIZE)) < 0)
            goto fail;
        else if (ret != ASF_CHUNK_HEADER_SIZE) {
            ret = AVERROR(EIO);
            goto fail;
        }

        ff_argo_asf_parse_chunk_header(&brp->basf.ckhdr, buf);

        if ((ret = ff_argo_asf_fill_stream(st, &hdr->extradata.basf, &brp->basf.ckhdr)) < 0)
            goto fail;

        /* Convert ms to samples. */
        st->start_time = (blk.start_ms * st->codecpar->sample_rate) / 1000;

        if ((ret = avio_tell(s->pb)) < 0)
            goto fail;

        brp->basf.offset = ret;

        if ((ret = avio_skip(s->pb, blk.size - ASF_CHUNK_HEADER_SIZE)) < 0)
            goto fail;
    }
    return 0;

fail:
    /* TODO: Remove once AVFMT_HEADER_CLEANUP lands. */
    argo_brp_read_close(s);
    return ret;
}

static int argo_brp_read_basf(AVFormatContext *s, AVPacket *pkt,
                              ArgoBRPDemuxContext *brp, int ignorepts)
{
    ArgoASFChunkHeader *ckhdr = &brp->basf.ckhdr;
    AVCodecParameters *par;
    int64_t ret, old;

    if (brp->basf.index < 0)
        return 0;

    par = s->streams[brp->basf.index]->codecpar;

    if (brp->basf.blocks_read >= ckhdr->num_blocks)
        return 0;

    if (!ignorepts && brp->lastpts < brp->basf.lastpts)
        return 0;

    if ((ret = avio_tell(s->pb)) < 0)
        return ret;

    old = ret;

    if ((ret = avio_seek(s->pb, brp->basf.offset, SEEK_SET)) < 0)
        return ret;
    else if (ret != brp->basf.offset)
        return AVERROR(EIO);

    if ((ret = av_get_packet(s->pb, pkt, par->frame_size)) < 0)
        return ret;

    if ((ret = avio_seek(s->pb, old, SEEK_SET)) < 0)
        return ret;
    else if (ret != old)
        return AVERROR(EIO);

    pkt->stream_index      = brp->basf.index;
    pkt->duration          = ckhdr->num_samples;

    brp->basf.offset      += pkt->size;
    brp->basf.blocks_read += 1;
    /* Need the ceil() because ((32 * 1000) / 44100) < 1 */
    brp->basf.lastpts     += ceilf((ckhdr->num_samples * 1000.0f) / ckhdr->sample_rate);
    return 1;
}

static int argo_brp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ArgoBRPDemuxContext *brp = s->priv_data;
    ArgoBRPBlockHeader blk;
    AVIOContext *pb = s->pb;
    uint8_t buf[BRP_BLOCK_HEADER_SIZE];
    int ret;

    /*
     * Special-case: send some more BASF content if we're running behind.
     * Grr, why couldn't they just interleave it.
     */
    if ((ret = argo_brp_read_basf(s, pkt, brp, brp->hit_eof)) < 0)
        return ret;
    else if (ret > 0)
        return 0;

    if (brp->hit_eof)
        return AVERROR_EOF;

    if ((ret = avio_read(pb, buf, BRP_BLOCK_HEADER_SIZE)) < 0)
        return ret;
    else if (ret != BRP_BLOCK_HEADER_SIZE)
        return AVERROR(EIO);

    blk.stream_id = AV_RL32(buf + 0);
    blk.start_ms  = AV_RL32(buf + 4);
    blk.size      = AV_RL32(buf + 8);

    /* This is meant to be EOF, but there might be more BASF packets. */
    if (blk.stream_id == -1) {
        brp->hit_eof = 1;
        /* This is nasty, but safe. */
        return argo_brp_read_packet(s, pkt);
    }

    if (blk.stream_id < -1 || blk.stream_id >= s->nb_streams)
        return AVERROR_INVALIDDATA;

    /* Skip BASF blocks. */
    if (blk.stream_id == brp->basf.index) {
        if ((ret = avio_skip(s->pb, blk.size)) < 0)
            return ret;
    } else {
        if ((ret = av_get_packet(s->pb, pkt, blk.size)) < 0)
            return ret;
        else if (ret != blk.size)
            return AVERROR_INVALIDDATA;
    }

    pkt->stream_index = blk.stream_id;
    pkt->pts          = blk.start_ms;
    brp->lastpts      = FFMAX(brp->lastpts, blk.start_ms);
    return 0;
}

AVInputFormat ff_argo_brp_demuxer = {
    .name           = "argo_brp",
    .long_name      = NULL_IF_CONFIG_SMALL("Argonaut Games BRP"),
    .priv_data_size = sizeof(ArgoBRPDemuxContext),
    .read_probe     = argo_brp_probe,
    .read_header    = argo_brp_read_header,
    .read_packet    = argo_brp_read_packet,
    .read_close     = argo_brp_read_close
};
