/*
 * Microsoft XMV demuxer
 * Copyright (c) 2011 Sven Hesse <drmccoy@drmccoy.de>
 * Copyright (c) 2011 Matthew Hoops <clone2727@gmail.com>
 *
 * This file is part of Libav.
 *
 * Libav is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * Libav is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with Libav; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Microsoft XMV demuxer
 */

#include <stdint.h>

#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "internal.h"
#include "riff.h"

#define XMV_MIN_HEADER_SIZE 36

#define XMV_AUDIO_ADPCM51_FRONTLEFTRIGHT 1
#define XMV_AUDIO_ADPCM51_FRONTCENTERLOW 2
#define XMV_AUDIO_ADPCM51_REARLEFTRIGHT  4

#define XMV_AUDIO_ADPCM51 (XMV_AUDIO_ADPCM51_FRONTLEFTRIGHT | \
                           XMV_AUDIO_ADPCM51_FRONTCENTERLOW | \
                           XMV_AUDIO_ADPCM51_REARLEFTRIGHT)

typedef struct XMVAudioTrack {
    uint16_t compression;
    uint16_t channels;
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint32_t bit_rate;
    uint16_t flags;
    uint16_t block_align;
    uint16_t block_samples;

    enum CodecID codec_id;
} XMVAudioTrack;

typedef struct XMVVideoPacket {
    /* The decoder stream index for this video packet. */
    int stream_index;

    uint32_t data_size;
    uint32_t data_offset;

    uint32_t current_frame;
    uint32_t frame_count;

    /* Does the video packet contain extra data? */
    int has_extradata;

    /* Extra data */
    uint8_t extradata[4];

    int64_t last_pts;
    int64_t pts;
} XMVVideoPacket;

typedef struct XMVAudioPacket {
    /* The decoder stream index for this audio packet. */
    int stream_index;

    /* The audio track this packet encodes. */
    XMVAudioTrack *track;

    uint32_t data_size;
    uint32_t data_offset;

    uint32_t frame_size;

    uint32_t block_count;
} XMVAudioPacket;

typedef struct XMVDemuxContext {
    uint16_t audio_track_count;

    XMVAudioTrack *audio_tracks;

    uint32_t this_packet_size;
    uint32_t next_packet_size;

    uint32_t this_packet_offset;
    uint32_t next_packet_offset;

    uint16_t current_stream;
    uint16_t stream_count;

    XMVVideoPacket  video;
    XMVAudioPacket *audio;
} XMVDemuxContext;

static int xmv_probe(AVProbeData *p)
{
    uint32_t file_version;

    if (p->buf_size < XMV_MIN_HEADER_SIZE)
        return 0;

    file_version = AV_RL32(p->buf + 16);
    if ((file_version == 0) || (file_version > 4))
        return 0;

    if (!memcmp(p->buf + 12, "xobX", 4))
        return AVPROBE_SCORE_MAX;

    return 0;
}

