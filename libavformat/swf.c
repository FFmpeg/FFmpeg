/*
 * Flash Compatible Streaming Format
 * Copyright (c) 2000 Fabrice Bellard.
 * Copyright (c) 2003 Tinic Uro.
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

/* should have a generic way to indicate probable size */
#define DUMMY_FILE_SIZE   (100 * 1024 * 1024)
#define DUMMY_DURATION    600 /* in seconds */

#define TAG_END           0
#define TAG_SHOWFRAME     1
#define TAG_DEFINESHAPE   2
#define TAG_FREECHARACTER 3
#define TAG_PLACEOBJECT   4
#define TAG_REMOVEOBJECT  5
#define TAG_STREAMHEAD    45
#define TAG_STREAMBLOCK   19
#define TAG_JPEG2         21
#define TAG_PLACEOBJECT2  26
#define TAG_STREAMHEAD2   45
#define TAG_VIDEOSTREAM	  60
#define TAG_VIDEOFRAME    61

#define TAG_LONG         0x100

/* flags for shape definition */
#define FLAG_MOVETO      0x01
#define FLAG_SETFILL0    0x02
#define FLAG_SETFILL1    0x04

#define SWF_VIDEO_CODEC_FLV1	0x02

#define AUDIO_FIFO_SIZE 65536

/* character id used */
#define BITMAP_ID 0
#define VIDEO_ID 0
#define SHAPE_ID  1

typedef struct SWFFrame_s {
    void *data;
    int size;
    struct SWFFrame_s *prev;
    struct SWFFrame_s *next;
} SWFFrame;

typedef struct {

    offset_t duration_pos;
    offset_t tag_pos;
    
    int samples_per_frame;
    int sound_samples;
    int video_samples;
    int skip_samples;
    int swf_frame_number;
    int video_frame_number;
    int ms_per_frame;
    int ch_id;
    int tag;

    uint8_t *audio_fifo;
    int audio_in_pos;
    int audio_out_pos;
    int audio_size;

    int video_type;
    int audio_type;
    
    SWFFrame *frame_head;
    SWFFrame *frame_tail;
} SWFContext;

static const int sSampleRates[3][4] = {
    {44100, 48000, 32000, 0},
    {22050, 24000, 16000, 0},
    {11025, 12000,  8000, 0},
};

static const int sBitRates[2][3][15] = {
    {   {  0, 32, 64, 96,128,160,192,224,256,288,320,352,384,416,448},
        {  0, 32, 48, 56, 64, 80, 96,112,128,160,192,224,256,320,384},
        {  0, 32, 40, 48, 56, 64, 80, 96,112,128,160,192,224,256,320}
    },
    {   {  0, 32, 48, 56, 64, 80, 96,112,128,144,160,176,192,224,256},
        {  0,  8, 16, 24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160},
        {  0,  8, 16, 24, 32, 40, 48, 56, 64, 80, 96,112,128,144,160}
    },
};

static const int sSamplesPerFrame[3][3] =
{
    {  384,     1152,    1152 },
    {  384,     1152,     576 },
    {  384,     1152,     576 }
};

static const int sBitsPerSlot[3] = {
    32,
    8,
    8
};

static int swf_mp3_info(void *data, int *byteSize, int *samplesPerFrame, int *sampleRate, int *isMono )
{
    uint8_t *dataTmp = (uint8_t *)data;
    uint32_t header = ( (uint32_t)dataTmp[0] << 24 ) | ( (uint32_t)dataTmp[1] << 16 ) | ( (uint32_t)dataTmp[2] << 8 ) | (uint32_t)dataTmp[3];
    int layerID = 3 - ((header >> 17) & 0x03);
    int bitRateID = ((header >> 12) & 0x0f);
    int sampleRateID = ((header >> 10) & 0x03);
    int bitRate = 0;
    int bitsPerSlot = sBitsPerSlot[layerID];
    int isPadded = ((header >> 9) & 0x01);
    
    if ( (( header >> 21 ) & 0x7ff) != 0x7ff ) {
        return 0;
    }

    *isMono = ((header >>  6) & 0x03) == 0x03;

    if ( (header >> 19 ) & 0x01 ) {
        *sampleRate = sSampleRates[0][sampleRateID];
        bitRate = sBitRates[0][layerID][bitRateID] * 1000;
        *samplesPerFrame = sSamplesPerFrame[0][layerID];
    } else {
        if ( (header >> 20) & 0x01 ) {
            *sampleRate = sSampleRates[1][sampleRateID];
            bitRate = sBitRates[1][layerID][bitRateID] * 1000;
            *samplesPerFrame = sSamplesPerFrame[1][layerID];
        } else {
            *sampleRate = sSampleRates[2][sampleRateID];
            bitRate = sBitRates[1][layerID][bitRateID] * 1000;
            *samplesPerFrame = sSamplesPerFrame[2][layerID];
        }
    }

    *byteSize = ( ( ( ( *samplesPerFrame * (bitRate / bitsPerSlot) ) / *sampleRate ) + isPadded ) );

    return 1;
}

