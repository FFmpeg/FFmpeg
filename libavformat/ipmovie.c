/*
 * Interplay MVE File Demuxer
 * Copyright (c) 2003 The ffmpeg Project
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

/**
 * @file ipmovie.c
 * Interplay MVE file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * For more information regarding the Interplay MVE file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 * The aforementioned site also contains a command line utility for parsing
 * IP MVE files so that you can get a good idea of the typical structure of
 * such files. This demuxer is not the best example to use if you are trying
 * to write your own as it uses a rather roundabout approach for splitting
 * up and sending out the chunks.
 */

#include "avformat.h"

/* debugging support: #define DEBUG_IPMOVIE as non-zero to see extremely
 * verbose information about the demux process */
#define DEBUG_IPMOVIE 0

#if DEBUG_IPMOVIE
#define debug_ipmovie printf
#else
static inline void debug_ipmovie(const char *format, ...) { }
#endif

#define IPMOVIE_SIGNATURE "Interplay MVE File\x1A\0"
#define IPMOVIE_SIGNATURE_SIZE 20
#define CHUNK_PREAMBLE_SIZE 4
#define OPCODE_PREAMBLE_SIZE 4

#define CHUNK_INIT_AUDIO   0x0000
#define CHUNK_AUDIO_ONLY   0x0001
#define CHUNK_INIT_VIDEO   0x0002
#define CHUNK_VIDEO        0x0003
#define CHUNK_SHUTDOWN     0x0004
#define CHUNK_END          0x0005
/* these last types are used internally */
#define CHUNK_DONE         0xFFFC
#define CHUNK_NOMEM        0xFFFD
#define CHUNK_EOF          0xFFFE
#define CHUNK_BAD          0xFFFF

#define OPCODE_END_OF_STREAM           0x00
#define OPCODE_END_OF_CHUNK            0x01
#define OPCODE_CREATE_TIMER            0x02
#define OPCODE_INIT_AUDIO_BUFFERS      0x03
#define OPCODE_START_STOP_AUDIO        0x04
#define OPCODE_INIT_VIDEO_BUFFERS      0x05
#define OPCODE_UNKNOWN_06              0x06
#define OPCODE_SEND_BUFFER             0x07
#define OPCODE_AUDIO_FRAME             0x08
#define OPCODE_SILENCE_FRAME           0x09
#define OPCODE_INIT_VIDEO_MODE         0x0A
#define OPCODE_CREATE_GRADIENT         0x0B
#define OPCODE_SET_PALETTE             0x0C
#define OPCODE_SET_PALETTE_COMPRESSED  0x0D
#define OPCODE_UNKNOWN_0E              0x0E
#define OPCODE_SET_DECODING_MAP        0x0F
#define OPCODE_UNKNOWN_10              0x10
#define OPCODE_VIDEO_DATA              0x11
#define OPCODE_UNKNOWN_12              0x12
#define OPCODE_UNKNOWN_13              0x13
#define OPCODE_UNKNOWN_14              0x14
#define OPCODE_UNKNOWN_15              0x15

#define PALETTE_COUNT 256

typedef struct IPMVEContext {

    unsigned char *buf;
    int buf_size;

    float fps;
    int frame_pts_inc;

    unsigned int video_width;
    unsigned int video_height;
    int64_t video_pts;

    unsigned int audio_bits;
    unsigned int audio_channels;
    unsigned int audio_sample_rate;
    unsigned int audio_type;
    unsigned int audio_frame_count;

    int video_stream_index;
    int audio_stream_index;

    offset_t audio_chunk_offset;
    int audio_chunk_size;
    offset_t video_chunk_offset;
    int video_chunk_size;
    offset_t decode_map_chunk_offset;
    int decode_map_chunk_size;

    offset_t next_chunk_offset;

    AVPaletteControl palette_control;

} IPMVEContext;

