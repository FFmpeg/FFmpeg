/*
 * AVI decoder.
 * Copyright (c) 2001 Fabrice Bellard.
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
#include "dv.h"

#undef NDEBUG
#include <assert.h>

//#define DEBUG
//#define DEBUG_SEEK

typedef struct AVIStream {
    int64_t frame_offset; /* current frame (video) or byte (audio) counter
                         (used to compute the pts) */
    int remaining;
    int packet_size;

    int scale;
    int rate;    
    int sample_size; /* audio only data */
    int start;
    
    int cum_len; /* temporary storage (used during seek) */
    
    int prefix;                       ///< normally 'd'<<8 + 'c' or 'w'<<8 + 'b'
    int prefix_count;
} AVIStream;

typedef struct {
    int64_t  riff_end;
    int64_t  movi_end;
    offset_t movi_list;
    int index_loaded;
    int is_odml;
    int non_interleaved;
    int stream_index;
    DVDemuxContext* dv_demux;
} AVIContext;

static int avi_load_index(AVFormatContext *s);

#ifdef DEBUG
static void print_tag(const char *str, unsigned int tag, int size)
{
    printf("%s: tag=%c%c%c%c size=0x%x\n",
           str, tag & 0xff,
           (tag >> 8) & 0xff,
           (tag >> 16) & 0xff,
           (tag >> 24) & 0xff,
           size);
}
#endif

static int get_riff(AVIContext *avi, ByteIOContext *pb)
{
    uint32_t tag; 
    /* check RIFF header */
    tag = get_le32(pb);

    if (tag != MKTAG('R', 'I', 'F', 'F'))
        return -1;
    avi->riff_end = get_le32(pb);   /* RIFF chunk size */
    avi->riff_end += url_ftell(pb); /* RIFF chunk end */
    tag = get_le32(pb);
    if (tag != MKTAG('A', 'V', 'I', ' ') && tag != MKTAG('A', 'V', 'I', 'X'))
        return -1;
    
    return 0;
}

