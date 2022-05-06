/*
 * Interplay MVE File Demuxer
 * Copyright (c) 2003 The FFmpeg project
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

/**
 * @file
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "demux.h"
#include "internal.h"

#define CHUNK_PREAMBLE_SIZE 4
#define OPCODE_PREAMBLE_SIZE 4

#define CHUNK_INIT_AUDIO   0x0000
#define CHUNK_AUDIO_ONLY   0x0001
#define CHUNK_INIT_VIDEO   0x0002
#define CHUNK_VIDEO        0x0003
#define CHUNK_SHUTDOWN     0x0004
#define CHUNK_END          0x0005
/* these last types are used internally */
#define CHUNK_HAVE_PACKET  0xFFFB
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
#define OPCODE_VIDEO_DATA_06           0x06
#define OPCODE_SEND_BUFFER             0x07
#define OPCODE_AUDIO_FRAME             0x08
#define OPCODE_SILENCE_FRAME           0x09
#define OPCODE_INIT_VIDEO_MODE         0x0A
#define OPCODE_CREATE_GRADIENT         0x0B
#define OPCODE_SET_PALETTE             0x0C
#define OPCODE_SET_PALETTE_COMPRESSED  0x0D
#define OPCODE_SET_SKIP_MAP            0x0E
#define OPCODE_SET_DECODING_MAP        0x0F
#define OPCODE_VIDEO_DATA_10           0x10
#define OPCODE_VIDEO_DATA_11           0x11
#define OPCODE_UNKNOWN_12              0x12
#define OPCODE_UNKNOWN_13              0x13
#define OPCODE_UNKNOWN_14              0x14
#define OPCODE_UNKNOWN_15              0x15

#define PALETTE_COUNT 256

typedef struct IPMVEContext {
    AVFormatContext *avf;
    unsigned char *buf;
    int buf_size;

    uint64_t frame_pts_inc;

    unsigned int video_bpp;
    unsigned int video_width;
    unsigned int video_height;
    int64_t video_pts;
    uint32_t     palette[256];
    int          has_palette;
    int          changed;
    uint8_t      send_buffer;
    uint8_t      frame_format;

    unsigned int audio_bits;
    unsigned int audio_channels;
    unsigned int audio_sample_rate;
    enum AVCodecID audio_type;
    unsigned int audio_frame_count;

    int video_stream_index;
    int audio_stream_index;

    int64_t audio_chunk_offset;
    int audio_chunk_size;
    int64_t video_chunk_offset;
    int video_chunk_size;
    int64_t skip_map_chunk_offset;
    int skip_map_chunk_size;
    int64_t decode_map_chunk_offset;
    int decode_map_chunk_size;

    int64_t next_chunk_offset;

} IPMVEContext;

