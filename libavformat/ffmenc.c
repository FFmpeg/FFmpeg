/*
 * FFM (ffserver live feed) muxer
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

static void flush_packet(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    int fill_size, h;
    ByteIOContext *pb = s->pb;

    fill_size = ffm->packet_end - ffm->packet_ptr;
    memset(ffm->packet_ptr, 0, fill_size);

    if (url_ftell(pb) % ffm->packet_size)
        av_abort();

    /* put header */
    put_be16(pb, PACKET_ID);
    put_be16(pb, fill_size);
    put_be64(pb, ffm->dts);
    h = ffm->frame_offset;
    if (ffm->first_packet)
        h |= 0x8000;
    put_be16(pb, h);
    put_buffer(pb, ffm->packet, ffm->packet_end - ffm->packet);
    put_flush_packet(pb);

    /* prepare next packet */
    ffm->frame_offset = 0; /* no key frame */
    ffm->packet_ptr = ffm->packet;
    ffm->first_packet = 0;
}

/* 'first' is true if first data of a frame */
static void ffm_write_data(AVFormatContext *s,
                           const uint8_t *buf, int size,
                           int64_t dts, int header)
{
    FFMContext *ffm = s->priv_data;
    int len;

    if (header && ffm->frame_offset == 0) {
        ffm->frame_offset = ffm->packet_ptr - ffm->packet + FFM_HEADER_SIZE;
        ffm->dts = dts;
    }

    /* write as many packets as needed */
    while (size > 0) {
        len = ffm->packet_end - ffm->packet_ptr;
        if (len > size)
            len = size;
        memcpy(ffm->packet_ptr, buf, len);

        ffm->packet_ptr += len;
        buf += len;
        size -= len;
        if (ffm->packet_ptr >= ffm->packet_end)
            flush_packet(s);
    }
}

static int ffm_write_header(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    AVStream *st;
    ByteIOContext *pb = s->pb;
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
        av_set_pts_info(st, 64, 1, 1000000);

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
            put_strz(pb, codec->rc_eq ? codec->rc_eq : "tex^qComp");
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
            put_byte(pb, codec->thread_count);
            break;
        case CODEC_TYPE_AUDIO:
            put_be32(pb, codec->sample_rate);
            put_le16(pb, codec->channels);
            put_le16(pb, codec->frame_size);
            break;
        default:
            return -1;
        }
        if (codec->flags & CODEC_FLAG_GLOBAL_HEADER) {
            put_be32(pb, codec->extradata_size);
            put_buffer(pb, codec->extradata, codec->extradata_size);
        }
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
    ffm->dts = 0;
    ffm->first_packet = 1;

    return 0;
}

static int ffm_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    int64_t dts;
    uint8_t header[FRAME_HEADER_SIZE+4];
    int header_size = FRAME_HEADER_SIZE;

    dts = s->timestamp + pkt->dts;
    /* packet size & key_frame */
    header[0] = pkt->stream_index;
    header[1] = 0;
    if (pkt->flags & PKT_FLAG_KEY)
        header[1] |= FLAG_KEY_FRAME;
    AV_WB24(header+2, pkt->size);
    AV_WB24(header+5, pkt->duration);
    AV_WB64(header+8, s->timestamp + pkt->pts);
    if (pkt->pts != pkt->dts) {
        header[1] |= FLAG_DTS;
        AV_WB32(header+16, pkt->pts - pkt->dts);
        header_size += 4;
    }
    ffm_write_data(s, header, header_size, dts, 1);
    ffm_write_data(s, pkt->data, pkt->size, dts, 0);

    return 0;
}

static int ffm_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
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

AVOutputFormat ffm_muxer = {
    "ffm",
    NULL_IF_CONFIG_SMALL("FFM (FFserver live feed) format"),
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
