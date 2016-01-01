/*
 * MP3 demuxer
 * Copyright (c) 2003 Fabrice Bellard
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

#include "libavutil/opt.h"
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

#define SAME_HEADER_MASK \
   (0xffe00000 | (3 << 17) | (3 << 10) | (3 << 19))

typedef struct {
    AVClass *class;
    int64_t filesize;
    int xing_toc;
    int start_pad;
    int end_pad;
    int usetoc;
    unsigned frames; /* Total number of frames in file */
    unsigned header_filesize;   /* Total number of bytes in the stream */
    int is_cbr;
} MP3DecContext;

static int check(AVIOContext *pb, int64_t pos, uint32_t *header);

/* mp3 read */

static int mp3_read_probe(AVProbeData *p)
{
    int max_frames, first_frames = 0;
    int frames, ret;
    uint32_t header;
    const uint8_t *buf, *buf0, *buf2, *end;

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
    if   (first_frames>=7) return AVPROBE_SCORE_EXTENSION + 1;
    else if(max_frames>200)return AVPROBE_SCORE_EXTENSION;
    else if(max_frames>=4 && max_frames >= p->buf_size/10000) return AVPROBE_SCORE_EXTENSION / 2;
    else if(ff_id3v2_match(buf0, ID3v2_DEFAULT_MAGIC) && 2*ff_id3v2_tag_len(buf0) >= p->buf_size)
                           return p->buf_size < PROBE_BUF_MAX ? AVPROBE_SCORE_EXTENSION / 4 : AVPROBE_SCORE_EXTENSION - 2;
    else if(max_frames>=1 && max_frames >= p->buf_size/10000) return 1;
    else                   return 0;
//mpegps_mp3_unrecognized_format.mpg has max_frames=3
}

static void read_xing_toc(AVFormatContext *s, int64_t filesize, int64_t duration)
{
    int i;
    MP3DecContext *mp3 = s->priv_data;
    int fast_seek = s->flags & AVFMT_FLAG_FAST_SEEK;
    int fill_index = (mp3->usetoc || fast_seek) && duration > 0;

    if (!filesize &&
        !(filesize = avio_size(s->pb))) {
        av_log(s, AV_LOG_WARNING, "Cannot determine file size, skipping TOC table.\n");
        fill_index = 0;
    }

    for (i = 0; i < XING_TOC_COUNT; i++) {
        uint8_t b = avio_r8(s->pb);
        if (fill_index)
            av_add_index_entry(s->streams[0],
                           av_rescale(b, filesize, 256),
                           av_rescale(i, duration, XING_TOC_COUNT),
                           0, 0, AVINDEX_KEYFRAME);
    }
    if (fill_index)
        mp3->xing_toc = 1;
}

