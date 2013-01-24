/*
 * 4X Technologies .4xm File Demuxer (no muxer)
 * Copyright (c) 2003  The ffmpeg Project
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
 * 4X Technologies file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the .4xm file format, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 */

#include "libavutil/intreadwrite.h"
#include "libavutil/intfloat.h"
#include "avformat.h"
#include "internal.h"

#define     RIFF_TAG MKTAG('R', 'I', 'F', 'F')
#define  FOURXMV_TAG MKTAG('4', 'X', 'M', 'V')
#define     LIST_TAG MKTAG('L', 'I', 'S', 'T')
#define     HEAD_TAG MKTAG('H', 'E', 'A', 'D')
#define     TRK__TAG MKTAG('T', 'R', 'K', '_')
#define     MOVI_TAG MKTAG('M', 'O', 'V', 'I')
#define     VTRK_TAG MKTAG('V', 'T', 'R', 'K')
#define     STRK_TAG MKTAG('S', 'T', 'R', 'K')
#define     std__TAG MKTAG('s', 't', 'd', '_')
#define     name_TAG MKTAG('n', 'a', 'm', 'e')
#define     vtrk_TAG MKTAG('v', 't', 'r', 'k')
#define     strk_TAG MKTAG('s', 't', 'r', 'k')
#define     ifrm_TAG MKTAG('i', 'f', 'r', 'm')
#define     pfrm_TAG MKTAG('p', 'f', 'r', 'm')
#define     cfrm_TAG MKTAG('c', 'f', 'r', 'm')
#define     ifr2_TAG MKTAG('i', 'f', 'r', '2')
#define     pfr2_TAG MKTAG('p', 'f', 'r', '2')
#define     cfr2_TAG MKTAG('c', 'f', 'r', '2')
#define     snd__TAG MKTAG('s', 'n', 'd', '_')

#define vtrk_SIZE 0x44
#define strk_SIZE 0x28

#define GET_LIST_HEADER() \
    fourcc_tag = avio_rl32(pb); \
    size = avio_rl32(pb); \
    if (fourcc_tag != LIST_TAG) \
        return AVERROR_INVALIDDATA; \
    fourcc_tag = avio_rl32(pb);

typedef struct AudioTrack {
    int sample_rate;
    int bits;
    int channels;
    int stream_index;
    int adpcm;
    int64_t audio_pts;
} AudioTrack;

typedef struct FourxmDemuxContext {
    int width;
    int height;
    int video_stream_index;
    int track_count;
    AudioTrack *tracks;

    int64_t video_pts;
    float fps;
} FourxmDemuxContext;

