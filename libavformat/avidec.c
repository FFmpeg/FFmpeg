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

typedef struct AVIIndex {
    unsigned char tag[4];
    unsigned int flags, pos, len;
    struct AVIIndex *next;
} AVIIndex;

typedef struct {
    int64_t  riff_end;
    int64_t  movi_end;
    offset_t movi_list;
    AVIIndex *first, *last;
    DVDemuxContext* dv_demux;
} AVIContext;

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
                avi->movi_end = url_ftell(pb) + size - 4;
#ifdef DEBUG
                printf("movi end=%Lx\n", avi->movi_end);
#endif
                goto end_of_header;
            }
            break;
        case MKTAG('a', 'v', 'i', 'h'):
	    /* avi header */
            /* using frame_period is bad idea */
            frame_period = get_le32(pb);
            bit_rate = get_le32(pb) * 8;
	    url_fskip(pb, 4 * 4);
            n = get_le32(pb);
            for(i=0;i<n;i++) {
                st = av_new_stream(s, i);
                if (!st)
                    goto fail;
	    }
            url_fskip(pb, size - 7 * 4);
            break;
        case MKTAG('s', 't', 'r', 'h'):
            /* stream header */
            stream_index++;
            tag1 = get_le32(pb);
            handler = get_le32(pb); /* codec tag */
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

                get_le32(pb); /* flags */
                get_le16(pb); /* priority */
                get_le16(pb); /* language */
                get_le32(pb); /* XXX: initial frame ? */
                scale = get_le32(pb); /* scale */
                rate = get_le32(pb); /* rate */

                if(scale && rate){
                    st->codec.frame_rate = rate;
                    st->codec.frame_rate_base = scale;
                }else if(frame_period){
                    st->codec.frame_rate = 1000000;
                    st->codec.frame_rate_base = frame_period;
                }else{
                    st->codec.frame_rate = 25;
                    st->codec.frame_rate_base = 1;
                }
                get_le32(pb); /* start */
                nb_frames = get_le32(pb);
                st->start_time = 0;
                st->duration = (double)nb_frames * 
                    st->codec.frame_rate_base * AV_TIME_BASE / 
                    st->codec.frame_rate;
                
		url_fskip(pb, size - 9 * 4);
                break;
            case MKTAG('a', 'u', 'd', 's'):
                {
                    unsigned int length, rate;

                    codec_type = CODEC_TYPE_AUDIO;

                    if (stream_index >= s->nb_streams) {
                        url_fskip(pb, size - 8);
                        break;
                    } 
                    st = s->streams[stream_index];

                    get_le32(pb); /* flags */
                    get_le16(pb); /* priority */
                    get_le16(pb); /* language */
                    get_le32(pb); /* initial frame */
                    get_le32(pb); /* scale */
                    rate = get_le32(pb);
                    get_le32(pb); /* start */
                    length = get_le32(pb); /* length, in samples or bytes */
                    st->start_time = 0;
                    if (rate != 0)
                        st->duration = (int64_t)length * AV_TIME_BASE / rate;
                    url_fskip(pb, size - 9 * 4);
                }
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
                    if (xan_video)
                        st->codec.codec_id = CODEC_ID_XAN_DPCM;
                    break;
                default:
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

    return 0;
}

static int avi_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVIContext *avi = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int n, d[8], size, i;
    void* dstr;

    memset(d, -1, sizeof(int)*8);
   
    if (avi->dv_demux) {
        size = dv_get_packet(avi->dv_demux, pkt);
	if (size >= 0)
	    return size;
    }
        
    for(i=url_ftell(pb); !url_feof(pb); i++) {
        int j;

	if (i >= avi->movi_end) { /* Let's see if it's an OpenDML AVI */
	    uint32_t tag, size, tag2;
	    url_fskip(pb, avi->riff_end - url_ftell(pb));
	    if (get_riff(avi, pb) < 0)
	        return -1;
	    
	    tag = get_le32(pb);
	    size = get_le32(pb);
	    tag2 = get_le32(pb);
	    if (tag == MKTAG('L','I','S','T') && tag2 == MKTAG('m','o','v','i'))
	        avi->movi_end = url_ftell(pb) + size - 4; 
	    else
	        return -1;
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
	    } else {
                pkt->stream_index = n;
                pkt->flags |= PKT_FLAG_KEY; // FIXME: We really should read 
		                            //        index for that
	    }
            return size;
        }
    }
    return -1;
}

static int avi_read_close(AVFormatContext *s)
{
    int i;
    AVIContext *avi = s->priv_data;

    for(i=0;i<s->nb_streams;i++) {
        AVStream *st = s->streams[i];
//        av_free(st->priv_data);
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
};

int avidec_init(void)
{
    av_register_input_format(&avi_iformat);
    return 0;
}
