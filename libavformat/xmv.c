/*
 * Microsoft XMV demuxer
 * Copyright (c) 2011 Sven Hesse <drmccoy@drmccoy.de>
 * Copyright (c) 2011 Matthew Hoops <clone2727@gmail.com>
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
 * Microsoft XMV demuxer
 */

#include <inttypes.h>

#include "libavutil/intreadwrite.h"

#include "avformat.h"
#include "internal.h"
#include "riff.h"
#include "libavutil/avassert.h"

/** The min size of an XMV header. */
#define XMV_MIN_HEADER_SIZE 36

/** Audio flag: ADPCM'd 5.1 stream, front left / right channels */
#define XMV_AUDIO_ADPCM51_FRONTLEFTRIGHT 1
/** Audio flag: ADPCM'd 5.1 stream, front center / low frequency channels */
#define XMV_AUDIO_ADPCM51_FRONTCENTERLOW 2
/** Audio flag: ADPCM'd 5.1 stream, rear left / right channels */
#define XMV_AUDIO_ADPCM51_REARLEFTRIGHT  4

/** Audio flag: Any of the ADPCM'd 5.1 stream flags. */
#define XMV_AUDIO_ADPCM51 (XMV_AUDIO_ADPCM51_FRONTLEFTRIGHT | \
                           XMV_AUDIO_ADPCM51_FRONTCENTERLOW | \
                           XMV_AUDIO_ADPCM51_REARLEFTRIGHT)

#define XMV_BLOCK_ALIGN_SIZE 36

/** A video packet with an XMV file. */
typedef struct XMVVideoPacket {
    int created;
    int stream_index; ///< The decoder stream index for this video packet.

    uint32_t data_size;   ///< The size of the remaining video data.
    uint64_t data_offset; ///< The offset of the video data within the file.

    uint32_t current_frame; ///< The current frame within this video packet.
    uint32_t frame_count;   ///< The amount of frames within this video packet.

    int     has_extradata; ///< Does the video packet contain extra data?
    uint8_t extradata[4];  ///< The extra data

    int64_t last_pts; ///< PTS of the last video frame.
    int64_t pts;      ///< PTS of the most current video frame.
} XMVVideoPacket;

/** An audio packet with an XMV file. */
typedef struct XMVAudioPacket {
    int created;
    int stream_index; ///< The decoder stream index for this audio packet.

    /* Stream format properties. */
    uint16_t compression;     ///< The type of compression.
    uint16_t channels;        ///< Number of channels.
    int32_t sample_rate;      ///< Sampling rate.
    uint16_t bits_per_sample; ///< Bits per compressed sample.
    uint32_t bit_rate;        ///< Bits of compressed data per second.
    uint16_t flags;           ///< Flags
    unsigned block_align;     ///< Bytes per compressed block.
    uint16_t block_samples;   ///< Decompressed samples per compressed block.

    enum AVCodecID codec_id; ///< The codec ID of the compression scheme.

    uint32_t data_size;   ///< The size of the remaining audio data.
    uint64_t data_offset; ///< The offset of the audio data within the file.

    uint32_t frame_size; ///< Number of bytes to put into an audio frame.

    uint64_t block_count; ///< Running counter of decompressed audio block.
} XMVAudioPacket;