#ifdef CONFIG_ENCODERS
static void put_swf_tag(AVFormatContext *s, int tag)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = &s->pb;

    swf->tag_pos = url_ftell(pb);
    swf->tag = tag;
    /* reserve some room for the tag */
    if (tag & TAG_LONG) {
        put_le16(pb, 0);
        put_le32(pb, 0);
    } else {
        put_le16(pb, 0);
    }
}

static void put_swf_end_tag(AVFormatContext *s)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    offset_t pos;
    int tag_len, tag;

    pos = url_ftell(pb);
    tag_len = pos - swf->tag_pos - 2;
    tag = swf->tag;
    url_fseek(pb, swf->tag_pos, SEEK_SET);
    if (tag & TAG_LONG) {
        tag &= ~TAG_LONG;
        put_le16(pb, (tag << 6) | 0x3f);
        put_le32(pb, tag_len - 4);
    } else {
        assert(tag_len < 0x3f);
        put_le16(pb, (tag << 6) | tag_len);
    }
    url_fseek(pb, pos, SEEK_SET);
}

static inline void max_nbits(int *nbits_ptr, int val)
{
    int n;

    if (val == 0)
        return;
    val = abs(val);
    n = 1;
    while (val != 0) {
        n++;
        val >>= 1;
    }
    if (n > *nbits_ptr)
        *nbits_ptr = n;
}

static void put_swf_rect(ByteIOContext *pb, 
                         int xmin, int xmax, int ymin, int ymax)
{
    PutBitContext p;
    uint8_t buf[256];
    int nbits, mask;

    init_put_bits(&p, buf, sizeof(buf));
    
    nbits = 0;
    max_nbits(&nbits, xmin);
    max_nbits(&nbits, xmax);
    max_nbits(&nbits, ymin);
    max_nbits(&nbits, ymax);
    mask = (1 << nbits) - 1;

    /* rectangle info */
    put_bits(&p, 5, nbits);
    put_bits(&p, nbits, xmin & mask);
    put_bits(&p, nbits, xmax & mask);
    put_bits(&p, nbits, ymin & mask);
    put_bits(&p, nbits, ymax & mask);
    
    flush_put_bits(&p);
    put_buffer(pb, buf, pbBufPtr(&p) - p.buf);
}

static void put_swf_line_edge(PutBitContext *pb, int dx, int dy)
{
    int nbits, mask;

    put_bits(pb, 1, 1); /* edge */
    put_bits(pb, 1, 1); /* line select */
    nbits = 2;
    max_nbits(&nbits, dx);
    max_nbits(&nbits, dy);

    mask = (1 << nbits) - 1;
    put_bits(pb, 4, nbits - 2); /* 16 bits precision */
    if (dx == 0) {
      put_bits(pb, 1, 0); 
      put_bits(pb, 1, 1); 
      put_bits(pb, nbits, dy & mask);
    } else if (dy == 0) {
      put_bits(pb, 1, 0); 
      put_bits(pb, 1, 0); 
      put_bits(pb, nbits, dx & mask);
    } else {
      put_bits(pb, 1, 1); 
      put_bits(pb, nbits, dx & mask);
      put_bits(pb, nbits, dy & mask);
    }
}

