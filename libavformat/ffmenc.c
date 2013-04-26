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
#include "libavutil/intfloat.h"
#include "libavutil/avassert.h"
#include "libavutil/parseutils.h"
#include "avformat.h"
#include "internal.h"
#include "ffm.h"

static void flush_packet(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    int fill_size, h;
    AVIOContext *pb = s->pb;

    fill_size = ffm->packet_end - ffm->packet_ptr;
    memset(ffm->packet_ptr, 0, fill_size);

    av_assert1(avio_tell(pb) % ffm->packet_size == 0);

    /* put header */
    avio_wb16(pb, PACKET_ID);
    avio_wb16(pb, fill_size);
    avio_wb64(pb, ffm->dts);
    h = ffm->frame_offset;
    if (ffm->first_packet)
        h |= 0x8000;
    avio_wb16(pb, h);
    avio_write(pb, ffm->packet, ffm->packet_end - ffm->packet);
    avio_flush(pb);

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

static void write_header_chunk(AVIOContext *pb, AVIOContext *dpb, unsigned id)
{
    uint8_t *dyn_buf;
    int dyn_size= avio_close_dyn_buf(dpb, &dyn_buf);
    avio_wb32(pb, id);
    avio_wb32(pb, dyn_size);
    avio_write(pb, dyn_buf, dyn_size);
    av_free(dyn_buf);
}

static int ffm_write_header(AVFormatContext *s)
{
    FFMContext *ffm = s->priv_data;
    AVDictionaryEntry *t;
    AVStream *st;
    AVIOContext *pb = s->pb;
    AVCodecContext *codec;
    int bit_rate, i;

    if (t = av_dict_get(s->metadata, "creation_time", NULL, 0)) {
        int ret = av_parse_time(&ffm->start_time, t->value, 0);
        if (ret < 0)
            return ret;
    }

    ffm->packet_size = FFM_PACKET_SIZE;

    /* header */
    avio_wl32(pb, MKTAG('F', 'F', 'M', '2'));
    avio_wb32(pb, ffm->packet_size);
    avio_wb64(pb, 0); /* current write position */

    if(avio_open_dyn_buf(&pb) < 0)
        return AVERROR(ENOMEM);

    avio_wb32(pb, s->nb_streams);
    bit_rate = 0;
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        bit_rate += st->codec->bit_rate;
    }
    avio_wb32(pb, bit_rate);

    write_header_chunk(s->pb, pb, MKBETAG('M', 'A', 'I', 'N'));

    /* list of streams */
    for(i=0;i<s->nb_streams;i++) {
        st = s->streams[i];
        avpriv_set_pts_info(st, 64, 1, 1000000);
        if(avio_open_dyn_buf(&pb) < 0)
            return AVERROR(ENOMEM);

        codec = st->codec;
        /* generic info */
        avio_wb32(pb, codec->codec_id);
        avio_w8(pb, codec->codec_type);
        avio_wb32(pb, codec->bit_rate);
        avio_wb32(pb, codec->flags);
        avio_wb32(pb, codec->flags2);
        avio_wb32(pb, codec->debug);
        if (codec->flags & CODEC_FLAG_GLOBAL_HEADER) {
            avio_wb32(pb, codec->extradata_size);
            avio_write(pb, codec->extradata, codec->extradata_size);
        }
        write_header_chunk(s->pb, pb, MKBETAG('C', 'O', 'M', 'M'));
        if(avio_open_dyn_buf(&pb) < 0)
            return AVERROR(ENOMEM);
        /* specific info */
        switch(codec->codec_type) {
        case AVMEDIA_TYPE_VIDEO:
            avio_wb32(pb, codec->time_base.num);
            avio_wb32(pb, codec->time_base.den);
            avio_wb16(pb, codec->width);
            avio_wb16(pb, codec->height);
            avio_wb16(pb, codec->gop_size);
            avio_wb32(pb, codec->pix_fmt);
            avio_w8(pb, codec->qmin);
            avio_w8(pb, codec->qmax);
            avio_w8(pb, codec->max_qdiff);
            avio_wb16(pb, (int) (codec->qcompress * 10000.0));
            avio_wb16(pb, (int) (codec->qblur * 10000.0));
            avio_wb32(pb, codec->bit_rate_tolerance);
            avio_put_str(pb, codec->rc_eq ? codec->rc_eq : "tex^qComp");
            avio_wb32(pb, codec->rc_max_rate);
            avio_wb32(pb, codec->rc_min_rate);
            avio_wb32(pb, codec->rc_buffer_size);
            avio_wb64(pb, av_double2int(codec->i_quant_factor));
            avio_wb64(pb, av_double2int(codec->b_quant_factor));
            avio_wb64(pb, av_double2int(codec->i_quant_offset));
            avio_wb64(pb, av_double2int(codec->b_quant_offset));
            avio_wb32(pb, codec->dct_algo);
            avio_wb32(pb, codec->strict_std_compliance);
            avio_wb32(pb, codec->max_b_frames);
            avio_wb32(pb, codec->mpeg_quant);
            avio_wb32(pb, codec->intra_dc_precision);
            avio_wb32(pb, codec->me_method);
            avio_wb32(pb, codec->mb_decision);
            avio_wb32(pb, codec->nsse_weight);
            avio_wb32(pb, codec->frame_skip_cmp);
            avio_wb64(pb, av_double2int(codec->rc_buffer_aggressivity));
            avio_wb32(pb, codec->codec_tag);
            avio_w8(pb, codec->thread_count);
            avio_wb32(pb, codec->coder_type);
            avio_wb32(pb, codec->me_cmp);
            avio_wb32(pb, codec->me_subpel_quality);
            avio_wb32(pb, codec->me_range);
            avio_wb32(pb, codec->keyint_min);
            avio_wb32(pb, codec->scenechange_threshold);
            avio_wb32(pb, codec->b_frame_strategy);
            avio_wb64(pb, av_double2int(codec->qcompress));
            avio_wb64(pb, av_double2int(codec->qblur));
            avio_wb32(pb, codec->max_qdiff);
            avio_wb32(pb, codec->refs);
            write_header_chunk(s->pb, pb, MKBETAG('S', 'T', 'V', 'I'));
            break;
        case AVMEDIA_TYPE_AUDIO:
            avio_wb32(pb, codec->sample_rate);
            avio_wl16(pb, codec->channels);
            avio_wl16(pb, codec->frame_size);
            write_header_chunk(s->pb, pb, MKBETAG('S', 'T', 'A', 'U'));
            break;
        default:
            return -1;
        }
    }
    pb = s->pb;

    avio_wb64(pb, 0); // end of header

    /* flush until end of block reached */
    while ((avio_tell(pb) % ffm->packet_size) != 0)
        avio_w8(pb, 0);

    avio_flush(pb);

    /* init packet mux */
    ffm->packet_ptr = ffm->packet;
    ffm->packet_end = ffm->packet + ffm->packet_size - FFM_HEADER_SIZE;
    av_assert0(ffm->packet_end >= ffm->packet);
    ffm->frame_offset = 0;
    ffm->dts = 0;
    ffm->first_packet = 1;

    return 0;
}

