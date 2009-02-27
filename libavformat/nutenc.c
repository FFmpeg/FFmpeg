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

#include "libavutil/intreadwrite.h"
#include "libavutil/tree.h"
#include "libavcodec/mpegaudiodata.h"
#include "nut.h"

static int find_expected_header(AVCodecContext *c, int size, int key_frame, uint8_t out[64]){
    int sample_rate= c->sample_rate;

    if(size>4096)
        return 0;

    AV_WB24(out, 1);

    if(c->codec_id == CODEC_ID_MPEG4){
        if(key_frame){
            return 3;
        }else{
            out[3]= 0xB6;
            return 4;
        }
    }else if(c->codec_id == CODEC_ID_MPEG1VIDEO || c->codec_id == CODEC_ID_MPEG2VIDEO){
        return 3;
    }else if(c->codec_id == CODEC_ID_H264){
        return 3;
    }else if(c->codec_id == CODEC_ID_MP3 || c->codec_id == CODEC_ID_MP2){
        int lsf, mpeg25, sample_rate_index, bitrate_index, frame_size;
        int layer= c->codec_id == CODEC_ID_MP3 ? 3 : 2;
        unsigned int header= 0xFFF00000;

        lsf     = sample_rate < (24000+32000)/2;
        mpeg25  = sample_rate < (12000+16000)/2;
        sample_rate <<= lsf + mpeg25;
        if     (sample_rate < (32000 + 44100)/2) sample_rate_index=2;
        else if(sample_rate < (44100 + 48000)/2) sample_rate_index=0;
        else                                     sample_rate_index=1;

        sample_rate= ff_mpa_freq_tab[sample_rate_index] >> (lsf + mpeg25);

        for(bitrate_index=2; bitrate_index<30; bitrate_index++){
            frame_size = ff_mpa_bitrate_tab[lsf][layer-1][bitrate_index>>1];
            frame_size = (frame_size * 144000) / (sample_rate << lsf) + (bitrate_index&1);

            if(frame_size == size)
                break;
        }

        header |= (!lsf)<<19;
        header |= (4-layer)<<17;
        header |= 1<<16; //no crc
        AV_WB32(out, header);
        if(size <= 0)
            return 2; //we guess there is no crc, if there is one the user clearly does not care about overhead
        if(bitrate_index == 30)
            return -1; //something is wrong ...

        header |= (bitrate_index>>1)<<12;
        header |= sample_rate_index<<10;
        header |= (bitrate_index&1)<<9;

        return 2; //FIXME actually put the needed ones in build_elision_headers()
        return 3; //we guess that the private bit is not set
//FIXME the above assumptions should be checked, if these turn out false too often something should be done
    }
    return 0;
}

static int find_header_idx(AVFormatContext *s, AVCodecContext *c, int size, int frame_type){
    NUTContext *nut = s->priv_data;
    uint8_t out[64];
    int i;
    int len= find_expected_header(c, size, frame_type, out);

//av_log(NULL, AV_LOG_ERROR, "expected_h len=%d size=%d codec_id=%d\n", len, size, c->codec_id);

    for(i=1; i<nut->header_count; i++){
        if(   len == nut->header_len[i]
           && !memcmp(out, nut->header[i], len)){
//    av_log(NULL, AV_LOG_ERROR, "found %d\n", i);
            return i;
        }
    }
//    av_log(NULL, AV_LOG_ERROR, "nothing found\n");
    return 0;
}

static void build_elision_headers(AVFormatContext *s){
    NUTContext *nut = s->priv_data;
    int i;
    //FIXME this is lame
    //FIXME write a 2pass mode to find the maximal headers
    static const uint8_t headers[][5]={
        {3, 0x00, 0x00, 0x01},
        {4, 0x00, 0x00, 0x01, 0xB6},
        {2, 0xFF, 0xFA}, //mp3+crc
        {2, 0xFF, 0xFB}, //mp3
        {2, 0xFF, 0xFC}, //mp2+crc
        {2, 0xFF, 0xFD}, //mp2
    };

    nut->header_count= 7;
    for(i=1; i<nut->header_count; i++){
        nut->header_len[i]=  headers[i-1][0];
        nut->header    [i]= &headers[i-1][1];
    }
}

