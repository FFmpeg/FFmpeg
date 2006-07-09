/*
 * Creative Voice File demuxer.
 * Copyright (c) 2006  Aurelien Jacobs <aurel@gnuage.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "avformat.h"
#include "avi.h"    /* for CodecTag */
#include "voc.h"


typedef enum voc_type {
    VOC_TYPE_EOF              = 0x00,
    VOC_TYPE_VOICE_DATA       = 0x01,
    VOC_TYPE_VOICE_DATA_CONT  = 0x02,
    VOC_TYPE_SILENCE          = 0x03,
    VOC_TYPE_MARKER           = 0x04,
    VOC_TYPE_ASCII            = 0x05,
    VOC_TYPE_REPETITION_START = 0x06,
    VOC_TYPE_REPETITION_END   = 0x07,
    VOC_TYPE_EXTENDED         = 0x08,
    VOC_TYPE_NEW_VOICE_DATA   = 0x09,
} voc_type_t;


static const int voc_max_pkt_size = 2048;
static const unsigned char voc_magic[] = "Creative Voice File\x1A";

static const CodecTag voc_codec_tags[] = {
    {CODEC_ID_PCM_U8,        0x00},
    {CODEC_ID_ADPCM_SBPRO_4, 0x01},
    {CODEC_ID_ADPCM_SBPRO_3, 0x02},
    {CODEC_ID_ADPCM_SBPRO_2, 0x03},
    {CODEC_ID_PCM_S16LE,     0x04},
    {CODEC_ID_PCM_ALAW,      0x06},
    {CODEC_ID_PCM_MULAW,     0x07},
    {CODEC_ID_ADPCM_CT,    0x0200},
    {0, 0},
};


#ifdef CONFIG_DEMUXERS

static int voc_probe(AVProbeData *p)
{
    int version, check;

    if (p->buf_size < 26)
        return 0;
    if (memcmp(p->buf, voc_magic, sizeof(voc_magic) - 1))
        return 0;
    version = p->buf[22] | (p->buf[23] << 8);
    check = p->buf[24] | (p->buf[25] << 8);
    if (~version + 0x1234 != check)
        return 10;

    return AVPROBE_SCORE_MAX;
}

static int voc_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    voc_dec_context_t *voc = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int header_size;
    AVStream *st;

    url_fskip(pb, 20);
    header_size = get_le16(pb) - 22;
    if (header_size != 4) {
        av_log(s, AV_LOG_ERROR, "unkown header size: %d\n", header_size);
        return AVERROR_NOTSUPP;
    }
    url_fskip(pb, header_size);
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;
    st->codec->codec_type = CODEC_TYPE_AUDIO;

    voc->remaining_size = 0;
    return 0;
}

int
voc_get_packet(AVFormatContext *s, AVPacket *pkt, AVStream *st, int max_size)
{
    voc_dec_context_t *voc = s->priv_data;
    AVCodecContext *dec = st->codec;
    ByteIOContext *pb = &s->pb;
    voc_type_t type;
    int size;
    int sample_rate = 0;
    int channels = 1;

    while (!voc->remaining_size) {
        type = get_byte(pb);
        if (type == VOC_TYPE_EOF)
            return AVERROR_IO;
        voc->remaining_size = get_le24(pb);
        max_size -= 4;

        switch (type) {
        case VOC_TYPE_VOICE_DATA:
            dec->sample_rate = 1000000 / (256 - get_byte(pb));
            if (sample_rate)
                dec->sample_rate = sample_rate;
            dec->channels = channels;
            dec->codec_id = codec_get_id(voc_codec_tags, get_byte(pb));
            dec->bits_per_sample = av_get_bits_per_sample(dec->codec_id);
            voc->remaining_size -= 2;
            max_size -= 2;
            channels = 1;
            break;

        case VOC_TYPE_VOICE_DATA_CONT:
            break;

        case VOC_TYPE_EXTENDED:
            sample_rate = get_le16(pb);
            get_byte(pb);
            channels = get_byte(pb) + 1;
            sample_rate = 256000000 / (channels * (65536 - sample_rate));
            voc->remaining_size = 0;
            max_size -= 4;
            break;

        case VOC_TYPE_NEW_VOICE_DATA:
            dec->sample_rate = get_le32(pb);
            dec->bits_per_sample = get_byte(pb);
            dec->channels = get_byte(pb);
            dec->codec_id = codec_get_id(voc_codec_tags, get_le16(pb));
            url_fskip(pb, 4);
            voc->remaining_size -= 12;
            max_size -= 12;
            break;

        default:
            url_fskip(pb, voc->remaining_size);
            max_size -= voc->remaining_size;
            voc->remaining_size = 0;
            break;
        }
    }

    dec->bit_rate = dec->sample_rate * dec->bits_per_sample;

    if (max_size <= 0)
        max_size = voc_max_pkt_size;
    size = FFMIN(voc->remaining_size, max_size);
    voc->remaining_size -= size;
    return av_get_packet(pb, pkt, size);
}

