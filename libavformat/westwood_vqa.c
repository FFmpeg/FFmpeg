/*
 * Westwood Studios VQA Format Demuxer
 * Copyright (c) 2003 The ffmpeg Project
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
 * Westwood Studios VQA file demuxer
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the Westwood file formats, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *   http://www.geocities.com/SiliconValley/8682/aud3.txt
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

#define FORM_TAG MKBETAG('F', 'O', 'R', 'M')
#define WVQA_TAG MKBETAG('W', 'V', 'Q', 'A')
#define VQHD_TAG MKBETAG('V', 'Q', 'H', 'D')
#define FINF_TAG MKBETAG('F', 'I', 'N', 'F')
#define SND0_TAG MKBETAG('S', 'N', 'D', '0')
#define SND1_TAG MKBETAG('S', 'N', 'D', '1')
#define SND2_TAG MKBETAG('S', 'N', 'D', '2')
#define VQFR_TAG MKBETAG('V', 'Q', 'F', 'R')

/* don't know what these tags are for, but acknowledge their existence */
#define CINF_TAG MKBETAG('C', 'I', 'N', 'F')
#define CINH_TAG MKBETAG('C', 'I', 'N', 'H')
#define CIND_TAG MKBETAG('C', 'I', 'N', 'D')
#define PINF_TAG MKBETAG('P', 'I', 'N', 'F')
#define PINH_TAG MKBETAG('P', 'I', 'N', 'H')
#define PIND_TAG MKBETAG('P', 'I', 'N', 'D')
#define CMDS_TAG MKBETAG('C', 'M', 'D', 'S')

#define VQA_HEADER_SIZE 0x2A
#define VQA_PREAMBLE_SIZE 8

typedef struct WsVqaDemuxContext {
    int audio_channels;
    int audio_stream_index;
    int video_stream_index;
} WsVqaDemuxContext;

