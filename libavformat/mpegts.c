/*
 * MPEG2 transport stream (aka DVB) demux
 * Copyright (c) 2002 Fabrice Bellard.
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

#define TS_FEC_PACKET_SIZE 204
#define TS_PACKET_SIZE 188
#define NB_PID_MAX 8192

enum MpegTSState {
    MPEGTS_HEADER = 0,
    MPEGTS_PESHEADER_FILL,
    MPEGTS_PESHEADER_FLAGS,
    MPEGTS_PESHEADER_SIZE,
    MPEGTS_PESHEADER_READ,
    MPEGTS_PAYLOAD,
    MPEGTS_SKIP,
};

/* enough for PES header + length */
#define MAX_HEADER_SIZE 6

typedef struct MpegTSStream {
    int pid;
    enum MpegTSState state;
    int last_cc; /* last cc code (-1 if first packet) */
    /* used to get the format */
    int header_size;
    int payload_size;
    int pes_header_size;
    AVStream *st;
    unsigned char header[MAX_HEADER_SIZE];
} MpegTSStream;

typedef struct MpegTSContext {
    int raw_packet_size; /* raw packet size, including FEC if present */
    MpegTSStream *pids[NB_PID_MAX];
} MpegTSContext;

/* autodetect fec presence. Must have at least 1024 bytes  */
static int get_packet_size(const unsigned char *buf, int size)
{
    int i;

    if (size < (TS_FEC_PACKET_SIZE * 5 + 1))
        return -1;
    for(i=0;i<5;i++) {
        if (buf[i * TS_PACKET_SIZE] != 0x47)
            goto try_fec;
    }
    return TS_PACKET_SIZE;
 try_fec:
    for(i=0;i<5;i++) {
        if (buf[i * TS_FEC_PACKET_SIZE] != 0x47)
            return -1;
    }
    return TS_FEC_PACKET_SIZE;
}

static int mpegts_probe(AVProbeData *p)
{
    int size;
    size = get_packet_size(p->buf, p->buf_size);
    if (size < 0)
        return 0;
    return AVPROBE_SCORE_MAX - 1;
}

static int mpegts_read_header(AVFormatContext *s,
                              AVFormatParameters *ap)
{
    MpegTSContext *ts = s->priv_data;
    ByteIOContext *pb = &s->pb;
    unsigned char buf[1024];
    int len;
    int64_t pos;

    /* read the first 1024 bytes to get packet size */
    pos = url_ftell(pb);
    len = get_buffer(pb, buf, sizeof(buf));
    if (len != sizeof(buf))
        goto fail;
    ts->raw_packet_size = get_packet_size(buf, sizeof(buf));
    if (ts->raw_packet_size <= 0)
        goto fail;
    /* go again to the start */
    url_fseek(pb, pos, SEEK_SET);
    return 0;
 fail:
    return -1;
}

