/* Electronic Arts Multimedia File Demuxer
 * Copyright (c) 2004  The ffmpeg Project
 * Copyright (c) 2006-2008 Peter Ross
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
 * @file libavformat/electronicarts.c
 * Electronic Arts Multimedia file demuxer (WVE/UV2/etc.)
 * by Robin Kay (komadori at gekkou.co.uk)
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"

#define SCHl_TAG MKTAG('S', 'C', 'H', 'l')
#define SEAD_TAG MKTAG('S', 'E', 'A', 'D')    /* Sxxx header */
#define SNDC_TAG MKTAG('S', 'N', 'D', 'C')    /* Sxxx data */
#define SEND_TAG MKTAG('S', 'E', 'N', 'D')    /* Sxxx end */
#define SHEN_TAG MKTAG('S', 'H', 'E', 'N')    /* SxEN header */
#define SDEN_TAG MKTAG('S', 'D', 'E', 'N')    /* SxEN data */
#define SEEN_TAG MKTAG('S', 'E', 'E', 'N')    /* SxEN end */
#define ISNh_TAG MKTAG('1', 'S', 'N', 'h')    /* 1SNx header */
#define EACS_TAG MKTAG('E', 'A', 'C', 'S')
#define ISNd_TAG MKTAG('1', 'S', 'N', 'd')    /* 1SNx data */
#define ISNe_TAG MKTAG('1', 'S', 'N', 'e')    /* 1SNx end */
#define PT00_TAG MKTAG('P', 'T', 0x0, 0x0)
#define GSTR_TAG MKTAG('G', 'S', 'T', 'R')
#define SCDl_TAG MKTAG('S', 'C', 'D', 'l')
#define SCEl_TAG MKTAG('S', 'C', 'E', 'l')
#define kVGT_TAG MKTAG('k', 'V', 'G', 'T')    /* TGV i-frame */
#define fVGT_TAG MKTAG('f', 'V', 'G', 'T')    /* TGV p-frame */
#define mTCD_TAG MKTAG('m', 'T', 'C', 'D')    /* MDEC */
#define MADk_TAG MKTAG('M', 'A', 'D', 'k')    /* MAD i-frame */
#define MPCh_TAG MKTAG('M', 'P', 'C', 'h')    /* MPEG2 */
#define TGQs_TAG MKTAG('T', 'G', 'Q', 's')    /* TGQ i-frame (appears in .TGQ files) */
#define pQGT_TAG MKTAG('p', 'Q', 'G', 'T')    /* TGQ i-frame (appears in .UV files) */
#define pIQT_TAG MKTAG('p', 'I', 'Q', 'T')    /* TQI/UV2 i-frame (.UV2/.WVE) */
#define MVhd_TAG MKTAG('M', 'V', 'h', 'd')
#define MV0K_TAG MKTAG('M', 'V', '0', 'K')
#define MV0F_TAG MKTAG('M', 'V', '0', 'F')
#define MVIh_TAG MKTAG('M', 'V', 'I', 'h')    /* CMV header */
#define MVIf_TAG MKTAG('M', 'V', 'I', 'f')    /* CMV i-frame */

typedef struct EaDemuxContext {
    int big_endian;

    enum CodecID video_codec;
    AVRational time_base;
    int width, height;
    int video_stream_index;

    enum CodecID audio_codec;
    int audio_stream_index;
    int audio_frame_counter;

    int64_t audio_pts;

    int bytes;
    int sample_rate;
    int num_channels;
    int num_samples;
} EaDemuxContext;

static uint32_t read_arbitary(ByteIOContext *pb) {
    uint8_t size, byte;
    int i;
    uint32_t word;

    size = get_byte(pb);

    word = 0;
    for (i = 0; i < size; i++) {
        byte = get_byte(pb);
        word <<= 8;
        word |= byte;
    }

    return word;
}

/*
 * Process PT/GSTR sound header
 * return 1 if success, 0 if invalid format, otherwise AVERROR_xxx
 */
