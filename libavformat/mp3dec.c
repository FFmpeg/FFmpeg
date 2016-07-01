/*
 * MP3 demuxer
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavutil/avstring.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/crc.h"
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "avformat.h"
#include "internal.h"
#include "avio_internal.h"
#include "id3v2.h"
#include "id3v1.h"
#include "replaygain.h"

#include "libavcodec/avcodec.h"
#include "libavcodec/mpegaudiodecheader.h"

#define XING_FLAG_FRAMES 0x01
#define XING_FLAG_SIZE   0x02
#define XING_FLAG_TOC    0x04
#define XING_FLAC_QSCALE 0x08

#define XING_TOC_COUNT 100

typedef struct MP3DecContext {
    int xing_toc;
    unsigned frames; /* Total number of frames in file */
    unsigned size;   /* Total number of bytes in the stream */
    int is_cbr;
} MP3DecContext;

/* mp3 read */

static int mp3_read_probe(AVProbeData *p)
{
    int max_frames, first_frames = 0;
    int frames, ret;
    uint32_t header;
    uint8_t *buf, *buf0, *buf2, *end;

    buf0 = p->buf;
    end = p->buf + p->buf_size - sizeof(uint32_t);
    while(buf0 < end && !*buf0)
        buf0++;

    max_frames = 0;
    buf = buf0;

    for(; buf < end; buf= buf2+1) {
        buf2 = buf;

        for(frames = 0; buf2 < end; frames++) {
            MPADecodeHeader h;

            header = AV_RB32(buf2);
            ret = avpriv_mpegaudio_decode_header(&h, header);
            if (ret != 0)
                break;
            buf2 += h.frame_size;
        }
        max_frames = FFMAX(max_frames, frames);
        if(buf == buf0)
            first_frames= frames;
    }
    // keep this in sync with ac3 probe, both need to avoid
    // issues with MPEG-files!
    if (first_frames >= 10)
        return AVPROBE_SCORE_EXTENSION + 5;
    if (first_frames >= 4)
        return AVPROBE_SCORE_EXTENSION + 1;

    if (max_frames) {
        int pes = 0, i;
        unsigned int code = -1;

#define VIDEO_ID 0x000001e0
#define AUDIO_ID 0x000001c0
        /* do a search for mpegps headers to be able to properly bias
         * towards mpegps if we detect this stream as both. */
        for (i = 0; i<p->buf_size; i++) {
            code = (code << 8) + p->buf[i];
            if ((code & 0xffffff00) == 0x100) {
                if     ((code & 0x1f0) == VIDEO_ID) pes++;
                else if((code & 0x1e0) == AUDIO_ID) pes++;
            }
        }

        if (pes)
            max_frames = (max_frames + pes - 1) / pes;
    }
    if      (max_frames >  500) return AVPROBE_SCORE_EXTENSION;
    else if (max_frames >= 4)   return AVPROBE_SCORE_EXTENSION / 2;
    else if (max_frames >= 1)   return 1;
    else                        return 0;
//mpegps_mp3_unrecognized_format.mpg has max_frames=3
}

static void read_xing_toc(AVFormatContext *s, int64_t filesize, int64_t duration)
{
    int i;
    MP3DecContext *mp3 = s->priv_data;

    if (!filesize &&
        !(filesize = avio_size(s->pb))) {
        av_log(s, AV_LOG_WARNING, "Cannot determine file size, skipping TOC table.\n");
        return;
    }

    for (i = 0; i < XING_TOC_COUNT; i++) {
        uint8_t b = avio_r8(s->pb);

        av_add_index_entry(s->streams[0],
                           av_rescale(b, filesize, 256),
                           av_rescale(i, duration, XING_TOC_COUNT),
                           0, 0, AVINDEX_KEYFRAME);
    }
    mp3->xing_toc = 1;
}

