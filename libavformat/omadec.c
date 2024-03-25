/*
 * Sony OpenMG (OMA) demuxer
 *
 * Copyright (c) 2008, 2013 Maxim Poliakovski
 *               2008 Benjamin Larsson
 *               2011 David Goldwich
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
 * This is a demuxer for Sony OpenMG Music files
 *
 * Known file extensions: ".oma", "aa3"
 * The format of such files consists of three parts:
 * - "ea3" header carrying overall info and metadata. Except for starting with
 *   "ea" instead of "ID", it's an ID3v2 header.
 * - "EA3" header is a Sony-specific header containing information about
 *   the OpenMG file: codec type (usually ATRAC, can also be MP3 or WMA),
 *   codec specific info (packet size, sample rate, channels and so on)
 *   and DRM related info (file encryption, content id).
 * - Sound data organized in packets follow the EA3 header
 *   (can be encrypted using the Sony DRM!).
 *
 * Supported decoders: ATRAC3, ATRAC3+, MP3, LPCM
 */

#include <inttypes.h>

#include "libavutil/channel_layout.h"
#include "libavutil/mem.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/des.h"
#include "libavutil/mathematics.h"
#include "oma.h"
#include "pcm.h"
#include "id3v2.h"


static const uint64_t leaf_table[] = {
    0xd79e8283acea4620, 0x7a9762f445afd0d8,
    0x354d60a60b8c79f1, 0x584e1cde00b07aee,
    0x1573cd93da7df623, 0x47f98d79620dd535
};

/** map ATRAC-X channel id to internal channel layout */
static const AVChannelLayout  oma_chid_to_native_layout[7] = {
    AV_CHANNEL_LAYOUT_MONO,
    AV_CHANNEL_LAYOUT_STEREO,
    AV_CHANNEL_LAYOUT_SURROUND,
    AV_CHANNEL_LAYOUT_4POINT0,
    AV_CHANNEL_LAYOUT_5POINT1_BACK,
    AV_CHANNEL_LAYOUT_6POINT1_BACK,
    AV_CHANNEL_LAYOUT_7POINT1
};

typedef struct OMAContext {
    uint64_t content_start;
    int encrypted;
    uint16_t k_size;
    uint16_t e_size;
    uint16_t i_size;
    uint16_t s_size;
    uint32_t rid;
    uint8_t r_val[24];
    uint8_t n_val[24];
    uint8_t m_val[8];
    uint8_t s_val[8];
    uint8_t sm_val[8];
    uint8_t e_val[8];
    uint8_t iv[8];
    struct AVDES *av_des;

    int (*read_packet)(AVFormatContext *s, AVPacket *pkt);
} OMAContext;

static int oma_read_close(AVFormatContext *s)
{
    OMAContext *oc = s->priv_data;
    av_freep(&oc->av_des);
    return 0;
}

static void hex_log(AVFormatContext *s, int level,
                    const char *name, const uint8_t *value, int len)
{
    char buf[33];
    len = FFMIN(len, 16);
    if (av_log_get_level() < level)
        return;
    ff_data_to_hex(buf, value, len, 1);
    av_log(s, level, "%s: %s\n", name, buf);
}

static int kset(AVFormatContext *s, const uint8_t *r_val, const uint8_t *n_val,
                int len)
{
    OMAContext *oc = s->priv_data;

    if (!r_val && !n_val)
        return -1;

    len = FFMIN(len, 16);

    /* use first 64 bits in the third round again */
    if (r_val) {
        if (r_val != oc->r_val) {
            memset(oc->r_val, 0, 24);
            memcpy(oc->r_val, r_val, len);
        }
        memcpy(&oc->r_val[16], r_val, 8);
    }
    if (n_val) {
        if (n_val != oc->n_val) {
            memset(oc->n_val, 0, 24);
            memcpy(oc->n_val, n_val, len);
        }
        memcpy(&oc->n_val[16], n_val, 8);
    }

    return 0;
}

#define OMA_RPROBE_M_VAL 48 + 1

