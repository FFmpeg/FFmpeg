/*
 * Ogg muxer
 * Copyright (c) 2007 Baptiste Coudurier <baptiste dot coudurier at free dot fr>
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
#include "libavcodec/xiph.h"
#include "libavcodec/bytestream.h"
#include "libavcodec/flac.h"
#include "avformat.h"
#include "internal.h"

typedef struct {
    int64_t duration;
    unsigned page_counter;
    uint8_t *header[3];
    int header_len[3];
    /** for theora granule */
    int kfgshift;
    int64_t last_kf_pts;
    int vrev;
    int eos;
} OGGStreamContext;

static void ogg_update_checksum(AVFormatContext *s, int64_t crc_offset)
{
    int64_t pos = url_ftell(s->pb);
    uint32_t checksum = get_checksum(s->pb);
    url_fseek(s->pb, crc_offset, SEEK_SET);
    put_be32(s->pb, checksum);
    url_fseek(s->pb, pos, SEEK_SET);
}

static int ogg_write_page(AVFormatContext *s, const uint8_t *data, int size,
                          int64_t granule, int stream_index, int flags)
{
    OGGStreamContext *oggstream = s->streams[stream_index]->priv_data;
    int64_t crc_offset;
    int page_segments, i;

    if (size >= 255*255) {
        granule = -1;
        size = 255*255;
    } else if (oggstream->eos)
        flags |= 4;

    page_segments = FFMIN((size/255)+!!size, 255);

    init_checksum(s->pb, ff_crc04C11DB7_update, 0);
    put_tag(s->pb, "OggS");
    put_byte(s->pb, 0);
    put_byte(s->pb, flags);
    put_le64(s->pb, granule);
    put_le32(s->pb, stream_index);
    put_le32(s->pb, oggstream->page_counter++);
    crc_offset = url_ftell(s->pb);
    put_le32(s->pb, 0); // crc
    put_byte(s->pb, page_segments);
    for (i = 0; i < page_segments-1; i++)
        put_byte(s->pb, 255);
    if (size) {
        put_byte(s->pb, size - (page_segments-1)*255);
        put_buffer(s->pb, data, size);
    }
    ogg_update_checksum(s, crc_offset);
    put_flush_packet(s->pb);
    return size;
}

static int ogg_build_flac_headers(AVCodecContext *avctx,
                                  OGGStreamContext *oggstream, int bitexact)
{
    const char *vendor = bitexact ? "ffmpeg" : LIBAVFORMAT_IDENT;
    enum FLACExtradataFormat format;
    uint8_t *streaminfo;
    uint8_t *p;
    if (!ff_flac_is_extradata_valid(avctx, &format, &streaminfo))
        return -1;
    oggstream->header_len[0] = 51;
    oggstream->header[0] = av_mallocz(51); // per ogg flac specs
    p = oggstream->header[0];
    bytestream_put_byte(&p, 0x7F);
    bytestream_put_buffer(&p, "FLAC", 4);
    bytestream_put_byte(&p, 1); // major version
    bytestream_put_byte(&p, 0); // minor version
    bytestream_put_be16(&p, 1); // headers packets without this one
    bytestream_put_buffer(&p, "fLaC", 4);
    bytestream_put_byte(&p, 0x00); // streaminfo
    bytestream_put_be24(&p, 34);
    bytestream_put_buffer(&p, streaminfo, FLAC_STREAMINFO_SIZE);
    oggstream->header_len[1] = 1+3+4+strlen(vendor)+4;
    oggstream->header[1] = av_mallocz(oggstream->header_len[1]);
    p = oggstream->header[1];
    bytestream_put_byte(&p, 0x84); // last metadata block and vorbis comment
    bytestream_put_be24(&p, oggstream->header_len[1] - 4);
    bytestream_put_le32(&p, strlen(vendor));
    bytestream_put_buffer(&p, vendor, strlen(vendor));
    bytestream_put_le32(&p, 0); // user comment list length
    return 0;
}

