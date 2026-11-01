/*
 * Digital Speech Standard (DSS) demuxer
 * Copyright (c) 2014 Oleksij Rempel <linux@rempel-privat.de>
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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem.h"

#include "avformat.h"
#include "demux.h"
#include "internal.h"
#include "avio_internal.h"

#define DSS_HEAD_OFFSET_AUTHOR        0xc
#define DSS_AUTHOR_SIZE               16

#define DSS_HEAD_OFFSET_START_TIME    0x26
#define DSS_HEAD_OFFSET_END_TIME      0x32
#define DSS_TIME_SIZE                 12

#define DSS_HEAD_OFFSET_ACODEC        0x2a4
#define DSS_ACODEC_DSS_SP             0x0    /* SP mode */
#define DSS_ACODEC_G723_1             0x2    /* LP mode */

#define DSS_HEAD_OFFSET_COMMENT       0x31e
#define DSS_COMMENT_SIZE              64

#define DSS_BLOCK_SIZE                512
#define DSS_AUDIO_BLOCK_HEADER_SIZE   6
#define DSS_BLOCK_KEEP_BEFORE         2
#define DSS_FRAME_SIZE                42

static const uint8_t frame_size[4] = { 24, 20, 4, 1 };

typedef struct DSSDemuxContext {
    unsigned int audio_codec;
    int counter;
    int swap;
    int64_t block_pos;      /* offset of the current audio block header */
    int frames_left;        /* frames still to come from that block */
    int dss_sp_swap_byte;

    int packet_size;
    int dss_header_size;
} DSSDemuxContext;

/* Frames that start in an audio block: byte 2 when it is not 0, otherwise
 * byte 5 unless that is 0xff. A recorder writes the second form where it
 * pauses or stops, and those frames are audio like any other. */
static int dss_block_frames(const uint8_t *header)
{
    if (header[2] || header[5] == 0xff)
        return header[2];
    return header[5];
}

static int dss_probe(const AVProbeData *p)
{
    if (   AV_RL32(p->buf) != MKTAG(0x2, 'd', 's', 's')
        && AV_RL32(p->buf) != MKTAG(0x3, 'd', 's', 's'))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int dss_read_metadata_date(AVFormatContext *s, unsigned int offset,
                                  const char *key)
{
    AVIOContext *pb = s->pb;
    char datetime[64], string[DSS_TIME_SIZE + 1] = { 0 };
    int y, month, d, h, minute, sec;
    int ret;

    avio_seek(pb, offset, SEEK_SET);

    ret = avio_read(s->pb, string, DSS_TIME_SIZE);
    if (ret < DSS_TIME_SIZE)
        return ret < 0 ? ret : AVERROR_EOF;

    if (sscanf(string, "%2d%2d%2d%2d%2d%2d", &y, &month, &d, &h, &minute, &sec) != 6)
        return AVERROR_INVALIDDATA;
    /* We deal with a two-digit year here, so set the default date to 2000
     * and hope it will never be used in the next century. */
    snprintf(datetime, sizeof(datetime), "%.4d-%.2d-%.2dT%.2d:%.2d:%.2d",
             y + 2000, month, d, h, minute, sec);
    return av_dict_set(&s->metadata, key, datetime, 0);
}

static int dss_read_metadata_string(AVFormatContext *s, unsigned int offset,
                                    unsigned int size, const char *key)
{
    AVIOContext *pb = s->pb;
    char *value;
    int ret;

    avio_seek(pb, offset, SEEK_SET);

    value = av_mallocz(size + 1);
    if (!value)
        return AVERROR(ENOMEM);

    ret = avio_read(s->pb, value, size);
    if (ret < size) {
        av_free(value);
        return ret < 0 ? ret : AVERROR_EOF;
    }

    return av_dict_set(&s->metadata, key, value, AV_DICT_DONT_STRDUP_VAL);
}

static int dss_read_header(AVFormatContext *s)
{
    DSSDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    int64_t ret64;
    int ret, version;

    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);

    version = avio_r8(pb);
    ctx->dss_header_size = version * DSS_BLOCK_SIZE;

    ret = dss_read_metadata_string(s, DSS_HEAD_OFFSET_AUTHOR,
                                   DSS_AUTHOR_SIZE, "author");
    if (ret)
        return ret;

    ret = dss_read_metadata_date(s, DSS_HEAD_OFFSET_END_TIME, "date");
    if (ret)
        return ret;

    ret = dss_read_metadata_string(s, DSS_HEAD_OFFSET_COMMENT,
                                   DSS_COMMENT_SIZE, "comment");
    if (ret)
        return ret;

    avio_seek(pb, DSS_HEAD_OFFSET_ACODEC, SEEK_SET);
    ctx->audio_codec = avio_r8(pb);

    if (ctx->audio_codec == DSS_ACODEC_DSS_SP) {
        st->codecpar->codec_id    = AV_CODEC_ID_DSS_SP;
        /* See libavcodec/dss_sp.c: the decoder runs at 12000 Hz and
         * decimates 11:12, so the output rate is 11000 Hz, not 11025. This
         * also sets the packet time base just below. */
        st->codecpar->sample_rate = 11000;
        s->bit_rate = 8 * (DSS_FRAME_SIZE - 1) * st->codecpar->sample_rate
                        * 512 / (506 * 264);
    } else if (ctx->audio_codec == DSS_ACODEC_G723_1) {
        st->codecpar->codec_id    = AV_CODEC_ID_G723_1;
        st->codecpar->sample_rate = 8000;
    } else {
        avpriv_request_sample(s, "Support for codec %x in DSS",
                              ctx->audio_codec);
        return AVERROR_PATCHWELCOME;
    }

    st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    st->codecpar->ch_layout      = (AVChannelLayout)AV_CHANNEL_LAYOUT_MONO;

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    st->start_time = 0;

    /* Jump over header */

    if ((ret64 = avio_seek(pb, ctx->dss_header_size, SEEK_SET)) < 0)
        return (int)ret64;

    ctx->counter = 0;
    ctx->swap        = 0;
    ctx->frames_left = 0;
    ctx->dss_sp_swap_byte = -1;
    /* dss_sp_next_block steps forward first, so start one block short. */
    ctx->block_pos   = ctx->dss_header_size - DSS_BLOCK_SIZE;

    return 0;
}

