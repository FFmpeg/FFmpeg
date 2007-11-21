/*
 * Yamaha SMAF format
 * Copyright (c) 2005 Vidar Madsen
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
#include "raw.h"
#include "riff.h"

typedef struct {
    offset_t atrpos, atsqpos, awapos;
    offset_t data_size;
} MMFContext;

static int mmf_rates[] = { 4000, 8000, 11025, 22050, 44100 };

static int mmf_rate_code(int rate)
{
    int i;
    for(i = 0; i < 5; i++)
        if(mmf_rates[i] == rate)
            return i;
    return -1;
}

static int mmf_rate(int code)
{
    if((code < 0) || (code > 4))
        return -1;
    return mmf_rates[code];
}

#ifdef CONFIG_MUXERS
/* Copy of end_tag() from avienc.c, but for big-endian chunk size */
static void end_tag_be(ByteIOContext *pb, offset_t start)
{
    offset_t pos;

    pos = url_ftell(pb);
    url_fseek(pb, start - 4, SEEK_SET);
    put_be32(pb, (uint32_t)(pos - start));
    url_fseek(pb, pos, SEEK_SET);
}

static int mmf_write_header(AVFormatContext *s)
{
    MMFContext *mmf = s->priv_data;
    ByteIOContext *pb = s->pb;
    offset_t pos;
    int rate;

    rate = mmf_rate_code(s->streams[0]->codec->sample_rate);
    if(rate < 0) {
        av_log(s, AV_LOG_ERROR, "Unsupported sample rate %d\n", s->streams[0]->codec->sample_rate);
        return -1;
    }

    put_tag(pb, "MMMD");
    put_be32(pb, 0);
    pos = start_tag(pb, "CNTI");
    put_byte(pb, 0); /* class */
    put_byte(pb, 0); /* type */
    put_byte(pb, 0); /* code type */
    put_byte(pb, 0); /* status */
    put_byte(pb, 0); /* counts */
    put_tag(pb, "VN:libavcodec,"); /* metadata ("ST:songtitle,VN:version,...") */
    end_tag_be(pb, pos);

    put_buffer(pb, "ATR\x00", 4);
    put_be32(pb, 0);
    mmf->atrpos = url_ftell(pb);
    put_byte(pb, 0); /* format type */
    put_byte(pb, 0); /* sequence type */
    put_byte(pb, (0 << 7) | (1 << 4) | rate); /* (channel << 7) | (format << 4) | rate */
    put_byte(pb, 0); /* wave base bit */
    put_byte(pb, 2); /* time base d */
    put_byte(pb, 2); /* time base g */

    put_tag(pb, "Atsq");
    put_be32(pb, 16);
    mmf->atsqpos = url_ftell(pb);
    /* Will be filled on close */
    put_buffer(pb, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16);

    mmf->awapos = start_tag(pb, "Awa\x01");

    av_set_pts_info(s->streams[0], 64, 1, s->streams[0]->codec->sample_rate);

    put_flush_packet(pb);

    return 0;
}

static int mmf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    put_buffer(pb, pkt->data, pkt->size);
    return 0;
}

/* Write a variable-length symbol */
static void put_varlength(ByteIOContext *pb, int val)
{
    if(val < 128)
        put_byte(pb, val);
    else {
        val -= 128;
        put_byte(pb, 0x80 | val >> 7);
        put_byte(pb, 0x7f & val);
    }
}

static int mmf_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    MMFContext *mmf = s->priv_data;
    offset_t pos, size;
    int gatetime;

    if (!url_is_streamed(s->pb)) {
        /* Fill in length fields */
        end_tag_be(pb, mmf->awapos);
        end_tag_be(pb, mmf->atrpos);
        end_tag_be(pb, 8);

        pos = url_ftell(pb);
        size = pos - mmf->awapos;

        /* Fill Atsq chunk */
        url_fseek(pb, mmf->atsqpos, SEEK_SET);

        /* "play wav" */
        put_byte(pb, 0); /* start time */
        put_byte(pb, 1); /* (channel << 6) | wavenum */
        gatetime = size * 500 / s->streams[0]->codec->sample_rate;
        put_varlength(pb, gatetime); /* duration */

        /* "nop" */
        put_varlength(pb, gatetime); /* start time */
        put_buffer(pb, "\xff\x00", 2); /* nop */

        /* "end of sequence" */
        put_buffer(pb, "\x00\x00\x00\x00", 4);

        url_fseek(pb, pos, SEEK_SET);

        put_flush_packet(pb);
    }
    return 0;
}
#endif //CONFIG_MUXERS

