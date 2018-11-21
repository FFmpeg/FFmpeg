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

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/dict.h"
#include "avformat.h"
#include "internal.h"
#include "apetag.h"
#include "id3v1.h"
#include "wv.h"

enum WV_FLAGS {
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
    WV_DSD    = 0x80000000,
};

static const int wv_rates[16] = {
     6000,  8000,  9600, 11025, 12000, 16000,  22050, 24000,
    32000, 44100, 48000, 64000, 88200, 96000, 192000,    -1
};

typedef struct WVContext {
    uint8_t block_header[WV_HEADER_SIZE];
    WvHeader header;
    int rate, chan, bpp;
    uint32_t chmask;
    int multichannel;
    int block_parsed;
    int64_t pos;

    int64_t apetag_start;
} WVContext;

static int wv_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if (AV_RL32(&p->buf[0]) == MKTAG('w', 'v', 'p', 'k') &&
        AV_RL32(&p->buf[4]) >= 24 &&
        AV_RL32(&p->buf[4]) <= WV_BLOCK_LIMIT &&
        AV_RL16(&p->buf[8]) >= 0x402 &&
        AV_RL16(&p->buf[8]) <= 0x410)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int wv_read_block_header(AVFormatContext *ctx, AVIOContext *pb)
{
    WVContext *wc = ctx->priv_data;
    int ret;
    int rate, bpp, chan;
    uint32_t chmask, flags;

    wc->pos = avio_tell(pb);

    /* don't return bogus packets with the ape tag data */
    if (wc->apetag_start && wc->pos >= wc->apetag_start)
        return AVERROR_EOF;

    ret = avio_read(pb, wc->block_header, WV_HEADER_SIZE);
    if (ret != WV_HEADER_SIZE)
        return (ret < 0) ? ret : AVERROR_EOF;

    ret = ff_wv_parse_header(&wc->header, wc->block_header);
    if (ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Invalid block header.\n");
        return ret;
    }

    if (wc->header.flags & WV_DSD) {
        avpriv_report_missing_feature(ctx, "WV DSD");
        return AVERROR_PATCHWELCOME;
    }

    if (wc->header.version < 0x402 || wc->header.version > 0x410) {
        avpriv_report_missing_feature(ctx, "WV version 0x%03X",
                                      wc->header.version);
        return AVERROR_PATCHWELCOME;
    }

    /* Blocks with zero samples don't contain actual audio information
     * and should be ignored */
    if (!wc->header.samples)
        return 0;
    // parse flags
    flags  = wc->header.flags;
    bpp    = ((flags & 3) + 1) << 3;
    chan   = 1 + !(flags & WV_MONO);
    chmask = flags & WV_MONO ? AV_CH_LAYOUT_MONO : AV_CH_LAYOUT_STEREO;
    rate   = wv_rates[(flags >> 23) & 0xF];
    wc->multichannel = !(wc->header.initial && wc->header.final);
    if (wc->multichannel) {
        chan   = wc->chan;
        chmask = wc->chmask;
    }
    if ((rate == -1 || !chan) && !wc->block_parsed) {
        int64_t block_end = avio_tell(pb) + wc->header.blocksize;
        if (!(pb->seekable & AVIO_SEEKABLE_NORMAL)) {
            av_log(ctx, AV_LOG_ERROR,
                   "Cannot determine additional parameters\n");
            return AVERROR_INVALIDDATA;
        }
        while (avio_tell(pb) < block_end && !avio_feof(pb)) {
            int id, size;
            id   = avio_r8(pb);
            size = (id & 0x80) ? avio_rl24(pb) : avio_r8(pb);
            size <<= 1;
            if (id & 0x40)
                size--;
            switch (id & 0x3F) {
            case 0xD:
                if (size <= 1) {
                    av_log(ctx, AV_LOG_ERROR,
                           "Insufficient channel information\n");
                    return AVERROR_INVALIDDATA;
                }
                chan = avio_r8(pb);
                switch (size - 2) {
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
                case 4:
                    avio_skip(pb, 1);
                    chan  |= (avio_r8(pb) & 0xF) << 8;
                    chan  += 1;
                    chmask = avio_rl24(pb);
                    break;
                case 5:
                    avio_skip(pb, 1);
                    chan  |= (avio_r8(pb) & 0xF) << 8;
                    chan  += 1;
                    chmask = avio_rl32(pb);
                    break;
                default:
                    av_log(ctx, AV_LOG_ERROR,
                           "Invalid channel info size %d\n", size);
                    return AVERROR_INVALIDDATA;
                }
                break;
            case 0x27:
                rate = avio_rl24(pb);
                break;
            default:
                avio_skip(pb, size);
            }
            if (id & 0x40)
                avio_skip(pb, 1);
        }
        if (rate == -1) {
            av_log(ctx, AV_LOG_ERROR,
                   "Cannot determine custom sampling rate\n");
            return AVERROR_INVALIDDATA;
        }
        avio_seek(pb, block_end - wc->header.blocksize, SEEK_SET);
    }
    if (!wc->bpp)
        wc->bpp    = bpp;
    if (!wc->chan)
        wc->chan   = chan;
    if (!wc->chmask)
        wc->chmask = chmask;
    if (!wc->rate)
        wc->rate   = rate;

    if (flags && bpp != wc->bpp) {
        av_log(ctx, AV_LOG_ERROR,
               "Bits per sample differ, this block: %i, header block: %i\n",
               bpp, wc->bpp);
        return AVERROR_INVALIDDATA;
    }
    if (flags && !wc->multichannel && chan != wc->chan) {
        av_log(ctx, AV_LOG_ERROR,
               "Channels differ, this block: %i, header block: %i\n",
               chan, wc->chan);
        return AVERROR_INVALIDDATA;
    }
    if (flags && rate != -1 && rate != wc->rate) {
        av_log(ctx, AV_LOG_ERROR,
               "Sampling rate differ, this block: %i, header block: %i\n",
               rate, wc->rate);
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

static int wv_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    WVContext *wc = s->priv_data;
    AVStream *st;
    int ret;

    wc->block_parsed = 0;
    for (;;) {
        if ((ret = wv_read_block_header(s, pb)) < 0)
            return ret;
        if (!wc->header.samples)
            avio_skip(pb, wc->header.blocksize);
        else
            break;
    }

    /* now we are ready: build format streams */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->codecpar->codec_type            = AVMEDIA_TYPE_AUDIO;
    st->codecpar->codec_id              = AV_CODEC_ID_WAVPACK;
    st->codecpar->channels              = wc->chan;
    st->codecpar->channel_layout        = wc->chmask;
    st->codecpar->sample_rate           = wc->rate;
    st->codecpar->bits_per_coded_sample = wc->bpp;
    avpriv_set_pts_info(st, 64, 1, wc->rate);
    st->start_time = 0;
    if (wc->header.total_samples != 0xFFFFFFFFu)
        st->duration = wc->header.total_samples;

    if (s->pb->seekable & AVIO_SEEKABLE_NORMAL) {
        int64_t cur = avio_tell(s->pb);
        wc->apetag_start = ff_ape_parse_tag(s);
        if (!av_dict_get(s->metadata, "", NULL, AV_DICT_IGNORE_SUFFIX))
            ff_id3v1_read(s);
        avio_seek(s->pb, cur, SEEK_SET);
    }

    return 0;
}

static int wv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    WVContext *wc = s->priv_data;
    int ret;
    int off;
    int64_t pos;
    uint32_t block_samples;

    if (avio_feof(s->pb))
        return AVERROR_EOF;
    if (wc->block_parsed) {
        if ((ret = wv_read_block_header(s, s->pb)) < 0)
            return ret;
    }

    pos = wc->pos;
    if (av_new_packet(pkt, wc->header.blocksize + WV_HEADER_SIZE) < 0)
        return AVERROR(ENOMEM);
    memcpy(pkt->data, wc->block_header, WV_HEADER_SIZE);
    ret = avio_read(s->pb, pkt->data + WV_HEADER_SIZE, wc->header.blocksize);
    if (ret != wc->header.blocksize) {
        av_packet_unref(pkt);
        return AVERROR(EIO);
    }
    while (!(wc->header.flags & WV_FLAG_FINAL_BLOCK)) {
        if ((ret = wv_read_block_header(s, s->pb)) < 0) {
            av_packet_unref(pkt);
            return ret;
        }

        off = pkt->size;
        if ((ret = av_grow_packet(pkt, WV_HEADER_SIZE + wc->header.blocksize)) < 0) {
            av_packet_unref(pkt);
            return ret;
        }
        memcpy(pkt->data + off, wc->block_header, WV_HEADER_SIZE);

        ret = avio_read(s->pb, pkt->data + off + WV_HEADER_SIZE, wc->header.blocksize);
        if (ret != wc->header.blocksize) {
            av_packet_unref(pkt);
            return (ret < 0) ? ret : AVERROR_EOF;
        }
    }
    pkt->stream_index = 0;
    pkt->pos          = pos;
    wc->block_parsed  = 1;
    pkt->pts          = wc->header.block_idx;
    block_samples     = wc->header.samples;
    if (block_samples > INT32_MAX)
        av_log(s, AV_LOG_WARNING,
               "Too many samples in block: %"PRIu32"\n", block_samples);
    else
        pkt->duration = block_samples;

    return 0;
}

AVInputFormat ff_wv_demuxer = {
    .name           = "wv",
    .long_name      = NULL_IF_CONFIG_SMALL("WavPack"),
    .priv_data_size = sizeof(WVContext),
    .read_probe     = wv_probe,
    .read_header    = wv_read_header,
    .read_packet    = wv_read_packet,
    .flags          = AVFMT_GENERIC_INDEX,
};
