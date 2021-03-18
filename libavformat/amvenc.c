/*
 * AMV muxer
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
#include "riff.h"
#include "internal.h"
#include "avio_internal.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/avassert.h"

/*
 * Things to note:
 * - AMV is a hard-coded (and broken) subset of AVI. It's not worth sullying the
 *   existing AVI muxer with its filth.
 * - No separate demuxer as the existing AVI demuxer can handle these.
 * - The sizes of certain tags are deliberately set to 0 as some players break
 *   when they're set correctly. Ditto with some header fields.
 * - There is no index.
 * - Players are **very** sensitive to the frame order and sizes.
 *   - Frames must be strictly interleaved as V-A, any V-V or A-A will
 *     cause crashes.
 *   - Variable video frame sizes seem to be handled fine.
 *   - Variable audio frame sizes cause crashes.
 *   - If audio is shorter than video, it's padded with silence.
 *   - If video is shorter than audio, the most recent frame is repeated.
 */

#define AMV_STREAM_COUNT     2
#define AMV_STREAM_VIDEO     0
#define AMV_STREAM_AUDIO     1
#define AMV_VIDEO_STRH_SIZE 56
#define AMV_VIDEO_STRF_SIZE 36
#define AMV_AUDIO_STRH_SIZE 48
#define AMV_AUDIO_STRF_SIZE 20 /* sizeof(WAVEFORMATEX) + 2 */

typedef struct AMVContext
{
    int64_t riff_start;
    int64_t movi_list;
    int64_t offset_duration;
    int     last_stream;

    int32_t us_per_frame; /* Microseconds per frame.         */

    int32_t aframe_size;  /* Expected audio frame size.      */
    int32_t ablock_align; /* Expected audio block align.     */
    AVPacket *apad;       /* Dummy audio packet for padding; not owned by us. */
    AVPacket *vpad;       /* Most recent video frame, for padding. */

    /*
     * Cumulative PTS values for each stream, used for the final
     * duration calculcation.
     */
    int64_t lastpts[AMV_STREAM_COUNT];
} AMVContext;

/* ff_{start,end}_tag(), but sets the size to 0. */
static int64_t amv_start_tag(AVIOContext *pb, const char *tag)
{
    ffio_wfourcc(pb, tag);
    avio_wl32(pb, 0);
    return avio_tell(pb);
}

static void amv_end_tag(AVIOContext *pb, int64_t start)
{
    int64_t pos;
    av_assert0((start&1) == 0);

    pos = avio_tell(pb);
    if (pos & 1)
        avio_w8(pb, 0);
}

static av_cold int amv_init(AVFormatContext *s)
{
    AMVContext *amv = s->priv_data;
    AVStream   *vst, *ast;
    int ret;

    amv->last_stream  = -1;

    if (s->nb_streams != AMV_STREAM_COUNT) {
        av_log(s, AV_LOG_ERROR, "AMV files only support 2 streams\n");
        return AVERROR(EINVAL);
    }

    vst = s->streams[AMV_STREAM_VIDEO];
    ast = s->streams[AMV_STREAM_AUDIO];

    if (vst->codecpar->codec_id != AV_CODEC_ID_AMV) {
        av_log(s, AV_LOG_ERROR, "First AMV stream must be %s\n",
                avcodec_get_name(AV_CODEC_ID_AMV));
        return AVERROR(EINVAL);
    }

    if (ast->codecpar->codec_id != AV_CODEC_ID_ADPCM_IMA_AMV) {
        av_log(s, AV_LOG_ERROR, "Second AMV stream must be %s\n",
                avcodec_get_name(AV_CODEC_ID_ADPCM_IMA_AMV));
        return AVERROR(EINVAL);
    }

    /* These files are broken-enough as they are. They shouldn't be streamed. */
    if (!(s->pb->seekable & AVIO_SEEKABLE_NORMAL)) {
        av_log(s, AV_LOG_ERROR, "Stream not seekable, unable to write output file\n");
        return AVERROR(EINVAL);
    }

    amv->us_per_frame = av_rescale(AV_TIME_BASE, vst->time_base.num, vst->time_base.den);
    amv->aframe_size  = av_rescale(ast->codecpar->sample_rate, amv->us_per_frame, AV_TIME_BASE);
    amv->ablock_align = 8 + (FFALIGN(amv->aframe_size, 2) / 2);

    av_log(s, AV_LOG_TRACE, "us_per_frame = %d\n", amv->us_per_frame);
    av_log(s, AV_LOG_TRACE, "aframe_size  = %d\n", amv->aframe_size);
    av_log(s, AV_LOG_TRACE, "ablock_align = %d\n", amv->ablock_align);

    /*
     * Bail if the framerate's too high. Prevents the audio frame size from
     * getting too small. 63fps is the closest value to 60fps that divides
     * cleanly, so cap it there.
     */
    if (amv->us_per_frame < 15873) {
        av_log(s, AV_LOG_ERROR, "Refusing to mux >63fps video\n");
        return AVERROR(EINVAL);
    }

    /*
     * frame_size will be set if coming from the encoder.
     * Make sure the its been configured correctly. The audio frame duration
     * needs to match that of the video.
     */
    if (ast->codecpar->frame_size) {
        AVCodecParameters *par = ast->codecpar;
        int bad = 0;

        if (par->frame_size != amv->aframe_size) {
            av_log(s, AV_LOG_ERROR, "Invalid audio frame size. Got %d, wanted %d\n",
                   par->frame_size, amv->aframe_size);
            bad = 1;
        }

        if (par->block_align != amv->ablock_align) {
            av_log(s, AV_LOG_ERROR, "Invalid audio block align. Got %d, wanted %d\n",
                   par->block_align, amv->ablock_align);
            bad = 1;
        }

        if (bad) {
            av_log(s, AV_LOG_ERROR, "Try -block_size %d\n", amv->aframe_size);
            return AVERROR(EINVAL);
        }

        if (ast->codecpar->sample_rate % amv->aframe_size) {
            av_log(s, AV_LOG_ERROR, "Audio sample rate not a multiple of the frame size.\n"
                "Please change video frame rate. Suggested rates: 10,14,15,18,21,25,30\n");
            return AVERROR(EINVAL);
        }
    } else {
        /* If remuxing from the same source, then this will match the video. */
        int32_t aus = av_rescale(AV_TIME_BASE, ast->time_base.num, ast->time_base.den);
        if (aus != amv->us_per_frame) {
            av_log(s, AV_LOG_ERROR, "Cannot remux streams with a different time base\n");
            return AVERROR(EINVAL);
        }
    }

    /* Allocate and fill dummy packet so we can pad the audio. */
    amv->apad = ffformatcontext(s)->pkt;
    if ((ret = av_new_packet(amv->apad, amv->ablock_align)) < 0) {
        return ret;
    }

    amv->apad->stream_index = AMV_STREAM_AUDIO;
    memset(amv->apad->data, 0, amv->ablock_align);
    AV_WL32(amv->apad->data + 4, amv->aframe_size);

    amv->vpad = av_packet_alloc();
    if (!amv->vpad) {
        return AVERROR(ENOMEM);
    }
    amv->vpad->stream_index = AMV_STREAM_VIDEO;
    amv->vpad->duration     = 1;
    return 0;
}