static void mp3_parse_info_tag(AVFormatContext *s, AVStream *st,
                               MPADecodeHeader *c, uint32_t spf)
{
#define LAST_BITS(k, n) ((k) & ((1 << (n)) - 1))
#define MIDDLE_BITS(k, m, n) LAST_BITS((k) >> (m), ((n) - (m)))

    uint16_t crc;
    uint32_t v, delays;

    char version[10];

    uint32_t peak   = 0;
    int32_t  r_gain = INT32_MIN, a_gain = INT32_MIN;

    MP3DecContext *mp3 = s->priv_data;
    static const int64_t xing_offtbl[2][2] = { { 32, 17 }, { 17, 9 } };

    /* Check for Xing / Info tag */
    avio_skip(s->pb, xing_offtbl[c->lsf == 1][c->nb_channels == 1]);
    v = avio_rb32(s->pb);
    mp3->is_cbr = v == MKBETAG('I', 'n', 'f', 'o');
    if (v != MKBETAG('X', 'i', 'n', 'g') && !mp3->is_cbr)
        return;

    v = avio_rb32(s->pb);
    if (v & XING_FLAG_FRAMES)
        mp3->frames = avio_rb32(s->pb);
    if (v & XING_FLAG_SIZE)
        mp3->size = avio_rb32(s->pb);
    if (v & XING_FLAG_TOC && mp3->frames)
        read_xing_toc(s, mp3->size, av_rescale_q(mp3->frames,
                                       (AVRational){spf, c->sample_rate},
                                       st->time_base));

    /* VBR quality */
    if (v & XING_FLAC_QSCALE)
        avio_rb32(s->pb);

    /* Encoder short version string */
    memset(version, 0, sizeof(version));
    avio_read(s->pb, version, 9);

    /* Info Tag revision + VBR method */
    avio_r8(s->pb);

    /* Lowpass filter value */
    avio_r8(s->pb);

    /* ReplayGain peak */
    v    = avio_rb32(s->pb);
    peak = av_rescale(v, 100000, 1 << 23);

    /* Radio ReplayGain */
    v = avio_rb16(s->pb);

    if (MIDDLE_BITS(v, 13, 15) == 1) {
        r_gain = MIDDLE_BITS(v, 0, 8) * 10000;

        if (v & (1 << 9))
            r_gain *= -1;
    }

    /* Audiophile ReplayGain */
    v = avio_rb16(s->pb);

    if (MIDDLE_BITS(v, 13, 15) == 2) {
        a_gain = MIDDLE_BITS(v, 0, 8) * 10000;

        if (v & (1 << 9))
            a_gain *= -1;
    }

    /* Encoding flags + ATH Type */
    avio_r8(s->pb);

    /* if ABR {specified bitrate} else {minimal bitrate} */
    avio_r8(s->pb);

    /* Encoder delays */
    delays = avio_rb24(s->pb);
    st->codecpar->initial_padding  = delays >> 12;
    st->codecpar->trailing_padding = delays & ((1 << 12) - 1);

    /* Misc */
    avio_r8(s->pb);

    /* MP3 gain */
    avio_r8(s->pb);

    /* Preset and surround info */
    avio_rb16(s->pb);

    /* Music length */
    avio_rb32(s->pb);

    /* Music CRC */
    avio_rb16(s->pb);

    /* Info Tag CRC */
    crc = ffio_get_checksum(s->pb);
    v   = avio_rb16(s->pb);

    if (v == crc) {
        ff_replaygain_export_raw(st, r_gain, peak, a_gain, 0);
        av_dict_set(&st->metadata, "encoder", version, 0);
    }
}

static void mp3_parse_vbri_tag(AVFormatContext *s, AVStream *st, int64_t base)
{
    uint32_t v;
    MP3DecContext *mp3 = s->priv_data;

    /* Check for VBRI tag (always 32 bytes after end of mpegaudio header) */
    avio_seek(s->pb, base + 4 + 32, SEEK_SET);
    v = avio_rb32(s->pb);
    if (v == MKBETAG('V', 'B', 'R', 'I')) {
        /* Check tag version */
        if (avio_rb16(s->pb) == 1) {
            /* skip delay and quality */
            avio_skip(s->pb, 4);
            mp3->size = avio_rb32(s->pb);
            mp3->frames = avio_rb32(s->pb);
        }
    }
}

/**
 * Try to find Xing/Info/VBRI tags and compute duration from info therein
 */
static int mp3_parse_vbr_tags(AVFormatContext *s, AVStream *st, int64_t base)
{
    uint32_t v, spf;
    MPADecodeHeader c;
    int vbrtag_size = 0;
    MP3DecContext *mp3 = s->priv_data;
    int ret;

    ffio_init_checksum(s->pb, ff_crcA001_update, 0);

    v = avio_rb32(s->pb);

    ret = avpriv_mpegaudio_decode_header(&c, v);
    if (ret < 0)
        return ret;
    else if (ret == 0)
        vbrtag_size = c.frame_size;
    if(c.layer != 3)
        return -1;

    spf = c.lsf ? 576 : 1152; /* Samples per frame, layer 3 */

    mp3->frames = 0;
    mp3->size   = 0;

    mp3_parse_info_tag(s, st, &c, spf);
    mp3_parse_vbri_tag(s, st, base);

    if (!mp3->frames && !mp3->size)
        return -1;

    /* Skip the vbr tag frame */
    avio_seek(s->pb, base + vbrtag_size, SEEK_SET);

    if (mp3->frames)
        st->duration = av_rescale_q(mp3->frames, (AVRational){spf, c.sample_rate},
                                    st->time_base);
    if (mp3->size && mp3->frames && !mp3->is_cbr)
        st->codecpar->bit_rate = av_rescale(mp3->size, 8 * c.sample_rate, mp3->frames * (int64_t)spf);

    return 0;
}

