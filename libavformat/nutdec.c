/*
 * "NUT" Container Format demuxer
 * Copyright (c) 2004-2006 Michael Niedermayer
 * Copyright (c) 2003 Alex Beregszaszi
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

#include "tree.h"
#include "nut.h"
#include "avstring.h"

#undef NDEBUG
#include <assert.h>

static int get_str(ByteIOContext *bc, char *string, unsigned int maxlen){
    unsigned int len= ff_get_v(bc);

    if(len && maxlen)
        get_buffer(bc, string, FFMIN(len, maxlen));
    while(len > maxlen){
        get_byte(bc);
        len--;
    }

    if(maxlen)
        string[FFMIN(len, maxlen-1)]= 0;

    if(maxlen == len)
        return -1;
    else
        return 0;
}

static int64_t get_s(ByteIOContext *bc){
    int64_t v = ff_get_v(bc) + 1;

    if (v&1) return -(v>>1);
    else     return  (v>>1);
}

static uint64_t get_fourcc(ByteIOContext *bc){
    unsigned int len= ff_get_v(bc);

    if     (len==2) return get_le16(bc);
    else if(len==4) return get_le32(bc);
    else            return -1;
}

#ifdef TRACE
static inline uint64_t get_v_trace(ByteIOContext *bc, char *file, char *func, int line){
    uint64_t v= ff_get_v(bc);

    av_log(NULL, AV_LOG_DEBUG, "get_v %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);
    return v;
}

static inline int64_t get_s_trace(ByteIOContext *bc, char *file, char *func, int line){
    int64_t v= get_s(bc);

    av_log(NULL, AV_LOG_DEBUG, "get_s %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);
    return v;
}

static inline uint64_t get_vb_trace(ByteIOContext *bc, char *file, char *func, int line){
    uint64_t v= get_vb(bc);

    av_log(NULL, AV_LOG_DEBUG, "get_vb %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);
    return v;
}
#define ff_get_v(bc)  get_v_trace(bc, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_s(bc)  get_s_trace(bc, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_vb(bc)  get_vb_trace(bc, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#endif

static int get_packetheader(NUTContext *nut, ByteIOContext *bc, int calculate_checksum, uint64_t startcode)
{
    int64_t size;
//    start= url_ftell(bc) - 8;

    startcode= be2me_64(startcode);
    startcode= ff_crc04C11DB7_update(0, &startcode, 8);

    init_checksum(bc, ff_crc04C11DB7_update, startcode);
    size= ff_get_v(bc);
    if(size > 4096)
        get_be32(bc);
    if(get_checksum(bc) && size > 4096)
        return -1;

    init_checksum(bc, calculate_checksum ? ff_crc04C11DB7_update : NULL, 0);

    return size;
}

static uint64_t find_any_startcode(ByteIOContext *bc, int64_t pos){
    uint64_t state=0;

    if(pos >= 0)
        url_fseek(bc, pos, SEEK_SET); //note, this may fail if the stream is not seekable, but that should not matter, as in this case we simply start where we currently are

    while(!url_feof(bc)){
        state= (state<<8) | get_byte(bc);
        if((state>>56) != 'N')
            continue;
        switch(state){
        case MAIN_STARTCODE:
        case STREAM_STARTCODE:
        case SYNCPOINT_STARTCODE:
        case INFO_STARTCODE:
        case INDEX_STARTCODE:
            return state;
        }
    }

    return 0;
}

/**
 * Find the given startcode.
 * @param code the startcode
 * @param pos the start position of the search, or -1 if the current position
 * @returns the position of the startcode or -1 if not found
 */
static int64_t find_startcode(ByteIOContext *bc, uint64_t code, int64_t pos){
    for(;;){
        uint64_t startcode= find_any_startcode(bc, pos);
        if(startcode == code)
            return url_ftell(bc) - 8;
        else if(startcode == 0)
            return -1;
        pos=-1;
    }
}