static int wsvqa_probe(AVProbeData *p)
{
    /* need 12 bytes to qualify */
    if (p->buf_size < 12)
        return 0;

    /* check for the VQA signatures */
    if ((AV_RB32(&p->buf[0]) != FORM_TAG) ||
        (AV_RB32(&p->buf[8]) != WVQA_TAG))
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int wsvqa_read_header(AVFormatContext *s)
{
    WsVqaDemuxContext *wsvqa = s->priv_data;
    AVIOContext *pb = s->pb;
    AVStream *st;
    unsigned char *header;
    unsigned char scratch[VQA_PREAMBLE_SIZE];
    unsigned int chunk_tag;
    unsigned int chunk_size;
    int fps, version, flags, sample_rate, channels;

    /* initialize the video decoder stream */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    st->start_time = 0;
    wsvqa->video_stream_index = st->index;
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_WS_VQA;
    st->codec->codec_tag = 0;  /* no fourcc */

    /* skip to the start of the VQA header */
    avio_seek(pb, 20, SEEK_SET);

    /* the VQA header needs to go to the decoder */
    st->codec->extradata_size = VQA_HEADER_SIZE;
    st->codec->extradata = av_mallocz(VQA_HEADER_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
    header = (unsigned char *)st->codec->extradata;
    if (avio_read(pb, st->codec->extradata, VQA_HEADER_SIZE) !=
        VQA_HEADER_SIZE) {
        av_free(st->codec->extradata);
        return AVERROR(EIO);
    }
    st->codec->width = AV_RL16(&header[6]);
    st->codec->height = AV_RL16(&header[8]);
    fps = header[12];
    if (fps < 1 || fps > 30) {
        av_log(s, AV_LOG_ERROR, "invalid fps: %d\n", fps);
        return AVERROR_INVALIDDATA;
    }
    avpriv_set_pts_info(st, 64, 1, fps);

    /* initialize the audio decoder stream for VQA v1 or nonzero samplerate */
    version     = AV_RL16(&header[ 0]);
    flags       = AV_RL16(&header[ 2]);
    sample_rate = AV_RL16(&header[24]);
    channels    =          header[26];
    if (sample_rate || (version == 1 && flags == 1)) {
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        st->start_time = 0;
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;

        st->codec->extradata_size = VQA_HEADER_SIZE;
        st->codec->extradata = av_mallocz(VQA_HEADER_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
        if (!st->codec->extradata)
            return AVERROR(ENOMEM);
        memcpy(st->codec->extradata, header, VQA_HEADER_SIZE);

        if (!sample_rate)
            sample_rate = 22050;
        st->codec->sample_rate = sample_rate;
        avpriv_set_pts_info(st, 64, 1, st->codec->sample_rate);

        if (!channels)
            channels = 1;
        st->codec->channels = channels;

        switch (version) {
        case 1:
            st->codec->codec_id = CODEC_ID_WESTWOOD_SND1;
            break;
        case 2:
        case 3:
            st->codec->codec_id = CODEC_ID_ADPCM_IMA_WS;
            st->codec->bits_per_coded_sample = 4;
            st->codec->bit_rate = channels * sample_rate * 4;
            break;
        default:
            /* NOTE: version 0 is supposedly raw pcm_u8 or pcm_s16le, but we do
                     not have any samples to validate this */
            av_log_ask_for_sample(s, "VQA version %d audio\n", version);
            return AVERROR_PATCHWELCOME;
        }

        wsvqa->audio_stream_index = st->index;
        wsvqa->audio_channels = st->codec->channels;
    }

    /* there are 0 or more chunks before the FINF chunk; iterate until
     * FINF has been skipped and the file will be ready to be demuxed */
    do {
        if (avio_read(pb, scratch, VQA_PREAMBLE_SIZE) != VQA_PREAMBLE_SIZE) {
            av_free(st->codec->extradata);
            return AVERROR(EIO);
        }
        chunk_tag = AV_RB32(&scratch[0]);
        chunk_size = AV_RB32(&scratch[4]);

        /* catch any unknown header tags, for curiousity */
        switch (chunk_tag) {
        case CINF_TAG:
        case CINH_TAG:
        case CIND_TAG:
        case PINF_TAG:
        case PINH_TAG:
        case PIND_TAG:
        case FINF_TAG:
        case CMDS_TAG:
            break;

        default:
            av_log (s, AV_LOG_ERROR, " note: unknown chunk seen (%c%c%c%c)\n",
                scratch[0], scratch[1],
                scratch[2], scratch[3]);
            break;
        }

        avio_skip(pb, chunk_size);
    } while (chunk_tag != FINF_TAG);

    return 0;
}

static int wsvqa_read_packet(AVFormatContext *s,
                             AVPacket *pkt)
{
    WsVqaDemuxContext *wsvqa = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret = -1;
    unsigned char preamble[VQA_PREAMBLE_SIZE];
    unsigned int chunk_type;
    unsigned int chunk_size;
    int skip_byte;

    while (avio_read(pb, preamble, VQA_PREAMBLE_SIZE) == VQA_PREAMBLE_SIZE) {
        chunk_type = AV_RB32(&preamble[0]);
        chunk_size = AV_RB32(&preamble[4]);
        skip_byte = chunk_size & 0x01;

        if ((chunk_type == SND2_TAG || chunk_type == SND1_TAG) && wsvqa->audio_channels == 0) {
            av_log(s, AV_LOG_ERROR, "audio chunk without any audio header information found\n");
            return AVERROR_INVALIDDATA;
        }

        if ((chunk_type == SND1_TAG) || (chunk_type == SND2_TAG) || (chunk_type == VQFR_TAG)) {

            if (av_new_packet(pkt, chunk_size))
                return AVERROR(EIO);
            ret = avio_read(pb, pkt->data, chunk_size);
            if (ret != chunk_size) {
                av_free_packet(pkt);
                return AVERROR(EIO);
            }

            if (chunk_type == SND2_TAG) {
                pkt->stream_index = wsvqa->audio_stream_index;
                /* 2 samples/byte, 1 or 2 samples per frame depending on stereo */
                pkt->duration = (chunk_size * 2) / wsvqa->audio_channels;
            } else if(chunk_type == SND1_TAG) {
                pkt->stream_index = wsvqa->audio_stream_index;
                /* unpacked size is stored in header */
                pkt->duration = AV_RL16(pkt->data) / wsvqa->audio_channels;
            } else {
                pkt->stream_index = wsvqa->video_stream_index;
                pkt->duration = 1;
            }
            /* stay on 16-bit alignment */
            if (skip_byte)
                avio_skip(pb, 1);

            return ret;
        } else {
            switch(chunk_type){
            case CMDS_TAG:
            case SND0_TAG:
                break;
            default:
                av_log(s, AV_LOG_INFO, "Skipping unknown chunk 0x%08X\n", chunk_type);
            }
            avio_skip(pb, chunk_size + skip_byte);
        }
    }

    return ret;
}

AVInputFormat ff_wsvqa_demuxer = {
    .name           = "wsvqa",
    .long_name      = NULL_IF_CONFIG_SMALL("Westwood Studios VQA format"),
    .priv_data_size = sizeof(WsVqaDemuxContext),
    .read_probe     = wsvqa_probe,
    .read_header    = wsvqa_read_header,
    .read_packet    = wsvqa_read_packet,
};
