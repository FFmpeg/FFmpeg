/*
 * Argonaut Games ASF (de)muxer
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
#include "libavutil/opt.h"

#define ASF_TAG                 MKTAG('A', 'S', 'F', '\0')
#define ASF_FILE_HEADER_SIZE    24
#define ASF_CHUNK_HEADER_SIZE   20
#define ASF_SAMPLE_COUNT        32

typedef struct ArgoASFFileHeader {
    uint32_t    magic;          /*< Magic Number, {'A', 'S', 'F', '\0'} */
    uint16_t    version_major;  /*< File Major Version. */
    uint16_t    version_minor;  /*< File Minor Version. */
    uint32_t    num_chunks;     /*< No. chunks in the file. */
    uint32_t    chunk_offset;   /*< Offset to the first chunk from the start of the file. */
    int8_t      name[8];        /*< Name. */
} ArgoASFFileHeader;

typedef struct ArgoASFChunkHeader {
    uint32_t    num_blocks;     /*< No. blocks in the chunk. */
    uint32_t    num_samples;    /*< No. samples per channel in a block. Always 32. */
    uint32_t    unk1;           /*< Unknown */
    uint16_t    sample_rate;    /*< Sample rate. */
    uint16_t    unk2;           /*< Unknown. */
    uint32_t    flags;          /*< Stream flags. */
} ArgoASFChunkHeader;

enum {
    ASF_CF_BITS_PER_SAMPLE  = (1 << 0), /*< 16-bit if set, 8 otherwise.      */
    ASF_CF_STEREO           = (1 << 1), /*< Stereo if set, mono otherwise.   */
    ASF_CF_ALWAYS1_1        = (1 << 2), /*< Unknown, always seems to be set. */
    ASF_CF_ALWAYS1_2        = (1 << 3), /*< Unknown, always seems to be set. */

    ASF_CF_ALWAYS1          = ASF_CF_ALWAYS1_1 | ASF_CF_ALWAYS1_2,
    ASF_CF_ALWAYS0          = ~(ASF_CF_BITS_PER_SAMPLE | ASF_CF_STEREO | ASF_CF_ALWAYS1)
};

typedef struct ArgoASFDemuxContext {
    ArgoASFFileHeader   fhdr;
    ArgoASFChunkHeader  ckhdr;
    uint32_t            blocks_read;
} ArgoASFDemuxContext;

typedef struct ArgoASFMuxContext {
    const AVClass *class;
    int            version_major;
    int            version_minor;
    const char    *name;
} ArgoASFMuxContext;

#if CONFIG_ARGO_ASF_DEMUXER
static void argo_asf_parse_file_header(ArgoASFFileHeader *hdr, const uint8_t *buf)
{
    hdr->magic          = AV_RL32(buf + 0);
    hdr->version_major  = AV_RL16(buf + 4);
    hdr->version_minor  = AV_RL16(buf + 6);
    hdr->num_chunks     = AV_RL32(buf + 8);
    hdr->chunk_offset   = AV_RL32(buf + 12);
    for (int i = 0; i < FF_ARRAY_ELEMS(hdr->name); i++)
        hdr->name[i]    = AV_RL8(buf + 16 + i);
}

static void argo_asf_parse_chunk_header(ArgoASFChunkHeader *hdr, const uint8_t *buf)
{
    hdr->num_blocks     = AV_RL32(buf + 0);
    hdr->num_samples    = AV_RL32(buf + 4);
    hdr->unk1           = AV_RL32(buf + 8);
    hdr->sample_rate    = AV_RL16(buf + 12);
    hdr->unk2           = AV_RL16(buf + 14);
    hdr->flags          = AV_RL32(buf + 16);
}

/*
 * Known versions:
 * 1.1: https://samples.ffmpeg.org/game-formats/brender/part2.zip
 *      FX Fighter
 * 1.2: Croc! Legend of the Gobbos
 * 2.1: Croc 2
 *      The Emperor's New Groove
 *      Disney's Aladdin in Nasira's Revenge
 */
