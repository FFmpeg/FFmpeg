/*
 * AIFF/AIFF-C muxer and demuxer
 * Copyright (c) 2006  Patrick Guimond
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

#include "libavutil/intfloat_readwrite.h"
#include "avformat.h"
#include "raw.h"
#include "riff.h"

static const AVCodecTag codec_aiff_tags[] = {
    { CODEC_ID_PCM_S16BE,    MKTAG('N','O','N','E') },
    { CODEC_ID_PCM_S8,       MKTAG('N','O','N','E') },
    { CODEC_ID_PCM_S24BE,    MKTAG('N','O','N','E') },
    { CODEC_ID_PCM_S32BE,    MKTAG('N','O','N','E') },
    { CODEC_ID_PCM_F32BE,    MKTAG('f','l','3','2') },
    { CODEC_ID_PCM_F64BE,    MKTAG('f','l','6','4') },
    { CODEC_ID_PCM_ALAW,     MKTAG('a','l','a','w') },
    { CODEC_ID_PCM_MULAW,    MKTAG('u','l','a','w') },
    { CODEC_ID_MACE3,        MKTAG('M','A','C','3') },
    { CODEC_ID_MACE6,        MKTAG('M','A','C','6') },
    { CODEC_ID_GSM,          MKTAG('G','S','M',' ') },
    { CODEC_ID_ADPCM_G726,   MKTAG('G','7','2','6') },
    { CODEC_ID_PCM_S16LE,    MKTAG('s','o','w','t') },
    { CODEC_ID_ADPCM_IMA_QT, MKTAG('i','m','a','4') },
    { CODEC_ID_QDM2,         MKTAG('Q','D','M','2') },
    { 0, 0 },
};

#define AIFF                    0
#define AIFF_C_VERSION1         0xA2805140

static enum CodecID aiff_codec_get_id(int bps)
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
    return CODEC_ID_NONE;
}

/* returns the size of the found tag */
static int get_tag(ByteIOContext *pb, uint32_t * tag)
{
    int size;

    if (url_feof(pb))
        return AVERROR(EIO);

    *tag = get_le32(pb);
    size = get_be32(pb);

    if (size < 0)
        size = 0x7fffffff;

    return size;
}

/* Metadata string read */
static void get_meta(AVFormatContext *s, const char *key, int size)
{
    uint8_t str[1024];
    int res = get_buffer(s->pb, str, FFMIN(sizeof(str)-1, size));
    if (res < 0)
        return;

    str[res] = 0;
    if (size & 1)
        size++;
    size -= res;
    if (size)
        url_fskip(s->pb, size);

    av_metadata_set(&s->metadata, key, str);
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
    codec->bits_per_coded_sample = get_be16(pb);

    get_buffer(pb, (uint8_t*)&ext, sizeof(ext));/* Sample rate is in */
    sample_rate = av_ext2dbl(ext);          /* 80 bits BE IEEE extended float */
    codec->sample_rate = sample_rate;
    size -= 18;

    /* Got an AIFF-C? */
    if (version == AIFF_C_VERSION1) {
        codec->codec_tag = get_le32(pb);
        codec->codec_id  = codec_get_id(codec_aiff_tags, codec->codec_tag);

        switch (codec->codec_id) {
        case CODEC_ID_PCM_S16BE:
            codec->codec_id = aiff_codec_get_id(codec->bits_per_coded_sample);
            codec->bits_per_coded_sample = av_get_bits_per_sample(codec->codec_id);
            break;
        case CODEC_ID_ADPCM_IMA_QT:
            codec->block_align = 34*codec->channels;
            codec->frame_size = 64;
            break;
        case CODEC_ID_MACE3:
            codec->block_align = 2*codec->channels;
            codec->frame_size = 6;
            break;
        case CODEC_ID_MACE6:
            codec->block_align = 1*codec->channels;
            codec->frame_size = 6;
            break;
        case CODEC_ID_GSM:
            codec->block_align = 33;
            codec->frame_size = 160;
            break;
        default:
            break;
        }
        size -= 4;
    } else {
        /* Need the codec type */
        codec->codec_id = aiff_codec_get_id(codec->bits_per_coded_sample);
        codec->bits_per_coded_sample = av_get_bits_per_sample(codec->codec_id);
    }

    /* Block align needs to be computed in all cases, as the definition
     * is specific to applications -> here we use the WAVE format definition */
    if (!codec->block_align)
        codec->block_align = (codec->bits_per_coded_sample * codec->channels) >> 3;

    codec->bit_rate = (codec->frame_size ? codec->sample_rate/codec->frame_size :
                       codec->sample_rate) * (codec->block_align << 3);

    /* Chunk is over */
    if (size)
        url_fseek(pb, size, SEEK_CUR);

    return num_frames;
}