static int load_ipmovie_packet(IPMVEContext *s, ByteIOContext *pb, 
    AVPacket *pkt) {

    int chunk_type;
    int64_t audio_pts = 0;

    if (s->audio_chunk_offset) {

        /* adjust for PCM audio by skipping chunk header */
        if (s->audio_type != CODEC_ID_INTERPLAY_DPCM) {
            s->audio_chunk_offset += 6;
            s->audio_chunk_size -= 6;
        }

        url_fseek(pb, s->audio_chunk_offset, SEEK_SET);
        s->audio_chunk_offset = 0;

        /* figure out the audio pts */
        audio_pts = 90000;
        audio_pts *= s->audio_frame_count;
        audio_pts /= s->audio_sample_rate;

        if (av_new_packet(pkt, s->audio_chunk_size))
            return CHUNK_NOMEM;

        pkt->stream_index = s->audio_stream_index;
        pkt->pts = audio_pts;
        if (get_buffer(pb, pkt->data, s->audio_chunk_size) != 
            s->audio_chunk_size) {
            av_free_packet(pkt);
            return CHUNK_EOF;
        }

        /* audio frame maintenance */
        if (s->audio_type != CODEC_ID_INTERPLAY_DPCM)
            s->audio_frame_count +=
            (s->audio_chunk_size / s->audio_channels / (s->audio_bits / 8));
        else
            s->audio_frame_count +=
                (s->audio_chunk_size - 6) / s->audio_channels;

        debug_ipmovie("sending audio frame with pts %lld (%d audio frames)\n",
            audio_pts, s->audio_frame_count);

        chunk_type = CHUNK_VIDEO;

    } else if (s->decode_map_chunk_offset) {

        /* send both the decode map and the video data together */

        if (av_new_packet(pkt, s->decode_map_chunk_size + s->video_chunk_size))
            return CHUNK_NOMEM;

        url_fseek(pb, s->decode_map_chunk_offset, SEEK_SET);
        s->decode_map_chunk_offset = 0;

        if (get_buffer(pb, pkt->data, s->decode_map_chunk_size) != 
            s->decode_map_chunk_size) {
            av_free_packet(pkt);
            return CHUNK_EOF;
        }

        url_fseek(pb, s->video_chunk_offset, SEEK_SET);
        s->video_chunk_offset = 0;

        if (get_buffer(pb, pkt->data + s->decode_map_chunk_size,
            s->video_chunk_size) != s->video_chunk_size) {
            av_free_packet(pkt);
            return CHUNK_EOF;
        }

        pkt->stream_index = s->video_stream_index;
        pkt->pts = s->video_pts;

        debug_ipmovie("sending video frame with pts %lld\n",
            pkt->pts);

        s->video_pts += s->frame_pts_inc;

        chunk_type = CHUNK_VIDEO;

    } else {

        url_fseek(pb, s->next_chunk_offset, SEEK_SET);
        chunk_type = CHUNK_DONE;

    }

    return chunk_type;
}

/* This function loads and processes a single chunk in an IP movie file.
 * It returns the type of chunk that was processed. */