static int process_audio_header_elements(AVFormatContext *s)
{
    int inHeader = 1;
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = s->pb;
    int compression_type = -1, revision = -1, revision2 = -1;

    ea->bytes = 2;
    ea->sample_rate = -1;
    ea->num_channels = 1;

    while (inHeader) {
        int inSubheader;
        uint8_t byte;
        byte = get_byte(pb);

        switch (byte) {
        case 0xFD:
            av_log (s, AV_LOG_INFO, "entered audio subheader\n");
            inSubheader = 1;
            while (inSubheader) {
                uint8_t subbyte;
                subbyte = get_byte(pb);

                switch (subbyte) {
                case 0x80:
                    revision = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "revision (element 0x80) set to 0x%08x\n", revision);
                    break;
                case 0x82:
                    ea->num_channels = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "num_channels (element 0x82) set to 0x%08x\n", ea->num_channels);
                    break;
                case 0x83:
                    compression_type = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "compression_type (element 0x83) set to 0x%08x\n", compression_type);
                    break;
                case 0x84:
                    ea->sample_rate = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "sample_rate (element 0x84) set to %i\n", ea->sample_rate);
                    break;
                case 0x85:
                    ea->num_samples = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "num_samples (element 0x85) set to 0x%08x\n", ea->num_samples);
                    break;
                case 0x8A:
                    av_log (s, AV_LOG_INFO, "element 0x%02x set to 0x%08x\n", subbyte, read_arbitary(pb));
                    av_log (s, AV_LOG_INFO, "exited audio subheader\n");
                    inSubheader = 0;
                    break;
                case 0xA0:
                    revision2 = read_arbitary(pb);
                    av_log (s, AV_LOG_INFO, "revision2 (element 0xA0) set to 0x%08x\n", revision2);
                    break;
                case 0xFF:
                    av_log (s, AV_LOG_INFO, "end of header block reached (within audio subheader)\n");
                    inSubheader = 0;
                    inHeader = 0;
                    break;
                default:
                    av_log (s, AV_LOG_INFO, "element 0x%02x set to 0x%08x\n", subbyte, read_arbitary(pb));
                    break;
                }
            }
            break;
        case 0xFF:
            av_log (s, AV_LOG_INFO, "end of header block reached\n");
            inHeader = 0;
            break;
        default:
            av_log (s, AV_LOG_INFO, "header element 0x%02x set to 0x%08x\n", byte, read_arbitary(pb));
            break;
        }
    }

    switch (compression_type) {
    case  0: ea->audio_codec = CODEC_ID_PCM_S16LE; break;
    case  7: ea->audio_codec = CODEC_ID_ADPCM_EA; break;
    case -1:
        switch (revision) {
        case  1: ea->audio_codec = CODEC_ID_ADPCM_EA_R1; break;
        case  2: ea->audio_codec = CODEC_ID_ADPCM_EA_R2; break;
        case  3: ea->audio_codec = CODEC_ID_ADPCM_EA_R3; break;
        case -1: break;
        default:
            av_log(s, AV_LOG_ERROR, "unsupported stream type; revision=%i\n", revision);
            return 0;
        }
        switch (revision2) {
        case  8: ea->audio_codec = CODEC_ID_PCM_S16LE_PLANAR; break;
        case 10: ea->audio_codec = CODEC_ID_ADPCM_EA_R2; break;
        case 16: ea->audio_codec = CODEC_ID_MP3; break;
        case -1: break;
        default:
            av_log(s, AV_LOG_ERROR, "unsupported stream type; revision2=%i\n", revision2);
            return 0;
        }
        break;
    default:
        av_log(s, AV_LOG_ERROR, "unsupported stream type; compression_type=%i\n", compression_type);
        return 0;
    }

    if (ea->sample_rate == -1)
        ea->sample_rate = revision==3 ? 48000 : 22050;

    return 1;
}

/*
 * Process EACS sound header
 * return 1 if success, 0 if invalid format, otherwise AVERROR_xxx
 */
static int process_audio_header_eacs(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = s->pb;
    int compression_type;

    ea->sample_rate  = ea->big_endian ? get_be32(pb) : get_le32(pb);
    ea->bytes        = get_byte(pb);   /* 1=8-bit, 2=16-bit */
    ea->num_channels = get_byte(pb);
    compression_type = get_byte(pb);
    url_fskip(pb, 13);

    switch (compression_type) {
    case 0:
        switch (ea->bytes) {
        case 1: ea->audio_codec = CODEC_ID_PCM_S8;    break;
        case 2: ea->audio_codec = CODEC_ID_PCM_S16LE; break;
        }
        break;
    case 1: ea->audio_codec = CODEC_ID_PCM_MULAW; ea->bytes = 1; break;
    case 2: ea->audio_codec = CODEC_ID_ADPCM_IMA_EA_EACS; break;
    default:
        av_log (s, AV_LOG_ERROR, "unsupported stream type; audio compression_type=%i\n", compression_type);
    }

    return 1;
}

