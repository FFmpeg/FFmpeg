/*
 * RAW muxer and demuxer
 * Copyright (c) 2001 Fabrice Bellard
 * Copyright (c) 2005 Alex Beregszaszi
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

#include "libavutil/crc.h"
#include "libavcodec/ac3_parser.h"
#include "libavcodec/bitstream.h"
#include "libavcodec/bytestream.h"
#include "avformat.h"
#include "raw.h"
#include "id3v2.h"

/* simple formats */

#if CONFIG_ROQ_MUXER
static int roq_write_header(struct AVFormatContext *s)
{
    static const uint8_t header[] = {
        0x84, 0x10, 0xFF, 0xFF, 0xFF, 0xFF, 0x1E, 0x00
    };

    put_buffer(s->pb, header, 8);
    put_flush_packet(s->pb);

    return 0;
}
#endif

#if CONFIG_NULL_MUXER
static int null_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    return 0;
}
#endif

#if CONFIG_MUXERS
static int raw_write_packet(struct AVFormatContext *s, AVPacket *pkt)
{
    put_buffer(s->pb, pkt->data, pkt->size);
    put_flush_packet(s->pb);
    return 0;
}
#endif

#if CONFIG_DEMUXERS
/* raw input */
static int raw_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVStream *st;
    int id;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

        id = s->iformat->value;
        if (id == CODEC_ID_RAWVIDEO) {
            st->codec->codec_type = CODEC_TYPE_VIDEO;
        } else {
            st->codec->codec_type = CODEC_TYPE_AUDIO;
        }
        st->codec->codec_id = id;

        switch(st->codec->codec_type) {
        case CODEC_TYPE_AUDIO:
            st->codec->sample_rate = ap->sample_rate;
            if(ap->channels) st->codec->channels = ap->channels;
            else             st->codec->channels = 1;
            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
            break;
        case CODEC_TYPE_VIDEO:
            if(ap->time_base.num)
                av_set_pts_info(st, 64, ap->time_base.num, ap->time_base.den);
            else
                av_set_pts_info(st, 64, 1, 25);
            st->codec->width = ap->width;
            st->codec->height = ap->height;
            st->codec->pix_fmt = ap->pix_fmt;
            if(st->codec->pix_fmt == PIX_FMT_NONE)
                st->codec->pix_fmt= PIX_FMT_YUV420P;
            break;
        default:
            return -1;
        }
    return 0;
}

#define RAW_PACKET_SIZE 1024

static int raw_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size, bps;
    //    AVStream *st = s->streams[0];

    size= RAW_PACKET_SIZE;

    ret= av_get_packet(s->pb, pkt, size);

    pkt->stream_index = 0;
    if (ret <= 0) {
        return AVERROR(EIO);
    }
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;

    bps= av_get_bits_per_sample(s->streams[0]->codec->codec_id);
    assert(bps); // if false there IS a bug elsewhere (NOT in this function)
    pkt->dts=
    pkt->pts= pkt->pos*8 / (bps * s->streams[0]->codec->channels);

    return ret;
}

int ff_raw_read_partial_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size;

    size = RAW_PACKET_SIZE;

    if (av_new_packet(pkt, size) < 0)
        return AVERROR(EIO);

    pkt->pos= url_ftell(s->pb);
    pkt->stream_index = 0;
    ret = get_partial_buffer(s->pb, pkt->data, size);
    if (ret <= 0) {
        av_free_packet(pkt);
        return AVERROR(EIO);
    }
    pkt->size = ret;
    return ret;
}
#endif

#if CONFIG_RAWVIDEO_DEMUXER
static int rawvideo_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int packet_size, ret, width, height;
    AVStream *st = s->streams[0];

    width = st->codec->width;
    height = st->codec->height;

    packet_size = avpicture_get_size(st->codec->pix_fmt, width, height);
    if (packet_size < 0)
        return -1;

    ret= av_get_packet(s->pb, pkt, packet_size);
    pkt->pts=
    pkt->dts= pkt->pos / packet_size;

    pkt->stream_index = 0;
    if (ret != packet_size) {
        return AVERROR(EIO);
    } else {
        return 0;
    }
}
#endif