static int process_ipmovie_chunk(IPMVEContext *s, ByteIOContext *pb, 
    AVPacket *pkt)
{
    unsigned char chunk_preamble[CHUNK_PREAMBLE_SIZE];
    int chunk_type;
    int chunk_size;
    unsigned char opcode_preamble[OPCODE_PREAMBLE_SIZE];
    unsigned char opcode_type;
    unsigned char opcode_version;
    int opcode_size;
    unsigned char scratch[1024];
    int i, j;
    int first_color, last_color;
    int audio_flags;
    unsigned char r, g, b;

    /* see if there are any pending packets */
    chunk_type = load_ipmovie_packet(s, pb, pkt);
    if ((chunk_type == CHUNK_VIDEO) && (chunk_type != CHUNK_DONE))
        return chunk_type;

    /* read the next chunk, wherever the file happens to be pointing */
    if (url_feof(pb))
        return CHUNK_EOF;
    if (get_buffer(pb, chunk_preamble, CHUNK_PREAMBLE_SIZE) !=
        CHUNK_PREAMBLE_SIZE)
        return CHUNK_BAD;
    chunk_size = LE_16(&chunk_preamble[0]);
    chunk_type = LE_16(&chunk_preamble[2]);

    debug_ipmovie("chunk type 0x%04X, 0x%04X bytes: ", chunk_type, chunk_size);

    switch (chunk_type) {

    case CHUNK_INIT_AUDIO:
        debug_ipmovie("initialize audio\n");
        break;

    case CHUNK_AUDIO_ONLY:
        debug_ipmovie("audio only\n");
        break;

    case CHUNK_INIT_VIDEO:
        debug_ipmovie("initialize video\n");
        break;

    case CHUNK_VIDEO:
        debug_ipmovie("video (and audio)\n");
        break;

    case CHUNK_SHUTDOWN:
        debug_ipmovie("shutdown\n");
        break;

    case CHUNK_END:
        debug_ipmovie("end\n");
        break;

    default:
        debug_ipmovie("invalid chunk\n");
        chunk_type = CHUNK_BAD;
        break;

    }

    while ((chunk_size > 0) && (chunk_type != CHUNK_BAD)) {

        /* read the next chunk, wherever the file happens to be pointing */
       if (url_feof(pb)) {
            chunk_type = CHUNK_EOF;
            break;
        }
        if (get_buffer(pb, opcode_preamble, CHUNK_PREAMBLE_SIZE) !=
            CHUNK_PREAMBLE_SIZE) {
            chunk_type = CHUNK_BAD;
            break;
        }

        opcode_size = LE_16(&opcode_preamble[0]);
        opcode_type = opcode_preamble[2];
        opcode_version = opcode_preamble[3];

        chunk_size -= OPCODE_PREAMBLE_SIZE;
        chunk_size -= opcode_size;
        if (chunk_size < 0) {
            debug_ipmovie("chunk_size countdown just went negative\n");
            chunk_type = CHUNK_BAD;
            break;
        }

        debug_ipmovie("  opcode type %02X, version %d, 0x%04X bytes: ",
            opcode_type, opcode_version, opcode_size);
        switch (opcode_type) {

        case OPCODE_END_OF_STREAM:
            debug_ipmovie("end of stream\n");
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_END_OF_CHUNK:
            debug_ipmovie("end of chunk\n");
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_CREATE_TIMER:
            debug_ipmovie("create timer\n");
            if ((opcode_version > 0) || (opcode_size > 6)) {
                debug_ipmovie("bad create_timer opcode\n");
                chunk_type = CHUNK_BAD;
                break;
            }
            if (get_buffer(pb, scratch, opcode_size) !=
                opcode_size) {
                chunk_type = CHUNK_BAD;
                break;
            }
            s->fps = 1000000.0 / (LE_32(&scratch[0]) * LE_16(&scratch[4]));
            s->frame_pts_inc = 90000 / s->fps;
            debug_ipmovie("  %.2f frames/second (timer div = %d, subdiv = %d)\n",
                s->fps, LE_32(&scratch[0]), LE_16(&scratch[4]));
            break;

        case OPCODE_INIT_AUDIO_BUFFERS:
            debug_ipmovie("initialize audio buffers\n");
            if ((opcode_version > 1) || (opcode_size > 10)) {
                debug_ipmovie("bad init_audio_buffers opcode\n");
                chunk_type = CHUNK_BAD;
                break;
            }
            if (get_buffer(pb, scratch, opcode_size) !=
                opcode_size) {
                chunk_type = CHUNK_BAD;
                break;
            }
            s->audio_sample_rate = LE_16(&scratch[4]);
            audio_flags = LE_16(&scratch[2]);
            /* bit 0 of the flags: 0 = mono, 1 = stereo */
            s->audio_channels = (audio_flags & 1) + 1;
            /* bit 1 of the flags: 0 = 8 bit, 1 = 16 bit */
            s->audio_bits = (((audio_flags >> 1) & 1) + 1) * 8;
            /* bit 2 indicates compressed audio in version 1 opcode */
            if ((opcode_version == 1) && (audio_flags & 0x4))
                s->audio_type = CODEC_ID_INTERPLAY_DPCM;
            else if (s->audio_bits == 16)
                s->audio_type = CODEC_ID_PCM_S16LE;
            else
                s->audio_type = CODEC_ID_PCM_U8;
            debug_ipmovie("audio: %d bits, %d Hz, %s, %s format\n",
                s->audio_bits,
                s->audio_sample_rate,
                (s->audio_channels == 2) ? "stereo" : "mono",
                (s->audio_type == CODEC_ID_INTERPLAY_DPCM) ? 
                "Interplay audio" : "PCM");
            break;

        case OPCODE_START_STOP_AUDIO:
            debug_ipmovie("start/stop audio\n");
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_INIT_VIDEO_BUFFERS:
            debug_ipmovie("initialize video buffers\n");
            if ((opcode_version > 2) || (opcode_size > 8)) {
                debug_ipmovie("bad init_video_buffers opcode\n");
                chunk_type = CHUNK_BAD;
                break;
            }
            if (get_buffer(pb, scratch, opcode_size) !=
                opcode_size) {
                chunk_type = CHUNK_BAD;
                break;
            }
            s->video_width = LE_16(&scratch[0]) * 8;
            s->video_height = LE_16(&scratch[2]) * 8;
            debug_ipmovie("video resolution: %d x %d\n",
                s->video_width, s->video_height);
            break;

        case OPCODE_UNKNOWN_06:
        case OPCODE_UNKNOWN_0E:
        case OPCODE_UNKNOWN_10:
        case OPCODE_UNKNOWN_12:
        case OPCODE_UNKNOWN_13:
        case OPCODE_UNKNOWN_14:
        case OPCODE_UNKNOWN_15:
            debug_ipmovie("unknown (but documented) opcode %02X\n", opcode_type);
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_SEND_BUFFER:
            debug_ipmovie("send buffer\n");
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_AUDIO_FRAME:
            debug_ipmovie("audio frame\n");

            /* log position and move on for now */
            s->audio_chunk_offset = url_ftell(pb);
            s->audio_chunk_size = opcode_size;
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_SILENCE_FRAME:
            debug_ipmovie("silence frame\n");
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_INIT_VIDEO_MODE:
            debug_ipmovie("initialize video mode\n");
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_CREATE_GRADIENT:
            debug_ipmovie("create gradient\n");
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_SET_PALETTE:
            debug_ipmovie("set palette\n");
            /* check for the logical maximum palette size
             * (3 * 256 + 4 bytes) */
            if (opcode_size > 0x304) {
                debug_ipmovie("demux_ipmovie: set_palette opcode too large\n");
                chunk_type = CHUNK_BAD;
                break;
            }
            if (get_buffer(pb, scratch, opcode_size) != opcode_size) {
                chunk_type = CHUNK_BAD;
                break;
            }

            /* load the palette into internal data structure */
            first_color = LE_16(&scratch[0]);
            last_color = first_color + LE_16(&scratch[2]) - 1;
            /* sanity check (since they are 16 bit values) */
            if ((first_color > 0xFF) || (last_color > 0xFF)) {
                debug_ipmovie("demux_ipmovie: set_palette indices out of range (%d -> %d)\n",
                    first_color, last_color);
                chunk_type = CHUNK_BAD;
                break;
            }
            j = 4;  /* offset of first palette data */
            for (i = first_color; i <= last_color; i++) {
                /* the palette is stored as a 6-bit VGA palette, thus each
                 * component is shifted up to a 8-bit range */
                r = scratch[j++] * 4;
                g = scratch[j++] * 4;
                b = scratch[j++] * 4;
                s->palette_control.palette[i] = (r << 16) | (g << 8) | (b);
            }
            /* indicate a palette change */
            s->palette_control.palette_changed = 1;
            break;

        case OPCODE_SET_PALETTE_COMPRESSED:
            debug_ipmovie("set palette compressed\n");
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_SET_DECODING_MAP:
            debug_ipmovie("set decoding map\n");

            /* log position and move on for now */
            s->decode_map_chunk_offset = url_ftell(pb);
            s->decode_map_chunk_size = opcode_size;
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        case OPCODE_VIDEO_DATA:
            debug_ipmovie("set video data\n");

            /* log position and move on for now */
            s->video_chunk_offset = url_ftell(pb);
            s->video_chunk_size = opcode_size;
            url_fseek(pb, opcode_size, SEEK_CUR);
            break;

        default:
            debug_ipmovie("*** unknown opcode type\n");
            chunk_type = CHUNK_BAD;
            break;

        }
    }

    /* make a note of where the stream is sitting */
    s->next_chunk_offset = url_ftell(pb);

    /* dispatch the first of any pending packets */
    if ((chunk_type == CHUNK_VIDEO) || (chunk_type == CHUNK_AUDIO_ONLY))
        chunk_type = load_ipmovie_packet(s, pb, pkt);

    return chunk_type;
}