static int avi_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint32_t tag, tag1, handler;
    int codec_type, stream_index, frame_period, bit_rate;
    unsigned int size, nb_frames;
    int i, n;
    AVStream *st;
    AVIStream *ast;
    int xan_video = 0;  /* hack to support Xan A/V */

    avi->stream_index= -1;
    
    if (get_riff(avi, pb) < 0)
        return -1;

    /* first list tag */
    stream_index = -1;
    codec_type = -1;
    frame_period = 0;
    for(;;) {
        if (url_feof(pb))
            goto fail;
        tag = get_le32(pb);
        size = get_le32(pb);
#ifdef DEBUG
        print_tag("tag", tag, size);
#endif

        switch(tag) {
        case MKTAG('L', 'I', 'S', 'T'):
            /* ignored, except when start of video packets */
            tag1 = get_le32(pb);
#ifdef DEBUG
            print_tag("list", tag1, 0);
#endif
            if (tag1 == MKTAG('m', 'o', 'v', 'i')) {
                avi->movi_list = url_ftell(pb) - 4;
                if(size) avi->movi_end = avi->movi_list + size;
                else     avi->movi_end = url_fsize(pb);
#ifdef DEBUG
                printf("movi end=%Lx\n", avi->movi_end);
#endif
                goto end_of_header;
            }
            break;
        case MKTAG('d', 'm', 'l', 'h'):
	    avi->is_odml = 1;
	    url_fskip(pb, size + (size & 1));
	    break;
        case MKTAG('a', 'v', 'i', 'h'):
	    /* avi header */
            /* using frame_period is bad idea */
            frame_period = get_le32(pb);
            bit_rate = get_le32(pb) * 8;
	    url_fskip(pb, 4 * 4);
            n = get_le32(pb);
            for(i=0;i<n;i++) {
                AVIStream *ast;
                st = av_new_stream(s, i);
                if (!st)
                    goto fail;

                ast = av_mallocz(sizeof(AVIStream));
                if (!ast)
                    goto fail;
                st->priv_data = ast;
	    }
            url_fskip(pb, size - 7 * 4);
            break;
        case MKTAG('s', 't', 'r', 'h'):
            /* stream header */
            stream_index++;
            tag1 = get_le32(pb);
            handler = get_le32(pb); /* codec tag */
#ifdef DEBUG
        print_tag("strh", tag1, -1);
#endif
            if(tag1 == MKTAG('i', 'a', 'v', 's') || tag1 == MKTAG('i', 'v', 'a', 's')){
                /* 
	         * After some consideration -- I don't think we 
	         * have to support anything but DV in a type1 AVIs.
	         */
	        if (s->nb_streams != 1)
		    goto fail;
	        
		if (handler != MKTAG('d', 'v', 's', 'd') &&
	            handler != MKTAG('d', 'v', 'h', 'd') &&
		    handler != MKTAG('d', 'v', 's', 'l'))
	           goto fail;
                
		ast = s->streams[0]->priv_data;
		av_freep(&s->streams[0]->codec->extradata);
		av_freep(&s->streams[0]);
		s->nb_streams = 0;
	        avi->dv_demux = dv_init_demux(s);
		if (!avi->dv_demux)
		    goto fail;
		s->streams[0]->priv_data = ast;
		url_fskip(pb, 3 * 4);
		ast->scale = get_le32(pb);
		ast->rate = get_le32(pb);
	        stream_index = s->nb_streams - 1;
		url_fskip(pb, size - 7*4);
                break;
            }

            if (stream_index >= s->nb_streams) {
                url_fskip(pb, size - 8);
                break;
            } 
            st = s->streams[stream_index];
            ast = st->priv_data;
            st->codec->stream_codec_tag= handler;

            get_le32(pb); /* flags */
            get_le16(pb); /* priority */
            get_le16(pb); /* language */
            get_le32(pb); /* initial frame */
            ast->scale = get_le32(pb);
            ast->rate = get_le32(pb);
            if(ast->scale && ast->rate){
            }else if(frame_period){
                ast->rate = 1000000;
                ast->scale = frame_period;
            }else{
                ast->rate = 25;
                ast->scale = 1;
            }
            av_set_pts_info(st, 64, ast->scale, ast->rate);
                    
            ast->start= get_le32(pb); /* start */
            nb_frames = get_le32(pb);

            st->start_time = 0;
            st->duration = nb_frames;
            get_le32(pb); /* buffer size */
            get_le32(pb); /* quality */
            ast->sample_size = get_le32(pb); /* sample ssize */
//            av_log(NULL, AV_LOG_DEBUG, "%d %d %d %d\n", ast->rate, ast->scale, ast->start, ast->sample_size);

            switch(tag1) {
	    case MKTAG('v', 'i', 'd', 's'):
                codec_type = CODEC_TYPE_VIDEO;

                ast->sample_size = 0;
                break;
            case MKTAG('a', 'u', 'd', 's'):
                codec_type = CODEC_TYPE_AUDIO;
                break;
            case MKTAG('t', 'x', 't', 's'):
                //FIXME 
                codec_type = CODEC_TYPE_DATA; //CODEC_TYPE_SUB ?  FIXME
                break;
            case MKTAG('p', 'a', 'd', 's'):
                codec_type = CODEC_TYPE_UNKNOWN;
                stream_index--;
                break;
            default:
                av_log(s, AV_LOG_ERROR, "unknown stream type %X\n", tag1);
                goto fail;
            }
            url_fskip(pb, size - 12 * 4);
            break;
        case MKTAG('s', 't', 'r', 'f'):
            /* stream header */
            if (stream_index >= s->nb_streams || avi->dv_demux) {
                url_fskip(pb, size);
            } else {
                st = s->streams[stream_index];
                switch(codec_type) {
                case CODEC_TYPE_VIDEO:
                    get_le32(pb); /* size */
                    st->codec->width = get_le32(pb);
                    st->codec->height = get_le32(pb);
                    get_le16(pb); /* panes */
                    st->codec->bits_per_sample= get_le16(pb); /* depth */
                    tag1 = get_le32(pb);
                    get_le32(pb); /* ImageSize */
                    get_le32(pb); /* XPelsPerMeter */
                    get_le32(pb); /* YPelsPerMeter */
                    get_le32(pb); /* ClrUsed */
                    get_le32(pb); /* ClrImportant */

                 if(size > 10*4 && size<(1<<30)){
                    st->codec->extradata_size= size - 10*4;
                    st->codec->extradata= av_malloc(st->codec->extradata_size + FF_INPUT_BUFFER_PADDING_SIZE);
                    get_buffer(pb, st->codec->extradata, st->codec->extradata_size);
                 }
                    
                    if(st->codec->extradata_size & 1) //FIXME check if the encoder really did this correctly
                        get_byte(pb);

                    /* Extract palette from extradata if bpp <= 8 */
                    /* This code assumes that extradata contains only palette */
                    /* This is true for all paletted codecs implemented in ffmpeg */
                    if (st->codec->extradata_size && (st->codec->bits_per_sample <= 8)) {
                        st->codec->palctrl = av_mallocz(sizeof(AVPaletteControl));
#ifdef WORDS_BIGENDIAN
                        for (i = 0; i < FFMIN(st->codec->extradata_size, AVPALETTE_SIZE)/4; i++)
                            st->codec->palctrl->palette[i] = bswap_32(((uint32_t*)st->codec->extradata)[i]);
#else
                        memcpy(st->codec->palctrl->palette, st->codec->extradata,
                               FFMIN(st->codec->extradata_size, AVPALETTE_SIZE));
#endif
                        st->codec->palctrl->palette_changed = 1;
                    }

#ifdef DEBUG
                    print_tag("video", tag1, 0);
#endif
                    st->codec->codec_type = CODEC_TYPE_VIDEO;
                    st->codec->codec_tag = tag1;
                    st->codec->codec_id = codec_get_id(codec_bmp_tags, tag1);
                    if (st->codec->codec_id == CODEC_ID_XAN_WC4)
                        xan_video = 1;
                    st->need_parsing = 2; //only parse headers dont do slower repacketization, this is needed to get the pict type which is needed for generating correct pts
//                    url_fskip(pb, size - 5 * 4);
                    break;
                case CODEC_TYPE_AUDIO:
                    get_wav_header(pb, st->codec, size);
                    if (size%2) /* 2-aligned (fix for Stargate SG-1 - 3x18 - Shades of Grey.avi) */
                        url_fskip(pb, 1);
                    /* special case time: To support Xan DPCM, hardcode
                     * the format if Xxan is the video codec */
                    st->need_parsing = 1;
                    /* force parsing as several audio frames can be in
                       one packet */
                    if (xan_video)
                        st->codec->codec_id = CODEC_ID_XAN_DPCM;
                    break;
                default:
                    st->codec->codec_type = CODEC_TYPE_DATA;
                    st->codec->codec_id= CODEC_ID_NONE;
                    st->codec->codec_tag= 0;
                    url_fskip(pb, size);
                    break;
                }
            }
            break;
        default:
            /* skip tag */
            size += (size & 1);
            url_fskip(pb, size);
            break;
        }
    }
 end_of_header:
    /* check stream number */
    if (stream_index != s->nb_streams - 1) {
    fail:
        for(i=0;i<s->nb_streams;i++) {
            av_freep(&s->streams[i]->codec->extradata);
            av_freep(&s->streams[i]);
        }
        return -1;
    }

    assert(!avi->index_loaded);
    avi_load_index(s);
    avi->index_loaded = 1;
 
    return 0;
}