static int rprobe(AVFormatContext *s, uint8_t *enc_header, unsigned size,
                  const uint8_t *r_val)
{
    OMAContext *oc = s->priv_data;
    unsigned int pos;
    struct AVDES *av_des;

    if (!enc_header || !r_val ||
        size < OMA_ENC_HEADER_SIZE + oc->k_size + oc->e_size + oc->i_size ||
        size < OMA_RPROBE_M_VAL)
        return -1;

    av_des = av_des_alloc();
    if (!av_des)
        return AVERROR(ENOMEM);

    /* m_val */
    av_des_init(av_des, r_val, 192, 1);
    av_des_crypt(av_des, oc->m_val, &enc_header[48], 1, NULL, 1);

    /* s_val */
    av_des_init(av_des, oc->m_val, 64, 0);
    av_des_crypt(av_des, oc->s_val, NULL, 1, NULL, 0);

    /* sm_val */
    pos = OMA_ENC_HEADER_SIZE + oc->k_size + oc->e_size;
    av_des_init(av_des, oc->s_val, 64, 0);
    av_des_mac(av_des, oc->sm_val, &enc_header[pos], (oc->i_size >> 3));

    pos += oc->i_size;

    av_free(av_des);

    return memcmp(&enc_header[pos], oc->sm_val, 8) ? -1 : 0;
}

static int nprobe(AVFormatContext *s, uint8_t *enc_header, unsigned size,
                  const uint8_t *n_val)
{
    OMAContext *oc = s->priv_data;
    uint64_t pos;
    uint32_t taglen, datalen;
    struct AVDES *av_des;

    if (!enc_header || !n_val ||
        size < OMA_ENC_HEADER_SIZE + oc->k_size + 4)
        return -1;

    pos = OMA_ENC_HEADER_SIZE + oc->k_size;
    if (!memcmp(&enc_header[pos], "EKB ", 4))
        pos += 32;

    if (size < pos + 44)
        return -1;

    if (AV_RB32(&enc_header[pos]) != oc->rid)
        av_log(s, AV_LOG_DEBUG, "Mismatching RID\n");

    taglen  = AV_RB32(&enc_header[pos + 32]);
    datalen = AV_RB32(&enc_header[pos + 36]) >> 4;

    pos += 44LL + taglen;

    if (pos + (((uint64_t)datalen) << 4) > size)
        return -1;

    av_des = av_des_alloc();
    if (!av_des)
        return AVERROR(ENOMEM);

    av_des_init(av_des, n_val, 192, 1);
    while (datalen-- > 0) {
        av_des_crypt(av_des, oc->r_val, &enc_header[pos], 2, NULL, 1);
        kset(s, oc->r_val, NULL, 16);
        if (!rprobe(s, enc_header, size, oc->r_val)) {
            av_free(av_des);
            return 0;
        }
        pos += 16;
    }

    av_free(av_des);
    return -1;
}