static int nut_probe(AVProbeData *p){
    int i;
    uint64_t code= 0;

    for (i = 0; i < p->buf_size; i++) {
        code = (code << 8) | p->buf[i];
        if (code == MAIN_STARTCODE)
            return AVPROBE_SCORE_MAX;
    }
    return 0;
}

#define GET_V(dst, check) \
    tmp= ff_get_v(bc);\
    if(!(check)){\
        av_log(s, AV_LOG_ERROR, "Error " #dst " is (%"PRId64")\n", tmp);\
        return -1;\
    }\
    dst= tmp;

static int skip_reserved(ByteIOContext *bc, int64_t pos){
    pos -= url_ftell(bc);
    if(pos<0){
        url_fseek(bc, pos, SEEK_CUR);
        return -1;
    }else{
        while(pos--)
            get_byte(bc);
        return 0;
    }
}

static int decode_main_header(NUTContext *nut){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = s->pb;
    uint64_t tmp, end;
    unsigned int stream_count;
    int i, j, tmp_stream, tmp_mul, tmp_pts, tmp_size, count, tmp_res, tmp_head_idx;
    int64_t tmp_match;

    end= get_packetheader(nut, bc, 1, MAIN_STARTCODE);
    end += url_ftell(bc);

    GET_V(tmp              , tmp >=2 && tmp <= 3)
    GET_V(stream_count     , tmp > 0 && tmp <=MAX_STREAMS)

    nut->max_distance = ff_get_v(bc);
    if(nut->max_distance > 65536){
        av_log(s, AV_LOG_DEBUG, "max_distance %d\n", nut->max_distance);
        nut->max_distance= 65536;
    }

    GET_V(nut->time_base_count, tmp>0 && tmp<INT_MAX / sizeof(AVRational))
    nut->time_base= av_malloc(nut->time_base_count * sizeof(AVRational));

    for(i=0; i<nut->time_base_count; i++){
        GET_V(nut->time_base[i].num, tmp>0 && tmp<(1ULL<<31))
        GET_V(nut->time_base[i].den, tmp>0 && tmp<(1ULL<<31))
        if(ff_gcd(nut->time_base[i].num, nut->time_base[i].den) != 1){
            av_log(s, AV_LOG_ERROR, "time base invalid\n");
            return -1;
        }
    }
    tmp_pts=0;
    tmp_mul=1;
    tmp_stream=0;
    tmp_match= 1-(1LL<<62);
    tmp_head_idx= 0;
    for(i=0; i<256;){
        int tmp_flags = ff_get_v(bc);
        int tmp_fields= ff_get_v(bc);
        if(tmp_fields>0) tmp_pts   = get_s(bc);
        if(tmp_fields>1) tmp_mul   = ff_get_v(bc);
        if(tmp_fields>2) tmp_stream= ff_get_v(bc);
        if(tmp_fields>3) tmp_size  = ff_get_v(bc);
        else             tmp_size  = 0;
        if(tmp_fields>4) tmp_res   = ff_get_v(bc);
        else             tmp_res   = 0;
        if(tmp_fields>5) count     = ff_get_v(bc);
        else             count     = tmp_mul - tmp_size;
        if(tmp_fields>6) tmp_match = get_s(bc);
        if(tmp_fields>7) tmp_head_idx= ff_get_v(bc);

        while(tmp_fields-- > 8)
           ff_get_v(bc);

        if(count == 0 || i+count > 256){
            av_log(s, AV_LOG_ERROR, "illegal count %d at %d\n", count, i);
            return -1;
        }
        if(tmp_stream >= stream_count){
            av_log(s, AV_LOG_ERROR, "illegal stream number\n");
            return -1;
        }

        for(j=0; j<count; j++,i++){
            if (i == 'N') {
                nut->frame_code[i].flags= FLAG_INVALID;
                j--;
                continue;
            }
            nut->frame_code[i].flags           = tmp_flags ;
            nut->frame_code[i].pts_delta       = tmp_pts   ;
            nut->frame_code[i].stream_id       = tmp_stream;
            nut->frame_code[i].size_mul        = tmp_mul   ;
            nut->frame_code[i].size_lsb        = tmp_size+j;
            nut->frame_code[i].reserved_count  = tmp_res   ;
            nut->frame_code[i].header_idx      = tmp_head_idx;
        }
    }
    assert(nut->frame_code['N'].flags == FLAG_INVALID);

    if(end > url_ftell(bc) + 4){
        int rem= 1024;
        GET_V(nut->header_count, tmp<128U)
        nut->header_count++;
        for(i=1; i<nut->header_count; i++){
            GET_V(nut->header_len[i], tmp>0 && tmp<256);
            rem -= nut->header_len[i];
            if(rem < 0){
                av_log(s, AV_LOG_ERROR, "invalid elision header\n");
                return -1;
            }
            nut->header[i]= av_malloc(nut->header_len[i]);
            get_buffer(bc, nut->header[i], nut->header_len[i]);
        }
        assert(nut->header_len[0]==0);
    }

    if(skip_reserved(bc, end) || get_checksum(bc)){
        av_log(s, AV_LOG_ERROR, "main header checksum mismatch\n");
        return -1;
    }

    nut->stream = av_mallocz(sizeof(StreamContext)*stream_count);
    for(i=0; i<stream_count; i++){
        av_new_stream(s, i);
    }

    return 0;
}

static int decode_stream_header(NUTContext *nut){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = s->pb;
    StreamContext *stc;
    int class, stream_id;
    uint64_t tmp, end;
    AVStream *st;

    end= get_packetheader(nut, bc, 1, STREAM_STARTCODE);
    end += url_ftell(bc);

    GET_V(stream_id, tmp < s->nb_streams && !nut->stream[tmp].time_base);
    stc= &nut->stream[stream_id];

    st = s->streams[stream_id];
    if (!st)
        return AVERROR(ENOMEM);

    class = ff_get_v(bc);
    tmp = get_fourcc(bc);
    st->codec->codec_tag= tmp;
    switch(class)
    {
        case 0:
            st->codec->codec_type = CODEC_TYPE_VIDEO;
            st->codec->codec_id = codec_get_id(codec_bmp_tags, tmp);
            break;
        case 1:
            st->codec->codec_type = CODEC_TYPE_AUDIO;
            st->codec->codec_id = codec_get_id(codec_wav_tags, tmp);
            break;
        case 2:
            st->codec->codec_type = CODEC_TYPE_SUBTITLE;
            st->codec->codec_id = codec_get_id(ff_nut_subtitle_tags, tmp);
            break;
        case 3:
            st->codec->codec_type = CODEC_TYPE_DATA;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "unknown stream class (%d)\n", class);
            return -1;
    }
    if(class<3 && st->codec->codec_id == CODEC_ID_NONE)
        av_log(s, AV_LOG_ERROR, "Unknown codec?!\n");

    GET_V(stc->time_base_id    , tmp < nut->time_base_count);
    GET_V(stc->msb_pts_shift   , tmp < 16);
    stc->max_pts_distance= ff_get_v(bc);
    GET_V(stc->decode_delay    , tmp < 1000); //sanity limit, raise this if Moore's law is true
    st->codec->has_b_frames= stc->decode_delay;
    ff_get_v(bc); //stream flags

    GET_V(st->codec->extradata_size, tmp < (1<<30));
    if(st->codec->extradata_size){
        st->codec->extradata= av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
        get_buffer(bc, st->codec->extradata, st->codec->extradata_size);
    }

    if (st->codec->codec_type == CODEC_TYPE_VIDEO){
        GET_V(st->codec->width , tmp > 0)
        GET_V(st->codec->height, tmp > 0)
        st->codec->sample_aspect_ratio.num= ff_get_v(bc);
        st->codec->sample_aspect_ratio.den= ff_get_v(bc);
        if((!st->codec->sample_aspect_ratio.num) != (!st->codec->sample_aspect_ratio.den)){
            av_log(s, AV_LOG_ERROR, "invalid aspect ratio %d/%d\n", st->codec->sample_aspect_ratio.num, st->codec->sample_aspect_ratio.den);
            return -1;
        }
        ff_get_v(bc); /* csp type */
    }else if (st->codec->codec_type == CODEC_TYPE_AUDIO){
        GET_V(st->codec->sample_rate , tmp > 0)
        ff_get_v(bc); // samplerate_den
        GET_V(st->codec->channels, tmp > 0)
    }
    if(skip_reserved(bc, end) || get_checksum(bc)){
        av_log(s, AV_LOG_ERROR, "stream header %d checksum mismatch\n", stream_id);
        return -1;
    }
    stc->time_base= &nut->time_base[stc->time_base_id];
    av_set_pts_info(s->streams[stream_id], 63, stc->time_base->num, stc->time_base->den);
    return 0;
}

