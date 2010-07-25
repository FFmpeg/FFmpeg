/*
 * FFM (ffserver live feed) demuxer
 * Copyright (c) 2001 Fabrice Bellard
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

#include "libavutil/intreadwrite.h"
#include "avformat.h"
#include "ffm.h"
#if CONFIG_FFSERVER
#include <unistd.h>

int64_t ffm_read_write_index(int fd)
{
    uint8_t buf[8];

    lseek(fd, 8, SEEK_SET);
    if (read(fd, buf, 8) != 8)
        return AVERROR(EIO);
    return AV_RB64(buf);
}

int ffm_write_write_index(int fd, int64_t pos)
{
    uint8_t buf[8];
    int i;

    for(i=0;i<8;i++)
        buf[i] = (pos >> (56 - i * 8)) & 0xff;
    lseek(fd, 8, SEEK_SET);
    if (write(fd, buf, 8) != 8)
        return AVERROR(EIO);
    return 8;
}

void ffm_set_write_index(AVFormatContext *s, int64_t pos, int64_t file_size)
{
    FFMContext *ffm = s->priv_data;
    ffm->write_index = pos;
    ffm->file_size = file_size;
}
#endif // CONFIG_FFSERVER

static int ffm_is_avail_data(AVFormatContext *s, int size)
{
    FFMContext *ffm = s->priv_data;
    int64_t pos, avail_size;
    int len;

    len = ffm->packet_end - ffm->packet_ptr;
    if (size <= len)
        return 1;
    pos = url_ftell(s->pb);
    if (!ffm->write_index) {
        if (pos == ffm->file_size)
            return AVERROR_EOF;
        avail_size = ffm->file_size - pos;
    } else {
    if (pos == ffm->write_index) {
        /* exactly at the end of stream */
        return AVERROR(EAGAIN);
    } else if (pos < ffm->write_index) {
        avail_size = ffm->write_index - pos;
    } else {
        avail_size = (ffm->file_size - pos) + (ffm->write_index - FFM_PACKET_SIZE);
    }
    }
    avail_size = (avail_size / ffm->packet_size) * (ffm->packet_size - FFM_HEADER_SIZE) + len;
    if (size <= avail_size)
        return 1;
    else
        return AVERROR(EAGAIN);
}

static int ffm_resync(AVFormatContext *s, int state)
{
    av_log(s, AV_LOG_ERROR, "resyncing\n");
    while (state != PACKET_ID) {
        if (url_feof(s->pb)) {
            av_log(s, AV_LOG_ERROR, "cannot find FFM syncword\n");
            return -1;
        }
        state = (state << 8) | get_byte(s->pb);
    }
    return 0;
}