static int avi_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int n, d[8], size;
    offset_t i, sync;
    void* dstr;
   
    if (avi->dv_demux) {
        size = dv_get_packet(avi->dv_demux, pkt);
	if (size >= 0)
	    return size;
    }
    
    if(avi->non_interleaved){
        int best_stream_index = 0;
        AVStream *best_st= NULL;
        AVIStream *best_ast;
        int64_t best_ts= INT64_MAX;
        int i;
        
        for(i=0; i<s->nb_streams; i++){
            AVStream *st = s->streams[i];
            AVIStream *ast = st->priv_data;
            int64_t ts= ast->frame_offset;

            if(ast->sample_size)
                ts /= ast->sample_size;
            ts= av_rescale(ts, AV_TIME_BASE * (int64_t)st->time_base.num, st->time_base.den);

//            av_log(NULL, AV_LOG_DEBUG, "%Ld %d/%d %Ld\n", ts, st->time_base.num, st->time_base.den, ast->frame_offset);
            if(ts < best_ts){
                best_ts= ts;
                best_st= st;
                best_stream_index= i;
            }
        }
        best_ast = best_st->priv_data;
        best_ts= av_rescale(best_ts, best_st->time_base.den, AV_TIME_BASE * (int64_t)best_st->time_base.num); //FIXME a little ugly
        if(best_ast->remaining)
            i= av_index_search_timestamp(best_st, best_ts, AVSEEK_FLAG_ANY | AVSEEK_FLAG_BACKWARD);
        else
            i= av_index_search_timestamp(best_st, best_ts, AVSEEK_FLAG_ANY);

//        av_log(NULL, AV_LOG_DEBUG, "%d\n", i);
        if(i>=0){
            int64_t pos= best_st->index_entries[i].pos;
            pos += avi->movi_list + best_ast->packet_size - best_ast->remaining;
            url_fseek(&s->pb, pos, SEEK_SET);
//        av_log(NULL, AV_LOG_DEBUG, "pos=%Ld\n", pos);
            
            if(best_ast->remaining)
                avi->stream_index= best_stream_index;
            else
                avi->stream_index= -1;
        }
    }
    
