/*
 * FFM (ffserver live feed) muxer and demuxer
 * Copyright (c) 2001 Fabrice Bellard.
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
#include <unistd.h>

/* The FFM file is made of blocks of fixed size */
#define FFM_HEADER_SIZE 14
#define PACKET_ID       0x666d

/* each packet contains frames (which can span several packets */
#define FRAME_HEADER_SIZE    8
#define FLAG_KEY_FRAME       0x01

typedef struct FFMStream {
    int64_t pts;
} FFMStream;

enum {
    READ_HEADER,
    READ_DATA,
};

typedef struct FFMContext {
    /* only reading mode */
    offset_t write_index, file_size;
    int read_state;
    uint8_t header[FRAME_HEADER_SIZE];

    /* read and write */
    int first_packet; /* true if first packet, needed to set the discontinuity tag */
    int first_frame_in_packet; /* true if first frame in packet, needed to know if PTS information is valid */
    int packet_size;
    int frame_offset;
    int64_t pts;
    uint8_t *packet_ptr, *packet_end;
    uint8_t packet[FFM_PACKET_SIZE];
} FFMContext;

static int64_t get_pts(AVFormatContext *s, offset_t pos);

/* disable pts hack for testing */
int ffm_nopts = 0;

#ifdef CONFIG_MUXERS
static void flush_packet(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    int fill_size, h;
    ByteIOContext *pb = &s->pb;

    fill_size = ffm->packet_end - ffm->packet_ptr;
    memset(ffm->packet_ptr, 0, fill_size);

    if (url_ftell(pb) % ffm->packet_size)
        av_abort();

    /* put header */
    put_be16(pb, PACKET_ID);
    put_be16(pb, fill_size);
    put_be64(pb, ffm->pts);
    h = ffm->frame_offset;
    if (ffm->first_packet)
        h |= 0x8000;
    put_be16(pb, h);
    put_buffer(pb, ffm->packet, ffm->packet_end - ffm->packet);
    put_flush_packet(pb);

    /* prepare next packet */
    ffm->frame_offset = 0; /* no key frame */
    ffm->pts = 0; /* no pts */
    ffm->packet_ptr = ffm->packet;
    ffm->first_packet = 0;
}

/* 'first' is true if first data of a frame */
static void ffm_write_data(AVFormatContext *s,
                           const uint8_t *buf, int size,
                           int64_t pts, int first)
{
    FFMContext *ffm = s->priv_data;
    int len;

    if (first && ffm->frame_offset == 0)
        ffm->frame_offset = ffm->packet_ptr - ffm->packet + FFM_HEADER_SIZE;
    if (first && ffm->pts == 0)
        ffm->pts = pts;

    /* write as many packets as needed */
    while (size > 0) {
        len = ffm->packet_end - ffm->packet_ptr;
        if (len > size)
            len = size;
        memcpy(ffm->packet_ptr, buf, len);

        ffm->packet_ptr += len;
        buf += len;
        size -= len;
        if (ffm->packet_ptr >= ffm->packet_end) {
            /* special case : no pts in packet : we leave the current one */
            if (ffm->pts == 0)
                ffm->pts = pts;

            flush_packet(s);
        }
    }
}

