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
#include "libavutil/dict.h"
#include "libavutil/mathematics.h"
#include "avformat.h"
#include "internal.h"
#include "id3v2.h"
#include "id3v1.h"
#include "libavcodec/mpegaudiodecheader.h"

/* mp3 read */

static int mp3_read_probe(AVProbeData *p)
{
    int max_frames, first_frames = 0;
    int fsize, frames, sample_rate;
    uint32_t header;
    uint8_t *buf, *buf0, *buf2, *end;
    AVCodecContext avctx;

    buf0 = p->buf;
    end = p->buf + p->buf_size - sizeof(uint32_t);
    while(buf0 < end && !*buf0)
        buf0++;

    max_frames = 0;
    buf = buf0;

    for(; buf < end; buf= buf2+1) {
        buf2 = buf;

        for(frames = 0; buf2 < end; frames++) {
            header = AV_RB32(buf2);
            fsize = avpriv_mpa_decode_header(&avctx, header, &sample_rate, &sample_rate, &sample_rate, &sample_rate);
            if(fsize < 0)
                break;
            buf2 += fsize;
        }
        max_frames = FFMAX(max_frames, frames);
        if(buf == buf0)
            first_frames= frames;
    }
    // keep this in sync with ac3 probe, both need to avoid
    // issues with MPEG-files!
    if (first_frames >= 4) return AVPROBE_SCORE_MAX / 2 + 1;

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
    if      (max_frames >  500) return AVPROBE_SCORE_MAX / 2;
    else if (max_frames >= 4)   return AVPROBE_SCORE_MAX / 4;
    else if (max_frames >= 1)   return 1;
    else                        return 0;
//mpegps_mp3_unrecognized_format.mpg has max_frames=3
}

/**
 * Try to find Xing/Info/VBRI tags and compute duration from info therein
 */
static int mp3_parse_vbr_tags(AVFormatContext *s, AVStream *st, int64_t base)
{
    uint32_t v, spf;
    unsigned frames = 0; /* Total number of frames in file */
    unsigned size = 0; /* Total number of bytes in the stream */
    const int64_t xing_offtbl[2][2] = {{32, 17}, {17,9}};
    MPADecodeHeader c;
    int vbrtag_size = 0;

    v = avio_rb32(s->pb);
    if(ff_mpa_check_header(v) < 0)
      return -1;

    if (avpriv_mpegaudio_decode_header(&c, v) == 0)
        vbrtag_size = c.frame_size;
    if(c.layer != 3)
        return -1;

    /* Check for Xing / Info tag */
    avio_skip(s->pb, xing_offtbl[c.lsf == 1][c.nb_channels == 1]);
    v = avio_rb32(s->pb);
    if(v == MKBETAG('X', 'i', 'n', 'g') || v == MKBETAG('I', 'n', 'f', 'o')) {
        v = avio_rb32(s->pb);
        if(v & 0x1)
            frames = avio_rb32(s->pb);
        if(v & 0x2)
            size = avio_rb32(s->pb);
    }

    /* Check for VBRI tag (always 32 bytes after end of mpegaudio header) */
    avio_seek(s->pb, base + 4 + 32, SEEK_SET);
    v = avio_rb32(s->pb);
    if(v == MKBETAG('V', 'B', 'R', 'I')) {
        /* Check tag version */
        if(avio_rb16(s->pb) == 1) {
            /* skip delay and quality */
            avio_skip(s->pb, 4);
            size = avio_rb32(s->pb);
            frames = avio_rb32(s->pb);
        }
    }

    if(!frames && !size)
        return -1;

    /* Skip the vbr tag frame */
    avio_seek(s->pb, base + vbrtag_size, SEEK_SET);

    spf = c.lsf ? 576 : 1152; /* Samples per frame, layer 3 */
    if(frames)
        st->duration = av_rescale_q(frames, (AVRational){spf, c.sample_rate},
                                    st->time_base);
    if(size && frames)
        st->codec->bit_rate = av_rescale(size, 8 * c.sample_rate, frames * (int64_t)spf);

    return 0;
}

static int mp3_read_header(AVFormatContext *s)
{
    AVStream *st;
    int64_t off;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_MP3;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    st->start_time = 0;

    // lcm of all mp3 sample rates
    avpriv_set_pts_info(st, 64, 1, 14112000);

    off = avio_tell(s->pb);

    if (!av_dict_get(s->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX))
        ff_id3v1_read(s);

    if (mp3_parse_vbr_tags(s, st, off) < 0)
        avio_seek(s->pb, off, SEEK_SET);

    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

#define MP3_PACKET_SIZE 1024

static int mp3_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;
    //    AVStream *st = s->streams[0];

    size= MP3_PACKET_SIZE;

    ret= av_get_packet(s->pb, pkt, size);

    pkt->stream_index = 0;
    if (ret <= 0) {
        return AVERROR(EIO);
    }

    if (ret > ID3v1_TAG_SIZE &&
        memcmp(&pkt->data[ret - ID3v1_TAG_SIZE], "TAG", 3) == 0)
        ret -= ID3v1_TAG_SIZE;

    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

AVInputFormat ff_mp3_demuxer = {
    .name           = "mp3",
    .long_name      = NULL_IF_CONFIG_SMALL("MPEG audio layer 2/3"),
    .read_probe     = mp3_read_probe,
    .read_header    = mp3_read_header,
    .read_packet    = mp3_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
    .extensions     = "mp2,mp3,m2a", /* XXX: use probe */
};
