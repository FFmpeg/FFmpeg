/*
 * "Real" compatible mux and demux.
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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

/* in ms */
#define BUFFER_DURATION 0 

typedef struct {
    int nb_packets;
    int packet_total_size;
    int packet_max_size;
    /* codec related output */
    int bit_rate;
    float frame_rate;
    int nb_frames;    /* current frame number */
    int total_frames; /* total number of frames */
    int num;
    AVCodecContext *enc;
} StreamInfo;

typedef struct {
    StreamInfo streams[2];
    StreamInfo *audio_stream, *video_stream;
    int data_pos; /* position of the data after the header */
    int nb_packets;
    int old_format;
    int current_stream;
    int remaining_len;
} RMContext;

#ifdef CONFIG_MUXERS
static void put_str(ByteIOContext *s, const char *tag)
{
    put_be16(s,strlen(tag));
    while (*tag) {
        put_byte(s, *tag++);
    }
}

static void put_str8(ByteIOContext *s, const char *tag)
{
    put_byte(s, strlen(tag));
    while (*tag) {
        put_byte(s, *tag++);
    }
}

static void rv10_write_header(AVFormatContext *ctx, 
                              int data_size, int index_pos)
{
    RMContext *rm = ctx->priv_data;
    ByteIOContext *s = &ctx->pb;
    StreamInfo *stream;
    unsigned char *data_offset_ptr, *start_ptr;
    const char *desc, *mimetype;
    int nb_packets, packet_total_size, packet_max_size, size, packet_avg_size, i;
    int bit_rate, v, duration, flags, data_pos;

    start_ptr = s->buf_ptr;

    put_tag(s, ".RMF");
    put_be32(s,18); /* header size */
    put_be16(s,0);
    put_be32(s,0);
    put_be32(s,4 + ctx->nb_streams); /* num headers */

    put_tag(s,"PROP");
    put_be32(s, 50);
    put_be16(s, 0);
    packet_max_size = 0;
    packet_total_size = 0;
    nb_packets = 0;
    bit_rate = 0;
    duration = 0;
    for(i=0;i<ctx->nb_streams;i++) {
        StreamInfo *stream = &rm->streams[i];
        bit_rate += stream->bit_rate;
        if (stream->packet_max_size > packet_max_size)
            packet_max_size = stream->packet_max_size;
        nb_packets += stream->nb_packets;
        packet_total_size += stream->packet_total_size;
        /* select maximum duration */
        v = (int) (1000.0 * (float)stream->total_frames / stream->frame_rate);
        if (v > duration)
            duration = v;
    }
    put_be32(s, bit_rate); /* max bit rate */
    put_be32(s, bit_rate); /* avg bit rate */
    put_be32(s, packet_max_size);        /* max packet size */
    if (nb_packets > 0)
        packet_avg_size = packet_total_size / nb_packets;
    else
        packet_avg_size = 0;
    put_be32(s, packet_avg_size);        /* avg packet size */
    put_be32(s, nb_packets);  /* num packets */
    put_be32(s, duration); /* duration */
    put_be32(s, BUFFER_DURATION);           /* preroll */
    put_be32(s, index_pos);           /* index offset */
    /* computation of data the data offset */
    data_offset_ptr = s->buf_ptr;
    put_be32(s, 0);           /* data offset : will be patched after */
    put_be16(s, ctx->nb_streams);    /* num streams */
    flags = 1 | 2; /* save allowed & perfect play */
    if (url_is_streamed(s))
        flags |= 4; /* live broadcast */
    put_be16(s, flags);
    
    /* comments */

    put_tag(s,"CONT");
    size = strlen(ctx->title) + strlen(ctx->author) + strlen(ctx->copyright) + 
        strlen(ctx->comment) + 4 * 2 + 10;
    put_be32(s,size);
    put_be16(s,0);
    put_str(s, ctx->title);
    put_str(s, ctx->author);
    put_str(s, ctx->copyright);
    put_str(s, ctx->comment);
    
    for(i=0;i<ctx->nb_streams;i++) {
        int codec_data_size;

        stream = &rm->streams[i];
        
        if (stream->enc->codec_type == CODEC_TYPE_AUDIO) {
            desc = "The Audio Stream";
            mimetype = "audio/x-pn-realaudio";
            codec_data_size = 73;
        } else {
            desc = "The Video Stream";
            mimetype = "video/x-pn-realvideo";
            codec_data_size = 34;
        }

        put_tag(s,"MDPR");
        size = 10 + 9 * 4 + strlen(desc) + strlen(mimetype) + codec_data_size;
        put_be32(s, size);
        put_be16(s, 0);

        put_be16(s, i); /* stream number */
        put_be32(s, stream->bit_rate); /* max bit rate */
        put_be32(s, stream->bit_rate); /* avg bit rate */
        put_be32(s, stream->packet_max_size);        /* max packet size */
        if (stream->nb_packets > 0)
            packet_avg_size = stream->packet_total_size / 
                stream->nb_packets;
        else
            packet_avg_size = 0;
        put_be32(s, packet_avg_size);        /* avg packet size */
        put_be32(s, 0);           /* start time */
        put_be32(s, BUFFER_DURATION);           /* preroll */
        /* duration */
        if (url_is_streamed(s) || !stream->total_frames)
            put_be32(s, (int)(3600 * 1000));
        else
            put_be32(s, (int)(stream->total_frames * 1000 / stream->frame_rate));
        put_str8(s, desc);
        put_str8(s, mimetype);
        put_be32(s, codec_data_size);
        
        if (stream->enc->codec_type == CODEC_TYPE_AUDIO) {
            int coded_frame_size, fscode, sample_rate;
            sample_rate = stream->enc->sample_rate;
            coded_frame_size = (stream->enc->bit_rate * 
                                stream->enc->frame_size) / (8 * sample_rate);
            /* audio codec info */
            put_tag(s, ".ra");
            put_byte(s, 0xfd);
            put_be32(s, 0x00040000); /* version */
            put_tag(s, ".ra4");
            put_be32(s, 0x01b53530); /* stream length */
            put_be16(s, 4); /* unknown */
            put_be32(s, 0x39); /* header size */

            switch(sample_rate) {
            case 48000:
            case 24000:
            case 12000:
                fscode = 1;
                break;
            default:
            case 44100:
            case 22050:
            case 11025:
                fscode = 2;
                break;
            case 32000:
            case 16000:
            case 8000:
                fscode = 3;
            }
            put_be16(s, fscode); /* codec additional info, for AC3, seems
                                     to be a frequency code */
            /* special hack to compensate rounding errors... */
            if (coded_frame_size == 557)
                coded_frame_size--;
            put_be32(s, coded_frame_size); /* frame length */
            put_be32(s, 0x51540); /* unknown */
            put_be32(s, 0x249f0); /* unknown */
            put_be32(s, 0x249f0); /* unknown */
            put_be16(s, 0x01);
            /* frame length : seems to be very important */
            put_be16(s, coded_frame_size); 
            put_be32(s, 0); /* unknown */
            put_be16(s, stream->enc->sample_rate); /* sample rate */
            put_be32(s, 0x10); /* unknown */
            put_be16(s, stream->enc->channels);
            put_str8(s, "Int0"); /* codec name */
            put_str8(s, "dnet"); /* codec name */
            put_be16(s, 0); /* title length */
            put_be16(s, 0); /* author length */
            put_be16(s, 0); /* copyright length */
            put_byte(s, 0); /* end of header */
        } else {
            /* video codec info */
            put_be32(s,34); /* size */
            if(stream->enc->codec_id == CODEC_ID_RV10)
                put_tag(s,"VIDORV10");
            else
                put_tag(s,"VIDORV20");
            put_be16(s, stream->enc->width);
            put_be16(s, stream->enc->height);
            put_be16(s, (int) stream->frame_rate); /* frames per seconds ? */
            put_be32(s,0);     /* unknown meaning */
            put_be16(s, (int) stream->frame_rate);  /* unknown meaning */
            put_be32(s,0);     /* unknown meaning */
            put_be16(s, 8);    /* unknown meaning */
            /* Seems to be the codec version: only use basic H263. The next
               versions seems to add a diffential DC coding as in
               MPEG... nothing new under the sun */
            if(stream->enc->codec_id == CODEC_ID_RV10)
                put_be32(s,0x10000000); 
            else
                put_be32(s,0x20103001); 
            //put_be32(s,0x10003000); 
        }
    }

    /* patch data offset field */
    data_pos = s->buf_ptr - start_ptr;
    rm->data_pos = data_pos;
    data_offset_ptr[0] = data_pos >> 24;
    data_offset_ptr[1] = data_pos >> 16;
    data_offset_ptr[2] = data_pos >> 8;
    data_offset_ptr[3] = data_pos;
    
    /* data stream */
    put_tag(s,"DATA");
    put_be32(s,data_size + 10 + 8);
    put_be16(s,0);

    put_be32(s, nb_packets); /* number of packets */
    put_be32(s,0); /* next data header */
}