static void amv_deinit(AVFormatContext *s)
{
    AMVContext *amv = s->priv_data;

    av_packet_free(&amv->vpad);
}

static void amv_write_vlist(AVFormatContext *s, AVCodecParameters *par)
{
    int64_t tag_list, tag_str;

    av_assert0(par->codec_id == AV_CODEC_ID_AMV);

    tag_list = amv_start_tag(s->pb, "LIST");
    ffio_wfourcc(s->pb, "strl");
    tag_str = ff_start_tag(s->pb, "strh");
    ffio_fill(s->pb, 0, AMV_VIDEO_STRH_SIZE);
    ff_end_tag(s->pb, tag_str);

    tag_str = ff_start_tag(s->pb, "strf");
    ffio_fill(s->pb, 0, AMV_VIDEO_STRF_SIZE);
    ff_end_tag(s->pb, tag_str);

    amv_end_tag(s->pb, tag_list);
}

static void amv_write_alist(AVFormatContext *s, AVCodecParameters *par)
{
    uint8_t buf[AMV_AUDIO_STRF_SIZE];
    AVIOContext *pb = s->pb;
    int64_t tag_list, tag_str;

    av_assert0(par->codec_id == AV_CODEC_ID_ADPCM_IMA_AMV);

    tag_list = amv_start_tag(pb, "LIST");
    ffio_wfourcc(pb, "strl");
    tag_str = ff_start_tag(pb, "strh");
    ffio_fill(s->pb, 0, AMV_AUDIO_STRH_SIZE);
    ff_end_tag(pb, tag_str);

    /* Bodge an (incorrect) WAVEFORMATEX (+2 pad bytes) */
    tag_str = ff_start_tag(pb, "strf");
    AV_WL16(buf +  0, 1);
    AV_WL16(buf +  2, par->channels);
    AV_WL32(buf +  4, par->sample_rate);
    AV_WL32(buf +  8, par->sample_rate * par->channels * 2);
    AV_WL16(buf + 12, 2);
    AV_WL16(buf + 14, 16);
    AV_WL16(buf + 16, 0);
    AV_WL16(buf + 18, 0);
    avio_write(pb, buf, AMV_AUDIO_STRF_SIZE);
    ff_end_tag(pb, tag_str);

    amv_end_tag(pb, tag_list);
}

