/*
 * Argonaut Games CVG demuxer
 *
 * Copyright (C) 2021 Zane van Iperen (zane@zanevaniperen.com)
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

/*
 * .CVG files are essentially PSX ADPCM wrapped with a size and checksum.
 * Found in the PSX versions of the game.
 */

#define ARGO_CVG_HEADER_SIZE        12
#define ARGO_CVG_BLOCK_ALIGN        0x10
#define ARGO_CVG_NB_BLOCKS          32
#define ARGO_CVG_SAMPLES_PER_BLOCK  28

typedef struct ArgoCVGHeader {
    uint32_t size; /*< File size -8 (this + trailing checksum) */
    uint32_t unk1; /*< Unknown. Always seems to be 0 or 1. */
    uint32_t unk2; /*< Unknown. Always seems to be 0 or 1. */
} ArgoCVGHeader;

typedef struct ArgoCVGOverride {
    const char    *name;
    ArgoCVGHeader header;
    uint32_t      checksum;
    int           sample_rate;
} ArgoCVGOverride;

typedef struct ArgoCVGDemuxContext {
    ArgoCVGHeader header;
    uint32_t      checksum;
    uint32_t      num_blocks;
    uint32_t      blocks_read;
} ArgoCVGDemuxContext;

/* "Special" files that are played at a different rate. */
static ArgoCVGOverride overrides[] = {
    { "CRYS.CVG",     { 23592, 0, 1 }, 2495499, 88200 }, /* Beta */
    { "REDCRY88.CVG", { 38280, 0, 1 }, 4134848, 88200 }, /* Beta */
    { "DANLOOP1.CVG", { 54744, 1, 0 }, 5684641, 37800 }, /* Beta */
    { "PICKUP88.CVG", { 12904, 0, 1 }, 1348091, 48000 }, /* Beta */
    { "SELECT1.CVG",  {  5080, 0, 1 },  549987, 44100 }, /* Beta */
};

static int argo_cvg_probe(const AVProbeData *p)
{
    ArgoCVGHeader cvg;

    /*
     * It's almost impossible to detect these files based
     * on the header alone. File extension is (unfortunately)
     * the best way forward.
     */
    if (!av_match_ext(p->filename, "cvg"))
        return 0;

    if (p->buf_size < ARGO_CVG_HEADER_SIZE)
        return 0;

    cvg.size = AV_RL32(p->buf + 0);
    cvg.unk1 = AV_RL32(p->buf + 4);
    cvg.unk2 = AV_RL32(p->buf + 8);

    if (cvg.size < 8)
        return 0;

    if (cvg.unk1 != 0 && cvg.unk1 != 1)
        return 0;

    if (cvg.unk2 != 0 && cvg.unk2 != 1)
        return 0;

    return AVPROBE_SCORE_MAX / 4 + 1;
}