static void write_packet_header(AVFormatContext *ctx, StreamInfo *stream, 
                                int length, int key_frame)
{
    int timestamp;
    ByteIOContext *s = &ctx->pb;

    stream->nb_packets++;
    stream->packet_total_size += length;
    if (length > stream->packet_max_size)
        stream->packet_max_size =  length;

    put_be16(s,0); /* version */
    put_be16(s,length + 12);
    put_be16(s, stream->num); /* stream number */
    timestamp = (1000 * (float)stream->nb_frames) / stream->frame_rate;
    put_be32(s, timestamp); /* timestamp */
    put_byte(s, 0); /* reserved */
    put_byte(s, key_frame ? 2 : 0); /* flags */
}

static int rm_write_header(AVFormatContext *s)
{
    RMContext *rm = s->priv_data;
    StreamInfo *stream;
    int n;
    AVCodecContext *codec;

    for(n=0;n<s->nb_streams;n++) {
        s->streams[n]->id = n;
        codec = s->streams[n]->codec;
        stream = &rm->streams[n];
        memset(stream, 0, sizeof(StreamInfo));
        stream->num = n;
        stream->bit_rate = codec->bit_rate;
        stream->enc = codec;

        switch(codec->codec_type) {
        case CODEC_TYPE_AUDIO:
            rm->audio_stream = stream;
            stream->frame_rate = (float)codec->sample_rate / (float)codec->frame_size;
            /* XXX: dummy values */
            stream->packet_max_size = 1024;
            stream->nb_packets = 0;
            stream->total_frames = stream->nb_packets;
            break;
        case CODEC_TYPE_VIDEO:
            rm->video_stream = stream;
            stream->frame_rate = (float)codec->time_base.den / (float)codec->time_base.num;
            /* XXX: dummy values */
            stream->packet_max_size = 4096;
            stream->nb_packets = 0;
            stream->total_frames = stream->nb_packets;
            break;
        default:
            return -1;
        }
    }

    rv10_write_header(s, 0, 0);
    put_flush_packet(&s->pb);
    return 0;
}

