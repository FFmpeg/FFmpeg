/*
 * YUV4MPEG format
 * Copyright (c) 2001, 2002, 2003 Fabrice Bellard
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

#define Y4M_MAGIC "YUV4MPEG2"
#define Y4M_FRAME_MAGIC "FRAME"
#define Y4M_LINE_MAX 256

struct frame_attributes {
    int interlaced_frame;
    int top_field_first;
};

#if CONFIG_YUV4MPEGPIPE_MUXER
static int yuv4_generate_header(AVFormatContext *s, char* buf)
{
    AVStream *st;
    int width, height;
    int raten, rated, aspectn, aspectd, n;
    char inter;
    const char *colorspace = "";

    st = s->streams[0];
    width = st->codec->width;
    height = st->codec->height;

    av_reduce(&raten, &rated, st->codec->time_base.den, st->codec->time_base.num, (1UL<<31)-1);

    aspectn = st->sample_aspect_ratio.num;
    aspectd = st->sample_aspect_ratio.den;

    if ( aspectn == 0 && aspectd == 1 ) aspectd = 0;  // 0:0 means unknown

    inter = 'p'; /* progressive is the default */
    if (st->codec->coded_frame && st->codec->coded_frame->interlaced_frame) {
        inter = st->codec->coded_frame->top_field_first ? 't' : 'b';
    }

    switch(st->codec->pix_fmt) {
    case PIX_FMT_GRAY8:
        colorspace = " Cmono";
        break;
    case PIX_FMT_YUV411P:
        colorspace = " C411 XYSCSS=411";
        break;
    case PIX_FMT_YUV420P:
        colorspace = (st->codec->chroma_sample_location == AVCHROMA_LOC_TOPLEFT)?" C420paldv XYSCSS=420PALDV":
                     (st->codec->chroma_sample_location == AVCHROMA_LOC_LEFT)   ?" C420mpeg2 XYSCSS=420MPEG2":
                     " C420jpeg XYSCSS=420JPEG";
        break;
    case PIX_FMT_YUV422P:
        colorspace = " C422 XYSCSS=422";
        break;
    case PIX_FMT_YUV444P:
        colorspace = " C444 XYSCSS=444";
        break;
    }

    /* construct stream header, if this is the first frame */
    n = snprintf(buf, Y4M_LINE_MAX, "%s W%d H%d F%d:%d I%c A%d:%d%s\n",
                 Y4M_MAGIC,
                 width,
                 height,
                 raten, rated,
                 inter,
                 aspectn, aspectd,
                 colorspace);

    return n;
}

static int yuv4_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    ByteIOContext *pb = s->pb;
    AVPicture *picture;
    int* first_pkt = s->priv_data;
    int width, height, h_chroma_shift, v_chroma_shift;
    int i, m;
    char buf2[Y4M_LINE_MAX+1];
    char buf1[20];
    uint8_t *ptr, *ptr1, *ptr2;

    picture = (AVPicture *)pkt->data;

    /* for the first packet we have to output the header as well */
    if (*first_pkt) {
        *first_pkt = 0;
        if (yuv4_generate_header(s, buf2) < 0) {
            av_log(s, AV_LOG_ERROR, "Error. YUV4MPEG stream header write failed.\n");
            return AVERROR(EIO);
        } else {
            put_buffer(pb, buf2, strlen(buf2));
        }
    }

    /* construct frame header */

    m = snprintf(buf1, sizeof(buf1), "%s\n", Y4M_FRAME_MAGIC);
    put_buffer(pb, buf1, strlen(buf1));

    width = st->codec->width;
    height = st->codec->height;

    ptr = picture->data[0];
    for(i=0;i<height;i++) {
        put_buffer(pb, ptr, width);
        ptr += picture->linesize[0];
    }

    if (st->codec->pix_fmt != PIX_FMT_GRAY8){
    // Adjust for smaller Cb and Cr planes
    avcodec_get_chroma_sub_sample(st->codec->pix_fmt, &h_chroma_shift, &v_chroma_shift);
    width >>= h_chroma_shift;
    height >>= v_chroma_shift;

    ptr1 = picture->data[1];
    ptr2 = picture->data[2];
    for(i=0;i<height;i++) {     /* Cb */
        put_buffer(pb, ptr1, width);
        ptr1 += picture->linesize[1];
    }
    for(i=0;i<height;i++) {     /* Cr */
        put_buffer(pb, ptr2, width);
            ptr2 += picture->linesize[2];
    }
    }
    put_flush_packet(pb);
    return 0;
}

