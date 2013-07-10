/*
 * Brute Force & Ignorance (BFI) demuxer
 * Copyright (c) 2008 Sisir Koppaka
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
 * @brief Brute Force & Ignorance (.bfi) file demuxer
 * @author Sisir Koppaka ( sisir.koppaka at gmail dot com )
 * @see http://wiki.multimedia.cx/index.php?title=BFI
 */

#include "libavutil/channel_layout.h"
#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

typedef struct BFIContext {
    int nframes;
    int audio_frame;
    int video_frame;
    int video_size;
    int avflag;
} BFIContext;

static int bfi_probe(AVProbeData * p)
{
    /* Check file header */
    if (AV_RL32(p->buf) == MKTAG('B', 'F', '&', 'I'))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int bfi_read_header(AVFormatContext * s)
{
    BFIContext *bfi = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *vstream;
    AVStream *astream;
    int fps, chunk_header;

    /* Initialize the video codec... */
    vstream = avformat_new_stream(s, NULL);
    if (!vstream)
        return AVERROR(ENOMEM);

    /* Initialize the audio codec... */
    astream = avformat_new_stream(s, NULL);
    if (!astream)
        return AVERROR(ENOMEM);

    /* Set the total number of frames. */
    avio_skip(pb, 8);
    chunk_header           = avio_rl32(pb);
    bfi->nframes           = avio_rl32(pb);
    avio_rl32(pb);
    avio_rl32(pb);
    avio_rl32(pb);
    fps                    = avio_rl32(pb);
    avio_skip(pb, 12);
    vstream->codec->width  = avio_rl32(pb);
    vstream->codec->height = avio_rl32(pb);

    /*Load the palette to extradata */
    avio_skip(pb, 8);
    vstream->codec->extradata      = av_malloc(768);
    if (!vstream->codec->extradata)
        return AVERROR(ENOMEM);
    vstream->codec->extradata_size = 768;
    avio_read(pb, vstream->codec->extradata,
               vstream->codec->extradata_size);

    astream->codec->sample_rate = avio_rl32(pb);

    /* Set up the video codec... */
    avpriv_set_pts_info(vstream, 32, 1, fps);
    vstream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    vstream->codec->codec_id   = AV_CODEC_ID_BFI;
    vstream->codec->pix_fmt    = AV_PIX_FMT_PAL8;
    vstream->nb_frames         =
    vstream->duration          = bfi->nframes;

    /* Set up the audio codec now... */
    astream->codec->codec_type      = AVMEDIA_TYPE_AUDIO;
    astream->codec->codec_id        = AV_CODEC_ID_PCM_U8;
    astream->codec->channels        = 1;
    astream->codec->channel_layout  = AV_CH_LAYOUT_MONO;
    astream->codec->bits_per_coded_sample = 8;
    astream->codec->bit_rate        =
        astream->codec->sample_rate * astream->codec->bits_per_coded_sample;
    avio_seek(pb, chunk_header - 3, SEEK_SET);
    avpriv_set_pts_info(astream, 64, 1, astream->codec->sample_rate);
    return 0;
}


static int bfi_read_packet(AVFormatContext * s, AVPacket * pkt)
{
    BFIContext *bfi = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret, audio_offset, video_offset, chunk_size, audio_size = 0;
    if (bfi->nframes == 0 || url_feof(pb)) {
        return AVERROR_EOF;
    }

    /* If all previous chunks were completely read, then find a new one... */
    if (!bfi->avflag) {
        uint32_t state = 0;
        while(state != MKTAG('S','A','V','I')){
            if (url_feof(pb))
                return AVERROR(EIO);
            state = 256*state + avio_r8(pb);
        }
        /* Now that the chunk's location is confirmed, we proceed... */
        chunk_size      = avio_rl32(pb);
        avio_rl32(pb);
        audio_offset    = avio_rl32(pb);
        avio_rl32(pb);
        video_offset    = avio_rl32(pb);
        audio_size      = video_offset - audio_offset;
        bfi->video_size = chunk_size - video_offset;

        //Tossing an audio packet at the audio decoder.
        ret = av_get_packet(pb, pkt, audio_size);
        if (ret < 0)
            return ret;

        pkt->pts          = bfi->audio_frame;
        bfi->audio_frame += ret;
    }

    else {

        //Tossing a video packet at the video decoder.
        ret = av_get_packet(pb, pkt, bfi->video_size);
        if (ret < 0)
            return ret;

        pkt->pts          = bfi->video_frame;
        bfi->video_frame += bfi->video_size ? ret / bfi->video_size : 1;

        /* One less frame to read. A cursory decrement. */
        bfi->nframes--;
    }

    bfi->avflag       = !bfi->avflag;
    pkt->stream_index = bfi->avflag;
    return ret;
}

AVInputFormat ff_bfi_demuxer = {
    .name           = "bfi",
    .long_name      = NULL_IF_CONFIG_SMALL("Brute Force & Ignorance"),
    .priv_data_size = sizeof(BFIContext),
    .read_probe     = bfi_probe,
    .read_header    = bfi_read_header,
    .read_packet    = bfi_read_packet,
};
