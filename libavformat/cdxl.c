/*
 * CDXL demuxer
 * Copyright (c) 2011-2012 Paul B Mahol
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
#include "libavutil/parseutils.h"
#include "libavutil/opt.h"
#include "avformat.h"
#include "internal.h"

#define CDXL_HEADER_SIZE 32

typedef struct CDXLDemuxContext {
    AVClass     *class;
    int         read_chunk;
    int         frate;
    int         srate;
    uint8_t     header[CDXL_HEADER_SIZE];
    int         video_stream_index;
    int         audio_stream_index;
    int64_t     filesize;
} CDXLDemuxContext;

static int cdxl_read_probe(const AVProbeData *p)
{
    int score = AVPROBE_SCORE_EXTENSION + 10;
    const uint8_t *buf = p->buf;

    if (p->buf_size < CDXL_HEADER_SIZE)
        return 0;

    /* check type */
    if (buf[0] > 1)
        return 0;

    /* reserved bytes should always be set to 0 */
    if (AV_RL24(&buf[29]))
        return 0;

    /* check palette size */
    if (!AV_RN16(&buf[20]))
        return 0;
    if (buf[0] == 1 && AV_RB16(&buf[20]) > 512)
        return 0;
    if (buf[0] == 0 && AV_RB16(&buf[20]) > 768)
        return 0;

    if (!AV_RN16(&buf[22]) && AV_RN16(&buf[24]))
        return 0;

    if (buf[0] == 0 && (!buf[26] || !AV_RB16(&buf[24])))
        return 0;

    /* check number of planes */
    if (buf[19] != 6 && buf[19] != 8 && buf[19] != 24)
        return 0;

    if (buf[18])
        return 0;

    /* check widh and height */
    if (AV_RB16(&buf[14]) > 640 || AV_RB16(&buf[16]) > 480 ||
        AV_RB16(&buf[14]) == 0 || AV_RB16(&buf[16]) == 0)
        return 0;

    /* chunk size */
    if (AV_RB32(&buf[2]) <= AV_RB16(&buf[20]) + AV_RB16(&buf[22]) * (1 + !!(buf[1] & 0x10)) + CDXL_HEADER_SIZE)
        return 0;

    /* previous chunk size */
    if (AV_RN32(&buf[6]))
        score /= 2;

    /* current frame number, usually starts from 1 */
    if (AV_RB32(&buf[10]) != 1)
        score /= 2;

    return score;
}

static int cdxl_read_header(AVFormatContext *s)
{
    CDXLDemuxContext *cdxl = s->priv_data;

    cdxl->read_chunk         =  0;
    cdxl->video_stream_index = -1;
    cdxl->audio_stream_index = -1;

    cdxl->filesize = avio_size(s->pb);

    s->ctx_flags |= AVFMTCTX_NOHEADER;

    return 0;
}

