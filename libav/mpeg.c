/*
 * Output a MPEG1 multiplexed video/audio stream
 * Copyright (c) 2000 Gerard Lantau.
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
#include "tick.h"

#define MAX_PAYLOAD_SIZE 4096
#define NB_STREAMS 2

typedef struct {
    UINT8 buffer[MAX_PAYLOAD_SIZE];
    int buffer_ptr;
    UINT8 id;
    int max_buffer_size; /* in bytes */
    int packet_number;
    INT64 pts;
    Ticker pts_ticker;
    INT64 start_pts;
} StreamInfo;

typedef struct {
    int packet_size; /* required packet size */
    int packet_data_max_size; /* maximum data size inside a packet */
    int packet_number;
    int pack_header_freq;     /* frequency (in packets^-1) at which we send pack headers */
    int system_header_freq;
    int mux_rate; /* bitrate in units of 50 bytes/s */
    /* stream info */
    int audio_bound;
    int video_bound;
} MpegMuxContext;

#define PACK_START_CODE             ((unsigned int)0x000001ba)
#define SYSTEM_HEADER_START_CODE    ((unsigned int)0x000001bb)
#define PACKET_START_CODE_MASK      ((unsigned int)0xffffff00)
#define PACKET_START_CODE_PREFIX    ((unsigned int)0x00000100)
#define ISO_11172_END_CODE          ((unsigned int)0x000001b9)
  
/* mpeg2 */
#define PROGRAM_STREAM_MAP 0x1bc
#define PRIVATE_STREAM_1   0x1bd
#define PADDING_STREAM     0x1be
#define PRIVATE_STREAM_2   0x1bf


#define AUDIO_ID 0xc0
#define VIDEO_ID 0xe0

static int mpeg_mux_check_packet(AVFormatContext *s, int *size);

static int put_pack_header(AVFormatContext *ctx, 
                           UINT8 *buf, INT64 timestamp)
{
    MpegMuxContext *s = ctx->priv_data;
    PutBitContext pb;
    
    init_put_bits(&pb, buf, 128, NULL, NULL);

    put_bits(&pb, 32, PACK_START_CODE);
    put_bits(&pb, 4, 0x2);
    put_bits(&pb, 3, (UINT32)((timestamp >> 30) & 0x07));
    put_bits(&pb, 1, 1);
    put_bits(&pb, 15, (UINT32)((timestamp >> 15) & 0x7fff));
    put_bits(&pb, 1, 1);
    put_bits(&pb, 15, (UINT32)((timestamp) & 0x7fff));
    put_bits(&pb, 1, 1);
    put_bits(&pb, 1, 1);
    put_bits(&pb, 22, s->mux_rate);
    put_bits(&pb, 1, 1);

    flush_put_bits(&pb);
    return pbBufPtr(&pb) - pb.buf;
}

static int put_system_header(AVFormatContext *ctx, UINT8 *buf)
{
    MpegMuxContext *s = ctx->priv_data;
    int size, rate_bound, i, private_stream_coded, id;
    PutBitContext pb;

    init_put_bits(&pb, buf, 128, NULL, NULL);

    put_bits(&pb, 32, SYSTEM_HEADER_START_CODE);
    put_bits(&pb, 16, 0);
    put_bits(&pb, 1, 1);
    
    rate_bound = s->mux_rate; /* maximum bit rate of the multiplexed stream */
    put_bits(&pb, 22, rate_bound);
    put_bits(&pb, 1, 1); /* marker */
    put_bits(&pb, 6, s->audio_bound);

    put_bits(&pb, 1, 1); /* variable bitrate */
    put_bits(&pb, 1, 1); /* non constrainted bit stream */
    
    put_bits(&pb, 1, 0); /* audio locked */
    put_bits(&pb, 1, 0); /* video locked */
    put_bits(&pb, 1, 1); /* marker */

    put_bits(&pb, 5, s->video_bound);
    put_bits(&pb, 8, 0xff); /* reserved byte */
    
    /* audio stream info */
    private_stream_coded = 0;
    for(i=0;i<ctx->nb_streams;i++) {
        StreamInfo *stream = ctx->streams[i]->priv_data;
        id = stream->id;
        if (id < 0xc0) {
            /* special case for private streams (AC3 use that) */
            if (private_stream_coded)
                continue;
            private_stream_coded = 1;
            id = 0xbd;
        }
        put_bits(&pb, 8, id); /* stream ID */
        put_bits(&pb, 2, 3);
        if (id < 0xe0) {
            /* audio */
            put_bits(&pb, 1, 0);
            put_bits(&pb, 13, stream->max_buffer_size / 128);
        } else {
            /* video */
            put_bits(&pb, 1, 1);
            put_bits(&pb, 13, stream->max_buffer_size / 1024);
        }
    }
    flush_put_bits(&pb);
    size = pbBufPtr(&pb) - pb.buf;
    /* patch packet size */
    buf[4] = (size - 6) >> 8;
    buf[5] = (size - 6) & 0xff;

    return size;
}