#if CONFIG_INGENIENT_DEMUXER
// http://www.artificis.hu/files/texts/ingenient.txt
static int ingenient_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int ret, size, w, h, unk1, unk2;

    if (get_le32(s->pb) != MKTAG('M', 'J', 'P', 'G'))
        return AVERROR(EIO); // FIXME

    size = get_le32(s->pb);

    w = get_le16(s->pb);
    h = get_le16(s->pb);

    url_fskip(s->pb, 8); // zero + size (padded?)
    url_fskip(s->pb, 2);
    unk1 = get_le16(s->pb);
    unk2 = get_le16(s->pb);
    url_fskip(s->pb, 22); // ASCII timestamp

    av_log(s, AV_LOG_DEBUG, "Ingenient packet: size=%d, width=%d, height=%d, unk1=%d unk2=%d\n",
        size, w, h, unk1, unk2);

    if (av_new_packet(pkt, size) < 0)
        return AVERROR(EIO);

    pkt->pos = url_ftell(s->pb);
    pkt->stream_index = 0;
    ret = get_buffer(s->pb, pkt->data, size);
    if (ret <= 0) {
        av_free_packet(pkt);
        return AVERROR(EIO);
    }
    pkt->size = ret;
    return ret;
}
#endif

#if CONFIG_DEMUXERS
int pcm_read_seek(AVFormatContext *s,
                  int stream_index, int64_t timestamp, int flags)
{
    AVStream *st;
    int block_align, byte_rate, ret;
    int64_t pos;

    st = s->streams[0];

    block_align = st->codec->block_align ? st->codec->block_align :
        (av_get_bits_per_sample(st->codec->codec_id) * st->codec->channels) >> 3;
    byte_rate = st->codec->bit_rate ? st->codec->bit_rate >> 3 :
        block_align * st->codec->sample_rate;

    if (block_align <= 0 || byte_rate <= 0)
        return -1;

    /* compute the position by aligning it to block_align */
    pos = av_rescale_rnd(timestamp * byte_rate,
                         st->time_base.num,
                         st->time_base.den * (int64_t)block_align,
                         (flags & AVSEEK_FLAG_BACKWARD) ? AV_ROUND_DOWN : AV_ROUND_UP);
    pos *= block_align;

    /* recompute exact position */
    st->cur_dts = av_rescale(pos, st->time_base.den, byte_rate * (int64_t)st->time_base.num);
    if ((ret = url_fseek(s->pb, pos + s->data_offset, SEEK_SET)) < 0)
        return ret;
    return 0;
}

static int audio_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVStream *st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);
    st->codec->codec_type = CODEC_TYPE_AUDIO;
    st->codec->codec_id = s->iformat->value;
    st->need_parsing = AVSTREAM_PARSE_FULL;
    /* the parameters will be extracted from the compressed bitstream */

    return 0;
}

/* MPEG-1/H.263 input */
static int video_read_header(AVFormatContext *s,
                             AVFormatParameters *ap)
{
    AVStream *st;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR(ENOMEM);

    st->codec->codec_type = CODEC_TYPE_VIDEO;
    st->codec->codec_id = s->iformat->value;
    st->need_parsing = AVSTREAM_PARSE_FULL;

    /* for MJPEG, specify frame rate */
    /* for MPEG-4 specify it, too (most MPEG-4 streams do not have the fixed_vop_rate set ...)*/
    if (ap->time_base.num) {
        st->codec->time_base= ap->time_base;
    } else if ( st->codec->codec_id == CODEC_ID_MJPEG ||
                st->codec->codec_id == CODEC_ID_MPEG4 ||
                st->codec->codec_id == CODEC_ID_DIRAC ||
                st->codec->codec_id == CODEC_ID_H264) {
        st->codec->time_base= (AVRational){1,25};
    }
    av_set_pts_info(st, 64, 1, 1200000);

    return 0;
}
#endif

#if CONFIG_MPEGVIDEO_DEMUXER
#define SEQ_START_CODE          0x000001b3
#define GOP_START_CODE          0x000001b8
#define PICTURE_START_CODE      0x00000100
#define SLICE_START_CODE        0x00000101
#define PACK_START_CODE         0x000001ba
#define VIDEO_ID                0x000001e0
#define AUDIO_ID                0x000001c0