static int argo_asf_is_known_version(const ArgoASFFileHeader *hdr)
{
    return (hdr->version_major == 1 && hdr->version_minor == 1) ||
           (hdr->version_major == 1 && hdr->version_minor == 2) ||
           (hdr->version_major == 2 && hdr->version_minor == 1);
}

static int argo_asf_probe(const AVProbeData *p)
{
    ArgoASFFileHeader hdr;

    av_assert0(AVPROBE_PADDING_SIZE >= ASF_FILE_HEADER_SIZE);

    argo_asf_parse_file_header(&hdr, p->buf);

    if (hdr.magic != ASF_TAG)
        return 0;

    if (!argo_asf_is_known_version(&hdr))
        return AVPROBE_SCORE_EXTENSION / 2;

    return AVPROBE_SCORE_EXTENSION + 1;
}

static int argo_asf_read_header(AVFormatContext *s)
{
    int64_t ret;
    AVIOContext *pb = s->pb;
    AVStream *st;
    ArgoASFDemuxContext *asf = s->priv_data;
    uint8_t buf[FFMAX(ASF_FILE_HEADER_SIZE, ASF_CHUNK_HEADER_SIZE)];

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    if ((ret = avio_read(pb, buf, ASF_FILE_HEADER_SIZE)) < 0)
        return ret;
    else if (ret != ASF_FILE_HEADER_SIZE)
        return AVERROR(EIO);

    argo_asf_parse_file_header(&asf->fhdr, buf);

    if (asf->fhdr.num_chunks == 0) {
        return AVERROR_INVALIDDATA;
    } else if (asf->fhdr.num_chunks > 1) {
        avpriv_request_sample(s, ">1 chunk");
        return AVERROR_PATCHWELCOME;
    }

    if (asf->fhdr.chunk_offset < ASF_FILE_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    if ((ret = avio_skip(pb, asf->fhdr.chunk_offset - ASF_FILE_HEADER_SIZE)) < 0)
        return ret;

    if ((ret = avio_read(pb, buf, ASF_CHUNK_HEADER_SIZE)) < 0)
        return ret;
    else if (ret != ASF_CHUNK_HEADER_SIZE)
        return AVERROR(EIO);

    argo_asf_parse_chunk_header(&asf->ckhdr, buf);

    if (asf->ckhdr.num_samples != ASF_SAMPLE_COUNT) {
        av_log(s, AV_LOG_ERROR, "Invalid sample count. Got %u, expected %d\n",
               asf->ckhdr.num_samples, ASF_SAMPLE_COUNT);
        return AVERROR_INVALIDDATA;
    }

    if ((asf->ckhdr.flags & ASF_CF_ALWAYS1) != ASF_CF_ALWAYS1 || (asf->ckhdr.flags & ASF_CF_ALWAYS0) != 0) {
        avpriv_request_sample(s, "Nonstandard flags (0x%08X)", asf->ckhdr.flags);
        return AVERROR_PATCHWELCOME;
    }

    st->codecpar->codec_type                = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id                  = AV_CODEC_ID_ADPCM_ARGO;
    st->codecpar->format                    = AV_SAMPLE_FMT_S16P;

    if (asf->ckhdr.flags & ASF_CF_STEREO) {
        st->codecpar->channel_layout        = AV_CH_LAYOUT_STEREO;
        st->codecpar->channels              = 2;
    } else {
        st->codecpar->channel_layout        = AV_CH_LAYOUT_MONO;
        st->codecpar->channels              = 1;
    }

    st->codecpar->sample_rate               = asf->ckhdr.sample_rate;

    st->codecpar->bits_per_coded_sample     = 4;

    if (asf->ckhdr.flags & ASF_CF_BITS_PER_SAMPLE)
        st->codecpar->bits_per_raw_sample   = 16;
    else
        st->codecpar->bits_per_raw_sample   = 8;

    if (st->codecpar->bits_per_raw_sample != 16) {
        /* The header allows for these, but I've never seen any files with them. */
        avpriv_request_sample(s, "Non 16-bit samples");
        return AVERROR_PATCHWELCOME;
    }

    /*
     * (nchannel control bytes) + ((bytes_per_channel) * nchannel)
     * For mono, this is 17. For stereo, this is 34.
     */
    st->codecpar->frame_size            = st->codecpar->channels +
                                          (asf->ckhdr.num_samples / 2) *
                                          st->codecpar->channels;

    st->codecpar->block_align           = st->codecpar->frame_size;

    st->codecpar->bit_rate              = st->codecpar->channels *
                                          st->codecpar->sample_rate *
                                          st->codecpar->bits_per_coded_sample;

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    st->start_time      = 0;
    st->duration        = asf->ckhdr.num_blocks * asf->ckhdr.num_samples;
    st->nb_frames       = asf->ckhdr.num_blocks;
    return 0;
}

static int argo_asf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ArgoASFDemuxContext *asf = s->priv_data;

    AVStream *st = s->streams[0];
    AVIOContext *pb = s->pb;
    int ret;

    if (asf->blocks_read >= asf->ckhdr.num_blocks)
        return AVERROR_EOF;

    if ((ret = av_get_packet(pb, pkt, st->codecpar->frame_size)) < 0)
        return ret;
    else if (ret != st->codecpar->frame_size)
        return AVERROR_INVALIDDATA;

    pkt->stream_index   = st->index;
    pkt->duration       = asf->ckhdr.num_samples;

    ++asf->blocks_read;
    return 0;
}