static int mpeg_mux_init(AVFormatContext *ctx)
{
    MpegMuxContext *s;
    int bitrate, i, mpa_id, mpv_id, ac3_id;
    AVStream *st;
    StreamInfo *stream;

    s = malloc(sizeof(MpegMuxContext));
    if (!s)
        return -1;
    memset(s, 0, sizeof(MpegMuxContext));
    ctx->priv_data = s;
    s->packet_number = 0;

    /* XXX: hardcoded */
    s->packet_size = 2048;
    /* startcode(4) + length(2) + flags(1) */
    s->packet_data_max_size = s->packet_size - 7;
    s->audio_bound = 0;
    s->video_bound = 0;
    mpa_id = AUDIO_ID;
    ac3_id = 0x80;
    mpv_id = VIDEO_ID;
    for(i=0;i<ctx->nb_streams;i++) {
        st = ctx->streams[i];
        stream = av_mallocz(sizeof(StreamInfo));
        if (!stream)
            goto fail;
        st->priv_data = stream;

        switch(st->codec.codec_type) {
        case CODEC_TYPE_AUDIO:
            if (st->codec.codec_id == CODEC_ID_AC3)
                stream->id = ac3_id++;
            else
                stream->id = mpa_id++;
            stream->max_buffer_size = 4 * 1024; 
            s->audio_bound++;
            break;
        case CODEC_TYPE_VIDEO:
            stream->id = mpv_id++;
            stream->max_buffer_size = 46 * 1024; 
            s->video_bound++;
            break;
        }
    }

    /* we increase slightly the bitrate to take into account the
       headers. XXX: compute it exactly */
    bitrate = 2000;
    for(i=0;i<ctx->nb_streams;i++) {
        st = ctx->streams[i];
        bitrate += st->codec.bit_rate;
    }
    s->mux_rate = (bitrate + (8 * 50) - 1) / (8 * 50);
    /* every 2 seconds */
    s->pack_header_freq = 2 * bitrate / s->packet_size / 8;
    /* every 10 seconds */
    s->system_header_freq = s->pack_header_freq * 5;

    for(i=0;i<ctx->nb_streams;i++) {
        stream = ctx->streams[i]->priv_data;
        stream->buffer_ptr = 0;
        stream->packet_number = 0;
        stream->pts = 0;
        stream->start_pts = -1;

        st = ctx->streams[i];
        switch (st->codec.codec_type) {
        case CODEC_TYPE_AUDIO:
            ticker_init(&stream->pts_ticker,
                        st->codec.sample_rate,
                        90000 * st->codec.frame_size);
            break;
        case CODEC_TYPE_VIDEO:
            ticker_init(&stream->pts_ticker,
                        st->codec.frame_rate,
                        90000 * FRAME_RATE_BASE);
            break;
        }
    }
    return 0;
 fail:
    for(i=0;i<ctx->nb_streams;i++) {
        free(ctx->streams[i]->priv_data);
    }
    free(s);
    return -ENOMEM;
}