static int decrypt_init(AVFormatContext *s, ID3v2ExtraMeta *em, uint8_t *header)
{
    OMAContext *oc = s->priv_data;
    ID3v2ExtraMetaGEOB *geob = NULL;
    uint8_t *gdata;

    oc->encrypted = 1;
    av_log(s, AV_LOG_INFO, "File is encrypted\n");

    /* find GEOB metadata */
    for (; em; em = em->next) {
        if (strcmp(em->tag, "GEOB"))
            continue;
        geob = &em->data.geob;
        if (!strcmp(geob->description, "OMG_LSI") ||
            !strcmp(geob->description, "OMG_BKLSI"))
            break;
    }
    if (!em) {
        av_log(s, AV_LOG_ERROR, "No encryption header found\n");
        return AVERROR_INVALIDDATA;
    }

    if (geob->datasize < 64) {
        av_log(s, AV_LOG_ERROR,
               "Invalid GEOB data size: %"PRIu32"\n", geob->datasize);
        return AVERROR_INVALIDDATA;
    }

    gdata = geob->data;

    if (AV_RB16(gdata) != 1)
        av_log(s, AV_LOG_WARNING, "Unknown version in encryption header\n");

    oc->k_size = AV_RB16(&gdata[2]);
    oc->e_size = AV_RB16(&gdata[4]);
    oc->i_size = AV_RB16(&gdata[6]);
    oc->s_size = AV_RB16(&gdata[8]);

    if (memcmp(&gdata[OMA_ENC_HEADER_SIZE], "KEYRING     ", 12)) {
        av_log(s, AV_LOG_ERROR, "Invalid encryption header\n");
        return AVERROR_INVALIDDATA;
    }
    if (OMA_ENC_HEADER_SIZE + oc->k_size + oc->e_size + oc->i_size + 8 > geob->datasize ||
        OMA_ENC_HEADER_SIZE + 48 > geob->datasize) {
        av_log(s, AV_LOG_ERROR, "Too little GEOB data\n");
        return AVERROR_INVALIDDATA;
    }
    oc->rid = AV_RB32(&gdata[OMA_ENC_HEADER_SIZE + 28]);
    av_log(s, AV_LOG_DEBUG, "RID: %.8"PRIx32"\n", oc->rid);

    memcpy(oc->iv, &header[0x58], 8);
    hex_log(s, AV_LOG_DEBUG, "IV", oc->iv, 8);

    hex_log(s, AV_LOG_DEBUG, "CBC-MAC",
            &gdata[OMA_ENC_HEADER_SIZE + oc->k_size + oc->e_size + oc->i_size],
            8);

    if (s->keylen > 0) {
        kset(s, s->key, s->key, s->keylen);
    }
    if (!memcmp(oc->r_val, (const uint8_t[8]){0}, 8) ||
        rprobe(s, gdata, geob->datasize, oc->r_val) < 0 &&
        nprobe(s, gdata, geob->datasize, oc->n_val) < 0) {
        int i;
        for (i = 0; i < FF_ARRAY_ELEMS(leaf_table); i += 2) {
            uint8_t buf[16];
            AV_WL64(buf,     leaf_table[i]);
            AV_WL64(&buf[8], leaf_table[i + 1]);
            kset(s, buf, buf, 16);
            if (!rprobe(s, gdata, geob->datasize, oc->r_val) ||
                !nprobe(s, gdata, geob->datasize, oc->n_val))
                break;
        }
        if (i >= FF_ARRAY_ELEMS(leaf_table)) {
            av_log(s, AV_LOG_ERROR, "Invalid key\n");
            return AVERROR_INVALIDDATA;
        }
    }

    oc->av_des = av_des_alloc();
    if (!oc->av_des)
        return AVERROR(ENOMEM);

    /* e_val */
    av_des_init(oc->av_des, oc->m_val, 64, 0);
    av_des_crypt(oc->av_des, oc->e_val,
                 &gdata[OMA_ENC_HEADER_SIZE + 40], 1, NULL, 0);
    hex_log(s, AV_LOG_DEBUG, "EK", oc->e_val, 8);

    /* init e_val */
    av_des_init(oc->av_des, oc->e_val, 64, 1);

    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    OMAContext *oc  = s->priv_data;
    AVStream *st    = s->streams[0];
    int packet_size = st->codecpar->block_align;
    int byte_rate   = st->codecpar->bit_rate >> 3;
    int64_t pos     = avio_tell(s->pb);
    int ret         = av_get_packet(s->pb, pkt, packet_size);

    if (ret < packet_size)
        pkt->flags |= AV_PKT_FLAG_CORRUPT;

    if (ret < 0)
        return ret;
    if (!ret)
        return AVERROR_EOF;

    pkt->stream_index = 0;

    if (pos >= oc->content_start && byte_rate > 0) {
        pkt->pts =
        pkt->dts = av_rescale(pos - oc->content_start, st->time_base.den,
                              byte_rate * (int64_t)st->time_base.num);
    }

    if (oc->encrypted) {
        /* previous unencrypted block saved in IV for
         * the next packet (CBC mode) */
        if (ret == packet_size)
            av_des_crypt(oc->av_des, pkt->data, pkt->data,
                         (packet_size >> 3), oc->iv, 1);
        else
            memset(oc->iv, 0, 8);
    }

    return ret;
}

static int aal_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int64_t pos = avio_tell(s->pb);
    int ret, pts;
    int packet_size;
    unsigned tag;

    if (avio_feof(s->pb))
        return AVERROR_EOF;

    tag = avio_rb24(s->pb);
    if (tag == 0)
        return AVERROR_EOF;
    else if (tag != MKBETAG(0,'B','L','K'))
        return AVERROR_INVALIDDATA;

    avio_skip(s->pb, 1);
    packet_size = avio_rb16(s->pb);
    avio_skip(s->pb, 2);
    pts = avio_rb32(s->pb);
    avio_skip(s->pb, 12);
    ret = av_get_packet(s->pb, pkt, packet_size);
    if (ret < packet_size)
        pkt->flags |= AV_PKT_FLAG_CORRUPT;

    if (ret < 0)
        return ret;
    if (!ret)
        return AVERROR_EOF;

    pkt->stream_index = 0;
    pkt->pos = pos;
    if (s->streams[0]->codecpar->codec_id == AV_CODEC_ID_ATRAC3AL) {
        pkt->duration = 1024;
        pkt->pts = pts * 1024LL;
    } else {
        pkt->duration = 2048;
        pkt->pts = pts * 2048LL;
    }

    return ret;
}