static int mmf_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf[0] == 'M' && p->buf[1] == 'M' &&
        p->buf[2] == 'M' && p->buf[3] == 'D' &&
        p->buf[8] == 'C' && p->buf[9] == 'N' &&
        p->buf[10] == 'T' && p->buf[11] == 'I')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

/* mmf input */
static int mmf_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    MMFContext *mmf = s->priv_data;
    unsigned int tag;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    offset_t file_size, size;
    int rate, params;

    tag = get_le32(pb);
    if (tag != MKTAG('M', 'M', 'M', 'D'))
        return -1;
    file_size = get_be32(pb);

    /* Skip some unused chunks that may or may not be present */
    for(;; url_fseek(pb, size, SEEK_CUR)) {
        tag = get_le32(pb);
        size = get_be32(pb);
        if(tag == MKTAG('C','N','T','I')) continue;
        if(tag == MKTAG('O','P','D','A')) continue;
        break;
    }

    /* Tag = "ATRx", where "x" = track number */
    if ((tag & 0xffffff) == MKTAG('M', 'T', 'R', 0)) {
        av_log(s, AV_LOG_ERROR, "MIDI like format found, unsupported\n");
        return -1;
    }
    if ((tag & 0xffffff) != MKTAG('A', 'T', 'R', 0)) {
        av_log(s, AV_LOG_ERROR, "Unsupported SMAF chunk %08x\n", tag);
        return -1;
    }

    get_byte(pb); /* format type */
    get_byte(pb); /* sequence type */
    params = get_byte(pb); /* (channel << 7) | (format << 4) | rate */
    rate = mmf_rate(params & 0x0f);
    if(rate  < 0) {
        av_log(s, AV_LOG_ERROR, "Invalid sample rate\n");
        return -1;
    }
    get_byte(pb); /* wave base bit */
    get_byte(pb); /* time base d */
    get_byte(pb); /* time base g */

    /* Skip some unused chunks that may or may not be present */
    for(;; url_fseek(pb, size, SEEK_CUR)) {
        tag = get_le32(pb);
        size = get_be32(pb);
        if(tag == MKTAG('A','t','s','q')) continue;
        if(tag == MKTAG('A','s','p','I')) continue;
        break;
    }

    /* Make sure it's followed by an Awa chunk, aka wave data */
    if ((tag & 0xffffff) != MKTAG('A', 'w', 'a', 0)) {
        av_log(s, AV_LOG_ERROR, "Unexpected SMAF chunk %08x\n", tag);
        return -1;
    }
    mmf->data_size = size;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_ADPCM_YAMAHA;
    st->codec->sample_rate = rate;
    st->codec->channels = 1;
    st->codec->bits_per_sample = 4;
    st->codec->bit_rate = st->codec->sample_rate * st->codec->bits_per_sample;

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);

    return 0;
}

#define MAX_SIZE 4096

static int mmf_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    MMFContext *mmf = s->priv_data;
    AVStream *st;
    int ret, size;

    if (url_feof(s->pb))
        return AVERROR(EIO);
    st = s->streams[0];

    size = MAX_SIZE;
    if(size > mmf->data_size)
        size = mmf->data_size;

    if(!size)
        return AVERROR(EIO);

    if (av_new_packet(pkt, size))
        return AVERROR(EIO);
    pkt->stream_index = 0;

    ret = get_buffer(s->pb, pkt->data, pkt->size);
    if (ret < 0)
        av_free_packet(pkt);

    mmf->data_size -= ret;

    pkt->size = ret;
    return ret;
}

static int mmf_read_close(AVFormatContext *s)
{
    return 0;
}

static int mmf_read_seek(AVFormatContext *s,
                         int stream_index, int64_t timestamp, int flags)
{
    return pcm_read_seek(s, stream_index, timestamp, flags);
}

#ifdef CONFIG_MMF_DEMUXER
AVInputFormat mmf_demuxer = {
    "mmf",
    "mmf format",
    sizeof(MMFContext),
    mmf_probe,
    mmf_read_header,
    mmf_read_packet,
    mmf_read_close,
    mmf_read_seek,
};
#endif
#ifdef CONFIG_MMF_MUXER
AVOutputFormat mmf_muxer = {
    "mmf",
    "mmf format",
    "application/vnd.smaf",
    "mmf",
    sizeof(MMFContext),
    CODEC_ID_ADPCM_YAMAHA,
    CODEC_ID_NONE,
    mmf_write_header,
    mmf_write_packet,
    mmf_write_trailer,
};
#endif
