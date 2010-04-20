/*
 * Westwood Studios Multimedia Formats Demuxer (VQA, AUD)
 * Copyright (c) 2003 The ffmpeg Project
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
 * Westwood Studios VQA & AUD file demuxers
 * by Mike Melanson (melanson@pcisys.net)
 * for more information on the Westwood file formats, visit:
 *   http://www.pcisys.net/~melanson/codecs/
 *   http://www.geocities.com/SiliconValley/8682/aud3.txt
 *
 * Implementation note: There is no definite file signature for AUD files.
 * The demuxer uses a probabilistic strategy for content detection. This
 * entails performing sanity checks on certain header values in order to
 * qualify a file. Refer to wsaud_probe() for the precise parameters.
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define AUD_HEADER_SIZE 12
#define AUD_CHUNK_PREAMBLE_SIZE 8
#define AUD_CHUNK_SIGNATURE 0x0000DEAF

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
#define VQA_FRAMERATE 15
#define VQA_PREAMBLE_SIZE 8

typedef struct WsAudDemuxContext {
    int audio_samplerate;
    int audio_channels;
    int audio_bits;
    enum CodecID audio_type;
    int audio_stream_index;
    int64_t audio_frame_counter;
} WsAudDemuxContext;

typedef struct WsVqaDemuxContext {
    int audio_samplerate;
    int audio_channels;
    int audio_bits;

    int audio_stream_index;
    int video_stream_index;

    int64_t audio_frame_counter;
} WsVqaDemuxContext;

static int wsaud_probe(AVProbeData *p)
{
    int field;

    /* Probabilistic content detection strategy: There is no file signature
     * so perform sanity checks on various header parameters:
     *   8000 <= sample rate (16 bits) <= 48000  ==> 40001 acceptable numbers
     *   flags <= 0x03 (2 LSBs are used)         ==> 4 acceptable numbers
     *   compression type (8 bits) = 1 or 99     ==> 2 acceptable numbers
     *   first audio chunk signature (32 bits)   ==> 1 acceptable number
     * The number space contains 2^64 numbers. There are 40001 * 4 * 2 * 1 =
     * 320008 acceptable number combinations.
     */

    if (p->buf_size < AUD_HEADER_SIZE + AUD_CHUNK_PREAMBLE_SIZE)
        return 0;

    /* check sample rate */
    field = AV_RL16(&p->buf[0]);
    if ((field < 8000) || (field > 48000))
        return 0;

    /* enforce the rule that the top 6 bits of this flags field are reserved (0);
     * this might not be true, but enforce it until deemed unnecessary */
    if (p->buf[10] & 0xFC)
        return 0;

    /* note: only check for WS IMA (type 99) right now since there is no
     * support for type 1 */
    if (p->buf[11] != 99)
        return 0;

    /* read ahead to the first audio chunk and validate the first header signature */
    if (AV_RL32(&p->buf[16]) != AUD_CHUNK_SIGNATURE)
        return 0;

    /* return 1/2 certainty since this file check is a little sketchy */
    return AVPROBE_SCORE_MAX / 2;
}

static int wsaud_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    WsAudDemuxContext *wsaud = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    unsigned char header[AUD_HEADER_SIZE];

    if (get_buffer(pb, header, AUD_HEADER_SIZE) != AUD_HEADER_SIZE)
        return AVERROR(EIO);
    wsaud->audio_samplerate = AV_RL16(&header[0]);
    if (header[11] == 99)
        wsaud->audio_type = CODEC_ID_ADPCM_IMA_WS;
    else
        return AVERROR_INVALIDDATA;

    /* flag 0 indicates stereo */
    wsaud->audio_channels = (header[10] & 0x1) + 1;
    /* flag 1 indicates 16 bit audio */
    wsaud->audio_bits = (((header[10] & 0x2) >> 1) + 1) * 8;

    /* initialize the audio decoder stream */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    av_set_pts_info(st, 33, 1, wsaud->audio_samplerate);
    st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
    st->codec->codec_id = wsaud->audio_type;
    st->codec->codec_tag = 0;  /* no tag */
    st->codec->channels = wsaud->audio_channels;
    st->codec->sample_rate = wsaud->audio_samplerate;
    st->codec->bits_per_coded_sample = wsaud->audio_bits;
    st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
        st->codec->bits_per_coded_sample / 4;
    st->codec->block_align = st->codec->channels * st->codec->bits_per_coded_sample;

    wsaud->audio_stream_index = st->index;
    wsaud->audio_frame_counter = 0;

    return 0;
}

