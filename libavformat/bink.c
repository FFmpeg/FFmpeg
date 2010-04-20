/*
 * Bink demuxer
 * Copyright (c) 2008-2010 Peter Ross (pross@xvid.org)
 * Copyright (c) 2009 Daniel Verkamp (daniel@drv.nu)
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
 * Bink demuxer
 *
 * Technical details here:
 *  http://wiki.multimedia.cx/index.php?title=Bink_Container
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

enum BinkAudFlags {
    BINK_AUD_16BITS = 0x4000, ///< prefer 16-bit output
    BINK_AUD_STEREO = 0x2000,
    BINK_AUD_USEDCT = 0x1000,
};

#define BINK_EXTRADATA_SIZE     1
#define BINK_MAX_AUDIO_TRACKS   256
#define BINK_MAX_WIDTH          7680
#define BINK_MAX_HEIGHT         4800

typedef struct {
    uint32_t file_size;

    uint32_t num_audio_tracks;
    int current_track;      ///< audio track to return in next packet
    int64_t video_pts;
    int64_t audio_pts[BINK_MAX_AUDIO_TRACKS];

    uint32_t remain_packet_size;
} BinkDemuxContext;

static int probe(AVProbeData *p)
{
    const uint8_t *b = p->buf;

    if ( b[0] == 'B' && b[1] == 'I' && b[2] == 'K' &&
        (b[3] == 'b' || b[3] == 'f' || b[3] == 'g' || b[3] == 'h' || b[3] == 'i') &&
        AV_RL32(b+8) > 0 &&  // num_frames
        AV_RL32(b+20) > 0 && AV_RL32(b+20) <= BINK_MAX_WIDTH &&
        AV_RL32(b+24) > 0 && AV_RL32(b+24) <= BINK_MAX_HEIGHT &&
        AV_RL32(b+28) > 0 && AV_RL32(b+32) > 0)  // fps num,den
        return AVPROBE_SCORE_MAX;
    return 0;
}

static int read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    BinkDemuxContext *bink = s->priv_data;
    ByteIOContext *pb = s->pb;
    uint32_t fps_num, fps_den;
    AVStream *vst, *ast;
    unsigned int i;
    uint32_t pos, next_pos;
    uint16_t flags;
    int keyframe;

    vst = av_new_stream(s, 0);
    if (!vst)
        return AVERROR(ENOMEM);

    vst->codec->codec_tag = get_le32(pb);

    bink->file_size = get_le32(pb) + 8;
    vst->duration   = get_le32(pb);

    if (vst->duration > 1000000) {
        av_log(s, AV_LOG_ERROR, "invalid header: more than 1000000 frames\n");
        return AVERROR(EIO);
    }

    if (get_le32(pb) > bink->file_size) {
        av_log(s, AV_LOG_ERROR,
               "invalid header: largest frame size greater than file size\n");
        return AVERROR(EIO);
    }

    url_fskip(pb, 4);

    vst->codec->width  = get_le32(pb);
    vst->codec->height = get_le32(pb);

    fps_num = get_le32(pb);
    fps_den = get_le32(pb);
    if (fps_num == 0 || fps_den == 0) {
        av_log(s, AV_LOG_ERROR, "invalid header: invalid fps (%d/%d)\n", fps_num, fps_den);
        return AVERROR(EIO);
    }
    av_set_pts_info(vst, 64, fps_den, fps_num);

    vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codec->codec_id   = CODEC_ID_BINKVIDEO;
    vst->codec->extradata  = av_mallocz(4 + FF_INPUT_BUFFER_PADDING_SIZE);
    vst->codec->extradata_size = 4;
    get_buffer(pb, vst->codec->extradata, 4);

    bink->num_audio_tracks = get_le32(pb);

    if (bink->num_audio_tracks > BINK_MAX_AUDIO_TRACKS) {
        av_log(s, AV_LOG_ERROR,
               "invalid header: more than "AV_STRINGIFY(BINK_MAX_AUDIO_TRACKS)" audio tracks (%d)\n",
               bink->num_audio_tracks);
        return AVERROR(EIO);
    }

    if (bink->num_audio_tracks) {
        url_fskip(pb, 4 * bink->num_audio_tracks);

        for (i = 0; i < bink->num_audio_tracks; i++) {
            ast = av_new_stream(s, 1);
            if (!ast)
                return AVERROR(ENOMEM);
            ast->codec->codec_type  = AVMEDIA_TYPE_AUDIO;
            ast->codec->codec_tag   = 0;
            ast->codec->sample_rate = get_le16(pb);
            av_set_pts_info(ast, 64, 1, ast->codec->sample_rate);
            flags = get_le16(pb);
            ast->codec->codec_id = flags & BINK_AUD_USEDCT ?
                                   CODEC_ID_BINKAUDIO_DCT : CODEC_ID_BINKAUDIO_RDFT;
            ast->codec->channels = flags & BINK_AUD_STEREO ? 2 : 1;
        }

        url_fskip(pb, 4 * bink->num_audio_tracks);
    }

    /* frame index table */
    next_pos = get_le32(pb);
    for (i = 0; i < vst->duration; i++) {
        pos = next_pos;
        if (i == vst->duration - 1) {
            next_pos = bink->file_size;
            keyframe = 0;
        } else {
            next_pos = get_le32(pb);
            keyframe = pos & 1;
        }
        pos &= ~1;
        next_pos &= ~1;

        if (next_pos <= pos) {
            av_log(s, AV_LOG_ERROR, "invalid frame index table\n");
            return AVERROR(EIO);
        }
        av_add_index_entry(vst, pos, i, next_pos - pos, 0,
                           keyframe ? AVINDEX_KEYFRAME : 0);
    }

    url_fskip(pb, 4);

    bink->current_track = -1;
    return 0;
}