static int ffm_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    FFMContext *ffm = s->priv_data;
    int64_t dts;
    uint8_t header[FRAME_HEADER_SIZE+4];
    int header_size = FRAME_HEADER_SIZE;

    dts = ffm->start_time + pkt->dts;
    /* packet size & key_frame */
    header[0] = pkt->stream_index;
    header[1] = 0;
    if (pkt->flags & AV_PKT_FLAG_KEY)
        header[1] |= FLAG_KEY_FRAME;
    AV_WB24(header+2, pkt->size);
    AV_WB24(header+5, pkt->duration);
    AV_WB64(header+8, ffm->start_time + pkt->pts);
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
    FFMContext *ffm = s->priv_data;

    /* flush packets */
    if (ffm->packet_ptr > ffm->packet)
        flush_packet(s);

    return 0;
}

AVOutputFormat ff_ffm_muxer = {
    .name              = "ffm",
    .long_name         = NULL_IF_CONFIG_SMALL("FFM (FFserver live feed)"),
    .extensions        = "ffm",
    .priv_data_size    = sizeof(FFMContext),
    .audio_codec       = AV_CODEC_ID_MP2,
    .video_codec       = AV_CODEC_ID_MPEG1VIDEO,
    .write_header      = ffm_write_header,
    .write_packet      = ffm_write_packet,
    .write_trailer     = ffm_write_trailer,
    .flags             = AVFMT_TS_NEGATIVE,
};