static int ipmovie_probe(AVProbeData *p)
{
    if (p->buf_size < IPMOVIE_SIGNATURE_SIZE)
        return 0;
    if (strncmp(p->buf, IPMOVIE_SIGNATURE, IPMOVIE_SIGNATURE_SIZE) != 0)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int ipmovie_read_header(AVFormatContext *s,
                               AVFormatParameters *ap)
{
    IPMVEContext *ipmovie = (IPMVEContext *)s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVPacket pkt;
    AVStream *st;
    unsigned char chunk_preamble[CHUNK_PREAMBLE_SIZE];
    int chunk_type;

    /* initialize private context members */
    ipmovie->video_pts = ipmovie->audio_frame_count = 0;
    ipmovie->audio_chunk_offset = ipmovie->video_chunk_offset =
    ipmovie->decode_map_chunk_offset = 0;

    /* on the first read, this will position the stream at the first chunk */
    ipmovie->next_chunk_offset = IPMOVIE_SIGNATURE_SIZE + 6;

    /* process the first chunk which should be CHUNK_INIT_VIDEO */
    if (process_ipmovie_chunk(ipmovie, pb, &pkt) != CHUNK_INIT_VIDEO)
        return AVERROR_INVALIDDATA;

    /* peek ahead to the next chunk-- if it is an init audio chunk, process
     * it; if it is the first video chunk, this is a silent file */
    if (get_buffer(pb, chunk_preamble, CHUNK_PREAMBLE_SIZE) !=
        CHUNK_PREAMBLE_SIZE)
        return AVERROR_IO;
    chunk_type = LE_16(&chunk_preamble[2]);
    url_fseek(pb, -CHUNK_PREAMBLE_SIZE, SEEK_CUR);

    if (chunk_type == CHUNK_VIDEO)
        ipmovie->audio_type = 0;  /* no audio */
    else if (process_ipmovie_chunk(ipmovie, pb, &pkt) != CHUNK_INIT_AUDIO)
        return AVERROR_INVALIDDATA;

    /* initialize the stream decoders */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    av_set_pts_info(st, 33, 1, 90000);
    ipmovie->video_stream_index = st->index;
    st->codec.codec_type = CODEC_TYPE_VIDEO;
    st->codec.codec_id = CODEC_ID_INTERPLAY_VIDEO;
    st->codec.codec_tag = 0;  /* no fourcc */
    st->codec.width = ipmovie->video_width;
    st->codec.height = ipmovie->video_height;

    /* palette considerations */
    st->codec.palctrl = &ipmovie->palette_control;

    if (ipmovie->audio_type) {
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR_NOMEM;
        av_set_pts_info(st, 33, 1, 90000);
        ipmovie->audio_stream_index = st->index;
        st->codec.codec_type = CODEC_TYPE_AUDIO;
        st->codec.codec_id = ipmovie->audio_type;
        st->codec.codec_tag = 0;  /* no tag */
        st->codec.channels = ipmovie->audio_channels;
        st->codec.sample_rate = ipmovie->audio_sample_rate;
        st->codec.bits_per_sample = ipmovie->audio_bits;
        st->codec.bit_rate = st->codec.channels * st->codec.sample_rate *
            st->codec.bits_per_sample;
        if (st->codec.codec_id == CODEC_ID_INTERPLAY_DPCM)
            st->codec.bit_rate /= 2;
        st->codec.block_align = st->codec.channels * st->codec.bits_per_sample;
    }

    return 0;
}

static int ipmovie_read_packet(AVFormatContext *s,
                               AVPacket *pkt)
{
    IPMVEContext *ipmovie = (IPMVEContext *)s->priv_data;
    ByteIOContext *pb = &s->pb;
    int ret;

    ret = process_ipmovie_chunk(ipmovie, pb, pkt);
    if (ret == CHUNK_BAD)
        ret = AVERROR_INVALIDDATA;
    else if (ret == CHUNK_EOF)
        ret = AVERROR_IO;
    else if (ret == CHUNK_NOMEM)
        ret = AVERROR_NOMEM;
    else
        ret = 0;

    return ret;
}

static int ipmovie_read_close(AVFormatContext *s)
{
//    IPMVEContext *ipmovie = (IPMVEContext *)s->priv_data;

    return 0;
}

static AVInputFormat ipmovie_iformat = {
    "ipmovie",
    "Interplay MVE format",
    sizeof(IPMVEContext),
    ipmovie_probe,
    ipmovie_read_header,
    ipmovie_read_packet,
    ipmovie_read_close,
};

int ipmovie_init(void)
{
    av_register_input_format(&ipmovie_iformat);
    return 0;
}

