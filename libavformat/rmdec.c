/*
 * "Real" compatible demuxer.
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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
#include "avformat.h"
#include "rm.h"
#include "avstring.h"

static inline void get_strl(ByteIOContext *pb, char *buf, int buf_size, int len)
{
    int i;
    char *q, r;

    q = buf;
    for(i=0;i<len;i++) {
        r = get_byte(pb);
        if (i < buf_size - 1)
            *q++ = r;
    }
    if (buf_size > 0) *q = '\0';
}

static void get_str16(ByteIOContext *pb, char *buf, int buf_size)
{
    get_strl(pb, buf, buf_size, get_be16(pb));
}

static void get_str8(ByteIOContext *pb, char *buf, int buf_size)
{
    get_strl(pb, buf, buf_size, get_byte(pb));
}

static int rm_read_audio_stream_info(AVFormatContext *s, AVStream *st,
                                      int read_all)
{
    RMContext *rm = s->priv_data;
    ByteIOContext *pb = s->pb;
    char buf[256];
    uint32_t version;
    int i;

    /* ra type header */
    version = get_be32(pb); /* version */
    if (((version >> 16) & 0xff) == 3) {
        int64_t startpos = url_ftell(pb);
        /* very old version */
        for(i = 0; i < 14; i++)
            get_byte(pb);
        get_str8(pb, s->title, sizeof(s->title));
        get_str8(pb, s->author, sizeof(s->author));
        get_str8(pb, s->copyright, sizeof(s->copyright));
        get_str8(pb, s->comment, sizeof(s->comment));
        if ((startpos + (version & 0xffff)) >= url_ftell(pb) + 2) {
        // fourcc (should always be "lpcJ")
        get_byte(pb);
        get_str8(pb, buf, sizeof(buf));
        }
        // Skip extra header crap (this should never happen)
        if ((startpos + (version & 0xffff)) > url_ftell(pb))
            url_fskip(pb, (version & 0xffff) + startpos - url_ftell(pb));
        st->codec->sample_rate = 8000;
        st->codec->channels = 1;
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        st->codec->codec_id = CODEC_ID_RA_144;
    } else {
        int flavor, sub_packet_h, coded_framesize, sub_packet_size;
        /* old version (4) */
        get_be32(pb); /* .ra4 */
        get_be32(pb); /* data size */
        get_be16(pb); /* version2 */
        get_be32(pb); /* header size */
        flavor= get_be16(pb); /* add codec info / flavor */
        rm->coded_framesize = coded_framesize = get_be32(pb); /* coded frame size */
        get_be32(pb); /* ??? */
        get_be32(pb); /* ??? */
        get_be32(pb); /* ??? */
        rm->sub_packet_h = sub_packet_h = get_be16(pb); /* 1 */
        st->codec->block_align= get_be16(pb); /* frame size */
        rm->sub_packet_size = sub_packet_size = get_be16(pb); /* sub packet size */
        get_be16(pb); /* ??? */
        if (((version >> 16) & 0xff) == 5) {
            get_be16(pb); get_be16(pb); get_be16(pb); }
        st->codec->sample_rate = get_be16(pb);
        get_be32(pb);
        st->codec->channels = get_be16(pb);
        if (((version >> 16) & 0xff) == 5) {
            get_be32(pb);
            buf[0] = get_byte(pb);
            buf[1] = get_byte(pb);
            buf[2] = get_byte(pb);
            buf[3] = get_byte(pb);
            buf[4] = 0;
        } else {
            get_str8(pb, buf, sizeof(buf)); /* desc */
            get_str8(pb, buf, sizeof(buf)); /* desc */
        }
        st->codec->codec_type = CODEC_TYPE_AUDIO;
        if (!strcmp(buf, "dnet")) {
            st->codec->codec_id = CODEC_ID_AC3;
            st->need_parsing = AVSTREAM_PARSE_FULL;
        } else if (!strcmp(buf, "28_8")) {
            st->codec->codec_id = CODEC_ID_RA_288;
            st->codec->extradata_size= 0;
            rm->audio_framesize = st->codec->block_align;
            st->codec->block_align = coded_framesize;

            if(rm->audio_framesize >= UINT_MAX / sub_packet_h){
                av_log(s, AV_LOG_ERROR, "rm->audio_framesize * sub_packet_h too large\n");
                return -1;
            }

            rm->audiobuf = av_malloc(rm->audio_framesize * sub_packet_h);
        } else if ((!strcmp(buf, "cook")) || (!strcmp(buf, "atrc"))) {
            int codecdata_length, i;
            get_be16(pb); get_byte(pb);
            if (((version >> 16) & 0xff) == 5)
                get_byte(pb);
            codecdata_length = get_be32(pb);
            if(codecdata_length + FF_INPUT_BUFFER_PADDING_SIZE <= (unsigned)codecdata_length){
                av_log(s, AV_LOG_ERROR, "codecdata_length too large\n");
                return -1;
            }

            if (!strcmp(buf, "cook")) st->codec->codec_id = CODEC_ID_COOK;
            else st->codec->codec_id = CODEC_ID_ATRAC3;
            st->codec->extradata_size= codecdata_length;
            st->codec->extradata= av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
            for(i = 0; i < codecdata_length; i++)
                ((uint8_t*)st->codec->extradata)[i] = get_byte(pb);
            rm->audio_framesize = st->codec->block_align;
            st->codec->block_align = rm->sub_packet_size;

            if(rm->audio_framesize >= UINT_MAX / sub_packet_h){
                av_log(s, AV_LOG_ERROR, "rm->audio_framesize * sub_packet_h too large\n");
                return -1;
            }

            rm->audiobuf = av_malloc(rm->audio_framesize * sub_packet_h);
        } else if (!strcmp(buf, "raac") || !strcmp(buf, "racp")) {
            int codecdata_length, i;
            get_be16(pb); get_byte(pb);
            if (((version >> 16) & 0xff) == 5)
                get_byte(pb);
            st->codec->codec_id = CODEC_ID_AAC;
            codecdata_length = get_be32(pb);
            if(codecdata_length + FF_INPUT_BUFFER_PADDING_SIZE <= (unsigned)codecdata_length){
                av_log(s, AV_LOG_ERROR, "codecdata_length too large\n");
                return -1;
            }
            if (codecdata_length >= 1) {
                st->codec->extradata_size = codecdata_length - 1;
                st->codec->extradata = av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
                get_byte(pb);
                for(i = 0; i < st->codec->extradata_size; i++)
                    ((uint8_t*)st->codec->extradata)[i] = get_byte(pb);
            }
        } else {
            st->codec->codec_id = CODEC_ID_NONE;
            av_strlcpy(st->codec->codec_name, buf, sizeof(st->codec->codec_name));
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
    return 0;
}

int
ff_rm_read_mdpr_codecdata (AVFormatContext *s, AVStream *st)
{
    ByteIOContext *pb = s->pb;
    unsigned int v;
    int codec_data_size, size;
    int64_t codec_pos;

    codec_data_size = get_be32(pb);
    codec_pos = url_ftell(pb);
    v = get_be32(pb);
    if (v == MKTAG(0xfd, 'a', 'r', '.')) {
        /* ra type header */
        if (rm_read_audio_stream_info(s, st, 0))
            return -1;
    } else {
        int fps, fps2;
        if (get_le32(pb) != MKTAG('V', 'I', 'D', 'O')) {
        fail1:
            av_log(st->codec, AV_LOG_ERROR, "Unsupported video codec\n");
            goto skip;
        }
        st->codec->codec_tag = get_le32(pb);
//        av_log(NULL, AV_LOG_DEBUG, "%X %X\n", st->codec->codec_tag, MKTAG('R', 'V', '2', '0'));
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

        if(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE <= (unsigned)st->codec->extradata_size){
            //check is redundant as get_buffer() will catch this
            av_log(s, AV_LOG_ERROR, "st->codec->extradata_size too large\n");
            return -1;
        }
        st->codec->extradata= av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
        get_buffer(pb, st->codec->extradata, st->codec->extradata_size);

//        av_log(NULL, AV_LOG_DEBUG, "fps= %d fps2= %d\n", fps, fps2);
        st->codec->time_base.den = fps * st->codec->time_base.num;
        switch(((uint8_t*)st->codec->extradata)[4]>>4){
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

    return 0;
}


static int rm_read_header_old(AVFormatContext *s, AVFormatParameters *ap)
{
    RMContext *rm = s->priv_data;
    AVStream *st;

    rm->old_format = 1;
    st = av_new_stream(s, 0);
    if (!st)
        return -1;
    return rm_read_audio_stream_info(s, st, 1);
}

static int rm_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    RMContext *rm = s->priv_data;
    AVStream *st;
    ByteIOContext *pb = s->pb;
    unsigned int tag;
    int tag_size, i;
    unsigned int start_time, duration;
    char buf[128];
    int flags = 0;

    tag = get_le32(pb);
    if (tag == MKTAG('.', 'r', 'a', 0xfd)) {
        /* very old .ra format */
        return rm_read_header_old(s, ap);
    } else if (tag != MKTAG('.', 'R', 'M', 'F')) {
        return AVERROR(EIO);
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
            get_str16(pb, s->title, sizeof(s->title));
            get_str16(pb, s->author, sizeof(s->author));
            get_str16(pb, s->copyright, sizeof(s->copyright));
            get_str16(pb, s->comment, sizeof(s->comment));
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
            st->codec->codec_type = CODEC_TYPE_DATA;
            av_set_pts_info(st, 64, 1, 1000);
            if (ff_rm_read_mdpr_codecdata(s, st) < 0)
                return -1;
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
    rm->curpic_num = -1;
    return 0;

 fail:
    for(i=0;i<s->nb_streams;i++) {
        av_free(s->streams[i]);
    }
    return AVERROR(EIO);
}

static int get_num(ByteIOContext *pb, int *len)
{
    int n, n1;

    n = get_be16(pb);
    (*len)-=2;
    n &= 0x7FFF;
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
    ByteIOContext *pb = s->pb;
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

static int rm_assemble_video_frame(AVFormatContext *s, RMContext *rm, AVPacket *pkt, int len)
{
    ByteIOContext *pb = s->pb;
    int hdr, seq, pic_num, len2, pos;
    int type;

    hdr = get_byte(pb); len--;
    type = hdr >> 6;
    switch(type){
    case 0: // slice
    case 2: // last slice
        seq = get_byte(pb); len--;
        len2 = get_num(pb, &len);
        pos = get_num(pb, &len);
        pic_num = get_byte(pb); len--;
        rm->remaining_len = len;
        break;
    case 1: //whole frame
        seq = get_byte(pb); len--;
        if(av_new_packet(pkt, len + 9) < 0)
            return AVERROR(EIO);
        pkt->data[0] = 0;
        AV_WL32(pkt->data + 1, 1);
        AV_WL32(pkt->data + 5, 0);
        get_buffer(pb, pkt->data + 9, len);
        rm->remaining_len = 0;
        return 0;
    case 3: //frame as a part of packet
        len2 = get_num(pb, &len);
        pos = get_num(pb, &len);
        pic_num = get_byte(pb); len--;
        rm->remaining_len = len - len2;
        if(av_new_packet(pkt, len2 + 9) < 0)
            return AVERROR(EIO);
        pkt->data[0] = 0;
        AV_WL32(pkt->data + 1, 1);
        AV_WL32(pkt->data + 5, 0);
        get_buffer(pb, pkt->data + 9, len2);
        return 0;
    }
    //now we have to deal with single slice

    if((seq & 0x7F) == 1 || rm->curpic_num != pic_num){
        rm->slices = ((hdr & 0x3F) << 1) + 1;
        rm->videobufsize = len2 + 8*rm->slices + 1;
        av_free(rm->videobuf);
        if(!(rm->videobuf = av_malloc(rm->videobufsize)))
            return AVERROR(ENOMEM);
        rm->videobufpos = 8*rm->slices + 1;
        rm->cur_slice = 0;
        rm->curpic_num = pic_num;
        rm->pktpos = url_ftell(pb);
    }
    if(type == 2)
        len = FFMIN(len, pos);

    if(++rm->cur_slice > rm->slices)
        return 1;
    AV_WL32(rm->videobuf - 7 + 8*rm->cur_slice, 1);
    AV_WL32(rm->videobuf - 3 + 8*rm->cur_slice, rm->videobufpos - 8*rm->slices - 1);
    if(rm->videobufpos + len > rm->videobufsize)
        return 1;
    if (get_buffer(pb, rm->videobuf + rm->videobufpos, len) != len)
        return AVERROR(EIO);
    rm->videobufpos += len;
    rm->remaining_len-= len;

    if(type == 2 || (rm->videobufpos) == rm->videobufsize){
         rm->videobuf[0] = rm->cur_slice-1;
         if(av_new_packet(pkt, rm->videobufpos - 8*(rm->slices - rm->cur_slice)) < 0)
             return AVERROR(ENOMEM);
         memcpy(pkt->data, rm->videobuf, 1 + 8*rm->cur_slice);
         memcpy(pkt->data + 1 + 8*rm->cur_slice, rm->videobuf + 1 + 8*rm->slices,
                rm->videobufpos - 1 - 8*rm->slices);
         pkt->pts = AV_NOPTS_VALUE;
         pkt->pos = rm->pktpos;
         return 0;
    }

    return 1;
}

static inline void
rm_ac3_swap_bytes (AVStream *st, AVPacket *pkt)
{
    uint8_t *ptr;
    int j;

    if (st->codec->codec_id == CODEC_ID_AC3) {
        ptr = pkt->data;
        for (j=0;j<pkt->size;j+=2) {
            FFSWAP(int, ptr[0], ptr[1]);
            ptr += 2;
        }
    }
}

int
ff_rm_parse_packet (AVFormatContext *s, AVStream *st, int len, AVPacket *pkt,
                    int *seq, int *flags, int64_t *timestamp)
{
    ByteIOContext *pb = s->pb;
    RMContext *rm = s->priv_data;

    if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
        rm->current_stream= st->id;
        if(rm_assemble_video_frame(s, rm, pkt, len) == 1)
            return -1; //got partial frame
    } else if (st->codec->codec_type == CODEC_TYPE_AUDIO) {
        if ((st->codec->codec_id == CODEC_ID_RA_288) ||
            (st->codec->codec_id == CODEC_ID_COOK) ||
            (st->codec->codec_id == CODEC_ID_ATRAC3)) {
            int x;
            int sps = rm->sub_packet_size;
            int cfs = rm->coded_framesize;
            int h = rm->sub_packet_h;
            int y = rm->sub_packet_cnt;
            int w = rm->audio_framesize;

            if (*flags & 2)
                y = rm->sub_packet_cnt = 0;
            if (!y)
                rm->audiotimestamp = *timestamp;

            switch(st->codec->codec_id) {
                case CODEC_ID_RA_288:
                    for (x = 0; x < h/2; x++)
                        get_buffer(pb, rm->audiobuf+x*2*w+y*cfs, cfs);
                    break;
                case CODEC_ID_ATRAC3:
                case CODEC_ID_COOK:
                    for (x = 0; x < w/sps; x++)
                        get_buffer(pb, rm->audiobuf+sps*(h*x+((h+1)/2)*(y&1)+(y>>1)), sps);
                    break;
            }

            if (++(rm->sub_packet_cnt) < h)
                return -1;
            else {
                rm->sub_packet_cnt = 0;
                rm->audio_stream_num = st->index;
                rm->audio_pkt_cnt = h * w / st->codec->block_align - 1;
                // Release first audio packet
                av_new_packet(pkt, st->codec->block_align);
                memcpy(pkt->data, rm->audiobuf, st->codec->block_align);
                *timestamp = rm->audiotimestamp;
                *flags = 2; // Mark first packet as keyframe
            }
        } else if (st->codec->codec_id == CODEC_ID_AAC) {
            int x;
            rm->audio_stream_num = st->index;
            rm->sub_packet_cnt = (get_be16(pb) & 0xf0) >> 4;
            if (rm->sub_packet_cnt) {
                for (x = 0; x < rm->sub_packet_cnt; x++)
                    rm->sub_packet_lengths[x] = get_be16(pb);
                // Release first audio packet
                rm->audio_pkt_cnt = rm->sub_packet_cnt - 1;
                av_get_packet(pb, pkt, rm->sub_packet_lengths[0]);
                *flags = 2; // Mark first packet as keyframe
            }
        } else {
            av_get_packet(pb, pkt, len);
            rm_ac3_swap_bytes(st, pkt);
        }
    } else
        av_get_packet(pb, pkt, len);

    if(  (st->discard >= AVDISCARD_NONKEY && !(*flags&2))
       || st->discard >= AVDISCARD_ALL){
        av_free_packet(pkt);
        return -1;
    }

    pkt->stream_index = st->index;

#if 0
    if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
        if(st->codec->codec_id == CODEC_ID_RV20){
            int seq= 128*(pkt->data[2]&0x7F) + (pkt->data[3]>>1);
            av_log(NULL, AV_LOG_DEBUG, "%d %"PRId64" %d\n", *timestamp, *timestamp*512LL/25, seq);

            seq |= (*timestamp&~0x3FFF);
            if(seq - *timestamp >  0x2000) seq -= 0x4000;
            if(seq - *timestamp < -0x2000) seq += 0x4000;
        }
    }
#endif

    pkt->pts= *timestamp;
    if (*flags & 2)
        pkt->flags |= PKT_FLAG_KEY;

    return 0;
}

void
ff_rm_retrieve_cache (AVFormatContext *s, AVStream *st, AVPacket *pkt)
{
    ByteIOContext *pb = s->pb;
    RMContext *rm = s->priv_data;

    assert (rm->audio_pkt_cnt > 0);

    if (st->codec->codec_id == CODEC_ID_AAC)
        av_get_packet(pb, pkt, rm->sub_packet_lengths[rm->sub_packet_cnt - rm->audio_pkt_cnt]);
    else {
        av_new_packet(pkt, st->codec->block_align);
        memcpy(pkt->data, rm->audiobuf + st->codec->block_align *
               (rm->sub_packet_h * rm->audio_framesize / st->codec->block_align - rm->audio_pkt_cnt),
               st->codec->block_align);
    }
    rm->audio_pkt_cnt--;
    pkt->flags = 0;
    pkt->stream_index = st->index;
}

static int rm_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    RMContext *rm = s->priv_data;
    ByteIOContext *pb = s->pb;
    AVStream *st;
    int i, len;
    int64_t timestamp, pos;
    int flags;

    if (rm->audio_pkt_cnt) {
        // If there are queued audio packet return them first
        st = s->streams[rm->audio_stream_num];
        ff_rm_retrieve_cache(s, st, pkt);
    } else if (rm->old_format) {
        st = s->streams[0];
        if (st->codec->codec_id == CODEC_ID_RA_288) {
            int x, y;

            for (y = 0; y < rm->sub_packet_h; y++)
                for (x = 0; x < rm->sub_packet_h/2; x++)
                    if (get_buffer(pb, rm->audiobuf+x*2*rm->audio_framesize+y*rm->coded_framesize, rm->coded_framesize) <= 0)
                        return AVERROR(EIO);
            rm->audio_stream_num = 0;
            rm->audio_pkt_cnt = rm->sub_packet_h * rm->audio_framesize / st->codec->block_align - 1;
            // Release first audio packet
            av_new_packet(pkt, st->codec->block_align);
            memcpy(pkt->data, rm->audiobuf, st->codec->block_align);
            pkt->flags |= PKT_FLAG_KEY; // Mark first packet as keyframe
            pkt->stream_index = 0;
        } else {
            /* just read raw bytes */
            len = RAW_PACKET_SIZE;
            len= av_get_packet(pb, pkt, len);
            pkt->stream_index = 0;
            if (len <= 0) {
                return AVERROR(EIO);
            }
            pkt->size = len;
        }
        rm_ac3_swap_bytes(st, pkt);
    } else {
        int seq=1;
resync:
        len=sync(s, &timestamp, &flags, &i, &pos);
        if(len<0)
            return AVERROR(EIO);
        st = s->streams[i];

        if (ff_rm_parse_packet (s, st, len, pkt, &seq, &flags, &timestamp) < 0)
            goto resync;

        if((flags&2) && (seq&0x7F) == 1)
            av_add_index_entry(st, pos, timestamp, 0, 0, AVINDEX_KEYFRAME);
    }

    return 0;
}

static int rm_read_close(AVFormatContext *s)
{
    RMContext *rm = s->priv_data;

    av_free(rm->audiobuf);
    av_free(rm->videobuf);
    return 0;
}

static int rm_probe(AVProbeData *p)
{
    /* check file header */
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

    url_fseek(s->pb, pos, SEEK_SET);
    rm->remaining_len=0;
    for(;;){
        int seq=1;
        AVStream *st;

        len=sync(s, &dts, &flags, &stream_index2, &pos);
        if(len<0)
            return AV_NOPTS_VALUE;

        st = s->streams[stream_index2];
        if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
            h= get_byte(s->pb); len--;
            if(!(h & 0x40)){
                seq = get_byte(s->pb); len--;
            }
        }

        if((flags&2) && (seq&0x7F) == 1){
//            av_log(s, AV_LOG_DEBUG, "%d %d-%d %"PRId64" %d\n", flags, stream_index2, stream_index, dts, seq);
            av_add_index_entry(st, pos, dts, 0, 0, AVINDEX_KEYFRAME);
            if(stream_index2 == stream_index)
                break;
        }

        url_fskip(s->pb, len);
    }
    *ppos = pos;
    return dts;
}

AVInputFormat rm_demuxer = {
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