static int load_ipmovie_packet(IPMVEContext *s, AVIOContext *pb,
    AVPacket *pkt) {

    int chunk_type;

    if (s->audio_chunk_offset && s->audio_channels && s->audio_bits) {
        if (s->audio_type == AV_CODEC_ID_NONE) {
            av_log(s->avf, AV_LOG_ERROR, "Can not read audio packet before"
                   "audio codec is known\n");
                return CHUNK_BAD;
        }

        /* adjust for PCM audio by skipping chunk header */
        if (s->audio_type != AV_CODEC_ID_INTERPLAY_DPCM) {
            s->audio_chunk_offset += 6;
            s->audio_chunk_size -= 6;
        }

        avio_seek(pb, s->audio_chunk_offset, SEEK_SET);
        s->audio_chunk_offset = 0;

        if (s->audio_chunk_size != av_get_packet(pb, pkt, s->audio_chunk_size))
            return CHUNK_EOF;

        pkt->stream_index = s->audio_stream_index;
        pkt->pts = s->audio_frame_count;

        /* audio frame maintenance */
        if (s->audio_type != AV_CODEC_ID_INTERPLAY_DPCM)
            s->audio_frame_count +=
            (s->audio_chunk_size / s->audio_channels / (s->audio_bits / 8));
        else
            s->audio_frame_count +=
                (s->audio_chunk_size - 6 - s->audio_channels) / s->audio_channels;

        av_log(s->avf, AV_LOG_TRACE, "sending audio frame with pts %"PRId64" (%d audio frames)\n",
                pkt->pts, s->audio_frame_count);

        chunk_type = CHUNK_HAVE_PACKET;

    } else if (s->frame_format) {

        /* send the frame format, decode map, the video data, skip map, and the send_buffer flag together */

        if (av_new_packet(pkt, 8 + s->decode_map_chunk_size + s->video_chunk_size + s->skip_map_chunk_size))
            return CHUNK_NOMEM;

        if (s->has_palette) {
            uint8_t *pal;

            pal = av_packet_new_side_data(pkt, AV_PKT_DATA_PALETTE,
                                          AVPALETTE_SIZE);
            if (pal) {
                memcpy(pal, s->palette, AVPALETTE_SIZE);
                s->has_palette = 0;
            }
        }

        if (s->changed) {
            ff_add_param_change(pkt, 0, 0, 0, s->video_width, s->video_height);
            s->changed = 0;
        }

        AV_WL8(pkt->data, s->frame_format);
        AV_WL8(pkt->data + 1, s->send_buffer);
        AV_WL16(pkt->data + 2, s->video_chunk_size);
        AV_WL16(pkt->data + 4, s->decode_map_chunk_size);
        AV_WL16(pkt->data + 6, s->skip_map_chunk_size);

        s->frame_format = 0;
        s->send_buffer = 0;

        pkt->pos = s->video_chunk_offset;
        avio_seek(pb, s->video_chunk_offset, SEEK_SET);
        s->video_chunk_offset = 0;

        if (avio_read(pb, pkt->data + 8, s->video_chunk_size) !=
            s->video_chunk_size) {
            return CHUNK_EOF;
        }

        if (s->decode_map_chunk_size) {
            pkt->pos = s->decode_map_chunk_offset;
            avio_seek(pb, s->decode_map_chunk_offset, SEEK_SET);
            s->decode_map_chunk_offset = 0;

            if (avio_read(pb, pkt->data + 8 + s->video_chunk_size,
                s->decode_map_chunk_size) != s->decode_map_chunk_size) {
                return CHUNK_EOF;
            }
        }

        if (s->skip_map_chunk_size) {
            pkt->pos = s->skip_map_chunk_offset;
            avio_seek(pb, s->skip_map_chunk_offset, SEEK_SET);
            s->skip_map_chunk_offset = 0;

            if (avio_read(pb, pkt->data + 8 + s->video_chunk_size + s->decode_map_chunk_size,
                s->skip_map_chunk_size) != s->skip_map_chunk_size) {
                return CHUNK_EOF;
            }
        }

        s->video_chunk_size = 0;
        s->decode_map_chunk_size = 0;
        s->skip_map_chunk_size = 0;

        pkt->stream_index = s->video_stream_index;
        pkt->pts = s->video_pts;

        av_log(s->avf, AV_LOG_TRACE, "sending video frame with pts %"PRId64"\n", pkt->pts);

        s->video_pts += s->frame_pts_inc;

        chunk_type = CHUNK_HAVE_PACKET;

    } else {

        avio_seek(pb, s->next_chunk_offset, SEEK_SET);
        chunk_type = CHUNK_DONE;

    }

    return chunk_type;
}

static int init_audio(AVFormatContext *s)
{
    IPMVEContext *ipmovie = s->priv_data;
    AVStream *st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 32, 1, ipmovie->audio_sample_rate);
    ipmovie->audio_stream_index = st->index;
    st->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id = ipmovie->audio_type;
    st->codecpar->codec_tag = 0;  /* no tag */
    av_channel_layout_default(&st->codecpar->ch_layout, ipmovie->audio_channels);
    st->codecpar->sample_rate = ipmovie->audio_sample_rate;
    st->codecpar->bits_per_coded_sample = ipmovie->audio_bits;
    st->codecpar->bit_rate = ipmovie->audio_channels * st->codecpar->sample_rate *
        st->codecpar->bits_per_coded_sample;
    if (st->codecpar->codec_id == AV_CODEC_ID_INTERPLAY_DPCM)
        st->codecpar->bit_rate /= 2;
    st->codecpar->block_align = ipmovie->audio_channels * st->codecpar->bits_per_coded_sample;

    return 0;
}