static int voc_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    return voc_get_packet(s, pkt, s->streams[0], 0);
}

static int voc_read_close(AVFormatContext *s)
{
    return 0;
}

static AVInputFormat voc_demuxer = {
    "voc",
    "Creative Voice File format",
    sizeof(voc_dec_context_t),
    voc_probe,
    voc_read_header,
    voc_read_packet,
    voc_read_close,
};

#endif /* CONFIG_DEMUXERS */


#ifdef CONFIG_MUXERS

typedef struct voc_enc_context {
    int param_written;
} voc_enc_context_t;

static int voc_write_header(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    const int header_size = 26;
    const int version = 0x0114;

    if (s->nb_streams != 1
        || s->streams[0]->codec->codec_type != CODEC_TYPE_AUDIO)
        return AVERROR_NOTSUPP;

    put_buffer(pb, voc_magic, sizeof(voc_magic) - 1);
    put_le16(pb, header_size);
    put_le16(pb, version);
    put_le16(pb, ~version + 0x1234);

    return 0;
}

static int voc_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    voc_enc_context_t *voc = s->priv_data;
    AVCodecContext *enc = s->streams[0]->codec;
    ByteIOContext *pb = &s->pb;

    if (!voc->param_written) {
        int format = codec_get_tag(voc_codec_tags, enc->codec_id);

        if (format > 0xFF) {
            put_byte(pb, VOC_TYPE_NEW_VOICE_DATA);
            put_le24(pb, pkt->size + 12);
            put_le32(pb, enc->sample_rate);
            put_byte(pb, enc->bits_per_sample);
            put_byte(pb, enc->channels);
            put_le16(pb, format);
            put_le32(pb, 0);
        } else {
            if (s->streams[0]->codec->channels > 1) {
                put_byte(pb, VOC_TYPE_EXTENDED);
                put_le24(pb, 4);
                put_le16(pb, 65536-256000000/(enc->sample_rate*enc->channels));
                put_byte(pb, format);
                put_byte(pb, enc->channels - 1);
            }
            put_byte(pb, VOC_TYPE_VOICE_DATA);
            put_le24(pb, pkt->size + 2);
            put_byte(pb, 256 - 1000000 / enc->sample_rate);
            put_byte(pb, format);
        }
        voc->param_written = 1;
    } else {
        put_byte(pb, VOC_TYPE_VOICE_DATA_CONT);
        put_le24(pb, pkt->size);
    }

    put_buffer(pb, pkt->data, pkt->size);
    return 0;
}

static int voc_write_trailer(AVFormatContext *s)
{
    put_byte(&s->pb, 0);
    return 0;
}

static AVOutputFormat voc_muxer = {
    "voc",
    "Creative Voice File format",
    "audio/x-voc",
    "voc",
    sizeof(voc_enc_context_t),
    CODEC_ID_PCM_U8,
    CODEC_ID_NONE,
    voc_write_header,
    voc_write_packet,
    voc_write_trailer,
};

#endif /* CONFIG_MUXERS */


int voc_init(void)
{
#ifdef CONFIG_DEMUXERS
    av_register_input_format(&voc_demuxer);
#endif /* CONFIG_DEMUXERS */
#ifdef CONFIG_MUXERS
    av_register_output_format(&voc_muxer);
#endif /* CONFIG_MUXERS */
    return 0;
}