/* G.723.1 blocks carry no byte-swap parity and their frames do not straddle,
 * so this path keeps the running walk it has always used. */
static void dss_skip_audio_header(AVFormatContext *s, AVPacket *pkt)
{
    DSSDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;

    avio_skip(pb, DSS_AUDIO_BLOCK_HEADER_SIZE);
    ctx->counter += DSS_BLOCK_SIZE - DSS_AUDIO_BLOCK_HEADER_SIZE;
}

static int dss_seek_block(AVFormatContext *s, int64_t pos)
{
    DSSDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    int64_t keep = FFMAX(pos - DSS_BLOCK_KEEP_BEFORE, ctx->dss_header_size);
    int ret;

    if (!(pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        if (avio_seek(pb, keep, SEEK_SET) < 0)
            return AVERROR_EOF;
        ret = ffio_ensure_seekback(pb, pos - keep + 2 * DSS_BLOCK_SIZE);
        if (ret < 0)
            return ret;
    }
    return avio_seek(pb, pos, SEEK_SET) < 0 ? AVERROR_EOF : 0;
}

/* Read `size` bytes of frame payload, stepping over any block header met on
 * the way. Block payloads are contiguous as far as the frames are concerned,
 * and a frame is short enough to straddle at most one boundary. */
static int dss_sp_read_payload(AVFormatContext *s, uint8_t *buf, int size)
{
    DSSDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;

    while (size > 0) {
        int64_t off = (avio_tell(pb) - ctx->dss_header_size) % DSS_BLOCK_SIZE;
        int chunk;

        if (off < DSS_AUDIO_BLOCK_HEADER_SIZE) {
            if (avio_skip(pb, DSS_AUDIO_BLOCK_HEADER_SIZE - off) < 0)
                return AVERROR_EOF;
            continue;
        }
        chunk = FFMIN(size, (int)(DSS_BLOCK_SIZE - off));
        ret = ffio_read_size(pb, buf, chunk);
        if (ret < 0)
            return ret;
        buf  += chunk;
        size -= chunk;
    }
    return 0;
}

/* Move to the next audio block and take its framing from its header.
 *
 * Each block states the length of the frame fragment carried over from the
 * previous block, the byte-swap parity of its first whole frame, and how many
 * frames it holds. Deriving the framing by running on from the previous frame
 * instead gives the same answer for as long as nothing disturbs the recording,
 * which is why most files decode; but a paused or edited recording restates
 * them, and a running walk then reads every remaining frame a byte out of
 * phase. Believing the block keeps the walk in step, and costs nothing when
 * the two agree. */
static int dss_sp_next_block(AVFormatContext *s)
{
    DSSDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    uint8_t header[DSS_AUDIO_BLOCK_HEADER_SIZE];
    int64_t block_pos = ctx->block_pos;
    int cont, ret;

    for (;;) {
        ctx->block_pos += DSS_BLOCK_SIZE;
        if ((ret = dss_seek_block(s, ctx->block_pos)) < 0)
            goto fail;
        ret = avio_read(pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
        if (ret < 0 && ret != AVERROR_EOF)
            goto fail;
        if (ret != DSS_AUDIO_BLOCK_HEADER_SIZE) {
            ret = AVERROR_EOF;
            goto fail;
        }

        ctx->swap = !!(header[0] & 0x80);
        cont      = 2 * header[1] + 2 * ctx->swap - DSS_AUDIO_BLOCK_HEADER_SIZE;
        if (cont < 0 || cont > DSS_BLOCK_SIZE - DSS_AUDIO_BLOCK_HEADER_SIZE)
            cont = 0;

        ctx->frames_left = dss_block_frames(header);
        if (!ctx->frames_left)
            continue;

        /* A swapped frame takes one byte from the frame before it, two bytes
         * before its own start, in this block or at the end of the previous
         * one. Read it from there rather than carry it over: after a seek, at
         * the start of a cut file, or where a block restarts the framing, the
         * frame before has not been read. */
        if (ctx->swap) {
            int64_t at = ctx->block_pos + cont - 2;

            if (cont >= 2)
                at += DSS_AUDIO_BLOCK_HEADER_SIZE;
            if (at >= ctx->dss_header_size + DSS_AUDIO_BLOCK_HEADER_SIZE &&
                avio_seek(pb, at, SEEK_SET) >= 0)
                ctx->dss_sp_swap_byte = avio_r8(pb);
        }

        if (avio_seek(pb, ctx->block_pos + DSS_AUDIO_BLOCK_HEADER_SIZE + cont,
                      SEEK_SET) < 0) {
            ret = AVERROR_EOF;
            goto fail;
        }
        return 0;
    }

fail:
    ctx->block_pos   = block_pos;
    ctx->frames_left = 0;
    return ret;
}

/* Offset of the next frame. A frame can start where a block does, after the
 * block header that dss_sp_read_payload() steps over. */
static int64_t dss_frame_pos(AVFormatContext *s)
{
    DSSDemuxContext *ctx = s->priv_data;
    int64_t pos = avio_tell(s->pb);
    int64_t off = (pos - ctx->dss_header_size) % DSS_BLOCK_SIZE;

    if (off < DSS_AUDIO_BLOCK_HEADER_SIZE)
        pos += DSS_AUDIO_BLOCK_HEADER_SIZE - off;
    return pos;
}

static void dss_sp_byte_swap(DSSDemuxContext *ctx, uint8_t *data)
{
    int i;

    if (ctx->swap) {
        for (i = 0; i < DSS_FRAME_SIZE - 2; i += 2)
            data[i] = data[i + 4];

        /* Zero the padding. */
        data[DSS_FRAME_SIZE] = 0;
        data[1] = ctx->dss_sp_swap_byte;
    } else {
        ctx->dss_sp_swap_byte = data[DSS_FRAME_SIZE - 2];
    }

    /* make sure byte 40 is always 0 */
    data[DSS_FRAME_SIZE - 2] = 0;
    ctx->swap             ^= 1;
}

static int dss_sp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DSSDemuxContext *ctx = s->priv_data;
    int read_size, ret, buff_offset = 0;
    int64_t pos;

    if (ctx->frames_left <= 0) {
        ret = dss_sp_next_block(s);
        if (ret < 0)
            return ret;
    }
    pos = dss_frame_pos(s);

    if (ctx->swap) {
        read_size   = DSS_FRAME_SIZE - 2;
        buff_offset = 3;
    } else
        read_size = DSS_FRAME_SIZE;

    ret = av_new_packet(pkt, DSS_FRAME_SIZE);
    if (ret < 0)
        return ret;

    pkt->duration     = 264;
    pkt->pos = pos;
    pkt->stream_index = 0;

    /* This will write one byte into pkt's padding if buff_offset == 3 */
    ret = dss_sp_read_payload(s, pkt->data + buff_offset, read_size);
    if (ret < 0)
        return ret == AVERROR_EOF ? ret : AVERROR_EOF;

    ctx->frames_left--;

    dss_sp_byte_swap(ctx, pkt->data);

    if (ctx->dss_sp_swap_byte < 0) {
        return FFERROR_REDO;
    }

    return 0;
}

static int dss_723_1_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DSSDemuxContext *ctx = s->priv_data;
    AVStream *st = s->streams[0];
    int size, byte, ret, offset;
    int64_t pos = avio_tell(s->pb);

    if (ctx->counter == 0)
        dss_skip_audio_header(s, pkt);

    /* We make one byte-step here. Don't forget to add offset. */
    byte = avio_r8(s->pb);
    if (byte == 0xff)
        return AVERROR_INVALIDDATA;

    size = frame_size[byte & 3];

    ctx->packet_size = size;
    ctx->counter--;

    ret = av_new_packet(pkt, size);
    if (ret < 0)
        return ret;
    pkt->pos = pos;

    pkt->data[0]  = byte;
    offset        = 1;
    pkt->duration = 240;
    s->bit_rate = 8LL * size-- * st->codecpar->sample_rate * 512 / (506 * pkt->duration);

    pkt->stream_index = 0;

    if (ctx->counter < size) {
        ret = avio_read(s->pb, pkt->data + offset,
                        ctx->counter);
        if (ret < ctx->counter)
            return ret < 0 ? ret : AVERROR_EOF;

        offset += ctx->counter;
        size   -= ctx->counter;
        ctx->counter = 0;
        dss_skip_audio_header(s, pkt);
    }
    ctx->counter -= size;

    ret = avio_read(s->pb, pkt->data + offset, size);
    if (ret < size)
        return ret < 0 ? ret : AVERROR_EOF;

    return 0;
}