static void set_disposition_bits(AVFormatContext* avf, char* value, int stream_id){
    int flag = 0, i;
    for (i=0; ff_nut_dispositions[i].flag; ++i) {
        if (!strcmp(ff_nut_dispositions[i].str, value))
            flag = ff_nut_dispositions[i].flag;
    }
    if (!flag)
        av_log(avf, AV_LOG_INFO, "unknown disposition type '%s'\n", value);
    for (i = 0; i < avf->nb_streams; ++i)
        if (stream_id == i || stream_id == -1)
            avf->streams[i]->disposition |= flag;
}

static int decode_info_header(NUTContext *nut){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = s->pb;
    uint64_t tmp;
    unsigned int stream_id_plus1, chapter_start, chapter_len, count;
    int chapter_id, i;
    int64_t value, end;
    char name[256], str_value[1024], type_str[256];
    const char *type;

    end= get_packetheader(nut, bc, 1, INFO_STARTCODE);
    end += url_ftell(bc);

    GET_V(stream_id_plus1, tmp <= s->nb_streams)
    chapter_id   = get_s(bc);
    chapter_start= ff_get_v(bc);
    chapter_len  = ff_get_v(bc);
    count        = ff_get_v(bc);
    for(i=0; i<count; i++){
        get_str(bc, name, sizeof(name));
        value= get_s(bc);
        if(value == -1){
            type= "UTF-8";
            get_str(bc, str_value, sizeof(str_value));
        }else if(value == -2){
            get_str(bc, type_str, sizeof(type_str));
            type= type_str;
            get_str(bc, str_value, sizeof(str_value));
        }else if(value == -3){
            type= "s";
            value= get_s(bc);
        }else if(value == -4){
            type= "t";
            value= ff_get_v(bc);
        }else if(value < -4){
            type= "r";
            get_s(bc);
        }else{
            type= "v";
        }

        if (stream_id_plus1 < 0 || stream_id_plus1 > s->nb_streams) {
            av_log(s, AV_LOG_ERROR, "invalid stream id for info packet\n");
            continue;
        }

        if(chapter_id==0 && !strcmp(type, "UTF-8")){
            if     (!strcmp(name, "Author"))
                av_strlcpy(s->author   , str_value, sizeof(s->author));
            else if(!strcmp(name, "Title"))
                av_strlcpy(s->title    , str_value, sizeof(s->title));
            else if(!strcmp(name, "Copyright"))
                av_strlcpy(s->copyright, str_value, sizeof(s->copyright));
            else if(!strcmp(name, "Description"))
                av_strlcpy(s->comment  , str_value, sizeof(s->comment));
            else if(!strcmp(name, "Disposition"))
                set_disposition_bits(s, str_value, stream_id_plus1 - 1);
        }
    }

    if(skip_reserved(bc, end) || get_checksum(bc)){
        av_log(s, AV_LOG_ERROR, "info header checksum mismatch\n");
        return -1;
    }
    return 0;
}