#define FRAC_BITS 16

/* put matrix */
static void put_swf_matrix(ByteIOContext *pb,
                           int a, int b, int c, int d, int tx, int ty)
{
    PutBitContext p;
    uint8_t buf[256];
    int nbits;

    init_put_bits(&p, buf, sizeof(buf));
    
    put_bits(&p, 1, 1); /* a, d present */
    nbits = 1;
    max_nbits(&nbits, a);
    max_nbits(&nbits, d);
    put_bits(&p, 5, nbits); /* nb bits */
    put_bits(&p, nbits, a);
    put_bits(&p, nbits, d);
    
    put_bits(&p, 1, 1); /* b, c present */
    nbits = 1;
    max_nbits(&nbits, c);
    max_nbits(&nbits, b);
    put_bits(&p, 5, nbits); /* nb bits */
    put_bits(&p, nbits, c);
    put_bits(&p, nbits, b);

    nbits = 1;
    max_nbits(&nbits, tx);
    max_nbits(&nbits, ty);
    put_bits(&p, 5, nbits); /* nb bits */
    put_bits(&p, nbits, tx);
    put_bits(&p, nbits, ty);

    flush_put_bits(&p);
    put_buffer(pb, buf, pbBufPtr(&p) - p.buf);
}

/* */
static int swf_write_header(AVFormatContext *s)
{
    SWFContext *swf;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc, *audio_enc, *video_enc;
    PutBitContext p;
    uint8_t buf1[256];
    int i, width, height, rate, rate_base;

    swf = av_malloc(sizeof(SWFContext));
    if (!swf)
        return -1;
    s->priv_data = swf;

    swf->ch_id = -1;
    swf->audio_in_pos = 0;
    swf->audio_out_pos = 0;
    swf->audio_size = 0;
    swf->audio_fifo = av_malloc(AUDIO_FIFO_SIZE);
    swf->frame_head = 0;
    swf->frame_tail = 0;
    swf->sound_samples = 0;
    swf->video_samples = 0;
    swf->swf_frame_number = 0;
    swf->video_frame_number = 0;
    swf->skip_samples = 0;

    video_enc = NULL;
    audio_enc = NULL;
    for(i=0;i<s->nb_streams;i++) {
        enc = &s->streams[i]->codec;
        if (enc->codec_type == CODEC_TYPE_AUDIO)
            audio_enc = enc;
        else {
            if ( enc->codec_id == CODEC_ID_FLV1 || enc->codec_id == CODEC_ID_MJPEG ) {
                video_enc = enc;
            } else {
                av_log(enc, AV_LOG_ERROR, "SWF only supports FLV1 and MJPEG\n");
                return -1;
            }
        }
    }

    if (!video_enc) {
        /* currenty, cannot work correctly if audio only */
        swf->video_type = 0;
        width = 320;
        height = 200;
        rate = 10;
        rate_base= 1;
    } else {
        swf->video_type = video_enc->codec_id;
        width = video_enc->width;
        height = video_enc->height;
        rate = video_enc->frame_rate;
        rate_base = video_enc->frame_rate_base;
    }

    if (!audio_enc ) {
        swf->audio_type = 0;
        swf->samples_per_frame = ( 44100. * rate_base ) / rate;
    } else {
        swf->audio_type = audio_enc->codec_id;
        swf->samples_per_frame = ( ( audio_enc->sample_rate ) * rate_base ) / rate;
    }

    put_tag(pb, "FWS");
    if ( video_enc && video_enc->codec_id == CODEC_ID_FLV1 ) {
        put_byte(pb, 6); /* version (version 6 and above support FLV1 codec) */
    } else {
        put_byte(pb, 4); /* version (should use 4 for mpeg audio support) */
    }
    put_le32(pb, DUMMY_FILE_SIZE); /* dummy size 
                                      (will be patched if not streamed) */ 

    put_swf_rect(pb, 0, width * 20, 0, height * 20);
    put_le16(pb, (rate * 256) / rate_base); /* frame rate */
    swf->duration_pos = url_ftell(pb);
    put_le16(pb, (uint16_t)(DUMMY_DURATION * (int64_t)rate / rate_base)); /* frame count */
    
    /* define a shape with the jpeg inside */
    if ( video_enc && video_enc->codec_id == CODEC_ID_FLV1 ) {
    } else if ( video_enc && video_enc->codec_id == CODEC_ID_MJPEG ) {
        put_swf_tag(s, TAG_DEFINESHAPE);

        put_le16(pb, SHAPE_ID); /* ID of shape */
        /* bounding rectangle */
        put_swf_rect(pb, 0, width, 0, height);
        /* style info */
        put_byte(pb, 1); /* one fill style */
        put_byte(pb, 0x41); /* clipped bitmap fill */
        put_le16(pb, BITMAP_ID); /* bitmap ID */
        /* position of the bitmap */
        put_swf_matrix(pb, (int)(1.0 * (1 << FRAC_BITS)), 0, 
                        0, (int)(1.0 * (1 << FRAC_BITS)), 0, 0);
        put_byte(pb, 0); /* no line style */
    
        /* shape drawing */
        init_put_bits(&p, buf1, sizeof(buf1));
        put_bits(&p, 4, 1); /* one fill bit */
        put_bits(&p, 4, 0); /* zero line bit */
     
        put_bits(&p, 1, 0); /* not an edge */
        put_bits(&p, 5, FLAG_MOVETO | FLAG_SETFILL0);
        put_bits(&p, 5, 1); /* nbits */
        put_bits(&p, 1, 0); /* X */
        put_bits(&p, 1, 0); /* Y */
        put_bits(&p, 1, 1); /* set fill style 1 */
    
        /* draw the rectangle ! */
        put_swf_line_edge(&p, width, 0);
        put_swf_line_edge(&p, 0, height);
        put_swf_line_edge(&p, -width, 0);
        put_swf_line_edge(&p, 0, -height);
    
        /* end of shape */
        put_bits(&p, 1, 0); /* not an edge */
        put_bits(&p, 5, 0);

        flush_put_bits(&p);
        put_buffer(pb, buf1, pbBufPtr(&p) - p.buf);

        put_swf_end_tag(s);
    }
    
    if (audio_enc && audio_enc->codec_id == CODEC_ID_MP3 ) {
        int v;

        /* start sound */
        put_swf_tag(s, TAG_STREAMHEAD2);

        v = 0;
        switch(audio_enc->sample_rate) {
        case 11025:
            v |= 1 << 2;
            break;
        case 22050:
            v |= 2 << 2;
            break;
        case 44100:
            v |= 3 << 2;
            break;
        default:
            /* not supported */
            av_free(swf->audio_fifo);
            av_free(swf);
            return -1;
        }
        v |= 0x02; /* 16 bit playback */
        if (audio_enc->channels == 2)
            v |= 0x01; /* stereo playback */
        put_byte(&s->pb, v);
        v |= 0x20; /* mp3 compressed */
        put_byte(&s->pb, v);
        put_le16(&s->pb, swf->samples_per_frame);  /* avg samples per frame */
        put_le16(&s->pb, 0);
        
        put_swf_end_tag(s);
    }

    put_flush_packet(&s->pb);
    return 0;
}