static int rm_write_audio(AVFormatContext *s, const uint8_t *buf, int size, int flags)
{
    uint8_t *buf1;
    RMContext *rm = s->priv_data;
    ByteIOContext *pb = &s->pb;
    StreamInfo *stream = rm->audio_stream;
    int i;

    /* XXX: suppress this malloc */
    buf1= (uint8_t*) av_malloc( size * sizeof(uint8_t) );
    
    write_packet_header(s, stream, size, !!(flags & PKT_FLAG_KEY));
    
    /* for AC3, the words seems to be reversed */
    for(i=0;i<size;i+=2) {
        buf1[i] = buf[i+1];
        buf1[i+1] = buf[i];
    }
    put_buffer(pb, buf1, size);
    put_flush_packet(pb);
    stream->nb_frames++;
    av_free(buf1);
    return 0;
}

static int rm_write_video(AVFormatContext *s, const uint8_t *buf, int size, int flags)
{
    RMContext *rm = s->priv_data;
    ByteIOContext *pb = &s->pb;
    StreamInfo *stream = rm->video_stream;
    int key_frame = !!(flags & PKT_FLAG_KEY);

    /* XXX: this is incorrect: should be a parameter */

    /* Well, I spent some time finding the meaning of these bits. I am
       not sure I understood everything, but it works !! */
#if 1
    write_packet_header(s, stream, size + 7, key_frame);
    /* bit 7: '1' if final packet of a frame converted in several packets */
    put_byte(pb, 0x81); 
    /* bit 7: '1' if I frame. bits 6..0 : sequence number in current
       frame starting from 1 */
    if (key_frame) {
        put_byte(pb, 0x81); 
    } else {
        put_byte(pb, 0x01); 
    }
    put_be16(pb, 0x4000 + (size)); /* total frame size */
    put_be16(pb, 0x4000 + (size));              /* offset from the start or the end */
#else
    /* full frame */
    write_packet_header(s, size + 6);
    put_byte(pb, 0xc0); 
    put_be16(pb, 0x4000 + size); /* total frame size */
    put_be16(pb, 0x4000 + packet_number * 126); /* position in stream */
#endif
    put_byte(pb, stream->nb_frames & 0xff); 
    
    put_buffer(pb, buf, size);
    put_flush_packet(pb);

    stream->nb_frames++;
    return 0;
}