static int oma_read_header(AVFormatContext *s)
{
    int     ret, framesize, jsflag, samplerate;
    uint32_t codec_params, channel_id;
    int16_t eid;
    uint8_t buf[EA3_HEADER_SIZE];
    uint8_t *edata;
    AVStream *st;
    ID3v2ExtraMeta *extra_meta;
    OMAContext *oc = s->priv_data;

    ff_id3v2_read(s, ID3v2_EA3_MAGIC, &extra_meta, 0);
    if ((ret = ff_id3v2_parse_chapters(s, extra_meta)) < 0) {
        ff_id3v2_free_extra_meta(&extra_meta);
        return ret;
    }

    ret = avio_read(s->pb, buf, EA3_HEADER_SIZE);
    if (ret < EA3_HEADER_SIZE) {
        ff_id3v2_free_extra_meta(&extra_meta);
        return -1;
    }

    if (memcmp(buf, ((const uint8_t[]){'E', 'A', '3'}), 3) ||
        buf[4] != 0 || buf[5] != EA3_HEADER_SIZE) {
        ff_id3v2_free_extra_meta(&extra_meta);
        av_log(s, AV_LOG_ERROR, "Couldn't find the EA3 header !\n");
        return AVERROR_INVALIDDATA;
    }

    oc->content_start = avio_tell(s->pb);

    /* encrypted file */
    eid = AV_RB16(&buf[6]);
    if (eid != -1 && eid != -128 && decrypt_init(s, extra_meta, buf) < 0) {
        ff_id3v2_free_extra_meta(&extra_meta);
        return -1;
    }

    ff_id3v2_free_extra_meta(&extra_meta);

    codec_params = AV_RB24(&buf[33]);

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->start_time = 0;
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_tag  = buf[32];
    st->codecpar->codec_id   = ff_codec_get_id(ff_oma_codec_tags,
                                               st->codecpar->codec_tag);

    oc->read_packet = read_packet;

    switch (buf[32]) {
    case OMA_CODECID_ATRAC3:
        samplerate = ff_oma_srate_tab[(codec_params >> 13) & 7] * 100;
        if (!samplerate) {
            av_log(s, AV_LOG_ERROR, "Unsupported sample rate\n");
            return AVERROR_INVALIDDATA;
        }
        if (samplerate != 44100)
            avpriv_request_sample(s, "Sample rate %d", samplerate);

        framesize = (codec_params & 0x3FF) * 8;

        /* get stereo coding mode, 1 for joint-stereo */
        jsflag = (codec_params >> 17) & 1;

        st->codecpar->ch_layout   = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        st->codecpar->sample_rate = samplerate;
        st->codecpar->bit_rate    = st->codecpar->sample_rate * framesize / (1024 / 8);

        /* fake the ATRAC3 extradata
         * (wav format, makes stream copy to wav work) */
        if ((ret = ff_alloc_extradata(st->codecpar, 14)) < 0)
            return ret;

        edata = st->codecpar->extradata;
        AV_WL16(&edata[0],  1);             // always 1
        AV_WL32(&edata[2],  samplerate);    // samples rate
        AV_WL16(&edata[6],  jsflag);        // coding mode
        AV_WL16(&edata[8],  jsflag);        // coding mode
        AV_WL16(&edata[10], 1);             // always 1
        AV_WL16(&edata[12], 0);             // always 0

        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
        break;
    case OMA_CODECID_ATRAC3P:
        channel_id = (codec_params >> 10) & 7;
        if (!channel_id) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid ATRAC-X channel id: %"PRIu32"\n", channel_id);
            return AVERROR_INVALIDDATA;
        }
        av_channel_layout_copy(&st->codecpar->ch_layout,
                               &oma_chid_to_native_layout[channel_id - 1]);
        framesize = ((codec_params & 0x3FF) * 8) + 8;
        samplerate = ff_oma_srate_tab[(codec_params >> 13) & 7] * 100;
        if (!samplerate) {
            av_log(s, AV_LOG_ERROR, "Unsupported sample rate\n");
            return AVERROR_INVALIDDATA;
        }
        st->codecpar->sample_rate = samplerate;
        st->codecpar->bit_rate    = samplerate * framesize / (2048 / 8);
        avpriv_set_pts_info(st, 64, 1, samplerate);
        break;
    case OMA_CODECID_MP3:
        ffstream(st)->need_parsing = AVSTREAM_PARSE_FULL_RAW;
        framesize = 1024;
        break;
    case OMA_CODECID_LPCM:
        /* PCM 44.1 kHz 16 bit stereo big-endian */
        st->codecpar->ch_layout   = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        st->codecpar->sample_rate = 44100;
        framesize = 1024;
        /* bit rate = sample rate x PCM block align (= 4) x 8 */
        st->codecpar->bit_rate = st->codecpar->sample_rate * 32;
        st->codecpar->bits_per_coded_sample =
            av_get_bits_per_sample(st->codecpar->codec_id);
        avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
        break;
    case OMA_CODECID_ATRAC3AL:
        st->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        st->codecpar->sample_rate = 44100;
        avpriv_set_pts_info(st, 64, 1, 44100);
        oc->read_packet = aal_read_packet;
        framesize = 4096;
        break;
    case OMA_CODECID_ATRAC3PAL:
        st->codecpar->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
        st->codecpar->sample_rate = 44100;
        avpriv_set_pts_info(st, 64, 1, 44100);
        oc->read_packet = aal_read_packet;
        framesize = 4096;
        break;
    default:
        av_log(s, AV_LOG_ERROR, "Unsupported codec %d!\n", buf[32]);
        return AVERROR(ENOSYS);
    }

    st->codecpar->block_align = framesize;

    return 0;
}

