/*
 * THP Demuxer
 * Copyright (c) 2007 Marco Gerards
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

#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "avformat.h"
#include "internal.h"

typedef struct ThpDemuxContext {
    int              version;
    int              first_frame;
    int              first_framesz;
    int              last_frame;
    int              compoff;
    int              framecnt;
    AVRational       fps;
    int              frame;
    int              next_frame;
    int              next_framesz;
    int              video_stream_index;
    int              audio_stream_index;
    int              compcount;
    unsigned char    components[16];
    AVStream*        vst;
    int              has_audio;
    unsigned         audiosize;
} ThpDemuxContext;


static int thp_probe(AVProbeData *p)
{
    /* check file header */
    if (AV_RL32(p->buf) == MKTAG('T', 'H', 'P', '\0'))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int thp_read_header(AVFormatContext *s)
{
    ThpDemuxContext *thp = s->priv_data;
    AVStream *st;
    AVIOContext *pb = s->pb;
    int64_t fsize= avio_size(pb);
    int i;

    /* Read the file header.  */
                           avio_rb32(pb); /* Skip Magic.  */
    thp->version         = avio_rb32(pb);

                           avio_rb32(pb); /* Max buf size.  */
                           avio_rb32(pb); /* Max samples.  */

    thp->fps             = av_d2q(av_int2float(avio_rb32(pb)), INT_MAX);
    thp->framecnt        = avio_rb32(pb);
    thp->first_framesz   = avio_rb32(pb);
    pb->maxsize          = avio_rb32(pb);
    if(fsize>0 && (!pb->maxsize || fsize < pb->maxsize))
        pb->maxsize= fsize;

    thp->compoff         = avio_rb32(pb);
                           avio_rb32(pb); /* offsetDataOffset.  */
    thp->first_frame     = avio_rb32(pb);
    thp->last_frame      = avio_rb32(pb);

    thp->next_framesz    = thp->first_framesz;
    thp->next_frame      = thp->first_frame;

    /* Read the component structure.  */
    avio_seek (pb, thp->compoff, SEEK_SET);
    thp->compcount       = avio_rb32(pb);

    /* Read the list of component types.  */
    avio_read(pb, thp->components, 16);

    for (i = 0; i < thp->compcount; i++) {
        if (thp->components[i] == 0) {
            if (thp->vst != 0)
                break;

            /* Video component.  */
            st = avformat_new_stream(s, NULL);
            if (!st)
                return AVERROR(ENOMEM);

            /* The denominator and numerator are switched because 1/fps
               is required.  */
            avpriv_set_pts_info(st, 64, thp->fps.den, thp->fps.num);
            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_id = AV_CODEC_ID_THP;
            st->codec->codec_tag = 0;  /* no fourcc */
            st->codec->width = avio_rb32(pb);
            st->codec->height = avio_rb32(pb);
            st->codec->sample_rate = av_q2d(thp->fps);
            st->nb_frames =
            st->duration = thp->framecnt;
            thp->vst = st;
            thp->video_stream_index = st->index;

            if (thp->version == 0x11000)
                avio_rb32(pb); /* Unknown.  */
        } else if (thp->components[i] == 1) {
            if (thp->has_audio != 0)
                break;

            /* Audio component.  */
            st = avformat_new_stream(s, NULL);
            if (!st)
                return AVERROR(ENOMEM);

            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            st->codec->codec_id = AV_CODEC_ID_ADPCM_THP;
            st->codec->codec_tag = 0;  /* no fourcc */
            st->codec->channels    = avio_rb32(pb); /* numChannels.  */
            st->codec->sample_rate = avio_rb32(pb); /* Frequency.  */

            avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

            thp->audio_stream_index = st->index;
            thp->has_audio = 1;
        }
    }

    return 0;
}

static int thp_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    ThpDemuxContext *thp = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned int size;
    int ret;

    if (thp->audiosize == 0) {
        /* Terminate when last frame is reached.  */
        if (thp->frame >= thp->framecnt)
            return AVERROR_EOF;

        avio_seek(pb, thp->next_frame, SEEK_SET);

        /* Locate the next frame and read out its size.  */
        thp->next_frame += thp->next_framesz;
        thp->next_framesz = avio_rb32(pb);

                        avio_rb32(pb); /* Previous total size.  */
        size          = avio_rb32(pb); /* Total size of this frame.  */

        /* Store the audiosize so the next time this function is called,
           the audio can be read.  */
        if (thp->has_audio)
            thp->audiosize = avio_rb32(pb); /* Audio size.  */
        else
            thp->frame++;

        ret = av_get_packet(pb, pkt, size);
        if (ret != size) {
            av_free_packet(pkt);
            return AVERROR(EIO);
        }

        pkt->stream_index = thp->video_stream_index;
    } else {
        ret = av_get_packet(pb, pkt, thp->audiosize);
        if (ret != thp->audiosize) {
            av_free_packet(pkt);
            return AVERROR(EIO);
        }

        pkt->stream_index = thp->audio_stream_index;
        if (thp->audiosize >= 8)
            pkt->duration = AV_RB32(&pkt->data[4]);

        thp->audiosize = 0;
        thp->frame++;
    }

    return 0;
}

AVInputFormat ff_thp_demuxer = {
    .name           = "thp",
    .long_name      = NULL_IF_CONFIG_SMALL("THP"),
    .priv_data_size = sizeof(ThpDemuxContext),
    .read_probe     = thp_probe,
    .read_header    = thp_read_header,
    .read_packet    = thp_read_packet
};