static int rm_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    if (s->streams[pkt->stream_index]->codec->codec_type == 
        CODEC_TYPE_AUDIO)
        return rm_write_audio(s, pkt->data, pkt->size, pkt->flags);
    else
        return rm_write_video(s, pkt->data, pkt->size, pkt->flags);
}
        
static int rm_write_trailer(AVFormatContext *s)
{
    RMContext *rm = s->priv_data;
    int data_size, index_pos, i;
    ByteIOContext *pb = &s->pb;

    if (!url_is_streamed(&s->pb)) {
        /* end of file: finish to write header */
        index_pos = url_fseek(pb, 0, SEEK_CUR);
        data_size = index_pos - rm->data_pos;

        /* index */
        put_tag(pb, "INDX");
        put_be32(pb, 10 + 10 * s->nb_streams);
        put_be16(pb, 0);
        
        for(i=0;i<s->nb_streams;i++) {
            put_be32(pb, 0); /* zero indices */
            put_be16(pb, i); /* stream number */
            put_be32(pb, 0); /* next index */
        }
        /* undocumented end header */
        put_be32(pb, 0);
        put_be32(pb, 0);
        
        url_fseek(pb, 0, SEEK_SET);
        for(i=0;i<s->nb_streams;i++)
            rm->streams[i].total_frames = rm->streams[i].nb_frames;
        rv10_write_header(s, data_size, index_pos);
    } else {
        /* undocumented end header */
        put_be32(pb, 0);
        put_be32(pb, 0);
    }
    put_flush_packet(pb);
    return 0;
}
#endif //CONFIG_MUXERS

/***************************************************/

static void get_str(ByteIOContext *pb, char *buf, int buf_size)
{
    int len, i;
    char *q;

    len = get_be16(pb);
    q = buf;
    for(i=0;i<len;i++) {
        if (i < buf_size - 1)
            *q++ = get_byte(pb);
    }
    *q = '\0';
}

static void get_str8(ByteIOContext *pb, char *buf, int buf_size)
{
    int len, i;
    char *q;

    len = get_byte(pb);
    q = buf;
    for(i=0;i<len;i++) {
        if (i < buf_size - 1)
            *q++ = get_byte(pb);
    }
    *q = '\0';
}