static int yuv4_write_header(AVFormatContext *s)
{
    int* first_pkt = s->priv_data;

    if (s->nb_streams != 1)
        return AVERROR(EIO);

    if (s->streams[0]->codec->pix_fmt == PIX_FMT_YUV411P) {
        av_log(s, AV_LOG_ERROR, "Warning: generating rarely used 4:1:1 YUV stream, some mjpegtools might not work.\n");
    }
    else if ((s->streams[0]->codec->pix_fmt != PIX_FMT_YUV420P) &&
             (s->streams[0]->codec->pix_fmt != PIX_FMT_YUV422P) &&
             (s->streams[0]->codec->pix_fmt != PIX_FMT_GRAY8) &&
             (s->streams[0]->codec->pix_fmt != PIX_FMT_YUV444P)) {
        av_log(s, AV_LOG_ERROR, "ERROR: yuv4mpeg only handles yuv444p, yuv422p, yuv420p, yuv411p and gray pixel formats. Use -pix_fmt to select one.\n");
        return AVERROR(EIO);
    }

    *first_pkt = 1;
    return 0;
}

AVOutputFormat yuv4mpegpipe_muxer = {
    "yuv4mpegpipe",
    NULL_IF_CONFIG_SMALL("YUV4MPEG pipe format"),
    "",
    "y4m",
    sizeof(int),
    CODEC_ID_NONE,
    CODEC_ID_RAWVIDEO,
    yuv4_write_header,
    yuv4_write_packet,
    .flags = AVFMT_RAWPICTURE,
};
#endif

/* Header size increased to allow room for optional flags */
#define MAX_YUV4_HEADER 80
#define MAX_FRAME_HEADER 80

static int yuv4_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    char header[MAX_YUV4_HEADER+10];  // Include headroom for the longest option
    char *tokstart,*tokend,*header_end;
    int i;
    ByteIOContext *pb = s->pb;
    int width=-1, height=-1, raten=0, rated=0, aspectn=0, aspectd=0;
    enum PixelFormat pix_fmt=PIX_FMT_NONE,alt_pix_fmt=PIX_FMT_NONE;
    enum AVChromaLocation chroma_sample_location = AVCHROMA_LOC_UNSPECIFIED;
    AVStream *st;
    struct frame_attributes *s1 = s->priv_data;

    for (i=0; i<MAX_YUV4_HEADER; i++) {
        header[i] = get_byte(pb);
        if (header[i] == '\n') {
            header[i+1] = 0x20;  // Add a space after last option. Makes parsing "444" vs "444alpha" easier.
            header[i+2] = 0;
            break;
        }
    }
    if (i == MAX_YUV4_HEADER) return -1;
    if (strncmp(header, Y4M_MAGIC, strlen(Y4M_MAGIC))) return -1;

    s1->interlaced_frame = 0;
    s1->top_field_first = 0;
    header_end = &header[i+1]; // Include space
    for(tokstart = &header[strlen(Y4M_MAGIC) + 1]; tokstart < header_end; tokstart++) {
        if (*tokstart==0x20) continue;
        switch (*tokstart++) {
        case 'W': // Width. Required.
            width = strtol(tokstart, &tokend, 10);
            tokstart=tokend;
            break;
        case 'H': // Height. Required.
            height = strtol(tokstart, &tokend, 10);
            tokstart=tokend;
            break;
        case 'C': // Color space
            if (strncmp("420jpeg",tokstart,7)==0) {
                pix_fmt = PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_CENTER;
            } else if (strncmp("420mpeg2",tokstart,8)==0) {
                pix_fmt = PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_LEFT;
            } else if (strncmp("420paldv", tokstart, 8)==0) {
                pix_fmt = PIX_FMT_YUV420P;
                chroma_sample_location = AVCHROMA_LOC_TOPLEFT;
            } else if (strncmp("411", tokstart, 3)==0)
                pix_fmt = PIX_FMT_YUV411P;
            else if (strncmp("422", tokstart, 3)==0)
                pix_fmt = PIX_FMT_YUV422P;
            else if (strncmp("444alpha", tokstart, 8)==0) {
                av_log(s, AV_LOG_ERROR, "Cannot handle 4:4:4:4 YUV4MPEG stream.\n");
                return -1;
            } else if (strncmp("444", tokstart, 3)==0)
                pix_fmt = PIX_FMT_YUV444P;
            else if (strncmp("mono",tokstart, 4)==0) {
                pix_fmt = PIX_FMT_GRAY8;
            } else {
                av_log(s, AV_LOG_ERROR, "YUV4MPEG stream contains an unknown pixel format.\n");
                return -1;
            }
            while(tokstart<header_end&&*tokstart!=0x20) tokstart++;
            break;
        case 'I': // Interlace type
            switch (*tokstart++){
            case '?':
                break;
            case 'p':
                s1->interlaced_frame=0;
                break;
            case 't':
                s1->interlaced_frame=1;
                s1->top_field_first=1;
                break;
            case 'b':
                s1->interlaced_frame=1;
                s1->top_field_first=0;
                break;
            case 'm':
                av_log(s, AV_LOG_ERROR, "YUV4MPEG stream contains mixed interlaced and non-interlaced frames.\n");
                return -1;
            default:
                av_log(s, AV_LOG_ERROR, "YUV4MPEG has invalid header.\n");
                return -1;
            }
            break;
        case 'F': // Frame rate
            sscanf(tokstart,"%d:%d",&raten,&rated); // 0:0 if unknown
            while(tokstart<header_end&&*tokstart!=0x20) tokstart++;
            break;
        case 'A': // Pixel aspect
            sscanf(tokstart,"%d:%d",&aspectn,&aspectd); // 0:0 if unknown
            while(tokstart<header_end&&*tokstart!=0x20) tokstart++;
            break;
        case 'X': // Vendor extensions
            if (strncmp("YSCSS=",tokstart,6)==0) {
                // Older nonstandard pixel format representation
                tokstart+=6;
                if (strncmp("420JPEG",tokstart,7)==0)
                    alt_pix_fmt=PIX_FMT_YUV420P;
                else if (strncmp("420MPEG2",tokstart,8)==0)
                    alt_pix_fmt=PIX_FMT_YUV420P;
                else if (strncmp("420PALDV",tokstart,8)==0)
                    alt_pix_fmt=PIX_FMT_YUV420P;
                else if (strncmp("411",tokstart,3)==0)
                    alt_pix_fmt=PIX_FMT_YUV411P;
                else if (strncmp("422",tokstart,3)==0)
                    alt_pix_fmt=PIX_FMT_YUV422P;
                else if (strncmp("444",tokstart,3)==0)
                    alt_pix_fmt=PIX_FMT_YUV444P;
            }
            while(tokstart<header_end&&*tokstart!=0x20) tokstart++;
            break;
        }
    }

    if ((width == -1) || (height == -1)) {
        av_log(s, AV_LOG_ERROR, "YUV4MPEG has invalid header.\n");
        return -1;
    }

    if (pix_fmt == PIX_FMT_NONE) {
        if (alt_pix_fmt == PIX_FMT_NONE)
            pix_fmt = PIX_FMT_YUV420P;
        else
            pix_fmt = alt_pix_fmt;
    }

    if (raten <= 0 || rated <= 0) {
        // Frame rate unknown
        raten = 25;
        rated = 1;
    }

    if (aspectn == 0 && aspectd == 0) {
        // Pixel aspect unknown
        aspectd = 1;
    }

    st = av_new_stream(s, 0);
    if(!st)
        return AVERROR(ENOMEM);
    st->codec->width = width;
    st->codec->height = height;
    av_reduce(&raten, &rated, raten, rated, (1UL<<31)-1);
    av_set_pts_info(st, 64, rated, raten);
    st->codec->pix_fmt = pix_fmt;
    st->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    st->codec->codec_id = CODEC_ID_RAWVIDEO;
    st->sample_aspect_ratio= (AVRational){aspectn, aspectd};
    st->codec->chroma_sample_location = chroma_sample_location;

    return 0;
}