static int xmv_read_header(AVFormatContext *s)
{
    XMVDemuxContext *xmv = s->priv_data;
    AVIOContext     *pb  = s->pb;
    AVStream        *vst = NULL;

    uint32_t file_version;
    uint32_t this_packet_size;
    uint16_t audio_track;

    avio_skip(pb, 4); /* Next packet size */

    this_packet_size = avio_rl32(pb);

    avio_skip(pb, 4); /* Max packet size */
    avio_skip(pb, 4); /* "xobX" */

    file_version = avio_rl32(pb);
    if ((file_version != 4) && (file_version != 2))
        av_log_ask_for_sample(s, "Found uncommon version %d\n", file_version);


    /* Video track */

    vst = avformat_new_stream(s, NULL);
    if (!vst)
        return AVERROR(ENOMEM);

    avpriv_set_pts_info(vst, 32, 1, 1000);

    vst->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    vst->codec->codec_id   = CODEC_ID_WMV2;
    vst->codec->codec_tag  = MKBETAG('W', 'M', 'V', '2');
    vst->codec->width      = avio_rl32(pb);
    vst->codec->height     = avio_rl32(pb);

    vst->duration          = avio_rl32(pb);

    xmv->video.stream_index = vst->index;

    /* Audio tracks */

    xmv->audio_track_count = avio_rl16(pb);

    avio_skip(pb, 2); /* Unknown (padding?) */

    xmv->audio_tracks = av_malloc(xmv->audio_track_count * sizeof(XMVAudioTrack));
    if (!xmv->audio_tracks)
        return AVERROR(ENOMEM);

    xmv->audio = av_malloc(xmv->audio_track_count * sizeof(XMVAudioPacket));
    if (!xmv->audio)
        return AVERROR(ENOMEM);

    for (audio_track = 0; audio_track < xmv->audio_track_count; audio_track++) {
        XMVAudioTrack  *track  = &xmv->audio_tracks[audio_track];
        XMVAudioPacket *packet = &xmv->audio       [audio_track];
        AVStream *ast = NULL;

        track->compression     = avio_rl16(pb);
        track->channels        = avio_rl16(pb);
        track->sample_rate     = avio_rl32(pb);
        track->bits_per_sample = avio_rl16(pb);
        track->flags           = avio_rl16(pb);

        track->bit_rate      = track->bits_per_sample *
                               track->sample_rate *
                               track->channels;
        track->block_align   = 36 * track->channels;
        track->block_samples = 64;
        track->codec_id      = ff_wav_codec_get_id(track->compression,
                                                   track->bits_per_sample);

        packet->track        = track;
        packet->stream_index = -1;

        packet->frame_size  = 0;
        packet->block_count = 0;

        /* TODO: ADPCM'd 5.1 sound is encoded in three separate streams.
         *       Those need to be interleaved to a proper 5.1 stream. */
        if (track->flags & XMV_AUDIO_ADPCM51)
            av_log(s, AV_LOG_WARNING, "Unsupported 5.1 ADPCM audio stream "
                                      "(0x%04X)\n", track->flags);

        ast = avformat_new_stream(s, NULL);
        if (!ast)
            return AVERROR(ENOMEM);

        ast->codec->codec_type            = AVMEDIA_TYPE_AUDIO;
        ast->codec->codec_id              = track->codec_id;
        ast->codec->codec_tag             = track->compression;
        ast->codec->channels              = track->channels;
        ast->codec->sample_rate           = track->sample_rate;
        ast->codec->bits_per_coded_sample = track->bits_per_sample;
        ast->codec->bit_rate              = track->bit_rate;
        ast->codec->block_align           = 36 * track->channels;

        avpriv_set_pts_info(ast, 32, track->block_samples, track->sample_rate);

        packet->stream_index = ast->index;

        ast->duration = vst->duration;
    }


    /** Initialize the packet context */

    xmv->next_packet_offset = avio_tell(pb);
    xmv->next_packet_size   = this_packet_size - xmv->next_packet_offset;
    xmv->stream_count       = xmv->audio_track_count + 1;

    return 0;
}

static void xmv_read_extradata(uint8_t *extradata, AVIOContext *pb)
{
    /* Read the XMV extradata */

    uint32_t data = avio_rl32(pb);

    int mspel_bit        = !!(data & 0x01);
    int loop_filter      = !!(data & 0x02);
    int abt_flag         = !!(data & 0x04);
    int j_type_bit       = !!(data & 0x08);
    int top_left_mv_flag = !!(data & 0x10);
    int per_mb_rl_bit    = !!(data & 0x20);
    int slice_count      = (data >> 6) & 7;

    /* Write it back as standard WMV2 extradata */

    data = 0;

    data |= mspel_bit        << 15;
    data |= loop_filter      << 14;
    data |= abt_flag         << 13;
    data |= j_type_bit       << 12;
    data |= top_left_mv_flag << 11;
    data |= per_mb_rl_bit    << 10;
    data |= slice_count      <<  7;

    AV_WB32(extradata, data);
}