static int read_packet(AVFormatContext *s, AVPacket *pkt)
{
    BinkDemuxContext *bink = s->priv_data;
    ByteIOContext *pb = s->pb;
    int ret;

    if (bink->current_track < 0) {
        int index_entry;
        AVStream *st = s->streams[0]; // stream 0 is video stream with index

        if (bink->video_pts >= st->duration)
            return AVERROR(EIO);

        index_entry = av_index_search_timestamp(st, bink->video_pts,
                                                AVSEEK_FLAG_ANY);
        if (index_entry < 0) {
            av_log(s, AV_LOG_ERROR,
                   "could not find index entry for frame %"PRId64"\n",
                   bink->video_pts);
            return AVERROR(EIO);
        }

        bink->remain_packet_size = st->index_entries[index_entry].size;
        bink->current_track = 0;
    }

    while (bink->current_track < bink->num_audio_tracks) {
        uint32_t audio_size = get_le32(pb);
        if (audio_size > bink->remain_packet_size - 4) {
            av_log(s, AV_LOG_ERROR,
                   "frame %"PRId64": audio size in header (%u) > size of packet left (%u)\n",
                   bink->video_pts, audio_size, bink->remain_packet_size);
            return AVERROR(EIO);
        }
        bink->remain_packet_size -= 4 + audio_size;
        bink->current_track++;
        if (audio_size >= 4) {
            /* get one audio packet per track */
            if ((ret = av_get_packet(pb, pkt, audio_size)) < 0)
                return ret;
            pkt->stream_index = bink->current_track;
            pkt->pts = bink->audio_pts[bink->current_track - 1];

            /* Each audio packet reports the number of decompressed samples
               (in bytes). We use this value to calcuate the audio PTS */
            if (pkt->size >= 4)
                bink->audio_pts[bink->current_track -1] +=
                    AV_RL32(pkt->data) / (2 * s->streams[bink->current_track]->codec->channels);
            return 0;
        } else {
            url_fseek(pb, audio_size, SEEK_CUR);
        }
    }

    /* get video packet */
    if ((ret = av_get_packet(pb, pkt, bink->remain_packet_size)) < 0)
        return ret;
    pkt->stream_index = 0;
    pkt->pts = bink->video_pts++;
    pkt->flags |= AV_PKT_FLAG_KEY;

    /* -1 instructs the next call to read_packet() to read the next frame */
    bink->current_track = -1;

    return 0;
}

static int read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    BinkDemuxContext *bink = s->priv_data;
    AVStream *vst = s->streams[0];

    if (url_is_streamed(s->pb))
        return -1;

    /* seek to the first frame */
    url_fseek(s->pb, vst->index_entries[0].pos, SEEK_SET);
    bink->video_pts = 0;
    memset(bink->audio_pts, 0, sizeof(bink->audio_pts));
    bink->current_track = -1;
    return 0;
}

AVInputFormat bink_demuxer = {
    "bink",
    NULL_IF_CONFIG_SMALL("Bink"),
    sizeof(BinkDemuxContext),
    probe,
    read_header,
    read_packet,
    NULL,
    read_seek,
};
