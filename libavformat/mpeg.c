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
//#define DEBUG_SEEK

typedef struct {
    uint8_t buffer[MAX_PAYLOAD_SIZE];
    int buffer_ptr;
    int nb_frames;    /* number of starting frame encountered (AC3) */
    int frame_start_offset; /* starting offset of the frame + 1 (0 if none) */
    uint8_t id;
    int max_buffer_size; /* in bytes */
    int packet_number;
    int64_t start_pts;
    int64_t start_dts;
    uint8_t lpcm_header[3];
    int lpcm_align;
} StreamInfo;

typedef struct {
    int packet_size; /* required packet size */
    int packet_number;
    int pack_header_freq;     /* frequency (in packets^-1) at which we send pack headers */
    int system_header_freq;
    int system_header_size;
    int mux_rate; /* bitrate in units of 50 bytes/s */
    /* stream info */
    int audio_bound;
    int video_bound;
    int is_mpeg2;
    int is_vcd;
    int scr_stream_index; /* stream from which the system clock is
                             computed (VBR case) */
    int64_t last_scr; /* current system clock */
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
#define AC3_ID   0x80
#define LPCM_ID  0xa0

#ifdef CONFIG_ENCODERS
extern AVOutputFormat mpeg1system_mux;
extern AVOutputFormat mpeg1vcd_mux;
extern AVOutputFormat mpeg2vob_mux;

static const int lpcm_freq_tab[4] = { 48000, 96000, 44100, 32000 };

static int put_pack_header(AVFormatContext *ctx, 
                           uint8_t *buf, int64_t timestamp)
{
    MpegMuxContext *s = ctx->priv_data;
    PutBitContext pb;
    
    init_put_bits(&pb, buf, 128);

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

    init_put_bits(&pb, buf, 128);

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

static int get_system_header_size(AVFormatContext *ctx)
{
    int buf_index, i, private_stream_coded;
    StreamInfo *stream;

    buf_index = 12;
    private_stream_coded = 0;
    for(i=0;i<ctx->nb_streams;i++) {
        stream = ctx->streams[i]->priv_data;
        if (stream->id < 0xc0) {
            if (private_stream_coded)
                continue;
            private_stream_coded = 1;
        }
        buf_index += 3;
    }
    return buf_index;
}

static int mpeg_mux_init(AVFormatContext *ctx)
{
    MpegMuxContext *s = ctx->priv_data;
    int bitrate, i, mpa_id, mpv_id, ac3_id, lpcm_id, j;
    AVStream *st;
    StreamInfo *stream;

    s->packet_number = 0;
    s->is_vcd = (ctx->oformat == &mpeg1vcd_mux);
    s->is_mpeg2 = (ctx->oformat == &mpeg2vob_mux);
    
    if (s->is_vcd)
        s->packet_size = 2324; /* VCD packet size */
    else
        s->packet_size = 2048;
        
    s->audio_bound = 0;
    s->video_bound = 0;
    mpa_id = AUDIO_ID;
    ac3_id = AC3_ID;
    mpv_id = VIDEO_ID;
    lpcm_id = LPCM_ID;
    s->scr_stream_index = -1;
    for(i=0;i<ctx->nb_streams;i++) {
        st = ctx->streams[i];
        stream = av_mallocz(sizeof(StreamInfo));
        if (!stream)
            goto fail;
        st->priv_data = stream;

        switch(st->codec.codec_type) {
        case CODEC_TYPE_AUDIO:
            if (st->codec.codec_id == CODEC_ID_AC3) {
                stream->id = ac3_id++;
            } else if (st->codec.codec_id == CODEC_ID_PCM_S16BE) {
                stream->id = lpcm_id++;
                for(j = 0; j < 4; j++) {
                    if (lpcm_freq_tab[j] == st->codec.sample_rate)
                        break;
                }
                if (j == 4)
                    goto fail;
                if (st->codec.channels > 8)
                    return -1;
                stream->lpcm_header[0] = 0x0c;
                stream->lpcm_header[1] = (st->codec.channels - 1) | (j << 4);
                stream->lpcm_header[2] = 0x80;
                stream->lpcm_align = st->codec.channels * 2;
            } else {
                stream->id = mpa_id++;
            }
            stream->max_buffer_size = 4 * 1024; 
            s->audio_bound++;
            break;
        case CODEC_TYPE_VIDEO:
            /* by default, video is used for the SCR computation */
            if (s->scr_stream_index == -1)
                s->scr_stream_index = i;
            stream->id = mpv_id++;
            stream->max_buffer_size = 46 * 1024; 
            s->video_bound++;
            break;
        default:
            av_abort();
        }
    }
    /* if no SCR, use first stream (audio) */
    if (s->scr_stream_index == -1)
        s->scr_stream_index = 0;

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

    /* the above seems to make pack_header_freq zero sometimes */
    if (s->pack_header_freq == 0)
       s->pack_header_freq = 1;
    
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
        stream->start_pts = AV_NOPTS_VALUE;
        stream->start_dts = AV_NOPTS_VALUE;
    }
    s->system_header_size = get_system_header_size(ctx);
    s->last_scr = 0;
    return 0;
 fail:
    for(i=0;i<ctx->nb_streams;i++) {
        av_free(ctx->streams[i]->priv_data);
    }
    return -ENOMEM;
}

static inline void put_timestamp(ByteIOContext *pb, int id, int64_t timestamp)
{
    put_byte(pb, 
             (id << 4) | 
             (((timestamp >> 30) & 0x07) << 1) | 
             1);
    put_be16(pb, (uint16_t)((((timestamp >> 15) & 0x7fff) << 1) | 1));
    put_be16(pb, (uint16_t)((((timestamp) & 0x7fff) << 1) | 1));
}


/* return the exact available payload size for the next packet for
   stream 'stream_index'. 'pts' and 'dts' are only used to know if
   timestamps are needed in the packet header. */
static int get_packet_payload_size(AVFormatContext *ctx, int stream_index,
                                   int64_t pts, int64_t dts)
{
    MpegMuxContext *s = ctx->priv_data;
    int buf_index;
    StreamInfo *stream;

    buf_index = 0;
    if (((s->packet_number % s->pack_header_freq) == 0)) {
        /* pack header size */
        if (s->is_mpeg2) 
            buf_index += 14;
        else
            buf_index += 12;
        if ((s->packet_number % s->system_header_freq) == 0)
            buf_index += s->system_header_size;
    }

    /* packet header size */
    buf_index += 6;
    if (s->is_mpeg2)
        buf_index += 3;
    if (pts != AV_NOPTS_VALUE) {
        if (dts != AV_NOPTS_VALUE)
            buf_index += 5 + 5;
        else
            buf_index += 5;
    } else {
        if (!s->is_mpeg2)
            buf_index++;
    }
    
    stream = ctx->streams[stream_index]->priv_data;
    if (stream->id < 0xc0) {
        /* AC3/LPCM private data header */
        buf_index += 4;
        if (stream->id >= 0xa0) {
            int n;
            buf_index += 3;
            /* NOTE: we round the payload size to an integer number of
               LPCM samples */
            n = (s->packet_size - buf_index) % stream->lpcm_align;
            if (n)
                buf_index += (stream->lpcm_align - n);
        }
    }
    return s->packet_size - buf_index; 
}

/* flush the packet on stream stream_index */
static void flush_packet(AVFormatContext *ctx, int stream_index, 
                         int64_t pts, int64_t dts, int64_t scr)
{
    MpegMuxContext *s = ctx->priv_data;
    StreamInfo *stream = ctx->streams[stream_index]->priv_data;
    uint8_t *buf_ptr;
    int size, payload_size, startcode, id, stuffing_size, i, header_len;
    int packet_size;
    uint8_t buffer[128];
    
    id = stream->id;
    
#if 0
    printf("packet ID=%2x PTS=%0.3f\n", 
           id, pts / 90000.0);
#endif

    buf_ptr = buffer;
    if (((s->packet_number % s->pack_header_freq) == 0)) {
        /* output pack and systems header if needed */
        size = put_pack_header(ctx, buf_ptr, scr);
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
        header_len = 3;
    } else {
        header_len = 0;
    }
    if (pts != AV_NOPTS_VALUE) {
        if (dts != AV_NOPTS_VALUE)
            header_len += 5 + 5;
        else
            header_len += 5;
    } else {
        if (!s->is_mpeg2)
            header_len++;
    }

    packet_size = s->packet_size - (size + 6);
    payload_size = packet_size - header_len;
    if (id < 0xc0) {
        startcode = PRIVATE_STREAM_1;
        payload_size -= 4;
        if (id >= 0xa0)
            payload_size -= 3;
    } else {
        startcode = 0x100 + id;
    }

    stuffing_size = payload_size - stream->buffer_ptr;
    if (stuffing_size < 0)
        stuffing_size = 0;
    put_be32(&ctx->pb, startcode);

    put_be16(&ctx->pb, packet_size);
    /* stuffing */
    for(i=0;i<stuffing_size;i++)
        put_byte(&ctx->pb, 0xff);

    if (s->is_mpeg2) {
        put_byte(&ctx->pb, 0x80); /* mpeg2 id */

        if (pts != AV_NOPTS_VALUE) {
            if (dts != AV_NOPTS_VALUE) {
                put_byte(&ctx->pb, 0xc0); /* flags */
                put_byte(&ctx->pb, header_len - 3);
                put_timestamp(&ctx->pb, 0x03, pts);
                put_timestamp(&ctx->pb, 0x01, dts);
            } else {
                put_byte(&ctx->pb, 0x80); /* flags */
                put_byte(&ctx->pb, header_len - 3);
                put_timestamp(&ctx->pb, 0x02, pts);
            }
        } else {
            put_byte(&ctx->pb, 0x00); /* flags */
            put_byte(&ctx->pb, header_len - 3);
        }
    } else {
        if (pts != AV_NOPTS_VALUE) {
            if (dts != AV_NOPTS_VALUE) {
                put_timestamp(&ctx->pb, 0x03, pts);
                put_timestamp(&ctx->pb, 0x01, dts);
            } else {
                put_timestamp(&ctx->pb, 0x02, pts);
            }
        } else {
            put_byte(&ctx->pb, 0x0f);
        }
    }

    if (startcode == PRIVATE_STREAM_1) {
        put_byte(&ctx->pb, id);
        if (id >= 0xa0) {
            /* LPCM (XXX: check nb_frames) */
            put_byte(&ctx->pb, 7);
            put_be16(&ctx->pb, 4); /* skip 3 header bytes */
            put_byte(&ctx->pb, stream->lpcm_header[0]);
            put_byte(&ctx->pb, stream->lpcm_header[1]);
            put_byte(&ctx->pb, stream->lpcm_header[2]);
        } else {
            /* AC3 */
            put_byte(&ctx->pb, stream->nb_frames);
            put_be16(&ctx->pb, stream->frame_start_offset);
        }
    }

    /* output data */
    put_buffer(&ctx->pb, stream->buffer, payload_size - stuffing_size);
    put_flush_packet(&ctx->pb);
    
    s->packet_number++;
    stream->packet_number++;
    stream->nb_frames = 0;
    stream->frame_start_offset = 0;
}

static int mpeg_mux_write_packet(AVFormatContext *ctx, int stream_index,
                                 const uint8_t *buf, int size, int64_t pts)
{
    MpegMuxContext *s = ctx->priv_data;
    AVStream *st = ctx->streams[stream_index];
    StreamInfo *stream = st->priv_data;
    int64_t dts, new_start_pts, new_start_dts;
    int len, avail_size;

    /* XXX: system clock should be computed precisely, especially for
       CBR case. The current mode gives at least something coherent */
    if (stream_index == s->scr_stream_index)
        s->last_scr = pts;
    
#if 0
    printf("%d: pts=%0.3f scr=%0.3f\n", 
           stream_index, pts / 90000.0, s->last_scr / 90000.0);
#endif
    
    /* XXX: currently no way to pass dts, will change soon */
    dts = AV_NOPTS_VALUE;

    /* we assume here that pts != AV_NOPTS_VALUE */
    new_start_pts = stream->start_pts;
    new_start_dts = stream->start_dts;
    
    if (stream->start_pts == AV_NOPTS_VALUE) {
        new_start_pts = pts;
        new_start_dts = dts;
    }
    avail_size = get_packet_payload_size(ctx, stream_index,
                                         new_start_pts, 
                                         new_start_dts);
    if (stream->buffer_ptr >= avail_size) {
        /* unlikely case: outputing the pts or dts increase the packet
           size so that we cannot write the start of the next
           packet. In this case, we must flush the current packet with
           padding */
        flush_packet(ctx, stream_index,
                     stream->start_pts, stream->start_dts, s->last_scr);
        stream->buffer_ptr = 0;
    }
    stream->start_pts = new_start_pts;
    stream->start_dts = new_start_dts;
    stream->nb_frames++;
    if (stream->frame_start_offset == 0)
        stream->frame_start_offset = stream->buffer_ptr;
    while (size > 0) {
        avail_size = get_packet_payload_size(ctx, stream_index,
                                             stream->start_pts, 
                                             stream->start_dts);
        len = avail_size - stream->buffer_ptr;
        if (len > size)
            len = size;
        memcpy(stream->buffer + stream->buffer_ptr, buf, len);
        stream->buffer_ptr += len;
        buf += len;
        size -= len;
        if (stream->buffer_ptr >= avail_size) {
            /* if packet full, we send it now */
            flush_packet(ctx, stream_index,
                         stream->start_pts, stream->start_dts, s->last_scr);
            stream->buffer_ptr = 0;
            /* Make sure only the FIRST pes packet for this frame has
               a timestamp */
            stream->start_pts = AV_NOPTS_VALUE;
            stream->start_dts = AV_NOPTS_VALUE;
        }
    }

    return 0;
}

static int mpeg_mux_end(AVFormatContext *ctx)
{
    MpegMuxContext *s = ctx->priv_data;
    StreamInfo *stream;
    int i;

    /* flush each packet */
    for(i=0;i<ctx->nb_streams;i++) {
        stream = ctx->streams[i]->priv_data;
        if (stream->buffer_ptr > 0) {
            /* NOTE: we can always write the remaining data as it was
               tested before in mpeg_mux_write_packet() */
            flush_packet(ctx, i, stream->start_pts, stream->start_dts, 
                         s->last_scr);
        }
    }

    /* End header according to MPEG1 systems standard. We do not write
       it as it is usually not needed by decoders and because it
       complicates MPEG stream concatenation. */
    //put_be32(&ctx->pb, ISO_11172_END_CODE);
    //put_flush_packet(&ctx->pb);

    for(i=0;i<ctx->nb_streams;i++)
        av_freep(&ctx->streams[i]->priv_data);

    return 0;
}
#endif //CONFIG_ENCODERS

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

static int mpegps_read_header(AVFormatContext *s,
                              AVFormatParameters *ap)
{
    MpegDemuxContext *m = s->priv_data;
    m->header_state = 0xff;
    s->ctx_flags |= AVFMTCTX_NOHEADER;

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

static int find_next_start_code(ByteIOContext *pb, int *size_ptr, 
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

/* XXX: optimize */
static int find_prev_start_code(ByteIOContext *pb, int *size_ptr)
{
    int64_t pos, pos_start;
    int max_size, start_code;

    max_size = *size_ptr;
    pos_start = url_ftell(pb);

    /* in order to go faster, we fill the buffer */
    pos = pos_start - 16386;
    if (pos < 0)
        pos = 0;
    url_fseek(pb, pos, SEEK_SET);
    get_byte(pb);

    pos = pos_start;
    for(;;) {
        pos--;
        if (pos < 0 || (pos_start - pos) >= max_size) {
            start_code = -1;
            goto the_end;
        }
        url_fseek(pb, pos, SEEK_SET);
        start_code = get_be32(pb);
        if ((start_code & 0xffffff00) == 0x100)
            break;
    }
 the_end:
    *size_ptr = pos_start - pos;
    return start_code;
}

/* read the next (or previous) PES header. Return its position in ppos 
   (if not NULL), and its start code, pts and dts.
 */
static int mpegps_read_pes_header(AVFormatContext *s,
                                  int64_t *ppos, int *pstart_code, 
                                  int64_t *ppts, int64_t *pdts, int find_next)
{
    MpegDemuxContext *m = s->priv_data;
    int len, size, startcode, c, flags, header_len;
    int64_t pts, dts, last_pos;

    last_pos = -1;
 redo:
    if (find_next) {
        /* next start code (should be immediately after) */
        m->header_state = 0xff;
        size = MAX_SYNC_SIZE;
        startcode = find_next_start_code(&s->pb, &size, &m->header_state);
    } else {
        if (last_pos >= 0)
            url_fseek(&s->pb, last_pos, SEEK_SET);
        size = MAX_SYNC_SIZE;
        startcode = find_prev_start_code(&s->pb, &size);
        last_pos = url_ftell(&s->pb) - 4;
    }
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
    if (ppos) {
        *ppos = url_ftell(&s->pb) - 4;
    }
    len = get_be16(&s->pb);
    pts = AV_NOPTS_VALUE;
    dts = AV_NOPTS_VALUE;
    /* stuffing */
    for(;;) {
        if (len < 1)
            goto redo;
        c = get_byte(&s->pb);
        len--;
        /* XXX: for mpeg1, should test only bit 7 */
        if (c != 0xff) 
            break;
    }
    if ((c & 0xc0) == 0x40) {
        /* buffer scale & size */
        if (len < 2)
            goto redo;
        get_byte(&s->pb);
        c = get_byte(&s->pb);
        len -= 2;
    }
    if ((c & 0xf0) == 0x20) {
        if (len < 4)
            goto redo;
        dts = pts = get_pts(&s->pb, c);
        len -= 4;
    } else if ((c & 0xf0) == 0x30) {
        if (len < 9)
            goto redo;
        pts = get_pts(&s->pb, c);
        dts = get_pts(&s->pb, -1);
        len -= 9;
    } else if ((c & 0xc0) == 0x80) {
        /* mpeg 2 PES */
        if ((c & 0x30) != 0) {
            /* Encrypted multiplex not handled */
            goto redo;
        }
        flags = get_byte(&s->pb);
        header_len = get_byte(&s->pb);
        len -= 2;
        if (header_len > len)
            goto redo;
        if ((flags & 0xc0) == 0x80) {
            dts = pts = get_pts(&s->pb, -1);
            if (header_len < 5)
                goto redo;
            header_len -= 5;
            len -= 5;
        } if ((flags & 0xc0) == 0xc0) {
            pts = get_pts(&s->pb, -1);
            dts = get_pts(&s->pb, -1);
            if (header_len < 10)
                goto redo;
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
        if (len < 1)
            goto redo;
        startcode = get_byte(&s->pb);
        len--;
        if (startcode >= 0x80 && startcode <= 0xbf) {
            /* audio: skip header */
            if (len < 3)
                goto redo;
            get_byte(&s->pb);
            get_byte(&s->pb);
            get_byte(&s->pb);
            len -= 3;
        }
    }
    *pstart_code = startcode;
    *ppts = pts;
    *pdts = dts;
    return len;
}

static int mpegps_read_packet(AVFormatContext *s,
                              AVPacket *pkt)
{
    AVStream *st;
    int len, startcode, i, type, codec_id;
    int64_t pts, dts;

 redo:
    len = mpegps_read_pes_header(s, NULL, &startcode, &pts, &dts, 1);
    if (len < 0)
        return len;
    
    /* now find stream */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        if (st->id == startcode)
            goto found;
    }
    if (startcode >= 0x1e0 && startcode <= 0x1ef) {
        type = CODEC_TYPE_VIDEO;
        codec_id = CODEC_ID_MPEG2VIDEO;
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
    if (codec_id != CODEC_ID_PCM_S16BE)
        st->need_parsing = 1;
 found:
    if (startcode >= 0xa0 && startcode <= 0xbf) {
        int b1, freq;

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
    get_buffer(&s->pb, pkt->data, pkt->size);
    pkt->pts = pts;
    pkt->dts = dts;
    pkt->stream_index = st->index;
#if 0
    printf("%d: pts=%0.3f dts=%0.3f\n",
           pkt->stream_index, pkt->pts / 90000.0, pkt->dts / 90000.0);
#endif
    return 0;
}

static int mpegps_read_close(AVFormatContext *s)
{
    return 0;
}

static int64_t mpegps_read_dts(AVFormatContext *s, int stream_index, 
                               int64_t *ppos, int find_next)
{
    int len, startcode;
    int64_t pos, pts, dts;

    pos = *ppos;
#ifdef DEBUG_SEEK
    printf("read_dts: pos=0x%llx next=%d -> ", pos, find_next);
#endif
    url_fseek(&s->pb, pos, SEEK_SET);
    for(;;) {
        len = mpegps_read_pes_header(s, &pos, &startcode, &pts, &dts, find_next);
        if (len < 0) {
#ifdef DEBUG_SEEK
            printf("none (ret=%d)\n", len);
#endif
            return AV_NOPTS_VALUE;
        }
        if (startcode == s->streams[stream_index]->id && 
            dts != AV_NOPTS_VALUE) {
            break;
        }
        if (find_next) {
            url_fskip(&s->pb, len);
        } else {
            url_fseek(&s->pb, pos, SEEK_SET);
        }
    }
#ifdef DEBUG_SEEK
    printf("pos=0x%llx dts=0x%llx %0.3f\n", pos, dts, dts / 90000.0);
#endif
    *ppos = pos;
    return dts;
}

static int find_stream_index(AVFormatContext *s)
{
    int i;
    AVStream *st;

    if (s->nb_streams <= 0)
        return -1;
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        if (st->codec.codec_type == CODEC_TYPE_VIDEO) {
            return i;
        }
    }
    return 0;
}

static int mpegps_read_seek(AVFormatContext *s, 
                            int stream_index, int64_t timestamp)
{
    int64_t pos_min, pos_max, pos;
    int64_t dts_min, dts_max, dts;

    timestamp = (timestamp * 90000) / AV_TIME_BASE;

#ifdef DEBUG_SEEK
    printf("read_seek: %d %0.3f\n", stream_index, timestamp / 90000.0);
#endif

    /* XXX: find stream_index by looking at the first PES packet found */
    if (stream_index < 0) {
        stream_index = find_stream_index(s);
        if (stream_index < 0)
            return -1;
    }
    pos_min = 0;
    dts_min = mpegps_read_dts(s, stream_index, &pos_min, 1);
    if (dts_min == AV_NOPTS_VALUE) {
        /* we can reach this case only if no PTS are present in
           the whole stream */
        return -1;
    }
    pos_max = url_filesize(url_fileno(&s->pb)) - 1;
    dts_max = mpegps_read_dts(s, stream_index, &pos_max, 0);
    
    while (pos_min <= pos_max) {
#ifdef DEBUG_SEEK
        printf("pos_min=0x%llx pos_max=0x%llx dts_min=%0.3f dts_max=%0.3f\n", 
               pos_min, pos_max,
               dts_min / 90000.0, dts_max / 90000.0);
#endif
        if (timestamp <= dts_min) {
            pos = pos_min;
            goto found;
        } else if (timestamp >= dts_max) {
            pos = pos_max;
            goto found;
        } else {
            /* interpolate position (better than dichotomy) */
            pos = (int64_t)((double)(pos_max - pos_min) * 
                            (double)(timestamp - dts_min) /
                            (double)(dts_max - dts_min)) + pos_min;
        }
#ifdef DEBUG_SEEK
        printf("pos=0x%llx\n", pos);
#endif
        /* read the next timestamp */
        dts = mpegps_read_dts(s, stream_index, &pos, 1);
        /* check if we are lucky */
        if (dts == AV_NOPTS_VALUE) {
            /* should never happen */
            pos = pos_min;
            goto found;
        } else if (timestamp == dts) {
            goto found;
        } else if (timestamp < dts) {
            pos_max = pos;
            dts_max = mpegps_read_dts(s, stream_index, &pos_max, 0);
            if (dts_max == AV_NOPTS_VALUE) {
                /* should never happen */
                break;
            } else if (timestamp >= dts_max) {
                pos = pos_max;
                goto found;
            }
        } else {
            pos_min = pos + 1;
            dts_min = mpegps_read_dts(s, stream_index, &pos_min, 1);
            if (dts_min == AV_NOPTS_VALUE) {
                /* should never happen */
                goto found;
            } else if (timestamp <= dts_min) {
                goto found;
            }
        }
    }
    pos = pos_min;
 found:
#ifdef DEBUG_SEEK
    pos_min = pos;
    dts_min = mpegps_read_dts(s, stream_index, &pos_min, 1);
    pos_min++;
    dts_max = mpegps_read_dts(s, stream_index, &pos_min, 1);
    printf("pos=0x%llx %0.3f<=%0.3f<=%0.3f\n", 
           pos, dts_min / 90000.0, timestamp / 90000.0, dts_max / 90000.0);
#endif
    /* do the seek */
    url_fseek(&s->pb, pos, SEEK_SET);
    return 0;
}

#ifdef CONFIG_ENCODERS
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
    CODEC_ID_MPEG2VIDEO,
    mpeg_mux_init,
    mpeg_mux_write_packet,
    mpeg_mux_end,
};
#endif //CONFIG_ENCODERS

AVInputFormat mpegps_demux = {
    "mpeg",
    "MPEG PS format",
    sizeof(MpegDemuxContext),
    mpegps_probe,
    mpegps_read_header,
    mpegps_read_packet,
    mpegps_read_close,
    mpegps_read_seek,
};

int mpegps_init(void)
{
#ifdef CONFIG_ENCODERS
    av_register_output_format(&mpeg1system_mux);
    av_register_output_format(&mpeg1vcd_mux);
    av_register_output_format(&mpeg2vob_mux);
#endif //CONFIG_ENCODERS
    av_register_input_format(&mpegps_demux);
    return 0;
}
