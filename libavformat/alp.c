/*
 * LEGO Racers ALP (.tun & .pcm) (de)muxer
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

#include "config_components.h"

#include "libavutil/channel_layout.h"
#include "avformat.h"
#include "internal.h"
#include "mux.h"
#include "rawenc.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/internal.h"
#include "libavutil/opt.h"

#define ALP_TAG            MKTAG('A', 'L', 'P', ' ')
#define ALP_MAX_READ_SIZE  4096

typedef struct ALPHeader {
    uint32_t    magic;          /*< Magic Number, {'A', 'L', 'P', ' '} */
    uint32_t    header_size;    /*< Header size (after this). */
    char        adpcm[6];       /*< "ADPCM" */
    uint8_t     unk1;           /*< Unknown */
    uint8_t     num_channels;   /*< Channel Count. */
    uint32_t    sample_rate;    /*< Sample rate, only if header_size >= 12. */
} ALPHeader;

typedef enum ALPType {
    ALP_TYPE_AUTO = 0, /*< Autodetect based on file extension. */
    ALP_TYPE_TUN  = 1, /*< Force a .TUN file. */
    ALP_TYPE_PCM  = 2, /*< Force a .PCM file. */
} ALPType;

typedef struct ALPMuxContext {
    const AVClass *class;
    ALPType type;
} ALPMuxContext;

#if CONFIG_ALP_DEMUXER
static int alp_probe(const AVProbeData *p)
{
    uint32_t i;

    if (AV_RL32(p->buf) != ALP_TAG)
        return 0;

    /* Only allowed header sizes are 8 and 12. */
    i = AV_RL32(p->buf + 4);
    if (i != 8 && i != 12)
        return 0;

    if (strncmp("ADPCM", p->buf + 8, 6) != 0)
        return 0;

    return AVPROBE_SCORE_MAX - 1;
}

static int alp_read_header(AVFormatContext *s)
{
    int ret;
    AVStream *st;
    ALPHeader *hdr = s->priv_data;
    AVCodecParameters *par;

    if ((hdr->magic = avio_rl32(s->pb)) != ALP_TAG)
        return AVERROR_INVALIDDATA;

    hdr->header_size = avio_rl32(s->pb);

    if (hdr->header_size != 8 && hdr->header_size != 12) {
        return AVERROR_INVALIDDATA;
    }

    if ((ret = avio_read(s->pb, hdr->adpcm, sizeof(hdr->adpcm))) < 0)
        return ret;
    else if (ret != sizeof(hdr->adpcm))
        return AVERROR(EIO);

    if (strncmp("ADPCM", hdr->adpcm, sizeof(hdr->adpcm)) != 0)
        return AVERROR_INVALIDDATA;

    hdr->unk1                   = avio_r8(s->pb);
    hdr->num_channels           = avio_r8(s->pb);

    if (hdr->header_size == 8) {
        /* .TUN music file */
        hdr->sample_rate        = 22050;

    } else {
        /* .PCM sound file */
        hdr->sample_rate        = avio_rl32(s->pb);
    }

    if (hdr->sample_rate > 44100) {
        avpriv_request_sample(s, "Sample Rate > 44100");
        return AVERROR_PATCHWELCOME;
    }

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    par                         = st->codecpar;
    par->codec_type             = AVMEDIA_TYPE_AUDIO;
    par->codec_id               = AV_CODEC_ID_ADPCM_IMA_ALP;
    par->format                 = AV_SAMPLE_FMT_S16;
    par->sample_rate            = hdr->sample_rate;

    if (hdr->num_channels > 2 || hdr->num_channels == 0)
        return AVERROR_INVALIDDATA;

    av_channel_layout_default(&par->ch_layout, hdr->num_channels);
    par->bits_per_coded_sample  = 4;
    par->block_align            = 1;
    par->bit_rate               = par->ch_layout.nb_channels *
                                  par->sample_rate *
                                  par->bits_per_coded_sample;

    avpriv_set_pts_info(st, 64, 1, par->sample_rate);
    return 0;
}

static int alp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    AVCodecParameters *par = s->streams[0]->codecpar;

    if ((ret = av_get_packet(s->pb, pkt, ALP_MAX_READ_SIZE)) < 0)
        return ret;

    pkt->flags         &= ~AV_PKT_FLAG_CORRUPT;
    pkt->stream_index   = 0;
    pkt->duration       = ret * 2 / par->ch_layout.nb_channels;

    return 0;
}

static int alp_seek(AVFormatContext *s, int stream_index,
                     int64_t pts, int flags)
{
    const ALPHeader *hdr = s->priv_data;

    if (pts != 0)
        return AVERROR(EINVAL);

    return avio_seek(s->pb, hdr->header_size + 8, SEEK_SET);
}

