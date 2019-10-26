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

#include "avformat.h"
#include "internal.h"

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
#define DSS_FRAME_SIZE                42

static const uint8_t frame_size[4] = { 24, 20, 4, 1 };

typedef struct DSSDemuxContext {
    unsigned int audio_codec;
    int counter;
    int swap;
    int dss_sp_swap_byte;
    int8_t *dss_sp_buf;

    int packet_size;
    int dss_header_size;
} DSSDemuxContext;

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
        ret = ret < 0 ? ret : AVERROR_EOF;
        goto exit;
    }

    ret = av_dict_set(&s->metadata, key, value, 0);

exit:
    av_free(value);
    return ret;
}

static int dss_read_header(AVFormatContext *s)
{
    DSSDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
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
        st->codecpar->sample_rate = 11025;
    } else if (ctx->audio_codec == DSS_ACODEC_G723_1) {
        st->codecpar->codec_id    = AV_CODEC_ID_G723_1;
        st->codecpar->sample_rate = 8000;
    } else {
        avpriv_request_sample(s, "Support for codec %x in DSS",
                              ctx->audio_codec);
        return AVERROR_PATCHWELCOME;
    }

    st->codecpar->codec_type     = AVMEDIA_TYPE_AUDIO;
    st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
    st->codecpar->channels       = 1;

    avpriv_set_pts_info(st, 64, 1, st->codecpar->sample_rate);
    st->start_time = 0;

    /* Jump over header */

    if (avio_seek(pb, ctx->dss_header_size, SEEK_SET) != ctx->dss_header_size)
        return AVERROR(EIO);

    ctx->counter = 0;
    ctx->swap    = 0;

    ctx->dss_sp_buf = av_malloc(DSS_FRAME_SIZE + 1);
    if (!ctx->dss_sp_buf)
        return AVERROR(ENOMEM);

    return 0;
}

static void dss_skip_audio_header(AVFormatContext *s, AVPacket *pkt)
{
    DSSDemuxContext *ctx = s->priv_data;
    AVIOContext *pb = s->pb;

    avio_skip(pb, DSS_AUDIO_BLOCK_HEADER_SIZE);
    ctx->counter += DSS_BLOCK_SIZE - DSS_AUDIO_BLOCK_HEADER_SIZE;
}

static void dss_sp_byte_swap(DSSDemuxContext *ctx,
                             uint8_t *dst, const uint8_t *src)
{
    int i;

    if (ctx->swap) {
        for (i = 3; i < DSS_FRAME_SIZE; i += 2)
            dst[i] = src[i];

        for (i = 0; i < DSS_FRAME_SIZE - 2; i += 2)
            dst[i] = src[i + 4];

        dst[1] = ctx->dss_sp_swap_byte;
    } else {
        memcpy(dst, src, DSS_FRAME_SIZE);
        ctx->dss_sp_swap_byte = src[DSS_FRAME_SIZE - 2];
    }

    /* make sure byte 40 is always 0 */
    dst[DSS_FRAME_SIZE - 2] = 0;
    ctx->swap             ^= 1;
}

static int dss_sp_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DSSDemuxContext *ctx = s->priv_data;
    AVStream *st = s->streams[0];
    int read_size, ret, offset = 0, buff_offset = 0;
    int64_t pos = avio_tell(s->pb);

    if (ctx->counter == 0)
        dss_skip_audio_header(s, pkt);

    if (ctx->swap) {
        read_size   = DSS_FRAME_SIZE - 2;
        buff_offset = 3;
    } else
        read_size = DSS_FRAME_SIZE;

    ctx->counter -= read_size;
    ctx->packet_size = DSS_FRAME_SIZE - 1;

    ret = av_new_packet(pkt, DSS_FRAME_SIZE);
    if (ret < 0)
        return ret;

    pkt->duration     = 264;
    pkt->pos = pos;
    pkt->stream_index = 0;
    s->bit_rate = 8LL * ctx->packet_size * st->codecpar->sample_rate * 512 / (506 * pkt->duration);

    if (ctx->counter < 0) {
        int size2 = ctx->counter + read_size;

        ret = avio_read(s->pb, ctx->dss_sp_buf + offset + buff_offset,
                        size2 - offset);
        if (ret < size2 - offset)
            goto error_eof;

        dss_skip_audio_header(s, pkt);
        offset = size2;
    }

    ret = avio_read(s->pb, ctx->dss_sp_buf + offset + buff_offset,
                    read_size - offset);
    if (ret < read_size - offset)
        goto error_eof;

    dss_sp_byte_swap(ctx, pkt->data, ctx->dss_sp_buf);

    if (ctx->dss_sp_swap_byte < 0) {
        ret = AVERROR(EAGAIN);
        goto error_eof;
    }

    return pkt->size;

error_eof:
    av_packet_unref(pkt);
    return ret < 0 ? ret : AVERROR_EOF;
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
    ctx->counter -= size;

    ret = av_new_packet(pkt, size);
    if (ret < 0)
        return ret;
    pkt->pos = pos;

    pkt->data[0]  = byte;
    offset        = 1;
    pkt->duration = 240;
    s->bit_rate = 8LL * size * st->codecpar->sample_rate * 512 / (506 * pkt->duration);

    pkt->stream_index = 0;

    if (ctx->counter < 0) {
        int size2 = ctx->counter + size;

        ret = avio_read(s->pb, pkt->data + offset,
                        size2 - offset);
        if (ret < size2 - offset) {
            av_packet_unref(pkt);
            return ret < 0 ? ret : AVERROR_EOF;
        }

        dss_skip_audio_header(s, pkt);
        offset = size2;
    }

    ret = avio_read(s->pb, pkt->data + offset, size - offset);
    if (ret < size - offset) {
        av_packet_unref(pkt);
        return ret < 0 ? ret : AVERROR_EOF;
    }

    return pkt->size;
}

static int dss_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    DSSDemuxContext *ctx = s->priv_data;

    if (ctx->audio_codec == DSS_ACODEC_DSS_SP)
        return dss_sp_read_packet(s, pkt);
    else
        return dss_723_1_read_packet(s, pkt);
}

static int dss_read_close(AVFormatContext *s)
{
    DSSDemuxContext *ctx = s->priv_data;

    av_freep(&ctx->dss_sp_buf);

    return 0;
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

    avio_read(s->pb, header, DSS_AUDIO_BLOCK_HEADER_SIZE);
    ctx->swap = !!(header[0] & 0x80);
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


AVInputFormat ff_dss_demuxer = {
    .name           = "dss",
    .long_name      = NULL_IF_CONFIG_SMALL("Digital Speech Standard (DSS)"),
    .priv_data_size = sizeof(DSSDemuxContext),
    .read_probe     = dss_probe,
    .read_header    = dss_read_header,
    .read_packet    = dss_read_packet,
    .read_close     = dss_read_close,
    .read_seek      = dss_read_seek,
    .extensions     = "dss"
};