static void build_frame_code(AVFormatContext *s){
    NUTContext *nut = s->priv_data;
    int key_frame, index, pred, stream_id;
    int start=1;
    int end= 254;
    int keyframe_0_esc= s->nb_streams > 2;
    int pred_table[10];
    FrameCode *ft;

    ft= &nut->frame_code[start];
    ft->flags= FLAG_CODED;
    ft->size_mul=1;
    ft->pts_delta=1;
    start++;

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
                if(is_audio)
                    ft->header_idx= find_header_idx(s, codec, -1, key_frame);
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
                    ft->header_idx= find_header_idx(s, codec, frame_bytes + pred, key_frame);
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
                if(is_audio)
                    ft->header_idx= find_header_idx(s, codec, -1, key_frame);
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

static void put_tt(NUTContext *nut, StreamContext *nus, ByteIOContext *bc, uint64_t val){
    val *= nut->time_base_count;
    val += nus->time_base - nut->time_base;
    put_v(bc, val);
}

/**
 * Stores a string as vb.
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
    av_log(NULL, AV_LOG_DEBUG, "put_v %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);

    put_v(bc, v);
}

static inline void put_s_trace(ByteIOContext *bc, int64_t v, char *file, char *func, int line){
    av_log(NULL, AV_LOG_DEBUG, "put_s %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);

    put_s(bc, v);
}
#define put_v(bc, v)  put_v_trace(bc, v, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define put_s(bc, v)  put_s_trace(bc, v, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#endif

//FIXME remove calculate_checksum
static void put_packet(NUTContext *nut, ByteIOContext *bc, ByteIOContext *dyn_bc, int calculate_checksum, uint64_t startcode){
    uint8_t *dyn_buf=NULL;
    int dyn_size= url_close_dyn_buf(dyn_bc, &dyn_buf);
    int forw_ptr= dyn_size + 4*calculate_checksum;

    if(forw_ptr > 4096)
        init_checksum(bc, ff_crc04C11DB7_update, 0);
    put_be64(bc, startcode);
    put_v(bc, forw_ptr);
    if(forw_ptr > 4096)
        put_le32(bc, get_checksum(bc));

    if(calculate_checksum)
        init_checksum(bc, ff_crc04C11DB7_update, 0);
    put_buffer(bc, dyn_buf, dyn_size);
    if(calculate_checksum)
        put_le32(bc, get_checksum(bc));

    av_free(dyn_buf);
}

static void write_mainheader(NUTContext *nut, ByteIOContext *bc){
    int i, j, tmp_pts, tmp_flags, tmp_stream, tmp_mul, tmp_size, tmp_fields, tmp_head_idx;
    int64_t tmp_match;

    put_v(bc, 3); /* version */
    put_v(bc, nut->avf->nb_streams);
    put_v(bc, nut->max_distance);
    put_v(bc, nut->time_base_count);

    for(i=0; i<nut->time_base_count; i++){
        put_v(bc, nut->time_base[i].num);
        put_v(bc, nut->time_base[i].den);
    }

    tmp_pts=0;
    tmp_mul=1;
    tmp_stream=0;
    tmp_match= 1-(1LL<<62);
    tmp_head_idx= 0;
    for(i=0; i<256;){
        tmp_fields=0;
        tmp_size=0;
//        tmp_res=0;
        if(tmp_pts    != nut->frame_code[i].pts_delta) tmp_fields=1;
        if(tmp_mul    != nut->frame_code[i].size_mul ) tmp_fields=2;
        if(tmp_stream != nut->frame_code[i].stream_id) tmp_fields=3;
        if(tmp_size   != nut->frame_code[i].size_lsb ) tmp_fields=4;
//        if(tmp_res    != nut->frame_code[i].res            ) tmp_fields=5;
        if(tmp_head_idx!=nut->frame_code[i].header_idx)tmp_fields=8;

        tmp_pts   = nut->frame_code[i].pts_delta;
        tmp_flags = nut->frame_code[i].flags;
        tmp_stream= nut->frame_code[i].stream_id;
        tmp_mul   = nut->frame_code[i].size_mul;
        tmp_size  = nut->frame_code[i].size_lsb;
//        tmp_res   = nut->frame_code[i].res;
        tmp_head_idx= nut->frame_code[i].header_idx;

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
            if(nut->frame_code[i].header_idx!= tmp_head_idx) break;
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
        if(tmp_fields>6) put_v(bc, tmp_match);
        if(tmp_fields>7) put_v(bc, tmp_head_idx);
    }
    put_v(bc, nut->header_count-1);
    for(i=1; i<nut->header_count; i++){
        put_v(bc, nut->header_len[i]);
        put_buffer(bc, nut->header[i], nut->header_len[i]);
    }
}

