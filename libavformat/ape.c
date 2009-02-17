/*
 * Monkey's Audio APE demuxer
 * Copyright (c) 2007 Benjamin Zores <ben@geexbox.org>
 *  based upon libdemac from Dave Chapman.
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

#include <stdio.h>

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define ENABLE_DEBUG 0

/* The earliest and latest file formats supported by this library */
#define APE_MIN_VERSION 3950
#define APE_MAX_VERSION 3990

#define MAC_FORMAT_FLAG_8_BIT                 1 // is 8-bit [OBSOLETE]
#define MAC_FORMAT_FLAG_CRC                   2 // uses the new CRC32 error detection [OBSOLETE]
#define MAC_FORMAT_FLAG_HAS_PEAK_LEVEL        4 // uint32 nPeakLevel after the header [OBSOLETE]
#define MAC_FORMAT_FLAG_24_BIT                8 // is 24-bit [OBSOLETE]
#define MAC_FORMAT_FLAG_HAS_SEEK_ELEMENTS    16 // has the number of seek elements after the peak level
#define MAC_FORMAT_FLAG_CREATE_WAV_HEADER    32 // create the wave header on decompression (not stored)

#define MAC_SUBFRAME_SIZE 4608

#define APE_EXTRADATA_SIZE 6

/* APE tags */
#define APE_TAG_VERSION               2000
#define APE_TAG_FOOTER_BYTES          32
#define APE_TAG_FLAG_CONTAINS_HEADER  (1 << 31)
#define APE_TAG_FLAG_IS_HEADER        (1 << 29)

typedef struct {
    int64_t pos;
    int nblocks;
    int size;
    int skip;
    int64_t pts;
} APEFrame;

typedef struct {
    /* Derived fields */
    uint32_t junklength;
    uint32_t firstframe;
    uint32_t totalsamples;
    int currentframe;
    APEFrame *frames;

    /* Info from Descriptor Block */
    char magic[4];
    int16_t fileversion;
    int16_t padding1;
    uint32_t descriptorlength;
    uint32_t headerlength;
    uint32_t seektablelength;
    uint32_t wavheaderlength;
    uint32_t audiodatalength;
    uint32_t audiodatalength_high;
    uint32_t wavtaillength;
    uint8_t md5[16];

    /* Info from Header Block */
    uint16_t compressiontype;
    uint16_t formatflags;
    uint32_t blocksperframe;
    uint32_t finalframeblocks;
    uint32_t totalframes;
    uint16_t bps;
    uint16_t channels;
    uint32_t samplerate;

    /* Seektable */
    uint32_t *seektable;
} APEContext;

static void ape_tag_read_field(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    uint8_t key[1024], value[1024];
    uint32_t size;
    int i, l;

    size = get_le32(pb);  /* field size */
    url_fskip(pb, 4);     /* skip field flags */

    for (i=0; pb->buf_ptr[i]!='0' && pb->buf_ptr[i]>=0x20 && pb->buf_ptr[i]<=0x7E; i++);

    l = FFMIN(i,    sizeof(key) -1);
    get_buffer(pb, key,  l);
    key[l]  = 0;
    url_fskip(pb, 1 + i-l);
    l = FFMIN(size, sizeof(value)-1);
    get_buffer(pb, value, l);
    value[l] = 0;
    url_fskip(pb, size-l);
    if (l < size)
        av_log(s, AV_LOG_WARNING, "Too long '%s' tag was truncated.\n", key);
    av_metadata_set(&s->metadata, key, value);
}

