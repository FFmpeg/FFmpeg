/* Electronic Arts Multimedia File Demuxer
 * Copyright (c) 2004  The FFmpeg project
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

#include <inttypes.h>

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "internal.h"

#define SCHl_TAG MKTAG('S', 'C', 'H', 'l')
#define SEAD_TAG MKTAG('S', 'E', 'A', 'D')  /* Sxxx header */
#define SNDC_TAG MKTAG('S', 'N', 'D', 'C')  /* Sxxx data */
#define SEND_TAG MKTAG('S', 'E', 'N', 'D')  /* Sxxx end */
#define SHEN_TAG MKTAG('S', 'H', 'E', 'N')  /* SxEN header */
#define SDEN_TAG MKTAG('S', 'D', 'E', 'N')  /* SxEN data */
#define SEEN_TAG MKTAG('S', 'E', 'E', 'N')  /* SxEN end */
#define ISNh_TAG MKTAG('1', 'S', 'N', 'h')  /* 1SNx header */
#define EACS_TAG MKTAG('E', 'A', 'C', 'S')
#define ISNd_TAG MKTAG('1', 'S', 'N', 'd')  /* 1SNx data */
#define ISNe_TAG MKTAG('1', 'S', 'N', 'e')  /* 1SNx end */
#define PT00_TAG MKTAG('P', 'T', 0x0, 0x0)
#define GSTR_TAG MKTAG('G', 'S', 'T', 'R')
#define SCDl_TAG MKTAG('S', 'C', 'D', 'l')
#define SCEl_TAG MKTAG('S', 'C', 'E', 'l')
#define kVGT_TAG MKTAG('k', 'V', 'G', 'T')  /* TGV I-frame */
#define fVGT_TAG MKTAG('f', 'V', 'G', 'T')  /* TGV P-frame */
#define mTCD_TAG MKTAG('m', 'T', 'C', 'D')  /* MDEC */
#define MADk_TAG MKTAG('M', 'A', 'D', 'k')  /* MAD I-frame */
#define MADm_TAG MKTAG('M', 'A', 'D', 'm')  /* MAD P-frame */
#define MADe_TAG MKTAG('M', 'A', 'D', 'e')  /* MAD lqp-frame */
#define MPCh_TAG MKTAG('M', 'P', 'C', 'h')  /* MPEG-2 */
#define TGQs_TAG MKTAG('T', 'G', 'Q', 's')  /* TGQ I-frame (appears in .TGQ files) */
#define pQGT_TAG MKTAG('p', 'Q', 'G', 'T')  /* TGQ I-frame (appears in .UV files) */
#define pIQT_TAG MKTAG('p', 'I', 'Q', 'T')  /* TQI/UV2 I-frame (.UV2/.WVE) */
#define MVhd_TAG MKTAG('M', 'V', 'h', 'd')
#define MV0K_TAG MKTAG('M', 'V', '0', 'K')
#define MV0F_TAG MKTAG('M', 'V', '0', 'F')
#define AVhd_TAG MKTAG('A', 'V', 'h', 'd')
#define AV0K_TAG MKTAG('A', 'V', '0', 'K')
#define AV0F_TAG MKTAG('A', 'V', '0', 'F')
#define MVIh_TAG MKTAG('M', 'V', 'I', 'h')  /* CMV header */
#define MVIf_TAG MKTAG('M', 'V', 'I', 'f')  /* CMV I-frame */
#define AVP6_TAG MKTAG('A', 'V', 'P', '6')

typedef struct VideoProperties {
    enum AVCodecID codec;
    AVRational time_base;
    int width, height;
    int nb_frames;
    int stream_index;
} VideoProperties;

typedef struct EaDemuxContext {
    int big_endian;

    VideoProperties video, alpha;

    enum AVCodecID audio_codec;
    int audio_stream_index;

    int bytes;
    int sample_rate;
    int num_channels;
    int num_samples;

    int platform;
} EaDemuxContext;