static int argo_cvg_read_checksum(AVIOContext *pb, const ArgoCVGHeader *cvg, uint32_t *checksum)
{
    int ret;
    uint8_t buf[4];

    if (!(pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        *checksum = 0;
        return 0;
    }

    if ((ret = avio_seek(pb, cvg->size + 4, SEEK_SET)) < 0)
        return ret;

    /* NB: Not using avio_rl32() because no error checking. */
    if ((ret = avio_read(pb, buf, sizeof(buf))) < 0)
        return ret;
    else if (ret != sizeof(buf))
        return AVERROR(EIO);

    if ((ret = avio_seek(pb, ARGO_CVG_HEADER_SIZE, SEEK_SET)) < 0)
        return ret;

    *checksum = AV_RL32(buf);
    return 0;
}

static int argo_cvg_read_header(AVFormatContext *s)
{
    int ret;
    AVStream *st;
    AVCodecParameters *par;
    uint8_t buf[ARGO_CVG_HEADER_SIZE];
    const char *filename = av_basename(s->url);
    ArgoCVGDemuxContext *ctx = s->priv_data;

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    if ((ret = avio_read(s->pb, buf, ARGO_CVG_HEADER_SIZE)) < 0)
        return ret;
    else if (ret != ARGO_CVG_HEADER_SIZE)
        return AVERROR(EIO);

    ctx->header.size = AV_RL32(buf + 0);
    ctx->header.unk1 = AV_RL32(buf + 4);
    ctx->header.unk2 = AV_RL32(buf + 8);

    if (ctx->header.size < 8)
        return AVERROR_INVALIDDATA;

    av_log(s, AV_LOG_TRACE, "size       = %u\n", ctx->header.size);
    av_log(s, AV_LOG_TRACE, "unk        = %u, %u\n", ctx->header.unk1, ctx->header.unk2);

    if ((ret = argo_cvg_read_checksum(s->pb, &ctx->header, &ctx->checksum)) < 0)
        return ret;

    av_log(s, AV_LOG_TRACE, "checksum   = %u\n", ctx->checksum);

    par                         = st->codecpar;
    par->codec_type             = AVMEDIA_TYPE_AUDIO;
    par->codec_id               = AV_CODEC_ID_ADPCM_PSX;
    par->sample_rate            = 22050;

    for (size_t i = 0; i < FF_ARRAY_ELEMS(overrides); i++) {
        const ArgoCVGOverride *ovr = overrides + i;
        if (ovr->header.size != ctx->header.size ||
            ovr->header.unk1 != ctx->header.unk1 ||
            ovr->header.unk2 != ctx->header.unk2 ||
            ovr->checksum    != ctx->checksum    ||
            av_strcasecmp(filename, ovr->name) != 0)
            continue;

        av_log(s, AV_LOG_TRACE, "found override, name = %s\n", ovr->name);
        par->sample_rate = ovr->sample_rate;
        break;
    }

    par->channels               = 1;
    par->channel_layout         = AV_CH_LAYOUT_MONO;

    par->bits_per_coded_sample  = 4;
    par->bits_per_raw_sample    = 16;
    par->block_align            = ARGO_CVG_BLOCK_ALIGN;
    par->bit_rate               = par->sample_rate * par->bits_per_coded_sample;

    ctx->num_blocks = (ctx->header.size - 8) / ARGO_CVG_BLOCK_ALIGN;

    av_log(s, AV_LOG_TRACE, "num blocks = %u\n", ctx->num_blocks);

    avpriv_set_pts_info(st, 64, 1, par->sample_rate);

    st->start_time = 0;
    st->duration   = ctx->num_blocks * ARGO_CVG_SAMPLES_PER_BLOCK;
    st->nb_frames  = ctx->num_blocks;
    return 0;
}

static int argo_cvg_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    AVStream *st = s->streams[0];
    ArgoCVGDemuxContext *ctx = s->priv_data;

    if (ctx->blocks_read >= ctx->num_blocks)
        return AVERROR_EOF;

    ret = av_get_packet(s->pb, pkt, st->codecpar->block_align *
                        FFMIN(ARGO_CVG_NB_BLOCKS, ctx->num_blocks - ctx->blocks_read));

    if (ret < 0)
        return ret;

    if (ret % st->codecpar->block_align != 0)
        return AVERROR_INVALIDDATA;

    pkt->stream_index   = 0;
    pkt->duration       = ARGO_CVG_SAMPLES_PER_BLOCK * (ret / st->codecpar->block_align);
    pkt->pts            = ctx->blocks_read * ARGO_CVG_SAMPLES_PER_BLOCK;
    pkt->flags         &= ~AV_PKT_FLAG_CORRUPT;

    ctx->blocks_read   += ret / st->codecpar->block_align;

    return 0;
}

static int argo_cvg_seek(AVFormatContext *s, int stream_index,
                        int64_t pts, int flags)
{
    int64_t ret;
    ArgoCVGDemuxContext *ctx = s->priv_data;

    if (pts != 0 || stream_index != 0)
        return AVERROR(EINVAL);

    if ((ret = avio_seek(s->pb, ARGO_CVG_HEADER_SIZE, SEEK_SET)) < 0)
        return ret;

    ctx->blocks_read = 0;
    return 0;
}

const AVInputFormat ff_argo_cvg_demuxer = {
    .name           = "argo_cvg",
    .long_name      = NULL_IF_CONFIG_SMALL("Argonaut Games CVG"),
    .priv_data_size = sizeof(ArgoCVGDemuxContext),
    .read_probe     = argo_cvg_probe,
    .read_header    = argo_cvg_read_header,
    .read_packet    = argo_cvg_read_packet,
    .read_seek      = argo_cvg_seek,
};