static int xmv_process_packet_header(AVFormatContext *s)
{
    XMVDemuxContext *xmv = s->priv_data;
    AVIOContext     *pb  = s->pb;

    uint8_t  data[8];
    uint16_t audio_track;
    uint32_t data_offset;

    /* Next packet size */
    xmv->next_packet_size = avio_rl32(pb);

    /* Packet video header */

    if (avio_read(pb, data, 8) != 8)
        return AVERROR(EIO);

    xmv->video.data_size     = AV_RL32(data) & 0x007FFFFF;

    xmv->video.current_frame = 0;
    xmv->video.frame_count   = (AV_RL32(data) >> 23) & 0xFF;

    xmv->video.has_extradata = (data[3] & 0x80) != 0;

    /* Adding the audio data sizes and the video data size keeps you 4 bytes
     * short for every audio track. But as playing around with XMV files with
     * ADPCM audio showed, taking the extra 4 bytes from the audio data gives
     * you either completely distorted audio or click (when skipping the
     * remaining 68 bytes of the ADPCM block). Substracting 4 bytes for every
     * audio track from the video data works at least for the audio. Probably
     * some alignment thing?
     * The video data has (always?) lots of padding, so it should work out...
     */
    xmv->video.data_size -= xmv->audio_track_count * 4;

    xmv->current_stream = 0;
    if (!xmv->video.frame_count) {
        xmv->video.frame_count = 1;
        xmv->current_stream    = 1;
    }

    /* Packet audio header */

    for (audio_track = 0; audio_track < xmv->audio_track_count; audio_track++) {
        XMVAudioPacket *packet = &xmv->audio[audio_track];

        if (avio_read(pb, data, 4) != 4)
            return AVERROR(EIO);

        packet->data_size = AV_RL32(data) & 0x007FFFFF;
        if ((packet->data_size == 0) && (audio_track != 0))
            /* This happens when I create an XMV with several identical audio
             * streams. From the size calculations, duplicating the previous
             * stream's size works out, but the track data itself is silent.
             * Maybe this should also redirect the offset to the previous track?
             */
            packet->data_size = xmv->audio[audio_track - 1].data_size;

        /** Carve up the audio data in frame_count slices */
        packet->frame_size  = packet->data_size  / xmv->video.frame_count;
        packet->frame_size -= packet->frame_size % packet->track->block_align;
    }

    /* Packet data offsets */

    data_offset = avio_tell(pb);

    xmv->video.data_offset = data_offset;
    data_offset += xmv->video.data_size;

    for (audio_track = 0; audio_track < xmv->audio_track_count; audio_track++) {
        xmv->audio[audio_track].data_offset = data_offset;
        data_offset += xmv->audio[audio_track].data_size;
    }

    /* Video frames header */

    /* Read new video extra data */
    if (xmv->video.data_size > 0) {
        if (xmv->video.has_extradata) {
            xmv_read_extradata(xmv->video.extradata, pb);

            xmv->video.data_size   -= 4;
            xmv->video.data_offset += 4;

            if (xmv->video.stream_index >= 0) {
                AVStream *vst = s->streams[xmv->video.stream_index];

                assert(xmv->video.stream_index < s->nb_streams);

                if (vst->codec->extradata_size < 4) {
                    av_free(vst->codec->extradata);

                    vst->codec->extradata =
                        av_malloc(4 + FF_INPUT_BUFFER_PADDING_SIZE);
                    vst->codec->extradata_size = 4;
                }

                memcpy(vst->codec->extradata, xmv->video.extradata, 4);
            }
        }
    }

    return 0;
}

static int xmv_fetch_new_packet(AVFormatContext *s)
{
    XMVDemuxContext *xmv = s->priv_data;
    AVIOContext     *pb  = s->pb;
    int result;

    /* Seek to it */
    xmv->this_packet_offset = xmv->next_packet_offset;
    if (avio_seek(pb, xmv->this_packet_offset, SEEK_SET) != xmv->this_packet_offset)
        return AVERROR(EIO);

    /* Update the size */
    xmv->this_packet_size = xmv->next_packet_size;
    if (xmv->this_packet_size < (12 + xmv->audio_track_count * 4))
        return AVERROR(EIO);

    /* Process the header */
    result = xmv_process_packet_header(s);
    if (result)
        return result;

    /* Update the offset */
    xmv->next_packet_offset = xmv->this_packet_offset + xmv->this_packet_size;

    return 0;
}

