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

#define MTV_ASUBCHUNK_DATA_SIZE 500
#define MTV_HEADER_SIZE 512
#define MTV_AUDIO_PADDING_SIZE 12
#define AUDIO_SAMPLING_RATE 44100
#define VIDEO_SID 0
#define AUDIO_SID 1

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
    if(*(p->buf) != 'A' || *(p->buf+1) != 'M' || *(p->buf+2) != 'V')
        return 0;

    /* Check for nonzero in bpp and (width|height) header fields */
    if(!(p->buf[51] && AV_RL16(&p->buf[52]) | AV_RL16(&p->buf[54])))
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

static int mtv_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    MTVDemuxContext *mtv = s->priv_data;
    ByteIOContext   *pb  = s->pb;
    AVStream        *st;
    unsigned int    audio_subsegments;

    url_fskip(pb, 3);
    mtv->file_size         = get_le32(pb);
    mtv->segments          = get_le32(pb);
    url_fskip(pb, 32);
    mtv->audio_identifier  = get_le24(pb);
    mtv->audio_br          = get_le16(pb);
    mtv->img_colorfmt      = get_le24(pb);
    mtv->img_bpp           = get_byte(pb);
    mtv->img_width         = get_le16(pb);
    mtv->img_height        = get_le16(pb);
    mtv->img_segment_size  = get_le16(pb);

    /* Calculate width and height if missing from header */

    if(!mtv->img_width)
        mtv->img_width=mtv->img_segment_size / (mtv->img_bpp>>3)
                        / mtv->img_height;

    if(!mtv->img_height)
        mtv->img_height=mtv->img_segment_size / (mtv->img_bpp>>3)
                        / mtv->img_width;

    url_fskip(pb, 4);
    audio_subsegments = get_le16(pb);
    mtv->full_segment_size =
        audio_subsegments * (MTV_AUDIO_PADDING_SIZE + MTV_ASUBCHUNK_DATA_SIZE) +
        mtv->img_segment_size;
    mtv->video_fps         = (mtv->audio_br / 4) / audio_subsegments;

    // FIXME Add sanity check here

    // all systems go! init decoders

    // video - raw rgb565

    st = av_new_stream(s, VIDEO_SID);
    if(!st)
        return AVERROR(ENOMEM);

    av_set_pts_info(st, 64, 1, mtv->video_fps);
    st->codec->codec_type      = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id        = CODEC_ID_RAWVIDEO;
    st->codec->pix_fmt         = PIX_FMT_RGB565;
    st->codec->width           = mtv->img_width;
    st->codec->height          = mtv->img_height;
    st->codec->sample_rate     = mtv->video_fps;
    st->codec->extradata       = av_strdup("BottomUp");
    st->codec->extradata_size  = 9;

    // audio - mp3

    st = av_new_stream(s, AUDIO_SID);
    if(!st)
        return AVERROR(ENOMEM);

    av_set_pts_info(st, 64, 1, AUDIO_SAMPLING_RATE);
    st->codec->codec_type      = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id        = CODEC_ID_MP3;
    st->codec->bit_rate        = mtv->audio_br;
    st->need_parsing           = AVSTREAM_PARSE_FULL;

    // Jump over header

    if(url_fseek(pb, MTV_HEADER_SIZE, SEEK_SET) != MTV_HEADER_SIZE)
        return AVERROR(EIO);

    return 0;

}

static int mtv_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    MTVDemuxContext *mtv = s->priv_data;
    ByteIOContext *pb = s->pb;
    int ret;
#if !HAVE_BIGENDIAN
    int i;
#endif

    if((url_ftell(pb) - s->data_offset + mtv->img_segment_size) % mtv->full_segment_size)
    {
        url_fskip(pb, MTV_AUDIO_PADDING_SIZE);

        ret = av_get_packet(pb, pkt, MTV_ASUBCHUNK_DATA_SIZE);
        if(ret < 0)
            return ret;

        pkt->pos -= MTV_AUDIO_PADDING_SIZE;
        pkt->stream_index = AUDIO_SID;

    }else
    {
        ret = av_get_packet(pb, pkt, mtv->img_segment_size);
        if(ret < 0)
            return ret;

#if !HAVE_BIGENDIAN

        /* pkt->data is GGGRRRR BBBBBGGG
         * and we need RRRRRGGG GGGBBBBB
         * for PIX_FMT_RGB565 so here we
         * just swap bytes as they come
         */

        for(i=0;i<mtv->img_segment_size/2;i++)
            *((uint16_t *)pkt->data+i) = bswap_16(*((uint16_t *)pkt->data+i));
#endif
        pkt->stream_index = VIDEO_SID;
    }

    return ret;
}

AVInputFormat mtv_demuxer = {
    "MTV",
    NULL_IF_CONFIG_SMALL("MTV format"),
    sizeof(MTVDemuxContext),
    mtv_probe,
    mtv_read_header,
    mtv_read_packet,
};