/* flush the packet on stream stream_index */
static void flush_packet(AVFormatContext *ctx, int stream_index)
{
    MpegMuxContext *s = ctx->priv_data;
    StreamInfo *stream = ctx->streams[stream_index]->priv_data;
    UINT8 *buf_ptr;
    int size, payload_size, startcode, id, len, stuffing_size, i;
    INT64 timestamp;
    UINT8 buffer[128];

    id = stream->id;
    timestamp = stream->start_pts;

#if 0
    printf("packet ID=%2x PTS=%0.3f\n", 
           id, timestamp / 90000.0);
#endif

    buf_ptr = buffer;
    if ((s->packet_number % s->pack_header_freq) == 0) {
        /* output pack and systems header if needed */
        size = put_pack_header(ctx, buf_ptr, timestamp);
        buf_ptr += size;
        if ((s->packet_number % s->system_header_freq) == 0) {
            size = put_system_header(ctx, buf_ptr);
            buf_ptr += size;
        }
    }
    size = buf_ptr - buffer;
    put_buffer(&ctx->pb, buffer, size);

    /* packet header */
    payload_size = s->packet_size - (size + 6 + 5);
    if (id < 0xc0) {
        startcode = PRIVATE_STREAM_1;
        payload_size -= 4;
    } else {
        startcode = 0x100 + id;
    }
    stuffing_size = payload_size - stream->buffer_ptr;
    if (stuffing_size < 0)
        stuffing_size = 0;

    put_be32(&ctx->pb, startcode);

    put_be16(&ctx->pb, payload_size + 5);
    /* stuffing */
    for(i=0;i<stuffing_size;i++)
        put_byte(&ctx->pb, 0xff);
    
    /* presentation time stamp */
    put_byte(&ctx->pb, 
             (0x02 << 4) | 
             (((timestamp >> 30) & 0x07) << 1) | 
             1);
    put_be16(&ctx->pb, (UINT16)((((timestamp >> 15) & 0x7fff) << 1) | 1));
    put_be16(&ctx->pb, (UINT16)((((timestamp) & 0x7fff) << 1) | 1));

    if (startcode == PRIVATE_STREAM_1) {
        put_byte(&ctx->pb, id);
        if (id >= 0x80 && id <= 0xbf) {
            /* XXX: need to check AC3 spec */
            put_byte(&ctx->pb, 1);
            put_byte(&ctx->pb, 0);
            put_byte(&ctx->pb, 2);
        }
    }

    /* output data */
    put_buffer(&ctx->pb, stream->buffer, payload_size - stuffing_size);
    put_flush_packet(&ctx->pb);
    
    /* preserve remaining data */
    len = stream->buffer_ptr - payload_size;
    if (len < 0) 
        len = 0;
    memmove(stream->buffer, stream->buffer + stream->buffer_ptr - len, len);
    stream->buffer_ptr = len;

    s->packet_number++;
    stream->packet_number++;
    stream->start_pts = -1;
}

static int mpeg_mux_write_packet(AVFormatContext *ctx, int stream_index,
                                 UINT8 *buf, int size, int force_pts)
{
    MpegMuxContext *s = ctx->priv_data;
    AVStream *st = ctx->streams[stream_index];
    StreamInfo *stream = st->priv_data;
    int len;
    
    while (size > 0) {
        /* set pts */
        if (stream->start_pts == -1) {
            if (force_pts)
                stream->pts = force_pts;
            stream->start_pts = stream->pts;
        }
        len = s->packet_data_max_size - stream->buffer_ptr;
        if (len > size)
            len = size;
        memcpy(stream->buffer + stream->buffer_ptr, buf, len);
        stream->buffer_ptr += len;
        buf += len;
        size -= len;
        while (stream->buffer_ptr >= s->packet_data_max_size) {
            /* output the packet */
            if (stream->start_pts == -1)
                stream->start_pts = stream->pts;
            flush_packet(ctx, stream_index);
        }
    }

    stream->pts += ticker_tick(&stream->pts_ticker, 1);
    return 0;
}