static int swf_write_video(AVFormatContext *s, 
                           AVCodecContext *enc, const uint8_t *buf, int size)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    int c = 0;
    int outSize = 0;
    int outSamples = 0;
    
    /* Flash Player limit */
    if ( swf->swf_frame_number == 16000 ) {
        av_log(enc, AV_LOG_INFO, "warning: Flash Player limit of 16000 frames reached\n");
    }

    /* Store video data in queue */
    if ( enc->codec_type == CODEC_TYPE_VIDEO ) {
        SWFFrame *new_frame = av_malloc(sizeof(SWFFrame));
        new_frame->prev = 0;
        new_frame->next = swf->frame_head;
        new_frame->data = av_malloc(size);
        new_frame->size = size;
        memcpy(new_frame->data,buf,size);
        swf->frame_head = new_frame;
        if ( swf->frame_tail == 0 ) {
            swf->frame_tail = new_frame;
        }
    }
    
    if ( swf->audio_type ) {
        /* Prescan audio data for this swf frame */
retry_swf_audio_packet:
        if ( ( swf->audio_size-outSize ) >= 4 ) {
            int mp3FrameSize = 0;
            int mp3SampleRate = 0;
            int mp3IsMono = 0;
            int mp3SamplesPerFrame = 0;
            
            /* copy out mp3 header from ring buffer */
            uint8_t header[4];
            for (c=0; c<4; c++) {
                header[c] = swf->audio_fifo[(swf->audio_in_pos+outSize+c) % AUDIO_FIFO_SIZE];
            }
            
            if ( swf_mp3_info(header,&mp3FrameSize,&mp3SamplesPerFrame,&mp3SampleRate,&mp3IsMono) ) {
                if ( ( swf->audio_size-outSize ) >= mp3FrameSize ) {
                    outSize += mp3FrameSize;
                    outSamples += mp3SamplesPerFrame;
                    if ( ( swf->sound_samples + outSamples + swf->samples_per_frame ) < swf->video_samples ) {
                        goto retry_swf_audio_packet;
                    }
                }
            } else {
                /* invalid mp3 data, skip forward
                we need to do this since the Flash Player 
                does not like custom headers */
                swf->audio_in_pos ++;
                swf->audio_size --;
                swf->audio_in_pos %= AUDIO_FIFO_SIZE;
                goto retry_swf_audio_packet;
            }
        }
        
        /* audio stream is behind video stream, bail */
        if ( ( swf->sound_samples + outSamples + swf->samples_per_frame ) < swf->video_samples ) {
            return 0;
        }

        /* compute audio/video drift */
        if ( enc->codec_type == CODEC_TYPE_VIDEO ) {
            swf->skip_samples = (int)( ( (double)(swf->swf_frame_number) * (double)enc->frame_rate_base * 44100. ) / (double)(enc->frame_rate) );
            swf->skip_samples -=  swf->video_samples;
        }
    }

    /* check if we need to insert a padding frame */
    if (swf->skip_samples <= ( swf->samples_per_frame / 2 ) ) {
        /* no, it is time for a real frame, check if one is available */
        if ( swf->frame_tail ) {
            if ( swf->video_type == CODEC_ID_FLV1 ) {
                if ( swf->video_frame_number == 0 ) {
                    /* create a new video object */
                    put_swf_tag(s, TAG_VIDEOSTREAM);
                    put_le16(pb, VIDEO_ID);
                    put_le16(pb, 15000 ); /* hard flash player limit */
                    put_le16(pb, enc->width);
                    put_le16(pb, enc->height);
                    put_byte(pb, 0);
                    put_byte(pb, SWF_VIDEO_CODEC_FLV1);
                    put_swf_end_tag(s);
                    
                    /* place the video object for the first time */
                    put_swf_tag(s, TAG_PLACEOBJECT2);
                    put_byte(pb, 0x36);
                    put_le16(pb, 1);
                    put_le16(pb, VIDEO_ID);
                    put_swf_matrix(pb, 1 << FRAC_BITS, 0, 0, 1 << FRAC_BITS, 0, 0);
                    put_le16(pb, swf->video_frame_number );
                    put_byte(pb, 'v');
                    put_byte(pb, 'i');
                    put_byte(pb, 'd');
                    put_byte(pb, 'e');
                    put_byte(pb, 'o');
                    put_byte(pb, 0x00);
                    put_swf_end_tag(s);
                } else {
                    /* mark the character for update */
                    put_swf_tag(s, TAG_PLACEOBJECT2);
                    put_byte(pb, 0x11);
                    put_le16(pb, 1);
                    put_le16(pb, swf->video_frame_number );
                    put_swf_end_tag(s);
                }
    
                // write out pending frames
                for (; ( enc->frame_number - swf->video_frame_number ) > 0;) {
                    /* set video frame data */
                    put_swf_tag(s, TAG_VIDEOFRAME | TAG_LONG);
                    put_le16(pb, VIDEO_ID); 
                    put_le16(pb, swf->video_frame_number++ );
                    put_buffer(pb, swf->frame_tail->data, swf->frame_tail->size);
                    put_swf_end_tag(s);
                }

            } else if ( swf->video_type == CODEC_ID_MJPEG ) {
                if (swf->swf_frame_number > 0) {
                    /* remove the shape */
                    put_swf_tag(s, TAG_REMOVEOBJECT);
                    put_le16(pb, SHAPE_ID); /* shape ID */
                    put_le16(pb, 1); /* depth */
                    put_swf_end_tag(s);
                
                    /* free the bitmap */
                    put_swf_tag(s, TAG_FREECHARACTER);
                    put_le16(pb, BITMAP_ID);
                    put_swf_end_tag(s);
                }
        
                put_swf_tag(s, TAG_JPEG2 | TAG_LONG);
        
                put_le16(pb, BITMAP_ID); /* ID of the image */
        
                /* a dummy jpeg header seems to be required */
                put_byte(pb, 0xff); 
                put_byte(pb, 0xd8);
                put_byte(pb, 0xff);
                put_byte(pb, 0xd9);
                /* write the jpeg image */
                put_buffer(pb, swf->frame_tail->data, swf->frame_tail->size);
        
                put_swf_end_tag(s);
        
                /* draw the shape */
        
                put_swf_tag(s, TAG_PLACEOBJECT);
                put_le16(pb, SHAPE_ID); /* shape ID */
                put_le16(pb, 1); /* depth */
                put_swf_matrix(pb, 20 << FRAC_BITS, 0, 0, 20 << FRAC_BITS, 0, 0);
                put_swf_end_tag(s);
            } else {
                /* invalid codec */
            }
    
            av_free(swf->frame_tail->data);
            swf->frame_tail = swf->frame_tail->prev;
            if ( swf->frame_tail ) {
                if ( swf->frame_tail->next ) {
                    av_free(swf->frame_tail->next);
                }
                swf->frame_tail->next = 0;
            } else {
                swf->frame_head = 0;
            }
            swf->swf_frame_number ++;
        }
    }

    swf->video_samples += swf->samples_per_frame;

    /* streaming sound always should be placed just before showframe tags */
    if ( outSize > 0 ) {
        put_swf_tag(s, TAG_STREAMBLOCK | TAG_LONG);
        put_le16(pb, outSamples);
        put_le16(pb, 0);
        for (c=0; c<outSize; c++) {
            put_byte(pb,swf->audio_fifo[(swf->audio_in_pos+c) % AUDIO_FIFO_SIZE]);
        }
        put_swf_end_tag(s);
    
        /* update FIFO */
        swf->sound_samples += outSamples;
        swf->audio_in_pos += outSize;
        swf->audio_size -= outSize;
        swf->audio_in_pos %= AUDIO_FIFO_SIZE;
    }

    /* output the frame */
    put_swf_tag(s, TAG_SHOWFRAME);
    put_swf_end_tag(s);
    
    put_flush_packet(&s->pb);
    
    return 0;
}

