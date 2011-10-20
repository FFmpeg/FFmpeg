/*
 * WavPack demuxer
 * Copyright (c) 2006,2011 Konstantin Shishkov
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

#include "libavutil/audioconvert.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "avformat.h"
#include "apetag.h"
#include "id3v1.h"

// specs say that maximum block size is 1Mb
#define WV_BLOCK_LIMIT 1047576

#define WV_EXTRA_SIZE 12

#define WV_START_BLOCK  0x0800
#define WV_END_BLOCK    0x1000
#define WV_SINGLE_BLOCK (WV_START_BLOCK | WV_END_BLOCK)

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
    uint32_t chmask;
    uint32_t samples, soff;
    int multichannel;
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

static int wv_read_block_header(AVFormatContext *ctx, AVIOContext *pb, int append)
{
    WVContext *wc = ctx->priv_data;
    uint32_t tag, ver;
    int size;
    int rate, bpp, chan;
    uint32_t chmask;

    wc->pos = avio_tell(pb);
    if(!append){
        tag = avio_rl32(pb);
        if (tag != MKTAG('w', 'v', 'p', 'k'))
            return -1;
        size = avio_rl32(pb);
        if(size < 24 || size > WV_BLOCK_LIMIT){
            av_log(ctx, AV_LOG_ERROR, "Incorrect block size %i\n", size);
            return -1;
        }
        wc->blksize = size;
        ver = avio_rl16(pb);
        if(ver < 0x402 || ver > 0x410){
            av_log(ctx, AV_LOG_ERROR, "Unsupported version %03X\n", ver);
            return -1;
        }
        avio_r8(pb); // track no
        avio_r8(pb); // track sub index
        wc->samples = avio_rl32(pb); // total samples in file
        wc->soff = avio_rl32(pb); // offset in samples of current block
        avio_read(pb, wc->extra, WV_EXTRA_SIZE);
    }else{
        size = wc->blksize;
    }
    wc->flags = AV_RL32(wc->extra + 4);
    // blocks with zero samples don't contain actual audio information and should be ignored
    if (!AV_RN32(wc->extra))
        return 0;
    //parse flags
    bpp = ((wc->flags & 3) + 1) << 3;
    chan = 1 + !(wc->flags & WV_MONO);
    chmask = wc->flags & WV_MONO ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    rate = wv_rates[(wc->flags >> 23) & 0xF];
    wc->multichannel = !!((wc->flags & WV_SINGLE_BLOCK) != WV_SINGLE_BLOCK);
    if(wc->multichannel){
        chan = wc->chan;
        chmask = wc->chmask;
    }
    if((rate == -1 || !chan) && !wc->block_parsed){
        int64_t block_end = avio_tell(pb) + wc->blksize - 24;
        if(!pb->seekable){
            av_log(ctx, AV_LOG_ERROR, "Cannot determine additional parameters\n");
            return -1;
        }
        while(avio_tell(pb) < block_end){
            int id, size;
            id = avio_r8(pb);
            size = (id & 0x80) ? avio_rl24(pb) : avio_r8(pb);
            size <<= 1;
            if(id&0x40)
                size--;
            switch(id&0x3F){
            case 0xD:
                if(size <= 1){
                    av_log(ctx, AV_LOG_ERROR, "Insufficient channel information\n");
                    return -1;
                }
                chan = avio_r8(pb);
                switch(size - 2){
                case 0:
                    chmask = avio_r8(pb);
                    break;
                case 1:
                    chmask = avio_rl16(pb);
                    break;
                case 2:
                    chmask = avio_rl24(pb);
                    break;
                case 3:
                    chmask = avio_rl32(pb);
                    break;
                case 5:
                    avio_skip(pb, 1);
                    chan |= (avio_r8(pb) & 0xF) << 8;
                    chmask = avio_rl24(pb);
                    break;
                default:
                    av_log(ctx, AV_LOG_ERROR, "Invalid channel info size %d\n", size);
                    return -1;
                }
                break;
            case 0x27:
                rate = avio_rl24(pb);
                break;
            default:
                avio_skip(pb, size);
            }
            if(id&0x40)
                avio_skip(pb, 1);
        }
        if(rate == -1){
            av_log(ctx, AV_LOG_ERROR, "Cannot determine custom sampling rate\n");
            return -1;
        }
        avio_seek(pb, block_end - wc->blksize + 24, SEEK_SET);
    }
    if(!wc->bpp) wc->bpp = bpp;
    if(!wc->chan) wc->chan = chan;
    if(!wc->chmask) wc->chmask = chmask;
    if(!wc->rate) wc->rate = rate;

    if(wc->flags && bpp != wc->bpp){
        av_log(ctx, AV_LOG_ERROR, "Bits per sample differ, this block: %i, header block: %i\n", bpp, wc->bpp);
        return -1;
    }
    if(wc->flags && !wc->multichannel && chan != wc->chan){
        av_log(ctx, AV_LOG_ERROR, "Channels differ, this block: %i, header block: %i\n", chan, wc->chan);
        return -1;
    }
    if(wc->flags && rate != -1 && rate != wc->rate){
        av_log(ctx, AV_LOG_ERROR, "Sampling rate differ, this block: %i, header block: %i\n", rate, wc->rate);
        return -1;
    }
    wc->blksize = size - 24;
    return 0;
}

static int wv_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    AVIOContext *pb = s->pb;
    WVContext *wc = s->priv_data;
    AVStream *st;

    wc->block_parsed = 0;
    for(;;){
        if(wv_read_block_header(s, pb, 0) < 0)
            return -1;
        if(!AV_RN32(wc->extra))
            avio_skip(pb, wc->blksize - 24);
        else
            break;
    }

    /* now we are ready: build format streams */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return -1;
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = CODEC_ID_WAVPACK;
    st->codec->channels = wc->chan;
    st->codec->channel_layout = wc->chmask;
    st->codec->sample_rate = wc->rate;
    st->codec->bits_per_coded_sample = wc->bpp;
    av_set_pts_info(st, 64, 1, wc->rate);
    st->start_time = 0;
    st->duration = wc->samples;

    if(s->pb->seekable) {
        int64_t cur = avio_tell(s->pb);
        ff_ape_parse_tag(s);
        if(!av_dict_get(s->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX))
            ff_id3v1_read(s);
        avio_seek(s->pb, cur, SEEK_SET);
    }

    return 0;
}