/*
 * Not actually sure what ASF stands for.
 * - Argonaut Sound File?
 * - Audio Stream File?
 */
AVInputFormat ff_argo_asf_demuxer = {
    .name           = "argo_asf",
    .long_name      = NULL_IF_CONFIG_SMALL("Argonaut Games ASF"),
    .priv_data_size = sizeof(ArgoASFDemuxContext),
    .read_probe     = argo_asf_probe,
    .read_header    = argo_asf_read_header,
    .read_packet    = argo_asf_read_packet
};
#endif

#if CONFIG_ARGO_ASF_MUXER
static int argo_asf_write_init(AVFormatContext *s)
{
    const AVCodecParameters *par;

    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "ASF files have exactly one stream\n");
        return AVERROR(EINVAL);
    }

    par = s->streams[0]->codecpar;

    if (par->codec_id != AV_CODEC_ID_ADPCM_ARGO) {
        av_log(s, AV_LOG_ERROR, "%s codec not supported\n",
               avcodec_get_name(par->codec_id));
        return AVERROR(EINVAL);
    }

    if (par->channels > 2) {
        av_log(s, AV_LOG_ERROR, "ASF files only support up to 2 channels\n");
        return AVERROR(EINVAL);
    }

    if (par->sample_rate > UINT16_MAX) {
        av_log(s, AV_LOG_ERROR, "Sample rate too large\n");
        return AVERROR(EINVAL);
    }

    if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        av_log(s, AV_LOG_ERROR, "Stream not seekable, unable to write output file\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static void argo_asf_write_file_header(const ArgoASFFileHeader *fhdr, AVIOContext *pb)
{
    avio_wl32( pb, fhdr->magic);
    avio_wl16( pb, fhdr->version_major);
    avio_wl16( pb, fhdr->version_minor);
    avio_wl32( pb, fhdr->num_chunks);
    avio_wl32( pb, fhdr->chunk_offset);
    avio_write(pb, fhdr->name, sizeof(fhdr->name));
}

static void argo_asf_write_chunk_header(const ArgoASFChunkHeader *ckhdr, AVIOContext *pb)
{
    avio_wl32(pb, ckhdr->num_blocks);
    avio_wl32(pb, ckhdr->num_samples);
    avio_wl32(pb, ckhdr->unk1);
    avio_wl16(pb, ckhdr->sample_rate);
    avio_wl16(pb, ckhdr->unk2);
    avio_wl32(pb, ckhdr->flags);
}

static int argo_asf_write_header(AVFormatContext *s)
{
    const AVCodecParameters  *par = s->streams[0]->codecpar;
    ArgoASFMuxContext        *ctx = s->priv_data;
    ArgoASFFileHeader  fhdr;
    ArgoASFChunkHeader chdr;

    fhdr.magic         = ASF_TAG;
    fhdr.version_major = (uint16_t)ctx->version_major;
    fhdr.version_minor = (uint16_t)ctx->version_minor;
    fhdr.num_chunks    = 1;
    fhdr.chunk_offset  = ASF_FILE_HEADER_SIZE;
    /*
     * If the user specified a name, use it as is. Otherwise take the
     * basename and lop off the extension (if any).
     */
    if (ctx->name) {
        strncpy(fhdr.name, ctx->name, sizeof(fhdr.name));
    } else {
        const char *start = av_basename(s->url);
        const char *end   = strrchr(start, '.');
        size_t      len;

        if(end)
            len = end - start;
        else
            len = strlen(start);

        memcpy(fhdr.name, start, FFMIN(len, sizeof(fhdr.name)));
    }

    chdr.num_blocks    = 0;
    chdr.num_samples   = ASF_SAMPLE_COUNT;
    chdr.unk1          = 0;
    chdr.sample_rate   = par->sample_rate;
    chdr.unk2          = ~0;
    chdr.flags         = ASF_CF_BITS_PER_SAMPLE | ASF_CF_ALWAYS1;

    if (par->channels == 2)
        chdr.flags |= ASF_CF_STEREO;

    argo_asf_write_file_header(&fhdr, s->pb);
    argo_asf_write_chunk_header(&chdr, s->pb);
    return 0;
}

static int argo_asf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    if (pkt->size != 17 * s->streams[0]->codecpar->channels)
        return AVERROR_INVALIDDATA;

    if (s->streams[0]->nb_frames >= UINT32_MAX)
        return AVERROR_INVALIDDATA;

    avio_write(s->pb, pkt->data, pkt->size);
    return 0;
}