static int mpegvideo_probe(AVProbeData *p)
{
    uint32_t code= -1;
    int pic=0, seq=0, slice=0, pspack=0, pes=0;
    int i;

    for(i=0; i<p->buf_size; i++){
        code = (code<<8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            switch(code){
            case     SEQ_START_CODE:   seq++; break;
            case PICTURE_START_CODE:   pic++; break;
            case   SLICE_START_CODE: slice++; break;
            case    PACK_START_CODE: pspack++; break;
            }
            if     ((code & 0x1f0) == VIDEO_ID)   pes++;
            else if((code & 0x1e0) == AUDIO_ID)   pes++;
        }
    }
    if(seq && seq*9<=pic*10 && pic*9<=slice*10 && !pspack && !pes)
        return AVPROBE_SCORE_MAX/2+1; // +1 for .mpg
    return 0;
}

#define CAVS_SEQ_START_CODE       0x000001b0
#define CAVS_PIC_I_START_CODE     0x000001b3
#define CAVS_UNDEF_START_CODE     0x000001b4
#define CAVS_PIC_PB_START_CODE    0x000001b6
#define CAVS_VIDEO_EDIT_CODE      0x000001b7
#define CAVS_PROFILE_JIZHUN       0x20

static int cavsvideo_probe(AVProbeData *p)
{
    uint32_t code= -1;
    int pic=0, seq=0, slice_pos = 0;
    int i;

    for(i=0; i<p->buf_size; i++){
        code = (code<<8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            if(code < CAVS_SEQ_START_CODE) {
                /* slices have to be consecutive */
                if(code < slice_pos)
                    return 0;
                slice_pos = code;
            } else {
                slice_pos = 0;
            }
            if (code == CAVS_SEQ_START_CODE) {
                seq++;
                /* check for the only currently supported profile */
                if(p->buf[i+1] != CAVS_PROFILE_JIZHUN)
                    return 0;
            } else if ((code == CAVS_PIC_I_START_CODE) ||
                       (code == CAVS_PIC_PB_START_CODE)) {
                pic++;
            } else if ((code == CAVS_UNDEF_START_CODE) ||
                       (code >  CAVS_VIDEO_EDIT_CODE)) {
                return 0;
            }
        }
    }
    if(seq && seq*9<=pic*10)
        return AVPROBE_SCORE_MAX/2;
    return 0;
}
#endif

#if CONFIG_M4V_DEMUXER
#define VISUAL_OBJECT_START_CODE       0x000001b5
#define VOP_START_CODE                 0x000001b6

static int mpeg4video_probe(AVProbeData *probe_packet)
{
    uint32_t temp_buffer= -1;
    int VO=0, VOL=0, VOP = 0, VISO = 0, res=0;
    int i;

    for(i=0; i<probe_packet->buf_size; i++){
        temp_buffer = (temp_buffer<<8) + probe_packet->buf[i];
        if ((temp_buffer & 0xffffff00) != 0x100)
            continue;

        if (temp_buffer == VOP_START_CODE)                         VOP++;
        else if (temp_buffer == VISUAL_OBJECT_START_CODE)          VISO++;
        else if (temp_buffer < 0x120)                              VO++;
        else if (temp_buffer < 0x130)                              VOL++;
        else if (   !(0x1AF < temp_buffer && temp_buffer < 0x1B7)
                 && !(0x1B9 < temp_buffer && temp_buffer < 0x1C4)) res++;
    }

    if ( VOP >= VISO && VOP >= VOL && VO >= VOL && VOL > 0 && res==0)
        return AVPROBE_SCORE_MAX/2;
    return 0;
}
#endif

#if CONFIG_H264_DEMUXER
static int h264_probe(AVProbeData *p)
{
    uint32_t code= -1;
    int sps=0, pps=0, idr=0, res=0, sli=0;
    int i;

    for(i=0; i<p->buf_size; i++){
        code = (code<<8) + p->buf[i];
        if ((code & 0xffffff00) == 0x100) {
            int ref_idc= (code>>5)&3;
            int type   = code & 0x1F;
            static const int8_t ref_zero[32]={
                2, 0, 0, 0, 0,-1, 1,-1,
               -1, 1, 1, 1, 1,-1, 2, 2,
                2, 2, 2, 0, 2, 2, 2, 2,
                2, 2, 2, 2, 2, 2, 2, 2
            };

            if(code & 0x80) //forbidden bit
                return 0;

            if(ref_zero[type] == 1 && ref_idc)
                return 0;
            if(ref_zero[type] ==-1 && !ref_idc)
                return 0;
            if(ref_zero[type] == 2)
                res++;

            switch(type){
            case     1:   sli++; break;
            case     5:   idr++; break;
            case     7:
                if(p->buf[i+2]&0x0F)
                    return 0;
                sps++;
                break;
            case     8:   pps++; break;
            }
        }
    }
    if(sps && pps && (idr||sli>3) && res<(sps+pps+idr))
        return AVPROBE_SCORE_MAX/2+1; // +1 for .mpg
    return 0;
}
#endif