/*
 * Process SEAD sound header
 * return 1 if success, 0 if invalid format, otherwise AVERROR_xxx
 */
static int process_audio_header_sead(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = s->pb;

    ea->sample_rate  = get_le32(pb);
    ea->bytes        = get_le32(pb);  /* 1=8-bit, 2=16-bit */
    ea->num_channels = get_le32(pb);
    ea->audio_codec  = CODEC_ID_ADPCM_IMA_EA_SEAD;

    return 1;
}

static int process_video_header_mdec(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = s->pb;
    url_fskip(pb, 4);
    ea->width  = get_le16(pb);
    ea->height = get_le16(pb);
    ea->time_base = (AVRational){1,15};
    ea->video_codec = CODEC_ID_MDEC;
    return 1;
}

static int process_video_header_vp6(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = s->pb;

    url_fskip(pb, 16);
    ea->time_base.den = get_le32(pb);
    ea->time_base.num = get_le32(pb);
    ea->video_codec = CODEC_ID_VP6;

    return 1;
}

/*
 * Process EA file header
 * Returns 1 if the EA file is valid and successfully opened, 0 otherwise
 */
static int process_ea_header(AVFormatContext *s) {
    uint32_t blockid, size = 0;
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = s->pb;
    int i;

    for (i=0; i<5 && (!ea->audio_codec || !ea->video_codec); i++) {
        unsigned int startpos = url_ftell(pb);
        int err = 0;

        blockid = get_le32(pb);
        size = get_le32(pb);
        if (i == 0)
            ea->big_endian = size > 0x000FFFFF;
        if (ea->big_endian)
            size = bswap_32(size);

        switch (blockid) {
            case ISNh_TAG:
                if (get_le32(pb) != EACS_TAG) {
                    av_log (s, AV_LOG_ERROR, "unknown 1SNh headerid\n");
                    return 0;
                }
                err = process_audio_header_eacs(s);
                break;

            case SCHl_TAG :
            case SHEN_TAG :
                blockid = get_le32(pb);
                if (blockid == GSTR_TAG) {
                    url_fskip(pb, 4);
                } else if ((blockid & 0xFFFF)!=PT00_TAG) {
                    av_log (s, AV_LOG_ERROR, "unknown SCHl headerid\n");
                    return 0;
                }
                err = process_audio_header_elements(s);
                break;

            case SEAD_TAG:
                err = process_audio_header_sead(s);
                break;

            case MVIh_TAG :
                ea->video_codec = CODEC_ID_CMV;
                ea->time_base = (AVRational){0,0};
                break;

            case kVGT_TAG:
                ea->video_codec = CODEC_ID_TGV;
                ea->time_base = (AVRational){0,0};
                break;

            case mTCD_TAG :
                err = process_video_header_mdec(s);
                break;

            case MPCh_TAG:
                ea->video_codec = CODEC_ID_MPEG2VIDEO;
                break;

            case pQGT_TAG:
            case TGQs_TAG:
                ea->video_codec = CODEC_ID_TGQ;
                break;

            case pIQT_TAG:
                ea->video_codec = CODEC_ID_TQI;
                break;

            case MVhd_TAG :
                err = process_video_header_vp6(s);
                break;
        }

        if (err < 0) {
            av_log(s, AV_LOG_ERROR, "error parsing header: %i\n", err);
            return err;
        }

        url_fseek(pb, startpos + size, SEEK_SET);
    }

    url_fseek(pb, 0, SEEK_SET);

    return 1;
}