#if CONFIG_AIFF_MUXER
typedef struct {
    int64_t form;
    int64_t frames;
    int64_t ssnd;
} AIFFOutputContext;

static int aiff_write_header(AVFormatContext *s)
{
    AIFFOutputContext *aiff = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVCodecContext *enc = s->streams[0]->codec;
    AVExtFloat sample_rate;
    int aifc = 0;

    /* First verify if format is ok */
    if (!enc->codec_tag)
        return -1;
    if (enc->codec_tag != MKTAG('N','O','N','E'))
        aifc = 1;

    /* FORM AIFF header */
    put_tag(pb, "FORM");
    aiff->form = url_ftell(pb);
    put_be32(pb, 0);                    /* file length */
    put_tag(pb, aifc ? "AIFC" : "AIFF");

    if (aifc) { // compressed audio
        enc->bits_per_coded_sample = 16;
        if (!enc->block_align) {
            av_log(s, AV_LOG_ERROR, "block align not set\n");
            return -1;
        }
        /* Version chunk */
        put_tag(pb, "FVER");
        put_be32(pb, 4);
        put_be32(pb, 0xA2805140);
    }

    /* Common chunk */
    put_tag(pb, "COMM");
    put_be32(pb, aifc ? 24 : 18); /* size */
    put_be16(pb, enc->channels);  /* Number of channels */

    aiff->frames = url_ftell(pb);
    put_be32(pb, 0);              /* Number of frames */

    if (!enc->bits_per_coded_sample)
        enc->bits_per_coded_sample = av_get_bits_per_sample(enc->codec_id);
    if (!enc->bits_per_coded_sample) {
        av_log(s, AV_LOG_ERROR, "could not compute bits per sample\n");
        return -1;
    }
    if (!enc->block_align)
        enc->block_align = (enc->bits_per_coded_sample * enc->channels) >> 3;

    put_be16(pb, enc->bits_per_coded_sample); /* Sample size */

    sample_rate = av_dbl2ext((double)enc->sample_rate);
    put_buffer(pb, (uint8_t*)&sample_rate, sizeof(sample_rate));

    if (aifc) {
        put_le32(pb, enc->codec_tag);
        put_be16(pb, 0);
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
    ByteIOContext *pb = s->pb;
    put_buffer(pb, pkt->data, pkt->size);
    return 0;
}

static int aiff_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = s->pb;
    AIFFOutputContext *aiff = s->priv_data;
    AVCodecContext *enc = s->streams[0]->codec;

    /* Chunks sizes must be even */
    int64_t file_size, end_size;
    end_size = file_size = url_ftell(pb);
    if (file_size & 1) {
        put_byte(pb, 0);
        end_size++;
    }

    if (!url_is_streamed(s->pb)) {
        /* File length */
        url_fseek(pb, aiff->form, SEEK_SET);
        put_be32(pb, file_size - aiff->form - 4);

        /* Number of sample frames */
        url_fseek(pb, aiff->frames, SEEK_SET);
        put_be32(pb, (file_size-aiff->ssnd-12)/enc->block_align);

        /* Sound Data chunk size */
        url_fseek(pb, aiff->ssnd, SEEK_SET);
        put_be32(pb, file_size - aiff->ssnd - 4);

        /* return to the end */
        url_fseek(pb, end_size, SEEK_SET);

        put_flush_packet(pb);
    }

    return 0;
}
#endif /* CONFIG_AIFF_MUXER */