static int wsaud_read_packet(AVFormatContext *s,
                             AVPacket *pkt)
{
    WsAudDemuxContext *wsaud = s->priv_data;
    ByteIOContext *pb = s->pb;
    unsigned char preamble[AUD_CHUNK_PREAMBLE_SIZE];
    unsigned int chunk_size;
    int ret = 0;

    if (get_buffer(pb, preamble, AUD_CHUNK_PREAMBLE_SIZE) !=
        AUD_CHUNK_PREAMBLE_SIZE)
        return AVERROR(EIO);

    /* validate the chunk */
    if (AV_RL32(&preamble[4]) != AUD_CHUNK_SIGNATURE)
        return AVERROR_INVALIDDATA;

    chunk_size = AV_RL16(&preamble[0]);
    ret= av_get_packet(pb, pkt, chunk_size);
    if (ret != chunk_size)
        return AVERROR(EIO);
    pkt->stream_index = wsaud->audio_stream_index;
    pkt->pts = wsaud->audio_frame_counter;
    pkt->pts /= wsaud->audio_samplerate;

    /* 2 samples/byte, 1 or 2 samples per frame depending on stereo */
    wsaud->audio_frame_counter += (chunk_size * 2) / wsaud->audio_channels;

    return ret;
}

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

static int wsvqa_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    WsVqaDemuxContext *wsvqa = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    unsigned char *header;
    unsigned char scratch[VQA_PREAMBLE_SIZE];
    unsigned int chunk_tag;
    unsigned int chunk_size;

    /* initialize the video decoder stream */
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    av_set_pts_info(st, 33, 1, VQA_FRAMERATE);
    wsvqa->video_stream_index = st->index;
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_WS_VQA;
    st->codec->codec_tag = 0;  /* no fourcc */

    /* skip to the start of the VQA header */
    url_fseek(pb, 20, SEEK_SET);

    /* the VQA header needs to go to the decoder */
    st->codec->extradata_size = VQA_HEADER_SIZE;
    st->codec->extradata = av_mallocz(VQA_HEADER_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);
    header = (unsigned char *)st->codec->extradata;
    if (get_buffer(pb, st->codec->extradata, VQA_HEADER_SIZE) !=
        VQA_HEADER_SIZE) {
        av_free(st->codec->extradata);
        return AVERROR(EIO);
    }
    st->codec->width = AV_RL16(&header[6]);
    st->codec->height = AV_RL16(&header[8]);

    /* initialize the audio decoder stream for VQA v1 or nonzero samplerate */
    if (AV_RL16(&header[24]) || (AV_RL16(&header[0]) == 1 && AV_RL16(&header[2]) == 1)) {
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR(ENOMEM);
        av_set_pts_info(st, 33, 1, VQA_FRAMERATE);
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        if (AV_RL16(&header[0]) == 1)
            st->codec->codec_id = CODEC_ID_WESTWOOD_SND1;
        else
            st->codec->codec_id = CODEC_ID_ADPCM_IMA_WS;
        st->codec->codec_tag = 0;  /* no tag */
        st->codec->sample_rate = AV_RL16(&header[24]);
        if (!st->codec->sample_rate)
            st->codec->sample_rate = 22050;
        st->codec->channels = header[26];
        if (!st->codec->channels)
            st->codec->channels = 1;
        st->codec->bits_per_coded_sample = 16;
        st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
            st->codec->bits_per_coded_sample / 4;
        st->codec->block_align = st->codec->channels * st->codec->bits_per_coded_sample;

        wsvqa->audio_stream_index = st->index;
        wsvqa->audio_samplerate = st->codec->sample_rate;
        wsvqa->audio_channels = st->codec->channels;
        wsvqa->audio_frame_counter = 0;
    }

    /* there are 0 or more chunks before the FINF chunk; iterate until
     * FINF has been skipped and the file will be ready to be demuxed */
    do {
        if (get_buffer(pb, scratch, VQA_PREAMBLE_SIZE) != VQA_PREAMBLE_SIZE) {
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

        url_fseek(pb, chunk_size, SEEK_CUR);
    } while (chunk_tag != FINF_TAG);

    return 0;
}