/** Context for demuxing an XMV file. */
typedef struct XMVDemuxContext {
    uint16_t audio_track_count; ///< Number of audio track in this file.

    uint32_t this_packet_size; ///< Size of the current packet.
    uint32_t next_packet_size; ///< Size of the next packet.

    uint64_t this_packet_offset; ///< Offset of the current packet.
    uint64_t next_packet_offset; ///< Offset of the next packet.

    uint16_t current_stream; ///< The index of the stream currently handling.
    uint16_t stream_count;   ///< The number of streams in this file.

    uint32_t video_duration;
    uint32_t video_width;
    uint32_t video_height;

    XMVVideoPacket  video; ///< The video packet contained in each packet.
    XMVAudioPacket *audio; ///< The audio packets contained in each packet.
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

static int xmv_read_close(AVFormatContext *s)
{
    XMVDemuxContext *xmv = s->priv_data;

    av_freep(&xmv->audio);

    return 0;
}

static int xmv_read_header(AVFormatContext *s)
{
    XMVDemuxContext *xmv = s->priv_data;
    AVIOContext     *pb  = s->pb;

    uint32_t file_version;
    uint32_t this_packet_size;
    uint16_t audio_track;
    int ret;

    s->ctx_flags |= AVFMTCTX_NOHEADER;

    avio_skip(pb, 4); /* Next packet size */

    this_packet_size = avio_rl32(pb);

    avio_skip(pb, 4); /* Max packet size */
    avio_skip(pb, 4); /* "xobX" */

    file_version = avio_rl32(pb);
    if ((file_version != 4) && (file_version != 2))
        avpriv_request_sample(s, "Uncommon version %"PRIu32"", file_version);

    /* Video tracks */

    xmv->video_width    = avio_rl32(pb);
    xmv->video_height   = avio_rl32(pb);
    xmv->video_duration = avio_rl32(pb);

    /* Audio tracks */

    xmv->audio_track_count = avio_rl16(pb);

    avio_skip(pb, 2); /* Unknown (padding?) */

    xmv->audio = av_mallocz_array(xmv->audio_track_count, sizeof(XMVAudioPacket));
    if (!xmv->audio) {
        ret = AVERROR(ENOMEM);
        goto fail;
    }

    for (audio_track = 0; audio_track < xmv->audio_track_count; audio_track++) {
        XMVAudioPacket *packet = &xmv->audio[audio_track];

        packet->compression     = avio_rl16(pb);
        packet->channels        = avio_rl16(pb);
        packet->sample_rate     = avio_rl32(pb);
        packet->bits_per_sample = avio_rl16(pb);
        packet->flags           = avio_rl16(pb);

        packet->bit_rate      = packet->bits_per_sample *
                                packet->sample_rate *
                                packet->channels;
        packet->block_align   = XMV_BLOCK_ALIGN_SIZE * packet->channels;
        packet->block_samples = 64;
        packet->codec_id      = ff_wav_codec_get_id(packet->compression,
                                                    packet->bits_per_sample);

        packet->stream_index = -1;

        packet->frame_size  = 0;
        packet->block_count = 0;

        /* TODO: ADPCM'd 5.1 sound is encoded in three separate streams.
         *       Those need to be interleaved to a proper 5.1 stream. */
        if (packet->flags & XMV_AUDIO_ADPCM51)
            av_log(s, AV_LOG_WARNING, "Unsupported 5.1 ADPCM audio stream "
                                      "(0x%04X)\n", packet->flags);

        if (!packet->channels || packet->sample_rate <= 0 ||
             packet->channels >= UINT16_MAX / XMV_BLOCK_ALIGN_SIZE) {
            av_log(s, AV_LOG_ERROR, "Invalid parameters for audio track %"PRIu16".\n",
                   audio_track);
            ret = AVERROR_INVALIDDATA;
            goto fail;
        }
    }


    /* Initialize the packet context */

    xmv->next_packet_offset = avio_tell(pb);
    xmv->next_packet_size   = this_packet_size - xmv->next_packet_offset;
    xmv->stream_count       = xmv->audio_track_count + 1;

    return 0;

fail:
    xmv_read_close(s);
    return ret;
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
    int ret;

    uint8_t  data[8];
    uint16_t audio_track;
    uint64_t data_offset;

    /* Next packet size */
    xmv->next_packet_size = avio_rl32(pb);

    /* Packet video header */

    if (avio_read(pb, data, 8) != 8)
        return AVERROR(EIO);

    xmv->video.data_size     = AV_RL32(data) & 0x007FFFFF;

    xmv->video.current_frame = 0;
    xmv->video.frame_count   = (AV_RL32(data) >> 23) & 0xFF;

    xmv->video.has_extradata = (data[3] & 0x80) != 0;

    if (!xmv->video.created) {
        AVStream *vst = avformat_new_stream(s, NULL);
        if (!vst)
            return AVERROR(ENOMEM);

        avpriv_set_pts_info(vst, 32, 1, 1000);

        vst->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        vst->codecpar->codec_id   = AV_CODEC_ID_WMV2;
        vst->codecpar->codec_tag  = MKBETAG('W', 'M', 'V', '2');
        vst->codecpar->width      = xmv->video_width;
        vst->codecpar->height     = xmv->video_height;

        vst->duration = xmv->video_duration;

        xmv->video.stream_index = vst->index;

        xmv->video.created = 1;
    }

    /* Adding the audio data sizes and the video data size keeps you 4 bytes
     * short for every audio track. But as playing around with XMV files with
     * ADPCM audio showed, taking the extra 4 bytes from the audio data gives
     * you either completely distorted audio or click (when skipping the
     * remaining 68 bytes of the ADPCM block). Subtracting 4 bytes for every
     * audio track from the video data works at least for the audio. Probably
     * some alignment thing?
     * The video data has (always?) lots of padding, so it should work out...
     */
    xmv->video.data_size -= xmv->audio_track_count * 4;

    xmv->current_stream = 0;
    if (!xmv->video.frame_count) {
        xmv->video.frame_count = 1;
        xmv->current_stream    = xmv->stream_count > 1;
    }

    /* Packet audio header */

    for (audio_track = 0; audio_track < xmv->audio_track_count; audio_track++) {
        XMVAudioPacket *packet = &xmv->audio[audio_track];

        if (avio_read(pb, data, 4) != 4)
            return AVERROR(EIO);

        if (!packet->created) {
            AVStream *ast = avformat_new_stream(s, NULL);
            if (!ast)
                return AVERROR(ENOMEM);

            ast->codecpar->codec_type            = AVMEDIA_TYPE_AUDIO;
            ast->codecpar->codec_id              = packet->codec_id;
            ast->codecpar->codec_tag             = packet->compression;
            ast->codecpar->channels              = packet->channels;
            ast->codecpar->sample_rate           = packet->sample_rate;
            ast->codecpar->bits_per_coded_sample = packet->bits_per_sample;
            ast->codecpar->bit_rate              = packet->bit_rate;
            ast->codecpar->block_align           = 36 * packet->channels;

            avpriv_set_pts_info(ast, 32, packet->block_samples, packet->sample_rate);

            packet->stream_index = ast->index;

            ast->duration = xmv->video_duration;

            packet->created = 1;
        }

        packet->data_size = AV_RL32(data) & 0x007FFFFF;
        if ((packet->data_size == 0) && (audio_track != 0))
            /* This happens when I create an XMV with several identical audio
             * streams. From the size calculations, duplicating the previous
             * stream's size works out, but the track data itself is silent.
             * Maybe this should also redirect the offset to the previous track?
             */
            packet->data_size = xmv->audio[audio_track - 1].data_size;

        /* Carve up the audio data in frame_count slices */
        packet->frame_size  = packet->data_size  / xmv->video.frame_count;
        packet->frame_size -= packet->frame_size % packet->block_align;
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

                av_assert0(xmv->video.stream_index < s->nb_streams);

                if (vst->codecpar->extradata_size < 4) {
                    av_freep(&vst->codecpar->extradata);

                    if ((ret = ff_alloc_extradata(vst->codecpar, 4)) < 0)
                        return ret;
                }

                memcpy(vst->codecpar->extradata, xmv->video.extradata, 4);
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

    if (xmv->this_packet_offset == xmv->next_packet_offset)
        return AVERROR_EOF;

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

    block_count = data_size / audio->block_align;

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
    uint8_t *data, *end;

    /* Seek to it */
    if (avio_seek(pb, video->data_offset, SEEK_SET) != video->data_offset)
        return AVERROR(EIO);

    /* Read the frame header */
    frame_header = avio_rl32(pb);

    frame_size      = (frame_header & 0x1FFFF) * 4 + 4;
    frame_timestamp = (frame_header >> 17);

    if ((frame_size + 4) > video->data_size)
        return AVERROR(EIO);

    /* Get the packet data */
    result = av_get_packet(pb, pkt, frame_size);
    if (result != frame_size)
        return result;

    /* Contrary to normal WMV2 video, the bit stream in XMV's
     * WMV2 is little-endian.
     * TODO: This manual swap is of course suboptimal.
     */
    for (data = pkt->data, end = pkt->data + frame_size; data < end; data += 4)
        AV_WB32(data, AV_RL32(data));

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
    } else {
        /* Fetch an audio frame */

        result = xmv_fetch_audio_packet(s, pkt, xmv->current_stream - 1);
    }
    if (result) {
        xmv->current_stream = 0;
        xmv->video.current_frame = xmv->video.frame_count;
        return result;
    }


    /* Increase our counters */
    if (++xmv->current_stream >= xmv->stream_count) {
        xmv->current_stream       = 0;
        xmv->video.current_frame += 1;
    }

    return 0;
}

AVInputFormat ff_xmv_demuxer = {
    .name           = "xmv",
    .long_name      = NULL_IF_CONFIG_SMALL("Microsoft XMV"),
    .extensions     = "xmv",
    .priv_data_size = sizeof(XMVDemuxContext),
    .read_probe     = xmv_probe,
    .read_header    = xmv_read_header,
    .read_packet    = xmv_read_packet,
    .read_close     = xmv_read_close,
};