static int swf_write_audio(AVFormatContext *s, 
                           AVCodecContext *enc, const uint8_t *buf, int size)
{
    SWFContext *swf = s->priv_data;
    int c = 0;

    /* Flash Player limit */
    if ( swf->swf_frame_number == 16000 ) {
        av_log(enc, AV_LOG_INFO, "warning: Flash Player limit of 16000 frames reached\n");
    }

    if (enc->codec_id == CODEC_ID_MP3 ) {
        for (c=0; c<size; c++) {
            swf->audio_fifo[(swf->audio_out_pos+c)%AUDIO_FIFO_SIZE] = buf[c];
        }
        swf->audio_size += size;
        swf->audio_out_pos += size;
        swf->audio_out_pos %= AUDIO_FIFO_SIZE;
    }

    /* if audio only stream make sure we add swf frames */
    if ( swf->video_type == 0 ) {
        swf_write_video(s, enc, 0, 0);
    }

    return 0;
}

static int swf_write_packet(AVFormatContext *s, AVPacket *pkt)
{
    AVCodecContext *codec = &s->streams[pkt->stream_index]->codec;
    if (codec->codec_type == CODEC_TYPE_AUDIO)
        return swf_write_audio(s, codec, pkt->data, pkt->size);
    else
        return swf_write_video(s, codec, pkt->data, pkt->size);
}