static void mp3_parse_info_tag(AVFormatContext *s, AVStream *st,
                               MPADecodeHeader *c, uint32_t spf)
{
#define LAST_BITS(k, n) ((k) & ((1 << (n)) - 1))
#define MIDDLE_BITS(k, m, n) LAST_BITS((k) >> (m), ((n) - (m)))

    uint16_t crc;
    uint32_t v;

    char version[10];

    uint32_t peak   = 0;
    int32_t  r_gain = INT32_MIN, a_gain = INT32_MIN;

    MP3DecContext *mp3 = s->priv_data;
    static const int64_t xing_offtbl[2][2] = {{32, 17}, {17,9}};
    uint64_t fsize = avio_size(s->pb);
    fsize = fsize >= avio_tell(s->pb) ? fsize - avio_tell(s->pb) : 0;

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
        mp3->header_filesize = avio_rb32(s->pb);
    if (fsize && mp3->header_filesize) {
        uint64_t min, delta;
        min = FFMIN(fsize, mp3->header_filesize);
        delta = FFMAX(fsize, mp3->header_filesize) - min;
        if (fsize > mp3->header_filesize && delta > min >> 4) {
            mp3->frames = 0;
            av_log(s, AV_LOG_WARNING,
                   "invalid concatenated file detected - using bitrate for duration\n");
        } else if (delta > min >> 4) {
            av_log(s, AV_LOG_WARNING,
                   "filesize and duration do not match (growing file?)\n");
        }
    }
    if (v & XING_FLAG_TOC)
        read_xing_toc(s, mp3->header_filesize, av_rescale_q(mp3->frames,
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
    v= avio_rb24(s->pb);
    if(AV_RB32(version) == MKBETAG('L', 'A', 'M', 'E')
        || AV_RB32(version) == MKBETAG('L', 'a', 'v', 'f')
        || AV_RB32(version) == MKBETAG('L', 'a', 'v', 'c')
    ) {

        mp3->start_pad = v>>12;
        mp3->  end_pad = v&4095;
        st->start_skip_samples = mp3->start_pad + 528 + 1;
        if (mp3->frames) {
            st->first_discard_sample = -mp3->end_pad + 528 + 1 + mp3->frames * (int64_t)spf;
            st->last_discard_sample = mp3->frames * (int64_t)spf;
        }
        if (!st->start_time)
            st->start_time = av_rescale_q(st->start_skip_samples,
                                            (AVRational){1, c->sample_rate},
                                            st->time_base);
        av_log(s, AV_LOG_DEBUG, "pad %d %d\n", mp3->start_pad, mp3->  end_pad);
    }

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
            mp3->header_filesize = avio_rb32(s->pb);
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
    mp3->header_filesize   = 0;

    mp3_parse_info_tag(s, st, &c, spf);
    mp3_parse_vbri_tag(s, st, base);

    if (!mp3->frames && !mp3->header_filesize)
        return -1;

    /* Skip the vbr tag frame */
    avio_seek(s->pb, base + vbrtag_size, SEEK_SET);

    if (mp3->frames)
        st->duration = av_rescale_q(mp3->frames, (AVRational){spf, c.sample_rate},
                                    st->time_base);
    if (mp3->header_filesize && mp3->frames && !mp3->is_cbr)
        st->codec->bit_rate = av_rescale(mp3->header_filesize, 8 * c.sample_rate, mp3->frames * (int64_t)spf);

    return 0;
}

static int mp3_read_header(AVFormatContext *s)
{
    MP3DecContext *mp3 = s->priv_data;
    AVStream *st;
    int64_t off;
    int ret;
    int i;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = AV_CODEC_ID_MP3;
    st->need_parsing = AVSTREAM_PARSE_FULL_RAW;
    st->start_time = 0;

    // lcm of all mp3 sample rates
    avpriv_set_pts_info(st, 64, 1, 14112000);

    s->pb->maxsize = -1;
    off = avio_tell(s->pb);

    if (!av_dict_get(s->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX))
        ff_id3v1_read(s);

    if(s->pb->seekable)
        mp3->filesize = avio_size(s->pb);

    if (mp3_parse_vbr_tags(s, st, off) < 0)
        avio_seek(s->pb, off, SEEK_SET);

    ret = ff_replaygain_export(st, s->metadata);
    if (ret < 0)
        return ret;

    off = avio_tell(s->pb);
    for (i = 0; i < 64 * 1024; i++) {
        uint32_t header, header2;
        int frame_size;
        if (!(i&1023))
            ffio_ensure_seekback(s->pb, i + 1024 + 4);
        frame_size = check(s->pb, off + i, &header);
        if (frame_size > 0) {
            avio_seek(s->pb, off, SEEK_SET);
            ffio_ensure_seekback(s->pb, i + 1024 + frame_size + 4);
            if (check(s->pb, off + i + frame_size, &header2) >= 0 &&
                (header & SAME_HEADER_MASK) == (header2 & SAME_HEADER_MASK))
            {
                av_log(s, AV_LOG_INFO, "Skipping %d bytes of junk at %"PRId64".\n", i, off);
                avio_seek(s->pb, off + i, SEEK_SET);
                break;
            }
        }
        avio_seek(s->pb, off, SEEK_SET);
    }

    // the seek index is relative to the end of the xing vbr headers
    for (i = 0; i < st->nb_index_entries; i++)
        st->index_entries[i].pos += avio_tell(s->pb);

    /* the parameters will be extracted from the compressed bitstream */
    return 0;
}

#define MP3_PACKET_SIZE 1024

static int mp3_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MP3DecContext *mp3 = s->priv_data;
    int ret, size;
    int64_t pos;

    size= MP3_PACKET_SIZE;
    pos = avio_tell(s->pb);
    if(mp3->filesize > ID3v1_TAG_SIZE && pos < mp3->filesize)
        size= FFMIN(size, mp3->filesize - pos);

    ret= av_get_packet(s->pb, pkt, size);
    if (ret <= 0) {
        if(ret<0)
            return ret;
        return AVERROR_EOF;
    }

    pkt->flags &= ~AV_PKT_FLAG_CORRUPT;
    pkt->stream_index = 0;

    return ret;
}

#define SEEK_WINDOW 4096

static int check(AVIOContext *pb, int64_t pos, uint32_t *ret_header)
{
    int64_t ret = avio_seek(pb, pos, SEEK_SET);
    unsigned header;
    MPADecodeHeader sd;
    if (ret < 0)
        return ret;

    header = avio_rb32(pb);
    if (ff_mpa_check_header(header) < 0)
        return -1;
    if (avpriv_mpegaudio_decode_header(&sd, header) == 1)
        return -1;

    if (ret_header)
        *ret_header = header;
    return sd.frame_size;
}

static int64_t mp3_sync(AVFormatContext *s, int64_t target_pos, int flags)
{
    int dir = (flags&AVSEEK_FLAG_BACKWARD) ? -1 : 1;
    int64_t best_pos;
    int best_score, i, j;
    int64_t ret;

    avio_seek(s->pb, FFMAX(target_pos - SEEK_WINDOW, 0), SEEK_SET);
    ret = avio_seek(s->pb, target_pos, SEEK_SET);
    if (ret < 0)
        return ret;

#define MIN_VALID 3
    best_pos = target_pos;
    best_score = 999;
    for(i=0; i<SEEK_WINDOW; i++) {
        int64_t pos = target_pos + (dir > 0 ? i - SEEK_WINDOW/4 : -i);
        int64_t candidate = -1;
        int score = 999;

        if (pos < 0)
            continue;

        for(j=0; j<MIN_VALID; j++) {
            ret = check(s->pb, pos, NULL);
            if(ret < 0)
                break;
            if ((target_pos - pos)*dir <= 0 && abs(MIN_VALID/2-j) < score) {
                candidate = pos;
                score = abs(MIN_VALID/2-j);
            }
            pos += ret;
        }
        if (best_score > score && j == MIN_VALID) {
            best_pos = candidate;
            best_score = score;
            if(score == 0)
                break;
        }
    }

    return avio_seek(s->pb, best_pos, SEEK_SET);
}

static int mp3_seek(AVFormatContext *s, int stream_index, int64_t timestamp,
                    int flags)
{
    MP3DecContext *mp3 = s->priv_data;
    AVIndexEntry *ie, ie1;
    AVStream *st = s->streams[0];
    int64_t best_pos;
    int fast_seek = s->flags & AVFMT_FLAG_FAST_SEEK;
    int64_t filesize = mp3->header_filesize;

    if (filesize <= 0) {
        int64_t size = avio_size(s->pb);
        if (size > 0 && size > s->internal->data_offset)
            filesize = size - s->internal->data_offset;
    }

    if (mp3->xing_toc && (mp3->usetoc || (fast_seek && !mp3->is_cbr))) {
        int64_t ret = av_index_search_timestamp(st, timestamp, flags);

        // NOTE: The MP3 TOC is not a precise lookup table. Accuracy is worse
        // for bigger files.
        av_log(s, AV_LOG_WARNING, "Using MP3 TOC to seek; may be imprecise.\n");

        if (ret < 0)
            return ret;

        ie = &st->index_entries[ret];
    } else if (fast_seek && st->duration > 0 && filesize > 0) {
        if (!mp3->is_cbr)
            av_log(s, AV_LOG_WARNING, "Using scaling to seek VBR MP3; may be imprecise.\n");

        ie = &ie1;
        timestamp = av_clip64(timestamp, 0, st->duration);
        ie->timestamp = timestamp;
        ie->pos       = av_rescale(timestamp, filesize, st->duration) + s->internal->data_offset;
    } else {
        return -1; // generic index code
    }

    best_pos = mp3_sync(s, ie->pos, flags);
    if (best_pos < 0)
        return best_pos;

    if (mp3->is_cbr && ie == &ie1 && mp3->frames) {
        int frame_duration = av_rescale(st->duration, 1, mp3->frames);
        ie1.timestamp = frame_duration * av_rescale(best_pos - s->internal->data_offset, mp3->frames, mp3->header_filesize);
    }

    ff_update_cur_dts(s, st, ie->timestamp);
    return 0;
}

static const AVOption options[] = {
    { "usetoc", "use table of contents", offsetof(MP3DecContext, usetoc), AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, AV_OPT_FLAG_DECODING_PARAM},
    { NULL },
};

static const AVClass demuxer_class = {
    .class_name = "mp3",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DEMUXER,
};

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
    .priv_class     = &demuxer_class,
};