static int amv_write_header(AVFormatContext *s)
{
    AMVContext *amv = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *vst   = s->streams[AMV_STREAM_VIDEO];
    AVStream *ast   = s->streams[AMV_STREAM_AUDIO];
    uint8_t amvh[56] = {0};
    int64_t list1;

    amv->riff_start = amv_start_tag(pb, "RIFF");
    ffio_wfourcc(pb, "AMV ");
    list1 = amv_start_tag(pb, "LIST");
    ffio_wfourcc(pb, "hdrl");

    ffio_wfourcc(pb, "amvh");
    avio_wl32(pb, 56);

    AV_WL32(amvh +  0, amv->us_per_frame);
    AV_WL32(amvh + 32, vst->codecpar->width);
    AV_WL32(amvh + 36, vst->codecpar->height);
    AV_WL32(amvh + 40, vst->time_base.den);
    AV_WL32(amvh + 44, vst->time_base.num);
    AV_WL32(amvh + 48, 0);
    AV_WL32(amvh + 52, 0); /* duration, filled in later. */

    avio_write(pb, amvh, sizeof(amvh));
    amv->offset_duration = avio_tell(pb) - 4;

    amv_write_vlist(s, vst->codecpar);
    amv_write_alist(s, ast->codecpar);
    amv_end_tag(pb, list1);

    amv->movi_list = amv_start_tag(pb, "LIST");
    ffio_wfourcc(pb, "movi");
    return 0;
}

static int amv_write_packet_internal(AVFormatContext *s, AVPacket *pkt)
{
    AMVContext *amv = s->priv_data;

    if (pkt->stream_index == AMV_STREAM_VIDEO)
        ffio_wfourcc(s->pb, "00dc");
    else if (pkt->stream_index == AMV_STREAM_AUDIO)
        ffio_wfourcc(s->pb, "01wb");
    else
        av_assert0(0);

    if (pkt->stream_index == AMV_STREAM_AUDIO && pkt->size != amv->ablock_align) {
        /* Can happen when remuxing files produced by another encoder. */
        av_log(s, AV_LOG_WARNING, "Invalid audio packet size (%d != %d)\n",
               pkt->size, amv->ablock_align);
    }

    avio_wl32(s->pb, pkt->size);
    avio_write(s->pb, pkt->data, pkt->size);

    amv->lastpts[pkt->stream_index] += pkt->duration;
    amv->last_stream = pkt->stream_index;
    return 0;
}

static int amv_pad(AVFormatContext *s, AVPacket *pkt)
{
    AMVContext *amv = s->priv_data;
    int stream_index = pkt->stream_index;

    if (stream_index != amv->last_stream)
        return 0;

    stream_index = (stream_index + 1) % s->nb_streams;
    if (stream_index == AMV_STREAM_VIDEO)
        return amv_write_packet_internal(s, amv->vpad);
    else if (stream_index == AMV_STREAM_AUDIO)
        return amv_write_packet_internal(s, amv->apad);
    else
        av_assert0(0);

    return AVERROR(EINVAL);
}

static int amv_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AMVContext *amv = s->priv_data;
    int ret;

    /* Add a dummy frame if we've received two of the same index. */
    if ((ret = amv_pad(s, pkt)) < 0)
        return ret;

    if ((ret = amv_write_packet_internal(s, pkt)) < 0)
        return ret;

    if (pkt->stream_index == AMV_STREAM_VIDEO) {
        /* Save the last packet for padding. */
        av_packet_unref(amv->vpad);
        if ((ret = av_packet_ref(amv->vpad, pkt)) < 0)
            return ret;
    }

    return 0;
}

static int amv_write_trailer(AVFormatContext *s)
{
    AMVContext *amv = s->priv_data;
    AVStream   *vst = s->streams[AMV_STREAM_VIDEO];
    AVStream   *ast = s->streams[AMV_STREAM_AUDIO];
    int64_t maxpts, ret;
    int hh, mm, ss;

    /* Pad-out one last audio frame if needed. */
    if (amv->last_stream == AMV_STREAM_VIDEO) {
        if ((ret = amv_write_packet_internal(s, amv->apad)) < 0)
            return ret;
    }

    amv_end_tag(s->pb, amv->movi_list);
    amv_end_tag(s->pb, amv->riff_start);

    ffio_wfourcc(s->pb, "AMV_");
    ffio_wfourcc(s->pb, "END_");

    if ((ret = avio_seek(s->pb, amv->offset_duration, SEEK_SET)) < 0)
        return ret;

    /* Go back and write the duration. */
    maxpts = FFMAX(
        av_rescale_q(amv->lastpts[AMV_STREAM_VIDEO], vst->time_base, AV_TIME_BASE_Q),
        av_rescale_q(amv->lastpts[AMV_STREAM_AUDIO], ast->time_base, AV_TIME_BASE_Q)
    );

    ss  = maxpts / AV_TIME_BASE;
    mm  = ss / 60;
    hh  = mm / 60;
    ss %= 60;
    mm %= 60;

    avio_w8(s->pb, ss);
    avio_w8(s->pb, mm);
    avio_wl16(s->pb, hh);
    return 0;
}

const AVOutputFormat ff_amv_muxer = {
    .name           = "amv",
    .long_name      = NULL_IF_CONFIG_SMALL("AMV"),
    .mime_type      = "video/amv",
    .extensions     = "amv",
    .priv_data_size = sizeof(AMVContext),
    .audio_codec    = AV_CODEC_ID_ADPCM_IMA_AMV,
    .video_codec    = AV_CODEC_ID_AMV,
    .init           = amv_init,
    .deinit         = amv_deinit,
    .write_header   = amv_write_header,
    .write_packet   = amv_write_packet,
    .write_trailer  = amv_write_trailer,
};
