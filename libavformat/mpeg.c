/*
 * MPEG1/2 mux/demux
 * Copyright (c) 2000, 2001, 2002 Fabrice Bellard.
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

#define MAX_PAYLOAD_SIZE 4096
#define NB_STREAMS 2

typedef struct {
    uint8_t buffer[MAX_PAYLOAD_SIZE];
    int buffer_ptr;
    uint8_t id;
    int max_buffer_size; /* in bytes */
    int packet_number;
    int64_t start_pts;
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
    int is_mpeg2;
    int is_vcd;
} MpegMuxContext;

#define PACK_START_CODE             ((unsigned int)0x000001ba)
#define SYSTEM_HEADER_START_CODE    ((unsigned int)0x000001bb)
#define SEQUENCE_END_CODE           ((unsigned int)0x000001b7)
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

extern AVOutputFormat mpeg1system_mux;
extern AVOutputFormat mpeg1vcd_mux;
extern AVOutputFormat mpeg2vob_mux;

static int put_pack_header(AVFormatContext *ctx, 
                           uint8_t *buf, int64_t timestamp)
{
    MpegMuxContext *s = ctx->priv_data;
    PutBitContext pb;
    
    init_put_bits(&pb, buf, 128, NULL, NULL);

    put_bits(&pb, 32, PACK_START_CODE);
    if (s->is_mpeg2) {
        put_bits(&pb, 2, 0x1);
    } else {
        put_bits(&pb, 4, 0x2);
    }
    put_bits(&pb, 3, (uint32_t)((timestamp >> 30) & 0x07));
    put_bits(&pb, 1, 1);
    put_bits(&pb, 15, (uint32_t)((timestamp >> 15) & 0x7fff));
    put_bits(&pb, 1, 1);
    put_bits(&pb, 15, (uint32_t)((timestamp) & 0x7fff));
    put_bits(&pb, 1, 1);
    if (s->is_mpeg2) {
        /* clock extension */
        put_bits(&pb, 9, 0);
        put_bits(&pb, 1, 1);
    }
    put_bits(&pb, 1, 1);
    put_bits(&pb, 22, s->mux_rate);
    put_bits(&pb, 1, 1);
    if (s->is_mpeg2) {
        put_bits(&pb, 5, 0x1f); /* reserved */
        put_bits(&pb, 3, 0); /* stuffing length */
    }
    flush_put_bits(&pb);
    return pbBufPtr(&pb) - pb.buf;
}

static int put_system_header(AVFormatContext *ctx, uint8_t *buf)
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
    MpegMuxContext *s = ctx->priv_data;
    int bitrate, i, mpa_id, mpv_id, ac3_id;
    AVStream *st;
    StreamInfo *stream;

    s->packet_number = 0;
    s->is_vcd = (ctx->oformat == &mpeg1vcd_mux);
    s->is_mpeg2 = (ctx->oformat == &mpeg2vob_mux);
    
    if (s->is_vcd)
        s->packet_size = 2324; /* VCD packet size */
    else
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
        default:
            av_abort();
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
    
    if (s->is_vcd || s->is_mpeg2)
        /* every packet */
        s->pack_header_freq = 1;
    else
        /* every 2 seconds */
        s->pack_header_freq = 2 * bitrate / s->packet_size / 8;
    
    if (s->is_mpeg2)
        /* every 200 packets. Need to look at the spec.  */
        s->system_header_freq = s->pack_header_freq * 40;
    else if (s->is_vcd)
        /* every 40 packets, this is my invention */
        s->system_header_freq = s->pack_header_freq * 40;
    else
        s->system_header_freq = s->pack_header_freq * 5;
    
    for(i=0;i<ctx->nb_streams;i++) {
        stream = ctx->streams[i]->priv_data;
        stream->buffer_ptr = 0;
        stream->packet_number = 0;
        stream->start_pts = -1;
    }
    return 0;
 fail:
    for(i=0;i<ctx->nb_streams;i++) {
        av_free(ctx->streams[i]->priv_data);
    }
    return -ENOMEM;
}