static int ea_probe(AVProbeData *p)
{
    switch (AV_RL32(&p->buf[0])) {
    case ISNh_TAG:
    case SCHl_TAG:
    case SEAD_TAG:
    case SHEN_TAG:
    case kVGT_TAG:
    case MADk_TAG:
    case MPCh_TAG:
    case MVhd_TAG:
    case MVIh_TAG:
        return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static int ea_read_header(AVFormatContext *s,
                          AVFormatParameters *ap)
{
    EaDemuxContext *ea = s->priv_data;
    AVStream *st;

    if (!process_ea_header(s))
        return AVERROR(EIO);

    if (ea->video_codec) {
        /* initialize the video decoder stream */
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR(ENOMEM);
        ea->video_stream_index = st->index;
        st->codec->codec_type = CODEC_TYPE_VIDEO;
        st->codec->codec_id = ea->video_codec;
        st->codec->codec_tag = 0;  /* no fourcc */
        st->codec->time_base = ea->time_base;
        st->codec->width = ea->width;
        st->codec->height = ea->height;
    }

    if (ea->audio_codec) {
        /* initialize the audio decoder stream */
        st = av_new_stream(s, 0);
        if (!st)
            return AVERROR(ENOMEM);
        av_set_pts_info(st, 33, 1, ea->sample_rate);
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        st->codec->codec_id = ea->audio_codec;
        st->codec->codec_tag = 0;  /* no tag */
        st->codec->channels = ea->num_channels;
        st->codec->sample_rate = ea->sample_rate;
        st->codec->bits_per_coded_sample = ea->bytes * 8;
        st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
            st->codec->bits_per_coded_sample / 4;
        st->codec->block_align = st->codec->channels*st->codec->bits_per_coded_sample;
        ea->audio_stream_index = st->index;
        ea->audio_frame_counter = 0;
    }

    return 1;
}

static int ea_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    EaDemuxContext *ea = s->priv_data;
    ByteIOContext *pb = s->pb;
    int ret = 0;
    int packet_read = 0;
    unsigned int chunk_type, chunk_size;
    int key = 0;
    int num_samples;

    while (!packet_read) {
        chunk_type = get_le32(pb);
        chunk_size = (ea->big_endian ? get_be32(pb) : get_le32(pb)) - 8;

        switch (chunk_type) {
        /* audio data */
        case ISNh_TAG:
            /* header chunk also contains data; skip over the header portion*/
            url_fskip(pb, 32);
            chunk_size -= 32;
        case ISNd_TAG:
        case SCDl_TAG:
        case SNDC_TAG:
        case SDEN_TAG:
            if (!ea->audio_codec) {
                url_fskip(pb, chunk_size);
                break;
            } else if (ea->audio_codec == CODEC_ID_PCM_S16LE_PLANAR ||
                       ea->audio_codec == CODEC_ID_MP3) {
                num_samples = get_le32(pb);
                url_fskip(pb, 8);
                chunk_size -= 12;
            }
            ret = av_get_packet(pb, pkt, chunk_size);
            if (ret != chunk_size)
                ret = AVERROR(EIO);
            else {
                    pkt->stream_index = ea->audio_stream_index;
                    pkt->pts = 90000;
                    pkt->pts *= ea->audio_frame_counter;
                    pkt->pts /= ea->sample_rate;

                    switch (ea->audio_codec) {
                    case CODEC_ID_ADPCM_EA:
                    /* 2 samples/byte, 1 or 2 samples per frame depending
                     * on stereo; chunk also has 12-byte header */
                    ea->audio_frame_counter += ((chunk_size - 12) * 2) /
                        ea->num_channels;
                        break;
                    case CODEC_ID_PCM_S16LE_PLANAR:
                    case CODEC_ID_MP3:
                        ea->audio_frame_counter += num_samples;
                        break;
                    default:
                        ea->audio_frame_counter += chunk_size /
                            (ea->bytes * ea->num_channels);
                    }
            }

            packet_read = 1;
            break;

        /* ending tag */
        case 0:
        case ISNe_TAG:
        case SCEl_TAG:
        case SEND_TAG:
        case SEEN_TAG:
            ret = AVERROR(EIO);
            packet_read = 1;
            break;

        case MVIh_TAG:
        case kVGT_TAG:
        case pQGT_TAG:
        case TGQs_TAG:
            key = PKT_FLAG_KEY;
        case MVIf_TAG:
        case fVGT_TAG:
            url_fseek(pb, -8, SEEK_CUR);     // include chunk preamble
            chunk_size += 8;
            goto get_video_packet;

        case mTCD_TAG:
            url_fseek(pb, 8, SEEK_CUR);  // skip ea dct header
            chunk_size -= 8;
            goto get_video_packet;

        case MV0K_TAG:
        case MPCh_TAG:
        case pIQT_TAG:
            key = PKT_FLAG_KEY;
        case MV0F_TAG:
get_video_packet:
            ret = av_get_packet(pb, pkt, chunk_size);
            if (ret != chunk_size)
                ret = AVERROR_IO;
            else {
                pkt->stream_index = ea->video_stream_index;
                pkt->flags |= key;
            }
            packet_read = 1;
            break;

        default:
            url_fseek(pb, chunk_size, SEEK_CUR);
            break;
        }
    }

    return ret;
}

AVInputFormat ea_demuxer = {
    "ea",
    NULL_IF_CONFIG_SMALL("Electronic Arts Multimedia Format"),
    sizeof(EaDemuxContext),
    ea_probe,
    ea_read_header,
    ea_read_packet,
};