static int write_streamheader(NUTContext *nut, ByteIOContext *bc, AVStream *st, int i){
    AVCodecContext *codec = st->codec;
    put_v(bc, i);
    switch(codec->codec_type){
    case CODEC_TYPE_VIDEO: put_v(bc, 0); break;
    case CODEC_TYPE_AUDIO: put_v(bc, 1); break;
    case CODEC_TYPE_SUBTITLE: put_v(bc, 2); break;
    default              : put_v(bc, 3); break;
    }
    put_v(bc, 4);
    if (codec->codec_tag){
        put_le32(bc, codec->codec_tag);
    }else
        return -1;

    put_v(bc, nut->stream[i].time_base - nut->time_base);
    put_v(bc, nut->stream[i].msb_pts_shift);
    put_v(bc, nut->stream[i].max_pts_distance);
    put_v(bc, codec->has_b_frames);
    put_byte(bc, 0); /* flags: 0x1 - fixed_fps, 0x2 - index_present */

    put_v(bc, codec->extradata_size);
    put_buffer(bc, codec->extradata, codec->extradata_size);

    switch(codec->codec_type){
    case CODEC_TYPE_AUDIO:
        put_v(bc, codec->sample_rate);
        put_v(bc, 1);
        put_v(bc, codec->channels);
        break;
    case CODEC_TYPE_VIDEO:
        put_v(bc, codec->width);
        put_v(bc, codec->height);

        if(st->sample_aspect_ratio.num<=0 || st->sample_aspect_ratio.den<=0){
            put_v(bc, 0);
            put_v(bc, 0);
        }else{
            put_v(bc, st->sample_aspect_ratio.num);
            put_v(bc, st->sample_aspect_ratio.den);
        }
        put_v(bc, 0); /* csp type -- unknown */
        break;
    default:
        break;
    }
    return 0;
}

static int add_info(ByteIOContext *bc, const char *type, const char *value){
    put_str(bc, type);
    put_s(bc, -1);
    put_str(bc, value);
    return 1;
}

static int write_globalinfo(NUTContext *nut, ByteIOContext *bc){
    AVFormatContext *s= nut->avf;
    AVMetadataTag *title, *author, *copyright;
    ByteIOContext *dyn_bc;
    uint8_t *dyn_buf=NULL;
    int count=0, dyn_size;
    int ret = url_open_dyn_buf(&dyn_bc);
    if(ret < 0)
        return ret;

    title     = av_metadata_get(s->metadata, "Title"    , NULL, 0);
    author    = av_metadata_get(s->metadata, "Author"   , NULL, 0);
    copyright = av_metadata_get(s->metadata, "Copyright", NULL, 0);

    if(title    ) count+= add_info(dyn_bc, "Title"    , title->value);
    if(author   ) count+= add_info(dyn_bc, "Author"   , author->value);
    if(copyright) count+= add_info(dyn_bc, "Copyright", copyright->value);
    if(!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT))
                        count+= add_info(dyn_bc, "Encoder"  , LIBAVFORMAT_IDENT);

    put_v(bc, 0); //stream_if_plus1
    put_v(bc, 0); //chapter_id
    put_v(bc, 0); //timestamp_start
    put_v(bc, 0); //length

    put_v(bc, count);

    dyn_size= url_close_dyn_buf(dyn_bc, &dyn_buf);
    put_buffer(bc, dyn_buf, dyn_size);
    av_free(dyn_buf);
    return 0;
}

