/*
 * MXF muxer
 * Copyright (c) 2008 GUCAS, Zhentan Feng <spyfeng at gmail dot com>
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

/*
 * References
 * SMPTE 336M KLV Data Encoding Protocol Using Key-Length-Value
 * SMPTE 377M MXF File Format Specifications
 * SMPTE 379M MXF Generic Container
 * SMPTE 381M Mapping MPEG Streams into the MXF Generic Container
 * SMPTE RP210: SMPTE Metadata Dictionary
 * SMPTE RP224: Registry of SMPTE Universal Labels
 */

//#define DEBUG

static const MXFCodecUL *mxf_get_essence_container_ul(enum CodecID type)
{
    const MXFCodecUL *uls = mxf_essence_container_uls;
    while (uls->id != CODEC_ID_NONE) {
        if (uls->id == type)
            break;
        uls++;
    }
    return uls;
}

static void mxf_free(AVFormatContext *s)
{
    MXFContext *mxf = s->priv_data;
    AVStream *st;
    int i;

    av_freep(&mxf->reference.identification);
    av_freep(mxf->reference.package);
    av_freep(&mxf->reference.package);
    av_freep(&mxf->reference.content_storage);
    for (i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        av_freep(&st->priv_data);
    }
    av_freep(mxf->reference.sub_desc);
    av_freep(&mxf->reference.sub_desc);
    av_freep(&mxf->reference.mul_desc);
    av_freep(&mxf->essence_container_uls);
}

static const MXFDataDefinitionUL *mxf_get_data_definition_ul(enum CodecType type)
{
    const MXFDataDefinitionUL *uls = mxf_data_definition_uls;
    while (uls->type != CODEC_TYPE_DATA) {
        if (type == uls->type)
            break;
        uls ++;
    }
    return uls;
}

static int mux_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    AVStream *st = s->streams[pkt->stream_index];
    MXFStreamContext *sc = st->priv_data;

    put_buffer(pb, sc->track_essence_element_key, 16); // write key
    klv_encode_ber_length(pb, pkt->size); // write length
    put_buffer(pb, pkt->data, pkt->size); // write value

    put_flush_packet(pb);
    return 0;
}

AVOutputFormat mxf_muxer = {
    "mxf",
    NULL_IF_CONFIG_SMALL("Material eXchange Format"),
    NULL,
    "mxf",
    sizeof(MXFContext),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_MPEG2VIDEO,
    mux_write_header,
    mux_write_packet,
    mux_write_footer,
};