static int decode_syncpoint(NUTContext *nut, int64_t *ts, int64_t *back_ptr){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = s->pb;
    int64_t end, tmp;

    nut->last_syncpoint_pos= url_ftell(bc)-8;

    end= get_packetheader(nut, bc, 1, SYNCPOINT_STARTCODE);
    end += url_ftell(bc);

    tmp= ff_get_v(bc);
    *back_ptr= nut->last_syncpoint_pos - 16*ff_get_v(bc);
    if(*back_ptr < 0)
        return -1;

    ff_nut_reset_ts(nut, nut->time_base[tmp % nut->time_base_count], tmp / nut->time_base_count);

    if(skip_reserved(bc, end) || get_checksum(bc)){
        av_log(s, AV_LOG_ERROR, "sync point checksum mismatch\n");
        return -1;
    }

    *ts= tmp / s->nb_streams * av_q2d(nut->time_base[tmp % s->nb_streams])*AV_TIME_BASE;
    ff_nut_add_sp(nut, nut->last_syncpoint_pos, *back_ptr, *ts);

    return 0;
}

static int find_and_decode_index(NUTContext *nut){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = s->pb;
    uint64_t tmp, end;
    int i, j, syncpoint_count;
    int64_t filesize= url_fsize(bc);
    int64_t *syncpoints;
    int8_t *has_keyframe;

    url_fseek(bc, filesize-12, SEEK_SET);
    url_fseek(bc, filesize-get_be64(bc), SEEK_SET);
    if(get_be64(bc) != INDEX_STARTCODE){
        av_log(s, AV_LOG_ERROR, "no index at the end\n");
        return -1;
    }

    end= get_packetheader(nut, bc, 1, INDEX_STARTCODE);
    end += url_ftell(bc);

    ff_get_v(bc); //max_pts
    GET_V(syncpoint_count, tmp < INT_MAX/8 && tmp > 0)
    syncpoints= av_malloc(sizeof(int64_t)*syncpoint_count);
    has_keyframe= av_malloc(sizeof(int8_t)*(syncpoint_count+1));
    for(i=0; i<syncpoint_count; i++){
        GET_V(syncpoints[i], tmp>0)
        if(i)
            syncpoints[i] += syncpoints[i-1];
    }

    for(i=0; i<s->nb_streams; i++){
        int64_t last_pts= -1;
        for(j=0; j<syncpoint_count;){
            uint64_t x= ff_get_v(bc);
            int type= x&1;
            int n= j;
            x>>=1;
            if(type){
                int flag= x&1;
                x>>=1;
                if(n+x >= syncpoint_count + 1){
                    av_log(s, AV_LOG_ERROR, "index overflow A\n");
                    return -1;
                }
                while(x--)
                    has_keyframe[n++]= flag;
                has_keyframe[n++]= !flag;
            }else{
                while(x != 1){
                    if(n>=syncpoint_count + 1){
                        av_log(s, AV_LOG_ERROR, "index overflow B\n");
                        return -1;
                    }
                    has_keyframe[n++]= x&1;
                    x>>=1;
                }
            }
            if(has_keyframe[0]){
                av_log(s, AV_LOG_ERROR, "keyframe before first syncpoint in index\n");
                return -1;
            }
            assert(n<=syncpoint_count+1);
            for(; j<n && j<syncpoint_count; j++){
                if(has_keyframe[j]){
                    uint64_t B, A= ff_get_v(bc);
                    if(!A){
                        A= ff_get_v(bc);
                        B= ff_get_v(bc);
                        //eor_pts[j][i] = last_pts + A + B
                    }else
                        B= 0;
                    av_add_index_entry(
                        s->streams[i],
                        16*syncpoints[j-1],
                        last_pts + A,
                        0,
                        0,
                        AVINDEX_KEYFRAME);
                    last_pts += A + B;
                }
            }
        }
    }

    if(skip_reserved(bc, end) || get_checksum(bc)){
        av_log(s, AV_LOG_ERROR, "index checksum mismatch\n");
        return -1;
    }
    return 0;
}