static int ffm_write_header(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    AVStream *st;
    FFMStream *fst;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *codec;
    int bit_rate, i;

    ffm->packet_size = FFM_PACKET_SIZE;

    /* header */
    put_le32(pb, MKTAG('F', 'F', 'M', '1'));
    put_be32(pb, ffm->packet_size);
    /* XXX: store write position in other file ? */
    put_be64(pb, ffm->packet_size); /* current write position */

    put_be32(pb, s->nb_streams);
    bit_rate = 0;
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        bit_rate += st->codec->bit_rate;
    }
    put_be32(pb, bit_rate);

    /* list of streams */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        fst = av_mallocz(sizeof(FFMStream));
        if (!fst)
            goto fail;
        av_set_pts_info(st, 64, 1, 1000000);
        st->priv_data = fst;

        codec = st->codec;
        /* generic info */
        put_be32(pb, codec->codec_id);
        put_byte(pb, codec->codec_type);
        put_be32(pb, codec->bit_rate);
        put_be32(pb, st->quality);
        put_be32(pb, codec->flags);
        put_be32(pb, codec->flags2);
        put_be32(pb, codec->debug);
        /* specific info */
        switch(codec->codec_type) {
        case CODEC_TYPE_VIDEO:
            put_be32(pb, codec->time_base.num);
            put_be32(pb, codec->time_base.den);
            put_be16(pb, codec->width);
            put_be16(pb, codec->height);
            put_be16(pb, codec->gop_size);
            put_be32(pb, codec->pix_fmt);
            put_byte(pb, codec->qmin);
            put_byte(pb, codec->qmax);
            put_byte(pb, codec->max_qdiff);
            put_be16(pb, (int) (codec->qcompress * 10000.0));
            put_be16(pb, (int) (codec->qblur * 10000.0));
            put_be32(pb, codec->bit_rate_tolerance);
            put_strz(pb, codec->rc_eq);
            put_be32(pb, codec->rc_max_rate);
            put_be32(pb, codec->rc_min_rate);
            put_be32(pb, codec->rc_buffer_size);
            put_be64(pb, av_dbl2int(codec->i_quant_factor));
            put_be64(pb, av_dbl2int(codec->b_quant_factor));
            put_be64(pb, av_dbl2int(codec->i_quant_offset));
            put_be64(pb, av_dbl2int(codec->b_quant_offset));
            put_be32(pb, codec->dct_algo);
            put_be32(pb, codec->strict_std_compliance);
            put_be32(pb, codec->max_b_frames);
            put_be32(pb, codec->luma_elim_threshold);
            put_be32(pb, codec->chroma_elim_threshold);
            put_be32(pb, codec->mpeg_quant);
            put_be32(pb, codec->intra_dc_precision);
            put_be32(pb, codec->me_method);
            put_be32(pb, codec->mb_decision);
            put_be32(pb, codec->nsse_weight);
            put_be32(pb, codec->frame_skip_cmp);
            put_be64(pb, av_dbl2int(codec->rc_buffer_aggressivity));
            put_be32(pb, codec->codec_tag);
            break;
        case CODEC_TYPE_AUDIO:
            put_be32(pb, codec->sample_rate);
            put_le16(pb, codec->channels);
            put_le16(pb, codec->frame_size);
            break;
        default:
            return -1;
        }
        /* hack to have real time */
        if (ffm_nopts)
            fst->pts = 0;
        else
            fst->pts = av_gettime();
    }

    /* flush until end of block reached */
    while ((url_ftell(pb) % ffm->packet_size) != 0)
        put_byte(pb, 0);

    put_flush_packet(pb);

    /* init packet mux */
    ffm->packet_ptr = ffm->packet;
    ffm->packet_end = ffm->packet + ffm->packet_size - FFM_HEADER_SIZE;
    assert(ffm->packet_end >= ffm->packet);
    ffm->frame_offset = 0;
    ffm->pts = 0;
    ffm->first_packet = 1;

    return 0;
 fail:
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        av_freep(&st->priv_data);
    }
    return -1;
}

static int ffm_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    FFMStream *fst = st->priv_data;
    int64_t pts;
    uint8_t header[FRAME_HEADER_SIZE];
    int duration;
    int size= pkt->size;

    //XXX/FIXME use duration from pkt
    if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
        duration = ((float)st->codec->frame_size / st->codec->sample_rate * 1000000.0);
    } else {
        duration = (1000000.0 * st->codec->time_base.num / (float)st->codec->time_base.den);
    }

    pts = fst->pts;
    /* packet size & key_frame */
    header[0] = pkt->stream_index;
    header[1] = 0;
    if (pkt->flags & PKT_FLAG_KEY)
        header[1] |= FLAG_KEY_FRAME;
    header[2] = (size >> 16) & 0xff;
    header[3] = (size >> 8) & 0xff;
    header[4] = size & 0xff;
    header[5] = (duration >> 16) & 0xff;
    header[6] = (duration >> 8) & 0xff;
    header[7] = duration & 0xff;
    ffm_write_data(s, header, FRAME_HEADER_SIZE, pts, 1);
    ffm_write_data(s, pkt->data, size, pts, 0);

    fst->pts += duration;
    return 0;
}

