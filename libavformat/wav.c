/* 
 * WAV encoder and decoder
 * Copyright (c) 2001, 2002 Fabrice Bellard.
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "avformat.h"
#include "avi.h"

const CodecTag codec_wav_tags[] = {
    { CODEC_ID_MP2, 0x50 },
    { CODEC_ID_MP3, 0x55 },
    { CODEC_ID_AC3, 0x2000 },
    { CODEC_ID_PCM_S16LE, 0x01 },
    { CODEC_ID_PCM_U8, 0x01 }, /* must come after s16le in this list */
    { CODEC_ID_PCM_S24LE, 0x01 },
    { CODEC_ID_PCM_S32LE, 0x01 },
    { CODEC_ID_PCM_ALAW, 0x06 },
    { CODEC_ID_PCM_MULAW, 0x07 },
    { CODEC_ID_ADPCM_MS, 0x02 },
    { CODEC_ID_ADPCM_IMA_WAV, 0x11 },
    { CODEC_ID_ADPCM_YAMAHA, 0x20 },
    { CODEC_ID_ADPCM_G726, 0x45 },
    { CODEC_ID_ADPCM_IMA_DK4, 0x61 },  /* rogue format number */
    { CODEC_ID_ADPCM_IMA_DK3, 0x62 },  /* rogue format number */
    { CODEC_ID_WMAV1, 0x160 },
    { CODEC_ID_WMAV2, 0x161 },
    { CODEC_ID_AAC, 0x706d },
    { CODEC_ID_VORBIS, ('V'<<8)+'o' }, //HACK/FIXME, does vorbis in WAV/AVI have an (in)official id?
    { CODEC_ID_SONIC, 0x2048 },
    { CODEC_ID_SONIC_LS, 0x2048 },
    { CODEC_ID_ADPCM_CT, 0x200 },
    { CODEC_ID_ADPCM_SWF, ('S'<<8)+'F' },
    { 0, 0 },
};

#ifdef CONFIG_MUXERS
/* WAVEFORMATEX header */
/* returns the size or -1 on error */
int put_wav_header(ByteIOContext *pb, AVCodecContext *enc)
{
    int bps, blkalign, bytespersec;
    int hdrsize = 18;

    if(!enc->codec_tag)
       enc->codec_tag = codec_get_tag(codec_wav_tags, enc->codec_id);
    if(!enc->codec_tag)
        return -1;

    put_le16(pb, enc->codec_tag);
    put_le16(pb, enc->channels);
    put_le32(pb, enc->sample_rate);
    if (enc->codec_id == CODEC_ID_PCM_U8 ||
        enc->codec_id == CODEC_ID_PCM_ALAW ||
        enc->codec_id == CODEC_ID_PCM_MULAW) {
        bps = 8;
    } else if (enc->codec_id == CODEC_ID_MP2 || enc->codec_id == CODEC_ID_MP3) {
        bps = 0;
    } else if (enc->codec_id == CODEC_ID_ADPCM_IMA_WAV || enc->codec_id == CODEC_ID_ADPCM_MS || enc->codec_id == CODEC_ID_ADPCM_G726 || enc->codec_id == CODEC_ID_ADPCM_YAMAHA) { //
        bps = 4;
    } else if (enc->codec_id == CODEC_ID_PCM_S24LE) {
        bps = 24;
    } else if (enc->codec_id == CODEC_ID_PCM_S32LE) {
        bps = 32;
    } else {
        bps = 16;
    }
    
    if (enc->codec_id == CODEC_ID_MP2 || enc->codec_id == CODEC_ID_MP3) {
        blkalign = enc->frame_size; //this is wrong, but seems many demuxers dont work if this is set correctly
        //blkalign = 144 * enc->bit_rate/enc->sample_rate;
    } else if (enc->codec_id == CODEC_ID_ADPCM_G726) { //
        blkalign = 1;
    } else if (enc->block_align != 0) { /* specified by the codec */
        blkalign = enc->block_align;
    } else
        blkalign = enc->channels*bps >> 3;
    if (enc->codec_id == CODEC_ID_PCM_U8 ||
        enc->codec_id == CODEC_ID_PCM_S24LE ||
        enc->codec_id == CODEC_ID_PCM_S32LE ||
        enc->codec_id == CODEC_ID_PCM_S16LE) {
        bytespersec = enc->sample_rate * blkalign;
    } else {
        bytespersec = enc->bit_rate / 8;
    }
    put_le32(pb, bytespersec); /* bytes per second */
    put_le16(pb, blkalign); /* block align */
    put_le16(pb, bps); /* bits per sample */
    if (enc->codec_id == CODEC_ID_MP3) {
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
    } else if (enc->codec_id == CODEC_ID_ADPCM_IMA_WAV) {
        put_le16(pb, 2); /* wav_extra_size */
        hdrsize += 2;
        put_le16(pb, ((enc->block_align - 4 * enc->channels) / (4 * enc->channels)) * 8 + 1); /* wSamplesPerBlock */
    } else if(enc->extradata_size){
        put_le16(pb, enc->extradata_size);
        put_buffer(pb, enc->extradata, enc->extradata_size);
        hdrsize += enc->extradata_size;
        if(hdrsize&1){
            hdrsize++;
            put_byte(pb, 0);
        }
    } else {
        hdrsize -= 2;
    }

    return hdrsize;
}
#endif //CONFIG_MUXERS

/* We could be given one of the three possible structures here:
 * WAVEFORMAT, PCMWAVEFORMAT or WAVEFORMATEX. Each structure
 * is an expansion of the previous one with the fields added
 * at the bottom. PCMWAVEFORMAT adds 'WORD wBitsPerSample' and
 * WAVEFORMATEX adds 'WORD  cbSize' and basically makes itself
 * an openended structure.
 */