/* first is true if we read the frame header */
static int ffm_read_data(AVFormatContext *s,
                         uint8_t *buf, int size, int header)
{
    FFMContext *ffm = s->priv_data;
    ByteIOContext *pb = s->pb;
    int len, fill_size, size1, frame_offset, id;

    size1 = size;
    while (size > 0) {
    redo:
        len = ffm->packet_end - ffm->packet_ptr;
        if (len < 0)
            return -1;
        if (len > size)
            len = size;
        if (len == 0) {
            if (url_ftell(pb) == ffm->file_size)
                url_fseek(pb, ffm->packet_size, SEEK_SET);
    retry_read:
            id = get_be16(pb); /* PACKET_ID */
            if (id != PACKET_ID)
                if (ffm_resync(s, id) < 0)
                    return -1;
            fill_size = get_be16(pb);
            ffm->dts = get_be64(pb);
            frame_offset = get_be16(pb);
            get_buffer(pb, ffm->packet, ffm->packet_size - FFM_HEADER_SIZE);
            ffm->packet_end = ffm->packet + (ffm->packet_size - FFM_HEADER_SIZE - fill_size);
            if (ffm->packet_end < ffm->packet || frame_offset < 0)
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
                if ((frame_offset & 0x7fff) < FFM_HEADER_SIZE)
                    return -1;
                ffm->packet_ptr = ffm->packet + (frame_offset & 0x7fff) - FFM_HEADER_SIZE;
                if (!header)
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
        header = 0;
    }
    return size1 - size;
}

//#define DEBUG_SEEK

/* ensure that acutal seeking happens between FFM_PACKET_SIZE
   and file_size - FFM_PACKET_SIZE */
static void ffm_seek1(AVFormatContext *s, int64_t pos1)
{
    FFMContext *ffm = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t pos;

    pos = FFMIN(pos1, ffm->file_size - FFM_PACKET_SIZE);
    pos = FFMAX(pos, FFM_PACKET_SIZE);
#ifdef DEBUG_SEEK
    av_log(s, AV_LOG_DEBUG, "seek to %"PRIx64" -> %"PRIx64"\n", pos1, pos);
#endif
    url_fseek(pb, pos, SEEK_SET);
}

static int64_t get_dts(AVFormatContext *s, int64_t pos)
{
    ByteIOContext *pb = s->pb;
    int64_t dts;

    ffm_seek1(s, pos);
    url_fskip(pb, 4);
    dts = get_be64(pb);
#ifdef DEBUG_SEEK
    av_log(s, AV_LOG_DEBUG, "dts=%0.6f\n", dts / 1000000.0);
#endif
    return dts;
}

static void adjust_write_index(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    ByteIOContext *pb = s->pb;
    int64_t pts;
    //int64_t orig_write_index = ffm->write_index;
    int64_t pos_min, pos_max;
    int64_t pts_start;
    int64_t ptr = url_ftell(pb);


    pos_min = 0;
    pos_max = ffm->file_size - 2 * FFM_PACKET_SIZE;

    pts_start = get_dts(s, pos_min);

    pts = get_dts(s, pos_max);

    if (pts - 100000 > pts_start)
        goto end;

    ffm->write_index = FFM_PACKET_SIZE;

    pts_start = get_dts(s, pos_min);

    pts = get_dts(s, pos_max);

    if (pts - 100000 <= pts_start) {
        while (1) {
            int64_t newpos;
            int64_t newpts;

            newpos = ((pos_max + pos_min) / (2 * FFM_PACKET_SIZE)) * FFM_PACKET_SIZE;

            if (newpos == pos_min)
                break;

            newpts = get_dts(s, newpos);

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
    //printf("pts range %0.6f - %0.6f\n", get_dts(s, 0) / 1000000. , get_dts(s, ffm->file_size - 2 * FFM_PACKET_SIZE) / 1000000. );

 end:
    url_fseek(pb, ptr, SEEK_SET);
}


static int ffm_close(AVFormatContext *s)
{
    int i;

    for (i = 0; i < s->nb_streams; i++)
        av_freep(&s->streams[i]->codec->rc_eq);

    return 0;
}


static int ffm_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    FFMContext *ffm = s->priv_data;
    AVStream *st;
    ByteIOContext *pb = s->pb;
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
        if (ffm->write_index)
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

        av_set_pts_info(st, 64, 1, 1000000);

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
        case AVMEDIA_TYPE_VIDEO:
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
            codec->thread_count = get_byte(pb);
            codec->coder_type = get_be32(pb);
            codec->me_cmp = get_be32(pb);
            codec->partitions = get_be32(pb);
            codec->me_subpel_quality = get_be32(pb);
            codec->me_range = get_be32(pb);
            codec->keyint_min = get_be32(pb);
            codec->scenechange_threshold = get_be32(pb);
            codec->b_frame_strategy = get_be32(pb);
            codec->qcompress = av_int2dbl(get_be64(pb));
            codec->qblur = av_int2dbl(get_be64(pb));
            codec->max_qdiff = get_be32(pb);
            codec->refs = get_be32(pb);
            codec->directpred = get_be32(pb);
            break;
        case AVMEDIA_TYPE_AUDIO:
            codec->sample_rate = get_be32(pb);
            codec->channels = get_le16(pb);
            codec->frame_size = get_le16(pb);
            codec->sample_fmt = (int16_t) get_le16(pb);
            break;
        default:
            goto fail;
        }
        if (codec->flags & CODEC_FLAG_GLOBAL_HEADER) {
            codec->extradata_size = get_be32(pb);
            codec->extradata = av_malloc(codec->extradata_size);
            if (!codec->extradata)
                return AVERROR(ENOMEM);
            get_buffer(pb, codec->extradata, codec->extradata_size);
        }
    }

    /* get until end of block reached */
    while ((url_ftell(pb) % ffm->packet_size) != 0)
        get_byte(pb);

    /* init packet demux */
    ffm->packet_ptr = ffm->packet;
    ffm->packet_end = ffm->packet;
    ffm->frame_offset = 0;
    ffm->dts = 0;
    ffm->read_state = READ_HEADER;
    ffm->first_packet = 1;
    return 0;
 fail:
    ffm_close(s);
    return -1;
}