static int wv_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    WVContext *wc = s->priv_data;
    int ret;
    int size, ver, off;
    int64_t pos;

    if (url_feof(s->pb))
        return AVERROR(EIO);
    if(wc->block_parsed){
        if(wv_read_block_header(s, s->pb, 0) < 0)
            return -1;
    }

    pos = wc->pos;
    off = wc->multichannel ? 4 : 0;
    if(av_new_packet(pkt, wc->blksize + WV_EXTRA_SIZE + off) < 0)
        return AVERROR(ENOMEM);
    if(wc->multichannel)
        AV_WL32(pkt->data, wc->blksize + WV_EXTRA_SIZE + 12);
    memcpy(pkt->data + off, wc->extra, WV_EXTRA_SIZE);
    ret = avio_read(s->pb, pkt->data + WV_EXTRA_SIZE + off, wc->blksize);
    if(ret != wc->blksize){
        av_free_packet(pkt);
        return AVERROR(EIO);
    }
    while(!(wc->flags & WV_END_BLOCK)){
        if(avio_rl32(s->pb) != MKTAG('w', 'v', 'p', 'k')){
            av_free_packet(pkt);
            return -1;
        }
        if((ret = av_append_packet(s->pb, pkt, 4)) < 0){
            av_free_packet(pkt);
            return ret;
        }
        size = AV_RL32(pkt->data + pkt->size - 4);
        if(size < 24 || size > WV_BLOCK_LIMIT){
            av_free_packet(pkt);
            av_log(s, AV_LOG_ERROR, "Incorrect block size %d\n", size);
            return -1;
        }
        wc->blksize = size;
        ver = avio_rl16(s->pb);
        if(ver < 0x402 || ver > 0x410){
            av_free_packet(pkt);
            av_log(s, AV_LOG_ERROR, "Unsupported version %03X\n", ver);
            return -1;
        }
        avio_r8(s->pb); // track no
        avio_r8(s->pb); // track sub index
        wc->samples = avio_rl32(s->pb); // total samples in file
        wc->soff = avio_rl32(s->pb); // offset in samples of current block
        if((ret = av_append_packet(s->pb, pkt, WV_EXTRA_SIZE)) < 0){
            av_free_packet(pkt);
            return ret;
        }
        memcpy(wc->extra, pkt->data + pkt->size - WV_EXTRA_SIZE, WV_EXTRA_SIZE);

        if(wv_read_block_header(s, s->pb, 1) < 0){
            av_free_packet(pkt);
            return -1;
        }
        ret = av_append_packet(s->pb, pkt, wc->blksize);
        if(ret < 0){
            av_free_packet(pkt);
            return ret;
        }
    }
    pkt->stream_index = 0;
    wc->block_parsed = 1;
    pkt->pts = wc->soff;
    av_add_index_entry(s->streams[0], pos, pkt->pts, 0, 0, AVINDEX_KEYFRAME);
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
    if (index >= 0 &&
        timestamp <= st->index_entries[st->nb_index_entries - 1].timestamp) {
        wc->block_parsed = 1;
        avio_seek(s->pb, st->index_entries[index].pos, SEEK_SET);
        return 0;
    }
    /* if timestamp is out of bounds, return error */
    if(timestamp < 0 || timestamp >= s->duration)
        return -1;

    pos = avio_tell(s->pb);
    do{
        ret = av_read_frame(s, pkt);
        if (ret < 0){
            avio_seek(s->pb, pos, SEEK_SET);
            return -1;
        }
        pts = pkt->pts;
        av_free_packet(pkt);
    }while(pts < timestamp);
    return 0;
}

AVInputFormat ff_wv_demuxer = {
    .name           = "wv",
    .long_name      = NULL_IF_CONFIG_SMALL("WavPack"),
    .priv_data_size = sizeof(WVContext),
    .read_probe     = wv_probe,
    .read_header    = wv_read_header,
    .read_packet    = wv_read_packet,
    .read_seek      = wv_read_seek,
};
