/* 
 * WAV encoder and decoder
 * Copyright (c) 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include "avformat.h"
#include "avi.h"

CodecTag codec_wav_tags[] = {
    { CODEC_ID_MP2, 0x50 },
    { CODEC_ID_MP3LAME, 0x55 },
    { CODEC_ID_AC3, 0x2000 },
    { CODEC_ID_PCM_S16LE, 0x01 },
    { CODEC_ID_PCM_U8, 0x01 }, /* must come after s16le in this list */
    { CODEC_ID_PCM_ALAW, 0x06 },
    { CODEC_ID_PCM_MULAW, 0x07 },
    { 0, 0 },
};

/* WAVEFORMATEX header */
/* returns the size or -1 on error */
int put_wav_header(ByteIOContext *pb, AVCodecContext *enc)
{
    int tag, bps, blkalign, bytespersec;
    int hdrsize = 18;

    tag = codec_get_tag(codec_wav_tags, enc->codec_id);
    if (tag == 0)
        return -1;
    put_le16(pb, tag);
    put_le16(pb, enc->channels);
    put_le32(pb, enc->sample_rate);
    if (enc->codec_id == CODEC_ID_PCM_U8 ||
        enc->codec_id == CODEC_ID_PCM_ALAW ||
        enc->codec_id == CODEC_ID_PCM_MULAW) {
        bps = 8;
    } else if (enc->codec_id == CODEC_ID_MP2 || enc->codec_id == CODEC_ID_MP3LAME) {
        bps = 0;
    } else {
        bps = 16;
    }
    
    if (enc->codec_id == CODEC_ID_MP2 || enc->codec_id == CODEC_ID_MP3LAME) {
        blkalign = 1;
        //blkalign = 144 * enc->bit_rate/enc->sample_rate;
    } else
        blkalign = enc->channels*bps >> 3;
    if (enc->codec_id == CODEC_ID_PCM_U8 ||
        enc->codec_id == CODEC_ID_PCM_S16LE) {
        bytespersec = enc->sample_rate * blkalign;
    } else {
        bytespersec = enc->bit_rate / 8;
    }
    put_le32(pb, bytespersec); /* bytes per second */
    put_le16(pb, blkalign); /* block align */
    put_le16(pb, bps); /* bits per sample */
    if (enc->codec_id == CODEC_ID_MP3LAME) {
        put_le16(pb, 12); /* wav_extra_size */
        hdrsize += 12;
        put_le16(pb, 1); /* wID */
        put_le32(pb, 2); /* fdwFlags */
        put_le16(pb, 1152); /* nBlockSize */
        put_le16(pb, 1); /* nFramesPerBlock */
        put_le16(pb, 1393); /* nCodecDelay */
    } else if (enc->codec_id == CODEC_ID_MP2) {
        put_le16(pb, 22); /* wav_extra_size */
        hdrsize += 22;
        put_le16(pb, 2);  /* fwHeadLayer */
        put_le32(pb, enc->bit_rate); /* dwHeadBitrate */
        put_le16(pb, enc->channels == 2 ? 1 : 8); /* fwHeadMode */
        put_le16(pb, 0);  /* fwHeadModeExt */
        put_le16(pb, 1);  /* wHeadEmphasis */
        put_le16(pb, 16); /* fwHeadFlags */
        put_le32(pb, 0);  /* dwPTSLow */
        put_le32(pb, 0);  /* dwPTSHigh */
    } else
        put_le16(pb, 0); /* wav_extra_size */

    return hdrsize;
}

int wav_codec_get_id(unsigned int tag, int bps)
{
    int id;
    id = codec_get_id(codec_wav_tags, tag);
    if (id <= 0)
        return id;
    /* handle specific u8 codec */
    if (id == CODEC_ID_PCM_S16LE && bps == 8)
        id = CODEC_ID_PCM_U8;
    return id;
}

typedef struct {
    offset_t data;
} WAVContext;

static int wav_write_header(AVFormatContext *s)
{
    WAVContext *wav;
    ByteIOContext *pb = &s->pb;
    offset_t fmt;

    wav = malloc(sizeof(WAVContext));
    if (!wav)
        return -1;
    memset(wav, 0, sizeof(WAVContext));
    s->priv_data = wav;

    put_tag(pb, "RIFF");
    put_le32(pb, 0); /* file length */
    put_tag(pb, "WAVE");

    /* format header */
    fmt = start_tag(pb, "fmt ");
    if (put_wav_header(pb, &s->streams[0]->codec) < 0) {
        free(wav);
        return -1;
    }
    end_tag(pb, fmt);

    /* data header */
    wav->data = start_tag(pb, "data");
    
    put_flush_packet(pb);

    return 0;
}