static int mp3_read_header(AVFormatContext *s)
{
    AVStream *st;
    int64_t off;
    int ret;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = AV_CODEC_ID_MP3;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    st->start_time = 0;

    // lcm of all mp3 sample rates
    avpriv_set_pts_info(st, 64, 1, 14112000);

    off = avio_tell(s->pb);

    if (!av_dict_get(s->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX))
        ff_id3v1_read(s);

    if (mp3_parse_vbr_tags(s, st, off) < 0)
        avio_seek(s->pb, off, SEEK_SET);

    ret = ff_replaygain_export(st, s->metadata);
    if (ret < 0)
        return ret;

    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

#define MP3_PACKET_SIZE 1024

static int mp3_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret;

    ret = av_get_packet(s->pb, pkt, MP3_PACKET_SIZE);
    if (ret < 0)
        return ret;

    pkt->stream_index = 0;

    if (ret > ID3v1_TAG_SIZE &&
        memcmp(&pkt->data[ret - ID3v1_TAG_SIZE], "TAG", 3) == 0)
        ret -= ID3v1_TAG_SIZE;

    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

#define SEEK_PACKETS 4
#define SEEK_WINDOW (SEEK_PACKETS * MP3_PACKET_SIZE)

/* The toc entry can position to the wrong byte offset, try to pick
 * the closest frame by probing the data in a window of 4 packets.
 */

static int check(AVIOContext *pb, int64_t pos, int64_t *out_pos)
{
    MPADecodeHeader mh = { 0 };
    int i;
    uint32_t header;
    int64_t off = 0;


    for (i = 0; i < SEEK_PACKETS; i++) {
        off = avio_seek(pb, pos + mh.frame_size, SEEK_SET);
        if (off < 0)
            break;

        header = avio_rb32(pb);


        if (avpriv_mpegaudio_decode_header(&mh, header))
            break;
        out_pos[i] = off;
    }

    return i;
}

static int reposition(AVFormatContext *s, int64_t pos)
{
    int ret, best_valid = -1;
    int64_t p, best_pos = -1;

    for (p = FFMAX(pos - SEEK_WINDOW / 2, 0); p < pos + SEEK_WINDOW / 2; p++) {
        int64_t out_pos[SEEK_PACKETS];
        ret = check(s->pb, p, out_pos);

        if (best_valid < ret) {
            int i;
            for (i = 0; i < ret; i++) {
                if (llabs(best_pos - pos) > llabs(out_pos[i] - pos)) {
                    best_pos   = out_pos[i];
                    best_valid = ret;
                }
            }
            if (best_pos == pos && best_valid == SEEK_PACKETS)
                break;
        }
    }

    if (best_valid <= 0)
        return AVERROR(ENOSYS);

    p = avio_seek(s->pb, best_pos, SEEK_SET);
    if (p < 0)
        return p;

    return 0;
}

static int mp3_seek(AVFormatContext *s, int stream_index, int64_t timestamp,
                    int flags)
{
    MP3DecContext *mp3 = s->priv_data;
    AVIndexEntry *ie;
    AVStream *st = s->streams[0];
    int64_t ret  = av_index_search_timestamp(st, timestamp, flags);

    if (!mp3->xing_toc)
        return AVERROR(ENOSYS);

    if (ret < 0)
        return ret;

    ie = &st->index_entries[ret];

    ret = reposition(s, ie->pos);
    if (ret < 0)
        return ret;

    ff_update_cur_dts(s, st, ie->timestamp);

    return 0;
}

AVInputFormat ff_mp3_demuxer = {
    .name           = "mp3",
    .long_name      = NULL_IF_CONFIG_SMALL("MP2/3 (MPEG audio layer 2/3)"),
    .read_probe     = mp3_read_probe,
    .read_header    = mp3_read_header,
    .read_packet    = mp3_read_packet,
    .read_seek      = mp3_seek,
    .priv_data_size = sizeof(MP3DecContext),
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "mp2,mp3,m2a,mpa", /* XXX: use probe */
};
