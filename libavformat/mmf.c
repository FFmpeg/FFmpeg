/*
 * Yamaha SMAF format
 * Copyright (c) 2005 Vidar Madsen
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

#include "libavutil/channel_layout.h"
#include "avformat.h"
#include "avio_internal.h"
#include "internal.h"
#include "pcm.h"
#include "riff.h"

typedef struct MMFContext {
    int64_t atrpos, atsqpos, awapos;
    int64_t data_size;
} MMFContext;

static const int mmf_rates[] = { 4000, 8000, 11025, 22050, 44100 };

static int mmf_rate(int code)
{
    if ((code < 0) || (code > 4))
        return -1;
    return mmf_rates[code];
}

#if CONFIG_MMF_MUXER
static int mmf_rate_code(int rate)
{
    int i;
    for (i = 0; i < 5; i++)
        if (mmf_rates[i] == rate)
            return i;
    return -1;
}

/* Copy of end_tag() from avienc.c, but for big-endian chunk size */
static void end_tag_be(AVIOContext *pb, int64_t start)
{
    int64_t pos;

    pos = avio_tell(pb);
    avio_seek(pb, start - 4, SEEK_SET);
    avio_wb32(pb, (uint32_t)(pos - start));
    avio_seek(pb, pos, SEEK_SET);
}

static int mmf_write_header(AVFormatContext *s)
{
    MMFContext *mmf = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t pos;
    int rate;

    rate = mmf_rate_code(s->streams[0]->codecpar->sample_rate);
    if (rate < 0) {
        av_log(s, AV_LOG_ERROR, "Unsupported sample rate %d\n",
               s->streams[0]->codecpar->sample_rate);
        return -1;
    }

    ffio_wfourcc(pb, "MMMD");
    avio_wb32(pb, 0);
    pos = ff_start_tag(pb, "CNTI");
    avio_w8(pb, 0); /* class */
    avio_w8(pb, 0); /* type */
    avio_w8(pb, 0); /* code type */
    avio_w8(pb, 0); /* status */
    avio_w8(pb, 0); /* counts */
    end_tag_be(pb, pos);

    pos = ff_start_tag(pb, "OPDA");
    avio_write(pb, "VN:libavcodec,", sizeof("VN:libavcodec,") -1); /* metadata ("ST:songtitle,VN:version,...") */
    end_tag_be(pb, pos);

    avio_write(pb, "ATR\x00", 4);
    avio_wb32(pb, 0);
    mmf->atrpos = avio_tell(pb);
    avio_w8(pb, 0); /* format type */
    avio_w8(pb, 0); /* sequence type */
    avio_w8(pb, (0 << 7) | (1 << 4) | rate); /* (channel << 7) | (format << 4) | rate */
    avio_w8(pb, 0); /* wave base bit */
    avio_w8(pb, 2); /* time base d */
    avio_w8(pb, 2); /* time base g */

    ffio_wfourcc(pb, "Atsq");
    avio_wb32(pb, 16);
    mmf->atsqpos = avio_tell(pb);
    /* Will be filled on close */
    avio_write(pb, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00", 16);

    mmf->awapos = ff_start_tag(pb, "Awa\x01");

    avpriv_set_pts_info(s->streams[0], 64, 1, s->streams[0]->codecpar->sample_rate);

    avio_flush(pb);

    return 0;
}

static int mmf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIOContext *pb = s->pb;
    avio_write(pb, pkt->data, pkt->size);
    return 0;
}

/* Write a variable-length symbol */
static void put_varlength(AVIOContext *pb, int val)
{
    if (val < 128)
        avio_w8(pb, val);
    else {
        val -= 128;
        avio_w8(pb, 0x80 | val >> 7);
        avio_w8(pb, 0x7f & val);
    }
}