#if CONFIG_H263_DEMUXER
static int h263_probe(AVProbeData *p)
{
    int code;
    const uint8_t *d;

    d = p->buf;
    code = (d[0] << 14) | (d[1] << 6) | (d[2] >> 2);
    if (code == 0x20) {
        return 50;
    }
    return 0;
}
#endif

#if CONFIG_H261_DEMUXER
static int h261_probe(AVProbeData *p)
{
    int code;
    const uint8_t *d;

    d = p->buf;
    code = (d[0] << 12) | (d[1] << 4) | (d[2] >> 4);
    if (code == 0x10) {
        return 50;
    }
    return 0;
}
#endif

#if CONFIG_DTS_DEMUXER
#define DCA_MARKER_14B_BE 0x1FFFE800
#define DCA_MARKER_14B_LE 0xFF1F00E8
#define DCA_MARKER_RAW_BE 0x7FFE8001
#define DCA_MARKER_RAW_LE 0xFE7F0180
static int dts_probe(AVProbeData *p)
{
    const uint8_t *buf, *bufp;
    uint32_t state = -1;

    buf = p->buf;

    for(; buf < (p->buf+p->buf_size)-2; buf+=2) {
        bufp = buf;
        state = (state << 16) | bytestream_get_be16(&bufp);

        /* regular bitstream */
        if (state == DCA_MARKER_RAW_BE || state == DCA_MARKER_RAW_LE)
            return AVPROBE_SCORE_MAX/2+1;

        /* 14 bits big-endian bitstream */
        if (state == DCA_MARKER_14B_BE)
            if ((bytestream_get_be16(&bufp) & 0xFFF0) == 0x07F0)
                return AVPROBE_SCORE_MAX/2+1;

        /* 14 bits little-endian bitstream */
        if (state == DCA_MARKER_14B_LE)
            if ((bytestream_get_be16(&bufp) & 0xF0FF) == 0xF007)
                return AVPROBE_SCORE_MAX/2+1;
    }

    return 0;
}
#endif

#if CONFIG_DIRAC_DEMUXER
static int dirac_probe(AVProbeData *p)
{
    if (AV_RL32(p->buf) == MKTAG('B', 'B', 'C', 'D'))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}
#endif

#if CONFIG_DNXHD_DEMUXER
static int dnxhd_probe(AVProbeData *p)
{
    static const uint8_t header[] = {0x00,0x00,0x02,0x80,0x01};
    if (!memcmp(p->buf, header, 5))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}
#endif

#if CONFIG_AC3_DEMUXER || CONFIG_EAC3_DEMUXER
static int ac3_eac3_probe(AVProbeData *p, enum CodecID expected_codec_id)
{
    int max_frames, first_frames = 0, frames;
    uint8_t *buf, *buf2, *end;
    AC3HeaderInfo hdr;
    GetBitContext gbc;
    enum CodecID codec_id = CODEC_ID_AC3;

    max_frames = 0;
    buf = p->buf;
    end = buf + p->buf_size;

    for(; buf < end; buf++) {
        buf2 = buf;

        for(frames = 0; buf2 < end; frames++) {
            init_get_bits(&gbc, buf2, 54);
            if(ff_ac3_parse_header(&gbc, &hdr) < 0)
                break;
            if(buf2 + hdr.frame_size > end ||
               av_crc(av_crc_get_table(AV_CRC_16_ANSI), 0, buf2 + 2, hdr.frame_size - 2))
                break;
            if (hdr.bitstream_id > 10)
                codec_id = CODEC_ID_EAC3;
            buf2 += hdr.frame_size;
        }
        max_frames = FFMAX(max_frames, frames);
        if(buf == p->buf)
            first_frames = frames;
    }
    if(codec_id != expected_codec_id) return 0;
    if   (first_frames>=3) return AVPROBE_SCORE_MAX * 3 / 4;
    else if(max_frames>=3) return AVPROBE_SCORE_MAX / 2;
    else if(max_frames>=1) return 1;
    else                   return 0;
}
#endif