const AVInputFormat ff_alp_demuxer = {
    .name           = "alp",
    .long_name      = NULL_IF_CONFIG_SMALL("LEGO Racers ALP"),
    .priv_data_size = sizeof(ALPHeader),
    .read_probe     = alp_probe,
    .read_header    = alp_read_header,
    .read_packet    = alp_read_packet,
    .read_seek      = alp_seek,
};
#endif

#if CONFIG_ALP_MUXER

static int alp_write_init(AVFormatContext *s)
{
    ALPMuxContext *alp = s->priv_data;
    AVCodecParameters *par;

    if (alp->type == ALP_TYPE_AUTO) {
        if (av_match_ext(s->url, "pcm"))
            alp->type = ALP_TYPE_PCM;
        else
            alp->type = ALP_TYPE_TUN;
    }

    if (s->nb_streams != 1) {
        av_log(s, AV_LOG_ERROR, "Too many streams\n");
        return AVERROR(EINVAL);
    }

    par = s->streams[0]->codecpar;

    if (par->codec_id != AV_CODEC_ID_ADPCM_IMA_ALP) {
        av_log(s, AV_LOG_ERROR, "%s codec not supported\n",
               avcodec_get_name(par->codec_id));
        return AVERROR(EINVAL);
    }

    if (par->ch_layout.nb_channels > 2) {
        av_log(s, AV_LOG_ERROR, "A maximum of 2 channels are supported\n");
        return AVERROR(EINVAL);
    }

    if (par->sample_rate > 44100) {
        av_log(s, AV_LOG_ERROR, "Sample rate too large\n");
        return AVERROR(EINVAL);
    }

    if (alp->type == ALP_TYPE_TUN && par->sample_rate != 22050) {
        av_log(s, AV_LOG_ERROR, "Sample rate must be 22050 for TUN files\n");
        return AVERROR(EINVAL);
    }
    return 0;
}

static int alp_write_header(AVFormatContext *s)
{
    ALPMuxContext *alp = s->priv_data;
    AVCodecParameters *par = s->streams[0]->codecpar;

    avio_wl32(s->pb,  ALP_TAG);
    avio_wl32(s->pb,  alp->type == ALP_TYPE_PCM ? 12 : 8);
    avio_write(s->pb, "ADPCM", 6);
    avio_w8(s->pb,    0);
    avio_w8(s->pb,    par->ch_layout.nb_channels);
    if (alp->type == ALP_TYPE_PCM)
        avio_wl32(s->pb, par->sample_rate);

    return 0;
}

enum { AE = AV_OPT_FLAG_AUDIO_PARAM | AV_OPT_FLAG_ENCODING_PARAM };

static const AVOption alp_options[] = {
    {
        .name        = "type",
        .help        = "set file type",
        .offset      = offsetof(ALPMuxContext, type),
        .type        = AV_OPT_TYPE_INT,
        .default_val = {.i64 = ALP_TYPE_AUTO},
        .min         = ALP_TYPE_AUTO,
        .max         = ALP_TYPE_PCM,
        .flags       = AE,
        .unit        = "type",
    },
    {
        .name        = "auto",
        .help        = "autodetect based on file extension",
        .offset      = 0,
        .type        = AV_OPT_TYPE_CONST,
        .default_val = {.i64 = ALP_TYPE_AUTO},
        .min         = 0,
        .max         = 0,
        .flags       = AE,
        .unit        = "type"
    },
    {
        .name        = "tun",
        .help        = "force .tun, used for music",
        .offset      = 0,
        .type        = AV_OPT_TYPE_CONST,
        .default_val = {.i64 = ALP_TYPE_TUN},
        .min         = 0,
        .max         = 0,
        .flags       = AE,
        .unit        = "type"
    },
    {
        .name        = "pcm",
        .help        = "force .pcm, used for sfx",
        .offset      = 0,
        .type        = AV_OPT_TYPE_CONST,
        .default_val = {.i64 = ALP_TYPE_PCM},
        .min         = 0,
        .max         = 0,
        .flags       = AE,
        .unit        = "type"
    },
    { NULL }
};

static const AVClass alp_muxer_class = {
    .class_name = "alp",
    .item_name  = av_default_item_name,
    .option     = alp_options,
    .version    = LIBAVUTIL_VERSION_INT
};

const FFOutputFormat ff_alp_muxer = {
    .p.name         = "alp",
    .p.long_name    = NULL_IF_CONFIG_SMALL("LEGO Racers ALP"),
    .p.extensions   = "tun,pcm",
    .p.audio_codec  = AV_CODEC_ID_ADPCM_IMA_ALP,
    .p.video_codec  = AV_CODEC_ID_NONE,
    .p.priv_class   = &alp_muxer_class,
    .init           = alp_write_init,
    .write_header   = alp_write_header,
    .write_packet   = ff_raw_write_packet,
    .priv_data_size = sizeof(ALPMuxContext)
};
#endif