static void ape_parse_tag(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    int file_size = url_fsize(pb);
    uint32_t val, fields, tag_bytes;
    uint8_t buf[8];
    int i;

    if (file_size < APE_TAG_FOOTER_BYTES)
        return;

    url_fseek(pb, file_size - APE_TAG_FOOTER_BYTES, SEEK_SET);

    get_buffer(pb, buf, 8);    /* APETAGEX */
    if (strncmp(buf, "APETAGEX", 8)) {
        return;
    }

    val = get_le32(pb);        /* APE tag version */
    if (val > APE_TAG_VERSION) {
        av_log(s, AV_LOG_ERROR, "Unsupported tag version. (>=%d)\n", APE_TAG_VERSION);
        return;
    }

    tag_bytes = get_le32(pb);  /* tag size */
    if (tag_bytes - APE_TAG_FOOTER_BYTES > (1024 * 1024 * 16)) {
        av_log(s, AV_LOG_ERROR, "Tag size is way too big\n");
        return;
    }

    fields = get_le32(pb);     /* number of fields */
    if (fields > 65536) {
        av_log(s, AV_LOG_ERROR, "Too many tag fields (%d)\n", fields);
        return;
    }

    val = get_le32(pb);        /* flags */
    if (val & APE_TAG_FLAG_IS_HEADER) {
        av_log(s, AV_LOG_ERROR, "APE Tag is a header\n");
        return;
    }

    if (val & APE_TAG_FLAG_CONTAINS_HEADER)
        tag_bytes += 2*APE_TAG_FOOTER_BYTES;

    url_fseek(pb, file_size - tag_bytes, SEEK_SET);

    for (i=0; i<fields; i++)
        ape_tag_read_field(s);

#if ENABLE_DEBUG
    av_log(s, AV_LOG_DEBUG, "\nAPE Tags:\n\n");
    av_log(s, AV_LOG_DEBUG, "title     = %s\n", s->title);
    av_log(s, AV_LOG_DEBUG, "author    = %s\n", s->author);
    av_log(s, AV_LOG_DEBUG, "copyright = %s\n", s->copyright);
    av_log(s, AV_LOG_DEBUG, "comment   = %s\n", s->comment);
    av_log(s, AV_LOG_DEBUG, "album     = %s\n", s->album);
    av_log(s, AV_LOG_DEBUG, "year      = %d\n", s->year);
    av_log(s, AV_LOG_DEBUG, "track     = %d\n", s->track);
    av_log(s, AV_LOG_DEBUG, "genre     = %s\n", s->genre);
#endif
}

static int ape_probe(AVProbeData * p)
{
    if (p->buf[0] == 'M' && p->buf[1] == 'A' && p->buf[2] == 'C' && p->buf[3] == ' ')
        return AVPROBE_SCORE_MAX;

    return 0;
}

