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
#include "argo_asf.h"

/* Maximum number of blocks to read at once. */
#define ASF_NB_BLOCKS 32

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
    int64_t        nb_blocks;
} ArgoASFMuxContext;

void ff_argo_asf_parse_file_header(ArgoASFFileHeader *hdr, const uint8_t *buf)
{
    hdr->magic          = AV_RL32(buf + 0);
    hdr->version_major  = AV_RL16(buf + 4);
    hdr->version_minor  = AV_RL16(buf + 6);
    hdr->num_chunks     = AV_RL32(buf + 8);
    hdr->chunk_offset   = AV_RL32(buf + 12);
    for (int i = 0; i < FF_ARRAY_ELEMS(hdr->name); i++)
        hdr->name[i]    = AV_RL8(buf + 16 + i);
}

int ff_argo_asf_validate_file_header(AVFormatContext *s, const ArgoASFFileHeader *hdr)
{
    if (hdr->magic != ASF_TAG || hdr->num_chunks == 0)
        return AVERROR_INVALIDDATA;

    if (hdr->chunk_offset < ASF_FILE_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    return 0;
}

void ff_argo_asf_parse_chunk_header(ArgoASFChunkHeader *hdr, const uint8_t *buf)
{
    hdr->num_blocks     = AV_RL32(buf + 0);
    hdr->num_samples    = AV_RL32(buf + 4);
    hdr->unk1           = AV_RL32(buf + 8);
    hdr->sample_rate    = AV_RL16(buf + 12);
    hdr->unk2           = AV_RL16(buf + 14);
    hdr->flags          = AV_RL32(buf + 16);
}

int ff_argo_asf_fill_stream(AVFormatContext *s, AVStream *st, const ArgoASFFileHeader *fhdr,
                            const ArgoASFChunkHeader *ckhdr)
{
    if (ckhdr->num_samples != ASF_SAMPLE_COUNT) {
        av_log(s, AV_LOG_ERROR, "Invalid sample count. Got %u, expected %d\n",
               ckhdr->num_samples, ASF_SAMPLE_COUNT);
        return AVERROR_INVALIDDATA;
    }

    if ((ckhdr->flags & ASF_CF_ALWAYS1) != ASF_CF_ALWAYS1 || (ckhdr->flags & ASF_CF_ALWAYS0) != 0) {
        avpriv_request_sample(s, "Nonstandard flags (0x%08X)", ckhdr->flags);
        return AVERROR_PATCHWELCOME;
    }

    st->codecpar->codec_type                = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id                  = AV_CODEC_ID_ADPCM_ARGO;
    st->codecpar->format                    = AV_SAMPLE_FMT_S16P;

    if (ckhdr->flags & ASF_CF_STEREO) {
        st->codecpar->channel_layout        = AV_CH_LAYOUT_STEREO;
        st->codecpar->channels              = 2;
    } else {
        st->codecpar->channel_layout        = AV_CH_LAYOUT_MONO;
        st->codecpar->channels              = 1;
    }

    /* v1.1 files (FX Fighter) are all marked as 44100, but are actually 22050. */
    if (fhdr->version_major == 1 && fhdr->version_minor == 1)
        st->codecpar->sample_rate           = 22050;
    else
        st->codecpar->sample_rate           = ckhdr->sample_rate;

    st->codecpar->bits_per_coded_sample     = 4;

    if (ckhdr->flags & ASF_CF_BITS_PER_SAMPLE)
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
    st->codecpar->block_align           = st->codecpar->channels +
                                          (ckhdr->num_samples / 2) *
                                          st->codecpar->channels;

    st->codecpar->bit_rate              = st->codecpar->channels *
                                          st->codecpar->sample_rate *
                                          st->codecpar->bits_per_coded_sample;

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    st->start_time      = 0;

    if (fhdr->num_chunks == 1) {
        st->duration        = ckhdr->num_blocks * ckhdr->num_samples;
        st->nb_frames       = ckhdr->num_blocks;
    }

    return 0;
}

#if CONFIG_ARGO_ASF_DEMUXER
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

    ff_argo_asf_parse_file_header(&hdr, p->buf);

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
    uint8_t buf[ASF_MIN_BUFFER_SIZE];

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    if ((ret = avio_read(pb, buf, ASF_FILE_HEADER_SIZE)) < 0)
        return ret;
    else if (ret != ASF_FILE_HEADER_SIZE)
        return AVERROR(EIO);

    ff_argo_asf_parse_file_header(&asf->fhdr, buf);

    if ((ret = ff_argo_asf_validate_file_header(s, &asf->fhdr)) < 0)
        return ret;

    /* This should only be 1 in ASF files. >1 is fine if in BRP. */
    if (asf->fhdr.num_chunks != 1)
        return AVERROR_INVALIDDATA;

    if ((ret = avio_skip(pb, asf->fhdr.chunk_offset - ASF_FILE_HEADER_SIZE)) < 0)
        return ret;

    if ((ret = avio_read(pb, buf, ASF_CHUNK_HEADER_SIZE)) < 0)
        return ret;
    else if (ret != ASF_CHUNK_HEADER_SIZE)
        return AVERROR(EIO);

    ff_argo_asf_parse_chunk_header(&asf->ckhdr, buf);

    return ff_argo_asf_fill_stream(s, st, &asf->fhdr, &asf->ckhdr);
}

