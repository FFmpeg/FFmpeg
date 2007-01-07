/*
 * "NUT" Container Format muxer and demuxer (DRAFT-200403??)
 * Copyright (c) 2003 Alex Beregszaszi
 * Copyright (c) 2004 Michael Niedermayer
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
 *
 *
 * Visit the official site at http://www.nut.hu/
 *
 */

/*
 * TODO:
 * - index writing
 * - index packet reading support
*/

//#define DEBUG 1

#include <limits.h>
#include "avformat.h"
#include "mpegaudio.h"
#include "riff.h"
#include "adler32.h"

#undef NDEBUG
#include <assert.h>

//#define TRACE

//from /dev/random

#define     MAIN_STARTCODE (0x7A561F5F04ADULL + (((uint64_t)('N'<<8) + 'M')<<48))
#define   STREAM_STARTCODE (0x11405BF2F9DBULL + (((uint64_t)('N'<<8) + 'S')<<48))
#define KEYFRAME_STARTCODE (0xE4ADEECA4569ULL + (((uint64_t)('N'<<8) + 'K')<<48))
#define    INDEX_STARTCODE (0xDD672F23E64EULL + (((uint64_t)('N'<<8) + 'X')<<48))
#define     INFO_STARTCODE (0xAB68B596BA78ULL + (((uint64_t)('N'<<8) + 'I')<<48))

#define ID_STRING "nut/multimedia container\0"

#define MAX_DISTANCE (1024*16-1)
#define MAX_SHORT_DISTANCE (1024*4-1)

#define FLAG_DATA_SIZE       1
#define FLAG_KEY_FRAME       2
#define FLAG_INVALID         4

typedef struct {
    uint8_t flags;
    uint8_t stream_id_plus1;
    uint16_t size_mul;
    uint16_t size_lsb;
    int16_t timestamp_delta;
    uint8_t reserved_count;
} FrameCode;

typedef struct {
    int last_key_frame;
    int msb_timestamp_shift;
    int rate_num;
    int rate_den;
    int64_t last_pts;
    int64_t last_sync_pos;                    ///<pos of last 1/2 type frame
    int decode_delay;
} StreamContext;

typedef struct {
    AVFormatContext *avf;
    int written_packet_size;
    int64_t packet_start[3]; //0-> startcode less, 1-> short startcode 2-> long startcodes
    FrameCode frame_code[256];
    unsigned int stream_count;
    uint64_t next_startcode;     ///< stores the next startcode if it has alraedy been parsed but the stream isnt seekable
    StreamContext *stream;
    int max_distance;
    int max_short_distance;
    int rate_num;
    int rate_den;
    int short_startcode;
} NUTContext;

static char *info_table[][2]={
        {NULL                   ,  NULL }, // end
        {NULL                   ,  NULL },
        {NULL                   , "UTF8"},
        {NULL                   , "v"},
        {NULL                   , "s"},
        {"StreamId"             , "v"},
        {"SegmentId"            , "v"},
        {"StartTimestamp"       , "v"},
        {"EndTimestamp"         , "v"},
        {"Author"               , "UTF8"},
        {"Title"                , "UTF8"},
        {"Description"          , "UTF8"},
        {"Copyright"            , "UTF8"},
        {"Encoder"              , "UTF8"},
        {"Keyword"              , "UTF8"},
        {"Cover"                , "JPEG"},
        {"Cover"                , "PNG"},
};

static void update(NUTContext *nut, int stream_index, int64_t frame_start, int frame_type, int frame_code, int key_frame, int size, int64_t pts){
    StreamContext *stream= &nut->stream[stream_index];

    stream->last_key_frame= key_frame;
    nut->packet_start[ frame_type ]= frame_start;
    stream->last_pts= pts;
}

static void reset(AVFormatContext *s, int64_t global_ts){
    NUTContext *nut = s->priv_data;
    int i;

    for(i=0; i<s->nb_streams; i++){
        StreamContext *stream= &nut->stream[i];

        stream->last_key_frame= 1;

        stream->last_pts= av_rescale(global_ts, stream->rate_num*(int64_t)nut->rate_den, stream->rate_den*(int64_t)nut->rate_num);
    }
}

static void build_frame_code(AVFormatContext *s){
    NUTContext *nut = s->priv_data;
    int key_frame, index, pred, stream_id;
    int start=0;
    int end= 255;
    int keyframe_0_esc= s->nb_streams > 2;
    int pred_table[10];

    if(keyframe_0_esc){
        /* keyframe = 0 escape */
        FrameCode *ft= &nut->frame_code[start];
        ft->flags= FLAG_DATA_SIZE;
        ft->stream_id_plus1= 0;
        ft->size_mul=1;
        ft->timestamp_delta=0;
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
                ft->flags= FLAG_KEY_FRAME*key_frame;
                ft->flags|= FLAG_DATA_SIZE;
                ft->stream_id_plus1= stream_id + 1;
                ft->size_mul=1;
                ft->timestamp_delta=0;
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
                    ft->flags= FLAG_KEY_FRAME*key_frame;
                    ft->stream_id_plus1= stream_id + 1;
                    ft->size_mul=frame_bytes + 2;
                    ft->size_lsb=frame_bytes + pred;
                    ft->timestamp_delta=pts;
                    start2++;
                }
            }
        }else{
            FrameCode *ft= &nut->frame_code[start2];
            ft->flags= FLAG_KEY_FRAME | FLAG_DATA_SIZE;
            ft->stream_id_plus1= stream_id + 1;
            ft->size_mul=1;
            ft->timestamp_delta=1;
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
                ft->flags= FLAG_KEY_FRAME*key_frame;
                ft->flags|= FLAG_DATA_SIZE;
                ft->stream_id_plus1= stream_id + 1;
//FIXME use single byte size and pred from last
                ft->size_mul= end3-start3;
                ft->size_lsb= index - start3;
                ft->timestamp_delta= pred_table[pred];
            }
        }
    }
    memmove(&nut->frame_code['N'+1], &nut->frame_code['N'], sizeof(FrameCode)*(255-'N'));
    nut->frame_code['N'].flags= FLAG_INVALID;
}