static void rm_read_audio_stream_info(AVFormatContext *s, AVStream *st, 
                                      int read_all)
{
    ByteIOContext *pb = &s->pb;
    char buf[128];
    uint32_t version;
    int i;

    /* ra type header */
    version = get_be32(pb); /* version */
    if (((version >> 16) & 0xff) == 3) {
        /* very old version */
        for(i = 0; i < 14; i++)
            get_byte(pb);
        get_str8(pb, s->title, sizeof(s->title));
        get_str8(pb, s->author, sizeof(s->author));
        get_str8(pb, s->copyright, sizeof(s->copyright));
        get_str8(pb, s->comment, sizeof(s->comment));
        get_byte(pb);
        get_str8(pb, buf, sizeof(buf));
        st->codec->sample_rate = 8000;
        st->codec->channels = 1;
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        st->codec->codec_id = CODEC_ID_RA_144;
    } else {
        int flavor, sub_packet_h, coded_framesize;
        /* old version (4) */
        get_be32(pb); /* .ra4 */
        get_be32(pb); /* data size */
        get_be16(pb); /* version2 */
        get_be32(pb); /* header size */
        flavor= get_be16(pb); /* add codec info / flavor */
        coded_framesize= get_be32(pb); /* coded frame size */
        get_be32(pb); /* ??? */
        get_be32(pb); /* ??? */
        get_be32(pb); /* ??? */
        sub_packet_h= get_be16(pb); /* 1 */ 
        st->codec->block_align= get_be16(pb); /* frame size */
        get_be16(pb); /* sub packet size */
        get_be16(pb); /* ??? */
        st->codec->sample_rate = get_be16(pb);
        get_be32(pb);
        st->codec->channels = get_be16(pb);
        get_str8(pb, buf, sizeof(buf)); /* desc */
        get_str8(pb, buf, sizeof(buf)); /* desc */
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        if (!strcmp(buf, "dnet")) {
            st->codec->codec_id = CODEC_ID_AC3;
        } else if (!strcmp(buf, "28_8")) {
            st->codec->codec_id = CODEC_ID_RA_288;
            st->codec->extradata_size= 10;
            st->codec->extradata= av_mallocz(st->codec->extradata_size);
            /* this is completly braindead and broken, the idiot who added this codec and endianness
               specific reordering to mplayer and libavcodec/ra288.c should be drowned in a see of cola */
            //FIXME pass the unpermutated extradata
            ((uint16_t*)st->codec->extradata)[1]= sub_packet_h;
            ((uint16_t*)st->codec->extradata)[2]= flavor;
            ((uint16_t*)st->codec->extradata)[3]= coded_framesize;
        } else {
            st->codec->codec_id = CODEC_ID_NONE;
            pstrcpy(st->codec->codec_name, sizeof(st->codec->codec_name),
                    buf);
        }
        if (read_all) {
            get_byte(pb);
            get_byte(pb);
            get_byte(pb);
            
            get_str8(pb, s->title, sizeof(s->title));
            get_str8(pb, s->author, sizeof(s->author));
            get_str8(pb, s->copyright, sizeof(s->copyright));
            get_str8(pb, s->comment, sizeof(s->comment));
        }
    }
}

static int rm_read_header_old(AVFormatContext *s, AVFormatParameters *ap)
{
    RMContext *rm = s->priv_data;
    AVStream *st;

    rm->old_format = 1;
    st = av_new_stream(s, 0);
    if (!st)
        goto fail;
    rm_read_audio_stream_info(s, st, 1);
    return 0;
 fail:
    return -1;
}