static int mpeg_mux_end(AVFormatContext *ctx)
{
    StreamInfo *stream;
    int i;

    /* flush each packet */
    for(i=0;i<ctx->nb_streams;i++) {
        stream = ctx->streams[i]->priv_data;
        if (stream->buffer_ptr > 0) 
            flush_packet(ctx, i);
    }

    /* write the end header */
    put_be32(&ctx->pb, ISO_11172_END_CODE);
    put_flush_packet(&ctx->pb);
    return 0;
}

/*********************************************/
/* demux code */

#define MAX_SYNC_SIZE 100000

typedef struct MpegDemuxContext {
    int header_state;
    int mux_rate; /* 50 byte/s unit */
} MpegDemuxContext;

static int find_start_code(ByteIOContext *pb, int *size_ptr, 
                           UINT32 *header_state)
{
    unsigned int state, v;
    int val, n;

    state = *header_state;
    n = *size_ptr;
    while (n > 0) {
        if (url_feof(pb))
            break;
        v = get_byte(pb);
        n--;
        if (state == 0x000001) {
            state = ((state << 8) | v) & 0xffffff;
            val = state;
            goto found;
        }
        state = ((state << 8) | v) & 0xffffff;
    }
    val = -1;
 found:
    *header_state = state;
    *size_ptr = n;
    return val;
}

static int check_stream_id(AVFormatContext *s, int c_id)
{
    AVStream *st;
    int i;
    
    for(i = 0;i < s->nb_streams;i++) {
        st = s->streams[i];
        if (st && st->id == c_id)
            return 1;
    }
    return 0;   
}