static uint32_t read_arbitrary(AVIOContext *pb)
{
    uint8_t size, byte;
    int i;
    uint32_t word;

    size = avio_r8(pb);

    word = 0;
    for (i = 0; i < size; i++) {
        byte   = avio_r8(pb);
        word <<= 8;
        word  |= byte;
    }

    return word;
}

static int process_audio_header_elements(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    AVIOContext    *pb = s->pb;
    int in_header = 1;
    int compression_type = -1, revision = -1, revision2 = -1;

    ea->bytes        = 2;
    ea->sample_rate  = -1;
    ea->num_channels = 1;

    while (!avio_feof(pb) && in_header) {
        int in_subheader;
        uint8_t byte;
        byte = avio_r8(pb);

        switch (byte) {
        case 0xFD:
            av_log(s, AV_LOG_DEBUG, "entered audio subheader\n");
            in_subheader = 1;
            while (!avio_feof(pb) && in_subheader) {
                uint8_t subbyte;
                subbyte = avio_r8(pb);

                switch (subbyte) {
                case 0x80:
                    revision = read_arbitrary(pb);
                    av_log(s, AV_LOG_DEBUG,
                           "revision (element 0x80) set to 0x%08x\n", revision);
                    break;
                case 0x82:
                    ea->num_channels = read_arbitrary(pb);
                    av_log(s, AV_LOG_DEBUG,
                           "num_channels (element 0x82) set to 0x%08x\n",
                           ea->num_channels);
                    break;
                case 0x83:
                    compression_type = read_arbitrary(pb);
                    av_log(s, AV_LOG_DEBUG,
                           "compression_type (element 0x83) set to 0x%08x\n",
                           compression_type);
                    break;
                case 0x84:
                    ea->sample_rate = read_arbitrary(pb);
                    av_log(s, AV_LOG_DEBUG,
                           "sample_rate (element 0x84) set to %i\n",
                           ea->sample_rate);
                    break;
                case 0x85:
                    ea->num_samples = read_arbitrary(pb);
                    av_log(s, AV_LOG_DEBUG,
                           "num_samples (element 0x85) set to 0x%08x\n",
                           ea->num_samples);
                    break;
                case 0x8A:
                    av_log(s, AV_LOG_DEBUG,
                           "element 0x%02x set to 0x%08"PRIx32"\n",
                           subbyte, read_arbitrary(pb));
                    av_log(s, AV_LOG_DEBUG, "exited audio subheader\n");
                    in_subheader = 0;
                    break;
                case 0xA0:
                    revision2 = read_arbitrary(pb);
                    av_log(s, AV_LOG_DEBUG,
                           "revision2 (element 0xA0) set to 0x%08x\n",
                           revision2);
                    break;
                case 0xFF:
                    av_log(s, AV_LOG_DEBUG,
                           "end of header block reached (within audio subheader)\n");
                    in_subheader = 0;
                    in_header    = 0;
                    break;
                default:
                    av_log(s, AV_LOG_DEBUG,
                           "element 0x%02x set to 0x%08"PRIx32"\n",
                           subbyte, read_arbitrary(pb));
                    break;
                }
            }
            break;
        case 0xFF:
            av_log(s, AV_LOG_DEBUG, "end of header block reached\n");
            in_header = 0;
            break;
        default:
            av_log(s, AV_LOG_DEBUG,
                   "header element 0x%02x set to 0x%08"PRIx32"\n",
                   byte, read_arbitrary(pb));
            break;
        }
    }

    switch (compression_type) {
    case  0:
        ea->audio_codec = AV_CODEC_ID_PCM_S16LE;
        break;
    case  7:
        ea->audio_codec = AV_CODEC_ID_ADPCM_EA;
        break;
    case -1:
        switch (revision) {
        case  1:
            ea->audio_codec = AV_CODEC_ID_ADPCM_EA_R1;
            break;
        case  2:
            ea->audio_codec = AV_CODEC_ID_ADPCM_EA_R2;
            break;
        case  3:
            ea->audio_codec = AV_CODEC_ID_ADPCM_EA_R3;
            break;
        case -1:
            break;
        default:
            avpriv_request_sample(s, "stream type; revision=%i", revision);
            return 0;
        }
        switch (revision2) {
        case  8:
            ea->audio_codec = AV_CODEC_ID_PCM_S16LE_PLANAR;
            break;
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
        case 15:
        case 16:
            ea->audio_codec = AV_CODEC_ID_MP3;
            break;
        case -1:
            break;
        default:
            ea->audio_codec = AV_CODEC_ID_NONE;
            avpriv_request_sample(s, "stream type; revision2=%i", revision2);
            return 0;
        }
        break;
    default:
        avpriv_request_sample(s,
                              "stream type; compression_type=%i",
                              compression_type);
        return 0;
    }

    if (ea->audio_codec == AV_CODEC_ID_NONE && ea->platform == 0x01)
        ea->audio_codec = AV_CODEC_ID_ADPCM_PSX;
    if (ea->sample_rate == -1)
        ea->sample_rate = revision == 3 ? 48000 : 22050;

    return 1;
}