static int nut_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = s->pb;
    int64_t pos;
    int initialized_stream_count;

    nut->avf= s;

    /* main header */
    pos=0;
    do{
        pos= find_startcode(bc, MAIN_STARTCODE, pos)+1;
        if (pos<0+1){
            av_log(s, AV_LOG_ERROR, "No main startcode found.\n");
            return -1;
        }
    }while(decode_main_header(nut) < 0);

    /* stream headers */
    pos=0;
    for(initialized_stream_count=0; initialized_stream_count < s->nb_streams;){
        pos= find_startcode(bc, STREAM_STARTCODE, pos)+1;
        if (pos<0+1){
            av_log(s, AV_LOG_ERROR, "Not all stream headers found.\n");
            return -1;
        }
        if(decode_stream_header(nut) >= 0)
            initialized_stream_count++;
    }

    /* info headers */
    pos=0;
    for(;;){
        uint64_t startcode= find_any_startcode(bc, pos);
        pos= url_ftell(bc);

        if(startcode==0){
            av_log(s, AV_LOG_ERROR, "EOF before video frames\n");
            return -1;
        }else if(startcode == SYNCPOINT_STARTCODE){
            nut->next_startcode= startcode;
            break;
        }else if(startcode != INFO_STARTCODE){
            continue;
        }

        decode_info_header(nut);
    }

    s->data_offset= pos-8;

    if(!url_is_streamed(bc)){
        int64_t orig_pos= url_ftell(bc);
        find_and_decode_index(nut);
        url_fseek(bc, orig_pos, SEEK_SET);
    }
    assert(nut->next_startcode == SYNCPOINT_STARTCODE);

    return 0;
}