static int write_streaminfo(NUTContext *nut, ByteIOContext *bc, int stream_id){
    AVFormatContext *s= nut->avf;
    AVStream* st = s->streams[stream_id];
    ByteIOContext *dyn_bc;
    uint8_t *dyn_buf=NULL;
    int count=0, dyn_size, i;
    int ret = url_open_dyn_buf(&dyn_bc);
    if(ret < 0)
        return ret;

    for (i=0; ff_nut_dispositions[i].flag; ++i) {
        if (st->disposition & ff_nut_dispositions[i].flag)
            count += add_info(dyn_bc, "Disposition", ff_nut_dispositions[i].str);
    }
    dyn_size = url_close_dyn_buf(dyn_bc, &dyn_buf);

    if (count) {
        put_v(bc, stream_id + 1); //stream_id_plus1
        put_v(bc, 0); //chapter_id
        put_v(bc, 0); //timestamp_start
        put_v(bc, 0); //length

        put_v(bc, count);

        put_buffer(bc, dyn_buf, dyn_size);
    }

    av_free(dyn_buf);
    return count;
}

static int write_headers(NUTContext *nut, ByteIOContext *bc){
    ByteIOContext *dyn_bc;
    int i, ret;

    ret = url_open_dyn_buf(&dyn_bc);
    if(ret < 0)
        return ret;
    write_mainheader(nut, dyn_bc);
    put_packet(nut, bc, dyn_bc, 1, MAIN_STARTCODE);

    for (i=0; i < nut->avf->nb_streams; i++){
        ret = url_open_dyn_buf(&dyn_bc);
        if(ret < 0)
            return ret;
        write_streamheader(nut, dyn_bc, nut->avf->streams[i], i);
        put_packet(nut, bc, dyn_bc, 1, STREAM_STARTCODE);
    }

    ret = url_open_dyn_buf(&dyn_bc);
    if(ret < 0)
        return ret;
    write_globalinfo(nut, dyn_bc);
    put_packet(nut, bc, dyn_bc, 1, INFO_STARTCODE);

    for (i = 0; i < nut->avf->nb_streams; i++) {
        ret = url_open_dyn_buf(&dyn_bc);
        if(ret < 0)
            return ret;
        ret = write_streaminfo(nut, dyn_bc, i);
        if (ret < 0)
            return ret;
        if (ret > 0)
            put_packet(nut, bc, dyn_bc, 1, INFO_STARTCODE);
        else {
            uint8_t* buf;
            url_close_dyn_buf(dyn_bc, &buf);
            av_free(buf);
        }
    }

    nut->last_syncpoint_pos= INT_MIN;
    nut->header_count++;
    return 0;
}

static int write_header(AVFormatContext *s){
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = s->pb;
    int i, j;

    nut->avf= s;

    nut->stream   = av_mallocz(sizeof(StreamContext)*s->nb_streams);
    nut->time_base= av_mallocz(sizeof(AVRational   )*s->nb_streams);

    for(i=0; i<s->nb_streams; i++){
        AVStream *st= s->streams[i];
        int ssize;
        AVRational time_base;
        ff_parse_specific_params(st->codec, &time_base.den, &ssize, &time_base.num);

        av_set_pts_info(st, 64, time_base.num, time_base.den);

        for(j=0; j<nut->time_base_count; j++){
            if(!memcmp(&time_base, &nut->time_base[j], sizeof(AVRational))){
                break;
            }
        }
        nut->time_base[j]= time_base;
        nut->stream[i].time_base= &nut->time_base[j];
        if(j==nut->time_base_count)
            nut->time_base_count++;

        if(av_q2d(time_base) >= 0.001)
            nut->stream[i].msb_pts_shift = 7;
        else
            nut->stream[i].msb_pts_shift = 14;
        nut->stream[i].max_pts_distance= FFMAX(1/av_q2d(time_base), 1);
    }

    nut->max_distance = MAX_DISTANCE;
    build_elision_headers(s);
    build_frame_code(s);
    assert(nut->frame_code['N'].flags == FLAG_INVALID);

    put_buffer(bc, ID_STRING, strlen(ID_STRING));
    put_byte(bc, 0);

    write_headers(nut, bc);

    put_flush_packet(bc);

    //FIXME index

    return 0;
}