/* return < 0 if eof */
static int ffm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    int size;
    FFMContext *ffm = s->priv_data;
    int duration, ret;

    switch(ffm->read_state) {
    case READ_HEADER:
        if ((ret = ffm_is_avail_data(s, FRAME_HEADER_SIZE+4)) < 0)
            return ret;

        dprintf(s, "pos=%08"PRIx64" spos=%"PRIx64", write_index=%"PRIx64" size=%"PRIx64"\n",
               url_ftell(s->pb), s->pb->pos, ffm->write_index, ffm->file_size);
        if (ffm_read_data(s, ffm->header, FRAME_HEADER_SIZE, 1) !=
            FRAME_HEADER_SIZE)
            return -1;
        if (ffm->header[1] & FLAG_DTS)
            if (ffm_read_data(s, ffm->header+16, 4, 1) != 4)
                return -1;
#if 0
        av_hexdump_log(s, AV_LOG_DEBUG, ffm->header, FRAME_HEADER_SIZE);
#endif
        ffm->read_state = READ_DATA;
        /* fall thru */
    case READ_DATA:
        size = AV_RB24(ffm->header + 2);
        if ((ret = ffm_is_avail_data(s, size)) < 0)
            return ret;

        duration = AV_RB24(ffm->header + 5);

        av_new_packet(pkt, size);
        pkt->stream_index = ffm->header[0];
        if ((unsigned)pkt->stream_index >= s->nb_streams) {
            av_log(s, AV_LOG_ERROR, "invalid stream index %d\n", pkt->stream_index);
            av_free_packet(pkt);
            ffm->read_state = READ_HEADER;
            return -1;
        }
        pkt->pos = url_ftell(s->pb);
        if (ffm->header[1] & FLAG_KEY_FRAME)
            pkt->flags |= AV_PKT_FLAG_KEY;

        ffm->read_state = READ_HEADER;
        if (ffm_read_data(s, pkt->data, size, 0) != size) {
            /* bad case: desynchronized packet. we cancel all the packet loading */
            av_free_packet(pkt);
            return -1;
        }
        pkt->pts = AV_RB64(ffm->header+8);
        if (ffm->header[1] & FLAG_DTS)
            pkt->dts = pkt->pts - AV_RB32(ffm->header+16);
        else
            pkt->dts = pkt->pts;
        pkt->duration = duration;
        break;
    }
    return 0;
}

/* seek to a given time in the file. The file read pointer is
   positioned at or before pts. XXX: the following code is quite
   approximative */
static int ffm_seek(AVFormatContext *s, int stream_index, int64_t wanted_pts, int flags)
{
    FFMContext *ffm = s->priv_data;
    int64_t pos_min, pos_max, pos;
    int64_t pts_min, pts_max, pts;
    double pos1;

#ifdef DEBUG_SEEK
    av_log(s, AV_LOG_DEBUG, "wanted_pts=%0.6f\n", wanted_pts / 1000000.0);
#endif
    /* find the position using linear interpolation (better than
       dichotomy in typical cases) */
    pos_min = FFM_PACKET_SIZE;
    pos_max = ffm->file_size - FFM_PACKET_SIZE;
    while (pos_min <= pos_max) {
        pts_min = get_dts(s, pos_min);
        pts_max = get_dts(s, pos_max);
        /* linear interpolation */
        pos1 = (double)(pos_max - pos_min) * (double)(wanted_pts - pts_min) /
            (double)(pts_max - pts_min);
        pos = (((int64_t)pos1) / FFM_PACKET_SIZE) * FFM_PACKET_SIZE;
        if (pos <= pos_min)
            pos = pos_min;
        else if (pos >= pos_max)
            pos = pos_max;
        pts = get_dts(s, pos);
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

 found:
    ffm_seek1(s, pos);

    /* reset read state */
    ffm->read_state = READ_HEADER;
    ffm->packet_ptr = ffm->packet;
    ffm->packet_end = ffm->packet;
    ffm->first_packet = 1;

    return 0;
}

static int ffm_probe(AVProbeData *p)
{
    if (
        p->buf[0] == 'F' && p->buf[1] == 'F' && p->buf[2] == 'M' &&
        p->buf[3] == '1')
        return AVPROBE_SCORE_MAX + 1;
    return 0;
}

AVInputFormat ffm_demuxer = {
    "ffm",
    NULL_IF_CONFIG_SMALL("FFM (FFserver live feed) format"),
    sizeof(FFMContext),
    ffm_probe,
    ffm_read_header,
    ffm_read_packet,
    ffm_close,
    ffm_seek,
};