static int swf_write_trailer(AVFormatContext *s)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVCodecContext *enc, *video_enc;
    int file_size, i;

    video_enc = NULL;
    for(i=0;i<s->nb_streams;i++) {
        enc = &s->streams[i]->codec;
        if (enc->codec_type == CODEC_TYPE_VIDEO)
            video_enc = enc;
    }

    put_swf_tag(s, TAG_END);
    put_swf_end_tag(s);
    
    put_flush_packet(&s->pb);

    /* patch file size and number of frames if not streamed */
    if (!url_is_streamed(&s->pb) && video_enc) {
        file_size = url_ftell(pb);
        url_fseek(pb, 4, SEEK_SET);
        put_le32(pb, file_size);
        url_fseek(pb, swf->duration_pos, SEEK_SET);
        put_le16(pb, video_enc->frame_number);
    }
    
    av_free(swf->audio_fifo);

    return 0;
}
#endif //CONFIG_ENCODERS

/*********************************************/
/* Extract FLV encoded frame and MP3 from swf
   Note that the detection of the real frame
   is inaccurate at this point as it can be
   quite tricky to determine, you almost certainly 
   will get a bad audio/video sync */

static int get_swf_tag(ByteIOContext *pb, int *len_ptr)
{
    int tag, len;
    
    if (url_feof(pb))
        return -1;

    tag = get_le16(pb);
    len = tag & 0x3f;
    tag = tag >> 6;
    if (len == 0x3f) {
        len = get_le32(pb);
    }
    *len_ptr = len;
    return tag;
}


