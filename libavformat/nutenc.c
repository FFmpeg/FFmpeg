/*
 * nut muxer
 * Copyright (c) 2004-2007 Michael Niedermayer
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

#include "nut.h"

#define TRACE

static void build_frame_code(AVFormatContext *s){
    NUTContext *nut = s->priv_data;
    int key_frame, index, pred, stream_id;
    int start=1;
    int end= 254;
    int keyframe_0_esc= s->nb_streams > 2;
    int pred_table[10];

    if(keyframe_0_esc){
        /* keyframe = 0 escape */
        FrameCode *ft= &nut->frame_code[start];
        ft->flags= FLAG_STREAM_ID | FLAG_SIZE_MSB | FLAG_CODED_PTS;
        ft->size_mul=1;
        start++;
    }

    for(stream_id= 0; stream_id<s->nb_streams; stream_id++){
        int start2= start + (end-start)*stream_id / s->nb_streams;
        int end2  = start + (end-start)*(stream_id+1) / s->nb_streams;
        AVCodecContext *codec = s->streams[stream_id]->codec;
        int is_audio= codec->codec_type == CODEC_TYPE_AUDIO;
        int intra_only= /*codec->intra_only || */is_audio;
        int pred_count;

        for(key_frame=0; key_frame<2; key_frame++){
            if(intra_only && keyframe_0_esc && key_frame==0)
                continue;

            {
                FrameCode *ft= &nut->frame_code[start2];
                ft->flags= FLAG_KEY*key_frame;
                ft->flags|= FLAG_SIZE_MSB | FLAG_CODED_PTS;
                ft->stream_id= stream_id;
                ft->size_mul=1;
                start2++;
            }
        }

        key_frame= intra_only;
#if 1
        if(is_audio){
            int frame_bytes= codec->frame_size*(int64_t)codec->bit_rate / (8*codec->sample_rate);
            int pts;
            for(pts=0; pts<2; pts++){
                for(pred=0; pred<2; pred++){
                    FrameCode *ft= &nut->frame_code[start2];
                    ft->flags= FLAG_KEY*key_frame;
                    ft->stream_id= stream_id;
                    ft->size_mul=frame_bytes + 2;
                    ft->size_lsb=frame_bytes + pred;
                    ft->pts_delta=pts;
                    start2++;
                }
            }
        }else{
            FrameCode *ft= &nut->frame_code[start2];
            ft->flags= FLAG_KEY | FLAG_SIZE_MSB;
            ft->stream_id= stream_id;
            ft->size_mul=1;
            ft->pts_delta=1;
            start2++;
        }
#endif

        if(codec->has_b_frames){
            pred_count=5;
            pred_table[0]=-2;
            pred_table[1]=-1;
            pred_table[2]=1;
            pred_table[3]=3;
            pred_table[4]=4;
        }else if(codec->codec_id == CODEC_ID_VORBIS){
            pred_count=3;
            pred_table[0]=2;
            pred_table[1]=9;
            pred_table[2]=16;
        }else{
            pred_count=1;
            pred_table[0]=1;
        }

        for(pred=0; pred<pred_count; pred++){
            int start3= start2 + (end2-start2)*pred / pred_count;
            int end3  = start2 + (end2-start2)*(pred+1) / pred_count;

            for(index=start3; index<end3; index++){
                FrameCode *ft= &nut->frame_code[index];
                ft->flags= FLAG_KEY*key_frame;
                ft->flags|= FLAG_SIZE_MSB;
                ft->stream_id= stream_id;
//FIXME use single byte size and pred from last
                ft->size_mul= end3-start3;
                ft->size_lsb= index - start3;
                ft->pts_delta= pred_table[pred];
            }
        }
    }
    memmove(&nut->frame_code['N'+1], &nut->frame_code['N'], sizeof(FrameCode)*(255-'N'));
    nut->frame_code[  0].flags=
    nut->frame_code[255].flags=
    nut->frame_code['N'].flags= FLAG_INVALID;
}

/**
 * Gets the length in bytes which is needed to store val as v.
 */
static int get_length(uint64_t val){
    int i=1;

    while(val>>=7)
        i++;

    return i;
}

static void put_v(ByteIOContext *bc, uint64_t val){
    int i= get_length(val);

    while(--i>0)
        put_byte(bc, 128 | (val>>(7*i)));

    put_byte(bc, val&127);
}

/**
 * stores a string as vb.
 */
static void put_str(ByteIOContext *bc, const char *string){
    int len= strlen(string);

    put_v(bc, len);
    put_buffer(bc, string, len);
}

static void put_s(ByteIOContext *bc, int64_t val){
    put_v(bc, 2*FFABS(val) - (val>0));
}

