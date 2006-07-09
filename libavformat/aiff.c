/*
 * AIFF/AIFF-C encoder and decoder
 * Copyright (c) 2006  Patrick Guimond
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include "avformat.h"
#include "allformats.h"
#include "avi.h"
#include "intfloat_readwrite.h"

const CodecTag codec_aiff_tags[] = {
    { CODEC_ID_PCM_S16BE, MKTAG('N','O','N','E') },
    { CODEC_ID_PCM_S8, MKTAG('N','O','N','E') },
    { CODEC_ID_PCM_S24BE, MKTAG('N','O','N','E') },
    { CODEC_ID_PCM_S32BE, MKTAG('N','O','N','E') },
    { CODEC_ID_PCM_ALAW, MKTAG('a','l','a','w') },
    { CODEC_ID_PCM_ALAW, MKTAG('A','L','A','W') },
    { CODEC_ID_PCM_MULAW, MKTAG('u','l','a','w') },
    { CODEC_ID_PCM_MULAW, MKTAG('U','L','A','W') },
    { CODEC_ID_MACE3, MKTAG('M','A','C','3') },
    { CODEC_ID_MACE6, MKTAG('M','A','C','6') },
    { CODEC_ID_GSM, MKTAG('G','S','M',' ') },
    { CODEC_ID_ADPCM_G726, MKTAG('G','7','2','6') },
    { 0, 0 },
};

#define AIFF                    0
#define AIFF_C_VERSION1         0xA2805140

static int aiff_codec_get_id (int bps)
{
    if (bps <= 8)
        return CODEC_ID_PCM_S8;
    if (bps <= 16)
        return CODEC_ID_PCM_S16BE;
    if (bps <= 24)
        return CODEC_ID_PCM_S24BE;
    if (bps <= 32)
        return CODEC_ID_PCM_S32BE;

    /* bigger than 32 isn't allowed  */
    return 0;
}

/* returns the size of the found tag */
static int get_tag(ByteIOContext *pb, uint32_t * tag)
{
    int size;

    if (url_feof(pb))
        return AVERROR_IO;

    *tag = get_le32(pb);
    size = get_be32(pb);

    if (size < 0)
        size = 0x7fffffff;

    return size;
}

/* Metadata string read */
static void get_meta(ByteIOContext *pb, char * str, int strsize, int size)
{
    int res;

    if (size > strsize-1)
        res = get_buffer(pb, (uint8_t*)str, strsize-1);
    else
        res = get_buffer(pb, (uint8_t*)str, size);

    if (res < 0)
        return;

    str[res] = 0;
    if (size & 1)
        size++;
    size -= res;
    if (size);
        url_fskip(pb, size);
}

/* Returns the number of bits per second */
static int fix_bps(int codec_id)
{
    switch (codec_id) {
        case CODEC_ID_PCM_S8:
                return 8;
        case CODEC_ID_PCM_S16BE:
                return 16;
        case CODEC_ID_PCM_S24BE:
                return 24;
        case CODEC_ID_PCM_S32BE:
                return 32;
    }

    return -1;
}

/* Returns the number of sound data frames or negative on error */
static unsigned int get_aiff_header(ByteIOContext *pb, AVCodecContext *codec,
                             int size, unsigned version)
{
    AVExtFloat ext;
    double sample_rate;
    unsigned int num_frames;


    if (size & 1)
        size++;

    codec->codec_type = CODEC_TYPE_AUDIO;
    codec->channels = get_be16(pb);
    num_frames = get_be32(pb);
    codec->bits_per_sample = get_be16(pb);

    get_buffer(pb, (uint8_t*)&ext, sizeof(ext));/* Sample rate is in */
    sample_rate = av_ext2dbl(ext);          /* 80 bits BE IEEE extended float */
    codec->sample_rate = sample_rate;
    size -= 18;

    /* Got an AIFF-C? */
    if (version == AIFF_C_VERSION1) {
        codec->codec_tag = get_le32(pb);
        codec->codec_id  = codec_get_id (codec_aiff_tags, codec->codec_tag);

        if (codec->codec_id == CODEC_ID_PCM_S16BE) {
            codec->codec_id = aiff_codec_get_id (codec->bits_per_sample);
            codec->bits_per_sample = fix_bps(codec->codec_id);
        }

        size -= 4;
    } else {
        /* Need the codec type */
        codec->codec_id = aiff_codec_get_id (codec->bits_per_sample);
        codec->bits_per_sample = fix_bps(codec->codec_id);
    }

    if (!codec->codec_id)
        return AVERROR_INVALIDDATA;

    /* Block align needs to be computed in all cases, as the definition
     * is specific to applications -> here we use the WAVE format definition */
    codec->block_align = (codec->bits_per_sample * codec->channels) >> 3;

    codec->bit_rate = codec->sample_rate * codec->block_align;

    /* Chunk is over */
    if (size)
        url_fseek(pb, size, SEEK_CUR);

    return num_frames;
}