static int aiff_probe(AVProbeData *p)
{
    /* check file header */
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
    int size, filesize;
    int64_t offset = 0;
    uint32_t tag;
    unsigned version = AIFF_C_VERSION1;
    ByteIOContext *pb = s->pb;
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
        return AVERROR(ENOMEM);

    while (filesize > 0) {
        /* parse different chunks */
        size = get_tag(pb, &tag);
        if (size < 0)
            return size;

        filesize -= size + 8;

        switch (tag) {
        case MKTAG('C', 'O', 'M', 'M'):     /* Common chunk */
            /* Then for the complete header info */
            st->nb_frames = get_aiff_header(pb, st->codec, size, version);
            if (st->nb_frames < 0)
                return st->nb_frames;
            if (offset > 0) // COMM is after SSND
                goto got_sound;
            break;
        case MKTAG('F', 'V', 'E', 'R'):     /* Version chunk */
            version = get_be32(pb);
            break;
        case MKTAG('N', 'A', 'M', 'E'):     /* Sample name chunk */
            get_meta(s, "title"    , size);
            break;
        case MKTAG('A', 'U', 'T', 'H'):     /* Author chunk */
            get_meta(s, "author"   , size);
            break;
        case MKTAG('(', 'c', ')', ' '):     /* Copyright chunk */
            get_meta(s, "copyright", size);
            break;
        case MKTAG('A', 'N', 'N', 'O'):     /* Annotation chunk */
            get_meta(s, "comment"  , size);
            break;
        case MKTAG('S', 'S', 'N', 'D'):     /* Sampled sound chunk */
            offset = get_be32(pb);      /* Offset of sound data */
            get_be32(pb);               /* BlockSize... don't care */
            offset += url_ftell(pb);    /* Compute absolute data offset */
            if (st->codec->block_align)    /* Assume COMM already parsed */
                goto got_sound;
            if (url_is_streamed(pb)) {
                av_log(s, AV_LOG_ERROR, "file is not seekable\n");
                return -1;
            }
            url_fskip(pb, size - 8);
            break;
        case MKTAG('w', 'a', 'v', 'e'):
            if ((uint64_t)size > (1<<30))
                return -1;
            st->codec->extradata = av_mallocz(size + FF_INPUT_BUFFER_PADDING_SIZE);
            if (!st->codec->extradata)
                return AVERROR(ENOMEM);
            st->codec->extradata_size = size;
            get_buffer(pb, st->codec->extradata, size);
            break;
        default: /* Jump */
            if (size & 1)   /* Always even aligned */
                size++;
            url_fskip (pb, size);
        }
    }

    if (!st->codec->block_align) {
        av_log(s, AV_LOG_ERROR, "could not find COMM tag\n");
        return -1;
    }

got_sound:
    /* Now positioned, get the sound data start and end */
    if (st->nb_frames)
        s->file_size = st->nb_frames * st->codec->block_align;

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);
    st->start_time = 0;
    st->duration = st->codec->frame_size ?
        st->nb_frames * st->codec->frame_size : st->nb_frames;

    /* Position the stream at the first block */
    url_fseek(pb, offset, SEEK_SET);

    return 0;
}

#define MAX_SIZE 4096

static int aiff_read_packet(AVFormatContext *s,
                            AVPacket *pkt)
{
    AVStream *st = s->streams[0];
    int res;

    /* End of stream may be reached */
    if (url_feof(s->pb))
        return AVERROR(EIO);

    /* Now for that packet */
    res = av_get_packet(s->pb, pkt, (MAX_SIZE / st->codec->block_align) * st->codec->block_align);
    if (res < 0)
        return res;

    /* Only one stream in an AIFF file */
    pkt->stream_index = 0;
    return 0;
}

#if CONFIG_AIFF_DEMUXER
AVInputFormat aiff_demuxer = {
    "aiff",
    NULL_IF_CONFIG_SMALL("Audio IFF"),
    0,
    aiff_probe,
    aiff_read_header,
    aiff_read_packet,
    NULL,
    pcm_read_seek,
    .codec_tag= (const AVCodecTag* const []){codec_aiff_tags, 0},
};
#endif

#if CONFIG_AIFF_MUXER
AVOutputFormat aiff_muxer = {
    "aiff",
    NULL_IF_CONFIG_SMALL("Audio IFF"),
    "audio/aiff",
    "aif,aiff,afc,aifc",
    sizeof(AIFFOutputContext),
    CODEC_ID_PCM_S16BE,
    CODEC_ID_NONE,
    aiff_write_header,
    aiff_write_packet,
    aiff_write_trailer,
    .codec_tag= (const AVCodecTag* const []){codec_aiff_tags, 0},
};
#endif