static int ogg_write_header(AVFormatContext *s)
{
    OGGStreamContext *oggstream;
    int i, j;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        if (st->codec->codec_type == CODEC_TYPE_AUDIO)
            av_set_pts_info(st, 64, 1, st->codec->sample_rate);
        else if (st->codec->codec_type == CODEC_TYPE_VIDEO)
            av_set_pts_info(st, 64, st->codec->time_base.num, st->codec->time_base.den);
        if (st->codec->codec_id != CODEC_ID_VORBIS &&
            st->codec->codec_id != CODEC_ID_THEORA &&
            st->codec->codec_id != CODEC_ID_FLAC) {
            av_log(s, AV_LOG_ERROR, "Unsupported codec id in stream %d\n", i);
            return -1;
        }

        if (!st->codec->extradata || !st->codec->extradata_size) {
            av_log(s, AV_LOG_ERROR, "No extradata present\n");
            return -1;
        }
        oggstream = av_mallocz(sizeof(*oggstream));
        st->priv_data = oggstream;
        if (st->codec->codec_id == CODEC_ID_FLAC) {
            if (ogg_build_flac_headers(st->codec,
                                       oggstream, st->codec->flags & CODEC_FLAG_BITEXACT) < 0) {
                av_log(s, AV_LOG_ERROR, "Extradata corrupted\n");
                av_freep(&st->priv_data);
            }
        } else {
            if (ff_split_xiph_headers(st->codec->extradata, st->codec->extradata_size,
                                      st->codec->codec_id == CODEC_ID_VORBIS ? 30 : 42,
                                      oggstream->header, oggstream->header_len) < 0) {
                av_log(s, AV_LOG_ERROR, "Extradata corrupted\n");
                av_freep(&st->priv_data);
                return -1;
            }
            if (st->codec->codec_id == CODEC_ID_THEORA) {
                /** KFGSHIFT is the width of the less significant section of the granule position
                    The less significant section is the frame count since the last keyframe */
                oggstream->kfgshift = ((oggstream->header[0][40]&3)<<3)|(oggstream->header[0][41]>>5);
                oggstream->vrev = oggstream->header[0][9];
                av_log(s, AV_LOG_DEBUG, "theora kfgshift %d, vrev %d\n",
                       oggstream->kfgshift, oggstream->vrev);
            }
        }
    }
    for (i = 0; i < 3; i++) {
        for (j = 0; j < s->nb_streams; j++) {
            AVStream *st = s->streams[j];
            OGGStreamContext *oggstream = st->priv_data;
            if (oggstream && oggstream->header_len[i]) {
                ogg_write_page(s, oggstream->header[i], oggstream->header_len[i],
                               0, st->index, i ? 0 : 2); // bos
            }
        }
    }
    return 0;
}

static int ogg_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVStream *st = s->streams[pkt->stream_index];
    OGGStreamContext *oggstream = st->priv_data;
    uint8_t *ptr = pkt->data;
    int ret, size = pkt->size;
    int64_t granule;

    if (st->codec->codec_id == CODEC_ID_THEORA) {
        int64_t pts = oggstream->vrev < 1 ? pkt->pts : pkt->pts + pkt->duration;
        int pframe_count;
        if (pkt->flags & PKT_FLAG_KEY)
            oggstream->last_kf_pts = pts;
        pframe_count = pts - oggstream->last_kf_pts;
        // prevent frame count from overflow if key frame flag is not set
        if (pframe_count >= (1<<oggstream->kfgshift)) {
            oggstream->last_kf_pts += pframe_count;
            pframe_count = 0;
        }
        granule = (oggstream->last_kf_pts<<oggstream->kfgshift) | pframe_count;
    } else
        granule = pkt->pts + pkt->duration;
    oggstream->duration = granule;
    do {
        ret = ogg_write_page(s, ptr, size, granule, pkt->stream_index, ptr != pkt->data);
        ptr  += ret; size -= ret;
    } while (size > 0 || ret == 255*255); // need to output a last nil page

    return 0;
}

static int ogg_compare_granule(AVFormatContext *s, AVPacket *next, AVPacket *pkt)
{
    AVStream *st2 = s->streams[next->stream_index];
    AVStream *st  = s->streams[pkt ->stream_index];

    int64_t next_granule = av_rescale_q(next->pts + next->duration,
                                        st2->time_base, AV_TIME_BASE_Q);
    int64_t cur_granule  = av_rescale_q(pkt ->pts + pkt ->duration,
                                        st ->time_base, AV_TIME_BASE_Q);
    return next_granule > cur_granule;
}

static int ogg_interleave_per_granule(AVFormatContext *s, AVPacket *out, AVPacket *pkt, int flush)
{
    AVPacketList *pktl;
    int stream_count = 0;
    int streams[MAX_STREAMS] = {0};
    int interleaved = 0;

    if (pkt) {
        ff_interleave_add_packet(s, pkt, ogg_compare_granule);
    }

    pktl = s->packet_buffer;
    while (pktl) {
        if (streams[pktl->pkt.stream_index] == 0)
            stream_count++;
        streams[pktl->pkt.stream_index]++;
        // need to buffer at least one packet to set eos flag
        if (streams[pktl->pkt.stream_index] == 2)
            interleaved++;
        pktl = pktl->next;
    }

    if ((s->nb_streams == stream_count && interleaved == stream_count) ||
        (flush && stream_count)) {
        pktl= s->packet_buffer;
        *out= pktl->pkt;
        s->packet_buffer = pktl->next;
        if (flush && streams[out->stream_index] == 1) {
            OGGStreamContext *ogg = s->streams[out->stream_index]->priv_data;
            ogg->eos = 1;
        }
        av_freep(&pktl);
        return 1;
    } else {
        av_init_packet(out);
        return 0;
    }
}

static int ogg_write_trailer(AVFormatContext *s)
{
    int i;
    for (i = 0; i < s->nb_streams; i++) {
        AVStream *st = s->streams[i];
        OGGStreamContext *oggstream = st->priv_data;
        if (st->codec->codec_id == CODEC_ID_FLAC) {
            av_free(oggstream->header[0]);
            av_free(oggstream->header[1]);
        }
        av_freep(&st->priv_data);
    }
    return 0;
}

AVOutputFormat ogg_muxer = {
    "ogg",
    NULL_IF_CONFIG_SMALL("Ogg"),
    "application/ogg",
    "ogg,ogv",
    0,
    CODEC_ID_FLAC,
    CODEC_ID_THEORA,
    ogg_write_header,
    ogg_write_packet,
    ogg_write_trailer,
    .interleave_packet = ogg_interleave_per_granule,
};