static uint64_t get_v(ByteIOContext *bc)
{
    uint64_t val = 0;

    for(;;)
    {
        int tmp = get_byte(bc);

        if (tmp&0x80)
            val= (val<<7) + tmp - 0x80;
        else{
//av_log(NULL, AV_LOG_DEBUG, "get_v()= %"PRId64"\n", (val<<7) + tmp);
            return (val<<7) + tmp;
        }
    }
    return -1;
}

static int get_str(ByteIOContext *bc, char *string, unsigned int maxlen){
    unsigned int len= get_v(bc);

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
    int64_t v = get_v(bc) + 1;

    if (v&1) return -(v>>1);
    else     return  (v>>1);
}

static uint64_t get_vb(ByteIOContext *bc){
    uint64_t val=0;
    unsigned int i= get_v(bc);

    if(i>8)
        return UINT64_MAX;

    while(i--)
        val = (val<<8) + get_byte(bc);

//av_log(NULL, AV_LOG_DEBUG, "get_vb()= %"PRId64"\n", val);
    return val;
}

#ifdef TRACE
static inline uint64_t get_v_trace(ByteIOContext *bc, char *file, char *func, int line){
    uint64_t v= get_v(bc);

    printf("get_v %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);
    return v;
}

static inline int64_t get_s_trace(ByteIOContext *bc, char *file, char *func, int line){
    int64_t v= get_s(bc);

    printf("get_s %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);
    return v;
}

static inline uint64_t get_vb_trace(ByteIOContext *bc, char *file, char *func, int line){
    uint64_t v= get_vb(bc);

    printf("get_vb %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);
    return v;
}
#define get_v(bc)  get_v_trace(bc, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_s(bc)  get_s_trace(bc, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define get_vb(bc)  get_vb_trace(bc, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#endif


static int get_packetheader(NUTContext *nut, ByteIOContext *bc, int calculate_checksum)
{
    int64_t start, size;
    start= url_ftell(bc) - 8;

    size= get_v(bc);

    init_checksum(bc, calculate_checksum ? av_adler32_update : NULL, 1);

    nut->packet_start[2] = start;
    nut->written_packet_size= size;

    return size;
}

static int check_checksum(ByteIOContext *bc){
    unsigned long checksum= get_checksum(bc);
    return checksum != get_be32(bc);
}

/**
 *
 */
static int get_length(uint64_t val){
    int i;

    for (i=7; val>>i; i+=7);

    return i;
}

static uint64_t find_any_startcode(ByteIOContext *bc, int64_t pos){
    uint64_t state=0;

    if(pos >= 0)
        url_fseek(bc, pos, SEEK_SET); //note, this may fail if the stream isnt seekable, but that shouldnt matter, as in this case we simply start where we are currently

    while(!url_feof(bc)){
        state= (state<<8) | get_byte(bc);
        if((state>>56) != 'N')
            continue;
        switch(state){
        case MAIN_STARTCODE:
        case STREAM_STARTCODE:
        case KEYFRAME_STARTCODE:
        case INFO_STARTCODE:
        case INDEX_STARTCODE:
            return state;
        }
    }

    return 0;
}

/**
 * find the given startcode.
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

static int64_t lsb2full(StreamContext *stream, int64_t lsb){
    int64_t mask = (1<<stream->msb_timestamp_shift)-1;
    int64_t delta= stream->last_pts - mask/2;
    return  ((lsb - delta)&mask) + delta;
}

#ifdef CONFIG_MUXERS

static void put_v(ByteIOContext *bc, uint64_t val)
{
    int i;

//av_log(NULL, AV_LOG_DEBUG, "put_v()= %"PRId64"\n", val);
    val &= 0x7FFFFFFFFFFFFFFFULL; // FIXME can only encode upto 63 bits currently
    i= get_length(val);

    for (i-=7; i>0; i-=7){
        put_byte(bc, 0x80 | (val>>i));
    }

    put_byte(bc, val&0x7f);
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
    if (val<=0) put_v(bc, -2*val  );
    else        put_v(bc,  2*val-1);
}

static void put_vb(ByteIOContext *bc, uint64_t val){
    int i;

    for (i=8; val>>i; i+=8);

    put_v(bc, i>>3);
    for(i-=8; i>=0; i-=8)
        put_byte(bc, (val>>i)&0xFF);
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

static inline void put_vb_trace(ByteIOContext *bc, uint64_t v, char *file, char *func, int line){
    printf("get_vb %5"PRId64" / %"PRIX64" in %s %s:%d\n", v, v, file, func, line);

    put_vb(bc, v);
}
#define put_v(bc, v)  put_v_trace(bc, v, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define put_s(bc, v)  put_s_trace(bc, v, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#define put_vb(bc, v)  put_vb_trace(bc, v, __FILE__, __PRETTY_FUNCTION__, __LINE__)
#endif

static int put_packetheader(NUTContext *nut, ByteIOContext *bc, int max_size, int calculate_checksum)
{
    put_flush_packet(bc);
    nut->packet_start[2]= url_ftell(bc) - 8;
    nut->written_packet_size = max_size;

    /* packet header */
    put_v(bc, nut->written_packet_size); /* forward ptr */

    if(calculate_checksum)
        init_checksum(bc, av_adler32_update, 1);

    return 0;
}

/**
 *
 * must not be called more then once per packet
 */
static int update_packetheader(NUTContext *nut, ByteIOContext *bc, int additional_size, int calculate_checksum){
    int64_t start= nut->packet_start[2];
    int64_t cur= url_ftell(bc);
    int size= cur - start - get_length(nut->written_packet_size)/7 - 8;

    if(calculate_checksum)
        size += 4;

    if(size != nut->written_packet_size){
        int i;

        assert( size <= nut->written_packet_size );

        url_fseek(bc, start + 8, SEEK_SET);
        for(i=get_length(size); i < get_length(nut->written_packet_size); i+=7)
            put_byte(bc, 0x80);
        put_v(bc, size);

        url_fseek(bc, cur, SEEK_SET);
        nut->written_packet_size= size; //FIXME may fail if multiple updates with differing sizes, as get_length may differ

        if(calculate_checksum)
            put_be32(bc, get_checksum(bc));
    }

    return 0;
}

static int nut_write_header(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    AVCodecContext *codec;
    int i, j, tmp_time, tmp_flags,tmp_stream, tmp_mul, tmp_size, tmp_fields;

    if (strcmp(s->filename, "./data/b-libav.nut")) {
        av_log(s, AV_LOG_ERROR, " libavformat NUT is non-compliant and disabled\n");
        return -1;
    }

    nut->avf= s;

    nut->stream =
        av_mallocz(sizeof(StreamContext)*s->nb_streams);


    put_buffer(bc, ID_STRING, strlen(ID_STRING));
    put_byte(bc, 0);
    nut->packet_start[2]= url_ftell(bc);

    /* main header */
    put_be64(bc, MAIN_STARTCODE);
    put_packetheader(nut, bc, 120+5*256, 1);
    put_v(bc, 2); /* version */
    put_v(bc, s->nb_streams);
    put_v(bc, MAX_DISTANCE);
    put_v(bc, MAX_SHORT_DISTANCE);

    put_v(bc, nut->rate_num=1);
    put_v(bc, nut->rate_den=2);
    put_v(bc, nut->short_startcode=0x4EFE79);

    build_frame_code(s);
    assert(nut->frame_code['N'].flags == FLAG_INVALID);

    tmp_time= tmp_flags= tmp_stream= tmp_mul= tmp_size= /*tmp_res=*/ INT_MAX;
    for(i=0; i<256;){
        tmp_fields=0;
        tmp_size= 0;
        if(tmp_time   != nut->frame_code[i].timestamp_delta) tmp_fields=1;
        if(tmp_mul    != nut->frame_code[i].size_mul       ) tmp_fields=2;
        if(tmp_stream != nut->frame_code[i].stream_id_plus1) tmp_fields=3;
        if(tmp_size   != nut->frame_code[i].size_lsb       ) tmp_fields=4;
//        if(tmp_res    != nut->frame_code[i].res            ) tmp_fields=5;

        tmp_time  = nut->frame_code[i].timestamp_delta;
        tmp_flags = nut->frame_code[i].flags;
        tmp_stream= nut->frame_code[i].stream_id_plus1;
        tmp_mul   = nut->frame_code[i].size_mul;
        tmp_size  = nut->frame_code[i].size_lsb;
//        tmp_res   = nut->frame_code[i].res;

        for(j=0; i<256; j++,i++){
            if(nut->frame_code[i].timestamp_delta != tmp_time  ) break;
            if(nut->frame_code[i].flags           != tmp_flags ) break;
            if(nut->frame_code[i].stream_id_plus1 != tmp_stream) break;
            if(nut->frame_code[i].size_mul        != tmp_mul   ) break;
            if(nut->frame_code[i].size_lsb        != tmp_size+j) break;
//            if(nut->frame_code[i].res             != tmp_res   ) break;
        }
        if(j != tmp_mul - tmp_size) tmp_fields=6;

        put_v(bc, tmp_flags);
        put_v(bc, tmp_fields);
        if(tmp_fields>0) put_s(bc, tmp_time);
        if(tmp_fields>1) put_v(bc, tmp_mul);
        if(tmp_fields>2) put_v(bc, tmp_stream);
        if(tmp_fields>3) put_v(bc, tmp_size);
        if(tmp_fields>4) put_v(bc, 0 /*tmp_res*/);
        if(tmp_fields>5) put_v(bc, j);
    }

    update_packetheader(nut, bc, 0, 1);

    /* stream headers */
    for (i = 0; i < s->nb_streams; i++)
    {
        int nom, denom, ssize;

        codec = s->streams[i]->codec;

        put_be64(bc, STREAM_STARTCODE);
        put_packetheader(nut, bc, 120 + codec->extradata_size, 1);
        put_v(bc, i /*s->streams[i]->index*/);
        switch(codec->codec_type){
        case CODEC_TYPE_VIDEO: put_v(bc, 0); break;
        case CODEC_TYPE_AUDIO: put_v(bc, 1); break;
//        case CODEC_TYPE_TEXT : put_v(bc, 2); break;
        case CODEC_TYPE_DATA : put_v(bc, 3); break;
        default: return -1;
        }
        if (codec->codec_tag)
            put_vb(bc, codec->codec_tag);
        else if (codec->codec_type == CODEC_TYPE_VIDEO)
        {
            put_vb(bc, codec_get_bmp_tag(codec->codec_id));
        }
        else if (codec->codec_type == CODEC_TYPE_AUDIO)
        {
            put_vb(bc, codec_get_wav_tag(codec->codec_id));
        }
        else
            put_vb(bc, 0);

        ff_parse_specific_params(codec, &nom, &ssize, &denom);

        nut->stream[i].rate_num= nom;
        nut->stream[i].rate_den= denom;
        av_set_pts_info(s->streams[i], 60, denom, nom);

        put_v(bc, codec->bit_rate);
        put_vb(bc, 0); /* no language code */
        put_v(bc, nom);
        put_v(bc, denom);
        if(nom / denom < 1000)
            nut->stream[i].msb_timestamp_shift = 7;
        else
            nut->stream[i].msb_timestamp_shift = 14;
        put_v(bc, nut->stream[i].msb_timestamp_shift);
        put_v(bc, codec->has_b_frames);
        put_byte(bc, 0); /* flags: 0x1 - fixed_fps, 0x2 - index_present */

        if(codec->extradata_size){
            put_v(bc, 1);
            put_v(bc, codec->extradata_size);
            put_buffer(bc, codec->extradata, codec->extradata_size);
        }
        put_v(bc, 0); /* end of codec specific headers */

        switch(codec->codec_type)
        {
            case CODEC_TYPE_AUDIO:
                put_v(bc, codec->sample_rate);
                put_v(bc, 1);
                put_v(bc, codec->channels);
                break;
            case CODEC_TYPE_VIDEO:
                put_v(bc, codec->width);
                put_v(bc, codec->height);
                put_v(bc, codec->sample_aspect_ratio.num);
                put_v(bc, codec->sample_aspect_ratio.den);
                put_v(bc, 0); /* csp type -- unknown */
                break;
            default:
                break;
        }
        update_packetheader(nut, bc, 0, 1);
    }

    /* info header */
    put_be64(bc, INFO_STARTCODE);
    put_packetheader(nut, bc, 30+strlen(s->author)+strlen(s->title)+
        strlen(s->comment)+strlen(s->copyright)+strlen(LIBAVFORMAT_IDENT), 1);
    if (s->author[0])
    {
        put_v(bc, 9); /* type */
        put_str(bc, s->author);
    }
    if (s->title[0])
    {
        put_v(bc, 10); /* type */
        put_str(bc, s->title);
    }
    if (s->comment[0])
    {
        put_v(bc, 11); /* type */
        put_str(bc, s->comment);
    }
    if (s->copyright[0])
    {
        put_v(bc, 12); /* type */
        put_str(bc, s->copyright);
    }
    /* encoder */
    if(!(s->streams[0]->codec->flags & CODEC_FLAG_BITEXACT)){
        put_v(bc, 13); /* type */
        put_str(bc, LIBAVFORMAT_IDENT);
    }

    put_v(bc, 0); /* eof info */
    update_packetheader(nut, bc, 0, 1);

    put_flush_packet(bc);

    return 0;
}

static int nut_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    NUTContext *nut = s->priv_data;
    StreamContext *stream= &nut->stream[pkt->stream_index];
    ByteIOContext *bc = &s->pb;
    int key_frame = 0, full_pts=0;
    AVCodecContext *enc;
    int64_t coded_pts;
    int frame_type, best_length, frame_code, flags, i, size_mul, size_lsb, time_delta;
    const int64_t frame_start= url_ftell(bc);
    int64_t pts= pkt->pts;
    int size= pkt->size;
    int stream_index= pkt->stream_index;

    enc = s->streams[stream_index]->codec;
    key_frame = !!(pkt->flags & PKT_FLAG_KEY);

    frame_type=0;
    if(frame_start + size + 20 - FFMAX(nut->packet_start[1], nut->packet_start[2]) > MAX_DISTANCE)
        frame_type=2;
    if(key_frame && !stream->last_key_frame)
        frame_type=2;

    if(frame_type>1){
        int64_t global_ts= av_rescale(pts, stream->rate_den*(int64_t)nut->rate_num, stream->rate_num*(int64_t)nut->rate_den);
        reset(s, global_ts);
        put_be64(bc, KEYFRAME_STARTCODE);
        put_v(bc, global_ts);
    }
    assert(stream->last_pts != AV_NOPTS_VALUE);
    coded_pts = pts & ((1<<stream->msb_timestamp_shift)-1);
    if(lsb2full(stream, coded_pts) != pts)
        full_pts=1;

    if(full_pts)
        coded_pts= pts + (1<<stream->msb_timestamp_shift);

    best_length=INT_MAX;
    frame_code= -1;
    for(i=0; i<256; i++){
        int stream_id_plus1= nut->frame_code[i].stream_id_plus1;
        int fc_key_frame;
        int length=0;
        size_mul= nut->frame_code[i].size_mul;
        size_lsb= nut->frame_code[i].size_lsb;
        time_delta= nut->frame_code[i].timestamp_delta;
        flags= nut->frame_code[i].flags;

        assert(size_mul > size_lsb);

        if(stream_id_plus1 == 0) length+= get_length(stream_index);
        else if(stream_id_plus1 - 1 != stream_index)
            continue;
        fc_key_frame= !!(flags & FLAG_KEY_FRAME);

        assert(key_frame==0 || key_frame==1);
        if(fc_key_frame != key_frame)
            continue;

        if(flags & FLAG_DATA_SIZE){
            if(size % size_mul != size_lsb)
                continue;
            length += get_length(size / size_mul);
        }else if(size != size_lsb)
            continue;

        if(full_pts && time_delta)
            continue;

        if(!time_delta){
            length += get_length(coded_pts);
        }else{
            if(time_delta != pts - stream->last_pts)
                continue;
        }

        if(length < best_length){
            best_length= length;
            frame_code=i;
        }
//    av_log(s, AV_LOG_DEBUG, "%d %d %d %d %d %d %d %d %d %d\n", key_frame, frame_type, full_pts, size, stream_index, flags, size_mul, size_lsb, stream_id_plus1, length);
    }

    assert(frame_code != -1);
    flags= nut->frame_code[frame_code].flags;
    size_mul= nut->frame_code[frame_code].size_mul;
    size_lsb= nut->frame_code[frame_code].size_lsb;
    time_delta= nut->frame_code[frame_code].timestamp_delta;
#ifdef TRACE
    best_length /= 7;
    best_length ++; //frame_code
    if(frame_type==2){
        best_length += 8; // startcode
    }
    av_log(s, AV_LOG_DEBUG, "kf:%d ft:%d pt:%d fc:%2X len:%2d size:%d stream:%d flag:%d mul:%d lsb:%d s+1:%d pts_delta:%d pts:%"PRId64" fs:%"PRId64"\n", key_frame, frame_type, full_pts ? 1 : 0, frame_code, best_length, size, stream_index, flags, size_mul, size_lsb, nut->frame_code[frame_code].stream_id_plus1,(int)(pts - stream->last_pts), pts, frame_start);
//    av_log(s, AV_LOG_DEBUG, "%d %d %d\n", stream->lru_pts_delta[0], stream->lru_pts_delta[1], stream->lru_pts_delta[2]);
#endif

    assert(frame_type != 1); //short startcode not implemented yet
    put_byte(bc, frame_code);

    if(nut->frame_code[frame_code].stream_id_plus1 == 0)
        put_v(bc, stream_index);
    if (!time_delta){
        put_v(bc, coded_pts);
    }
    if(flags & FLAG_DATA_SIZE)
        put_v(bc, size / size_mul);
    else
        assert(size == size_lsb);
    if(size > MAX_DISTANCE){
        assert(frame_type > 1);
    }

    put_buffer(bc, pkt->data, size);

    update(nut, stream_index, frame_start, frame_type, frame_code, key_frame, size, pts);

    return 0;
}

static int nut_write_trailer(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;

#if 0
    int i;

    /* WRITE INDEX */

    for (i = 0; s->nb_streams; i++)
    {
        put_be64(bc, INDEX_STARTCODE);
        put_packetheader(nut, bc, 64, 1);
        put_v(bc, s->streams[i]->id);
        put_v(bc, ...);
        update_packetheader(nut, bc, 0, 1);
    }
#endif

    put_flush_packet(bc);

    av_freep(&nut->stream);

    return 0;
}
#endif //CONFIG_MUXERS

static int nut_probe(AVProbeData *p)
{
    int i;
    uint64_t code= 0xff;

    for (i = 0; i < p->buf_size; i++) {
        code = (code << 8) | p->buf[i];
        if (code == MAIN_STARTCODE)
            return AVPROBE_SCORE_MAX;
    }
    return 0;
}

static int decode_main_header(NUTContext *nut){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = &s->pb;
    uint64_t tmp;
    int i, j, tmp_stream, tmp_mul, tmp_time, tmp_size, count, tmp_res;

    get_packetheader(nut, bc, 1);

    tmp = get_v(bc);
    if (tmp != 2){
        av_log(s, AV_LOG_ERROR, "bad version (%"PRId64")\n", tmp);
        return -1;
    }

    nut->stream_count = get_v(bc);
    if(nut->stream_count > MAX_STREAMS){
        av_log(s, AV_LOG_ERROR, "too many streams\n");
        return -1;
    }
    nut->max_distance = get_v(bc);
    nut->max_short_distance = get_v(bc);
    nut->rate_num= get_v(bc);
    nut->rate_den= get_v(bc);
    nut->short_startcode= get_v(bc);
    if(nut->short_startcode>>16 != 'N'){
        av_log(s, AV_LOG_ERROR, "invalid short startcode %X\n", nut->short_startcode);
        return -1;
    }

    for(i=0; i<256;){
        int tmp_flags = get_v(bc);
        int tmp_fields= get_v(bc);
        if(tmp_fields>0) tmp_time  = get_s(bc);
        if(tmp_fields>1) tmp_mul   = get_v(bc);
        if(tmp_fields>2) tmp_stream= get_v(bc);
        if(tmp_fields>3) tmp_size  = get_v(bc);
        else             tmp_size  = 0;
        if(tmp_fields>4) tmp_res   = get_v(bc);
        else             tmp_res   = 0;
        if(tmp_fields>5) count     = get_v(bc);
        else             count     = tmp_mul - tmp_size;

        while(tmp_fields-- > 6)
           get_v(bc);

        if(count == 0 || i+count > 256){
            av_log(s, AV_LOG_ERROR, "illegal count %d at %d\n", count, i);
            return -1;
        }
        if(tmp_stream > nut->stream_count + 1){
            av_log(s, AV_LOG_ERROR, "illegal stream number\n");
            return -1;
        }

        for(j=0; j<count; j++,i++){
            nut->frame_code[i].flags           = tmp_flags ;
            nut->frame_code[i].timestamp_delta = tmp_time  ;
            nut->frame_code[i].stream_id_plus1 = tmp_stream;
            nut->frame_code[i].size_mul        = tmp_mul   ;
            nut->frame_code[i].size_lsb        = tmp_size+j;
            nut->frame_code[i].reserved_count  = tmp_res   ;
        }
    }
    if(nut->frame_code['N'].flags != FLAG_INVALID){
        av_log(s, AV_LOG_ERROR, "illegal frame_code table\n");
        return -1;
    }

    if(check_checksum(bc)){
        av_log(s, AV_LOG_ERROR, "Main header checksum mismatch\n");
        return -1;
    }

    return 0;
}

static int decode_stream_header(NUTContext *nut){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = &s->pb;
    int class, nom, denom, stream_id;
    uint64_t tmp;
    AVStream *st;

    get_packetheader(nut, bc, 1);
    stream_id= get_v(bc);
    if(stream_id >= nut->stream_count || s->streams[stream_id])
        return -1;

    st = av_new_stream(s, stream_id);
    if (!st)
        return AVERROR_NOMEM;

    class = get_v(bc);
    tmp = get_vb(bc);
    st->codec->codec_tag= tmp;
    switch(class)
    {
        case 0:
            st->codec->codec_type = CODEC_TYPE_VIDEO;
            st->codec->codec_id = codec_get_bmp_id(tmp);
            if (st->codec->codec_id == CODEC_ID_NONE)
                av_log(s, AV_LOG_ERROR, "Unknown codec?!\n");
            break;
        case 1:
        case 32: //compatibility
            st->codec->codec_type = CODEC_TYPE_AUDIO;
            st->codec->codec_id = codec_get_wav_id(tmp);
            if (st->codec->codec_id == CODEC_ID_NONE)
                av_log(s, AV_LOG_ERROR, "Unknown codec?!\n");
            break;
        case 2:
//            st->codec->codec_type = CODEC_TYPE_TEXT;
//            break;
        case 3:
            st->codec->codec_type = CODEC_TYPE_DATA;
            break;
        default:
            av_log(s, AV_LOG_ERROR, "Unknown stream class (%d)\n", class);
            return -1;
    }
    s->bit_rate += get_v(bc);
    get_vb(bc); /* language code */
    nom = get_v(bc);
    denom = get_v(bc);
    nut->stream[stream_id].msb_timestamp_shift = get_v(bc);
    st->codec->has_b_frames=
    nut->stream[stream_id].decode_delay= get_v(bc);
    get_byte(bc); /* flags */

    /* codec specific data headers */
    while(get_v(bc) != 0){
        st->codec->extradata_size= get_v(bc);
        if((unsigned)st->codec->extradata_size > (1<<30))
            return -1;
        st->codec->extradata= av_mallocz(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
        get_buffer(bc, st->codec->extradata, st->codec->extradata_size);
//            url_fskip(bc, get_v(bc));
    }

    if (st->codec->codec_type == CODEC_TYPE_VIDEO) /* VIDEO */
    {
        st->codec->width = get_v(bc);
        st->codec->height = get_v(bc);
        st->codec->sample_aspect_ratio.num= get_v(bc);
        st->codec->sample_aspect_ratio.den= get_v(bc);
        get_v(bc); /* csp type */
    }
    if (st->codec->codec_type == CODEC_TYPE_AUDIO) /* AUDIO */
    {
        st->codec->sample_rate = get_v(bc);
        get_v(bc); // samplerate_den
        st->codec->channels = get_v(bc);
    }
    if(check_checksum(bc)){
        av_log(s, AV_LOG_ERROR, "Stream header %d checksum mismatch\n", stream_id);
        return -1;
    }
    av_set_pts_info(s->streams[stream_id], 60, denom, nom);
    nut->stream[stream_id].rate_num= nom;
    nut->stream[stream_id].rate_den= denom;
    return 0;
}

static int decode_info_header(NUTContext *nut){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = &s->pb;

    get_packetheader(nut, bc, 1);

    for(;;){
        int id= get_v(bc);
        char *name, *type, custom_name[256], custom_type[256];

        if(!id)
            break;
        else if(id >= sizeof(info_table)/sizeof(info_table[0])){
            av_log(s, AV_LOG_ERROR, "info id is too large %d %zd\n", id, sizeof(info_table)/sizeof(info_table[0]));
            return -1;
        }

        type= info_table[id][1];
        name= info_table[id][0];
//av_log(s, AV_LOG_DEBUG, "%d %s %s\n", id, type, name);

        if(!type){
            get_str(bc, custom_type, sizeof(custom_type));
            type= custom_type;
        }
        if(!name){
            get_str(bc, custom_name, sizeof(custom_name));
            name= custom_name;
        }

        if(!strcmp(type, "v")){
            get_v(bc);
        }else{
            if(!strcmp(name, "Author"))
                get_str(bc, s->author, sizeof(s->author));
            else if(!strcmp(name, "Title"))
                get_str(bc, s->title, sizeof(s->title));
            else if(!strcmp(name, "Copyright"))
                get_str(bc, s->copyright, sizeof(s->copyright));
            else if(!strcmp(name, "Description"))
                get_str(bc, s->comment, sizeof(s->comment));
            else
                get_str(bc, NULL, 0);
        }
    }
    if(check_checksum(bc)){
        av_log(s, AV_LOG_ERROR, "Info header checksum mismatch\n");
        return -1;
    }
    return 0;
}

static int nut_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    int64_t pos;
    int inited_stream_count;

    nut->avf= s;

    /* main header */
    pos=0;
    for(;;){
        pos= find_startcode(bc, MAIN_STARTCODE, pos)+1;
        if (pos<0){
            av_log(s, AV_LOG_ERROR, "no main startcode found\n");
            return -1;
        }
        if(decode_main_header(nut) >= 0)
            break;
    }


    s->bit_rate = 0;

    nut->stream = av_malloc(sizeof(StreamContext)*nut->stream_count);

    /* stream headers */
    pos=0;
    for(inited_stream_count=0; inited_stream_count < nut->stream_count;){
        pos= find_startcode(bc, STREAM_STARTCODE, pos)+1;
        if (pos<0+1){
            av_log(s, AV_LOG_ERROR, "not all stream headers found\n");
            return -1;
        }
        if(decode_stream_header(nut) >= 0)
            inited_stream_count++;
    }

    /* info headers */
    pos=0;
    for(;;){
        uint64_t startcode= find_any_startcode(bc, pos);
        pos= url_ftell(bc);

        if(startcode==0){
            av_log(s, AV_LOG_ERROR, "EOF before video frames\n");
            return -1;
        }else if(startcode == KEYFRAME_STARTCODE){
            nut->next_startcode= startcode;
            break;
        }else if(startcode != INFO_STARTCODE){
            continue;
        }

        decode_info_header(nut);
    }

    return 0;
}

static int decode_frame_header(NUTContext *nut, int *key_frame_ret, int64_t *pts_ret, int *stream_id_ret, int frame_code, int frame_type, int64_t frame_start){
    AVFormatContext *s= nut->avf;
    StreamContext *stream;
    ByteIOContext *bc = &s->pb;
    int size, flags, size_mul, size_lsb, stream_id, time_delta;
    int64_t pts = 0;

    if(frame_type < 2 && frame_start - nut->packet_start[2] > nut->max_distance){
        av_log(s, AV_LOG_ERROR, "last frame must have been damaged\n");
        return -1;
    }

    if(frame_type)
        nut->packet_start[ frame_type ]= frame_start; //otherwise 1 goto 1 may happen

    flags= nut->frame_code[frame_code].flags;
    size_mul= nut->frame_code[frame_code].size_mul;
    size_lsb= nut->frame_code[frame_code].size_lsb;
    stream_id= nut->frame_code[frame_code].stream_id_plus1 - 1;
    time_delta= nut->frame_code[frame_code].timestamp_delta;

    if(stream_id==-1)
        stream_id= get_v(bc);
    if(stream_id >= s->nb_streams){
        av_log(s, AV_LOG_ERROR, "illegal stream_id\n");
        return -1;
    }
    stream= &nut->stream[stream_id];

//    av_log(s, AV_LOG_DEBUG, "ft:%d ppts:%d %d %d\n", frame_type, stream->lru_pts_delta[0], stream->lru_pts_delta[1], stream->lru_pts_delta[2]);

    *key_frame_ret= !!(flags & FLAG_KEY_FRAME);

    if(!time_delta){
        int64_t mask = (1<<stream->msb_timestamp_shift)-1;
        pts= get_v(bc);
        if(pts > mask){
            pts -= mask+1;
        }else{
            if(stream->last_pts == AV_NOPTS_VALUE){
                av_log(s, AV_LOG_ERROR, "no reference pts available\n");
                return -1;
            }
            pts= lsb2full(stream, pts);
        }
    }else{
        if(stream->last_pts == AV_NOPTS_VALUE){
            av_log(s, AV_LOG_ERROR, "no reference pts available\n");
            return -1;
        }
        pts= stream->last_pts + time_delta;
    }

    if(*key_frame_ret){
//        av_log(s, AV_LOG_DEBUG, "stream:%d start:%"PRId64" pts:%"PRId64" length:%"PRId64"\n",stream_id, frame_start, av_pts, frame_start - nut->stream[stream_id].last_sync_pos);
        av_add_index_entry(
            s->streams[stream_id],
            frame_start,
            pts,
            0,
            frame_start - nut->stream[stream_id].last_sync_pos,
            AVINDEX_KEYFRAME);
        nut->stream[stream_id].last_sync_pos= frame_start;
//                assert(nut->packet_start == frame_start);
    }

    assert(size_mul > size_lsb);
    size= size_lsb;
    if(flags & FLAG_DATA_SIZE)
        size+= size_mul*get_v(bc);

#ifdef TRACE
av_log(s, AV_LOG_DEBUG, "fs:%"PRId64" fc:%d ft:%d kf:%d pts:%"PRId64" size:%d mul:%d lsb:%d flags:%d delta:%d\n", frame_start, frame_code, frame_type, *key_frame_ret, pts, size, size_mul, size_lsb, flags, time_delta);
#endif

    if(frame_type==0 && url_ftell(bc) - nut->packet_start[2] + size > nut->max_distance){
        av_log(s, AV_LOG_ERROR, "frame size too large\n");
        return -1;
    }

    *stream_id_ret = stream_id;
    *pts_ret = pts;

    update(nut, stream_id, frame_start, frame_type, frame_code, *key_frame_ret, size, pts);

    return size;
}

static int decode_frame(NUTContext *nut, AVPacket *pkt, int frame_code, int frame_type, int64_t frame_start){
    AVFormatContext *s= nut->avf;
    ByteIOContext *bc = &s->pb;
    int size, stream_id, key_frame, discard;
    int64_t pts, last_IP_pts;

    size= decode_frame_header(nut, &key_frame, &pts, &stream_id, frame_code, frame_type, frame_start);
    if(size < 0)
        return -1;

    discard= s->streams[ stream_id ]->discard;
    last_IP_pts= s->streams[ stream_id ]->last_IP_pts;
    if(  (discard >= AVDISCARD_NONKEY && !key_frame)
       ||(discard >= AVDISCARD_BIDIR && last_IP_pts != AV_NOPTS_VALUE && last_IP_pts > pts)
       || discard >= AVDISCARD_ALL){
        url_fskip(bc, size);
        return 1;
    }

    av_get_packet(bc, pkt, size);
    pkt->stream_index = stream_id;
    if (key_frame)
        pkt->flags |= PKT_FLAG_KEY;
    pkt->pts = pts;

    return 0;
}

static int nut_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    NUTContext *nut = s->priv_data;
    ByteIOContext *bc = &s->pb;
    int i, frame_code=0, ret;

    for(;;){
        int64_t pos= url_ftell(bc);
        int frame_type= 0;
        uint64_t tmp= nut->next_startcode;
        nut->next_startcode=0;

        if (url_feof(bc))
            return -1;

        if(tmp){
            pos-=8;
        }else{
            frame_code = get_byte(bc);
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
            get_packetheader(nut, bc, 0);
            assert(nut->packet_start[2] == pos);
            url_fseek(bc, nut->written_packet_size, SEEK_CUR);
            break;
        case INFO_STARTCODE:
            if(decode_info_header(nut)<0)
                goto resync;
            break;
        case KEYFRAME_STARTCODE:
            frame_type = 2;
            reset(s, get_v(bc));
            frame_code = get_byte(bc);
        case 0:
            ret= decode_frame(nut, pkt, frame_code, frame_type, pos);
            if(ret==0)
                return 0;
            else if(ret==1) //ok but discard packet
                break;
        default:
resync:
av_log(s, AV_LOG_DEBUG, "syncing from %"PRId64"\n", nut->packet_start[2]+1);
            tmp= find_any_startcode(bc, nut->packet_start[2]+1);
            if(tmp==0)
                return -1;
av_log(s, AV_LOG_DEBUG, "sync\n");
            nut->next_startcode= tmp;
        }
    }
}