static void process_audio_header_eacs(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    AVIOContext *pb    = s->pb;
    int compression_type;

    ea->sample_rate  = ea->big_endian ? avio_rb32(pb) : avio_rl32(pb);
    ea->bytes        = avio_r8(pb);   /* 1=8-bit, 2=16-bit */
    ea->num_channels = avio_r8(pb);
    compression_type = avio_r8(pb);
    avio_skip(pb, 13);

    switch (compression_type) {
    case 0:
        switch (ea->bytes) {
        case 1:
            ea->audio_codec = AV_CODEC_ID_PCM_S8;
            break;
        case 2:
            ea->audio_codec = AV_CODEC_ID_PCM_S16LE;
            break;
        }
        break;
    case 1:
        ea->audio_codec = AV_CODEC_ID_PCM_MULAW;
        ea->bytes       = 1;
        break;
    case 2:
        ea->audio_codec = AV_CODEC_ID_ADPCM_IMA_EA_EACS;
        break;
    default:
        avpriv_request_sample(s,
                              "stream type; audio compression_type=%i",
                              compression_type);
    }
}

static void process_audio_header_sead(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    AVIOContext *pb    = s->pb;

    ea->sample_rate  = avio_rl32(pb);
    ea->bytes        = avio_rl32(pb);  /* 1=8-bit, 2=16-bit */
    ea->num_channels = avio_rl32(pb);
    ea->audio_codec  = AV_CODEC_ID_ADPCM_IMA_EA_SEAD;
}

static void process_video_header_mdec(AVFormatContext *s, VideoProperties *video)
{
    AVIOContext *pb    = s->pb;
    avio_skip(pb, 4);
    video->width       = avio_rl16(pb);
    video->height      = avio_rl16(pb);
    video->time_base   = (AVRational) { 1, 15 };
    video->codec = AV_CODEC_ID_MDEC;
}

static int process_video_header_vp6(AVFormatContext *s, VideoProperties *video)
{
    AVIOContext *pb = s->pb;

    avio_skip(pb, 8);
    video->nb_frames = avio_rl32(pb);
    avio_skip(pb, 4);
    video->time_base.den = avio_rl32(pb);
    video->time_base.num = avio_rl32(pb);
    if (video->time_base.den <= 0 || video->time_base.num <= 0) {
        av_log(s, AV_LOG_ERROR, "Timebase is invalid\n");
        return AVERROR_INVALIDDATA;
    }
    video->codec   = AV_CODEC_ID_VP6;

    return 1;
}

static void process_video_header_cmv(AVFormatContext *s, VideoProperties *video)
{
    int fps;

    avio_skip(s->pb, 10);
    fps = avio_rl16(s->pb);
    if (fps)
        video->time_base = (AVRational) { 1, fps };
    video->codec = AV_CODEC_ID_CMV;
}