static int wav_write_packet(AVFormatContext *s, int stream_index_ptr,
                           UINT8 *buf, int size, int force_pts)
{
    ByteIOContext *pb = &s->pb;
    put_buffer(pb, buf, size);
    return 0;
}

static int wav_write_trailer(AVFormatContext *s)
{
    ByteIOContext *pb = &s->pb;
    WAVContext *wav = s->priv_data;
    offset_t file_size;

    if (!url_is_streamed(&s->pb)) {
        end_tag(pb, wav->data);

        /* update file size */
        file_size = url_ftell(pb);
        url_fseek(pb, 4, SEEK_SET);
        put_le32(pb, (UINT32)(file_size - 8));
        url_fseek(pb, file_size, SEEK_SET);

        put_flush_packet(pb);
    }

    free(wav);
    return 0;
}

/* return the size of the found tag */
/* XXX: > 2GB ? */
static int find_tag(ByteIOContext *pb, UINT32 tag1)
{
    unsigned int tag;
    int size;

    for(;;) {
        if (url_feof(pb))
            return -1;
        tag = get_le32(pb);
        size = get_le32(pb);
        if (tag == tag1)
            break;
        url_fseek(pb, size, SEEK_CUR);
    }
    if (size < 0)
        size = 0x7fffffff;
    return size;
}

/* wav input */
static int wav_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    int size;
    unsigned int tag;
    ByteIOContext *pb = &s->pb;
    unsigned int id, channels, rate, bit_rate, extra_size, bps;
    AVStream *st;

    /* check RIFF header */
    tag = get_le32(pb);

    if (tag != MKTAG('R', 'I', 'F', 'F'))
        return -1;
    get_le32(pb); /* file size */
    tag = get_le32(pb);
    if (tag != MKTAG('W', 'A', 'V', 'E'))
        return -1;
    
    /* parse fmt header */
    size = find_tag(pb, MKTAG('f', 'm', 't', ' '));
    if (size < 0)
        return -1;
    id = get_le16(pb); 
    channels = get_le16(pb);
    rate = get_le32(pb);
    bit_rate = get_le32(pb) * 8;
    get_le16(pb); /* block align */
    bps = get_le16(pb); /* bits per sample */
    if (size >= 18) {
        /* wav_extra_size */
        extra_size = get_le16(pb); 
        /* skip unused data */
        url_fseek(pb, size - 18, SEEK_CUR);
    }

    size = find_tag(pb, MKTAG('d', 'a', 't', 'a'));
    if (size < 0)
        return -1;
    
    /* now we are ready: build format streams */
    st = malloc(sizeof(AVStream));
    if (!st)
        return -1;
    s->nb_streams = 1;
    s->streams[0] = st;

    st->id = 0;
    
    st->codec.codec_type = CODEC_TYPE_AUDIO;
    st->codec.codec_tag = id;
    st->codec.codec_id = wav_codec_get_id(id, bps);
    st->codec.channels = channels;
    st->codec.sample_rate = rate;
    return 0;
}

#define MAX_SIZE 4096

static int wav_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    int packet_size, n, ret;

    if (url_feof(&s->pb))
        return -EIO;
    packet_size = url_get_packet_size(&s->pb);
    n = MAX_SIZE / packet_size;
    if (n <= 0)
        return n = 1;
    if (av_new_packet(pkt, n * packet_size))
        return -EIO;
    pkt->stream_index = 0;

    ret = get_buffer(&s->pb, pkt->data, pkt->size);
    if (ret < 0)
        av_free_packet(pkt);
    /* note: we need to modify the packet size here to handle the last
       packet */
    pkt->size = ret;
    return ret;
}

static int wav_read_close(AVFormatContext *s)
{
    return 0;
}

AVFormat wav_format = {
    "wav",
    "wav format",
    "audio/x-wav",
    "wav",
    CODEC_ID_PCM_S16LE,
    CODEC_ID_NONE,
    wav_write_header,
    wav_write_packet,
    wav_write_trailer,

    wav_read_header,
    wav_read_packet,
    wav_read_close,
};