static int cdxl_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    CDXLDemuxContext *cdxl = s->priv_data;
    AVIOContext *pb = s->pb;
    uint32_t current_size, video_size, image_size;
    uint16_t audio_size, palette_size, width, height;
    int64_t  pos;
    int      type, format, frames, ret;

    if (avio_feof(pb))
        return AVERROR_EOF;

    pos = avio_tell(pb);
    if (!cdxl->read_chunk &&
        avio_read(pb, cdxl->header, CDXL_HEADER_SIZE) != CDXL_HEADER_SIZE)
        return AVERROR_EOF;
    if (cdxl->header[0] > 1) {
        av_log(s, AV_LOG_ERROR, "unsupported cdxl file\n");
        return AVERROR_INVALIDDATA;
    }

    type         = cdxl->header[0];
    format       = cdxl->header[1] & 0xE0;
    current_size = AV_RB32(&cdxl->header[2]);
    width        = AV_RB16(&cdxl->header[14]);
    height       = AV_RB16(&cdxl->header[16]);
    palette_size = AV_RB16(&cdxl->header[20]);
    audio_size   = AV_RB16(&cdxl->header[22]) * (1 + !!(cdxl->header[1] & 0x10));
    cdxl->srate  = AV_RB16(&cdxl->header[24]);
    if (!cdxl->srate)
        cdxl->srate = 11025;
    cdxl->frate  = cdxl->header[26];
    if (!cdxl->frate)
        cdxl->frate = 25;
    if (cdxl->header[19] == 0 ||
        FFALIGN(width, 16) * (uint64_t)height * cdxl->header[19] > INT_MAX)
        return AVERROR_INVALIDDATA;
    if (format == 0x20)
        image_size = width * height * cdxl->header[19] / 8;
    else
        image_size = FFALIGN(width, 16) * height * cdxl->header[19] / 8;
    video_size   = palette_size + image_size;

    if ((type == 1 && palette_size > 512) ||
        (type == 0 && palette_size > 768))
        return AVERROR_INVALIDDATA;
    if (current_size < (uint64_t)audio_size + video_size + CDXL_HEADER_SIZE)
        return AVERROR_INVALIDDATA;

    if (cdxl->read_chunk && audio_size) {
        if (cdxl->audio_stream_index == -1) {
            AVStream *st = avformat_new_stream(s, NULL);
            if (!st)
                return AVERROR(ENOMEM);

            st->codecpar->codec_type    = AVMEDIA_TYPE_AUDIO;
            st->codecpar->codec_tag     = 0;
            st->codecpar->codec_id      = AV_CODEC_ID_PCM_S8_PLANAR;
            if (cdxl->header[1] & 0x10) {
                st->codecpar->channels       = 2;
                st->codecpar->channel_layout = AV_CH_LAYOUT_STEREO;
            } else {
                st->codecpar->channels       = 1;
                st->codecpar->channel_layout = AV_CH_LAYOUT_MONO;
            }
            st->codecpar->sample_rate= cdxl->srate;
            st->start_time           = 0;
            cdxl->audio_stream_index = st->index;
            avpriv_set_pts_info(st, 64, 1, cdxl->srate);
        }

        ret = av_get_packet(pb, pkt, audio_size);
        if (ret < 0)
            return ret;
        pkt->stream_index = cdxl->audio_stream_index;
        pkt->pos          = pos;
        pkt->duration     = audio_size;
        cdxl->read_chunk  = 0;
    } else {
        if (cdxl->video_stream_index == -1) {
            AVStream *st = avformat_new_stream(s, NULL);
            if (!st)
                return AVERROR(ENOMEM);

            st->codecpar->codec_type    = AVMEDIA_TYPE_VIDEO;
            st->codecpar->codec_tag     = 0;
            st->codecpar->codec_id      = AV_CODEC_ID_CDXL;
            st->codecpar->width         = width;
            st->codecpar->height        = height;

            if (audio_size + video_size && cdxl->filesize > 0) {
                frames = cdxl->filesize / (audio_size + video_size);

                if (cdxl->frate)
                    st->duration = frames;
                else
                    st->duration = frames * (int64_t)audio_size;
            }
            st->start_time           = 0;
            cdxl->video_stream_index = st->index;
            if (cdxl->frate)
                avpriv_set_pts_info(st, 64, 1, cdxl->frate);
            else
                avpriv_set_pts_info(st, 64, 1, cdxl->srate);
        }

        if ((ret = av_new_packet(pkt, video_size + CDXL_HEADER_SIZE)) < 0)
            return ret;
        memcpy(pkt->data, cdxl->header, CDXL_HEADER_SIZE);
        ret = avio_read(pb, pkt->data + CDXL_HEADER_SIZE, video_size);
        if (ret < 0) {
            return ret;
        }
        av_shrink_packet(pkt, CDXL_HEADER_SIZE + ret);
        pkt->stream_index  = cdxl->video_stream_index;
        pkt->flags        |= AV_PKT_FLAG_KEY;
        pkt->pos           = pos;
        pkt->duration      = cdxl->frate ? 1 : audio_size ? audio_size : 220;
        cdxl->read_chunk   = audio_size;
    }

    if (!cdxl->read_chunk)
        avio_skip(pb, current_size - audio_size - video_size - CDXL_HEADER_SIZE);
    return ret;
}

AVInputFormat ff_cdxl_demuxer = {
    .name           = "cdxl",
    .long_name      = NULL_IF_CONFIG_SMALL("Commodore CDXL video"),
    .priv_data_size = sizeof(CDXLDemuxContext),
    .read_probe     = cdxl_read_probe,
    .read_header    = cdxl_read_header,
    .read_packet    = cdxl_read_packet,
    .extensions     = "cdxl,xl",
    .flags          = AVFMT_GENERIC_INDEX,
};
