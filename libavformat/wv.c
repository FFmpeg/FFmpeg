/*
 * WavPack demuxer
 * Copyright (c) 2006 Konstantin Shishkov.
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

#include "avformat.h"
#include "bswap.h"

// specs say that maximum block size is 1Mb
#define WV_BLOCK_LIMIT 1047576

#define WV_EXTRA_SIZE 12

enum WV_FLAGS{
    WV_MONO   = 0x0004,
    WV_HYBRID = 0x0008,
    WV_JOINT  = 0x0010,
    WV_CROSSD = 0x0020,
    WV_HSHAPE = 0x0040,
    WV_FLOAT  = 0x0080,
    WV_INT32  = 0x0100,
    WV_HBR    = 0x0200,
    WV_HBAL   = 0x0400,
    WV_MCINIT = 0x0800,
    WV_MCEND  = 0x1000,
};

static const int wv_rates[16] = {
     6000,  8000,  9600, 11025, 12000, 16000, 22050, 24000,
    32000, 44100, 48000, 64000, 88200, 96000, 192000, -1
};

typedef struct{
    uint32_t blksize, flags;
    int rate, chan, bpp;
    uint32_t samples, soff;
    int block_parsed;
    uint8_t extra[WV_EXTRA_SIZE];
    int64_t pos;
}WVContext;

static int wv_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if (p->buf[0] == 'w' && p->buf[1] == 'v' &&
        p->buf[2] == 'p' && p->buf[3] == 'k')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int wv_read_block_header(AVFormatContext *ctx, ByteIOContext *pb)
{
    WVContext *wc = ctx->priv_data;
    uint32_t tag, ver;
    int size;
    int rate, bpp, chan;

    wc->pos = url_ftell(pb);
    tag = get_le32(pb);
    if (tag != MKTAG('w', 'v', 'p', 'k'))
        return -1;
    size = get_le32(pb);
    if(size < 24 || size > WV_BLOCK_LIMIT){
        av_log(ctx, AV_LOG_ERROR, "Incorrect block size %i\n", size);
        return -1;
    }
    wc->blksize = size;
    ver = get_le16(pb);
    if(ver < 0x402 || ver > 0x410){
        av_log(ctx, AV_LOG_ERROR, "Unsupported version %03X\n", ver);
        return -1;
    }
    get_byte(pb); // track no
    get_byte(pb); // track sub index
    wc->samples = get_le32(pb); // total samples in file
    wc->soff = get_le32(pb); // offset in samples of current block
    get_buffer(pb, wc->extra, WV_EXTRA_SIZE);
    wc->flags = AV_RL32(wc->extra + 4);
    //parse flags
    if(wc->flags & WV_FLOAT){
        av_log(ctx, AV_LOG_ERROR, "Floating point data is not supported\n");
        return -1;
    }
    if(wc->flags & WV_HYBRID){
        av_log(ctx, AV_LOG_ERROR, "Hybrid coding mode is not supported\n");
        return -1;
    }

    bpp = ((wc->flags & 3) + 1) << 3;
    chan = 1 + !(wc->flags & WV_MONO);
    rate = wv_rates[(wc->flags >> 23) & 0xF];
    if(rate == -1){
        av_log(ctx, AV_LOG_ERROR, "Unknown sampling rate\n");
        return -1;
    }
    if(!wc->bpp) wc->bpp = bpp;
    if(!wc->chan) wc->chan = chan;
    if(!wc->rate) wc->rate = rate;

    if(wc->flags && bpp != wc->bpp){
        av_log(ctx, AV_LOG_ERROR, "Bits per sample differ, this block: %i, header block: %i\n", bpp, wc->bpp);
        return -1;
    }
    if(wc->flags && chan != wc->chan){
        av_log(ctx, AV_LOG_ERROR, "Channels differ, this block: %i, header block: %i\n", chan, wc->chan);
        return -1;
    }
    if(wc->flags && rate != wc->rate){
        av_log(ctx, AV_LOG_ERROR, "Sampling rate differ, this block: %i, header block: %i\n", rate, wc->rate);
        return -1;
    }
    wc->blksize = size - 24;
    return 0;
}

static int wv_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    ByteIOContext *pb = s->pb;
    WVContext *wc = s->priv_data;
    AVStream *st;

    if(wv_read_block_header(s, pb) < 0)
        return -1;

    wc->block_parsed = 0;
    /* now we are ready: build format streams */
    st = av_new_stream(s, 0);
    if (!st)
        return -1;
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_WAVPACK;
    st->codec->channels = wc->chan;
    st->codec->sample_rate = wc->rate;
    st->codec->bits_per_sample = wc->bpp;
    av_set_pts_info(st, 64, 1, wc->rate);
    s->start_time = 0;
    s->duration = (int64_t)wc->samples * AV_TIME_BASE / st->codec->sample_rate;
    return 0;
}

static int wv_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    WVContext *wc = s->priv_data;
    int ret;

    if (url_feof(s->pb))
        return AVERROR(EIO);
    if(wc->block_parsed){
        if(wv_read_block_header(s, s->pb) < 0)
            return -1;
    }

    if(av_new_packet(pkt, wc->blksize + WV_EXTRA_SIZE) < 0)
        return AVERROR(ENOMEM);
    memcpy(pkt->data, wc->extra, WV_EXTRA_SIZE);
    ret = get_buffer(s->pb, pkt->data + WV_EXTRA_SIZE, wc->blksize);
    if(ret != wc->blksize){
        av_free_packet(pkt);
        return AVERROR(EIO);
    }
    pkt->stream_index = 0;
    wc->block_parsed = 1;
    pkt->size = ret + WV_EXTRA_SIZE;
    pkt->pts = wc->soff;
    av_add_index_entry(s->streams[0], wc->pos, pkt->pts, 0, 0, AVINDEX_KEYFRAME);
    return 0;
}

static int wv_read_close(AVFormatContext *s)
{
    return 0;
}

static int wv_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    AVStream *st = s->streams[stream_index];
    WVContext *wc = s->priv_data;
    AVPacket pkt1, *pkt = &pkt1;
    int ret;
    int index = av_index_search_timestamp(st, timestamp, flags);
    int64_t pos, pts;

    /* if found, seek there */
    if (index >= 0){
        wc->block_parsed = 1;
        url_fseek(s->pb, st->index_entries[index].pos, SEEK_SET);
        return 0;
    }
    /* if timestamp is out of bounds, return error */
    if(timestamp < 0 || timestamp >= s->duration)
        return -1;

    pos = url_ftell(s->pb);
    do{
        ret = av_read_frame(s, pkt);
        if (ret < 0){
            url_fseek(s->pb, pos, SEEK_SET);
            return -1;
        }
        pts = pkt->pts;
        av_free_packet(pkt);
    }while(pts < timestamp);
    return 0;
}

AVInputFormat wv_demuxer = {
    "wv",
    "WavPack",
    sizeof(WVContext),
    wv_probe,
    wv_read_header,
    wv_read_packet,
    wv_read_close,
    wv_read_seek,
};