static int decode_frame_header(NUTContext *nut, int64_t *pts, int *stream_id, uint8_t *header_idx, int frame_code){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = s->pb;
    StreamContext *stc;
    int size, flags, size_mul, pts_delta, i, reserved_count;
    uint64_t tmp;

    if(url_ftell(bc) > nut->last_syncpoint_pos + nut->max_distance){
        av_log(s, AV_LOG_ERROR, "Last frame must have been damaged %"PRId64" > %"PRId64" + %d\n", url_ftell(bc), nut->last_syncpoint_pos, nut->max_distance);
        return -1;
    }

    flags          = nut->frame_code[frame_code].flags;
    size_mul       = nut->frame_code[frame_code].size_mul;
    size           = nut->frame_code[frame_code].size_lsb;
    *stream_id     = nut->frame_code[frame_code].stream_id;
    pts_delta      = nut->frame_code[frame_code].pts_delta;
    reserved_count = nut->frame_code[frame_code].reserved_count;
    *header_idx    = nut->frame_code[frame_code].header_idx;

    if(flags & FLAG_INVALID)
        return -1;
    if(flags & FLAG_CODED)
        flags ^= ff_get_v(bc);
    if(flags & FLAG_STREAM_ID){
        GET_V(*stream_id, tmp < s->nb_streams)
    }
    stc= &nut->stream[*stream_id];
    if(flags&FLAG_CODED_PTS){
        int coded_pts= ff_get_v(bc);
//FIXME check last_pts validity?
        if(coded_pts < (1<<stc->msb_pts_shift)){
            *pts=ff_lsb2full(stc, coded_pts);
        }else
            *pts=coded_pts - (1<<stc->msb_pts_shift);
    }else
        *pts= stc->last_pts + pts_delta;
    if(flags&FLAG_SIZE_MSB){
        size += size_mul*ff_get_v(bc);
    }
    if(flags&FLAG_MATCH_TIME)
        get_s(bc);
    if(flags&FLAG_HEADER_IDX)
        *header_idx= ff_get_v(bc);
    if(flags&FLAG_RESERVED)
        reserved_count= ff_get_v(bc);
    for(i=0; i<reserved_count; i++)
        ff_get_v(bc);

    if(*header_idx >= (unsigned)nut->header_count){
        av_log(s, AV_LOG_ERROR, "header_idx invalid\n");
        return -1;
    }
    if(size > 4096)
        *header_idx=0;
    size -= nut->header_len[*header_idx];

    if(flags&FLAG_CHECKSUM){
        get_be32(bc); //FIXME check this
    }else if(size > 2*nut->max_distance || FFABS(stc->last_pts - *pts) > stc->max_pts_distance){
        av_log(s, AV_LOG_ERROR, "frame size > 2max_distance and no checksum\n");
        return -1;
    }

    stc->last_pts= *pts;
    stc->last_flags= flags;

    return size;
}