static int mpeg_mux_read_header(AVFormatContext *s,
                                AVFormatParameters *ap)
{
    MpegDemuxContext *m;
    int size, startcode, c, rate_bound, audio_bound, video_bound, mux_rate, val;
    int codec_id, n, i, type;
    AVStream *st;
    offset_t start_pos;

    m = av_mallocz(sizeof(MpegDemuxContext));
    if (!m)
        return -ENOMEM;
    s->priv_data = m;

    /* search first pack header */
    m->header_state = 0xff;
    size = MAX_SYNC_SIZE;
    start_pos = url_ftell(&s->pb); /* remember this pos */
    for(;;) {
        while (size > 0) {
            startcode = find_start_code(&s->pb, &size, &m->header_state);
            if (startcode == PACK_START_CODE)
                goto found;
        }
        /* System Header not found find streams searching through file */
        fprintf(stderr,"libav: MPEG-PS System Header not found!\n");
        url_fseek(&s->pb, start_pos, SEEK_SET);
        video_bound = 0;
        audio_bound = 0;
        c = 0;
        s->nb_streams = 0;
        size = 15*MAX_SYNC_SIZE;
        while (size > 0) {
            type = 0;
            codec_id = 0;
            n = 0;
            startcode = find_start_code(&s->pb, &size, &m->header_state);
            //fprintf(stderr,"\nstartcode: %x pos=0x%Lx\n", startcode, url_ftell(&s->pb));
            if (startcode == 0x1bd) {
                url_fseek(&s->pb, -4, SEEK_CUR);
                size += 4;
                startcode = mpeg_mux_check_packet(s, &size);
                //fprintf(stderr,"\nstartcode: %x pos=0x%Lx\n", startcode, url_ftell(&s->pb));
                if (startcode >= 0x80 && startcode <= 0x9f && !check_stream_id(s, startcode)) {
                    //fprintf(stderr,"Found AC3 stream ID: 0x%x\n", startcode);
                    type = CODEC_TYPE_AUDIO;
                    codec_id = CODEC_ID_AC3;
                    audio_bound++;
                    n = 1;
                    c = startcode;
                }    
            } else if (startcode == 0x1e0 && !check_stream_id(s, startcode)) {
                //fprintf(stderr,"Found MPEGVIDEO stream ID: 0x%x\n", startcode);
                type = CODEC_TYPE_VIDEO;
                codec_id = CODEC_ID_MPEG1VIDEO;
                n = 1;
                c = startcode;
                video_bound++;
            } /*else if (startcode >= 0x1c0 && startcode <= 0x1df && !check_stream_id(s, startcode)) {
                fprintf(stderr,"Found MPEGAUDIO stream ID: 0x%x\n", startcode);
                type = CODEC_TYPE_AUDIO;
                codec_id = CODEC_ID_MP2;
                n = 1;
                c = startcode;
                audio_bound++;
            } */
            for(i=0;i<n;i++) {
                st = av_mallocz(sizeof(AVStream));
                if (!st)
                    return -ENOMEM;
                s->streams[s->nb_streams++] = st;
                st->id = c;
                st->codec.codec_type = type;
                st->codec.codec_id = codec_id;
            }
        }
        if (video_bound || audio_bound) {
            url_fseek(&s->pb, start_pos, SEEK_SET);
            return 0;
        } else
            return -ENODATA;
    found:
        /* search system header just after pack header */
        /* parse pack header */
        get_byte(&s->pb); /* ts1 */
        get_be16(&s->pb); /* ts2 */
        get_be16(&s->pb); /* ts3 */

        mux_rate = get_byte(&s->pb) << 16; 
        mux_rate |= get_byte(&s->pb) << 8;
        mux_rate |= get_byte(&s->pb);
        mux_rate &= (1 << 22) - 1;
        m->mux_rate = mux_rate;

        startcode = find_start_code(&s->pb, &size, &m->header_state);
        if (startcode == SYSTEM_HEADER_START_CODE)
            break;
    }
    size = get_be16(&s->pb);
    rate_bound = get_byte(&s->pb) << 16;
    rate_bound |= get_byte(&s->pb) << 8;
    rate_bound |= get_byte(&s->pb);
    rate_bound = (rate_bound >> 1) & ((1 << 22) - 1);
    audio_bound = get_byte(&s->pb) >> 2;
    video_bound = get_byte(&s->pb) & 0x1f;
    get_byte(&s->pb); /* reserved byte */
#if 0
    printf("mux_rate=%d kbit/s\n", (m->mux_rate * 50 * 8) / 1000);
    printf("rate_bound=%d\n", rate_bound);
    printf("audio_bound=%d\n", audio_bound);
    printf("video_bound=%d\n", video_bound);
#endif
    size -= 6;
    s->nb_streams = 0;
    while (size > 0) {
        c = get_byte(&s->pb);
        size--;
        if ((c & 0x80) == 0)
            break;
        val = get_be16(&s->pb);
        size -= 2;
        if (c >= 0xc0 && c <= 0xdf) {
            /* mpeg audio stream */
            type = CODEC_TYPE_AUDIO;
            codec_id = CODEC_ID_MP2;
            n = 1;
            c = c | 0x100;
        } else if (c >= 0xe0 && c <= 0xef) {
            type = CODEC_TYPE_VIDEO;
            codec_id = CODEC_ID_MPEG1VIDEO;
            n = 1;
            c = c | 0x100;
        } else if (c == 0xb8) {
            /* all audio streams */
            /* XXX: hack for DVD: we force AC3, although we do not
               know that this codec will be used */
            type = CODEC_TYPE_AUDIO;
            codec_id = CODEC_ID_AC3;
            /* XXX: Another hack for DVD: it seems, that AC3 streams
               aren't signaled on audio_bound on some DVDs (Matrix) */
            if (audio_bound == 0)
            	audio_bound++;
            n = audio_bound;
            c = 0x80;
            //c = 0x1c0;
        } else if (c == 0xb9) {
            /* all video streams */
            type = CODEC_TYPE_VIDEO;
            codec_id = CODEC_ID_MPEG1VIDEO;
            n = video_bound;
            c = 0x1e0;
        } else {
            type = 0;
            codec_id = 0;
            n = 0;
        }
        for(i=0;i<n;i++) {
            st = av_mallocz(sizeof(AVStream));
            if (!st)
                return -ENOMEM;
            s->streams[s->nb_streams++] = st;
            st->id = c + i;
            st->codec.codec_type = type;
            st->codec.codec_id = codec_id;
        }
    }

    return 0;
}