resync:
    if(avi->stream_index >= 0){
        AVStream *st= s->streams[ avi->stream_index ];
        AVIStream *ast= st->priv_data;
        int size;
        
        if(ast->sample_size == 0)
            size= INT_MAX;
        else if(ast->sample_size < 32) 
            size= 64*ast->sample_size;
        else
            size= ast->sample_size;

        if(size > ast->remaining)
            size= ast->remaining;
        av_get_packet(pb, pkt, size);
        
        if (avi->dv_demux) {
            dstr = pkt->destruct;
            size = dv_produce_packet(avi->dv_demux, pkt,
                                    pkt->data, pkt->size);
            pkt->destruct = dstr;
            pkt->flags |= PKT_FLAG_KEY;
        } else {
            /* XXX: how to handle B frames in avi ? */
            pkt->dts = ast->frame_offset;
//                pkt->dts += ast->start;
            if(ast->sample_size)
                pkt->dts /= ast->sample_size;
//av_log(NULL, AV_LOG_DEBUG, "dts:%Ld offset:%d %d/%d smpl_siz:%d base:%d st:%d size:%d\n", pkt->dts, ast->frame_offset, ast->scale, ast->rate, ast->sample_size, AV_TIME_BASE, n, size);
            pkt->stream_index = avi->stream_index;

            if (st->codec->codec_type == CODEC_TYPE_VIDEO) {
                if(st->index_entries){
                    AVIndexEntry *e;
                    int index;

                    index= av_index_search_timestamp(st, pkt->dts, 0);
                    e= &st->index_entries[index];
                    
                    if(index >= 0 && e->timestamp == ast->frame_offset){
                        if (e->flags & AVINDEX_KEYFRAME)
                            pkt->flags |= PKT_FLAG_KEY;
                    }
                } else {
                    /* if no index, better to say that all frames
                        are key frames */
                    pkt->flags |= PKT_FLAG_KEY;
                }
            } else {
                pkt->flags |= PKT_FLAG_KEY; 
            }
            if(ast->sample_size)
                ast->frame_offset += pkt->size;
            else
                ast->frame_offset++;
        }
        ast->remaining -= size;
        if(!ast->remaining){
            avi->stream_index= -1;
            ast->packet_size= 0;
            if (size & 1) {
                get_byte(pb);
                size++;
            }
        }

        return size;
    }

    memset(d, -1, sizeof(int)*8);
    for(i=sync=url_ftell(pb); !url_feof(pb); i++) {
        int j;

	if (i >= avi->movi_end) {
	    if (avi->is_odml) {
		url_fskip(pb, avi->riff_end - i);
	        avi->riff_end = avi->movi_end = url_fsize(pb);
	    } else
	        break;
	}

        for(j=0; j<7; j++)
            d[j]= d[j+1];
        d[7]= get_byte(pb);
        
        size= d[4] + (d[5]<<8) + (d[6]<<16) + (d[7]<<24);
        
        if(    d[2] >= '0' && d[2] <= '9'
            && d[3] >= '0' && d[3] <= '9'){
            n= (d[2] - '0') * 10 + (d[3] - '0');
        }else{
            n= 100; //invalid stream id
        }
//av_log(NULL, AV_LOG_DEBUG, "%X %X %X %X %X %X %X %X %lld %d %d\n", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7], i, size, n);
        if(i + size > avi->movi_end || d[0]<0)
            continue;
        
        //parse ix##
        if(  (d[0] == 'i' && d[1] == 'x' && n < s->nb_streams)
	//parse JUNK
           ||(d[0] == 'J' && d[1] == 'U' && d[2] == 'N' && d[3] == 'K')){
            url_fskip(pb, size);
//av_log(NULL, AV_LOG_DEBUG, "SKIP\n");
            goto resync;
        }

        if(    d[0] >= '0' && d[0] <= '9'
            && d[1] >= '0' && d[1] <= '9'){
            n= (d[0] - '0') * 10 + (d[1] - '0');
        }else{
            n= 100; //invalid stream id
        }
        
        //parse ##dc/##wb
        if(n < s->nb_streams){
          AVStream *st;
          AVIStream *ast;
          st = s->streams[n];
          ast = st->priv_data;
          
          if(   (st->discard >= AVDISCARD_DEFAULT && size==0)
             /*|| (st->discard >= AVDISCARD_NONKEY && !(pkt->flags & PKT_FLAG_KEY))*/ //FIXME needs a little reordering
             || st->discard >= AVDISCARD_ALL){
                if(ast->sample_size) ast->frame_offset += pkt->size;
                else                 ast->frame_offset++;
                url_fskip(pb, size);
                goto resync;
          }

          if(   ((ast->prefix_count<5 || sync+9 > i) && d[2]<128 && d[3]<128) || 
                d[2]*256+d[3] == ast->prefix /*||
                (d[2] == 'd' && d[3] == 'c') || 
	        (d[2] == 'w' && d[3] == 'b')*/) {

//av_log(NULL, AV_LOG_DEBUG, "OK\n");
            if(d[2]*256+d[3] == ast->prefix)
                ast->prefix_count++;
            else{
                ast->prefix= d[2]*256+d[3];
                ast->prefix_count= 0;
            }

            avi->stream_index= n;
            ast->packet_size= size + 8;
            ast->remaining= size;
            goto resync;
          }
        }
        /* palette changed chunk */
        if (   d[0] >= '0' && d[0] <= '9'
            && d[1] >= '0' && d[1] <= '9'
            && ((d[2] == 'p' && d[3] == 'c'))
            && n < s->nb_streams && i + size <= avi->movi_end) {

            AVStream *st;
            int first, clr, flags, k, p;

            st = s->streams[n];

            first = get_byte(pb);
            clr = get_byte(pb);
	    if(!clr) /* all 256 colors used */
		clr = 256;
            flags = get_le16(pb);
            p = 4;
            for (k = first; k < clr + first; k++) {
                int r, g, b;
                r = get_byte(pb);
                g = get_byte(pb);
                b = get_byte(pb);
                    get_byte(pb);
                st->codec->palctrl->palette[k] = b + (g << 8) + (r << 16);
            }
            st->codec->palctrl->palette_changed = 1;
            goto resync;
        }

    }

    return -1;
}