static int decode_frame(NUTContext *nut, AVPacket *pkt, int frame_code){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = s->pb;
    int size, stream_id, discard;
    int64_t pts, last_IP_pts;
    StreamContext *stc;
    uint8_t header_idx;

    size= decode_frame_header(nut, &pts, &stream_id, &header_idx, frame_code);
    if(size < 0)
        return -1;

    stc= &nut->stream[stream_id];

    if (stc->last_flags & FLAG_KEY)
        stc->skip_until_key_frame=0;

    discard= s->streams[ stream_id ]->discard;
    last_IP_pts= s->streams[ stream_id ]->last_IP_pts;
    if(  (discard >= AVDISCARD_NONKEY && !(stc->last_flags & FLAG_KEY))
       ||(discard >= AVDISCARD_BIDIR && last_IP_pts != AV_NOPTS_VALUE && last_IP_pts > pts)
       || discard >= AVDISCARD_ALL
       || stc->skip_until_key_frame){
        url_fskip(bc, size);
        return 1;
    }

    av_new_packet(pkt, size + nut->header_len[header_idx]);
    memcpy(pkt->data, nut->header[header_idx], nut->header_len[header_idx]);
    pkt->pos= url_ftell(bc); //FIXME
    get_buffer(bc, pkt->data + nut->header_len[header_idx], size);

    pkt->stream_index = stream_id;
    if (stc->last_flags & FLAG_KEY)
        pkt->flags |= PKT_FLAG_KEY;
    pkt->pts = pts;

    return 0;
}

static int nut_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = s->pb;
    int i, frame_code=0, ret, skip;
    int64_t ts, back_ptr;

    for(;;){
        int64_t pos= url_ftell(bc);
        uint64_t tmp= nut->next_startcode;
        nut->next_startcode=0;

        if(tmp){
            pos-=8;
        }else{
            frame_code = get_byte(bc);
            if(url_feof(bc))
                return -1;
            if(frame_code == 'N'){
                tmp= frame_code;
                for(i=1; i<8; i++)
                    tmp = (tmp<<8) + get_byte(bc);
            }
        }
        switch(tmp){
        case MAIN_STARTCODE:
        case STREAM_STARTCODE:
        case INDEX_STARTCODE:
            skip= get_packetheader(nut, bc, 0, tmp);
            url_fseek(bc, skip, SEEK_CUR);
            break;
        case INFO_STARTCODE:
            if(decode_info_header(nut)<0)
                goto resync;
            break;
        case SYNCPOINT_STARTCODE:
            if(decode_syncpoint(nut, &ts, &back_ptr)<0)
                goto resync;
            frame_code = get_byte(bc);
        case 0:
            ret= decode_frame(nut, pkt, frame_code);
            if(ret==0)
                return 0;
            else if(ret==1) //ok but discard packet
                break;
        default:
resync:
av_log(s, AV_LOG_DEBUG, "syncing from %"PRId64"\n", pos);
            tmp= find_any_startcode(bc, nut->last_syncpoint_pos+1);
            if(tmp==0)
                return -1;
av_log(s, AV_LOG_DEBUG, "sync\n");
            nut->next_startcode= tmp;
        }
    }
}