static int ffm_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    FFMContext *ffm = s->priv_data;

    /* flush packets */
    if (ffm->packet_ptr > ffm->packet)
        flush_packet(s);

    put_flush_packet(pb);

    if (!url_is_streamed(pb)) {
        int64_t size;
        /* update the write offset */
        size = url_ftell(pb);
        url_fseek(pb, 8, SEEK_SET);
        put_be64(pb, size);
        put_flush_packet(pb);
    }

    return 0;
}
#endif //CONFIG_MUXERS

/* ffm demux */

static int ffm_is_avail_data(AVFormatContext *s, int size)
{
    FFMContext *ffm = s->priv_data;
    offset_t pos, avail_size;
    int len;

    len = ffm->packet_end - ffm->packet_ptr;
    if (!ffm_nopts) {
        /* XXX: I don't understand this test, so I disabled it for testing */
        if (size <= len)
            return 1;
    }
    pos = url_ftell(&s->pb);
    if (pos == ffm->write_index) {
        /* exactly at the end of stream */
        return 0;
    } else if (pos < ffm->write_index) {
        avail_size = ffm->write_index - pos;
    } else {
        avail_size = (ffm->file_size - pos) + (ffm->write_index - FFM_PACKET_SIZE);
    }
    avail_size = (avail_size / ffm->packet_size) * (ffm->packet_size - FFM_HEADER_SIZE) + len;
    if (size <= avail_size)
        return 1;
    else
        return 0;
}

/* first is true if we read the frame header */
static int ffm_read_data(AVFormatContext *s,
                         uint8_t *buf, int size, int first)
{
    FFMContext *ffm = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int len, fill_size, size1, frame_offset;

    size1 = size;
    while (size > 0) {
    redo:
        len = ffm->packet_end - ffm->packet_ptr;
        if (len > size)
            len = size;
        if (len == 0) {
            if (url_ftell(pb) == ffm->file_size)
                url_fseek(pb, ffm->packet_size, SEEK_SET);
    retry_read:
            get_be16(pb); /* PACKET_ID */
            fill_size = get_be16(pb);
            ffm->pts = get_be64(pb);
            ffm->first_frame_in_packet = 1;
            frame_offset = get_be16(pb);
            get_buffer(pb, ffm->packet, ffm->packet_size - FFM_HEADER_SIZE);
            ffm->packet_end = ffm->packet + (ffm->packet_size - FFM_HEADER_SIZE - fill_size);
            if (ffm->packet_end < ffm->packet)
                return -1;
            /* if first packet or resynchronization packet, we must
               handle it specifically */
            if (ffm->first_packet || (frame_offset & 0x8000)) {
                if (!frame_offset) {
                    /* This packet has no frame headers in it */
                    if (url_ftell(pb) >= ffm->packet_size * 3) {
                        url_fseek(pb, -ffm->packet_size * 2, SEEK_CUR);
                        goto retry_read;
                    }
                    /* This is bad, we cannot find a valid frame header */
                    return 0;
                }
                ffm->first_packet = 0;
                if ((frame_offset & 0x7ffff) < FFM_HEADER_SIZE)
                    return -1;
                ffm->packet_ptr = ffm->packet + (frame_offset & 0x7fff) - FFM_HEADER_SIZE;
                if (!first)
                    break;
            } else {
                ffm->packet_ptr = ffm->packet;
            }
            goto redo;
        }
        memcpy(buf, ffm->packet_ptr, len);
        buf += len;
        ffm->packet_ptr += len;
        size -= len;
        first = 0;
    }
    return size1 - size;
}


