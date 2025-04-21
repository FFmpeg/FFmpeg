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
#include "avio_internal.h"
#include "demux.h"
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
#define BRP_BASF_LOOKAHEAD      10 /* How many blocks to search for the first BASF one. */
#define BVID_HEADER_SIZE        16
#define MASK_HEADER_SIZE        12
#define BRP_MIN_BUFFER_SIZE     FFMAX3(FFMAX3(BRP_FILE_HEADER_SIZE,    \
                                              BRP_BLOCK_HEADER_SIZE,   \
                                              BRP_STREAM_HEADER_SIZE), \
                                      BVID_HEADER_SIZE,                \
                                      MASK_HEADER_SIZE)

#define BRP_CODEC_ID_BVID       MKTAG('B', 'V', 'I', 'D')
#define BRP_CODEC_ID_BASF       MKTAG('B', 'A', 'S', 'F')
#define BRP_CODEC_ID_MASK       MKTAG('M', 'A', 'S', 'K')

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

typedef struct ArgoMASKHeader {
    uint32_t num_frames;
    uint32_t width;
    uint32_t height;
} ArgoMASKHeader;

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
        /* If codec_id == BRP_CODEC_ID_MASK */
        ArgoMASKHeader    mask;
    } extradata;
} ArgoBRPStreamHeader;

typedef struct ArgoBRPDemuxContext {
    ArgoBRPFileHeader   fhdr;
    ArgoBRPStreamHeader streams[BRP_MAX_STREAMS];

    struct {
        int                 index;
        ArgoASFChunkHeader  ckhdr;
    } basf;
} ArgoBRPDemuxContext;

static int argo_brp_probe(const AVProbeData *p)
{
    if (AV_RL32(p->buf) != BRP_TAG)
        return 0;

    return AVPROBE_SCORE_EXTENSION + 1;
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
    } else if (hdr->codec_id == BRP_CODEC_ID_MASK) {
        name = "MASK";
        size = MASK_HEADER_SIZE;
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

    ret = ffio_read_size(s->pb, buf, size);
    if (ret < 0)
        return ret;

    return 0;
}