static int oma_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    OMAContext *oc  = s->priv_data;
    return oc->read_packet(s, pkt);
}

static int oma_read_probe(const AVProbeData *p)
{
    const uint8_t *buf = p->buf;
    unsigned tag_len = 0;

    if (p->buf_size >= ID3v2_HEADER_SIZE && ff_id3v2_match(buf, ID3v2_EA3_MAGIC))
        tag_len = ff_id3v2_tag_len(buf);

    /* This check cannot overflow as tag_len has at most 28 bits */
    if (p->buf_size < tag_len + 5)
        /* EA3 header comes late, might be outside of the probe buffer */
        return tag_len ? AVPROBE_SCORE_EXTENSION/2 : 0;

    buf += tag_len;

    if (!memcmp(buf, "EA3", 3) && !buf[4] && buf[5] == EA3_HEADER_SIZE)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int oma_read_seek(struct AVFormatContext *s,
                         int stream_index, int64_t timestamp, int flags)
{
    OMAContext *oc = s->priv_data;
    AVStream *st = s->streams[0];
    int64_t err;

    if (st->codecpar->codec_id == AV_CODEC_ID_ATRAC3PAL ||
        st->codecpar->codec_id == AV_CODEC_ID_ATRAC3AL)
        return -1;

    err = ff_pcm_read_seek(s, stream_index, timestamp, flags);
    if (!oc->encrypted)
        return err;

    /* readjust IV for CBC */
    if (err || avio_tell(s->pb) < oc->content_start)
        goto wipe;
    if ((err = avio_seek(s->pb, -8, SEEK_CUR)) < 0)
        goto wipe;
    if ((err = avio_read(s->pb, oc->iv, 8)) < 8) {
        if (err >= 0)
            err = AVERROR_EOF;
        goto wipe;
    }

    return 0;
wipe:
    memset(oc->iv, 0, 8);
    return err;
}

const FFInputFormat ff_oma_demuxer = {
    .p.name         = "oma",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Sony OpenMG audio"),
    .p.flags        = AVFMT_GENERIC_INDEX,
    .p.extensions   = "oma,omg,aa3",
    .p.codec_tag    = ff_oma_codec_tags_list,
    .priv_data_size = sizeof(OMAContext),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = oma_read_probe,
    .read_header    = oma_read_header,
    .read_packet    = oma_read_packet,
    .read_seek      = oma_read_seek,
    .read_close     = oma_read_close,
};