#if CONFIG_AC3_DEMUXER
static int ac3_probe(AVProbeData *p)
{
    return ac3_eac3_probe(p, CODEC_ID_AC3);
}
#endif

#if CONFIG_EAC3_DEMUXER
static int eac3_probe(AVProbeData *p)
{
    return ac3_eac3_probe(p, CODEC_ID_EAC3);
}
#endif

#if CONFIG_AAC_DEMUXER
static int adts_aac_probe(AVProbeData *p)
{
    int max_frames = 0, first_frames = 0;
    int fsize, frames;
    uint8_t *buf0 = p->buf;
    uint8_t *buf2;
    uint8_t *buf;
    uint8_t *end = buf0 + p->buf_size - 7;

    if (ff_id3v2_match(buf0)) {
        buf0 += ff_id3v2_tag_len(buf0);
    }
    buf = buf0;

    for(; buf < end; buf= buf2+1) {
        buf2 = buf;

        for(frames = 0; buf2 < end; frames++) {
            uint32_t header = AV_RB16(buf2);
            if((header&0xFFF6) != 0xFFF0)
                break;
            fsize = (AV_RB32(buf2+3)>>13) & 0x8FFF;
            if(fsize < 7)
                break;
            buf2 += fsize;
        }
        max_frames = FFMAX(max_frames, frames);
        if(buf == buf0)
            first_frames= frames;
    }
    if   (first_frames>=3) return AVPROBE_SCORE_MAX/2+1;
    else if(max_frames>500)return AVPROBE_SCORE_MAX/2;
    else if(max_frames>=3) return AVPROBE_SCORE_MAX/4;
    else if(max_frames>=1) return 1;
    else                   return 0;
}
#endif

/* Note: Do not forget to add new entries to the Makefile as well. */

#if CONFIG_AAC_DEMUXER
AVInputFormat aac_demuxer = {
    "aac",
    NULL_IF_CONFIG_SMALL("raw ADTS AAC"),
    0,
    adts_aac_probe,
    audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "aac",
    .value = CODEC_ID_AAC,
};
#endif

#if CONFIG_AC3_DEMUXER
AVInputFormat ac3_demuxer = {
    "ac3",
    NULL_IF_CONFIG_SMALL("raw AC-3"),
    0,
    ac3_probe,
    audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "ac3",
    .value = CODEC_ID_AC3,
};
#endif