static int xmv_fetch_audio_packet(AVFormatContext *s,
                                  AVPacket *pkt, uint32_t stream)
{
    XMVDemuxContext *xmv   = s->priv_data;
    AVIOContext     *pb    = s->pb;
    XMVAudioPacket  *audio = &xmv->audio[stream];

    uint32_t data_size;
    uint32_t block_count;
    int result;

    /* Seek to it */
    if (avio_seek(pb, audio->data_offset, SEEK_SET) != audio->data_offset)
        return AVERROR(EIO);

    if ((xmv->video.current_frame + 1) < xmv->video.frame_count)
        /* Not the last frame, get at most frame_size bytes. */
        data_size = FFMIN(audio->frame_size, audio->data_size);
    else
        /* Last frame, get the rest. */
        data_size = audio->data_size;

    /* Read the packet */
    result = av_get_packet(pb, pkt, data_size);
    if (result <= 0)
        return result;

    pkt->stream_index = audio->stream_index;

    /* Calculate the PTS */

    block_count = data_size / audio->track->block_align;

    pkt->duration = block_count;
    pkt->pts      = audio->block_count;
    pkt->dts      = AV_NOPTS_VALUE;

    audio->block_count += block_count;

    /* Advance offset */
    audio->data_size   -= data_size;
    audio->data_offset += data_size;

    return 0;
}

static int xmv_fetch_video_packet(AVFormatContext *s,
                                  AVPacket *pkt)
{
    XMVDemuxContext *xmv   = s->priv_data;
    AVIOContext     *pb    = s->pb;
    XMVVideoPacket  *video = &xmv->video;

    int result;
    uint32_t frame_header;
    uint32_t frame_size, frame_timestamp;
    uint32_t i;

    /* Seek to it */
    if (avio_seek(pb, video->data_offset, SEEK_SET) != video->data_offset)
        return AVERROR(EIO);

    /* Read the frame header */
    frame_header = avio_rl32(pb);

    frame_size      = (frame_header & 0x1FFFF) * 4 + 4;
    frame_timestamp = (frame_header >> 17);

    if ((frame_size + 4) > video->data_size)
        return AVERROR(EIO);

    /* Create the packet */
    result = av_new_packet(pkt, frame_size);
    if (result)
        return result;

    /* Contrary to normal WMV2 video, the bit stream in XMV's
     * WMV2 is little-endian.
     * TODO: This manual swap is of course suboptimal.
     */
    for (i = 0; i < frame_size; i += 4)
        AV_WB32(pkt->data + i, avio_rl32(pb));

    pkt->stream_index = video->stream_index;

    /* Calculate the PTS */

    video->last_pts = frame_timestamp + video->pts;

    pkt->duration = 0;
    pkt->pts      = video->last_pts;
    pkt->dts      = AV_NOPTS_VALUE;

    video->pts += frame_timestamp;

    /* Keyframe? */
    pkt->flags = (pkt->data[0] & 0x80) ? 0 : AV_PKT_FLAG_KEY;

    /* Advance offset */
    video->data_size   -= frame_size + 4;
    video->data_offset += frame_size + 4;

    return 0;
}

static int xmv_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    XMVDemuxContext *xmv = s->priv_data;
    int result;

    if (xmv->video.current_frame == xmv->video.frame_count) {
        /* No frames left in this packet, so we fetch a new one */

        result = xmv_fetch_new_packet(s);
        if (result)
            return result;
    }

    if (xmv->current_stream == 0) {
        /* Fetch a video frame */

        result = xmv_fetch_video_packet(s, pkt);
        if (result)
            return result;

    } else {
        /* Fetch an audio frame */

        result = xmv_fetch_audio_packet(s, pkt, xmv->current_stream - 1);
        if (result)
            return result;
    }

    /* Increase our counters */
    if (++xmv->current_stream >= xmv->stream_count) {
        xmv->current_stream       = 0;
        xmv->video.current_frame += 1;
    }

    return 0;
}

static int xmv_read_close(AVFormatContext *s)
{
    XMVDemuxContext *xmv = s->priv_data;

    av_free(xmv->audio);
    av_free(xmv->audio_tracks);

    return 0;
}

AVInputFormat ff_xmv_demuxer = {
    .name           = "xmv",
    .long_name      = NULL_IF_CONFIG_SMALL("Microsoft XMV"),
    .priv_data_size = sizeof(XMVDemuxContext),
    .read_probe     = xmv_probe,
    .read_header    = xmv_read_header,
    .read_packet    = xmv_read_packet,
    .read_close     = xmv_read_close,
};
