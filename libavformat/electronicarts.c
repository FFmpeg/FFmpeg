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
 * @file
 * Electronic Arts Multimedia file demuxer (WVE/UV2/etc.)
 * by Robin Kay (komadori at gekkou.co.uk)
 */

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

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
#define MADm_TAG MKTAG('M', 'A', 'D', 'm')    /* MAD p-frame */
#define MADe_TAG MKTAG('M', 'A', 'D', 'e')    /* MAD lqp-frame */
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

    enum AVCodecID video_codec;
    AVRational time_base;
    int width, height;
    int nb_frames;
    int video_stream_index;

    enum AVCodecID audio_codec;
    int audio_stream_index;

    int bytes;
    int sample_rate;
    int num_channels;
    int num_samples;
} EaDemuxContext;

static uint32_t read_arbitary(AVIOContext *pb) {
    uint8_t size, byte;
    int i;
    uint32_t word;

    size = avio_r8(pb);

    word = 0;
    for (i = 0; i < size; i++) {
        byte = avio_r8(pb);
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
    AVIOContext *pb = s->pb;
    int compression_type = -1, revision = -1, revision2 = -1;

    ea->bytes = 2;
    ea->sample_rate = -1;
    ea->num_channels = 1;

    while (!url_feof(pb) && inHeader) {
        int inSubheader;
        uint8_t byte;
        byte = avio_r8(pb);

        switch (byte) {
        case 0xFD:
            av_log (s, AV_LOG_DEBUG, "entered audio subheader\n");
            inSubheader = 1;
            while (!url_feof(pb) && inSubheader) {
                uint8_t subbyte;
                subbyte = avio_r8(pb);

                switch (subbyte) {
                case 0x80:
                    revision = read_arbitary(pb);
                    av_log (s, AV_LOG_DEBUG, "revision (element 0x80) set to 0x%08x\n", revision);
                    break;
                case 0x82:
                    ea->num_channels = read_arbitary(pb);
                    av_log (s, AV_LOG_DEBUG, "num_channels (element 0x82) set to 0x%08x\n", ea->num_channels);
                    break;
                case 0x83:
                    compression_type = read_arbitary(pb);
                    av_log (s, AV_LOG_DEBUG, "compression_type (element 0x83) set to 0x%08x\n", compression_type);
                    break;
                case 0x84:
                    ea->sample_rate = read_arbitary(pb);
                    av_log (s, AV_LOG_DEBUG, "sample_rate (element 0x84) set to %i\n", ea->sample_rate);
                    break;
                case 0x85:
                    ea->num_samples = read_arbitary(pb);
                    av_log (s, AV_LOG_DEBUG, "num_samples (element 0x85) set to 0x%08x\n", ea->num_samples);
                    break;
                case 0x8A:
                    av_log (s, AV_LOG_DEBUG, "element 0x%02x set to 0x%08x\n", subbyte, read_arbitary(pb));
                    av_log (s, AV_LOG_DEBUG, "exited audio subheader\n");
                    inSubheader = 0;
                    break;
                case 0xA0:
                    revision2 = read_arbitary(pb);
                    av_log (s, AV_LOG_DEBUG, "revision2 (element 0xA0) set to 0x%08x\n", revision2);
                    break;
                case 0xFF:
                    av_log (s, AV_LOG_DEBUG, "end of header block reached (within audio subheader)\n");
                    inSubheader = 0;
                    inHeader = 0;
                    break;
                default:
                    av_log (s, AV_LOG_DEBUG, "element 0x%02x set to 0x%08x\n", subbyte, read_arbitary(pb));
                    break;
                }
            }
            break;
        case 0xFF:
            av_log (s, AV_LOG_DEBUG, "end of header block reached\n");
            inHeader = 0;
            break;
        default:
            av_log (s, AV_LOG_DEBUG, "header element 0x%02x set to 0x%08x\n", byte, read_arbitary(pb));
            break;
        }
    }

    switch (compression_type) {
    case  0: ea->audio_codec = AV_CODEC_ID_PCM_S16LE; break;
    case  7: ea->audio_codec = AV_CODEC_ID_ADPCM_EA; break;
    case -1:
        switch (revision) {
        case  1: ea->audio_codec = AV_CODEC_ID_ADPCM_EA_R1; break;
        case  2: ea->audio_codec = AV_CODEC_ID_ADPCM_EA_R2; break;
        case  3: ea->audio_codec = AV_CODEC_ID_ADPCM_EA_R3; break;
        case -1: break;
        default:
            avpriv_request_sample(s, "stream type; revision=%i", revision);
            return 0;
        }
        switch (revision2) {
        case  8: ea->audio_codec = AV_CODEC_ID_PCM_S16LE_PLANAR; break;
        case 10:
            switch (revision) {
            case -1:
            case  2: ea->audio_codec = AV_CODEC_ID_ADPCM_EA_R1; break;
            case  3: ea->audio_codec = AV_CODEC_ID_ADPCM_EA_R2; break;
            default:
                avpriv_request_sample(s, "stream type; revision=%i, revision2=%i", revision, revision2);
                return 0;
            }
            break;
        case 16: ea->audio_codec = AV_CODEC_ID_MP3; break;
        case -1: break;
        default:
            ea->audio_codec = AV_CODEC_ID_NONE;
            avpriv_request_sample(s, "stream type; revision2=%i", revision2);
            return 0;
        }
        break;
    default:
        avpriv_request_sample(s, "stream type; compression_type=%i", compression_type);
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
    AVIOContext *pb = s->pb;
    int compression_type;

    ea->sample_rate  = ea->big_endian ? avio_rb32(pb) : avio_rl32(pb);
    ea->bytes        = avio_r8(pb);   /* 1=8-bit, 2=16-bit */
    ea->num_channels = avio_r8(pb);
    compression_type = avio_r8(pb);
    avio_skip(pb, 13);

    switch (compression_type) {
    case 0:
        switch (ea->bytes) {
        case 1: ea->audio_codec = AV_CODEC_ID_PCM_S8;    break;
        case 2: ea->audio_codec = AV_CODEC_ID_PCM_S16LE; break;
        }
        break;
    case 1: ea->audio_codec = AV_CODEC_ID_PCM_MULAW; ea->bytes = 1; break;
    case 2: ea->audio_codec = AV_CODEC_ID_ADPCM_IMA_EA_EACS; break;
    default:
        avpriv_request_sample(s, "stream type; audio compression_type=%i", compression_type);
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
    AVIOContext *pb = s->pb;

    ea->sample_rate  = avio_rl32(pb);
    ea->bytes        = avio_rl32(pb);  /* 1=8-bit, 2=16-bit */
    ea->num_channels = avio_rl32(pb);
    ea->audio_codec  = AV_CODEC_ID_ADPCM_IMA_EA_SEAD;

    return 1;
}

static int process_video_header_mdec(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    AVIOContext *pb = s->pb;
    avio_skip(pb, 4);
    ea->width  = avio_rl16(pb);
    ea->height = avio_rl16(pb);
    ea->time_base = (AVRational){1,15};
    ea->video_codec = AV_CODEC_ID_MDEC;
    return 1;
}

static int process_video_header_vp6(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    AVIOContext *pb = s->pb;

    avio_skip(pb, 8);
    ea->nb_frames = avio_rl32(pb);
    avio_skip(pb, 4);
    ea->time_base.den = avio_rl32(pb);
    ea->time_base.num = avio_rl32(pb);
    if (ea->time_base.den <= 0 || ea->time_base.num <= 0) {
        av_log(s, AV_LOG_ERROR, "Timebase is invalid\n");
        return AVERROR_INVALIDDATA;
    }
    ea->video_codec = AV_CODEC_ID_VP6;

    return 1;
}

static int process_video_header_cmv(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    int fps;

    avio_skip(s->pb, 10);
    fps = avio_rl16(s->pb);
    if (fps)
        ea->time_base = (AVRational){1, fps};
    ea->video_codec = AV_CODEC_ID_CMV;

    return 0;
}

/*
 * Process EA file header
 * Returns 1 if the EA file is valid and successfully opened, 0 otherwise
 */
static int process_ea_header(AVFormatContext *s) {
    uint32_t blockid, size = 0;
    EaDemuxContext *ea = s->priv_data;
    AVIOContext *pb = s->pb;
    int i;

    for (i=0; i<5 && (!ea->audio_codec || !ea->video_codec); i++) {
        unsigned int startpos = avio_tell(pb);
        int err = 0;

        blockid = avio_rl32(pb);
        size = avio_rl32(pb);
        if (i == 0)
            ea->big_endian = size > 0x000FFFFF;
        if (ea->big_endian)
            size = av_bswap32(size);

        switch (blockid) {
            case ISNh_TAG:
                if (avio_rl32(pb) != EACS_TAG) {
                    avpriv_request_sample(s, "unknown 1SNh headerid");
                    return 0;
                }
                err = process_audio_header_eacs(s);
                break;

            case SCHl_TAG :
            case SHEN_TAG :
                blockid = avio_rl32(pb);
                if (blockid == GSTR_TAG) {
                    avio_skip(pb, 4);
                } else if ((blockid & 0xFFFF)!=PT00_TAG) {
                    avpriv_request_sample(s, "unknown SCHl headerid");
                    return 0;
                }
                err = process_audio_header_elements(s);
                break;

            case SEAD_TAG:
                err = process_audio_header_sead(s);
                break;

            case MVIh_TAG :
                err = process_video_header_cmv(s);
                break;

            case kVGT_TAG:
                ea->video_codec = AV_CODEC_ID_TGV;
                break;

            case mTCD_TAG :
                err = process_video_header_mdec(s);
                break;

            case MPCh_TAG:
                ea->video_codec = AV_CODEC_ID_MPEG2VIDEO;
                break;

            case pQGT_TAG:
            case TGQs_TAG:
                ea->video_codec = AV_CODEC_ID_TGQ;
                break;

            case pIQT_TAG:
                ea->video_codec = AV_CODEC_ID_TQI;
                break;

            case MADk_TAG :
                ea->video_codec = AV_CODEC_ID_MAD;
                break;

            case MVhd_TAG :
                err = process_video_header_vp6(s);
                break;
        }

        if (err < 0) {
            av_log(s, AV_LOG_ERROR, "error parsing header: %i\n", err);
            return err;
        }

        avio_seek(pb, startpos + size, SEEK_SET);
    }

    avio_seek(pb, 0, SEEK_SET);

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
        break;
    default:
        return 0;
    }
    if (AV_RL32(&p->buf[4]) > 0xfffff && AV_RB32(&p->buf[4]) > 0xfffff)
        return 0;
    return AVPROBE_SCORE_MAX;
}

static int ea_read_header(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    AVStream *st;

    if (process_ea_header(s)<=0)
        return AVERROR(EIO);

    if (ea->video_codec) {
        /* initialize the video decoder stream */
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        ea->video_stream_index = st->index;
        st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codec->codec_id = ea->video_codec;
        // parsing is necessary to make FFmpeg generate correct timestamps
        if (st->codec->codec_id == AV_CODEC_ID_MPEG2VIDEO)
            st->need_parsing = AVSTREAM_PARSE_HEADERS;
        st->codec->codec_tag = 0;  /* no fourcc */
        st->codec->width = ea->width;
        st->codec->height = ea->height;
        st->duration = st->nb_frames = ea->nb_frames;
        if (ea->time_base.num)
            avpriv_set_pts_info(st, 64, ea->time_base.num, ea->time_base.den);
        st->r_frame_rate =
        st->avg_frame_rate = av_inv_q(ea->time_base);
    }

    if (ea->audio_codec) {
        if (ea->num_channels <= 0) {
            av_log(s, AV_LOG_WARNING, "Unsupported number of channels: %d\n", ea->num_channels);
            ea->audio_codec = 0;
            return 1;
        }
        if (ea->sample_rate <= 0) {
            av_log(s, AV_LOG_ERROR, "Unsupported sample rate: %d\n", ea->sample_rate);
            ea->audio_codec = 0;
            return 1;
        }
        if (ea->bytes <= 0) {
            av_log(s, AV_LOG_ERROR, "Invalid number of bytes per sample: %d\n", ea->bytes);
            ea->audio_codec = AV_CODEC_ID_NONE;
            return 1;
        }

        /* initialize the audio decoder stream */
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        avpriv_set_pts_info(st, 33, 1, ea->sample_rate);
        st->codec->codec_type = AVMEDIA_TYPE_AUDIO;
        st->codec->codec_id = ea->audio_codec;
        st->codec->codec_tag = 0;  /* no tag */
        st->codec->channels = ea->num_channels;
        st->codec->sample_rate = ea->sample_rate;
        st->codec->bits_per_coded_sample = ea->bytes * 8;
        st->codec->bit_rate = st->codec->channels * st->codec->sample_rate *
            st->codec->bits_per_coded_sample / 4;
        st->codec->block_align = st->codec->channels*st->codec->bits_per_coded_sample;
        ea->audio_stream_index = st->index;
        st->start_time = 0;
    }

    return 1;
}

static int ea_read_packet(AVFormatContext *s,
                          AVPacket *pkt)
{
    EaDemuxContext *ea = s->priv_data;
    AVIOContext *pb = s->pb;
    int ret = 0;
    int packet_read = 0;
    int partial_packet = 0;
    unsigned int chunk_type, chunk_size;
    int key = 0;
    int av_uninit(num_samples);

    while (!packet_read || partial_packet) {
        chunk_type = avio_rl32(pb);
        chunk_size = ea->big_endian ? avio_rb32(pb) : avio_rl32(pb);
        if (chunk_size <= 8)
            return AVERROR_INVALIDDATA;
        chunk_size -= 8;

        switch (chunk_type) {
        /* audio data */
        case ISNh_TAG:
            /* header chunk also contains data; skip over the header portion*/
            if (chunk_size < 32)
                return AVERROR_INVALIDDATA;
            avio_skip(pb, 32);
            chunk_size -= 32;
        case ISNd_TAG:
        case SCDl_TAG:
        case SNDC_TAG:
        case SDEN_TAG:
            if (!ea->audio_codec) {
                avio_skip(pb, chunk_size);
                break;
            } else if (ea->audio_codec == AV_CODEC_ID_PCM_S16LE_PLANAR ||
                       ea->audio_codec == AV_CODEC_ID_MP3) {
                num_samples = avio_rl32(pb);
                avio_skip(pb, 8);
                chunk_size -= 12;
            }
            if (partial_packet) {
                avpriv_request_sample(s, "video header followed by audio packet");
                av_free_packet(pkt);
                partial_packet = 0;
            }
            ret = av_get_packet(pb, pkt, chunk_size);
            if (ret < 0)
                return ret;
            pkt->stream_index = ea->audio_stream_index;

            switch (ea->audio_codec) {
            case AV_CODEC_ID_ADPCM_EA:
            case AV_CODEC_ID_ADPCM_EA_R1:
            case AV_CODEC_ID_ADPCM_EA_R2:
            case AV_CODEC_ID_ADPCM_IMA_EA_EACS:
                if (pkt->size >= 4)
                    pkt->duration = AV_RL32(pkt->data);
                break;
            case AV_CODEC_ID_ADPCM_EA_R3:
                if (pkt->size >= 4)
                    pkt->duration = AV_RB32(pkt->data);
                break;
            case AV_CODEC_ID_ADPCM_IMA_EA_SEAD:
                pkt->duration = ret * 2 / ea->num_channels;
                break;
            case AV_CODEC_ID_PCM_S16LE_PLANAR:
            case AV_CODEC_ID_MP3:
                pkt->duration = num_samples;
                break;
            default:
                pkt->duration = chunk_size / (ea->bytes * ea->num_channels);
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
        case MADk_TAG:
            key = AV_PKT_FLAG_KEY;
        case MVIf_TAG:
        case fVGT_TAG:
        case MADm_TAG:
        case MADe_TAG:
            avio_seek(pb, -8, SEEK_CUR);     // include chunk preamble
            chunk_size += 8;
            goto get_video_packet;

        case mTCD_TAG:
            avio_skip(pb, 8);  // skip ea dct header
            chunk_size -= 8;
            goto get_video_packet;

        case MV0K_TAG:
        case MPCh_TAG:
        case pIQT_TAG:
            key = AV_PKT_FLAG_KEY;
        case MV0F_TAG:
get_video_packet:
            if (partial_packet) {
                ret = av_append_packet(pb, pkt, chunk_size);
            } else
                ret = av_get_packet(pb, pkt, chunk_size);
            if (ret < 0) {
                packet_read = 1;
                break;
            }
            partial_packet = chunk_type == MVIh_TAG;
            pkt->stream_index = ea->video_stream_index;
            pkt->flags |= key;
            packet_read = 1;
            break;

        default:
            avio_skip(pb, chunk_size);
            break;
        }
    }

    if (ret < 0 && partial_packet)
        av_free_packet(pkt);
    return ret;
}

AVInputFormat ff_ea_demuxer = {
    .name           = "ea",
    .long_name      = NULL_IF_CONFIG_SMALL("Electronic Arts Multimedia"),
    .priv_data_size = sizeof(EaDemuxContext),
    .read_probe     = ea_probe,
    .read_header    = ea_read_header,
    .read_packet    = ea_read_packet,
};