static int argo_brp_read_header(AVFormatContext *s)
{
    int64_t ret;
    AVIOContext *pb = s->pb;
    ArgoBRPDemuxContext *brp = s->priv_data;
    uint8_t buf[FFMAX(BRP_MIN_BUFFER_SIZE, ASF_MIN_BUFFER_SIZE)];

    ret = ffio_read_size(pb, buf, BRP_FILE_HEADER_SIZE);
    if (ret < 0)
        return ret;

    brp->fhdr.magic       = AV_RL32(buf + 0);
    brp->fhdr.num_streams = AV_RL32(buf + 4);
    brp->fhdr.byte_rate   = AV_RL32(buf + 8);

    if (brp->fhdr.magic != BRP_TAG)
        return AVERROR_INVALIDDATA;

    if (brp->fhdr.num_streams > BRP_MAX_STREAMS) {
        avpriv_request_sample(s, ">%d streams", BRP_MAX_STREAMS);
        return AVERROR_PATCHWELCOME;
    }

    /* Build the stream info. */
    brp->basf.index = -1;
    for (uint32_t i = 0; i < brp->fhdr.num_streams; i++) {
        ArgoBRPStreamHeader *hdr = brp->streams + i;
        AVStream *st;

        if (!(st = avformat_new_stream(s, NULL)))
            return AVERROR(ENOMEM);

        ret = ffio_read_size(pb, buf, BRP_STREAM_HEADER_SIZE);
        if (ret < 0)
            return ret;

        hdr->codec_id       = AV_RL32(buf + 0);
        hdr->id             = AV_RL32(buf + 4);
        hdr->duration_ms    = AV_RL32(buf + 8);
        hdr->byte_rate      = AV_RL32(buf + 12);
        hdr->extradata_size = AV_RL32(buf + 16);

        /* This should always be the case. */
        if (hdr->id != i)
            return AVERROR_INVALIDDATA;

        /* Timestamps are in milliseconds. */
        avpriv_set_pts_info(st, 64, 1, 1000);
        st->duration           = hdr->duration_ms;
        st->codecpar->bit_rate = hdr->byte_rate * 8;

        if ((ret = read_extradata(s, hdr, buf, sizeof(buf))) < 0) {
            return ret;
        } else if (ret > 0) {
            st->codecpar->codec_type = AVMEDIA_TYPE_UNKNOWN;
            continue;
        }

        if (hdr->codec_id == BRP_CODEC_ID_BVID) {
            ArgoBVIDHeader *bvid = &hdr->extradata.bvid;

            st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codecpar->codec_id   = AV_CODEC_ID_ARGO;

            bvid->num_frames = AV_RL32(buf +  0);
            bvid->width      = AV_RL32(buf +  4);
            bvid->height     = AV_RL32(buf +  8);
            bvid->depth      = AV_RL32(buf + 12);

            if (bvid->num_frames == 0)
                return AVERROR_INVALIDDATA;

            /* These are from 1990's games, sanity check this. */
            if (bvid->width >= 65536 || bvid->height >= 65536 ||
                bvid->depth > 24     || bvid->depth % 8 != 0) {
                return AVERROR_INVALIDDATA;
            }

            st->codecpar->width  = bvid->width;
            st->codecpar->height = bvid->height;
            st->nb_frames = bvid->num_frames;
            st->codecpar->bits_per_coded_sample = bvid->depth;
        } else if (hdr->codec_id == BRP_CODEC_ID_BASF) {
            /*
             * It would make the demuxer significantly more complicated
             * to support multiple BASF streams. I've never seen a file
             * with more than one.
             */
            if (brp->basf.index >= 0) {
                avpriv_request_sample(s, "Multiple BASF streams");
                return AVERROR_PATCHWELCOME;
            }

            st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
            st->codecpar->codec_id   = AV_CODEC_ID_ADPCM_ARGO;
            brp->basf.index          = i;
            ff_argo_asf_parse_file_header(&hdr->extradata.basf, buf);

            if ((ret = ff_argo_asf_validate_file_header(s, &hdr->extradata.basf)) < 0)
                return ret;

            st->nb_frames = hdr->extradata.basf.num_chunks;
        } else if (hdr->codec_id == BRP_CODEC_ID_MASK) {
            ArgoMASKHeader *mask = &hdr->extradata.mask;

            st->codecpar->codec_type = AVMEDIA_TYPE_DATA;

            mask->num_frames = AV_RL32(buf + 0);
            mask->width      = AV_RL32(buf + 4);
            mask->height     = AV_RL32(buf + 8);

            st->nb_frames    = mask->num_frames;
        } else {
            av_assert0(0); /* Caught above, should never happen. */
        }
    }

    /* Try to find the first BASF chunk. */
    if (brp->basf.index >= 0) {
        AVStream *st = s->streams[brp->basf.index];
        ArgoBRPStreamHeader *hdr = brp->streams + brp->basf.index;
        ArgoBRPBlockHeader blk;
        int64_t offset;
        int i;

        av_assert0(st->codecpar->codec_id == AV_CODEC_ID_ADPCM_ARGO);
        av_assert0(brp->streams[brp->basf.index].extradata_size == ASF_FILE_HEADER_SIZE);

        if ((ret = avio_tell(s->pb)) < 0)
            return ret;

        offset = ret;

        av_log(s, AV_LOG_TRACE, "Searching %d blocks for BASF...", BRP_BASF_LOOKAHEAD);

        for (i = 0; i < BRP_BASF_LOOKAHEAD; i++) {
            ret = ffio_read_size(pb, buf, BRP_BLOCK_HEADER_SIZE);
            if (ret < 0)
                return ret;

            blk.stream_id = AV_RL32(buf + 0);
            blk.start_ms  = AV_RL32(buf + 4);
            blk.size      = AV_RL32(buf + 8);

            if (blk.stream_id == brp->basf.index || blk.stream_id == -1)
                break;

            if ((ret = avio_skip(pb, blk.size)) < 0)
                return ret;
        }

        if (i == BRP_BASF_LOOKAHEAD || blk.stream_id == -1) {
            /* Don't error here, as there may still be a valid video stream. */
            av_log(s, AV_LOG_TRACE, "not found\n");
            goto done;
        }

        av_log(s, AV_LOG_TRACE, "found at index %d\n", i);

        if (blk.size < ASF_CHUNK_HEADER_SIZE)
            return AVERROR_INVALIDDATA;

        ret = ffio_read_size(pb, buf, BRP_BLOCK_HEADER_SIZE);
        if (ret < 0)
            return ret;

        ff_argo_asf_parse_chunk_header(&brp->basf.ckhdr, buf);

        /*
         * Special Case Hack. It seems that in files where the BASF block isn't first,
         * v1.1 streams are allowed to be non-22050...
         * Bump the version to 1.2 so ff_argo_asf_fill_stream() doesn't "correct" it.
         *
         * Found in Alien Odyssey games files in:
         * ./GRAPHICS/COMMBUNK/{{COMADD1,COMM2_{1,2,3E},COMM3_{2,3,4,5,6}},FADE{1,2}}.BRP
         *
         * Either this format really inconsistent, or FX Fighter and Croc just ignored the
         * sample rate field...
         */
        if (i != 0 && hdr->extradata.basf.version_major == 1 && hdr->extradata.basf.version_minor == 1)
            hdr->extradata.basf.version_minor = 2;

        if ((ret = ff_argo_asf_fill_stream(s, st, &hdr->extradata.basf, &brp->basf.ckhdr)) < 0)
            return ret;

        /* Convert ms to samples. */
        st->start_time = av_rescale_rnd(blk.start_ms, st->codecpar->sample_rate, 1000, AV_ROUND_UP);
        st->duration   = av_rescale_rnd(hdr->duration_ms, st->codecpar->sample_rate, 1000, AV_ROUND_UP);

done:
        if ((ret = avio_seek(s->pb, offset, SEEK_SET)) < 0)
            return ret;
    }
    return 0;
}