/* return non zero if a packet could be constructed */
static int mpegts_push_data(AVFormatContext *s, MpegTSStream *tss,
                            AVPacket *pkt,
                            const unsigned char *buf, int buf_size, int is_start)
{
    AVStream *st;
    const unsigned char *p;
    int len, code, codec_type, codec_id;

    if (is_start) {
        tss->state = MPEGTS_HEADER;
        tss->header_size = 0;
    }
    p = buf;
    while (buf_size > 0) {
        len = buf_size;
        switch(tss->state) {
        case MPEGTS_HEADER:
            if (len > MAX_HEADER_SIZE - tss->header_size)
                len = MAX_HEADER_SIZE - tss->header_size;
            memcpy(tss->header, p, len);
            tss->header_size += len;
            p += len;
            buf_size -= len;
            if (tss->header_size == MAX_HEADER_SIZE) {
                /* we got all the PES or section header. We can now
                   decide */
#if 0
                av_hex_dump(tss->header, tss->header_size);
#endif
                if (tss->header[0] == 0x00 && tss->header[1] == 0x00 &&
                    tss->header[2] == 0x01) {
                    /* it must be an mpeg2 PES stream */
                    /* XXX: add AC3 support */
                    code = tss->header[3] | 0x100;
                    if (!((code >= 0x1c0 && code <= 0x1df) ||
                          (code >= 0x1e0 && code <= 0x1ef)))
                        goto skip;
                    if (!tss->st) {
                        /* allocate stream */
                        if (code >= 0x1c0 && code <= 0x1df) {
                            codec_type = CODEC_TYPE_AUDIO;
                            codec_id = CODEC_ID_MP2;
                        } else {
                            codec_type = CODEC_TYPE_VIDEO;
                            codec_id = CODEC_ID_MPEG1VIDEO;
                        }
                        st = av_new_stream(s, tss->pid);
                        if (st) {
                            st->priv_data = tss;
                            st->codec.codec_type = codec_type;
                            st->codec.codec_id = codec_id;
                            tss->st = st;
                        }
                    }
                    tss->state = MPEGTS_PESHEADER_FILL;
                    tss->payload_size = (tss->header[4] << 8) | tss->header[5];
                    if (tss->payload_size == 0)
                        tss->payload_size = 65536;
                } else {
                    /* otherwise, it should be a table */
                    /* skip packet */
                skip:
                    tss->state = MPEGTS_SKIP;
                    continue;
                }
            }
            break;
            /**********************************************/
            /* PES packing parsing */
        case MPEGTS_PESHEADER_FILL:
            /* skip filling */
            code = *p++;
            buf_size--;
            tss->payload_size--;
            if (code != 0xff) {
                if ((code & 0xc0) != 0x80)
                    goto skip;
                tss->state = MPEGTS_PESHEADER_FLAGS;
            }
            break;
        case MPEGTS_PESHEADER_FLAGS:
            code = *p++;
            buf_size--;
            tss->payload_size--;
            tss->state = MPEGTS_PESHEADER_SIZE;
            break;
        case MPEGTS_PESHEADER_SIZE:
            tss->pes_header_size = *p++;
            buf_size--;
            tss->payload_size--;
            tss->state = MPEGTS_PESHEADER_READ;
            break;
        case MPEGTS_PESHEADER_READ:
            /* currently we do nothing except skipping */
            if (len > tss->pes_header_size)
                len = tss->pes_header_size;
            p += len;
            buf_size -= len;
            tss->pes_header_size -= len;
            tss->payload_size -= len;
            if (tss->pes_header_size == 0)
                tss->state = MPEGTS_PAYLOAD;
            break;
        case MPEGTS_PAYLOAD:
            if (len > tss->payload_size)
                len = tss->payload_size;
            if (len > 0) {
                if (tss->st && av_new_packet(pkt, buf_size) == 0) {
                    memcpy(pkt->data, p, buf_size);
                    pkt->stream_index = tss->st->index;
                    return 1;
                }
                tss->payload_size -= len;
            }
            buf_size = 0;
            break;
        case MPEGTS_SKIP:
            buf_size = 0;
            break;
        }
    }
    return 0;
}

static int mpegts_read_packet(AVFormatContext *s,
                              AVPacket *pkt)
{
    MpegTSContext *ts = s->priv_data;
    MpegTSStream *tss;
    ByteIOContext *pb = &s->pb;
    unsigned char packet[TS_FEC_PACKET_SIZE];
    int len, pid, cc, cc_ok, afc;
    const unsigned char *p;
    
    for(;;) {
        len = get_buffer(pb, packet, ts->raw_packet_size);
        if (len != ts->raw_packet_size)
            return AVERROR_IO;
        /* check paquet sync byte */
        /* XXX: accept to resync ? */
        if (packet[0] != 0x47)
            return AVERROR_INVALIDDATA;
        
        pid = ((packet[1] & 0x1f) << 8) | packet[2];
        tss = ts->pids[pid];
        if (tss == NULL) {
            /* if no pid found, then add a pid context */
            tss = av_mallocz(sizeof(MpegTSStream));
            if (!tss) 
                continue;
            ts->pids[pid] = tss;
            tss->pid = pid;
            tss->last_cc = -1;
            //            printf("new pid=0x%x\n", pid);
        }

        /* continuity check (currently not used) */
        cc = (packet[3] & 0xf);
        cc_ok = (tss->last_cc < 0) || ((((tss->last_cc + 1) & 0x0f) == cc));
        tss->last_cc = cc;
        
        /* skip adaptation field */
        afc = (packet[3] >> 4) & 3;
        p = packet + 4;
        if (afc == 0) /* reserved value */
            continue;
        if (afc == 2) /* adaptation field only */
            continue;
        if (afc == 3) {
            /* skip adapation field */
            p += p[0] + 1;
        }
        /* if past the end of packet, ignore */
        if (p >= packet + TS_PACKET_SIZE)
            continue;
    
        if (mpegts_push_data(s, tss, pkt, p, TS_PACKET_SIZE - (p - packet), 
                             packet[1] & 0x40))
            break;
    }
    return 0;
}

static int mpegts_read_close(AVFormatContext *s)
{
    MpegTSContext *ts = s->priv_data;
    int i;
    for(i=0;i<NB_PID_MAX;i++)
        av_free(ts->pids[i]);
    return 0;
}

AVInputFormat mpegts_demux = {
    "mpegts",
    "MPEG2 transport stream format",
    sizeof(MpegTSContext),
    mpegts_probe,
    mpegts_read_header,
    mpegts_read_packet,
    mpegts_read_close,
    .flags = AVFMT_NOHEADER | AVFMT_SHOW_IDS,
};

int mpegts_init(void)
{
    av_register_input_format(&mpegts_demux);
    return 0;
}
