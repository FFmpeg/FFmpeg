/*
 * AVI decoder.
 * Copyright (c) 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "avformat.h"
#include "avi.h"

//#define DEBUG

typedef struct AVIIndex {
    unsigned char tag[4];
    unsigned int flags, pos, len;
    struct AVIIndex *next;
} AVIIndex;

typedef struct {
    INT64 movi_end;
    offset_t movi_list;
    AVIIndex *first, *last;
} AVIContext;

#ifdef DEBUG
void print_tag(const char *str, unsigned int tag, int size)
{
    printf("%s: tag=%c%c%c%c size=0x%x\n",
           str, tag & 0xff,
           (tag >> 8) & 0xff,
           (tag >> 16) & 0xff,
           (tag >> 24) & 0xff,
           size);
}
#endif

int avi_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVIContext *avi;
    ByteIOContext *pb = &s->pb;
    UINT32 tag, tag1;
    int codec_type, stream_index, size, frame_period, bit_rate;
    int i, bps;
    AVStream *st;

    avi = malloc(sizeof(AVIContext));
    if (!avi)
        return -1;
    memset(avi, 0, sizeof(AVIContext));
    s->priv_data = avi;

    /* check RIFF header */
    tag = get_le32(pb);

    if (tag != MKTAG('R', 'I', 'F', 'F'))
        return -1;
    get_le32(pb); /* file size */
    tag = get_le32(pb);
    if (tag != MKTAG('A', 'V', 'I', ' '))
        return -1;
    
    /* first list tag */
    stream_index = -1;
    codec_type = -1;
    frame_period = 0;
    for(;;) {
        if (url_feof(pb))
            goto fail;
        tag = get_le32(pb);
        size = get_le32(pb);
#ifdef DEBUG
        print_tag("tag", tag, size);
#endif

        switch(tag) {
        case MKTAG('L', 'I', 'S', 'T'):
            /* ignored, except when start of video packets */
            tag1 = get_le32(pb);
#ifdef DEBUG
            print_tag("list", tag1, 0);
#endif
            if (tag1 == MKTAG('m', 'o', 'v', 'i')) {
                avi->movi_end = url_ftell(pb) + size - 4;
#ifdef DEBUG
                printf("movi end=%Lx\n", avi->movi_end);
#endif
                goto end_of_header;
            }
            break;
        case MKTAG('a', 'v', 'i', 'h'):
            /* avi header */
            frame_period = get_le32(pb);
            bit_rate = get_le32(pb) * 8;
            url_fskip(pb, 4 * 4);
            s->nb_streams = get_le32(pb);
            for(i=0;i<s->nb_streams;i++) {
                AVStream *st;
                st = malloc(sizeof(AVStream));
                if (!st)
                    goto fail;
                memset(st, 0, sizeof(AVStream));
                s->streams[i] = st;
            }
            url_fskip(pb, size - 7 * 4);
            break;
        case MKTAG('s', 't', 'r', 'h'):
            /* stream header */
            stream_index++;
            tag1 = get_le32(pb);
            switch(tag1) {
            case MKTAG('v', 'i', 'd', 's'):
                codec_type = CODEC_TYPE_VIDEO;
                get_le32(pb); /* codec tag */
                get_le32(pb); /* flags */
                get_le16(pb); /* priority */
                get_le16(pb); /* language */
                get_le32(pb); /* XXX: initial frame ? */
                get_le32(pb); /* scale */
                get_le32(pb); /* rate */
                url_fskip(pb, size - 7 * 4);
                break;
            case MKTAG('a', 'u', 'd', 's'):
                codec_type = CODEC_TYPE_AUDIO;
                /* nothing really useful */
                url_fskip(pb, size - 4);
                break;
            default:
                goto fail;
            }
            break;
        case MKTAG('s', 't', 'r', 'f'):
            /* stream header */
            if (stream_index >= s->nb_streams) {
                url_fskip(pb, size);
            } else {
                st = s->streams[stream_index];
                switch(codec_type) {
                case CODEC_TYPE_VIDEO:
                    get_le32(pb); /* size */
                    st->codec.width = get_le32(pb);
                    st->codec.height = get_le32(pb);
                    if (frame_period)
                        st->codec.frame_rate = (INT64_C(1000000) * FRAME_RATE_BASE) / frame_period;
                    else
                        st->codec.frame_rate = 25 * FRAME_RATE_BASE;
                    get_le16(pb); /* panes */
                    get_le16(pb); /* depth */
                    tag1 = get_le32(pb);
#ifdef DEBUG
                    print_tag("video", tag1, 0);
#endif
                    st->codec.codec_type = CODEC_TYPE_VIDEO;
                    st->codec.codec_tag = tag1;
                    st->codec.codec_id = codec_get_id(codec_bmp_tags, tag1);
                    url_fskip(pb, size - 5 * 4);
                    break;
                case CODEC_TYPE_AUDIO:
                    tag1 = get_le16(pb);
                    st->codec.codec_type = CODEC_TYPE_AUDIO;
                    st->codec.codec_tag = tag1;
#ifdef DEBUG
                    printf("audio: 0x%x\n", tag1);
#endif
                    st->codec.channels = get_le16(pb);
                    st->codec.sample_rate = get_le32(pb);
                    st->codec.bit_rate = get_le32(pb) * 8;
                    get_le16(pb); /* block align */
                    bps = get_le16(pb);
                    st->codec.codec_id = wav_codec_get_id(tag1, bps);
                    url_fskip(pb, size - 4 * 4);
                    break;
                default:
                    url_fskip(pb, size);
                    break;
                }
            }
            break;
        default:
            /* skip tag */
            size += (size & 1);
            url_fskip(pb, size);
            break;
        }
    }
 end_of_header:
    /* check stream number */
    if (stream_index != s->nb_streams - 1) {
    fail:
        for(i=0;i<s->nb_streams;i++) {
            if (s->streams[i])
                free(s->streams[i]);
        }
        return -1;
    }
        
    return 0;
}

int avi_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int n, d1, d2, size;
    
 find_next:
    if (url_feof(pb) || url_ftell(pb) >= avi->movi_end)
        return -1;
    d1 = get_byte(pb);
    if (d1 < '0' || d1 > '9')
        goto find_next;
    d2 = get_byte(pb);
    if (d2 < '0' || d2 > '9')
        goto find_next;
    n = (d1 - '0') * 10 + (d2 - '0');
    
    if (n < 0 || n >= s->nb_streams)
        goto find_next;
    
    d1 = get_byte(pb);
    d2 = get_byte(pb);
    if ((d1 != 'd' && d2 != 'c') &&
        (d1 != 'w' && d2 != 'b'))
        goto find_next;
    
    size = get_le32(pb);
    av_new_packet(pkt, size);
    pkt->stream_index = n;

    get_buffer(pb, pkt->data, pkt->size);
    
    if (size & 1)
        get_byte(pb);
    
    return 0;
}

int avi_read_close(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    free(avi);
    return 0;
}