static int64_t nut_read_timestamp(AVFormatContext *s, int stream_index, int64_t *pos_arg, int64_t pos_limit){
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = s->pb;
    int64_t pos, pts, back_ptr;
av_log(s, AV_LOG_DEBUG, "read_timestamp(X,%d,%"PRId64",%"PRId64")\n", stream_index, *pos_arg, pos_limit);

    pos= *pos_arg;
    do{
        pos= find_startcode(bc, SYNCPOINT_STARTCODE, pos)+1;
        if(pos < 1){
            assert(nut->next_startcode == 0);
            av_log(s, AV_LOG_ERROR, "read_timestamp failed.\n");
            return AV_NOPTS_VALUE;
        }
    }while(decode_syncpoint(nut, &pts, &back_ptr) < 0);
    *pos_arg = pos-1;
    assert(nut->last_syncpoint_pos == *pos_arg);

    av_log(s, AV_LOG_DEBUG, "return %"PRId64" %"PRId64"\n", pts,back_ptr );
    if     (stream_index == -1) return pts;
    else if(stream_index == -2) return back_ptr;

assert(0);
}

static int read_seek(AVFormatContext *s, int stream_index, int64_t pts, int flags){
    NUTContext *nut = s->priv_data;
    AVStream *st= s->streams[stream_index];
    syncpoint_t dummy={.ts= pts*av_q2d(st->time_base)*AV_TIME_BASE};
    syncpoint_t nopts_sp= {.ts= AV_NOPTS_VALUE, .back_ptr= AV_NOPTS_VALUE};
    syncpoint_t *sp, *next_node[2]= {&nopts_sp, &nopts_sp};
    int64_t pos, pos2, ts;
    int i;

    if(st->index_entries){
        int index= av_index_search_timestamp(st, pts, flags);
        if(index<0)
            return -1;

        pos2= st->index_entries[index].pos;
        ts  = st->index_entries[index].timestamp;
    }else{
        av_tree_find(nut->syncpoints, &dummy, ff_nut_sp_pts_cmp, next_node);
        av_log(s, AV_LOG_DEBUG, "%"PRIu64"-%"PRIu64" %"PRId64"-%"PRId64"\n", next_node[0]->pos, next_node[1]->pos,
                                                    next_node[0]->ts , next_node[1]->ts);
        pos= av_gen_search(s, -1, dummy.ts, next_node[0]->pos, next_node[1]->pos, next_node[1]->pos,
                                            next_node[0]->ts , next_node[1]->ts, AVSEEK_FLAG_BACKWARD, &ts, nut_read_timestamp);

        if(!(flags & AVSEEK_FLAG_BACKWARD)){
            dummy.pos= pos+16;
            next_node[1]= &nopts_sp;
            av_tree_find(nut->syncpoints, &dummy, ff_nut_sp_pos_cmp, next_node);
            pos2= av_gen_search(s, -2, dummy.pos, next_node[0]->pos     , next_node[1]->pos, next_node[1]->pos,
                                                next_node[0]->back_ptr, next_node[1]->back_ptr, flags, &ts, nut_read_timestamp);
            if(pos2>=0)
                pos= pos2;
            //FIXME dir but I think it does not matter
        }
        dummy.pos= pos;
        sp= av_tree_find(nut->syncpoints, &dummy, ff_nut_sp_pos_cmp, NULL);

        assert(sp);
        pos2= sp->back_ptr  - 15;
    }
    av_log(NULL, AV_LOG_DEBUG, "SEEKTO: %"PRId64"\n", pos2);
    pos= find_startcode(s->pb, SYNCPOINT_STARTCODE, pos2);
    url_fseek(s->pb, pos, SEEK_SET);
    av_log(NULL, AV_LOG_DEBUG, "SP: %"PRId64"\n", pos);
    if(pos2 > pos || pos2 + 15 < pos){
        av_log(NULL, AV_LOG_ERROR, "no syncpoint at backptr pos\n");
    }
    for(i=0; i<s->nb_streams; i++)
        nut->stream[i].skip_until_key_frame=1;

    return 0;
}

static int nut_read_close(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;

    av_freep(&nut->time_base);
    av_freep(&nut->stream);

    return 0;
}

#ifdef CONFIG_NUT_DEMUXER
AVInputFormat nut_demuxer = {
    "nut",
    "nut format",
    sizeof(NUTContext),
    nut_probe,
    nut_read_header,
    nut_read_packet,
    nut_read_close,
    read_seek,
    .extensions = "nut",
};
#endif
