/*
 * mtv demuxer
 * Copyright (c) 2006 Reynaldo H. Verdejo Pinochet
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
 * MTV demuxer.
 */

#include "libavutil/bswap.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

#define MTV_ASUBCHUNK_DATA_SIZE 500
#define MTV_HEADER_SIZE 512
#define MTV_AUDIO_PADDING_SIZE 12
#define AUDIO_SAMPLING_RATE 44100

typedef struct MTVDemuxContext {

    unsigned int file_size;         ///< filesize, not always right
    unsigned int segments;          ///< number of 512 byte segments
    unsigned int audio_identifier;  ///< 'MP3' on all files I have seen
    unsigned int audio_br;          ///< bitrate of audio channel (mp3)
    unsigned int img_colorfmt;      ///< frame colorfmt rgb 565/555
    unsigned int img_bpp;           ///< frame bits per pixel
    unsigned int img_width;         //
    unsigned int img_height;        //
    unsigned int img_segment_size;  ///< size of image segment
    unsigned int video_fps;         //
    unsigned int full_segment_size;

} MTVDemuxContext;

static int mtv_probe(AVProbeData *p)
{
    /* Magic is 'AMV' */
    if (*p->buf != 'A' || *(p->buf + 1) != 'M' || *(p->buf + 2) != 'V')
        return 0;

    /* Check for nonzero in bpp and (width|height) header fields */
    if(p->buf_size < 57 || !(p->buf[51] && AV_RL16(&p->buf[52]) | AV_RL16(&p->buf[54])))
        return 0;

    /* If width or height are 0 then imagesize header field should not */
    if(!AV_RL16(&p->buf[52]) || !AV_RL16(&p->buf[54]))
    {
        if(!!AV_RL16(&p->buf[56]))
            return AVPROBE_SCORE_MAX/2;
        else
            return 0;
    }

    if(p->buf[51] != 16)
        return AVPROBE_SCORE_MAX/4; // But we are going to assume 16bpp anyway ..

    return AVPROBE_SCORE_MAX;
}

static int mtv_read_header(AVFormatContext *s)
{
    MTVDemuxContext *mtv = s->priv_data;
    AVIOContext   *pb  = s->pb;
    AVStream        *st;
    unsigned int    audio_subsegments;

    avio_skip(pb, 3);
    mtv->file_size         = avio_rl32(pb);
    mtv->segments          = avio_rl32(pb);
    avio_skip(pb, 32);
    mtv->audio_identifier  = avio_rl24(pb);
    mtv->audio_br          = avio_rl16(pb);
    mtv->img_colorfmt      = avio_rl24(pb);
    mtv->img_bpp           = avio_r8(pb);
    mtv->img_width         = avio_rl16(pb);
    mtv->img_height        = avio_rl16(pb);
    mtv->img_segment_size  = avio_rl16(pb);

    /* Calculate width and height if missing from header */

    if(mtv->img_bpp>>3){
    if(!mtv->img_width && mtv->img_height)
        mtv->img_width=mtv->img_segment_size / (mtv->img_bpp>>3)
                        / mtv->img_height;

    if(!mtv->img_height && mtv->img_width)
        mtv->img_height=mtv->img_segment_size / (mtv->img_bpp>>3)
                        / mtv->img_width;
    }
    if(!mtv->img_height || !mtv->img_width){
        av_log(s, AV_LOG_ERROR, "width or height is invalid and I cannot calculate them from other information\n");
        return AVERROR(EINVAL);
    }

    avio_skip(pb, 4);
    audio_subsegments = avio_rl16(pb);

    if (audio_subsegments == 0) {
        av_log_ask_for_sample(s, "MTV files without audio are not supported\n");
        return AVERROR_PATCHWELCOME;
    }

    mtv->full_segment_size =
        audio_subsegments * (MTV_AUDIO_PADDING_SIZE + MTV_ASUBCHUNK_DATA_SIZE) +
        mtv->img_segment_size;
    mtv->video_fps         = (mtv->audio_br / 4) / audio_subsegments;

    // FIXME Add sanity check here

    // all systems go! init decoders

    // video - raw rgb565

    st = avformat_new_stream(s, NULL);
    if(!st)
        return AVERROR(ENOMEM);

    avpriv_set_pts_info(st, 64, 1, mtv->video_fps);
    st->codec->codec_type      = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id        = AV_CODEC_ID_RAWVIDEO;
    st->codec->pix_fmt         = AV_PIX_FMT_RGB565BE;
    st->codec->width           = mtv->img_width;
    st->codec->height          = mtv->img_height;
    st->codec->sample_rate     = mtv->video_fps;
    st->codec->extradata       = av_strdup("BottomUp");
    st->codec->extradata_size  = 9;

    // audio - mp3

    st = avformat_new_stream(s, NULL);
    if(!st)
        return AVERROR(ENOMEM);

    avpriv_set_pts_info(st, 64, 1, AUDIO_SAMPLING_RATE);
    st->codec->codec_type      = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id        = AV_CODEC_ID_MP3;
    st->codec->bit_rate        = mtv->audio_br;
    st->need_parsing           = AVSTREAM_PARSE_FULL;

    // Jump over header

    if(avio_seek(pb, MTV_HEADER_SIZE, SEEK_SET) != MTV_HEADER_SIZE)
        return AVERROR(EIO);

    return 0;

}

static int mtv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MTVDemuxContext *mtv = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret;

    if((avio_tell(pb) - s->data_offset + mtv->img_segment_size) % mtv->full_segment_size)
    {
        avio_skip(pb, MTV_AUDIO_PADDING_SIZE);

        ret = av_get_packet(pb, pkt, MTV_ASUBCHUNK_DATA_SIZE);
        if(ret < 0)
            return ret;

        pkt->pos -= MTV_AUDIO_PADDING_SIZE;
        pkt->stream_index = 1;

    }else
    {
        ret = av_get_packet(pb, pkt, mtv->img_segment_size);
        if(ret < 0)
            return ret;

        pkt->stream_index = 0;
    }

    return ret;
}

AVInputFormat ff_mtv_demuxer = {
    .name           = "mtv",
    .long_name      = NULL_IF_CONFIG_SMALL("MTV"),
    .priv_data_size = sizeof(MTVDemuxContext),
    .read_probe     = mtv_probe,
    .read_header    = mtv_read_header,
    .read_packet    = mtv_read_packet,
};
