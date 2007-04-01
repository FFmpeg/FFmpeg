/*
 * THP Demuxer
 * Copyright (c) 2007 Marco Gerards.
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
#include "allformats.h"

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
    int              compcount;
    unsigned char    components[16];
    AVStream*        vst;
    int              has_audio;
} ThpDemuxContext;


static int thp_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size < 4)
        return 0;

    if (AV_RL32(p->buf) == MKTAG('T', 'H', 'P', '\0'))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int thp_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
  ThpDemuxContext *thp = s->priv_data;
  AVStream *st;
  ByteIOContext *pb = &s->pb;
  int i;

  /* Read the file header.  */

                         get_be32(pb); /* Skip Magic.  */
  thp->version         = get_be32(pb);

                         get_be32(pb); /* Max buf size.  */
                         get_be32(pb); /* Max samples.  */

  thp->fps             = av_d2q(av_int2flt(get_be32(pb)), INT_MAX);
  thp->framecnt        = get_be32(pb);
  thp->first_framesz   = get_be32(pb);
                         get_be32(pb); /* Data size.  */

  thp->compoff         = get_be32(pb);
                         get_be32(pb); /* offsetDataOffset.  */
  thp->first_frame     = get_be32(pb);
  thp->last_frame      = get_be32(pb);

  thp->next_framesz    = thp->first_framesz;
  thp->next_frame      = thp->first_frame;

  /* Read the component structure.  */
  url_fseek (pb, thp->compoff, SEEK_SET);
  thp->compcount       = get_be32(pb);

  /* Read the list of component types.  */
  get_buffer(pb, thp->components, 16);

  for (i = 0; i < thp->compcount; i++) {
      if (thp->components[i] == 0) {
          if (thp->vst != 0)
             break;

          /* Video component.  */
          st = av_new_stream(s, 0);
          if (!st)
             return AVERROR_NOMEM;

          /* The denominator and numerator are switched because 1/fps
             is required.  */
          av_set_pts_info(st, 64, thp->fps.den, thp->fps.num);
          st->codec->codec_type = CODEC_TYPE_VIDEO;
          st->codec->codec_id = CODEC_ID_THP;
          st->codec->codec_tag = 0;  /* no fourcc */
          st->codec->width = get_be32(pb);
          st->codec->height = get_be32(pb);
          st->codec->sample_rate = av_q2d(thp->fps);
          thp->vst = st;
          thp->video_stream_index = st->index;

          if (thp->version == 0x11000)
             get_be32(pb); /* Unknown.  */
        }
      else if (thp->components[i] == 1) {
          /* XXX: Required for audio playback.  */
          thp->has_audio = 1;
      }
    }

  return 0;
}

static int thp_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    ThpDemuxContext *thp = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int size;
    int ret;

    /* Terminate when last frame is reached.  */
    if (thp->frame >= thp->framecnt)
       return AVERROR_IO;

    url_fseek(pb, thp->next_frame, SEEK_SET);

    /* Locate the next frame and read out its size.  */
    thp->next_frame += thp->next_framesz;
    thp->next_framesz = get_be32(pb);

                        get_be32(pb); /* Previous total size.  */
    size              = get_be32(pb); /* Total size of this frame.  */

    if (thp->has_audio)
                        get_be32(pb); /* Audio size.  */

    ret = av_get_packet(pb, pkt, size);
    if (ret != size) {
       av_free_packet(pkt);
       return AVERROR_IO;
    }

    pkt->stream_index = thp->video_stream_index;
    thp->frame++;

    return 0;
}

AVInputFormat thp_demuxer = {
    "thp",
    "THP",
    sizeof(ThpDemuxContext),
    thp_probe,
    thp_read_header,
    thp_read_packet
};