#ifdef CONFIG_MUXERS
typedef struct {
    offset_t form;
    offset_t frames;
    offset_t ssnd;
} AIFFOutputContext;

static int aiff_write_header(AVFormatContext *s)
{
    AIFFOutputContext *aiff = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc = s->streams[0]->codec;
    AVExtFloat sample_rate;
    int coder_len;

    /* First verify if format is ok */
    enc->codec_tag = codec_get_tag(codec_aiff_tags, enc->codec_id);
    if (!enc->codec_tag) {
        av_free(aiff);
        return -1;
    }

    coder_len = strlen(enc->codec->name);

    /* FORM AIFF header */
    put_tag(pb, "FORM");
    aiff->form = url_ftell(pb);
    put_be32(pb, 0);                    /* file length */
    put_tag(pb, "AIFC");

    /* Version chunk */
    put_tag(pb, "FVER");
    put_be32(pb, 4);
    put_be32(pb, 0xA2805140);

    /* Common chunk */
    put_tag(pb, "COMM");
    if (coder_len & 1)                  /* Common chunk is of var size */
        put_be32(pb, 23+coder_len);
    else
        put_be32(pb, 24+coder_len);
    put_be16(pb, enc->channels);        /* Number of channels */

    aiff->frames = url_ftell(pb);
    put_be32(pb, 0);                    /* Number of frames */

    if (!enc->bits_per_sample)
        enc->bits_per_sample = (enc->block_align<<3) / enc->channels;
    put_be16(pb, enc->bits_per_sample); /* Sample size */

    sample_rate = av_dbl2ext((double)enc->sample_rate);
    put_buffer(pb, (uint8_t*)&sample_rate, sizeof(sample_rate));

    put_le32(pb, enc->codec_tag);
    if (coder_len & 1) {
        put_byte(pb, coder_len);
        put_buffer(pb, (const uint8_t*)enc->codec->name, coder_len);
    } else {
        put_byte(pb, coder_len+1);
        put_buffer(pb, (const uint8_t*)enc->codec->name, coder_len);
        put_byte(pb, 0);
    }

    /* Sound data chunk */
    put_tag(pb, "SSND");
    aiff->ssnd = url_ftell(pb);         /* Sound chunk size */
    put_be32(pb, 0);                    /* Sound samples data size */
    put_be32(pb, 0);                    /* Data offset */
    put_be32(pb, 0);                    /* Block-size (block align) */

    av_set_pts_info(s->streams[0], 64, 1, s->streams[0]->codec->sample_rate);

    /* Data is starting here */
    put_flush_packet(pb);

    return 0;
}

static int aiff_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = &s->pb;
    put_buffer(pb, pkt->data, pkt->size);
    return 0;
}

static int aiff_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    AIFFOutputContext *aiff = s->priv_data;
    AVCodecContext *enc = s->streams[0]->codec;

    /* Chunks sizes must be even */
    offset_t file_size, end_size;
    end_size = file_size = url_ftell(pb);
    if (file_size & 1) {
        put_byte(pb, 0);
        end_size++;
    }

    if (!url_is_streamed(&s->pb)) {
        /* File length */
        url_fseek(pb, aiff->form, SEEK_SET);
        put_be32(pb, (uint32_t)(file_size - aiff->form - 4));

        /* Number of sample frames */
        url_fseek(pb, aiff->frames, SEEK_SET);
        put_be32(pb, ((uint32_t)(file_size-aiff->ssnd-12))/enc->block_align);

        /* Sound Data chunk size */
        url_fseek(pb, aiff->ssnd, SEEK_SET);
        put_be32(pb, (uint32_t)(file_size - aiff->ssnd - 4));

        /* return to the end */
        url_fseek(pb, end_size, SEEK_SET);

        put_flush_packet(pb);
    }

    return 0;
}
#endif //CONFIG_MUXERS

static int aiff_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size < 16)
        return 0;
    if (p->buf[0] == 'F' && p->buf[1] == 'O' &&
        p->buf[2] == 'R' && p->buf[3] == 'M' &&
        p->buf[8] == 'A' && p->buf[9] == 'I' &&
        p->buf[10] == 'F' && (p->buf[11] == 'F' || p->buf[11] == 'C'))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