/* XXX: we make the implicit supposition that the position are sorted
   for each stream */
static int avi_read_idx1(AVFormatContext *s, int size)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int nb_index_entries, i;
    AVStream *st;
    AVIStream *ast;
    unsigned int index, tag, flags, pos, len;
    unsigned last_pos= -1;
    
    nb_index_entries = size / 16;
    if (nb_index_entries <= 0)
        return -1;

    /* read the entries and sort them in each stream component */
    for(i = 0; i < nb_index_entries; i++) {
        tag = get_le32(pb);
        flags = get_le32(pb);
        pos = get_le32(pb);
        len = get_le32(pb);
#if defined(DEBUG_SEEK)
        av_log(NULL, AV_LOG_DEBUG, "%d: tag=0x%x flags=0x%x pos=0x%x len=%d/", 
               i, tag, flags, pos, len);
#endif
        if(i==0 && pos > avi->movi_list)
            avi->movi_list= 0; //FIXME better check

        index = ((tag & 0xff) - '0') * 10;
        index += ((tag >> 8) & 0xff) - '0';
        if (index >= s->nb_streams)
            continue;
        st = s->streams[index];
        ast = st->priv_data;
        
#if defined(DEBUG_SEEK)
        av_log(NULL, AV_LOG_DEBUG, "%d cum_len=%d\n", len, ast->cum_len);
#endif
        if(last_pos == pos)
            avi->non_interleaved= 1;
        else
            av_add_index_entry(st, pos, ast->cum_len, 0, (flags&AVIIF_INDEX) ? AVINDEX_KEYFRAME : 0);
        if(ast->sample_size)
            ast->cum_len += len / ast->sample_size;
        else
            ast->cum_len ++;
        last_pos= pos;
    }
    return 0;
}

static int guess_ni_flag(AVFormatContext *s){
    int i;
    int64_t last_start=0;
    int64_t first_end= INT64_MAX;
    
    for(i=0; i<s->nb_streams; i++){
        AVStream *st = s->streams[i];
        int n= st->nb_index_entries;

        if(n <= 0)
            continue;

        if(st->index_entries[0].pos > last_start)
            last_start= st->index_entries[0].pos;
        if(st->index_entries[n-1].pos < first_end)
            first_end= st->index_entries[n-1].pos;
    }
    return last_start > first_end;
}