static int wsvqa_read_packet(AVFormatContext *s,
                             AVPacket *pkt)
{
    WsVqaDemuxContext *wsvqa = s->priv_data;
    ByteIOContext *pb = s->pb;
    int ret = -1;
    unsigned char preamble[VQA_PREAMBLE_SIZE];
    unsigned int chunk_type;
    unsigned int chunk_size;
    int skip_byte;

    while (get_buffer(pb, preamble, VQA_PREAMBLE_SIZE) == VQA_PREAMBLE_SIZE) {
        chunk_type = AV_RB32(&preamble[0]);
        chunk_size = AV_RB32(&preamble[4]);
        skip_byte = chunk_size & 0x01;

        if ((chunk_type == SND1_TAG) || (chunk_type == SND2_TAG) || (chunk_type == VQFR_TAG)) {

            if (av_new_packet(pkt, chunk_size))
                return AVERROR(EIO);
            ret = get_buffer(pb, pkt->data, chunk_size);
            if (ret != chunk_size) {
                av_free_packet(pkt);
                return AVERROR(EIO);
            }

            if (chunk_type == SND2_TAG) {
                pkt->stream_index = wsvqa->audio_stream_index;
                /* 2 samples/byte, 1 or 2 samples per frame depending on stereo */
                wsvqa->audio_frame_counter += (chunk_size * 2) / wsvqa->audio_channels;
            } else if(chunk_type == SND1_TAG) {
                pkt->stream_index = wsvqa->audio_stream_index;
                /* unpacked size is stored in header */
                wsvqa->audio_frame_counter += AV_RL16(pkt->data) / wsvqa->audio_channels;
            } else {
                pkt->stream_index = wsvqa->video_stream_index;
            }
            /* stay on 16-bit alignment */
            if (skip_byte)
                url_fseek(pb, 1, SEEK_CUR);

            return ret;
        } else {
            switch(chunk_type){
            case CMDS_TAG:
            case SND0_TAG:
                break;
            default:
                av_log(s, AV_LOG_INFO, "Skipping unknown chunk 0x%08X\n", chunk_type);
            }
            url_fseek(pb, chunk_size + skip_byte, SEEK_CUR);
        }
    }

    return ret;
}

#if CONFIG_WSAUD_DEMUXER
AVInputFormat wsaud_demuxer = {
    "wsaud",
    NULL_IF_CONFIG_SMALL("Westwood Studios audio format"),
    sizeof(WsAudDemuxContext),
    wsaud_probe,
    wsaud_read_header,
    wsaud_read_packet,
};
#endif
#if CONFIG_WSVQA_DEMUXER
AVInputFormat wsvqa_demuxer = {
    "wsvqa",
    NULL_IF_CONFIG_SMALL("Westwood Studios VQA format"),
    sizeof(WsVqaDemuxContext),
    wsvqa_probe,
    wsvqa_read_header,
    wsvqa_read_packet,
};
#endif