/* aiff input */
static int aiff_read_header(AVFormatContext *s,
                            AVFormatParameters *ap)
{
    int size, filesize, offset;
    uint32_t tag;
    unsigned version = AIFF_C_VERSION1;
    ByteIOContext *pb = &s->pb;
    AVStream * st = s->streams[0];

    /* check FORM header */
    filesize = get_tag(pb, &tag);
    if (filesize < 0 || tag != MKTAG('F', 'O', 'R', 'M'))
        return AVERROR_INVALIDDATA;

    /* AIFF data type */
    tag = get_le32(pb);
    if (tag == MKTAG('A', 'I', 'F', 'F'))       /* Got an AIFF file */
        version = AIFF;
    else if (tag != MKTAG('A', 'I', 'F', 'C'))  /* An AIFF-C file then */
        return AVERROR_INVALIDDATA;

    filesize -= 4;

    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;

    while (filesize > 0) {
        /* parse different chunks */
        size = get_tag(pb, &tag);
        if (size < 0)
            return size;

        filesize -= size + 8;

        switch (tag) {
            case MKTAG('C', 'O', 'M', 'M'):     /* Common chunk */
                /* Then for the complete header info */
                st->nb_frames = get_aiff_header (pb, st->codec, size, version);
                if (st->nb_frames < 0)
                        return st->nb_frames;
                break;

            case MKTAG('F', 'V', 'E', 'R'):     /* Version chunk */
                version = get_be32(pb);
                break;

            case MKTAG('N', 'A', 'M', 'E'):     /* Sample name chunk */
                get_meta (pb, s->title, sizeof(s->title), size);
                break;

            case MKTAG('A', 'U', 'T', 'H'):     /* Author chunk */
                get_meta (pb, s->author, sizeof(s->author), size);
                break;

            case MKTAG('(', 'c', ')', ' '):     /* Copyright chunk */
                get_meta (pb, s->copyright, sizeof(s->copyright), size);
                break;

            case MKTAG('A', 'N', 'N', 'O'):     /* Annotation chunk */
                get_meta (pb, s->comment, sizeof(s->comment), size);
                break;

            case MKTAG('S', 'S', 'N', 'D'):     /* Sampled sound chunk */
                get_be32(pb);               /* Block align... don't care */
                offset = get_be32(pb);      /* Offset of sound data */
                goto got_sound;

            default: /* Jump */
                if (size & 1)   /* Always even aligned */
                    size++;
                url_fskip (pb, size);
        }
    }

    /* End of loop and didn't get sound */
    return AVERROR_INVALIDDATA;

got_sound:
    /* Now positioned, get the sound data start and end */
    if (st->nb_frames)
        s->file_size = st->nb_frames * st->codec->block_align;

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);
    st->start_time = 0;
    st->duration = st->nb_frames;

    /* Position the stream at the first block */
    url_fskip(pb, offset);

    return 0;
}

#define MAX_SIZE 4096

static int aiff_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    AVStream *st = s->streams[0];
    int res;

    /* End of stream may be reached */
    if (url_feof(&s->pb))
        return AVERROR_IO;

    /* Now for that packet */
    res = av_get_packet(&s->pb, pkt, (MAX_SIZE / st->codec->block_align) * st->codec->block_align);
    if (res < 0)
        return res;

    /* Only one stream in an AIFF file */
    pkt->stream_index = 0;
    return 0;
}

static int aiff_read_close(AVFormatContext *s)
{
    return 0;
}

static int aiff_read_seek(AVFormatContext *s,
                          int stream_index, int64_t timestamp, int flags)
{
    return pcm_read_seek(s, stream_index, timestamp, flags);
}


static AVInputFormat aiff_demuxer = {
    "aiff",
    "Audio IFF",
    0,
    aiff_probe,
    aiff_read_header,
    aiff_read_packet,
    aiff_read_close,
    aiff_read_seek,
};

#ifdef CONFIG_MUXERS
static AVOutputFormat aiff_muxer = {
    "aiff",
    "Audio IFF",
    "audio/aiff",
    "aif,aiff,afc,aifc",
    sizeof(AIFFOutputContext),
    CODEC_ID_PCM_S16BE,
    CODEC_ID_NONE,
    aiff_write_header,
    aiff_write_packet,
    aiff_write_trailer,
};
#endif //CONFIG_MUXERS

int ff_aiff_init(void)
{
    av_register_input_format(&aiff_demuxer);
#ifdef CONFIG_MUXERS
    av_register_output_format(&aiff_muxer);
#endif //CONFIG_MUXERS
    return 0;
}