void get_wav_header(ByteIOContext *pb, AVCodecContext *codec, int size) 
{
    int id;

    id = get_le16(pb);
    codec->codec_type = CODEC_TYPE_AUDIO;
    codec->codec_tag = id;
    codec->channels = get_le16(pb);
    codec->sample_rate = get_le32(pb);
    codec->bit_rate = get_le32(pb) * 8;
    codec->block_align = get_le16(pb);
    if (size == 14) {  /* We're dealing with plain vanilla WAVEFORMAT */
        codec->bits_per_sample = 8;
    }else
        codec->bits_per_sample = get_le16(pb);
    codec->codec_id = wav_codec_get_id(id, codec->bits_per_sample);
    
    if (size > 16) {  /* We're obviously dealing with WAVEFORMATEX */
	codec->extradata_size = get_le16(pb);
	if (codec->extradata_size > 0) {
	    if (codec->extradata_size > size - 18)
	        codec->extradata_size = size - 18;
            codec->extradata = av_mallocz(codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
            get_buffer(pb, codec->extradata, codec->extradata_size);
        } else
	    codec->extradata_size = 0;
	
	/* It is possible for the chunk to contain garbage at the end */
	if (size - codec->extradata_size - 18 > 0)
	    url_fskip(pb, size - codec->extradata_size - 18);
    }
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
    if (id == CODEC_ID_PCM_S16LE && bps == 24)
        id = CODEC_ID_PCM_S24LE;
    if (id == CODEC_ID_PCM_S16LE && bps == 32)
        id = CODEC_ID_PCM_S32LE;
    return id;
}

#ifdef CONFIG_MUXERS
typedef struct {
    offset_t data;
} WAVContext;

static int wav_write_header(AVFormatContext *s)
{
    WAVContext *wav = s->priv_data;
    ByteIOContext *pb = &s->pb;
    offset_t fmt;

    put_tag(pb, "RIFF");
    put_le32(pb, 0); /* file length */
    put_tag(pb, "WAVE");

    /* format header */
    fmt = start_tag(pb, "fmt ");
    if (put_wav_header(pb, s->streams[0]->codec) < 0) {
        av_free(wav);
        return -1;
    }
    end_tag(pb, fmt);

    av_set_pts_info(s->streams[0], 64, 1, s->streams[0]->codec->sample_rate);

    /* data header */
    wav->data = start_tag(pb, "data");
    
    put_flush_packet(pb);

    return 0;
}

static int wav_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    ByteIOContext *pb = &s->pb;
    put_buffer(pb, pkt->data, pkt->size);
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
        put_le32(pb, (uint32_t)(file_size - 8));
        url_fseek(pb, file_size, SEEK_SET);

        put_flush_packet(pb);
    }
    return 0;
}
#endif //CONFIG_MUXERS

/* return the size of the found tag */
/* XXX: > 2GB ? */
static int find_tag(ByteIOContext *pb, uint32_t tag1)
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

static int wav_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if (p->buf[0] == 'R' && p->buf[1] == 'I' &&
        p->buf[2] == 'F' && p->buf[3] == 'F' &&
        p->buf[8] == 'W' && p->buf[9] == 'A' &&
        p->buf[10] == 'V' && p->buf[11] == 'E')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

/* wav input */
static int wav_read_header(AVFormatContext *s,
                           AVFormatParameters *ap)
{
    int size;
    unsigned int tag;
    ByteIOContext *pb = &s->pb;
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
    st = av_new_stream(s, 0);
    if (!st)
        return AVERROR_NOMEM;

    get_wav_header(pb, st->codec, size);
    st->need_parsing = 1;

    av_set_pts_info(st, 64, 1, st->codec->sample_rate);

    size = find_tag(pb, MKTAG('d', 'a', 't', 'a'));
    if (size < 0)
        return -1;
    return 0;
}

#define MAX_SIZE 4096

static int wav_read_packet(AVFormatContext *s,
                           AVPacket *pkt)
{
    int ret, size;
    AVStream *st;

    if (url_feof(&s->pb))
        return AVERROR_IO;
    st = s->streams[0];

    size = MAX_SIZE;
    if (st->codec->block_align > 1) {
        if (size < st->codec->block_align)
            size = st->codec->block_align;
        size = (size / st->codec->block_align) * st->codec->block_align;
    }
    if (av_new_packet(pkt, size))
        return AVERROR_IO;
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

static int wav_read_seek(AVFormatContext *s, 
                         int stream_index, int64_t timestamp, int flags)
{
    AVStream *st;

    st = s->streams[0];
    switch(st->codec->codec_id) {
    case CODEC_ID_MP2:
    case CODEC_ID_MP3:
    case CODEC_ID_AC3:
    case CODEC_ID_DTS:
        /* use generic seeking with dynamically generated indexes */
        return -1;
    default:
        break;
    }
    return pcm_read_seek(s, stream_index, timestamp, flags);
}


static AVInputFormat wav_iformat = {
    "wav",
    "wav format",
    0,
    wav_probe,
    wav_read_header,
    wav_read_packet,
    wav_read_close,
    wav_read_seek,
};

#ifdef CONFIG_MUXERS
static AVOutputFormat wav_oformat = {
    "wav",
    "wav format",
    "audio/x-wav",
    "wav",
    sizeof(WAVContext),
    CODEC_ID_PCM_S16LE,
    CODEC_ID_NONE,
    wav_write_header,
    wav_write_packet,
    wav_write_trailer,
};
#endif //CONFIG_MUXERS

int ff_wav_init(void)
{
    av_register_input_format(&wav_iformat);
#ifdef CONFIG_MUXERS
    av_register_output_format(&wav_oformat);
#endif //CONFIG_MUXERS
    return 0;
}