static void ape_dumpinfo(AVFormatContext * s, APEContext * ape_ctx)
{
#if ENABLE_DEBUG
    int i;

    av_log(s, AV_LOG_DEBUG, "Descriptor Block:\n\n");
    av_log(s, AV_LOG_DEBUG, "magic                = \"%c%c%c%c\"\n", ape_ctx->magic[0], ape_ctx->magic[1], ape_ctx->magic[2], ape_ctx->magic[3]);
    av_log(s, AV_LOG_DEBUG, "fileversion          = %d\n", ape_ctx->fileversion);
    av_log(s, AV_LOG_DEBUG, "descriptorlength     = %d\n", ape_ctx->descriptorlength);
    av_log(s, AV_LOG_DEBUG, "headerlength         = %d\n", ape_ctx->headerlength);
    av_log(s, AV_LOG_DEBUG, "seektablelength      = %d\n", ape_ctx->seektablelength);
    av_log(s, AV_LOG_DEBUG, "wavheaderlength      = %d\n", ape_ctx->wavheaderlength);
    av_log(s, AV_LOG_DEBUG, "audiodatalength      = %d\n", ape_ctx->audiodatalength);
    av_log(s, AV_LOG_DEBUG, "audiodatalength_high = %d\n", ape_ctx->audiodatalength_high);
    av_log(s, AV_LOG_DEBUG, "wavtaillength        = %d\n", ape_ctx->wavtaillength);
    av_log(s, AV_LOG_DEBUG, "md5                  = ");
    for (i = 0; i < 16; i++)
         av_log(s, AV_LOG_DEBUG, "%02x", ape_ctx->md5[i]);
    av_log(s, AV_LOG_DEBUG, "\n");

    av_log(s, AV_LOG_DEBUG, "\nHeader Block:\n\n");

    av_log(s, AV_LOG_DEBUG, "compressiontype      = %d\n", ape_ctx->compressiontype);
    av_log(s, AV_LOG_DEBUG, "formatflags          = %d\n", ape_ctx->formatflags);
    av_log(s, AV_LOG_DEBUG, "blocksperframe       = %d\n", ape_ctx->blocksperframe);
    av_log(s, AV_LOG_DEBUG, "finalframeblocks     = %d\n", ape_ctx->finalframeblocks);
    av_log(s, AV_LOG_DEBUG, "totalframes          = %d\n", ape_ctx->totalframes);
    av_log(s, AV_LOG_DEBUG, "bps                  = %d\n", ape_ctx->bps);
    av_log(s, AV_LOG_DEBUG, "channels             = %d\n", ape_ctx->channels);
    av_log(s, AV_LOG_DEBUG, "samplerate           = %d\n", ape_ctx->samplerate);

    av_log(s, AV_LOG_DEBUG, "\nSeektable\n\n");
    if ((ape_ctx->seektablelength / sizeof(uint32_t)) != ape_ctx->totalframes) {
        av_log(s, AV_LOG_DEBUG, "No seektable\n");
    } else {
        for (i = 0; i < ape_ctx->seektablelength / sizeof(uint32_t); i++) {
            if (i < ape_ctx->totalframes - 1) {
                av_log(s, AV_LOG_DEBUG, "%8d   %d (%d bytes)\n", i, ape_ctx->seektable[i], ape_ctx->seektable[i + 1] - ape_ctx->seektable[i]);
            } else {
                av_log(s, AV_LOG_DEBUG, "%8d   %d\n", i, ape_ctx->seektable[i]);
            }
        }
    }

    av_log(s, AV_LOG_DEBUG, "\nFrames\n\n");
    for (i = 0; i < ape_ctx->totalframes; i++)
        av_log(s, AV_LOG_DEBUG, "%8d   %8lld %8d (%d samples)\n", i, ape_ctx->frames[i].pos, ape_ctx->frames[i].size, ape_ctx->frames[i].nblocks);

    av_log(s, AV_LOG_DEBUG, "\nCalculated information:\n\n");
    av_log(s, AV_LOG_DEBUG, "junklength           = %d\n", ape_ctx->junklength);
    av_log(s, AV_LOG_DEBUG, "firstframe           = %d\n", ape_ctx->firstframe);
    av_log(s, AV_LOG_DEBUG, "totalsamples         = %d\n", ape_ctx->totalsamples);
#endif
}