static int64_t nut_read_timestamp(AVFormatContext *s, int stream_index, int64_t *pos_arg, int64_t pos_limit){
    NUTContext *nut = s->priv_data;
    StreamContext *stream;
    ByteIOContext *bc = &s->pb;
    int64_t pos, pts;
    uint64_t code;
    int frame_code,step, stream_id, i,size, key_frame;
av_log(s, AV_LOG_DEBUG, "read_timestamp(X,%d,%"PRId64",%"PRId64")\n", stream_index, *pos_arg, pos_limit);

    if(*pos_arg < 0)
        return AV_NOPTS_VALUE;

    pos= *pos_arg;
    step= FFMIN(16*1024, pos);
    do{
        pos-= step;
        code= find_any_startcode(bc, pos);

        if(code && url_ftell(bc) - 8 <= *pos_arg)
            break;
        step= FFMIN(2*step, pos);
    }while(step);

    if(!code) //nothing found, not even after pos_arg
        return AV_NOPTS_VALUE;

    url_fseek(bc, -8, SEEK_CUR);
    for(i=0; i<s->nb_streams; i++)
        nut->stream[i].last_sync_pos= url_ftell(bc);

    for(;;){
        int frame_type=0;
        int64_t pos= url_ftell(bc);
        uint64_t tmp=0;

        if(pos > pos_limit || url_feof(bc))
            return AV_NOPTS_VALUE;

        frame_code = get_byte(bc);
        if(frame_code == 'N'){
            tmp= frame_code;
            for(i=1; i<8; i++)
                tmp = (tmp<<8) + get_byte(bc);
        }
//av_log(s, AV_LOG_DEBUG, "before switch %"PRIX64" at=%"PRId64"\n", tmp, pos);

        switch(tmp){
        case MAIN_STARTCODE:
        case STREAM_STARTCODE:
        case INDEX_STARTCODE:
        case INFO_STARTCODE:
            get_packetheader(nut, bc, 0);
            assert(nut->packet_start[2]==pos);
            url_fseek(bc, nut->written_packet_size, SEEK_CUR);
            break;
        case KEYFRAME_STARTCODE:
            frame_type=2;
            reset(s, get_v(bc));
            frame_code = get_byte(bc);
        case 0:
            size= decode_frame_header(nut, &key_frame, &pts, &stream_id, frame_code, frame_type, pos);
            if(size < 0)
                goto resync;

            stream= &nut->stream[stream_id];
            if(stream_id != stream_index || !key_frame || pos < *pos_arg){
                url_fseek(bc, size, SEEK_CUR);
                break;
            }

            *pos_arg= pos;
            return pts;
        default:
resync:
av_log(s, AV_LOG_DEBUG, "syncing from %"PRId64"\n", nut->packet_start[2]+1);
            if(!find_any_startcode(bc, nut->packet_start[2]+1))
                return AV_NOPTS_VALUE;

            url_fseek(bc, -8, SEEK_CUR);
        }
    }
    return AV_NOPTS_VALUE;
}

static int nut_read_seek(AVFormatContext *s, int stream_index, int64_t target_ts, int flags){
//    NUTContext *nut = s->priv_data;
    int64_t pos;

    if(av_seek_frame_binary(s, stream_index, target_ts, flags) < 0)
        return -1;

    pos= url_ftell(&s->pb);
    nut_read_timestamp(s, stream_index, &pos, pos-1);

    return 0;
}

static int nut_read_close(AVFormatContext *s)
{
    NUTContext *nut = s->priv_data;

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
    nut_read_seek,
    nut_read_timestamp,
    .extensions = "nut",
};
#endif
#ifdef CONFIG_NUT_MUXER
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
    nut_write_header,
    nut_write_packet,
    nut_write_trailer,
    .flags = AVFMT_GLOBALHEADER,
};
#endif