/* This function loads and processes a single chunk in an IP movie file.
 * It returns the type of chunk that was processed. */
static int process_ipmovie_chunk(IPMVEContext *s, AVIOContext *pb,
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
    unsigned int width, height;

    /* see if there are any pending packets */
    chunk_type = load_ipmovie_packet(s, pb, pkt);
    if (chunk_type != CHUNK_DONE)
        return chunk_type;

    /* read the next chunk, wherever the file happens to be pointing */
    if (avio_feof(pb))
        return CHUNK_EOF;
    if (avio_read(pb, chunk_preamble, CHUNK_PREAMBLE_SIZE) !=
        CHUNK_PREAMBLE_SIZE)
        return CHUNK_BAD;
    chunk_size = AV_RL16(&chunk_preamble[0]);
    chunk_type = AV_RL16(&chunk_preamble[2]);

    av_log(s->avf, AV_LOG_TRACE, "chunk type 0x%04X, 0x%04X bytes: ", chunk_type, chunk_size);

    switch (chunk_type) {

    case CHUNK_INIT_AUDIO:
        av_log(s->avf, AV_LOG_TRACE, "initialize audio\n");
        break;

    case CHUNK_AUDIO_ONLY:
        av_log(s->avf, AV_LOG_TRACE, "audio only\n");
        break;

    case CHUNK_INIT_VIDEO:
        av_log(s->avf, AV_LOG_TRACE, "initialize video\n");
        break;

    case CHUNK_VIDEO:
        av_log(s->avf, AV_LOG_TRACE, "video (and audio)\n");
        break;

    case CHUNK_SHUTDOWN:
        av_log(s->avf, AV_LOG_TRACE, "shutdown\n");
        break;

    case CHUNK_END:
        av_log(s->avf, AV_LOG_TRACE, "end\n");
        break;

    default:
        av_log(s->avf, AV_LOG_TRACE, "invalid chunk\n");
        chunk_type = CHUNK_BAD;
        break;

    }

    while ((chunk_size > 0) && (chunk_type != CHUNK_BAD)) {

        /* read the next chunk, wherever the file happens to be pointing */
        if (avio_feof(pb)) {
            chunk_type = CHUNK_EOF;
            break;
        }
        if (avio_read(pb, opcode_preamble, CHUNK_PREAMBLE_SIZE) !=
            CHUNK_PREAMBLE_SIZE) {
            chunk_type = CHUNK_BAD;
            break;
        }

        opcode_size = AV_RL16(&opcode_preamble[0]);
        opcode_type = opcode_preamble[2];
        opcode_version = opcode_preamble[3];

        chunk_size -= OPCODE_PREAMBLE_SIZE;
        chunk_size -= opcode_size;
        if (chunk_size < 0) {
            av_log(s->avf, AV_LOG_TRACE, "chunk_size countdown just went negative\n");
            chunk_type = CHUNK_BAD;
            break;
        }

        av_log(s->avf, AV_LOG_TRACE, "  opcode type %02X, version %d, 0x%04X bytes: ",
                opcode_type, opcode_version, opcode_size);
        switch (opcode_type) {

        case OPCODE_END_OF_STREAM:
            av_log(s->avf, AV_LOG_TRACE, "end of stream\n");
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_END_OF_CHUNK:
            av_log(s->avf, AV_LOG_TRACE, "end of chunk\n");
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_CREATE_TIMER:
            av_log(s->avf, AV_LOG_TRACE, "create timer\n");
            if ((opcode_version > 0) || (opcode_size != 6)) {
                av_log(s->avf, AV_LOG_TRACE, "bad create_timer opcode\n");
                chunk_type = CHUNK_BAD;
                break;
            }
            if (avio_read(pb, scratch, opcode_size) !=
                opcode_size) {
                chunk_type = CHUNK_BAD;
                break;
            }
            s->frame_pts_inc = ((uint64_t)AV_RL32(&scratch[0])) * AV_RL16(&scratch[4]);
            break;

        case OPCODE_INIT_AUDIO_BUFFERS:
            av_log(s->avf, AV_LOG_TRACE, "initialize audio buffers\n");
            if (opcode_version > 1 || opcode_size > 10 || opcode_size < 6) {
                av_log(s->avf, AV_LOG_TRACE, "bad init_audio_buffers opcode\n");
                chunk_type = CHUNK_BAD;
                break;
            }
            if (avio_read(pb, scratch, opcode_size) !=
                opcode_size) {
                chunk_type = CHUNK_BAD;
                break;
            }
            s->audio_sample_rate = AV_RL16(&scratch[4]);
            audio_flags = AV_RL16(&scratch[2]);
            /* bit 0 of the flags: 0 = mono, 1 = stereo */
            s->audio_channels = (audio_flags & 1) + 1;
            /* bit 1 of the flags: 0 = 8 bit, 1 = 16 bit */
            s->audio_bits = (((audio_flags >> 1) & 1) + 1) * 8;
            /* bit 2 indicates compressed audio in version 1 opcode */
            if ((opcode_version == 1) && (audio_flags & 0x4))
                s->audio_type = AV_CODEC_ID_INTERPLAY_DPCM;
            else if (s->audio_bits == 16)
                s->audio_type = AV_CODEC_ID_PCM_S16LE;
            else
                s->audio_type = AV_CODEC_ID_PCM_U8;
            av_log(s->avf, AV_LOG_TRACE, "audio: %d bits, %d Hz, %s, %s format\n",
                    s->audio_bits, s->audio_sample_rate,
                    (s->audio_channels == 2) ? "stereo" : "mono",
                    (s->audio_type == AV_CODEC_ID_INTERPLAY_DPCM) ?
                    "Interplay audio" : "PCM");
            break;

        case OPCODE_START_STOP_AUDIO:
            av_log(s->avf, AV_LOG_TRACE, "start/stop audio\n");
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_INIT_VIDEO_BUFFERS:
            av_log(s->avf, AV_LOG_TRACE, "initialize video buffers\n");
            if ((opcode_version > 2) || (opcode_size > 8) || opcode_size < 4
                || opcode_version == 2 && opcode_size < 8
            ) {
                av_log(s->avf, AV_LOG_TRACE, "bad init_video_buffers opcode\n");
                chunk_type = CHUNK_BAD;
                break;
            }
            if (avio_read(pb, scratch, opcode_size) !=
                opcode_size) {
                chunk_type = CHUNK_BAD;
                break;
            }
            width  = AV_RL16(&scratch[0]) * 8;
            height = AV_RL16(&scratch[2]) * 8;
            if (width != s->video_width) {
                s->video_width = width;
                s->changed++;
            }
            if (height != s->video_height) {
                s->video_height = height;
                s->changed++;
            }
            if (opcode_version < 2 || !AV_RL16(&scratch[6])) {
                s->video_bpp = 8;
            } else {
                s->video_bpp = 16;
            }
            av_log(s->avf, AV_LOG_TRACE, "video resolution: %d x %d\n",
                    s->video_width, s->video_height);
            break;

        case OPCODE_UNKNOWN_12:
        case OPCODE_UNKNOWN_13:
        case OPCODE_UNKNOWN_14:
        case OPCODE_UNKNOWN_15:
            av_log(s->avf, AV_LOG_TRACE, "unknown (but documented) opcode %02X\n", opcode_type);
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_SEND_BUFFER:
            av_log(s->avf, AV_LOG_TRACE, "send buffer\n");
            avio_skip(pb, opcode_size);
            s->send_buffer = 1;
            break;

        case OPCODE_AUDIO_FRAME:
            av_log(s->avf, AV_LOG_TRACE, "audio frame\n");

            /* log position and move on for now */
            s->audio_chunk_offset = avio_tell(pb);
            s->audio_chunk_size = opcode_size;
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_SILENCE_FRAME:
            av_log(s->avf, AV_LOG_TRACE, "silence frame\n");
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_INIT_VIDEO_MODE:
            av_log(s->avf, AV_LOG_TRACE, "initialize video mode\n");
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_CREATE_GRADIENT:
            av_log(s->avf, AV_LOG_TRACE, "create gradient\n");
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_SET_PALETTE:
            av_log(s->avf, AV_LOG_TRACE, "set palette\n");
            /* check for the logical maximum palette size
             * (3 * 256 + 4 bytes) */
            if (opcode_size > 0x304 || opcode_size < 4) {
                av_log(s->avf, AV_LOG_TRACE, "demux_ipmovie: set_palette opcode with invalid size\n");
                chunk_type = CHUNK_BAD;
                break;
            }
            if (avio_read(pb, scratch, opcode_size) != opcode_size) {
                chunk_type = CHUNK_BAD;
                break;
            }

            /* load the palette into internal data structure */
            first_color = AV_RL16(&scratch[0]);
            last_color = first_color + AV_RL16(&scratch[2]) - 1;
            /* sanity check (since they are 16 bit values) */
            if (   (first_color > 0xFF) || (last_color > 0xFF)
                || (last_color - first_color + 1)*3 + 4 > opcode_size) {
                av_log(s->avf, AV_LOG_TRACE, "demux_ipmovie: set_palette indexes out of range (%d -> %d)\n",
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
                s->palette[i] = (0xFFU << 24) | (r << 16) | (g << 8) | (b);
                s->palette[i] |= s->palette[i] >> 6 & 0x30303;
            }
            s->has_palette = 1;
            break;

        case OPCODE_SET_PALETTE_COMPRESSED:
            av_log(s->avf, AV_LOG_TRACE, "set palette compressed\n");
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_SET_SKIP_MAP:
            av_log(s->avf, AV_LOG_TRACE, "set skip map\n");

            /* log position and move on for now */
            s->skip_map_chunk_offset = avio_tell(pb);
            s->skip_map_chunk_size = opcode_size;
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_SET_DECODING_MAP:
            av_log(s->avf, AV_LOG_TRACE, "set decoding map\n");

            /* log position and move on for now */
            s->decode_map_chunk_offset = avio_tell(pb);
            s->decode_map_chunk_size = opcode_size;
            avio_skip(pb, opcode_size);
            break;

        case OPCODE_VIDEO_DATA_06:
        case OPCODE_VIDEO_DATA_10:
        case OPCODE_VIDEO_DATA_11:
            s->frame_format = opcode_type;
            av_log(s->avf, AV_LOG_TRACE, "set video data format 0x%02X\n",
                   opcode_type);

            /* log position and move on for now */
            s->video_chunk_offset = avio_tell(pb);
            s->video_chunk_size = opcode_size;
            avio_skip(pb, opcode_size);
            break;

        default:
            av_log(s->avf, AV_LOG_TRACE, "*** unknown opcode type\n");
            chunk_type = CHUNK_BAD;
            break;

        }
    }

    if (s->avf->nb_streams == 1 && s->audio_type)
        init_audio(s->avf);

    /* make a note of where the stream is sitting */
    s->next_chunk_offset = avio_tell(pb);

    return chunk_type;
}

static const char signature[] = "Interplay MVE File\x1A\0\x1A";

static int ipmovie_probe(const AVProbeData *p)
{
    const uint8_t *b = p->buf;
    const uint8_t *b_end = p->buf + p->buf_size - sizeof(signature);
    do {
        if (b[0] == signature[0] && memcmp(b, signature, sizeof(signature)) == 0)
            return AVPROBE_SCORE_MAX;
        b++;
    } while (b < b_end);

    return 0;
}

static int ipmovie_read_header(AVFormatContext *s)
{
    IPMVEContext *ipmovie = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    unsigned char chunk_preamble[CHUNK_PREAMBLE_SIZE];
    int chunk_type, i;
    uint8_t signature_buffer[sizeof(signature)];

    ipmovie->avf = s;

    avio_read(pb, signature_buffer, sizeof(signature_buffer));
    while (memcmp(signature_buffer, signature, sizeof(signature))) {
        memmove(signature_buffer, signature_buffer + 1, sizeof(signature_buffer) - 1);
        signature_buffer[sizeof(signature_buffer) - 1] = avio_r8(pb);
        if (avio_feof(pb))
            return AVERROR_EOF;
    }

    /* on the first read, this will position the stream at the first chunk */
    ipmovie->next_chunk_offset = avio_tell(pb) + 4;

    for (i = 0; i < 256; i++)
        ipmovie->palette[i] = 0xFFU << 24;

    /* process the first chunk which should be CHUNK_INIT_VIDEO */
    if (process_ipmovie_chunk(ipmovie, pb, NULL) != CHUNK_INIT_VIDEO) {
        return AVERROR_INVALIDDATA;
    }

    /* peek ahead to the next chunk-- if it is an init audio chunk, process
     * it; if it is the first video chunk, this is a silent file */
    if (avio_read(pb, chunk_preamble, CHUNK_PREAMBLE_SIZE) !=
        CHUNK_PREAMBLE_SIZE)
        return AVERROR(EIO);
    chunk_type = AV_RL16(&chunk_preamble[2]);
    avio_seek(pb, -CHUNK_PREAMBLE_SIZE, SEEK_CUR);

    if (chunk_type == CHUNK_VIDEO)
        ipmovie->audio_type = AV_CODEC_ID_NONE;  /* no audio */
    else if (process_ipmovie_chunk(ipmovie, pb, ffformatcontext(s)->parse_pkt) != CHUNK_INIT_AUDIO) {
        return AVERROR_INVALIDDATA;
    }

    /* initialize the stream decoders */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    avpriv_set_pts_info(st, 63, 1, 1000000);
    ipmovie->video_stream_index = st->index;
    st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = AV_CODEC_ID_INTERPLAY_VIDEO;
    st->codecpar->codec_tag = 0;  /* no fourcc */
    st->codecpar->width = ipmovie->video_width;
    st->codecpar->height = ipmovie->video_height;
    st->codecpar->bits_per_coded_sample = ipmovie->video_bpp;

    if (ipmovie->audio_type) {
        return init_audio(s);
    } else
       s->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0;
}

static int ipmovie_read_packet(AVFormatContext *s,
                               AVPacket *pkt)
{
    IPMVEContext *ipmovie = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;

    for (;;) {
        ret = process_ipmovie_chunk(ipmovie, pb, pkt);
        /* dispatch the first of any pending packets */
        if ((ret == CHUNK_VIDEO) || (ret == CHUNK_AUDIO_ONLY))
            ret = load_ipmovie_packet(ipmovie, pb, pkt);

        if (ret == CHUNK_BAD)
            ret = AVERROR_INVALIDDATA;
        else if (ret == CHUNK_EOF)
            ret = AVERROR(EIO);
        else if (ret == CHUNK_NOMEM)
            ret = AVERROR(ENOMEM);
        else if (ret == CHUNK_END || ret == CHUNK_SHUTDOWN)
            ret = AVERROR_EOF;
        else if (ret == CHUNK_HAVE_PACKET)
            ret = 0;
        else if (ret == CHUNK_INIT_VIDEO || ret == CHUNK_INIT_AUDIO)
            continue;
        else
            continue;

        return ret;
    }
}

const AVInputFormat ff_ipmovie_demuxer = {
    .name           = "ipmovie",
    .long_name      = NULL_IF_CONFIG_SMALL("Interplay MVE"),
    .priv_data_size = sizeof(IPMVEContext),
    .read_probe     = ipmovie_probe,
    .read_header    = ipmovie_read_header,
    .read_packet    = ipmovie_read_packet,
};