static int swf_probe(AVProbeData *p)
{
    /* check file header */
    if (p->buf_size <= 16)
        return 0;
    if (p->buf[0] == 'F' && p->buf[1] == 'W' &&
        p->buf[2] == 'S')
        return AVPROBE_SCORE_MAX;
    else
        return 0;
}

static int swf_read_header(AVFormatContext *s, AVFormatParameters *ap)
{
    SWFContext *swf = 0;
    ByteIOContext *pb = &s->pb;
    int nbits, len, frame_rate, tag, v;
    offset_t firstTagOff;
    AVStream *ast = 0;
    AVStream *vst = 0;

    swf = av_malloc(sizeof(SWFContext));
    if (!swf)
        return -1;
    s->priv_data = swf;

    if ((get_be32(pb) & 0xffffff00) != MKBETAG('F', 'W', 'S', 0))
        return AVERROR_IO;
    get_le32(pb);
    /* skip rectangle size */
    nbits = get_byte(pb) >> 3;
    len = (4 * nbits - 3 + 7) / 8;
    url_fskip(pb, len);
    frame_rate = get_le16(pb);
    get_le16(pb); /* frame count */
    
    /* The Flash Player converts 8.8 frame rates 
       to milliseconds internally. Do the same to get 
       a correct framerate */
    swf->ms_per_frame = ( 1000 * 256 ) / frame_rate;
    swf->samples_per_frame = 0;
    swf->ch_id = -1;

    firstTagOff = url_ftell(pb);
    for(;;) {
        tag = get_swf_tag(pb, &len);
        if (tag < 0) {
            if ( ast || vst ) {
                if ( vst && ast ) {
                    vst->codec.frame_rate = ast->codec.sample_rate / swf->samples_per_frame;
                    vst->codec.frame_rate_base = 1;
                }
                break;
            }
            av_log(s, AV_LOG_ERROR, "No media found in SWF\n");
            return AVERROR_IO;
        }
        if ( tag == TAG_VIDEOSTREAM && !vst) {
            swf->ch_id = get_le16(pb);
            get_le16(pb);
            get_le16(pb);
            get_le16(pb);
            get_byte(pb);
            /* Check for FLV1 */
            if ( get_byte(pb) == SWF_VIDEO_CODEC_FLV1 ) {
                vst = av_new_stream(s, 0);
                av_set_pts_info(vst, 24, 1, 1000); /* 24 bit pts in ms */
    
                vst->codec.codec_type = CODEC_TYPE_VIDEO;
                vst->codec.codec_id = CODEC_ID_FLV1;
                if ( swf->samples_per_frame ) {
                    vst->codec.frame_rate = 1000. / swf->ms_per_frame;
                    vst->codec.frame_rate_base = 1;
                }
            }
        } else if ( ( tag == TAG_STREAMHEAD || tag == TAG_STREAMHEAD2 ) && !ast) {
            /* streaming found */
            get_byte(pb);
            v = get_byte(pb);
            swf->samples_per_frame = get_le16(pb);
            if (len!=4)
                url_fskip(pb,len-4);
            /* if mp3 streaming found, OK */
            if ((v & 0x20) != 0) {
                if ( tag == TAG_STREAMHEAD2 ) {
                    get_le16(pb);
                }
                ast = av_new_stream(s, 1);
                av_set_pts_info(ast, 24, 1, 1000); /* 24 bit pts in ms */
                if (!ast)
                    return -ENOMEM;

                if (v & 0x01)
                    ast->codec.channels = 2;
                else
                    ast->codec.channels = 1;

                switch((v>> 2) & 0x03) {
                case 1:
                    ast->codec.sample_rate = 11025;
                    break;
                case 2:
                    ast->codec.sample_rate = 22050;
                    break;
                case 3:
                    ast->codec.sample_rate = 44100;
                    break;
                default:
                    av_free(ast);
                    return AVERROR_IO;
                }
                ast->codec.codec_type = CODEC_TYPE_AUDIO;
                ast->codec.codec_id = CODEC_ID_MP3;
            }
        } else {
            url_fskip(pb, len);
        }
    }
    url_fseek(pb, firstTagOff, SEEK_SET);
    
    return 0;
}