static int ape_read_header(AVFormatContext * s, AVFormatParameters * ap)
{
    ByteIOContext *pb = s->pb;
    APEContext *ape = s->priv_data;
    AVStream *st;
    uint32_t tag;
    int i;
    int total_blocks;
    int64_t pts;

    /* TODO: Skip any leading junk such as id3v2 tags */
    ape->junklength = 0;

    tag = get_le32(pb);
    if (tag != MKTAG('M', 'A', 'C', ' '))
        return -1;

    ape->fileversion = get_le16(pb);

    if (ape->fileversion < APE_MIN_VERSION || ape->fileversion > APE_MAX_VERSION) {
        av_log(s, AV_LOG_ERROR, "Unsupported file version - %d.%02d\n", ape->fileversion / 1000, (ape->fileversion % 1000) / 10);
        return -1;
    }

    if (ape->fileversion >= 3980) {
        ape->padding1             = get_le16(pb);
        ape->descriptorlength     = get_le32(pb);
        ape->headerlength         = get_le32(pb);
        ape->seektablelength      = get_le32(pb);
        ape->wavheaderlength      = get_le32(pb);
        ape->audiodatalength      = get_le32(pb);
        ape->audiodatalength_high = get_le32(pb);
        ape->wavtaillength        = get_le32(pb);
        get_buffer(pb, ape->md5, 16);

        /* Skip any unknown bytes at the end of the descriptor.
           This is for future compatibility */
        if (ape->descriptorlength > 52)
            url_fseek(pb, ape->descriptorlength - 52, SEEK_CUR);

        /* Read header data */
        ape->compressiontype      = get_le16(pb);
        ape->formatflags          = get_le16(pb);
        ape->blocksperframe       = get_le32(pb);
        ape->finalframeblocks     = get_le32(pb);
        ape->totalframes          = get_le32(pb);
        ape->bps                  = get_le16(pb);
        ape->channels             = get_le16(pb);
        ape->samplerate           = get_le32(pb);
    } else {
        ape->descriptorlength = 0;
        ape->headerlength = 32;

        ape->compressiontype      = get_le16(pb);
        ape->formatflags          = get_le16(pb);
        ape->channels             = get_le16(pb);
        ape->samplerate           = get_le32(pb);
        ape->wavheaderlength      = get_le32(pb);
        ape->wavtaillength        = get_le32(pb);
        ape->totalframes          = get_le32(pb);
        ape->finalframeblocks     = get_le32(pb);

        if (ape->formatflags & MAC_FORMAT_FLAG_HAS_PEAK_LEVEL) {
            url_fseek(pb, 4, SEEK_CUR); /* Skip the peak level */
            ape->headerlength += 4;
        }

        if (ape->formatflags & MAC_FORMAT_FLAG_HAS_SEEK_ELEMENTS) {
            ape->seektablelength = get_le32(pb);
            ape->headerlength += 4;
            ape->seektablelength *= sizeof(int32_t);
        } else
            ape->seektablelength = ape->totalframes * sizeof(int32_t);

        if (ape->formatflags & MAC_FORMAT_FLAG_8_BIT)
            ape->bps = 8;
        else if (ape->formatflags & MAC_FORMAT_FLAG_24_BIT)
            ape->bps = 24;
        else
            ape->bps = 16;

        if (ape->fileversion >= 3950)
            ape->blocksperframe = 73728 * 4;
        else if (ape->fileversion >= 3900 || (ape->fileversion >= 3800  && ape->compressiontype >= 4000))
            ape->blocksperframe = 73728;
        else
            ape->blocksperframe = 9216;

        /* Skip any stored wav header */
        if (!(ape->formatflags & MAC_FORMAT_FLAG_CREATE_WAV_HEADER))
            url_fskip(pb, ape->wavheaderlength);
    }

    if(ape->totalframes > UINT_MAX / sizeof(APEFrame)){
        av_log(s, AV_LOG_ERROR, "Too many frames: %d\n", ape->totalframes);
        return -1;
    }
    ape->frames       = av_malloc(ape->totalframes * sizeof(APEFrame));
    if(!ape->frames)
        return AVERROR_NOMEM;
    ape->firstframe   = ape->junklength + ape->descriptorlength + ape->headerlength + ape->seektablelength + ape->wavheaderlength;
    ape->currentframe = 0;


    ape->totalsamples = ape->finalframeblocks;
    if (ape->totalframes > 1)
        ape->totalsamples += ape->blocksperframe * (ape->totalframes - 1);

    if (ape->seektablelength > 0) {
        ape->seektable = av_malloc(ape->seektablelength);
        for (i = 0; i < ape->seektablelength / sizeof(uint32_t); i++)
            ape->seektable[i] = get_le32(pb);
    }

    ape->frames[0].pos     = ape->firstframe;
    ape->frames[0].nblocks = ape->blocksperframe;
    ape->frames[0].skip    = 0;
    for (i = 1; i < ape->totalframes; i++) {
        ape->frames[i].pos      = ape->seektable[i]; //ape->frames[i-1].pos + ape->blocksperframe;
        ape->frames[i].nblocks  = ape->blocksperframe;
        ape->frames[i - 1].size = ape->frames[i].pos - ape->frames[i - 1].pos;
        ape->frames[i].skip     = (ape->frames[i].pos - ape->frames[0].pos) & 3;
    }
    ape->frames[ape->totalframes - 1].size    = ape->finalframeblocks * 4;
    ape->frames[ape->totalframes - 1].nblocks = ape->finalframeblocks;

    for (i = 0; i < ape->totalframes; i++) {
        if(ape->frames[i].skip){
            ape->frames[i].pos  -= ape->frames[i].skip;
            ape->frames[i].size += ape->frames[i].skip;
        }
        ape->frames[i].size = (ape->frames[i].size + 3) & ~3;
    }


    ape_dumpinfo(s, ape);

    /* try to read APE tags */
    if (!url_is_streamed(pb)) {
        ape_parse_tag(s);
        url_fseek(pb, 0, SEEK_SET);
    }

    av_log(s, AV_LOG_DEBUG, "Decoding file - v%d.%02d, compression level %d\n", ape->fileversion / 1000, (ape->fileversion % 1000) / 10, ape->compressiontype);

    /* now we are ready: build format streams */
    st = av_new_stream(s, 0);
    if (!st)
        return -1;

    total_blocks = (ape->totalframes == 0) ? 0 : ((ape->totalframes - 1) * ape->blocksperframe) + ape->finalframeblocks;

    st->codec->codec_type      = CODEC_TYPE_AUDIO;
    st->codec->codec_id        = CODEC_ID_APE;
    st->codec->codec_tag       = MKTAG('A', 'P', 'E', ' ');
    st->codec->channels        = ape->channels;
    st->codec->sample_rate     = ape->samplerate;
    st->codec->bits_per_coded_sample = ape->bps;
    st->codec->frame_size      = MAC_SUBFRAME_SIZE;

    st->nb_frames = ape->totalframes;
    s->start_time = 0;
    s->duration   = (int64_t) total_blocks * AV_TIME_BASE / ape->samplerate;
    av_set_pts_info(st, 64, MAC_SUBFRAME_SIZE, ape->samplerate);

    st->codec->extradata = av_malloc(APE_EXTRADATA_SIZE);
    st->codec->extradata_size = APE_EXTRADATA_SIZE;
    AV_WL16(st->codec->extradata + 0, ape->fileversion);
    AV_WL16(st->codec->extradata + 2, ape->compressiontype);
    AV_WL16(st->codec->extradata + 4, ape->formatflags);

    pts = 0;
    for (i = 0; i < ape->totalframes; i++) {
        ape->frames[i].pts = pts;
        av_add_index_entry(st, ape->frames[i].pos, ape->frames[i].pts, 0, 0, AVINDEX_KEYFRAME);
        pts += ape->blocksperframe / MAC_SUBFRAME_SIZE;
    }

    return 0;
}

