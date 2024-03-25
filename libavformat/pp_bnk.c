/*
 * Pro Pinball Series Soundbank (44c, 22c, 11c, 5c) demuxer.
 *
 * Copyright (C) 2020 Zane van Iperen (zane@zanevaniperen.com)
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
#include "demux.h"
#include "internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/avassert.h"
#include "libavutil/channel_layout.h"
#include "libavutil/internal.h"
#include "libavutil/mem.h"

#define PP_BNK_MAX_READ_SIZE    4096
#define PP_BNK_FILE_HEADER_SIZE 20
#define PP_BNK_TRACK_SIZE       20

typedef struct PPBnkHeader {
    uint32_t        bank_id;        /*< Bank ID, useless for our purposes. */
    uint32_t        sample_rate;    /*< Sample rate of the contained tracks. */
    uint32_t        always1;        /*< Unknown, always seems to be 1. */
    uint32_t        track_count;    /*< Number of tracks in the file. */
    uint32_t        flags;          /*< Flags. */
} PPBnkHeader;

typedef struct PPBnkTrack {
    uint32_t        id;             /*< Track ID. Usually track[i].id == track[i-1].id + 1, but not always */
    uint32_t        size;           /*< Size of the data in bytes. */
    uint32_t        sample_rate;    /*< Sample rate. */
    uint32_t        always1_1;      /*< Unknown, always seems to be 1. */
    uint32_t        always1_2;      /*< Unknown, always seems to be 1. */
} PPBnkTrack;

typedef struct PPBnkCtxTrack {
    int64_t         data_offset;
    uint32_t        data_size;
    uint32_t        bytes_read;
} PPBnkCtxTrack;

typedef struct PPBnkCtx {
    int             track_count;
    PPBnkCtxTrack   *tracks;
    uint32_t        current_track;
    int             is_music;
} PPBnkCtx;

enum {
    PP_BNK_FLAG_PERSIST = (1 << 0), /*< This is a large file, keep in memory. */
    PP_BNK_FLAG_MUSIC   = (1 << 1), /*< This is music. */
    PP_BNK_FLAG_MASK    = (PP_BNK_FLAG_PERSIST | PP_BNK_FLAG_MUSIC)
};

static void pp_bnk_parse_header(PPBnkHeader *hdr, const uint8_t *buf)
{
    hdr->bank_id        = AV_RL32(buf +  0);
    hdr->sample_rate    = AV_RL32(buf +  4);
    hdr->always1        = AV_RL32(buf +  8);
    hdr->track_count    = AV_RL32(buf + 12);
    hdr->flags          = AV_RL32(buf + 16);
}

static void pp_bnk_parse_track(PPBnkTrack *trk, const uint8_t *buf)
{
    trk->id             = AV_RL32(buf +  0);
    trk->size           = AV_RL32(buf +  4);
    trk->sample_rate    = AV_RL32(buf +  8);
    trk->always1_1      = AV_RL32(buf + 12);
    trk->always1_2      = AV_RL32(buf + 16);
}

static int pp_bnk_probe(const AVProbeData *p)
{
    uint32_t sample_rate = AV_RL32(p->buf +  4);
    uint32_t track_count = AV_RL32(p->buf + 12);
    uint32_t flags       = AV_RL32(p->buf + 16);

    if (track_count == 0 || track_count > INT_MAX)
        return 0;

    if ((sample_rate !=  5512) && (sample_rate != 11025) &&
        (sample_rate != 22050) && (sample_rate != 44100))
        return 0;

    /* Check the first track header. */
    if (AV_RL32(p->buf + 28) != sample_rate)
        return 0;

    if ((flags & ~PP_BNK_FLAG_MASK) != 0)
        return 0;

    return AVPROBE_SCORE_MAX / 4 + 1;
}