static int argo_asf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    ArgoASFDemuxContext *asf = s->priv_data;

    AVStream *st = s->streams[0];
    AVIOContext *pb = s->pb;
    int ret;

    if (asf->blocks_read >= asf->ckhdr.num_blocks)
        return AVERROR_EOF;

    ret = av_get_packet(pb, pkt, st->codecpar->block_align *
                        FFMIN(ASF_NB_BLOCKS, asf->ckhdr.num_blocks - asf->blocks_read));
    if (ret < 0)
        return ret;

    /* Something real screwy is going on. */
    if (ret % st->codecpar->block_align != 0)
        return AVERROR_INVALIDDATA;


    pkt->stream_index   = st->index;
    pkt->duration       = asf->ckhdr.num_samples * (ret / st->codecpar->block_align);
    pkt->pts            = asf->blocks_read * asf->ckhdr.num_samples;
    asf->blocks_read   += (ret / st->codecpar->block_align);

    pkt->flags &= ~AV_PKT_FLAG_CORRUPT;
    return 0;
}

static int argo_asf_seek(AVFormatContext *s, int stream_index,
                         int64_t pts, int flags)
{
    ArgoASFDemuxContext *asf = s->priv_data;
    AVStream *st             = s->streams[stream_index];
    int64_t offset;
    uint32_t block = pts / asf->ckhdr.num_samples;

    if (block >= asf->ckhdr.num_blocks)
        return -1;

    offset = asf->fhdr.chunk_offset + ASF_CHUNK_HEADER_SIZE +
             (block * st->codecpar->block_align);

    if ((offset = avio_seek(s->pb, offset, SEEK_SET)) < 0)
        return offset;

    asf->blocks_read = block;
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
    .read_packet    = argo_asf_read_packet,
    .read_seek      = argo_asf_seek,
};
#endif

#if CONFIG_ARGO_ASF_MUXER
static int argo_asf_write_init(AVFormatContext *s)
{
    ArgoASFMuxContext *ctx = s->priv_data;
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

    if (ctx->version_major == 1 && ctx->version_minor == 1 && par->sample_rate != 22050) {
        av_log(s, AV_LOG_ERROR, "ASF v1.1 files only support a sample rate of 22050\n");
        return AVERROR(EINVAL);
    }

    if (par->channels > 2) {
        av_log(s, AV_LOG_ERROR, "ASF files only support up to 2 channels\n");
        return AVERROR(EINVAL);
    }

    if (par->block_align != 17 * par->channels)
        return AVERROR(EINVAL);

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
    ArgoASFChunkHeader chdr;
    ArgoASFFileHeader  fhdr = {
        .magic         = ASF_TAG,
        .version_major = (uint16_t)ctx->version_major,
        .version_minor = (uint16_t)ctx->version_minor,
        .num_chunks    = 1,
        .chunk_offset  = ASF_FILE_HEADER_SIZE
    };

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

        if (end)
            len = end - start;
        else
            len = strlen(start);

        memcpy(fhdr.name, start, FFMIN(len, sizeof(fhdr.name)));
    }

    chdr.num_blocks    = 0;
    chdr.num_samples   = ASF_SAMPLE_COUNT;
    chdr.unk1          = 0;

    if (ctx->version_major == 1 && ctx->version_minor == 1)
        chdr.sample_rate = 44100;
    else
        chdr.sample_rate = par->sample_rate;

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
    ArgoASFMuxContext *ctx = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;
    int nb_blocks = pkt->size / par->block_align;

    if (pkt->size % par->block_align != 0)
        return AVERROR_INVALIDDATA;

    if (ctx->nb_blocks + nb_blocks > UINT32_MAX)
        return AVERROR_INVALIDDATA;

    avio_write(s->pb, pkt->data, pkt->size);

    ctx->nb_blocks += nb_blocks;
    return 0;
}

static int argo_asf_write_trailer(AVFormatContext *s)
{
    ArgoASFMuxContext *ctx = s->priv_data;
    int64_t ret;

    if ((ret = avio_seek(s->pb, ASF_FILE_HEADER_SIZE, SEEK_SET)) < 0)
        return ret;

    avio_wl32(s->pb, (uint32_t)ctx->nb_blocks);
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