#ifdef TRACE
static inline void put_v_trace(ByteIOContext *bc, uint64_t v, char *file, char *func, int line){
    printf("get_v %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);

    put_v(bc, v);
}

static inline void put_s_trace(ByteIOContext *bc, int64_t v, char *file, char *func, int line){
    printf("get_s %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);

    put_s(bc, v);
}
#define put_v(bc, v)  put_v_trace(bc, v, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define put_s(bc, v)  put_s_trace(bc, v, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#endif

static int put_packetheader(NUTContext *nut, ByteIOContext *bc, int max_size, int calculate_checksum){
    put_flush_packet(bc);
    nut->packet_start= url_ftell(bc) - 8;
    nut->written_packet_size = max_size;

    /* packet header */
    put_v(bc, nut->written_packet_size); /* forward ptr */

    if(calculate_checksum)
        init_checksum(bc, av_crc04C11DB7_update, 0);

    return 0;
}

/**
 *
 * must not be called more then once per packet
 */
static int update_packetheader(NUTContext *nut, ByteIOContext *bc, int additional_size, int calculate_checksum){
    int64_t start= nut->packet_start;
    int64_t cur= url_ftell(bc);
    int size= cur - start - get_length(nut->written_packet_size) - 8;

    if(calculate_checksum)
        size += 4;

    if(size != nut->written_packet_size){
        int i;

        assert( size <= nut->written_packet_size );

        url_fseek(bc, start + 8, SEEK_SET);
        for(i=get_length(size); i < get_length(nut->written_packet_size); i++)
            put_byte(bc, 128);
        put_v(bc, size);

        url_fseek(bc, cur, SEEK_SET);
        nut->written_packet_size= size; //FIXME may fail if multiple updates with differing sizes, as get_length may differ

        if(calculate_checksum)
            put_le32(bc, get_checksum(bc));
    }

    return 0;
}

static int write_header(AVFormatContext *s){
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    AVCodecContext *codec;
    int i, j, tmp_pts, tmp_flags, tmp_stream, tmp_mul, tmp_size, tmp_fields;

    nut->avf= s;

    nut->stream   = av_mallocz(sizeof(StreamContext)*s->nb_streams);
    nut->time_base= av_mallocz(sizeof(AVRational   )*s->nb_streams);

    for(i=0; i<s->nb_streams; i++){
        AVStream *st= s->streams[i];
        int num, denom, ssize;
        ff_parse_specific_params(st->codec, &num, &ssize, &denom);

        nut->stream[i].time_base= (AVRational){denom, num};

        av_set_pts_info(st, 64, denom, num);

        for(j=0; j<nut->time_base_count; j++){
            if(!memcmp(&nut->stream[i].time_base, &nut->time_base[j], sizeof(AVRational))){
                break;
            }
        }
        nut->time_base[j]= nut->stream[i].time_base;
        if(j==nut->time_base_count)
            nut->time_base_count++;
    }
//FIXME make nut->stream[i].time_base pointers into nut->time_base

    put_buffer(bc, ID_STRING, strlen(ID_STRING));
    put_byte(bc, 0);

    /* main header */
    put_be64(bc, MAIN_STARTCODE);
    put_packetheader(nut, bc, 120+5*256/*FIXME check*/, 1);

    put_v(bc, 2); /* version */
    put_v(bc, s->nb_streams);
    put_v(bc, MAX_DISTANCE);
    put_v(bc, nut->time_base_count);

    for(i=0; i<nut->time_base_count; i++){
        put_v(bc, nut->time_base[i].num);
        put_v(bc, nut->time_base[i].den);
    }

    build_frame_code(s);
    assert(nut->frame_code['N'].flags == FLAG_INVALID);

    tmp_pts=0;
    tmp_mul=1;
    tmp_stream=0;
    for(i=0; i<256;){
        tmp_fields=0;
        tmp_size=0;
//        tmp_res=0;
        if(tmp_pts    != nut->frame_code[i].pts_delta) tmp_fields=1;
        if(tmp_mul    != nut->frame_code[i].size_mul ) tmp_fields=2;
        if(tmp_stream != nut->frame_code[i].stream_id) tmp_fields=3;
        if(tmp_size   != nut->frame_code[i].size_lsb ) tmp_fields=4;
//        if(tmp_res    != nut->frame_code[i].res            ) tmp_fields=5;

        tmp_pts   = nut->frame_code[i].pts_delta;
        tmp_flags = nut->frame_code[i].flags;
        tmp_stream= nut->frame_code[i].stream_id;
        tmp_mul   = nut->frame_code[i].size_mul;
        tmp_size  = nut->frame_code[i].size_lsb;
//        tmp_res   = nut->frame_code[i].res;

        for(j=0; i<256; j++,i++){
            if(i == 'N'){
                j--;
                continue;
            }
            if(nut->frame_code[i].pts_delta != tmp_pts   ) break;
            if(nut->frame_code[i].flags     != tmp_flags ) break;
            if(nut->frame_code[i].stream_id != tmp_stream) break;
            if(nut->frame_code[i].size_mul  != tmp_mul   ) break;
            if(nut->frame_code[i].size_lsb  != tmp_size+j) break;
//            if(nut->frame_code[i].res       != tmp_res   ) break;
        }
        if(j != tmp_mul - tmp_size) tmp_fields=6;

        put_v(bc, tmp_flags);
        put_v(bc, tmp_fields);
        if(tmp_fields>0) put_s(bc, tmp_pts);
        if(tmp_fields>1) put_v(bc, tmp_mul);
        if(tmp_fields>2) put_v(bc, tmp_stream);
        if(tmp_fields>3) put_v(bc, tmp_size);
        if(tmp_fields>4) put_v(bc, 0 /*tmp_res*/);
        if(tmp_fields>5) put_v(bc, j);
    }

    update_packetheader(nut, bc, 0, 1);

    put_flush_packet(bc);

    //FIXME stream header, ...

    return 0;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt){
    //FIXME
    return 0;
}

AVOutputFormat nut_muxer = {
    "nut",
    "nut format",
    "video/x-nut",
    "nut",
    sizeof(NUTContext),
#ifdef CONFIG_LIBVORBIS
    CODEC_ID_VORBIS,
#elif defined(CONFIG_LIBMP3LAME)
    CODEC_ID_MP3,
#else
    CODEC_ID_MP2, /* AC3 needs liba52 decoder */
#endif
    CODEC_ID_MPEG4,
    write_header,
    write_packet,
//    write_trailer,
    .flags = AVFMT_GLOBALHEADER,
};