/* Process EA file header.
 * Return 1 if the EA file is valid and successfully opened, 0 otherwise. */
static int process_ea_header(AVFormatContext *s)
{
    uint32_t blockid, size = 0;
    EaDemuxContext *ea = s->priv_data;
    AVIOContext *pb    = s->pb;
    int i;

    for (i = 0; i < 5 && (!ea->audio_codec || !ea->video.codec); i++) {
        uint64_t startpos     = avio_tell(pb);
        int err               = 0;

        blockid = avio_rl32(pb);
        size    = avio_rl32(pb);
        if (i == 0)
            ea->big_endian = size > av_bswap32(size);
        if (ea->big_endian)
            size = av_bswap32(size);

        if (size < 8) {
            av_log(s, AV_LOG_ERROR, "chunk size too small\n");
            return AVERROR_INVALIDDATA;
        }

        switch (blockid) {
        case ISNh_TAG:
            if (avio_rl32(pb) != EACS_TAG) {
                avpriv_request_sample(s, "unknown 1SNh headerid");
                return 0;
            }
            process_audio_header_eacs(s);
            break;

        case SCHl_TAG:
        case SHEN_TAG:
            blockid = avio_rl32(pb);
            if (blockid == GSTR_TAG) {
                avio_skip(pb, 4);
            } else if ((blockid & 0xFF) != (PT00_TAG & 0xFF)) {
                blockid = avio_rl32(pb);
            }
            ea->platform = (blockid >> 16) & 0xFF;
            err = process_audio_header_elements(s);
            break;

        case SEAD_TAG:
            process_audio_header_sead(s);
            break;

        case MVIh_TAG:
            process_video_header_cmv(s, &ea->video);
            break;

        case kVGT_TAG:
            ea->video.codec = AV_CODEC_ID_TGV;
            break;

        case mTCD_TAG:
            process_video_header_mdec(s, &ea->video);
            break;

        case MPCh_TAG:
            ea->video.codec = AV_CODEC_ID_MPEG2VIDEO;
            break;

        case pQGT_TAG:
        case TGQs_TAG:
            ea->video.codec = AV_CODEC_ID_TGQ;
            ea->video.time_base   = (AVRational) { 1, 15 };
            break;

        case pIQT_TAG:
            ea->video.codec = AV_CODEC_ID_TQI;
            ea->video.time_base   = (AVRational) { 1, 15 };
            break;

        case MADk_TAG:
            ea->video.codec = AV_CODEC_ID_MAD;
            avio_skip(pb, 6);
            ea->video.time_base = (AVRational) { avio_rl16(pb), 1000 };
            break;

        case MVhd_TAG:
            err = process_video_header_vp6(s, &ea->video);
            break;

        case AVhd_TAG:
            err = process_video_header_vp6(s, &ea->alpha);
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

static int ea_probe(const AVProbeData *p)
{
    unsigned big_endian, size;

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
    case AVP6_TAG:
        break;
    default:
        return 0;
    }
    size = AV_RL32(&p->buf[4]);
    big_endian = size > 0x000FFFFF;
    if (big_endian)
        size = av_bswap32(size);
    if (size > 0xfffff || size < 8)
        return 0;

    return AVPROBE_SCORE_MAX;
}

static int init_video_stream(AVFormatContext *s, VideoProperties *video)
{
    AVStream *st;

    if (!video->codec)
        return 0;

    /* initialize the video decoder stream */
    st = avformat_new_stream(s, NULL);
    if (!st)
        return AVERROR(ENOMEM);
    video->stream_index = st->index;
    st->codecpar->codec_type  = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id    = video->codec;
    // parsing is necessary to make FFmpeg generate correct timestamps
    if (st->codecpar->codec_id == AV_CODEC_ID_MPEG2VIDEO)
        ffstream(st)->need_parsing = AVSTREAM_PARSE_HEADERS;
    st->codecpar->codec_tag   = 0; /* no fourcc */
    st->codecpar->width       = video->width;
    st->codecpar->height      = video->height;
    st->duration           = st->nb_frames = video->nb_frames;
    if (video->time_base.num)
        avpriv_set_pts_info(st, 64, video->time_base.num, video->time_base.den);
    st->r_frame_rate       =
    st->avg_frame_rate     = av_inv_q(video->time_base);
    return 0;
}

static int ea_read_header(AVFormatContext *s)
{
    EaDemuxContext *ea = s->priv_data;
    AVStream *st;

    if (process_ea_header(s)<=0)
        return AVERROR(EIO);

    if (init_video_stream(s, &ea->video) || init_video_stream(s, &ea->alpha))
        return AVERROR(ENOMEM);

    if (ea->audio_codec) {
        if (ea->num_channels <= 0 || ea->num_channels > 2) {
            av_log(s, AV_LOG_WARNING,
                   "Unsupported number of channels: %d\n", ea->num_channels);
            goto no_audio;
        }
        if (ea->sample_rate <= 0) {
            av_log(s, AV_LOG_ERROR,
                   "Unsupported sample rate: %d\n", ea->sample_rate);
            goto no_audio;
        }
        if (ea->bytes <= 0 || ea->bytes > 2) {
            av_log(s, AV_LOG_ERROR,
                   "Invalid number of bytes per sample: %d\n", ea->bytes);
            goto no_audio;
        }

        /* initialize the audio decoder stream */
        st = avformat_new_stream(s, NULL);
        if (!st)
            return AVERROR(ENOMEM);
        avpriv_set_pts_info(st, 33, 1, ea->sample_rate);
        st->codecpar->codec_type            = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id              = ea->audio_codec;
        st->codecpar->codec_tag             = 0;   /* no tag */
        st->codecpar->ch_layout.nb_channels = ea->num_channels;
        st->codecpar->sample_rate           = ea->sample_rate;
        st->codecpar->bits_per_coded_sample = ea->bytes * 8;
        st->codecpar->bit_rate              = (int64_t)ea->num_channels *
                                              st->codecpar->sample_rate *
                                              st->codecpar->bits_per_coded_sample / 4;
        st->codecpar->block_align           = ea->num_channels *
                                              st->codecpar->bits_per_coded_sample;
        ea->audio_stream_index           = st->index;
        st->start_time                   = 0;
        return 0;
    }
no_audio:
    ea->audio_codec = AV_CODEC_ID_NONE;

    if (!ea->video.codec)
        return AVERROR_INVALIDDATA;
    return 0;
}

static int ea_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    EaDemuxContext *ea = s->priv_data;
    AVIOContext *pb    = s->pb;
    int partial_packet = 0;
    int hit_end = 0;
    unsigned int chunk_type, chunk_size;
    int ret = 0, packet_read = 0, key = 0;
    int av_uninit(num_samples);

    while ((!packet_read && !hit_end) || partial_packet) {
        if (avio_feof(pb))
            return AVERROR_EOF;
        chunk_type = avio_rl32(pb);
        chunk_size = ea->big_endian ? avio_rb32(pb) : avio_rl32(pb);
        if (chunk_size < 8)
            return AVERROR_INVALIDDATA;
        chunk_size -= 8;

        switch (chunk_type) {
        /* audio data */
        case ISNh_TAG:
            /* header chunk also contains data; skip over the header portion */
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
                if (chunk_size < 12)
                    return AVERROR_INVALIDDATA;
                num_samples = avio_rl32(pb);
                avio_skip(pb, 8);
                chunk_size -= 12;
            } else if (ea->audio_codec == AV_CODEC_ID_ADPCM_PSX) {
                if (chunk_size < 8)
                    return AVERROR_INVALIDDATA;
                avio_skip(pb, 8);
                chunk_size -= 8;
            }

            if (partial_packet) {
                avpriv_request_sample(s, "video header followed by audio packet");
                av_packet_unref(pkt);
                partial_packet = 0;
            }

            if (!chunk_size)
                continue;

            ret = av_get_packet(pb, pkt, chunk_size);
            if (ret < 0)
                return ret;
            pkt->stream_index = ea->audio_stream_index;

            switch (ea->audio_codec) {
            case AV_CODEC_ID_ADPCM_EA:
            case AV_CODEC_ID_ADPCM_EA_R1:
            case AV_CODEC_ID_ADPCM_EA_R2:
            case AV_CODEC_ID_ADPCM_IMA_EA_EACS:
            case AV_CODEC_ID_ADPCM_EA_R3:
                if (pkt->size < 4) {
                    av_log(s, AV_LOG_ERROR, "Packet is too short\n");
                    return AVERROR_INVALIDDATA;
                }
                if (ea->audio_codec == AV_CODEC_ID_ADPCM_EA_R3)
                    pkt->duration = AV_RB32(pkt->data);
                else
                    pkt->duration = AV_RL32(pkt->data);
                break;
            case AV_CODEC_ID_ADPCM_IMA_EA_SEAD:
                pkt->duration = ret * 2 / ea->num_channels;
                break;
            case AV_CODEC_ID_PCM_S16LE_PLANAR:
            case AV_CODEC_ID_MP3:
                pkt->duration = num_samples;
                break;
            case AV_CODEC_ID_ADPCM_PSX:
                pkt->duration = chunk_size / (16 * ea->num_channels) * 28;
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
            while (!avio_feof(pb)) {
                int tag = avio_rl32(pb);

                if (tag == ISNh_TAG ||
                    tag == SCHl_TAG ||
                    tag == SEAD_TAG ||
                    tag == SHEN_TAG) {
                    avio_skip(pb, -4);
                    break;
                }
            }
            if (avio_feof(pb))
                ret = AVERROR_EOF;
            hit_end = 1;
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
            if (chunk_size > INT_MAX - 8)
                return AVERROR_INVALIDDATA;
            avio_seek(pb, -8, SEEK_CUR);    // include chunk preamble
            chunk_size += 8;
            goto get_video_packet;

        case mTCD_TAG:
            if (chunk_size < 8)
                return AVERROR_INVALIDDATA;

            avio_skip(pb, 8);               // skip ea DCT header
            chunk_size -= 8;
            goto get_video_packet;

        case MV0K_TAG:
        case AV0K_TAG:
        case MPCh_TAG:
        case pIQT_TAG:
            key = AV_PKT_FLAG_KEY;
        case MV0F_TAG:
        case AV0F_TAG:
get_video_packet:
            if (!chunk_size)
                continue;

            if (partial_packet) {
                ret = av_append_packet(pb, pkt, chunk_size);
            } else
                ret = av_get_packet(pb, pkt, chunk_size);
            if (ret < 0) {
                packet_read = 1;
                partial_packet = 0;
                break;
            }
            partial_packet = chunk_type == MVIh_TAG;
            if (chunk_type == AV0K_TAG || chunk_type == AV0F_TAG)
                pkt->stream_index = ea->alpha.stream_index;
            else
                pkt->stream_index = ea->video.stream_index;
            pkt->flags       |= key;
            packet_read       = 1;
            break;

        default:
            avio_skip(pb, chunk_size);
            break;
        }
    }

    if (ret >= 0 && hit_end && !packet_read)
        return AVERROR(EAGAIN);

    return ret;
}

const AVInputFormat ff_ea_demuxer = {
    .name           = "ea",
    .long_name      = NULL_IF_CONFIG_SMALL("Electronic Arts Multimedia"),
    .priv_data_size = sizeof(EaDemuxContext),
    .read_probe     = ea_probe,
    .read_header    = ea_read_header,
    .read_packet    = ea_read_packet,
};
