/*
 * AVI decoder.
 * Copyright (c) 2001 Fabrice Bellard.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
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
    int64_t movi_end;
    offset_t movi_list;
    AVIIndex *first, *last;
} AVIContext;

#ifdef DEBUG
static void print_tag(const char *str, unsigned int tag, int size)
{
    printf("%s: tag=%c%c%c%c size=0x%x\n",
           str, tag & 0xff,
           (tag >> 8) & 0xff,
           (tag >> 16) & 0xff,
           (tag >> 24) & 0xff,
           size);
}
#endif

static int avi_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint32_t tag, tag1;
    int codec_type, stream_index, frame_period, bit_rate, scale, rate;
    unsigned int size;
    int i;
    AVStream *st;

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
            /* using frame_period is bad idea */
            frame_period = get_le32(pb);
            bit_rate = get_le32(pb) * 8;
	    url_fskip(pb, 4 * 4);
            s->nb_streams = get_le32(pb);
            for(i=0;i<s->nb_streams;i++) {
                AVStream *st = av_mallocz(sizeof(AVStream));
                if (!st)
                    goto fail;
                avcodec_get_context_defaults(&st->codec);

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

                if (stream_index >= s->nb_streams) {
                    url_fskip(pb, size - 4);
                    break;
                } 

                st = s->streams[stream_index];

                get_le32(pb); /* codec tag */
                get_le32(pb); /* flags */
                get_le16(pb); /* priority */
                get_le16(pb); /* language */
                get_le32(pb); /* XXX: initial frame ? */
                scale= get_le32(pb); /* scale */
                rate= get_le32(pb); /* rate */

                if(scale && rate)
                    st->codec.frame_rate= (rate * (uint64_t)FRAME_RATE_BASE + scale/2) / scale;
                else if(frame_period)
                    st->codec.frame_rate = (1000000LL * FRAME_RATE_BASE + frame_period/2) / frame_period;
                else
                    st->codec.frame_rate = 25 * FRAME_RATE_BASE;
                
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
                    get_le16(pb); /* panes */
                    st->codec.bits_per_sample= get_le16(pb); /* depth */
                    tag1 = get_le32(pb);
                    get_le32(pb); /* ImageSize */
                    get_le32(pb); /* XPelsPerMeter */
                    get_le32(pb); /* YPelsPerMeter */
                    get_le32(pb); /* ClrUsed */
                    get_le32(pb); /* ClrImportant */

                    st->codec.extradata_size= size - 10*4;
                    st->codec.extradata= av_malloc(st->codec.extradata_size); //FIXME where should we free this?
                    get_buffer(pb, st->codec.extradata, st->codec.extradata_size);
                    
                    if(st->codec.extradata_size & 1) //FIXME check if the encoder really did this correctly
                        get_byte(pb);

#ifdef DEBUG
                    print_tag("video", tag1, 0);
#endif
                    st->codec.codec_type = CODEC_TYPE_VIDEO;
                    st->codec.codec_tag = tag1;
                    st->codec.codec_id = codec_get_id(codec_bmp_tags, tag1);
//                    url_fskip(pb, size - 5 * 4);
                    break;
                case CODEC_TYPE_AUDIO:
                    get_wav_header(pb, &st->codec, (size >= 18));
                    if (size%2) /* 2-aligned (fix for Stargate SG-1 - 3x18 - Shades of Grey.avi) */
                        url_fskip(pb, 1);
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
            av_freep(&s->streams[i]);
        }
        return -1;
    }

    return 0;
}

static int avi_read_packet(AVFormatContext *s, AVPacket *pkt)
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

static int avi_read_close(AVFormatContext *s)
{
    return 0;
}

static int avi_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if (p->buf[0] == 'R' && p->buf[1] == 'I' &&
        p->buf[2] == 'F' && p->buf[3] == 'F' &&
        p->buf[8] == 'A' && p->buf[9] == 'V' &&
        p->buf[10] == 'I' && p->buf[11] == ' ')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static AVInputFormat avi_iformat = {
    "avi",
    "avi format",
    sizeof(AVIContext),
    avi_probe,
    avi_read_header,
    avi_read_packet,
    avi_read_close,
};

int avidec_init(void)
{
    av_register_input_format(&avi_iformat);
    return 0;
}
