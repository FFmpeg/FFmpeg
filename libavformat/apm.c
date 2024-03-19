/*
 * Rayman 2 APM (De)muxer
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

#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "mux.h"
#include "rawenc.h"
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/intreadwrite.h"

#define APM_FILE_HEADER_SIZE    20
#define APM_FILE_EXTRADATA_SIZE 80
#define APM_EXTRADATA_SIZE      28

#define APM_MAX_READ_SIZE       4096

#define APM_TAG_CODEC           0x2000
#define APM_TAG_VS12            MKTAG('v', 's', '1', '2')
#define APM_TAG_DATA            MKTAG('D', 'A', 'T', 'A')

typedef struct APMState {
    int32_t     has_saved;
    int32_t     predictor_r;
    int32_t     step_index_r;
    int32_t     saved_r;
    int32_t     predictor_l;
    int32_t     step_index_l;
    int32_t     saved_l;
} APMState;

typedef struct APMExtraData {
    uint32_t    magic;
    uint32_t    file_size;
    uint32_t    data_size;
    uint32_t    unk1;
    uint32_t    unk2;
    APMState    state;
    uint32_t    unk3[7];
    uint32_t    data;
} APMExtraData;

#if CONFIG_APM_DEMUXER
static void apm_parse_extradata(APMExtraData *ext, const uint8_t *buf)
{
    ext->magic              = AV_RL32(buf + 0);
    ext->file_size          = AV_RL32(buf + 4);
    ext->data_size          = AV_RL32(buf + 8);
    ext->unk1               = AV_RL32(buf + 12);
    ext->unk2               = AV_RL32(buf + 16);

    ext->state.has_saved    = AV_RL32(buf + 20);
    ext->state.predictor_r  = AV_RL32(buf + 24);
    ext->state.step_index_r = AV_RL32(buf + 28);
    ext->state.saved_r      = AV_RL32(buf + 32);
    ext->state.predictor_l  = AV_RL32(buf + 36);
    ext->state.step_index_l = AV_RL32(buf + 40);
    ext->state.saved_l      = AV_RL32(buf + 44);

    for (int i = 0; i < FF_ARRAY_ELEMS(ext->unk3); i++)
        ext->unk3[i]        = AV_RL32(buf + 48 + (i * 4));

    ext->data               = AV_RL32(buf + 76);
}

static int apm_probe(const AVProbeData *p)
{
    if (AV_RL16(p->buf) != APM_TAG_CODEC)
        return 0;

    if (p->buf_size < 100)
        return 0;

    if (AV_RL32(p->buf + 20) != APM_TAG_VS12)
        return 0;

    if (AV_RL32(p->buf + 96) != APM_TAG_DATA)
        return 0;

    return AVPROBE_SCORE_MAX - 1;
}

static int apm_read_header(AVFormatContext *s)
{
    int64_t ret;
    AVStream *st;
    APMExtraData extradata;
    AVCodecParameters *par;
    uint8_t buf[APM_FILE_EXTRADATA_SIZE];
    int channels;

    if (!(st = avformat_new_stream(s, NULL)))
        return AVERROR(ENOMEM);

    /*
     * This is 98% a WAVEFORMATEX, but there's something screwy with the extradata
     * that ff_get_wav_header() can't (and shouldn't) handle properly.
     */
    if (avio_rl16(s->pb) != APM_TAG_CODEC)
        return AVERROR_INVALIDDATA;

    par = st->codecpar;
    channels                   = avio_rl16(s->pb);
    par->sample_rate           = avio_rl32(s->pb);

    /* Skip the bitrate, it's usually wrong anyway. */
    if ((ret = avio_skip(s->pb, 4)) < 0)
        return ret;

    par->block_align           = avio_rl16(s->pb);
    par->bits_per_coded_sample = avio_rl16(s->pb);

    if (avio_rl32(s->pb) != APM_FILE_EXTRADATA_SIZE)
        return AVERROR_INVALIDDATA;

    /* 8 = bits per sample * max channels */
    if (par->sample_rate > (INT_MAX / 8))
        return AVERROR_INVALIDDATA;

    if (par->bits_per_coded_sample != 4)
        return AVERROR_INVALIDDATA;

    if (channels > 2 || channels == 0)
        return AVERROR_INVALIDDATA;

    av_channel_layout_default(&par->ch_layout, channels);
    par->codec_type            = AVMEDIA_TYPE_AUDIO;
    par->codec_id              = AV_CODEC_ID_ADPCM_IMA_APM;
    par->format                = AV_SAMPLE_FMT_S16;
    par->bit_rate              = par->ch_layout.nb_channels *
                                 (int64_t)par->sample_rate *
                                 par->bits_per_coded_sample;

    if ((ret = avio_read(s->pb, buf, APM_FILE_EXTRADATA_SIZE)) < 0)
        return ret;
    else if (ret != APM_FILE_EXTRADATA_SIZE)
        return AVERROR(EIO);

    apm_parse_extradata(&extradata, buf);

    if (extradata.magic != APM_TAG_VS12 || extradata.data != APM_TAG_DATA)
        return AVERROR_INVALIDDATA;

    if (extradata.state.has_saved) {
        avpriv_request_sample(s, "Saved Samples");
        return AVERROR_PATCHWELCOME;
    }

    if ((ret = ff_alloc_extradata(par, APM_EXTRADATA_SIZE)) < 0)
        return ret;

    /* Use the entire state as extradata. */
    memcpy(par->extradata, buf + 20, APM_EXTRADATA_SIZE);

    avpriv_set_pts_info(st, 64, 1, par->sample_rate);
    st->start_time  = 0;
    st->duration    = extradata.data_size *
                      (8 / par->bits_per_coded_sample) /
                      par->ch_layout.nb_channels;
    return 0;
}