static INT64 get_pts(ByteIOContext *pb, int c)
{
    INT64 pts;
    int val;

    if (c < 0)
        c = get_byte(pb);
    pts = (INT64)((c >> 1) & 0x07) << 30;
    val = get_be16(pb);
    pts |= (INT64)(val >> 1) << 15;
    val = get_be16(pb);
    pts |= (INT64)(val >> 1);
    return pts;
}

static int mpeg_mux_read_packet(AVFormatContext *s,
                                AVPacket *pkt)
{
    MpegDemuxContext *m = s->priv_data;
    AVStream *st;
    int len, size, startcode, i, c, flags, header_len;
    INT64 pts, dts;

    /* next start code (should be immediately after */
 redo:
    m->header_state = 0xff;
    size = MAX_SYNC_SIZE;
    startcode = find_start_code(&s->pb, &size, &m->header_state);
    //printf("startcode=%x pos=0x%Lx\n", startcode, url_ftell(&s->pb));
    if (startcode < 0)
        return -EIO;
    if (startcode == PACK_START_CODE)
        goto redo;
    if (startcode == SYSTEM_HEADER_START_CODE)
        goto redo;
    if (startcode == PADDING_STREAM ||
        startcode == PRIVATE_STREAM_2) {
        /* skip them */
        len = get_be16(&s->pb);
        url_fskip(&s->pb, len);
        goto redo;
    }
    /* find matching stream */
    if (!((startcode >= 0x1c0 && startcode <= 0x1df) ||
          (startcode >= 0x1e0 && startcode <= 0x1ef) ||
          (startcode == 0x1bd)))
        goto redo;

    len = get_be16(&s->pb);
    pts = 0;
    dts = 0;
    /* stuffing */
    for(;;) {
        c = get_byte(&s->pb);
        len--;
        /* XXX: for mpeg1, should test only bit 7 */
        if (c != 0xff) 
            break;
    }
    if ((c & 0xc0) == 0x40) {
        /* buffer scale & size */
        get_byte(&s->pb);
        c = get_byte(&s->pb);
        len -= 2;
    }
    if ((c & 0xf0) == 0x20) {
        pts = get_pts(&s->pb, c);
        len -= 4;
        dts = pts;
    } else if ((c & 0xf0) == 0x30) {
        pts = get_pts(&s->pb, c);
        dts = get_pts(&s->pb, -1);
        len -= 9;
    } else if ((c & 0xc0) == 0x80) {
        /* mpeg 2 PES */
        if ((c & 0x30) != 0) {
            fprintf(stderr, "Encrypted multiplex not handled\n");
            return -EIO;
        }
        flags = get_byte(&s->pb);
        header_len = get_byte(&s->pb);
        len -= 2;
        if (header_len > len)
            goto redo;
        if ((flags & 0xc0) == 0x40) {
            pts = get_pts(&s->pb, -1);
            dts = pts;
            header_len -= 5;
            len -= 5;
        } if ((flags & 0xc0) == 0xc0) {
            pts = get_pts(&s->pb, -1);
            dts = get_pts(&s->pb, -1);
            header_len -= 10;
            len -= 10;
        }
        len -= header_len;
        while (header_len > 0) {
            get_byte(&s->pb);
            header_len--;
        }
    }
    if (startcode == 0x1bd) {
        startcode = get_byte(&s->pb);
        len--;
        if (startcode >= 0x80 && startcode <= 0xbf) {
            /* audio: skip header */
            get_byte(&s->pb);
            get_byte(&s->pb);
            get_byte(&s->pb);
            len -= 3;
        }
    }

    /* now find stream */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        if (st->id == startcode)
            goto found;
    }
    /* skip packet */
    url_fskip(&s->pb, len);
    goto redo;
 found:
    av_new_packet(pkt, len);
    //printf("\nRead Packet ID: %x PTS: %f Size: %d", startcode,
    //       (float)pts/90000, len);
    get_buffer(&s->pb, pkt->data, pkt->size);
    pkt->pts = pts;
    pkt->stream_index = i;
    return 0;
}