static int pp_bnk_read_header(AVFormatContext *s)
{
    int64_t ret;
    AVStream *st;
    AVCodecParameters *par;
    PPBnkCtx *ctx = s->priv_data;
    uint8_t buf[FFMAX(PP_BNK_FILE_HEADER_SIZE, PP_BNK_TRACK_SIZE)];
    PPBnkHeader hdr;

    if ((ret = avio_read(s->pb, buf, PP_BNK_FILE_HEADER_SIZE)) < 0)
        return ret;
    else if (ret != PP_BNK_FILE_HEADER_SIZE)
        return AVERROR(EIO);

    pp_bnk_parse_header(&hdr, buf);

    if (hdr.track_count == 0 || hdr.track_count > INT_MAX)
        return AVERROR_INVALIDDATA;

    if (hdr.sample_rate == 0 || hdr.sample_rate > INT_MAX)
        return AVERROR_INVALIDDATA;

    if (hdr.always1 != 1) {
        avpriv_request_sample(s, "Non-one header value");
        return AVERROR_PATCHWELCOME;
    }

    ctx->track_count = hdr.track_count;

    if (!(ctx->tracks = av_malloc_array(hdr.track_count, sizeof(PPBnkCtxTrack))))
        return AVERROR(ENOMEM);

    /* Parse and validate each track. */
    for (int i = 0; i < hdr.track_count; i++) {
        PPBnkTrack e;
        PPBnkCtxTrack *trk = ctx->tracks + i;

        ret = avio_read(s->pb, buf, PP_BNK_TRACK_SIZE);
        if (ret < 0 && ret != AVERROR_EOF)
            return ret;

        /* Short byte-count or EOF, we have a truncated file. */
        if (ret != PP_BNK_TRACK_SIZE) {
            av_log(s, AV_LOG_WARNING, "File truncated at %d/%u track(s)\n",
                   i, hdr.track_count);
            ctx->track_count = i;
            break;
        }

        pp_bnk_parse_track(&e, buf);

        /* The individual sample rates of all tracks must match that of the file header. */
        if (e.sample_rate != hdr.sample_rate)
            return AVERROR_INVALIDDATA;

        if (e.always1_1 != 1 || e.always1_2 != 1) {
            avpriv_request_sample(s, "Non-one track header values");
            return AVERROR_PATCHWELCOME;
        }

        trk->data_offset = avio_tell(s->pb);
        trk->data_size   = e.size;
        trk->bytes_read  = 0;

        /*
         * Skip over the data to the next stream header.
         * Sometimes avio_skip() doesn't detect EOF. If it doesn't, either:
         *   - the avio_read() above will, or
         *   - pp_bnk_read_packet() will read a truncated last track.
         */
        if ((ret = avio_skip(s->pb, e.size)) == AVERROR_EOF) {
            ctx->track_count = i + 1;
            av_log(s, AV_LOG_WARNING,
                   "Track %d has truncated data, assuming track count == %d\n",
                   i, ctx->track_count);
            break;
        } else if (ret < 0) {
            return ret;
        }
    }

    /* File is only a header. */
    if (ctx->track_count == 0)
        return AVERROR_INVALIDDATA;

    ctx->is_music = (hdr.flags & PP_BNK_FLAG_MUSIC) &&
                    (ctx->track_count == 2) &&
                    (ctx->tracks[0].data_size == ctx->tracks[1].data_size);

    /* Build the streams. */
    for (int i = 0; i < (ctx->is_music ? 1 : ctx->track_count); i++) {
        if (!(st = avformat_new_stream(s, NULL)))
            return AVERROR(ENOMEM);

        par                         = st->codecpar;
        par->codec_type             = AVMEDIA_TYPE_AUDIO;
        par->codec_id               = AV_CODEC_ID_ADPCM_IMA_CUNNING;
        par->format                 = AV_SAMPLE_FMT_S16P;

        av_channel_layout_default(&par->ch_layout, ctx->is_music + 1);
        par->sample_rate            = hdr.sample_rate;
        par->bits_per_coded_sample  = 4;
        par->block_align            = 1;
        par->bit_rate               = par->sample_rate * (int64_t)par->bits_per_coded_sample *
                                      par->ch_layout.nb_channels;

        avpriv_set_pts_info(st, 64, 1, par->sample_rate);
        st->start_time              = 0;
        st->duration                = ctx->tracks[i].data_size * 2;
    }

    return 0;
}

static int pp_bnk_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    PPBnkCtx *ctx = s->priv_data;

    /*
     * Read a packet from each track, round-robin style.
     * This method is nasty, but needed to avoid "Too many packets buffered" errors.
     */
    for (int i = 0; i < ctx->track_count; i++, ctx->current_track++)
    {
        int64_t ret;
        int size;
        PPBnkCtxTrack *trk;

        ctx->current_track %= ctx->track_count;

        trk = ctx->tracks + ctx->current_track;

        if (trk->bytes_read == trk->data_size)
            continue;

        if ((ret = avio_seek(s->pb, trk->data_offset + trk->bytes_read, SEEK_SET)) < 0)
            return ret;
        else if (ret != trk->data_offset + trk->bytes_read)
            return AVERROR(EIO);

        size = FFMIN(trk->data_size - trk->bytes_read, PP_BNK_MAX_READ_SIZE);

        if (!ctx->is_music) {
            ret = av_get_packet(s->pb, pkt, size);
            if (ret == AVERROR_EOF) {
                /* If we've hit EOF, don't attempt this track again. */
                trk->data_size = trk->bytes_read;
                continue;
            }
        } else {
            if (!pkt->data && (ret = av_new_packet(pkt, size * 2)) < 0)
                return ret;
            ret = avio_read(s->pb, pkt->data + size * ctx->current_track, size);
            if (ret >= 0 && ret != size) {
                /* Only return stereo packets if both tracks could be read. */
                ret = AVERROR_EOF;
            }
        }
        if (ret < 0)
            return ret;

        trk->bytes_read    += ret;
        pkt->flags         &= ~AV_PKT_FLAG_CORRUPT;
        pkt->stream_index   = ctx->current_track;
        pkt->duration       = ret * 2;

        if (ctx->is_music) {
            if (pkt->stream_index == 0)
                continue;

            pkt->stream_index = 0;
        }

        ctx->current_track++;
        return 0;
    }

    /* If we reach here, we're done. */
    return AVERROR_EOF;
}

static int pp_bnk_read_close(AVFormatContext *s)
{
    PPBnkCtx *ctx = s->priv_data;

    av_freep(&ctx->tracks);

    return 0;
}

static int pp_bnk_seek(AVFormatContext *s, int stream_index,
                       int64_t pts, int flags)
{
    PPBnkCtx *ctx = s->priv_data;

    if (pts != 0)
        return AVERROR(EINVAL);

    if (ctx->is_music) {
        av_assert0(stream_index == 0);
        ctx->tracks[0].bytes_read = 0;
        ctx->tracks[1].bytes_read = 0;
    } else {
        ctx->tracks[stream_index].bytes_read = 0;
    }

    return 0;
}

const FFInputFormat ff_pp_bnk_demuxer = {
    .p.name         = "pp_bnk",
    .p.long_name    = NULL_IF_CONFIG_SMALL("Pro Pinball Series Soundbank"),
    .priv_data_size = sizeof(PPBnkCtx),
    .flags_internal = FF_INFMT_FLAG_INIT_CLEANUP,
    .read_probe     = pp_bnk_probe,
    .read_header    = pp_bnk_read_header,
    .read_packet    = pp_bnk_read_packet,
    .read_close     = pp_bnk_read_close,
    .read_seek      = pp_bnk_seek,
};
