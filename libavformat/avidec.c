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

//#define DEBUG
//#define DEBUG_SEEK

typedef struct AVIIndexEntry {
    unsigned int flags;
    unsigned int pos;
    unsigned int cum_len; /* sum of all lengths before this packet */
} AVIIndexEntry;

typedef struct AVIStream {
    AVIIndexEntry *index_entries;
    int nb_index_entries;
    int index_entries_allocated_size;
    int frame_offset; /* current frame (video) or byte (audio) counter
                         (used to compute the pts) */
    int scale;
    int rate;    
    int sample_size; /* audio only data */
    int start;
    
    int new_frame_offset; /* temporary storage (used during seek) */
    int cum_len; /* temporary storage (used during seek) */
} AVIStream;

typedef struct {
    int64_t  riff_end;
    int64_t  movi_end;
    offset_t movi_list;
    int index_loaded;
    int is_odml;
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
    int codec_type, stream_index, frame_period, bit_rate, scale, rate;
    unsigned int size, nb_frames;
    int i, n;
    AVStream *st;
    AVIStream *ast;
    int xan_video = 0;  /* hack to support Xan A/V */

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
                else     avi->movi_end = url_filesize(url_fileno(pb));
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
            switch(tag1) {
            case MKTAG('i', 'a', 'v', 's'):
	    case MKTAG('i', 'v', 'a', 's'):
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
                
		av_freep(&s->streams[0]->codec.extradata);
		av_freep(&s->streams[0]);
		s->nb_streams = 0;
	        avi->dv_demux = dv_init_demux(s);
		if (!avi->dv_demux)
		    goto fail;
	        stream_index = s->nb_streams - 1;
		url_fskip(pb, size - 8);
		break;
	    case MKTAG('v', 'i', 'd', 's'):
                codec_type = CODEC_TYPE_VIDEO;

                if (stream_index >= s->nb_streams) {
                    url_fskip(pb, size - 8);
                    break;
                } 

                st = s->streams[stream_index];
                ast = st->priv_data;
                st->codec.stream_codec_tag= handler;
                
                get_le32(pb); /* flags */
                get_le16(pb); /* priority */
                get_le16(pb); /* language */
                get_le32(pb); /* XXX: initial frame ? */
                scale = get_le32(pb); /* scale */
                rate = get_le32(pb); /* rate */

                if(scale && rate){
                }else if(frame_period){
                    rate = 1000000;
                    scale = frame_period;
                }else{
                    rate = 25;
                    scale = 1;
                }
                ast->rate = rate;
                ast->scale = scale;
                av_set_pts_info(st, 64, scale, rate);
                st->codec.frame_rate = rate;
                st->codec.frame_rate_base = scale;
                get_le32(pb); /* start */
                nb_frames = get_le32(pb);
                st->start_time = 0;
                st->duration = av_rescale(nb_frames,
                    st->codec.frame_rate_base * AV_TIME_BASE,
                    st->codec.frame_rate);
		url_fskip(pb, size - 9 * 4);
                break;
            case MKTAG('a', 'u', 'd', 's'):
                {
                    unsigned int length;

                    codec_type = CODEC_TYPE_AUDIO;

                    if (stream_index >= s->nb_streams) {
                        url_fskip(pb, size - 8);
                        break;
                    } 
                    st = s->streams[stream_index];
                    ast = st->priv_data;
                    
                    get_le32(pb); /* flags */
                    get_le16(pb); /* priority */
                    get_le16(pb); /* language */
                    get_le32(pb); /* initial frame */
                    ast->scale = get_le32(pb); /* scale */
                    ast->rate = get_le32(pb);
                    av_set_pts_info(st, 64, ast->scale, ast->rate);
                    ast->start= get_le32(pb); /* start */
                    length = get_le32(pb); /* length, in samples or bytes */
                    get_le32(pb); /* buffer size */
                    get_le32(pb); /* quality */
                    ast->sample_size = get_le32(pb); /* sample ssize */
//av_log(NULL, AV_LOG_DEBUG, "%d %d %d %d\n", ast->scale, ast->rate, ast->sample_size, ast->start);
                    st->start_time = 0;
                    if (ast->rate != 0)
                        st->duration = (int64_t)length * AV_TIME_BASE / ast->rate;
                    url_fskip(pb, size - 12 * 4);
                }
                break;
            case MKTAG('t', 'x', 't', 's'):
                //FIXME 
                codec_type = CODEC_TYPE_DATA; //CODEC_TYPE_SUB ?  FIXME
                url_fskip(pb, size - 8);
                break;
            default:
                goto fail;
            }
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
                    st->codec.width = get_le32(pb);
                    st->codec.height = get_le32(pb);
                    get_le16(pb); /* panes */
                    st->codec.bits_per_sample= get_le16(pb); /* depth */
                    tag1 = get_le32(pb);
                    get_le32(pb); /* ImageSize */
                    get_le32(pb); /* XPelsPerMeter */
                    get_le32(pb); /* YPelsPerMeter */
                    get_le32(pb); /* ClrUsed */
                    get_le32(pb); /* ClrImportant */

                    st->codec.extradata_size= size - 10*4;
                    st->codec.extradata= av_malloc(st->codec.extradata_size);
                    get_buffer(pb, st->codec.extradata, st->codec.extradata_size);
                    
                    if(st->codec.extradata_size & 1) //FIXME check if the encoder really did this correctly
                        get_byte(pb);

                    /* Extract palette from extradata if bpp <= 8 */
                    /* This code assumes that extradata contains only palette */
                    /* This is true for all paletted codecs implemented in ffmpeg */
                    if (st->codec.extradata_size && (st->codec.bits_per_sample <= 8)) {
                        st->codec.palctrl = av_mallocz(sizeof(AVPaletteControl));
#ifdef WORDS_BIGENDIAN
                        for (i = 0; i < FFMIN(st->codec.extradata_size, AVPALETTE_SIZE)/4; i++)
                            st->codec.palctrl->palette[i] = bswap_32(((uint32_t*)st->codec.extradata)[i]);
#else
                        memcpy(st->codec.palctrl->palette, st->codec.extradata,
                               FFMIN(st->codec.extradata_size, AVPALETTE_SIZE));
#endif
                        st->codec.palctrl->palette_changed = 1;
                    }

#ifdef DEBUG
                    print_tag("video", tag1, 0);
#endif
                    st->codec.codec_type = CODEC_TYPE_VIDEO;
                    st->codec.codec_tag = tag1;
                    st->codec.codec_id = codec_get_id(codec_bmp_tags, tag1);
                    if (st->codec.codec_id == CODEC_ID_XAN_WC4)
                        xan_video = 1;
//                    url_fskip(pb, size - 5 * 4);
                    break;
                case CODEC_TYPE_AUDIO:
                    get_wav_header(pb, &st->codec, size);
                    if (size%2) /* 2-aligned (fix for Stargate SG-1 - 3x18 - Shades of Grey.avi) */
                        url_fskip(pb, 1);
                    /* special case time: To support Xan DPCM, hardcode
                     * the format if Xxan is the video codec */
                    st->need_parsing = 1;
                    /* force parsing as several audio frames can be in
                       one packet */
                    if (xan_video)
                        st->codec.codec_id = CODEC_ID_XAN_DPCM;
                    break;
                default:
                    st->codec.codec_type = CODEC_TYPE_DATA;
                    st->codec.codec_id= CODEC_ID_NONE;
                    st->codec.codec_tag= 0;
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
            av_freep(&s->streams[i]->codec.extradata);
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
    offset_t i;
    void* dstr;

    memset(d, -1, sizeof(int)*8);
   
    if (avi->dv_demux) {
        size = dv_get_packet(avi->dv_demux, pkt);
	if (size >= 0)
	    return size;
    }
        
    for(i=url_ftell(pb); !url_feof(pb); i++) {
        int j;

	if (i >= avi->movi_end) {
	    if (avi->is_odml) {
		url_fskip(pb, avi->riff_end - i);
	        avi->riff_end = avi->movi_end = url_filesize(url_fileno(pb));
	    } else
	        break;
	}

        for(j=0; j<7; j++)
            d[j]= d[j+1];
        d[7]= get_byte(pb);
        
        size= d[4] + (d[5]<<8) + (d[6]<<16) + (d[7]<<24);
        
        //parse ix##
        n= (d[2] - '0') * 10 + (d[3] - '0');
        if(    d[2] >= '0' && d[2] <= '9'
            && d[3] >= '0' && d[3] <= '9'
            && d[0] == 'i' && d[1] == 'x'
            && n < s->nb_streams
            && i + size <= avi->movi_end){
            
            url_fskip(pb, size);
        }

	//parse JUNK
        if(d[0] == 'J' && d[1] == 'U' && d[2] == 'N' && d[3] == 'K' &&
           i + size <= avi->movi_end) {
            
            url_fskip(pb, size);
        }
        
        //parse ##dc/##wb
        n= (d[0] - '0') * 10 + (d[1] - '0');
        if(    d[0] >= '0' && d[0] <= '9'
            && d[1] >= '0' && d[1] <= '9'
            && ((d[2] == 'd' && d[3] == 'c') || 
	        (d[2] == 'w' && d[3] == 'b') || 
		(d[2] == 'd' && d[3] == 'b') ||
		(d[2] == '_' && d[3] == '_'))
            && n < s->nb_streams
            && i + size <= avi->movi_end) {
        
            av_new_packet(pkt, size);
            get_buffer(pb, pkt->data, size);
            if (size & 1) {
                get_byte(pb);
		size++;
	    }
	
	    if (avi->dv_demux) {
	        dstr = pkt->destruct;
	        size = dv_produce_packet(avi->dv_demux, pkt,
		                         pkt->data, pkt->size);
		pkt->destruct = dstr;
                pkt->flags |= PKT_FLAG_KEY;
	    } else {
                AVStream *st;
                AVIStream *ast;
                st = s->streams[n];
                ast = st->priv_data;
                
                /* XXX: how to handle B frames in avi ? */
                pkt->dts = ast->frame_offset;
//                pkt->dts += ast->start;
                if(ast->sample_size)
                    pkt->dts /= ast->sample_size;
//av_log(NULL, AV_LOG_DEBUG, "dts:%Ld offset:%d %d/%d smpl_siz:%d base:%d st:%d size:%d\n", pkt->dts, ast->frame_offset, ast->scale, ast->rate, ast->sample_size, AV_TIME_BASE, n, size);
                pkt->stream_index = n;
                /* FIXME: We really should read index for that */
                if (st->codec.codec_type == CODEC_TYPE_VIDEO) {
                    if (ast->frame_offset < ast->nb_index_entries) {
                        if (ast->index_entries[ast->frame_offset].flags & AVIIF_INDEX)
                            pkt->flags |= PKT_FLAG_KEY; 
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
            return size;
        }
    }
    return -1;
}

/* XXX: we make the implicit supposition that the position are sorted
   for each stream */
static int avi_read_idx1(AVFormatContext *s, int size)
{
    ByteIOContext *pb = &s->pb;
    int nb_index_entries, i;
    AVStream *st;
    AVIStream *ast;
    AVIIndexEntry *ie, *entries;
    unsigned int index, tag, flags, pos, len;
    
    nb_index_entries = size / 16;
    if (nb_index_entries <= 0)
        return -1;

    /* read the entries and sort them in each stream component */
    for(i = 0; i < nb_index_entries; i++) {
        tag = get_le32(pb);
        flags = get_le32(pb);
        pos = get_le32(pb);
        len = get_le32(pb);
#if defined(DEBUG_SEEK) && 0
        printf("%d: tag=0x%x flags=0x%x pos=0x%x len=%d\n", 
               i, tag, flags, pos, len);
#endif
        index = ((tag & 0xff) - '0') * 10;
        index += ((tag >> 8) & 0xff) - '0';
        if (index >= s->nb_streams)
            continue;
        st = s->streams[index];
        ast = st->priv_data;
        
        entries = av_fast_realloc(ast->index_entries,
                                  &ast->index_entries_allocated_size,
                                  (ast->nb_index_entries + 1) * 
                                  sizeof(AVIIndexEntry));
        if (entries) {
            ast->index_entries = entries;
            ie = &entries[ast->nb_index_entries++];
            ie->flags = flags;
            ie->pos = pos;
            ie->cum_len = ast->cum_len;
            ast->cum_len += len;
        }
    }
    return 0;
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
    url_fseek(pb, pos, SEEK_SET);
    return 0;
}

/* return the index entry whose position is immediately >= 'wanted_pos' */
static int locate_frame_in_index(AVIIndexEntry *entries, 
                                 int nb_entries, int wanted_pos)
{
    int a, b, m, pos;
    
    a = 0;
    b = nb_entries - 1;
    while (a <= b) {
        m = (a + b) >> 1;
        pos = entries[m].pos;
        if (pos == wanted_pos)
            goto found;
        else if (pos > wanted_pos) {
            b = m - 1;
        } else {
            a = m + 1;
        }
    }
    m = a;
    if (m > 0)
        m--;
 found:
    return m;
}

static int avi_read_seek(AVFormatContext *s, int stream_index, int64_t timestamp)
{
    AVIContext *avi = s->priv_data;
    AVStream *st;
    AVIStream *ast;
    int frame_number, i;
    int64_t pos;

    if (!avi->index_loaded) {
        /* we only load the index on demand */
        avi_load_index(s);
        avi->index_loaded = 1;
    }
    if (stream_index < 0) {
        for(i = 0; i < s->nb_streams; i++) {
            st = s->streams[i];
            if (st->codec.codec_type == CODEC_TYPE_VIDEO)
                goto found;
        }
        return -1;
    found:
        stream_index = i;
    }

    st = s->streams[stream_index];
    if (st->codec.codec_type != CODEC_TYPE_VIDEO)
        return -1;
    ast = st->priv_data;
    /* compute the frame number */
    frame_number = timestamp;
#ifdef DEBUG_SEEK
    printf("timestamp=%0.3f nb_indexes=%d frame_number=%d\n", 
           (double)timestamp / AV_TIME_BASE,
           ast->nb_index_entries, frame_number);
#endif
    /* find a closest key frame before */
    if (frame_number >= ast->nb_index_entries)
        return -1;
    while (frame_number >= 0 &&
           !(ast->index_entries[frame_number].flags & AVIIF_INDEX))
        frame_number--;
    if (frame_number < 0)
        return -1;
    ast->new_frame_offset = frame_number;

    /* find the position */
    pos = ast->index_entries[frame_number].pos;

#ifdef DEBUG_SEEK
    printf("key_frame_number=%d pos=0x%llx\n", 
           frame_number, pos);
#endif
    
    /* update the frame counters for all the other stream by looking
       at the positions just after the one found */
    for(i = 0; i < s->nb_streams; i++) {
        int j;
        if (i != stream_index) {
            st = s->streams[i];
            ast = st->priv_data;
            if (ast->nb_index_entries <= 0)
                return -1;
            j = locate_frame_in_index(ast->index_entries,
                                      ast->nb_index_entries,
                                      pos);
            /* get next frame */
            if ((j  + 1) < ast->nb_index_entries)
                j++;
            /* extract the current frame number */
            if (ast->sample_size==0)
                ast->new_frame_offset = j;
            else
                ast->new_frame_offset = ast->index_entries[j].cum_len;
        }
    }
    
    /* everything is OK now. We can update the frame offsets */
    for(i = 0; i < s->nb_streams; i++) {
        st = s->streams[i];
        ast = st->priv_data;
        ast->frame_offset = ast->new_frame_offset;
#ifdef DEBUG_SEEK
        printf("%d: frame_offset=%d\n", i, 
               ast->frame_offset);
#endif
    }
    /* do the seek */
    pos += avi->movi_list;
    url_fseek(&s->pb, pos, SEEK_SET);
    return 0;
}

static int avi_read_close(AVFormatContext *s)
{
    int i;
    AVIContext *avi = s->priv_data;

    for(i=0;i<s->nb_streams;i++) {
        AVStream *st = s->streams[i];
        AVIStream *ast = st->priv_data;
        if(ast){
            av_free(ast->index_entries);
            av_free(ast);
        }
        av_free(st->codec.extradata);
        av_free(st->codec.palctrl);
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