static void adjust_write_index(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int64_t pts;
    //offset_t orig_write_index = ffm->write_index;
    offset_t pos_min, pos_max;
    int64_t pts_start;
    offset_t ptr = url_ftell(pb);


    pos_min = 0;
    pos_max = ffm->file_size - 2 * FFM_PACKET_SIZE;

    pts_start = get_pts(s, pos_min);

    pts = get_pts(s, pos_max);

    if (pts - 100000 > pts_start)
        goto end;

    ffm->write_index = FFM_PACKET_SIZE;

    pts_start = get_pts(s, pos_min);

    pts = get_pts(s, pos_max);

    if (pts - 100000 <= pts_start) {
        while (1) {
            offset_t newpos;
            int64_t newpts;

            newpos = ((pos_max + pos_min) / (2 * FFM_PACKET_SIZE)) * FFM_PACKET_SIZE;

            if (newpos == pos_min)
                break;

            newpts = get_pts(s, newpos);

            if (newpts - 100000 <= pts) {
                pos_max = newpos;
                pts = newpts;
            } else {
                pos_min = newpos;
            }
        }
        ffm->write_index += pos_max;
    }

    //printf("Adjusted write index from %"PRId64" to %"PRId64": pts=%0.6f\n", orig_write_index, ffm->write_index, pts / 1000000.);
    //printf("pts range %0.6f - %0.6f\n", get_pts(s, 0) / 1000000. , get_pts(s, ffm->file_size - 2 * FFM_PACKET_SIZE) / 1000000. );

 end:
    url_fseek(pb, ptr, SEEK_SET);
}


static int ffm_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    FFMContext *ffm = s->priv_data;
    AVStream *st;
    FFMStream *fst;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *codec;
    int i, nb_streams;
    uint32_t tag;

    /* header */
    tag = get_le32(pb);
    if (tag != MKTAG('F', 'F', 'M', '1'))
        goto fail;
    ffm->packet_size = get_be32(pb);
    if (ffm->packet_size != FFM_PACKET_SIZE)
        goto fail;
    ffm->write_index = get_be64(pb);
    /* get also filesize */
    if (!url_is_streamed(pb)) {
        ffm->file_size = url_fsize(pb);
        adjust_write_index(s);
    } else {
        ffm->file_size = (UINT64_C(1) << 63) - 1;
    }

    nb_streams = get_be32(pb);
    get_be32(pb); /* total bitrate */
    /* read each stream */
    for(i=0;i<nb_streams;i++) {
        char rc_eq_buf[128];

        st = av_new_stream(s, 0);
        if (!st)
            goto fail;
        fst = av_mallocz(sizeof(FFMStream));
        if (!fst)
            goto fail;
        s->streams[i] = st;

        av_set_pts_info(st, 64, 1, 1000000);

        st->priv_data = fst;

        codec = st->codec;
        /* generic info */
        codec->codec_id = get_be32(pb);
        codec->codec_type = get_byte(pb); /* codec_type */
        codec->bit_rate = get_be32(pb);
        st->quality = get_be32(pb);
        codec->flags = get_be32(pb);
        codec->flags2 = get_be32(pb);
        codec->debug = get_be32(pb);
        /* specific info */
        switch(codec->codec_type) {
        case CODEC_TYPE_VIDEO:
            codec->time_base.num = get_be32(pb);
            codec->time_base.den = get_be32(pb);
            codec->width = get_be16(pb);
            codec->height = get_be16(pb);
            codec->gop_size = get_be16(pb);
            codec->pix_fmt = get_be32(pb);
            codec->qmin = get_byte(pb);
            codec->qmax = get_byte(pb);
            codec->max_qdiff = get_byte(pb);
            codec->qcompress = get_be16(pb) / 10000.0;
            codec->qblur = get_be16(pb) / 10000.0;
            codec->bit_rate_tolerance = get_be32(pb);
            codec->rc_eq = av_strdup(get_strz(pb, rc_eq_buf, sizeof(rc_eq_buf)));
            codec->rc_max_rate = get_be32(pb);
            codec->rc_min_rate = get_be32(pb);
            codec->rc_buffer_size = get_be32(pb);
            codec->i_quant_factor = av_int2dbl(get_be64(pb));
            codec->b_quant_factor = av_int2dbl(get_be64(pb));
            codec->i_quant_offset = av_int2dbl(get_be64(pb));
            codec->b_quant_offset = av_int2dbl(get_be64(pb));
            codec->dct_algo = get_be32(pb);
            codec->strict_std_compliance = get_be32(pb);
            codec->max_b_frames = get_be32(pb);
            codec->luma_elim_threshold = get_be32(pb);
            codec->chroma_elim_threshold = get_be32(pb);
            codec->mpeg_quant = get_be32(pb);
            codec->intra_dc_precision = get_be32(pb);
            codec->me_method = get_be32(pb);
            codec->mb_decision = get_be32(pb);
            codec->nsse_weight = get_be32(pb);
            codec->frame_skip_cmp = get_be32(pb);
            codec->rc_buffer_aggressivity = av_int2dbl(get_be64(pb));
            codec->codec_tag = get_be32(pb);
            break;
        case CODEC_TYPE_AUDIO:
            codec->sample_rate = get_be32(pb);
            codec->channels = get_le16(pb);
            codec->frame_size = get_le16(pb);
            break;
        default:
            goto fail;
        }

    }

    /* get until end of block reached */
    while ((url_ftell(pb) % ffm->packet_size) != 0)
        get_byte(pb);

    /* init packet demux */
    ffm->packet_ptr = ffm->packet;
    ffm->packet_end = ffm->packet;
    ffm->frame_offset = 0;
    ffm->pts = 0;
    ffm->read_state = READ_HEADER;
    ffm->first_packet = 1;
    return 0;
 fail:
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        if (st) {
            av_freep(&st->priv_data);
            av_free(st);
        }
    }
    return -1;
}