static int rm_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    RMContext *rm = s->priv_data;
    AVStream *st;
    ByteIOContext *pb = &s->pb;
    unsigned int tag, v;
    int tag_size, size, codec_data_size, i;
    int64_t codec_pos;
    unsigned int h263_hack_version, start_time, duration;
    char buf[128];
    int flags = 0;

    tag = get_le32(pb);
    if (tag == MKTAG('.', 'r', 'a', 0xfd)) {
        /* very old .ra format */
        return rm_read_header_old(s, ap);
    } else if (tag != MKTAG('.', 'R', 'M', 'F')) {
        return AVERROR_IO;
    }

    get_be32(pb); /* header size */
    get_be16(pb);
    get_be32(pb);
    get_be32(pb); /* number of headers */
    
    for(;;) {
        if (url_feof(pb))
            goto fail;
        tag = get_le32(pb);
        tag_size = get_be32(pb);
        get_be16(pb);
#if 0
        printf("tag=%c%c%c%c (%08x) size=%d\n", 
               (tag) & 0xff,
               (tag >> 8) & 0xff,
               (tag >> 16) & 0xff,
               (tag >> 24) & 0xff,
               tag,
               tag_size);
#endif
        if (tag_size < 10 && tag != MKTAG('D', 'A', 'T', 'A'))
            goto fail;
        switch(tag) {
        case MKTAG('P', 'R', 'O', 'P'):
            /* file header */
            get_be32(pb); /* max bit rate */
            get_be32(pb); /* avg bit rate */
            get_be32(pb); /* max packet size */
            get_be32(pb); /* avg packet size */
            get_be32(pb); /* nb packets */
            get_be32(pb); /* duration */
            get_be32(pb); /* preroll */
            get_be32(pb); /* index offset */
            get_be32(pb); /* data offset */
            get_be16(pb); /* nb streams */
            flags = get_be16(pb); /* flags */
            break;
        case MKTAG('C', 'O', 'N', 'T'):
            get_str(pb, s->title, sizeof(s->title));
            get_str(pb, s->author, sizeof(s->author));
            get_str(pb, s->copyright, sizeof(s->copyright));
            get_str(pb, s->comment, sizeof(s->comment));
            break;
        case MKTAG('M', 'D', 'P', 'R'):
            st = av_new_stream(s, 0);
            if (!st)
                goto fail;
            st->id = get_be16(pb);
            get_be32(pb); /* max bit rate */
            st->codec->bit_rate = get_be32(pb); /* bit rate */
            get_be32(pb); /* max packet size */
            get_be32(pb); /* avg packet size */
            start_time = get_be32(pb); /* start time */
            get_be32(pb); /* preroll */
            duration = get_be32(pb); /* duration */
            st->start_time = start_time;
            st->duration = duration;
            get_str8(pb, buf, sizeof(buf)); /* desc */
            get_str8(pb, buf, sizeof(buf)); /* mimetype */
            codec_data_size = get_be32(pb);
            codec_pos = url_ftell(pb);
            st->codec->codec_type = CODEC_TYPE_DATA;
            av_set_pts_info(st, 64, 1, 1000);

            v = get_be32(pb);
            if (v == MKTAG(0xfd, 'a', 'r', '.')) {
                /* ra type header */
                rm_read_audio_stream_info(s, st, 0);
            } else {
                int fps, fps2;
                if (get_le32(pb) != MKTAG('V', 'I', 'D', 'O')) {
                fail1:
                    av_log(st->codec, AV_LOG_ERROR, "Unsupported video codec\n");
                    goto skip;
                }
                st->codec->codec_tag = get_le32(pb);
//                av_log(NULL, AV_LOG_DEBUG, "%X %X\n", st->codec->codec_tag, MKTAG('R', 'V', '2', '0'));
                if (   st->codec->codec_tag != MKTAG('R', 'V', '1', '0')
                    && st->codec->codec_tag != MKTAG('R', 'V', '2', '0')
                    && st->codec->codec_tag != MKTAG('R', 'V', '3', '0')
                    && st->codec->codec_tag != MKTAG('R', 'V', '4', '0'))
                    goto fail1;
                st->codec->width = get_be16(pb);
                st->codec->height = get_be16(pb);
                st->codec->time_base.num= 1;
                fps= get_be16(pb);
                st->codec->codec_type = CODEC_TYPE_VIDEO;
                get_be32(pb);
                fps2= get_be16(pb);
                get_be16(pb);
                
                st->codec->extradata_size= codec_data_size - (url_ftell(pb) - codec_pos);
                st->codec->extradata= av_malloc(st->codec->extradata_size);
                get_buffer(pb, st->codec->extradata, st->codec->extradata_size);
                
//                av_log(NULL, AV_LOG_DEBUG, "fps= %d fps2= %d\n", fps, fps2);
                st->codec->time_base.den = fps * st->codec->time_base.num;
                /* modification of h263 codec version (!) */
#ifdef WORDS_BIGENDIAN
                h263_hack_version = ((uint32_t*)st->codec->extradata)[1];
#else
                h263_hack_version = bswap_32(((uint32_t*)st->codec->extradata)[1]);
#endif
                st->codec->sub_id = h263_hack_version;
                switch((h263_hack_version>>28)){
                case 1: st->codec->codec_id = CODEC_ID_RV10; break;
                case 2: st->codec->codec_id = CODEC_ID_RV20; break;
                case 3: st->codec->codec_id = CODEC_ID_RV30; break;
                case 4: st->codec->codec_id = CODEC_ID_RV40; break;
                default: goto fail1;
                }
            }
skip:
            /* skip codec info */
            size = url_ftell(pb) - codec_pos;
            url_fskip(pb, codec_data_size - size);
            break;
        case MKTAG('D', 'A', 'T', 'A'):
            goto header_end;
        default:
            /* unknown tag: skip it */
            url_fskip(pb, tag_size - 10);
            break;
        }
    }
 header_end:
    rm->nb_packets = get_be32(pb); /* number of packets */
    if (!rm->nb_packets && (flags & 4))
        rm->nb_packets = 3600 * 25;
    get_be32(pb); /* next data header */
    return 0;

 fail:
    for(i=0;i<s->nb_streams;i++) {
        av_free(s->streams[i]);
    }
    return AVERROR_IO;
}