static int get_needed_flags(NUTContext *nut, StreamContext *nus, FrameCode *fc, AVPacket *pkt){
    int flags= 0;

    if(pkt->flags & PKT_FLAG_KEY                ) flags |= FLAG_KEY;
    if(pkt->stream_index != fc->stream_id       ) flags |= FLAG_STREAM_ID;
    if(pkt->size / fc->size_mul                 ) flags |= FLAG_SIZE_MSB;
    if(pkt->pts - nus->last_pts != fc->pts_delta) flags |= FLAG_CODED_PTS;
    if(pkt->size > 2*nut->max_distance          ) flags |= FLAG_CHECKSUM;
    if(FFABS(pkt->pts - nus->last_pts)
                         > nus->max_pts_distance) flags |= FLAG_CHECKSUM;
    if(   pkt->size < nut->header_len[fc->header_idx]
       || (pkt->size > 4096 && fc->header_idx)
       || memcmp(pkt->data, nut->header[fc->header_idx], nut->header_len[fc->header_idx]))
                                                  flags |= FLAG_HEADER_IDX;

    return flags | (fc->flags & FLAG_CODED);
}

static int find_best_header_idx(NUTContext *nut, AVPacket *pkt){
    int i;
    int best_i  = 0;
    int best_len= 0;

    if(pkt->size > 4096)
        return 0;

    for(i=1; i<nut->header_count; i++){
        if(   pkt->size >= nut->header_len[i]
           &&  nut->header_len[i] > best_len
           && !memcmp(pkt->data, nut->header[i], nut->header_len[i])){
            best_i= i;
            best_len= nut->header_len[i];
        }
    }
    return best_i;
}