#if CONFIG_AC3_MUXER
AVOutputFormat ac3_muxer = {
    "ac3",
    NULL_IF_CONFIG_SMALL("raw AC-3"),
    "audio/x-ac3",
    "ac3",
    0,
    CODEC_ID_AC3,
    CODEC_ID_NONE,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DIRAC_DEMUXER
AVInputFormat dirac_demuxer = {
    "dirac",
    NULL_IF_CONFIG_SMALL("raw Dirac"),
    0,
    dirac_probe,
    video_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .value = CODEC_ID_DIRAC,
};
#endif

#if CONFIG_DIRAC_MUXER
AVOutputFormat dirac_muxer = {
    "dirac",
    NULL_IF_CONFIG_SMALL("raw Dirac"),
    NULL,
    "drc",
    0,
    CODEC_ID_NONE,
    CODEC_ID_DIRAC,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DNXHD_DEMUXER
AVInputFormat dnxhd_demuxer = {
    "dnxhd",
    NULL_IF_CONFIG_SMALL("raw DNxHD (SMPTE VC-3)"),
    0,
    dnxhd_probe,
    video_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .value = CODEC_ID_DNXHD,
};
#endif

#if CONFIG_DNXHD_MUXER
AVOutputFormat dnxhd_muxer = {
    "dnxhd",
    NULL_IF_CONFIG_SMALL("raw DNxHD (SMPTE VC-3)"),
    NULL,
    "dnxhd",
    0,
    CODEC_ID_NONE,
    CODEC_ID_DNXHD,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_DTS_DEMUXER
AVInputFormat dts_demuxer = {
    "dts",
    NULL_IF_CONFIG_SMALL("raw DTS"),
    0,
    dts_probe,
    audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "dts",
    .value = CODEC_ID_DTS,
};
#endif

#if CONFIG_DTS_MUXER
AVOutputFormat dts_muxer = {
    "dts",
    NULL_IF_CONFIG_SMALL("raw DTS"),
    "audio/x-dca",
    "dts",
    0,
    CODEC_ID_DTS,
    CODEC_ID_NONE,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_EAC3_DEMUXER
AVInputFormat eac3_demuxer = {
    "eac3",
    NULL_IF_CONFIG_SMALL("raw E-AC-3"),
    0,
    eac3_probe,
    audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "eac3",
    .value = CODEC_ID_EAC3,
};
#endif

#if CONFIG_EAC3_MUXER
AVOutputFormat eac3_muxer = {
    "eac3",
    NULL_IF_CONFIG_SMALL("raw E-AC-3"),
    "audio/x-eac3",
    "eac3",
    0,
    CODEC_ID_EAC3,
    CODEC_ID_NONE,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_GSM_DEMUXER
AVInputFormat gsm_demuxer = {
    "gsm",
    NULL_IF_CONFIG_SMALL("raw GSM"),
    0,
    NULL,
    audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "gsm",
    .value = CODEC_ID_GSM,
};
#endif

#if CONFIG_H261_DEMUXER
AVInputFormat h261_demuxer = {
    "h261",
    NULL_IF_CONFIG_SMALL("raw H.261"),
    0,
    h261_probe,
    video_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "h261",
    .value = CODEC_ID_H261,
};
#endif

#if CONFIG_H261_MUXER
AVOutputFormat h261_muxer = {
    "h261",
    NULL_IF_CONFIG_SMALL("raw H.261"),
    "video/x-h261",
    "h261",
    0,
    CODEC_ID_NONE,
    CODEC_ID_H261,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_H263_DEMUXER
AVInputFormat h263_demuxer = {
    "h263",
    NULL_IF_CONFIG_SMALL("raw H.263"),
    0,
    h263_probe,
    video_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
//    .extensions = "h263", //FIXME remove after writing mpeg4_probe
    .value = CODEC_ID_H263,
};
#endif

#if CONFIG_H263_MUXER
AVOutputFormat h263_muxer = {
    "h263",
    NULL_IF_CONFIG_SMALL("raw H.263"),
    "video/x-h263",
    "h263",
    0,
    CODEC_ID_NONE,
    CODEC_ID_H263,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_H264_DEMUXER
AVInputFormat h264_demuxer = {
    "h264",
    NULL_IF_CONFIG_SMALL("raw H.264 video format"),
    0,
    h264_probe,
    video_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "h26l,h264,264", //FIXME remove after writing mpeg4_probe
    .value = CODEC_ID_H264,
};
#endif

#if CONFIG_H264_MUXER
AVOutputFormat h264_muxer = {
    "h264",
    NULL_IF_CONFIG_SMALL("raw H.264 video format"),
    NULL,
    "h264",
    0,
    CODEC_ID_NONE,
    CODEC_ID_H264,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_INGENIENT_DEMUXER
AVInputFormat ingenient_demuxer = {
    "ingenient",
    NULL_IF_CONFIG_SMALL("raw Ingenient MJPEG"),
    0,
    NULL,
    video_read_header,
    ingenient_read_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "cgi", // FIXME
    .value = CODEC_ID_MJPEG,
};
#endif

#if CONFIG_M4V_DEMUXER
AVInputFormat m4v_demuxer = {
    "m4v",
    NULL_IF_CONFIG_SMALL("raw MPEG-4 video format"),
    0,
    mpeg4video_probe, /** probing for MPEG-4 data */
    video_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "m4v",
    .value = CODEC_ID_MPEG4,
};
#endif

#if CONFIG_M4V_MUXER
AVOutputFormat m4v_muxer = {
    "m4v",
    NULL_IF_CONFIG_SMALL("raw MPEG-4 video format"),
    NULL,
    "m4v",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MPEG4,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MJPEG_DEMUXER
AVInputFormat mjpeg_demuxer = {
    "mjpeg",
    NULL_IF_CONFIG_SMALL("raw MJPEG video"),
    0,
    NULL,
    video_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "mjpg,mjpeg",
    .value = CODEC_ID_MJPEG,
};
#endif

#if CONFIG_MJPEG_MUXER
AVOutputFormat mjpeg_muxer = {
    "mjpeg",
    NULL_IF_CONFIG_SMALL("raw MJPEG video"),
    "video/x-mjpeg",
    "mjpg,mjpeg",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MJPEG,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MLP_DEMUXER
AVInputFormat mlp_demuxer = {
    "mlp",
    NULL_IF_CONFIG_SMALL("raw MLP"),
    0,
    NULL,
    audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "mlp",
    .value = CODEC_ID_MLP,
};
#endif

#if CONFIG_MPEG1VIDEO_MUXER
AVOutputFormat mpeg1video_muxer = {
    "mpeg1video",
    NULL_IF_CONFIG_SMALL("raw MPEG-1 video"),
    "video/x-mpeg",
    "mpg,mpeg,m1v",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MPEG1VIDEO,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MPEG2VIDEO_MUXER
AVOutputFormat mpeg2video_muxer = {
    "mpeg2video",
    NULL_IF_CONFIG_SMALL("raw MPEG-2 video"),
    NULL,
    "m2v",
    0,
    CODEC_ID_NONE,
    CODEC_ID_MPEG2VIDEO,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_MPEGVIDEO_DEMUXER
AVInputFormat mpegvideo_demuxer = {
    "mpegvideo",
    NULL_IF_CONFIG_SMALL("raw MPEG video"),
    0,
    mpegvideo_probe,
    video_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .value = CODEC_ID_MPEG1VIDEO,
};
#endif

#if CONFIG_CAVSVIDEO_DEMUXER
AVInputFormat cavsvideo_demuxer = {
    "cavsvideo",
    NULL_IF_CONFIG_SMALL("raw Chinese AVS video"),
    0,
    cavsvideo_probe,
    video_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .value = CODEC_ID_CAVS,
};
#endif

#if CONFIG_NULL_MUXER
AVOutputFormat null_muxer = {
    "null",
    NULL_IF_CONFIG_SMALL("raw null video format"),
    NULL,
    NULL,
    0,
#ifdef WORDS_BIGENDIAN
    CODEC_ID_PCM_S16BE,
#else
    CODEC_ID_PCM_S16LE,
#endif
    CODEC_ID_RAWVIDEO,
    NULL,
    null_write_packet,
    .flags = AVFMT_NOFILE | AVFMT_RAWPICTURE | AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_RAWVIDEO_DEMUXER
AVInputFormat rawvideo_demuxer = {
    "rawvideo",
    NULL_IF_CONFIG_SMALL("raw video format"),
    0,
    NULL,
    raw_read_header,
    rawvideo_read_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "yuv,cif,qcif,rgb",
    .value = CODEC_ID_RAWVIDEO,
};
#endif

#if CONFIG_RAWVIDEO_MUXER
AVOutputFormat rawvideo_muxer = {
    "rawvideo",
    NULL_IF_CONFIG_SMALL("raw video format"),
    NULL,
    "yuv,rgb",
    0,
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    NULL,
    raw_write_packet,
    .flags= AVFMT_NOTIMESTAMPS,
};
#endif

#if CONFIG_ROQ_MUXER
AVOutputFormat roq_muxer =
{
    "RoQ",
    NULL_IF_CONFIG_SMALL("raw id RoQ format"),
    NULL,
    "roq",
    0,
    CODEC_ID_ROQ_DPCM,
    CODEC_ID_ROQ,
    roq_write_header,
    raw_write_packet,
};
#endif

#if CONFIG_SHORTEN_DEMUXER
AVInputFormat shorten_demuxer = {
    "shn",
    NULL_IF_CONFIG_SMALL("raw Shorten"),
    0,
    NULL,
    audio_read_header,
    ff_raw_read_partial_packet,
    .flags= AVFMT_GENERIC_INDEX,
    .extensions = "shn",
    .value = CODEC_ID_SHORTEN,
};
#endif

#if CONFIG_VC1_DEMUXER
AVInputFormat vc1_demuxer = {
    "vc1",
    NULL_IF_CONFIG_SMALL("raw VC-1"),
    0,
    NULL /* vc1_probe */,
    video_read_header,
    ff_raw_read_partial_packet,
    .extensions = "vc1",
    .value = CODEC_ID_VC1,
};
#endif

/* PCM formats */

#define PCMINPUTDEF(name, long_name, ext, codec) \
AVInputFormat pcm_ ## name ## _demuxer = {\
    #name,\
    NULL_IF_CONFIG_SMALL(long_name),\
    0,\
    NULL,\
    raw_read_header,\
    raw_read_packet,\
    NULL,\
    pcm_read_seek,\
    .flags= AVFMT_GENERIC_INDEX,\
    .extensions = ext,\
    .value = codec,\
};

#define PCMOUTPUTDEF(name, long_name, ext, codec) \
AVOutputFormat pcm_ ## name ## _muxer = {\
    #name,\
    NULL_IF_CONFIG_SMALL(long_name),\
    NULL,\
    ext,\
    0,\
    codec,\
    CODEC_ID_NONE,\
    NULL,\
    raw_write_packet,\
    .flags= AVFMT_NOTIMESTAMPS,\
};


#if  !CONFIG_MUXERS && CONFIG_DEMUXERS
#define PCMDEF(name, long_name, ext, codec) \
        PCMINPUTDEF(name, long_name, ext, codec)
#elif CONFIG_MUXERS && !CONFIG_DEMUXERS
#define PCMDEF(name, long_name, ext, codec) \
        PCMOUTPUTDEF(name, long_name, ext, codec)
#elif CONFIG_MUXERS && CONFIG_DEMUXERS
#define PCMDEF(name, long_name, ext, codec) \
        PCMINPUTDEF(name, long_name, ext, codec)\
        PCMOUTPUTDEF(name, long_name, ext, codec)
#else
#define PCMDEF(name, long_name, ext, codec)
#endif

#ifdef WORDS_BIGENDIAN
#define BE_DEF(s) s
#define LE_DEF(s) NULL
#else
#define BE_DEF(s) NULL
#define LE_DEF(s) s
#endif

PCMDEF(f64be, "PCM 64 bit floating-point big-endian format",
       NULL, CODEC_ID_PCM_F64BE)

PCMDEF(f64le, "PCM 64 bit floating-point little-endian format",
       NULL, CODEC_ID_PCM_F64LE)

PCMDEF(f32be, "PCM 32 bit floating-point big-endian format",
       NULL, CODEC_ID_PCM_F32BE)

PCMDEF(f32le, "PCM 32 bit floating-point little-endian format",
       NULL, CODEC_ID_PCM_F32LE)

PCMDEF(s32be, "PCM signed 32 bit big-endian format",
       NULL, CODEC_ID_PCM_S32BE)

PCMDEF(s32le, "PCM signed 32 bit little-endian format",
       NULL, CODEC_ID_PCM_S32LE)

PCMDEF(s24be, "PCM signed 24 bit big-endian format",
       NULL, CODEC_ID_PCM_S24BE)

PCMDEF(s24le, "PCM signed 24 bit little-endian format",
       NULL, CODEC_ID_PCM_S24LE)

PCMDEF(s16be, "PCM signed 16 bit big-endian format",
       BE_DEF("sw"), CODEC_ID_PCM_S16BE)

PCMDEF(s16le, "PCM signed 16 bit little-endian format",
       LE_DEF("sw"), CODEC_ID_PCM_S16LE)

PCMDEF(s8, "PCM signed 8 bit format",
       "sb", CODEC_ID_PCM_S8)

PCMDEF(u32be, "PCM unsigned 32 bit big-endian format",
       NULL, CODEC_ID_PCM_U32BE)

PCMDEF(u32le, "PCM unsigned 32 bit little-endian format",
       NULL, CODEC_ID_PCM_U32LE)

PCMDEF(u24be, "PCM unsigned 24 bit big-endian format",
       NULL, CODEC_ID_PCM_U24BE)

PCMDEF(u24le, "PCM unsigned 24 bit little-endian format",
       NULL, CODEC_ID_PCM_U24LE)

PCMDEF(u16be, "PCM unsigned 16 bit big-endian format",
       BE_DEF("uw"), CODEC_ID_PCM_U16BE)

PCMDEF(u16le, "PCM unsigned 16 bit little-endian format",
       LE_DEF("uw"), CODEC_ID_PCM_U16LE)

PCMDEF(u8, "PCM unsigned 8 bit format",
       "ub", CODEC_ID_PCM_U8)

PCMDEF(alaw, "PCM A-law format",
       "al", CODEC_ID_PCM_ALAW)

PCMDEF(mulaw, "PCM mu-law format",
       "ul", CODEC_ID_PCM_MULAW)