static int mpeg_mux_check_packet(AVFormatContext *s, int *size)
{
    MpegDemuxContext *m = s->priv_data;
    int len, startcode, c, n, flags, header_len;
    INT64 pts, dts;

    /* next start code (should be immediately after */
 redo:
    m->header_state = 0xff;
    startcode = find_start_code(&s->pb, size, &m->header_state);
    
    if (startcode < 0)
        return -EIO;
    if (startcode == PACK_START_CODE)
        goto redo;
    if (startcode == SYSTEM_HEADER_START_CODE)
        goto redo;
    if (startcode == PADDING_STREAM ||
        startcode == PRIVATE_STREAM_2) {
        /* skip them */
        len = get_be16(&s->pb);
        url_fskip(&s->pb, len);
        goto redo;
    }
    /* find matching stream */
    if (!((startcode >= 0x1c0 && startcode <= 0x1df) ||
          (startcode >= 0x1e0 && startcode <= 0x1ef) ||
          (startcode == 0x1bd)))
        goto redo;

    n = *size;
    len = get_be16(&s->pb);
    n -= 2;
    pts = 0;
    dts = 0;
    /* stuffing */
    for(;;) {
        c = get_byte(&s->pb);
        len--;
        n--;
        /* XXX: for mpeg1, should test only bit 7 */
        if (c != 0xff) 
            break;
    }
    if ((c & 0xc0) == 0x40) {
        /* buffer scale & size */
        get_byte(&s->pb);
        c = get_byte(&s->pb);
        len -= 2;
        n -= 2;
    }
    if ((c & 0xf0) == 0x20) {
        pts = get_pts(&s->pb, c);
        len -= 4;
        n -= 4;
        dts = pts;
    } else if ((c & 0xf0) == 0x30) {
        pts = get_pts(&s->pb, c);
        dts = get_pts(&s->pb, -1);
        len -= 9;
        n -= 9;
    } else if ((c & 0xc0) == 0x80) {
        /* mpeg 2 PES */
        if ((c & 0x30) != 0) {
            fprintf(stderr, "Encrypted multiplex not handled\n");
            return -EIO;
        }
        flags = get_byte(&s->pb);
        header_len = get_byte(&s->pb);
        len -= 2;
        n -= 2;
        if (header_len > len)
            goto redo;
        if ((flags & 0xc0) == 0x40) {
            pts = get_pts(&s->pb, -1);
            dts = pts;
            header_len -= 5;
            len -= 5;
            n -= 5;
        } if ((flags & 0xc0) == 0xc0) {
            pts = get_pts(&s->pb, -1);
            dts = get_pts(&s->pb, -1);
            header_len -= 10;
            len -= 10;
            n -= 10;
        }
        len -= header_len;
        n -= header_len;
        while (header_len > 0) {
            get_byte(&s->pb);
            header_len--;
        }
    }
    if (startcode == 0x1bd) {
        startcode = get_byte(&s->pb);
        len--;
        n--;
        if (startcode >= 0x80 && startcode <= 0xbf) {
            /* audio: skip header */
            get_byte(&s->pb);
            get_byte(&s->pb);
            get_byte(&s->pb);
            len -= 3;
            n -= 3;
        }
    }
    *size = n;
    return startcode;
}


static int mpeg_mux_read_close(AVFormatContext *s)
{
    MpegDemuxContext *m = s->priv_data;
    free(m);
    return 0;
}

AVFormat mpeg_mux_format = {
    "mpeg",
    "MPEG multiplex format",
    "video/x-mpeg",
    "mpg,mpeg,vob",
    CODEC_ID_MP2,
    CODEC_ID_MPEG1VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_packet,
    mpeg_mux_end,

    mpeg_mux_read_header,
    mpeg_mux_read_packet,
    mpeg_mux_read_close,
};