static int dss_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DSSDemuxContext *ctx = s->priv_data;

    if (ctx->audio_codec == DSS_ACODEC_DSS_SP)
        return dss_sp_read_packet(s, pkt);
    else
        return dss_723_1_read_packet(s, pkt);
}

static int dss_read_seek(AVFormatContext *s, int stream_index,
                         int64_t timestamp, int flags)
{
    DSSDemuxContext *ctx = s->priv_data;
    int64_t ret, seekto;
    uint8_t header[DSS_AUDIO_BLOCK_HEADER_SIZE];
    int offset;

    if (ctx->audio_codec == DSS_ACODEC_DSS_SP)
        seekto = timestamp / 264 * 41 / 506 * 512;
    else
        seekto = timestamp / 240 * ctx->packet_size / 506 * 512;

    if (seekto < 0)
        seekto = 0;

    seekto += ctx->dss_header_size;

    ret = avio_seek(s->pb, seekto, SEEK_SET);
    if (ret < 0)
        return ret;

    ret = ffio_read_size(s->pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
    if (ret < 0)
        return ret;
    ctx->swap        = !!(header[0] & 0x80);
    ctx->frames_left = 0;
    ctx->block_pos   = seekto - DSS_BLOCK_SIZE;
    if (ctx->audio_codec == DSS_ACODEC_DSS_SP) {
        ctx->dss_sp_swap_byte = -1;
        return 0;
    }

    offset = 2*header[1] + 2*ctx->swap;
    if (offset < DSS_AUDIO_BLOCK_HEADER_SIZE)
        return AVERROR_INVALIDDATA;
    if (offset == DSS_AUDIO_BLOCK_HEADER_SIZE) {
        ctx->counter = 0;
        offset = avio_skip(s->pb, -DSS_AUDIO_BLOCK_HEADER_SIZE);
    } else {
        ctx->counter = DSS_BLOCK_SIZE - offset;
        offset = avio_skip(s->pb, offset - DSS_AUDIO_BLOCK_HEADER_SIZE);
    }
    ctx->dss_sp_swap_byte = -1;
    return 0;
}


const FFInputFormat ff_dss_demuxer = {
    .p.name         = "dss",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Digital Speech Standard (DSS)"),
    .p.extensions   = "dss",
    .priv_data_size = sizeof(DSSDemuxContext),
    .read_probe     = dss_probe,
    .read_header    = dss_read_header,
    .read_packet    = dss_read_packet,
    .read_seek      = dss_read_seek,
};