static int get_num(ByteIOContext *pb, int *len)
{
    int n, n1;

    n = get_be16(pb);
    (*len)-=2;
    if (n >= 0x4000) {
        return n - 0x4000;
    } else {
        n1 = get_be16(pb);
        (*len)-=2;
        return (n << 16) | n1;
    }
}

/* multiple of 20 bytes for ra144 (ugly) */
#define RAW_PACKET_SIZE 1000

static int sync(AVFormatContext *s, int64_t *timestamp, int *flags, int *stream_index, int64_t *pos){
    RMContext *rm = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int len, num, res, i;
    AVStream *st;
    uint32_t state=0xFFFFFFFF;

    while(!url_feof(pb)){
        *pos= url_ftell(pb);
        if(rm->remaining_len > 0){
            num= rm->current_stream;
            len= rm->remaining_len;
            *timestamp = AV_NOPTS_VALUE;
            *flags= 0;
        }else{
            state= (state<<8) + get_byte(pb);
            
            if(state == MKBETAG('I', 'N', 'D', 'X')){
                len = get_be16(pb) - 6;
                if(len<0)
                    continue;
                goto skip;
            }
            
            if(state > (unsigned)0xFFFF || state < 12)
                continue;
            len=state;
            state= 0xFFFFFFFF;

            num = get_be16(pb);
            *timestamp = get_be32(pb);
            res= get_byte(pb); /* reserved */
            *flags = get_byte(pb); /* flags */

            
            len -= 12;
        }
        for(i=0;i<s->nb_streams;i++) {
            st = s->streams[i];
            if (num == st->id)
                break;
        }
        if (i == s->nb_streams) {
skip:
            /* skip packet if unknown number */
            url_fskip(pb, len);
            rm->remaining_len -= len;
            continue;
        }
        *stream_index= i;
        
        return len;
    }
    return -1;
}

static int rm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    RMContext *rm = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVStream *st;
    int i, len, tmp, j;
    int64_t timestamp, pos;
    uint8_t *ptr;
    int flags;

    if (rm->old_format) {
        /* just read raw bytes */
        len = RAW_PACKET_SIZE;
        len= av_get_packet(pb, pkt, len);
        pkt->stream_index = 0;
        if (len <= 0) {
            return AVERROR_IO;
        }
        pkt->size = len;
        st = s->streams[0];
    } else {
        int seq=1;
resync:
        len=sync(s, &timestamp, &flags, &i, &pos);
        if(len<0)
            return AVERROR_IO;
        st = s->streams[i];

        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            int h, pic_num, len2, pos;

            h= get_byte(pb); len--;
            if(!(h & 0x40)){
                seq = get_byte(pb); len--;
            }

            if((h & 0xc0) == 0x40){
                len2= pos= 0;
            }else{
                len2 = get_num(pb, &len);
                pos = get_num(pb, &len);
            }
            /* picture number */
            pic_num= get_byte(pb); len--;
            rm->remaining_len= len;
            rm->current_stream= st->id;

//            av_log(NULL, AV_LOG_DEBUG, "%X len:%d pos:%d len2:%d pic_num:%d\n",h, len, pos, len2, pic_num);
            if(len2 && len2<len)
                len=len2;
            rm->remaining_len-= len;
        }

        if(  (st->discard >= AVDISCARD_NONKEY && !(flags&2))
           || st->discard >= AVDISCARD_ALL){
            url_fskip(pb, len);
            goto resync;
        }
        
        av_get_packet(pb, pkt, len);
        pkt->stream_index = i;