static int swf_read_packet(AVFormatContext *s, AVPacket *pkt)
{
    SWFContext *swf = s->priv_data;
    ByteIOContext *pb = &s->pb;
    AVStream *st = 0;
    int tag, len, i, frame;
    
    for(;;) {
        tag = get_swf_tag(pb, &len);
        if (tag < 0) 
            return AVERROR_IO;
        if (tag == TAG_VIDEOFRAME) {
            for( i=0; i<s->nb_streams; i++ ) {
        	st = s->streams[i];
                if (st->id == 0) {
                    if ( get_le16(pb) == swf->ch_id ) {
                        frame = get_le16(pb);
                        av_new_packet(pkt, len-4);
                        pkt->pts = frame * swf->ms_per_frame;
                        pkt->stream_index = st->index;
                        get_buffer(pb, pkt->data, pkt->size);
                        return pkt->size;
                    } else {
                        url_fskip(pb, len-2);
                        continue;
                    }
                }
            }    
            url_fskip(pb, len);
        } else if (tag == TAG_STREAMBLOCK) {
            for( i=0; i<s->nb_streams; i++ ) {
        	st = s->streams[i];
                if (st->id == 1) {
                    av_new_packet(pkt, len);
                    pkt->stream_index = st->index;
                    get_buffer(pb, pkt->data, pkt->size);
                    return pkt->size;
                }
            }
            url_fskip(pb, len);
        } else {
            url_fskip(pb, len);
        }
    }
    return 0;
}

static int swf_read_close(AVFormatContext *s)
{
     return 0;
}

static AVInputFormat swf_iformat = {
    "swf",
    "Flash format",
    sizeof(SWFContext),
    swf_probe,
    swf_read_header,
    swf_read_packet,
    swf_read_close,
};

#ifdef CONFIG_ENCODERS
static AVOutputFormat swf_oformat = {
    "swf",
    "Flash format",
    "application/x-shockwave-flash",
    "swf",
    sizeof(SWFContext),
    CODEC_ID_MP3,
    CODEC_ID_FLV1,
    swf_write_header,
    swf_write_packet,
    swf_write_trailer,
};
#endif //CONFIG_ENCODERS

int swf_init(void)
{
    av_register_input_format(&swf_iformat);
#ifdef CONFIG_ENCODERS
    av_register_output_format(&swf_oformat);
#endif //CONFIG_ENCODERS
    return 0;
}