/* flush the packet on stream stream_index */
static void flush_packet(AVFormatContext *ctx, int stream_index, int last_pkt)
{
    MpegMuxContext *s = ctx->priv_data;
    StreamInfo *stream = ctx->streams[stream_index]->priv_data;
    uint8_t *buf_ptr;
    int size, payload_size, startcode, id, len, stuffing_size, i, header_len;
    int64_t timestamp;
    uint8_t buffer[128];
    int last = last_pkt ? 4 : 0;
    
    id = stream->id;
    timestamp = stream->start_pts;

#if 0
    printf("packet ID=%2x PTS=%0.3f\n", 
           id, timestamp / 90000.0);
#endif

    buf_ptr = buffer;
    if (((s->packet_number % s->pack_header_freq) == 0)) {
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
    if (s->is_mpeg2) {
        header_len = 8;
    } else {
        header_len = 5;
    }
    payload_size = s->packet_size - (size + 6 + header_len + last);
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

    put_be16(&ctx->pb, payload_size + header_len);
    /* stuffing */
    for(i=0;i<stuffing_size;i++)
        put_byte(&ctx->pb, 0xff);

    if (s->is_mpeg2) {
        put_byte(&ctx->pb, 0x80); /* mpeg2 id */
        put_byte(&ctx->pb, 0x80); /* flags */
        put_byte(&ctx->pb, 0x05); /* header len (only pts is included) */
    }
    put_byte(&ctx->pb, 
             (0x02 << 4) | 
             (((timestamp >> 30) & 0x07) << 1) | 
             1);
    put_be16(&ctx->pb, (uint16_t)((((timestamp >> 15) & 0x7fff) << 1) | 1));
    put_be16(&ctx->pb, (uint16_t)((((timestamp) & 0x7fff) << 1) | 1));

    if (startcode == PRIVATE_STREAM_1) {
        put_byte(&ctx->pb, id);
        if (id >= 0x80 && id <= 0xbf) {
            /* XXX: need to check AC3 spec */
            put_byte(&ctx->pb, 1);
            put_byte(&ctx->pb, 0);
            put_byte(&ctx->pb, 2);
        }
    }

    if (last_pkt) {
        put_be32(&ctx->pb, ISO_11172_END_CODE);
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
                                 const uint8_t *buf, int size, int64_t pts)
{
    MpegMuxContext *s = ctx->priv_data;
    AVStream *st = ctx->streams[stream_index];
    StreamInfo *stream = st->priv_data;
    int len;
    
    while (size > 0) {
        /* set pts */
        if (stream->start_pts == -1) {
            stream->start_pts = pts;
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
                stream->start_pts = pts;
            flush_packet(ctx, stream_index, 0);
        }
    }
    return 0;
}

static int mpeg_mux_end(AVFormatContext *ctx)
{
    StreamInfo *stream;
    int i;

    /* flush each packet */
    for(i=0;i<ctx->nb_streams;i++) {
        stream = ctx->streams[i]->priv_data;
        if (stream->buffer_ptr > 0) {
            if (i == (ctx->nb_streams - 1)) 
                flush_packet(ctx, i, 1);
            else
                flush_packet(ctx, i, 0);
        }
    }

    /* write the end header */
    //put_be32(&ctx->pb, ISO_11172_END_CODE);
    //put_flush_packet(&ctx->pb);

    for(i=0;i<ctx->nb_streams;i++)
        av_freep(&ctx->streams[i]->priv_data);

    return 0;
}

/*********************************************/
/* demux code */

#define MAX_SYNC_SIZE 100000

static int mpegps_probe(AVProbeData *p)
{
    int code, c, i;

    code = 0xff;
    /* we search the first start code. If it is a packet start code,
       then we decide it is mpeg ps. We do not send highest value to
       give a chance to mpegts */
    /* NOTE: the search range was restricted to avoid too many false
       detections */

    if (p->buf_size < 6)
        return 0;

    for (i = 0; i < 20; i++) {
        c = p->buf[i];
        code = (code << 8) | c;
        if ((code & 0xffffff00) == 0x100) {
            if (code == PACK_START_CODE ||
                code == SYSTEM_HEADER_START_CODE ||
                (code >= 0x1e0 && code <= 0x1ef) ||
                (code >= 0x1c0 && code <= 0x1df) ||
                code == PRIVATE_STREAM_2 ||
                code == PROGRAM_STREAM_MAP ||
                code == PRIVATE_STREAM_1 ||
                code == PADDING_STREAM)
                return AVPROBE_SCORE_MAX - 2;
            else
                return 0;
        }
    }
    return 0;
}


typedef struct MpegDemuxContext {
    int header_state;
} MpegDemuxContext;

static int find_start_code(ByteIOContext *pb, int *size_ptr, 
                           uint32_t *header_state)
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

static int mpegps_read_header(AVFormatContext *s,
                                  AVFormatParameters *ap)
{
    MpegDemuxContext *m = s->priv_data;
    m->header_state = 0xff;
    /* no need to do more */
    return 0;
}

static int64_t get_pts(ByteIOContext *pb, int c)
{
    int64_t pts;
    int val;

    if (c < 0)
        c = get_byte(pb);
    pts = (int64_t)((c >> 1) & 0x07) << 30;
    val = get_be16(pb);
    pts |= (int64_t)(val >> 1) << 15;
    val = get_be16(pb);
    pts |= (int64_t)(val >> 1);
    return pts;
}

static int mpegps_read_packet(AVFormatContext *s,
                                  AVPacket *pkt)
{
    MpegDemuxContext *m = s->priv_data;
    AVStream *st;
    int len, size, startcode, i, c, flags, header_len, type, codec_id;
    int64_t pts, dts;

    /* next start code (should be immediately after) */
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
    pts = AV_NOPTS_VALUE;
    dts = AV_NOPTS_VALUE;
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
        if ((flags & 0xc0) == 0x80) {
            pts = get_pts(&s->pb, -1);
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
    if (startcode >= 0x1e0 && startcode <= 0x1ef) {
        type = CODEC_TYPE_VIDEO;
        codec_id = CODEC_ID_MPEG1VIDEO;
    } else if (startcode >= 0x1c0 && startcode <= 0x1df) {
        type = CODEC_TYPE_AUDIO;
        codec_id = CODEC_ID_MP2;
    } else if (startcode >= 0x80 && startcode <= 0x9f) {
        type = CODEC_TYPE_AUDIO;
        codec_id = CODEC_ID_AC3;
    } else if (startcode >= 0xa0 && startcode <= 0xbf) {
        type = CODEC_TYPE_AUDIO;
        codec_id = CODEC_ID_PCM_S16BE;
    } else {
    skip:
        /* skip packet */
        url_fskip(&s->pb, len);
        goto redo;
    }
    /* no stream found: add a new stream */
    st = av_new_stream(s, startcode);
    if (!st) 
        goto skip;
    st->codec.codec_type = type;
    st->codec.codec_id = codec_id;
 found:
    if (startcode >= 0xa0 && startcode <= 0xbf) {
        int b1, freq;
        static const int lpcm_freq_tab[4] = { 48000, 96000, 44100, 32000 };

        /* for LPCM, we just skip the header and consider it is raw
           audio data */
        if (len <= 3)
            goto skip;
        get_byte(&s->pb); /* emphasis (1), muse(1), reserved(1), frame number(5) */
        b1 = get_byte(&s->pb); /* quant (2), freq(2), reserved(1), channels(3) */
        get_byte(&s->pb); /* dynamic range control (0x80 = off) */
        len -= 3;
        freq = (b1 >> 4) & 3;
        st->codec.sample_rate = lpcm_freq_tab[freq];
        st->codec.channels = 1 + (b1 & 7);
        st->codec.bit_rate = st->codec.channels * st->codec.sample_rate * 2;
    }
    av_new_packet(pkt, len);
    //printf("\nRead Packet ID: %x PTS: %f Size: %d", startcode,
    //       (float)pts/90000, len);
    get_buffer(&s->pb, pkt->data, pkt->size);
    pkt->pts = pts;
    pkt->stream_index = st->index;
    return 0;
}

static int mpegps_read_close(AVFormatContext *s)
{
    return 0;
}

static AVOutputFormat mpeg1system_mux = {
    "mpeg",
    "MPEG1 System format",
    "video/mpeg",
    "mpg,mpeg",
    sizeof(MpegMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG1VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_packet,
    mpeg_mux_end,
};

static AVOutputFormat mpeg1vcd_mux = {
    "vcd",
    "MPEG1 System format (VCD)",
    "video/mpeg",
    NULL,
    sizeof(MpegMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG1VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_packet,
    mpeg_mux_end,
};

static AVOutputFormat mpeg2vob_mux = {
    "vob",
    "MPEG2 PS format (VOB)",
    "video/mpeg",
    "vob",
    sizeof(MpegMuxContext),
    CODEC_ID_MP2,
    CODEC_ID_MPEG1VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_packet,
    mpeg_mux_end,
};

AVInputFormat mpegps_demux = {
    "mpeg",
    "MPEG PS format",
    sizeof(MpegDemuxContext),
    mpegps_probe,
    mpegps_read_header,
    mpegps_read_packet,
    mpegps_read_close,
    .flags = AVFMT_NOHEADER,
};

int mpegps_init(void)
{
    av_register_output_format(&mpeg1system_mux);
    av_register_output_format(&mpeg1vcd_mux);
    av_register_output_format(&mpeg2vob_mux);
    av_register_input_format(&mpegps_demux);
    return 0;
}