static int fourxm_probe(AVProbeData *p)
{
    if ((AV_RL32(&p->buf[0]) != RIFF_TAG) ||
        (AV_RL32(&p->buf[8]) != FOURXMV_TAG))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int fourxm_read_header(AVFormatContext *s)
{
    AVIOContext *pb = s->pb;
    unsigned int fourcc_tag;
    unsigned int size;
    int header_size;
    FourxmDemuxContext *fourxm = s->priv_data;
    unsigned char *header;
    int i, ret;
    AVStream *st;

    fourxm->track_count = 0;
    fourxm->tracks = NULL;
    fourxm->fps = 1.0;

    /* skip the first 3 32-bit numbers */
    avio_skip(pb, 12);

    /* check for LIST-HEAD */
    GET_LIST_HEADER();
    header_size = size - 4;
    if (fourcc_tag != HEAD_TAG || header_size < 0)
        return AVERROR_INVALIDDATA;

    /* allocate space for the header and load the whole thing */
    header = av_malloc(header_size);
    if (!header)
        return AVERROR(ENOMEM);
    if (avio_read(pb, header, header_size) != header_size){
        av_free(header);
        return AVERROR(EIO);
    }

    /* take the lazy approach and search for any and all vtrk and strk chunks */
    for (i = 0; i < header_size - 8; i++) {
        fourcc_tag = AV_RL32(&header[i]);
        size = AV_RL32(&header[i + 4]);
        if (size > header_size - i - 8 && (fourcc_tag == vtrk_TAG || fourcc_tag == strk_TAG)) {
            av_log(s, AV_LOG_ERROR, "chunk larger than array %d>%d\n", size, header_size - i - 8);
            return AVERROR_INVALIDDATA;
        }

        if (fourcc_tag == std__TAG) {
            if (header_size < i + 16) {
                av_log(s, AV_LOG_ERROR, "std TAG truncated\n");
                return AVERROR_INVALIDDATA;
            }
            fourxm->fps = av_int2float(AV_RL32(&header[i + 12]));
        } else if (fourcc_tag == vtrk_TAG) {
            /* check that there is enough data */
            if (size != vtrk_SIZE) {
                ret= AVERROR_INVALIDDATA;
                goto fail;
            }
            fourxm->width  = AV_RL32(&header[i + 36]);
            fourxm->height = AV_RL32(&header[i + 40]);

            /* allocate a new AVStream */
            st = avformat_new_stream(s, NULL);
            if (!st){
                ret= AVERROR(ENOMEM);
                goto fail;
            }
            avpriv_set_pts_info(st, 60, 1, fourxm->fps);

            fourxm->video_stream_index = st->index;

            st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
            st->codec->codec_id = AV_CODEC_ID_4XM;
            st->codec->extradata_size = 4;
            st->codec->extradata = av_malloc(4);
            AV_WL32(st->codec->extradata, AV_RL32(&header[i + 16]));
            st->codec->width  = fourxm->width;
            st->codec->height = fourxm->height;

            i += 8 + size;
        } else if (fourcc_tag == strk_TAG) {
            int current_track;
            /* check that there is enough data */
            if (size != strk_SIZE) {
                ret= AVERROR_INVALIDDATA;
                goto fail;
            }
            current_track = AV_RL32(&header[i + 8]);
            if((unsigned)current_track >= UINT_MAX / sizeof(AudioTrack) - 1){
                av_log(s, AV_LOG_ERROR, "current_track too large\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            if (current_track + 1 > fourxm->track_count) {
                fourxm->tracks = av_realloc_f(fourxm->tracks,
                                              sizeof(AudioTrack),
                                              current_track + 1);
                if (!fourxm->tracks) {
                    ret = AVERROR(ENOMEM);
                    goto fail;
                }
                memset(&fourxm->tracks[fourxm->track_count], 0,
                       sizeof(AudioTrack) * (current_track + 1 - fourxm->track_count));
                fourxm->track_count = current_track + 1;
            }
            fourxm->tracks[current_track].adpcm       = AV_RL32(&header[i + 12]);
            fourxm->tracks[current_track].channels    = AV_RL32(&header[i + 36]);
            fourxm->tracks[current_track].sample_rate = AV_RL32(&header[i + 40]);
            fourxm->tracks[current_track].bits        = AV_RL32(&header[i + 44]);
            fourxm->tracks[current_track].audio_pts   = 0;
            if(   fourxm->tracks[current_track].channels    <= 0
               || fourxm->tracks[current_track].sample_rate <= 0
               || fourxm->tracks[current_track].bits        <  0){
                av_log(s, AV_LOG_ERROR, "audio header invalid\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            if(!fourxm->tracks[current_track].adpcm && fourxm->tracks[current_track].bits<8){
                av_log(s, AV_LOG_ERROR, "bits unspecified for non ADPCM\n");
                ret = AVERROR_INVALIDDATA;
                goto fail;
            }
            i += 8 + size;

            /* allocate a new AVStream */
            st = avformat_new_stream(s, NULL);
            if (!st){
                ret= AVERROR(ENOMEM);
                goto fail;
            }

            st->id = current_track;
            avpriv_set_pts_info(st, 60, 1, fourxm->tracks[current_track].sample_rate);

            fourxm->tracks[current_track].stream_index = st->index;

            st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
            st->codec->codec_tag = 0;
            st->codec->channels              = fourxm->tracks[current_track].channels;
            st->codec->sample_rate           = fourxm->tracks[current_track].sample_rate;
            st->codec->bits_per_coded_sample = fourxm->tracks[current_track].bits;
            st->codec->bit_rate              = st->codec->channels * st->codec->sample_rate *
                st->codec->bits_per_coded_sample;
            st->codec->block_align = st->codec->channels * st->codec->bits_per_coded_sample;
            if (fourxm->tracks[current_track].adpcm){
                st->codec->codec_id = AV_CODEC_ID_ADPCM_4XM;
            }else if (st->codec->bits_per_coded_sample == 8){
                st->codec->codec_id = AV_CODEC_ID_PCM_U8;
            }else
                st->codec->codec_id = AV_CODEC_ID_PCM_S16LE;
        }
    }

    /* skip over the LIST-MOVI chunk (which is where the stream should be */
    GET_LIST_HEADER();
    if (fourcc_tag != MOVI_TAG){
        ret= AVERROR_INVALIDDATA;
        goto fail;
    }

    av_free(header);
    /* initialize context members */
    fourxm->video_pts = -1;  /* first frame will push to 0 */

    return 0;
fail:
    av_freep(&fourxm->tracks);
    av_free(header);
    return ret;
}

static int fourxm_read_packet(AVFormatContext *s,
                              AVPacket *pkt)
{
    FourxmDemuxContext *fourxm = s->priv_data;
    AVIOContext *pb = s->pb;
    unsigned int fourcc_tag;
    unsigned int size;
    int ret = 0;
    unsigned int track_number;
    int packet_read = 0;
    unsigned char header[8];
    int audio_frame_count;

    while (!packet_read) {

        if ((ret = avio_read(s->pb, header, 8)) < 0)
            return ret;
        fourcc_tag = AV_RL32(&header[0]);
        size = AV_RL32(&header[4]);
        if (url_feof(pb))
            return AVERROR(EIO);
        switch (fourcc_tag) {

        case LIST_TAG:
            /* this is a good time to bump the video pts */
            fourxm->video_pts ++;

            /* skip the LIST-* tag and move on to the next fourcc */
            avio_rl32(pb);
            break;

        case ifrm_TAG:
        case pfrm_TAG:
        case cfrm_TAG:
        case ifr2_TAG:
        case pfr2_TAG:
        case cfr2_TAG:
            /* allocate 8 more bytes than 'size' to account for fourcc
             * and size */
            if (size + 8 < size || av_new_packet(pkt, size + 8))
                return AVERROR(EIO);
            pkt->stream_index = fourxm->video_stream_index;
            pkt->pts = fourxm->video_pts;
            pkt->pos = avio_tell(s->pb);
            memcpy(pkt->data, header, 8);
            ret = avio_read(s->pb, &pkt->data[8], size);

            if (ret < 0){
                av_free_packet(pkt);
            }else
                packet_read = 1;
            break;

        case snd__TAG:
            track_number = avio_rl32(pb);
            avio_skip(pb, 4);
            size-=8;

            if (track_number < fourxm->track_count && fourxm->tracks[track_number].channels>0) {
                ret= av_get_packet(s->pb, pkt, size);
                if(ret<0)
                    return AVERROR(EIO);
                pkt->stream_index =
                    fourxm->tracks[track_number].stream_index;
                pkt->pts = fourxm->tracks[track_number].audio_pts;
                packet_read = 1;

                /* pts accounting */
                audio_frame_count = size;
                if (fourxm->tracks[track_number].adpcm)
                    audio_frame_count -=
                        2 * (fourxm->tracks[track_number].channels);
                audio_frame_count /=
                      fourxm->tracks[track_number].channels;
                if (fourxm->tracks[track_number].adpcm){
                    audio_frame_count *= 2;
                }else
                    audio_frame_count /=
                    (fourxm->tracks[track_number].bits / 8);
                fourxm->tracks[track_number].audio_pts += audio_frame_count;

            } else {
                avio_skip(pb, size);
            }
            break;

        default:
            avio_skip(pb, size);
            break;
        }
    }
    return ret;
}

static int fourxm_read_close(AVFormatContext *s)
{
    FourxmDemuxContext *fourxm = s->priv_data;

    av_freep(&fourxm->tracks);

    return 0;
}

AVInputFormat ff_fourxm_demuxer = {
    .name           = "4xm",
    .long_name      = NULL_IF_CONFIG_SMALL("4X Technologies"),
    .priv_data_size = sizeof(FourxmDemuxContext),
    .read_probe     = fourxm_probe,
    .read_header    = fourxm_read_header,
    .read_packet    = fourxm_read_packet,
    .read_close     = fourxm_read_close,
};