static int mmf_write_trailer(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    MMFContext *mmf = s->priv_data;
    int64_t pos, size;
    int gatetime;

    if (s->pb->seekable) {
        /* Fill in length fields */
        end_tag_be(pb, mmf->awapos);
        end_tag_be(pb, mmf->atrpos);
        end_tag_be(pb, 8);

        pos  = avio_tell(pb);
        size = pos - mmf->awapos;

        /* Fill Atsq chunk */
        avio_seek(pb, mmf->atsqpos, SEEK_SET);

        /* "play wav" */
        avio_w8(pb, 0); /* start time */
        avio_w8(pb, 1); /* (channel << 6) | wavenum */
        gatetime = size * 500 / s->streams[0]->codecpar->sample_rate;
        put_varlength(pb, gatetime); /* duration */

        /* "nop" */
        put_varlength(pb, gatetime); /* start time */
        avio_write(pb, "\xff\x00", 2); /* nop */

        /* "end of sequence" */
        avio_write(pb, "\x00\x00\x00\x00", 4);

        avio_seek(pb, pos, SEEK_SET);

        avio_flush(pb);
    }
    return 0;
}
#endif /* CONFIG_MMF_MUXER */

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
static int mmf_read_header(AVFormatContext *s)
{
    MMFContext *mmf = s->priv_data;
    unsigned int tag;
    AVIOContext *pb = s->pb;
    AVStream *st;
    int64_t size;
    int rate, params;

    tag = avio_rl32(pb);
    if (tag != MKTAG('M', 'M', 'M', 'D'))
        return -1;
    avio_skip(pb, 4); /* file_size */

    /* Skip some unused chunks that may or may not be present */
    for (;; avio_skip(pb, size)) {
        tag  = avio_rl32(pb);
        size = avio_rb32(pb);
        if (tag == MKTAG('C', 'N', 'T', 'I'))
            continue;
        if (tag == MKTAG('O', 'P', 'D', 'A'))
            continue;
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

    avio_r8(pb); /* format type */
    avio_r8(pb); /* sequence type */
    params = avio_r8(pb); /* (channel << 7) | (format << 4) | rate */
    rate   = mmf_rate(params & 0x0f);
    if (rate < 0) {
        av_log(s, AV_LOG_ERROR, "Invalid sample rate\n");
        return -1;
    }
    avio_r8(pb); /* wave base bit */
    avio_r8(pb); /* time base d */
    avio_r8(pb); /* time base g */

    /* Skip some unused chunks that may or may not be present */
    for (;; avio_skip(pb, size)) {
        tag  = avio_rl32(pb);
        size = avio_rb32(pb);
        if (tag == MKTAG('A', 't', 's', 'q'))
            continue;
        if (tag == MKTAG('A', 's', 'p', 'I'))
            continue;
        break;
    }

    /* Make sure it's followed by an Awa chunk, aka wave data */
    if ((tag & 0xffffff) != MKTAG('A', 'w', 'a', 0)) {
        av_log(s, AV_LOG_ERROR, "Unexpected SMAF chunk %08x\n", tag);
        return -1;
    }
    mmf->data_size = size;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codecpar->codec_type            = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id              = AV_CODEC_ID_ADPCM_YAMAHA;
    st->codecpar->sample_rate           = rate;
    st->codecpar->channels              = 1;
    st->codecpar->channel_layout        = AV_CH_LAYOUT_MONO;
    st->codecpar->bits_per_coded_sample = 4;
    st->codecpar->bit_rate              = st->codecpar->sample_rate *
                                          st->codecpar->bits_per_coded_sample;

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);

    return 0;
}

#define MAX_SIZE 4096

static int mmf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MMFContext *mmf = s->priv_data;
    int ret, size;

    if (s->pb->eof_reached)
        return AVERROR(EIO);

    size = MAX_SIZE;
    if (size > mmf->data_size)
        size = mmf->data_size;

    if (!size)
        return AVERROR(EIO);

    if (av_new_packet(pkt, size))
        return AVERROR(EIO);
    pkt->stream_index = 0;

    ret = avio_read(s->pb, pkt->data, pkt->size);
    if (ret < 0)
        av_packet_unref(pkt);

    mmf->data_size -= ret;

    pkt->size = ret;
    return ret;
}

#if CONFIG_MMF_DEMUXER
AVInputFormat ff_mmf_demuxer = {
    .name           = "mmf",
    .long_name      = NULL_IF_CONFIG_SMALL("Yamaha SMAF"),
    .priv_data_size = sizeof(MMFContext),
    .read_probe     = mmf_probe,
    .read_header    = mmf_read_header,
    .read_packet    = mmf_read_packet,
    .read_seek      = ff_pcm_read_seek,
};
#endif

#if CONFIG_MMF_MUXER
AVOutputFormat ff_mmf_muxer = {
    .name           = "mmf",
    .long_name      = NULL_IF_CONFIG_SMALL("Yamaha SMAF"),
    .mime_type      = "application/vnd.smaf",
    .extensions     = "mmf",
    .priv_data_size = sizeof(MMFContext),
    .audio_codec    = AV_CODEC_ID_ADPCM_YAMAHA,
    .video_codec    = AV_CODEC_ID_NONE,
    .write_header   = mmf_write_header,
    .write_packet   = mmf_write_packet,
    .write_trailer  = mmf_write_trailer,
};
#endif