static int apm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;
    AVCodecParameters *par = s->streams[0]->codecpar;

    /*
     * For future reference: if files with the `has_saved` field set ever
     * surface, `saved_l`, and `saved_r` will each contain 8 "saved" samples
     * that should be sent to the decoder before the actual data.
     */

    if ((ret = av_get_packet(s->pb, pkt, APM_MAX_READ_SIZE)) < 0)
        return ret;

    pkt->flags          &= ~AV_PKT_FLAG_CORRUPT;
    pkt->stream_index   = 0;
    pkt->duration       = ret * (8 / par->bits_per_coded_sample) / par->ch_layout.nb_channels;

    return 0;
}

const FFInputFormat ff_apm_demuxer = {
    .p.name         = "apm",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Ubisoft Rayman 2 APM"),
    .read_probe     = apm_probe,
    .read_header    = apm_read_header,
    .read_packet    = apm_read_packet
};
#endif

#if CONFIG_APM_MUXER
static int apm_write_init(AVFormatContext *s)
{
    AVCodecParameters *par = s->streams[0]->codecpar;

    if (par->ch_layout.nb_channels > 2) {
        av_log(s, AV_LOG_ERROR, "APM files only support up to 2 channels\n");
        return AVERROR(EINVAL);
    }

    if (par->sample_rate > (INT_MAX / 8)) {
        av_log(s, AV_LOG_ERROR, "Sample rate too large\n");
        return AVERROR(EINVAL);
    }

    if (par->extradata_size != APM_EXTRADATA_SIZE) {
        av_log(s, AV_LOG_ERROR, "Invalid/missing extradata\n");
        return AVERROR(EINVAL);
    }

    if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        av_log(s, AV_LOG_ERROR, "Stream not seekable, unable to write output file\n");
        return AVERROR(EINVAL);
    }

    return 0;
}

static int apm_write_header(AVFormatContext *s)
{
    uint8_t buf[APM_FILE_EXTRADATA_SIZE] = { 0 };
    AVCodecParameters *par = s->streams[0]->codecpar;

    /*
     * Bodge a WAVEFORMATEX manually, ff_put_wav_header() can't
     * be used because of the extra 2 bytes.
     */
    avio_wl16(s->pb, APM_TAG_CODEC);
    avio_wl16(s->pb, par->ch_layout.nb_channels);
    avio_wl32(s->pb, par->sample_rate);
    /* This is the wrong calculation, but it's what the orginal files have. */
    avio_wl32(s->pb, par->sample_rate * par->ch_layout.nb_channels * 2);
    avio_wl16(s->pb, par->block_align);
    avio_wl16(s->pb, par->bits_per_coded_sample);
    avio_wl32(s->pb, APM_FILE_EXTRADATA_SIZE);

    /*
     * Build the extradata. Assume the codec's given us correct data.
     * File and data sizes are fixed later.
     */
    AV_WL32(buf +  0, APM_TAG_VS12); /* magic */
    AV_WL32(buf + 12, 0xFFFFFFFF);   /* unk1  */
    memcpy( buf + 20, par->extradata, APM_EXTRADATA_SIZE);
    AV_WL32(buf + 76, APM_TAG_DATA); /* data */

    avio_write(s->pb, buf, APM_FILE_EXTRADATA_SIZE);
    return 0;
}

static int apm_write_trailer(AVFormatContext *s)
{
    int64_t file_size, data_size;

    file_size = avio_tell(s->pb);
    data_size = file_size - (APM_FILE_HEADER_SIZE + APM_FILE_EXTRADATA_SIZE);

    if (file_size >= UINT32_MAX) {
        av_log(s, AV_LOG_ERROR,
               "Filesize %"PRId64" invalid for APM, output file will be broken\n",
               file_size);
        return AVERROR(ERANGE);
    }

    avio_seek(s->pb, 24, SEEK_SET);
    avio_wl32(s->pb, (uint32_t)file_size);
    avio_wl32(s->pb, (uint32_t)data_size);

    return 0;
}

const FFOutputFormat ff_apm_muxer = {
    .p.name         = "apm",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Ubisoft Rayman 2 APM"),
    .p.extensions   = "apm",
    .p.audio_codec  = AV_CODEC_ID_ADPCM_IMA_APM,
    .p.video_codec  = AV_CODEC_ID_NONE,
    .p.subtitle_codec = AV_CODEC_ID_NONE,
    .flags_internal   = FF_OFMT_FLAG_MAX_ONE_OF_EACH |
                        FF_OFMT_FLAG_ONLY_DEFAULT_CODECS,
    .init           = apm_write_init,
    .write_header   = apm_write_header,
    .write_packet   = ff_raw_write_packet,
    .write_trailer  = apm_write_trailer
};
#endif