#if 0
        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            if(st->codec->codec_id == CODEC_ID_RV20){
                int seq= 128*(pkt->data[2]&0x7F) + (pkt->data[3]>>1);
                av_log(NULL, AV_LOG_DEBUG, "%d %Ld %d\n", timestamp, timestamp*512LL/25, seq);

                seq |= (timestamp&~0x3FFF);
                if(seq - timestamp >  0x2000) seq -= 0x4000;
                if(seq - timestamp < -0x2000) seq += 0x4000;
            }
        }
#endif
        pkt->pts= timestamp;
        if(flags&2){
            pkt->flags |= PKT_FLAG_KEY;
            if((seq&0x7F) == 1)
                av_add_index_entry(st, pos, timestamp, 0, AVINDEX_KEYFRAME);
        }
    }

    /* for AC3, needs to swap bytes */
    if (st->codec->codec_id == CODEC_ID_AC3) {
        ptr = pkt->data;
        for(j=0;j<len;j+=2) {
            tmp = ptr[0];
            ptr[0] = ptr[1];
            ptr[1] = tmp;
                ptr += 2;
        }
    }
    return 0;
}

static int rm_read_close(AVFormatContext *s)
{
    return 0;
}

static int rm_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if ((p->buf[0] == '.' && p->buf[1] == 'R' &&
         p->buf[2] == 'M' && p->buf[3] == 'F' &&
         p->buf[4] == 0 && p->buf[5] == 0) ||
        (p->buf[0] == '.' && p->buf[1] == 'r' &&
         p->buf[2] == 'a' && p->buf[3] == 0xfd))
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int64_t rm_read_dts(AVFormatContext *s, int stream_index, 
                               int64_t *ppos, int64_t pos_limit)
{
    RMContext *rm = s->priv_data;
    int64_t pos, dts;
    int stream_index2, flags, len, h;

    pos = *ppos;
    
    if(rm->old_format)
        return AV_NOPTS_VALUE;

    url_fseek(&s->pb, pos, SEEK_SET);
    rm->remaining_len=0;
    for(;;){
        int seq=1;
        AVStream *st;

        len=sync(s, &dts, &flags, &stream_index2, &pos);
        if(len<0)
            return AV_NOPTS_VALUE;

        st = s->streams[stream_index2];
        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            h= get_byte(&s->pb); len--;
            if(!(h & 0x40)){
                seq = get_byte(&s->pb); len--;
            }
        }
            
        if((flags&2) && (seq&0x7F) == 1){
//            av_log(s, AV_LOG_DEBUG, "%d %d-%d %Ld %d\n", flags, stream_index2, stream_index, dts, seq);
            av_add_index_entry(st, pos, dts, 0, AVINDEX_KEYFRAME);
            if(stream_index2 == stream_index)
                break;
        }

        url_fskip(&s->pb, len);
    }
    *ppos = pos;
    return dts;
}

static AVInputFormat rm_iformat = {
    "rm",
    "rm format",
    sizeof(RMContext),
    rm_probe,
    rm_read_header,
    rm_read_packet,
    rm_read_close,
    NULL,
    rm_read_dts,
};

#ifdef CONFIG_MUXERS
static AVOutputFormat rm_oformat = {
    "rm",
    "rm format",
    "application/vnd.rn-realmedia",
    "rm,ra",
    sizeof(RMContext),
    CODEC_ID_AC3,
    CODEC_ID_RV10,
    rm_write_header,
    rm_write_packet,
    rm_write_trailer,
};
#endif //CONFIG_MUXERS

int rm_init(void)
{
    av_register_input_format(&rm_iformat);
#ifdef CONFIG_MUXERS
    av_register_output_format(&rm_oformat);
#endif //CONFIG_MUXERS
    return 0;
}