static int write_packet(AVFormatContext *s, AVPacket *pkt){
    NUTContext *nut = s->priv_data;
    StreamContext *nus= &nut->stream[pkt->stream_index];
    ByteIOContext *bc = s->pb, *dyn_bc;
    FrameCode *fc;
    int64_t coded_pts;
    int best_length, frame_code, flags, needed_flags, i, header_idx, best_header_idx;
    int key_frame = !!(pkt->flags & PKT_FLAG_KEY);
    int store_sp=0;
    int ret;

    if(pkt->pts < 0)
        return -1;

    if(1LL<<(20+3*nut->header_count) <= url_ftell(bc))
        write_headers(nut, bc);

    if(key_frame && !(nus->last_flags & FLAG_KEY))
        store_sp= 1;

    if(pkt->size + 30/*FIXME check*/ + url_ftell(bc) >= nut->last_syncpoint_pos + nut->max_distance)
        store_sp= 1;

//FIXME: Ensure store_sp is 1 in the first place.

    if(store_sp){
        Syncpoint *sp, dummy= {.pos= INT64_MAX};

        ff_nut_reset_ts(nut, *nus->time_base, pkt->dts);
        for(i=0; i<s->nb_streams; i++){
            AVStream *st= s->streams[i];
            int64_t dts_tb = av_rescale_rnd(pkt->dts,
                nus->time_base->num * (int64_t)nut->stream[i].time_base->den,
                nus->time_base->den * (int64_t)nut->stream[i].time_base->num,
                AV_ROUND_DOWN);
            int index= av_index_search_timestamp(st, dts_tb, AVSEEK_FLAG_BACKWARD);
            if(index>=0) dummy.pos= FFMIN(dummy.pos, st->index_entries[index].pos);
        }
        if(dummy.pos == INT64_MAX)
            dummy.pos= 0;
        sp= av_tree_find(nut->syncpoints, &dummy, ff_nut_sp_pos_cmp, NULL);

        nut->last_syncpoint_pos= url_ftell(bc);
        ret = url_open_dyn_buf(&dyn_bc);
        if(ret < 0)
            return ret;
        put_tt(nut, nus, dyn_bc, pkt->dts);
        put_v(dyn_bc, sp ? (nut->last_syncpoint_pos - sp->pos)>>4 : 0);
        put_packet(nut, bc, dyn_bc, 1, SYNCPOINT_STARTCODE);

        ff_nut_add_sp(nut, nut->last_syncpoint_pos, 0/*unused*/, pkt->dts);
    }
    assert(nus->last_pts != AV_NOPTS_VALUE);

    coded_pts = pkt->pts & ((1<<nus->msb_pts_shift)-1);
    if(ff_lsb2full(nus, coded_pts) != pkt->pts)
        coded_pts= pkt->pts + (1<<nus->msb_pts_shift);

    best_header_idx= find_best_header_idx(nut, pkt);

    best_length=INT_MAX;
    frame_code= -1;
    for(i=0; i<256; i++){
        int length= 0;
        FrameCode *fc= &nut->frame_code[i];
        int flags= fc->flags;

        if(flags & FLAG_INVALID)
            continue;
        needed_flags= get_needed_flags(nut, nus, fc, pkt);

        if(flags & FLAG_CODED){
            length++;
            flags = needed_flags;
        }

        if((flags & needed_flags) != needed_flags)
            continue;

        if((flags ^ needed_flags) & FLAG_KEY)
            continue;

        if(flags & FLAG_STREAM_ID)
            length+= get_length(pkt->stream_index);

        if(pkt->size % fc->size_mul != fc->size_lsb)
            continue;
        if(flags & FLAG_SIZE_MSB)
            length += get_length(pkt->size / fc->size_mul);

        if(flags & FLAG_CHECKSUM)
            length+=4;

        if(flags & FLAG_CODED_PTS)
            length += get_length(coded_pts);

        if(   (flags & FLAG_CODED)
           && nut->header_len[best_header_idx] > nut->header_len[fc->header_idx]+1){
            flags |= FLAG_HEADER_IDX;
        }

        if(flags & FLAG_HEADER_IDX){
            length += 1 - nut->header_len[best_header_idx];
        }else{
            length -= nut->header_len[fc->header_idx];
        }

        length*=4;
        length+= !(flags & FLAG_CODED_PTS);
        length+= !(flags & FLAG_CHECKSUM);

        if(length < best_length){
            best_length= length;
            frame_code=i;
        }
    }
    assert(frame_code != -1);
    fc= &nut->frame_code[frame_code];
    flags= fc->flags;
    needed_flags= get_needed_flags(nut, nus, fc, pkt);
    header_idx= fc->header_idx;

    init_checksum(bc, ff_crc04C11DB7_update, 0);
    put_byte(bc, frame_code);
    if(flags & FLAG_CODED){
        put_v(bc, (flags^needed_flags) & ~(FLAG_CODED));
        flags = needed_flags;
    }
    if(flags & FLAG_STREAM_ID)  put_v(bc, pkt->stream_index);
    if(flags & FLAG_CODED_PTS)  put_v(bc, coded_pts);
    if(flags & FLAG_SIZE_MSB)   put_v(bc, pkt->size / fc->size_mul);
    if(flags & FLAG_HEADER_IDX) put_v(bc, header_idx= best_header_idx);

    if(flags & FLAG_CHECKSUM)   put_le32(bc, get_checksum(bc));
    else                        get_checksum(bc);

    put_buffer(bc, pkt->data + nut->header_len[header_idx], pkt->size - nut->header_len[header_idx]);
    nus->last_flags= flags;
    nus->last_pts= pkt->pts;

    //FIXME just store one per syncpoint
    if(flags & FLAG_KEY)
        av_add_index_entry(
            s->streams[pkt->stream_index],
            nut->last_syncpoint_pos,
            pkt->pts,
            0,
            0,
            AVINDEX_KEYFRAME);

    return 0;
}

static int write_trailer(AVFormatContext *s){
    NUTContext *nut= s->priv_data;
    ByteIOContext *bc= s->pb;

    while(nut->header_count<3)
        write_headers(nut, bc);
    put_flush_packet(bc);

    return 0;
}

AVOutputFormat nut_muxer = {
    "nut",
    NULL_IF_CONFIG_SMALL("NUT format"),
    "video/x-nut",
    "nut",
    sizeof(NUTContext),
#if   CONFIG_LIBVORBIS
    CODEC_ID_VORBIS,
#elif CONFIG_LIBMP3LAME
    CODEC_ID_MP3,
#else
    CODEC_ID_MP2,
#endif
    CODEC_ID_MPEG4,
    write_header,
    write_packet,
    write_trailer,
    .flags = AVFMT_GLOBALHEADER | AVFMT_VARIABLE_FPS,
    .codec_tag= (const AVCodecTag* const []){codec_bmp_tags, codec_wav_tags, ff_nut_subtitle_tags, 0},
};