static int avi_load_index(AVFormatContext *s)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    uint32_t tag, size;
    offset_t pos= url_ftell(pb);
    
    url_fseek(pb, avi->movi_end, SEEK_SET);
#ifdef DEBUG_SEEK
    printf("movi_end=0x%llx\n", avi->movi_end);
#endif
    for(;;) {
        if (url_feof(pb))
            break;
        tag = get_le32(pb);
        size = get_le32(pb);
#ifdef DEBUG_SEEK
        printf("tag=%c%c%c%c size=0x%x\n",
               tag & 0xff,
               (tag >> 8) & 0xff,
               (tag >> 16) & 0xff,
               (tag >> 24) & 0xff,
               size);
#endif
        switch(tag) {
        case MKTAG('i', 'd', 'x', '1'):
            if (avi_read_idx1(s, size) < 0)
                goto skip;
            else
                goto the_end;
            break;
        default:
        skip:
            size += (size & 1);
            url_fskip(pb, size);
            break;
        }
    }
 the_end:
    avi->non_interleaved |= guess_ni_flag(s);
    url_fseek(pb, pos, SEEK_SET);
    return 0;
}

static int avi_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp, int flags)
{
    AVIContext *avi = s->priv_data;
    AVStream *st;
    int i, index;
    int64_t pos;

    if (!avi->index_loaded) {
        /* we only load the index on demand */
        avi_load_index(s);
        avi->index_loaded = 1;
    }
    assert(stream_index>= 0);

    st = s->streams[stream_index];
    index= av_index_search_timestamp(st, timestamp, flags);
    if(index<0)
        return -1;
    
    /* find the position */
    pos = st->index_entries[index].pos;
    timestamp = st->index_entries[index].timestamp;

//    av_log(NULL, AV_LOG_DEBUG, "XX %Ld %d %Ld\n", timestamp, index, st->index_entries[index].timestamp);

    for(i = 0; i < s->nb_streams; i++) {
        AVStream *st2 = s->streams[i];
        AVIStream *ast2 = st2->priv_data;

        ast2->packet_size=
        ast2->remaining= 0;

        if (st2->nb_index_entries <= 0)
            continue;
        
//        assert(st2->codec.block_align);
        assert(st2->time_base.den == ast2->rate);
        assert(st2->time_base.num == ast2->scale);
        index = av_index_search_timestamp(
                st2, 
                av_rescale(timestamp, st2->time_base.den*(int64_t)st->time_base.num, st->time_base.den * (int64_t)st2->time_base.num),
                flags | AVSEEK_FLAG_BACKWARD);
        if(index<0)
            index=0;
        
        if(!avi->non_interleaved){
            while(index>0 && st2->index_entries[index].pos > pos)
                index--;
            while(index+1 < st2->nb_index_entries && st2->index_entries[index].pos < pos)
                index++;
        }

//        av_log(NULL, AV_LOG_DEBUG, "%Ld %d %Ld\n", timestamp, index, st2->index_entries[index].timestamp);
        /* extract the current frame number */
        ast2->frame_offset = st2->index_entries[index].timestamp;
        if(ast2->sample_size)
            ast2->frame_offset *=ast2->sample_size;
    }

    if (avi->dv_demux)
        dv_flush_audio_packets(avi->dv_demux);
    /* do the seek */
    pos += avi->movi_list;
    url_fseek(&s->pb, pos, SEEK_SET);
    avi->stream_index= -1;
    return 0;
}

static int avi_read_close(AVFormatContext *s)
{
    int i;
    AVIContext *avi = s->priv_data;

    for(i=0;i<s->nb_streams;i++) {
        AVStream *st = s->streams[i];
        AVIStream *ast = st->priv_data;
        av_free(ast);
        av_free(st->codec->extradata);
        av_free(st->codec->palctrl);
    }

    if (avi->dv_demux)
        av_free(avi->dv_demux);

    return 0;
}

static int avi_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 32)
        return 0;
    if (p->buf[0] == 'R' && p->buf[1] == 'I' &&
        p->buf[2] == 'F' && p->buf[3] == 'F' &&
        p->buf[8] == 'A' && p->buf[9] == 'V' &&
        p->buf[10] == 'I' && p->buf[11] == ' ')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static AVInputFormat avi_iformat = {
    "avi",
    "avi format",
    sizeof(AVIContext),
    avi_probe,
    avi_read_header,
    avi_read_packet,
    avi_read_close,
    avi_read_seek,
};

int avidec_init(void)
{
    av_register_input_format(&avi_iformat);
    return 0;
}