/* return < 0 if eof */
static int ffm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int size;
    FFMContext *ffm = s->priv_data;
    int duration;

    switch(ffm->read_state) {
    case READ_HEADER:
        if (!ffm_is_avail_data(s, FRAME_HEADER_SIZE)) {
            return AVERROR(EAGAIN);
        }
#if 0
        printf("pos=%08"PRIx64" spos=%"PRIx64", write_index=%"PRIx64" size=%"PRIx64"\n",
               url_ftell(&s->pb), s->pb.pos, ffm->write_index, ffm->file_size);
#endif
        if (ffm_read_data(s, ffm->header, FRAME_HEADER_SIZE, 1) !=
            FRAME_HEADER_SIZE)
            return AVERROR(EAGAIN);
#if 0
        {
            int i;
            for(i=0;i<FRAME_HEADER_SIZE;i++)
                printf("%02x ", ffm->header[i]);
            printf("\n");
        }
#endif
        ffm->read_state = READ_DATA;
        /* fall thru */
    case READ_DATA:
        size = (ffm->header[2] << 16) | (ffm->header[3] << 8) | ffm->header[4];
        if (!ffm_is_avail_data(s, size)) {
            return AVERROR(EAGAIN);
        }

        duration = (ffm->header[5] << 16) | (ffm->header[6] << 8) | ffm->header[7];

        av_new_packet(pkt, size);
        pkt->stream_index = ffm->header[0];
        pkt->pos = url_ftell(&s->pb);
        if (ffm->header[1] & FLAG_KEY_FRAME)
            pkt->flags |= PKT_FLAG_KEY;

        ffm->read_state = READ_HEADER;
        if (ffm_read_data(s, pkt->data, size, 0) != size) {
            /* bad case: desynchronized packet. we cancel all the packet loading */
            av_free_packet(pkt);
            return AVERROR(EAGAIN);
        }
        if (ffm->first_frame_in_packet)
        {
            pkt->pts = ffm->pts;
            ffm->first_frame_in_packet = 0;
        }
        pkt->duration = duration;
        break;
    }
    return 0;
}

//#define DEBUG_SEEK

/* pos is between 0 and file_size - FFM_PACKET_SIZE. It is translated
   by the write position inside this function */
static void ffm_seek1(AVFormatContext *s, offset_t pos1)
{
    FFMContext *ffm = s->priv_data;
    ByteIOContext *pb = &s->pb;
    offset_t pos;

    pos = pos1 + ffm->write_index;
    if (pos >= ffm->file_size)
        pos -= (ffm->file_size - FFM_PACKET_SIZE);
#ifdef DEBUG_SEEK
    printf("seek to %"PRIx64" -> %"PRIx64"\n", pos1, pos);
#endif
    url_fseek(pb, pos, SEEK_SET);
}