static int argo_brp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ArgoBRPDemuxContext *brp = s->priv_data;
    ArgoBRPBlockHeader blk;
    const ArgoBRPStreamHeader *shdr;
    AVStream *st;
    uint8_t buf[BRP_MIN_BUFFER_SIZE];
    ArgoASFChunkHeader ckhdr;
    int ret;

    ret = ffio_read_size(s->pb, buf, BRP_BLOCK_HEADER_SIZE);
    if (ret < 0)
        return ret;

    blk.stream_id = AV_RL32(buf + 0);
    blk.start_ms  = AV_RL32(buf + 4);
    blk.size      = AV_RL32(buf + 8);

    if (blk.stream_id == -1)
        return AVERROR_EOF;

    if (blk.stream_id < -1 || blk.stream_id >= s->nb_streams)
        return AVERROR_INVALIDDATA;

    st = s->streams[blk.stream_id];
    shdr = brp->streams + blk.stream_id;

    if (blk.stream_id == brp->basf.index) {
        if (blk.size < ASF_CHUNK_HEADER_SIZE)
            return AVERROR_INVALIDDATA;

        ret = ffio_read_size(s->pb, buf, ASF_CHUNK_HEADER_SIZE);
        if (ret < 0)
            return ret;

        ff_argo_asf_parse_chunk_header(&ckhdr, buf);

        /* Ensure the chunk attributes are the same. */
        if (ckhdr.sample_rate != brp->basf.ckhdr.sample_rate ||
            ckhdr.flags       != brp->basf.ckhdr.flags       ||
            ckhdr.unk1        != brp->basf.ckhdr.unk1        ||
            ckhdr.unk2        != brp->basf.ckhdr.unk2)
            return AVERROR_INVALIDDATA;

        blk.size -= ASF_CHUNK_HEADER_SIZE;
    }

    if ((ret = av_get_packet(s->pb, pkt, blk.size)) < 0)
        return ret;
    else if (ret != blk.size)
        return AVERROR_INVALIDDATA;

    if (blk.stream_id == brp->basf.index) {
        pkt->duration = ckhdr.num_samples * ckhdr.num_blocks;
        pkt->pts      = av_rescale_rnd(blk.start_ms, ckhdr.sample_rate, 1000, AV_ROUND_UP);
    } else if (shdr->codec_id == BRP_CODEC_ID_BVID) {
        pkt->duration = av_rescale_rnd(1, st->duration, shdr->extradata.bvid.num_frames, AV_ROUND_UP);
        pkt->pts      = blk.start_ms;
    } else {
        pkt->pts      = blk.start_ms;
    }

    pkt->stream_index = blk.stream_id;
    return 0;
}

const FFInputFormat ff_argo_brp_demuxer = {
    .p.name         = "argo_brp",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Argonaut Games BRP"),
    .priv_data_size = sizeof(ArgoBRPDemuxContext),
    .read_probe     = argo_brp_probe,
    .read_header    = argo_brp_read_header,
    .read_packet    = argo_brp_read_packet,
};