static int ape_read_packet(AVFormatContext * s, AVPacket * pkt)
{
    int ret;
    int nblocks;
    APEContext *ape = s->priv_data;
    uint32_t extra_size = 8;

    if (url_feof(s->pb))
        return AVERROR_IO;
    if (ape->currentframe > ape->totalframes)
        return AVERROR_IO;

    url_fseek (s->pb, ape->frames[ape->currentframe].pos, SEEK_SET);

    /* Calculate how many blocks there are in this frame */
    if (ape->currentframe == (ape->totalframes - 1))
        nblocks = ape->finalframeblocks;
    else
        nblocks = ape->blocksperframe;

    if (av_new_packet(pkt,  ape->frames[ape->currentframe].size + extra_size) < 0)
        return AVERROR_NOMEM;

    AV_WL32(pkt->data    , nblocks);
    AV_WL32(pkt->data + 4, ape->frames[ape->currentframe].skip);
    ret = get_buffer(s->pb, pkt->data + extra_size, ape->frames[ape->currentframe].size);

    pkt->pts = ape->frames[ape->currentframe].pts;
    pkt->stream_index = 0;

    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret + extra_size;

    ape->currentframe++;

    return 0;
}

static int ape_read_close(AVFormatContext * s)
{
    APEContext *ape = s->priv_data;

    av_freep(&ape->frames);
    av_freep(&ape->seektable);
    return 0;
}

static int ape_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    APEContext *ape = s->priv_data;
    int index = av_index_search_timestamp(st, timestamp, flags);

    if (index < 0)
        return -1;

    ape->currentframe = index;
    return 0;
}

AVInputFormat ape_demuxer = {
    "ape",
    NULL_IF_CONFIG_SMALL("Monkey's Audio"),
    sizeof(APEContext),
    ape_probe,
    ape_read_header,
    ape_read_packet,
    ape_read_close,
    ape_read_seek,
    .extensions = "ape,apl,mac"
};