static int yuv4_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int i;
    char header[MAX_FRAME_HEADER+1];
    int packet_size, width, height;
    AVStream *st = s->streams[0];
    struct frame_attributes *s1 = s->priv_data;

    for (i=0; i<MAX_FRAME_HEADER; i++) {
        header[i] = get_byte(s->pb);
        if (header[i] == '\n') {
            header[i+1] = 0;
            break;
        }
    }
    if (i == MAX_FRAME_HEADER) return -1;
    if (strncmp(header, Y4M_FRAME_MAGIC, strlen(Y4M_FRAME_MAGIC))) return -1;

    width = st->codec->width;
    height = st->codec->height;

    packet_size = avpicture_get_size(st->codec->pix_fmt, width, height);
    if (packet_size < 0)
        return -1;

    if (av_get_packet(s->pb, pkt, packet_size) != packet_size)
        return AVERROR(EIO);

    if (s->streams[0]->codec->coded_frame) {
        s->streams[0]->codec->coded_frame->interlaced_frame = s1->interlaced_frame;
        s->streams[0]->codec->coded_frame->top_field_first = s1->top_field_first;
    }

    pkt->stream_index = 0;
    return 0;
}

static int yuv4_probe(AVProbeData *pd)
{
    /* check file header */
    if (strncmp(pd->buf, Y4M_MAGIC, sizeof(Y4M_MAGIC)-1)==0)
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

#if CONFIG_YUV4MPEGPIPE_DEMUXER
AVInputFormat yuv4mpegpipe_demuxer = {
    "yuv4mpegpipe",
    NULL_IF_CONFIG_SMALL("YUV4MPEG pipe format"),
    sizeof(struct frame_attributes),
    yuv4_probe,
    yuv4_read_header,
    yuv4_read_packet,
    .extensions = "y4m"
};
#endif