static int64_t get_pts(AVFormatContext *s, offset_t pos)
{
    ByteIOContext *pb = &s->pb;
    int64_t pts;

    ffm_seek1(s, pos);
    url_fskip(pb, 4);
    pts = get_be64(pb);
#ifdef DEBUG_SEEK
    printf("pts=%0.6f\n", pts / 1000000.0);
#endif
    return pts;
}

/* seek to a given time in the file. The file read pointer is
   positionned at or before pts. XXX: the following code is quite
   approximative */
static int ffm_seek(AVFormatContext *s, int stream_index, int64_t wanted_pts, int flags)
{
    FFMContext *ffm = s->priv_data;
    offset_t pos_min, pos_max, pos;
    int64_t pts_min, pts_max, pts;
    double pos1;

#ifdef DEBUG_SEEK
    printf("wanted_pts=%0.6f\n", wanted_pts / 1000000.0);
#endif
    /* find the position using linear interpolation (better than
       dichotomy in typical cases) */
    pos_min = 0;
    pos_max = ffm->file_size - 2 * FFM_PACKET_SIZE;
    while (pos_min <= pos_max) {
        pts_min = get_pts(s, pos_min);
        pts_max = get_pts(s, pos_max);
        /* linear interpolation */
        pos1 = (double)(pos_max - pos_min) * (double)(wanted_pts - pts_min) /
            (double)(pts_max - pts_min);
        pos = (((int64_t)pos1) / FFM_PACKET_SIZE) * FFM_PACKET_SIZE;
        if (pos <= pos_min)
            pos = pos_min;
        else if (pos >= pos_max)
            pos = pos_max;
        pts = get_pts(s, pos);
        /* check if we are lucky */
        if (pts == wanted_pts) {
            goto found;
        } else if (pts > wanted_pts) {
            pos_max = pos - FFM_PACKET_SIZE;
        } else {
            pos_min = pos + FFM_PACKET_SIZE;
        }
    }
    pos = (flags & AVSEEK_FLAG_BACKWARD) ? pos_min : pos_max;
    if (pos > 0)
        pos -= FFM_PACKET_SIZE;
 found:
    ffm_seek1(s, pos);
    return 0;
}

#ifdef CONFIG_FFSERVER
offset_t ffm_read_write_index(int fd)
{
    uint8_t buf[8];
    offset_t pos;
    int i;

    lseek(fd, 8, SEEK_SET);
    read(fd, buf, 8);
    pos = 0;
    for(i=0;i<8;i++)
        pos |= (int64_t)buf[i] << (56 - i * 8);
    return pos;
}

void ffm_write_write_index(int fd, offset_t pos)
{
    uint8_t buf[8];
    int i;

    for(i=0;i<8;i++)
        buf[i] = (pos >> (56 - i * 8)) & 0xff;
    lseek(fd, 8, SEEK_SET);
    write(fd, buf, 8);
}

void ffm_set_write_index(AVFormatContext *s, offset_t pos, offset_t file_size)
{
    FFMContext *ffm = s->priv_data;
    ffm->write_index = pos;
    ffm->file_size = file_size;
}
#endif // CONFIG_FFSERVER

static int ffm_read_close(AVFormatContext *s)
{
    AVStream *st;
    int i;

    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        av_freep(&st->priv_data);
    }
    return 0;
}

static int ffm_probe(AVProbeData *p)
{
    if (p->buf_size >= 4 &&
        p->buf[0] == 'F' && p->buf[1] == 'F' && p->buf[2] == 'M' &&
        p->buf[3] == '1')
        return AVPROBE_SCORE_MAX + 1;
    return 0;
}

#ifdef CONFIG_FFM_DEMUXER
AVInputFormat ffm_demuxer = {
    "ffm",
    "ffm format",
    sizeof(FFMContext),
    ffm_probe,
    ffm_read_header,
    ffm_read_packet,
    ffm_read_close,
    ffm_seek,
};
#endif
#ifdef CONFIG_FFM_MUXER
AVOutputFormat ffm_muxer = {
    "ffm",
    "ffm format",
    "",
    "ffm",
    sizeof(FFMContext),
    /* not really used */
    CODEC_ID_MP2,
    CODEC_ID_MPEG1VIDEO,
    ffm_write_header,
    ffm_write_packet,
    ffm_write_trailer,
};
#endif //CONFIG_FFM_MUXER