static int argo_asf_write_trailer(AVFormatContext *s)
{
    int64_t ret;

    if ((ret = avio_seek(s->pb, ASF_FILE_HEADER_SIZE, SEEK_SET) < 0))
        return ret;

    avio_wl32(s->pb, (uint32_t)s->streams[0]->nb_frames);
    return 0;
}

static const AVOption argo_asf_options[] = {
    {
        .name        = "version_major",
        .help        = "override file major version",
        .offset      = offsetof(ArgoASFMuxContext, version_major),
        .type        = AV_OPT_TYPE_INT,
        .default_val = {.i64 = 2},
        .min         = 0,
        .max         = UINT16_MAX,
        .flags       = AV_OPT_FLAG_ENCODING_PARAM
    },
    {
        .name        = "version_minor",
        .help        = "override file minor version",
        .offset      = offsetof(ArgoASFMuxContext, version_minor),
        .type        = AV_OPT_TYPE_INT,
        .default_val = {.i64 = 1},
        .min         = 0,
        .max         = UINT16_MAX,
        .flags       = AV_OPT_FLAG_ENCODING_PARAM
    },
    {
        .name        = "name",
        .help        = "embedded file name (max 8 characters)",
        .offset      = offsetof(ArgoASFMuxContext, name),
        .type        = AV_OPT_TYPE_STRING,
        .default_val = {.str = NULL},
        .flags       = AV_OPT_FLAG_ENCODING_PARAM
    },
    { NULL }
};

static const AVClass argo_asf_muxer_class = {
    .class_name = "argo_asf_muxer",
    .item_name  = av_default_item_name,
    .option     = argo_asf_options,
    .version    = LIBAVUTIL_VERSION_INT
};

AVOutputFormat ff_argo_asf_muxer = {
    .name           = "argo_asf",
    .long_name      = NULL_IF_CONFIG_SMALL("Argonaut Games ASF"),
    /*
     * NB: Can't do this as it conflicts with the actual ASF format.
     * .extensions  = "asf",
     */
    .audio_codec    = AV_CODEC_ID_ADPCM_ARGO,
    .video_codec    = AV_CODEC_ID_NONE,
    .init           = argo_asf_write_init,
    .write_header   = argo_asf_write_header,
    .write_packet   = argo_asf_write_packet,
    .write_trailer  = argo_asf_write_trailer,
    .priv_class     = &argo_asf_muxer_class,
    .priv_data_size = sizeof(ArgoASFMuxContext)
};
#endif
