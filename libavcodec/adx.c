/*
 * ADX ADPCM codecs
 * Copyright (c) 2001,2003 BERO
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
#include "avcodec.h"

/**
 * @file adx.c
 * SEGA CRI adx codecs.
 *
 * Reference documents:
 * http://ku-www.ss.titech.ac.jp/~yatsushi/adx.html
 * adx2wav & wav2adx http://www.geocities.co.jp/Playtown/2004/
 */

typedef struct {
    int s1,s2;
} PREV;

typedef struct {
    PREV prev[2];
    int header_parsed;
    unsigned char dec_temp[18*2];
    unsigned short enc_temp[32*2];
    int in_temp;
} ADXContext;

//#define    BASEVOL    0x11e0
#define    BASEVOL   0x4000
#define    SCALE1    0x7298
#define    SCALE2    0x3350

#define    CLIP(s)    if (s>32767) s=32767; else if (s<-32768) s=-32768

/* 18 bytes <-> 32 samples */

#ifdef CONFIG_ENCODERS
static void adx_encode(unsigned char *adx,const short *wav,PREV *prev)
{
    int scale;
    int i;
    int s0,s1,s2,d;
    int max=0;
    int min=0;
    int data[32];

    s1 = prev->s1;
    s2 = prev->s2;
    for(i=0;i<32;i++) {
        s0 = wav[i];
        d = ((s0<<14) - SCALE1*s1 + SCALE2*s2)/BASEVOL;
        data[i]=d;
        if (max<d) max=d;
        if (min>d) min=d;
        s2 = s1;
        s1 = s0;
    }
    prev->s1 = s1;
    prev->s2 = s2;

    /* -8..+7 */

    if (max==0 && min==0) {
        memset(adx,0,18);
        return;
    }

    if (max/7>-min/8) scale = max/7;
    else scale = -min/8;

    if (scale==0) scale=1;

    adx[0] = scale>>8;
    adx[1] = scale;

    for(i=0;i<16;i++) {
        adx[i+2] = ((data[i*2]/scale)<<4) | ((data[i*2+1]/scale)&0xf);
    }
}
#endif //CONFIG_ENCODERS

static void adx_decode(short *out,const unsigned char *in,PREV *prev)
{
    int scale = ((in[0]<<8)|(in[1]));
    int i;
    int s0,s1,s2,d;

//    printf("%x ",scale);

    in+=2;
    s1 = prev->s1;
    s2 = prev->s2;
    for(i=0;i<16;i++) {
        d = in[i];
        // d>>=4; if (d&8) d-=16;
        d = ((signed char)d >> 4);
        s0 = (BASEVOL*d*scale + SCALE1*s1 - SCALE2*s2)>>14;
        CLIP(s0);
        *out++=s0;
        s2 = s1;
        s1 = s0;

        d = in[i];
        //d&=15; if (d&8) d-=16;
        d = ((signed char)(d<<4) >> 4);
        s0 = (BASEVOL*d*scale + SCALE1*s1 - SCALE2*s2)>>14;
        CLIP(s0);
        *out++=s0;
        s2 = s1;
        s1 = s0;
    }
    prev->s1 = s1;
    prev->s2 = s2;

}

static void adx_decode_stereo(short *out,const unsigned char *in,PREV *prev)
{
    short tmp[32*2];
    int i;

    adx_decode(tmp   ,in   ,prev);
    adx_decode(tmp+32,in+18,prev+1);
    for(i=0;i<32;i++) {
        out[i*2]   = tmp[i];
        out[i*2+1] = tmp[i+32];
    }
}

#ifdef CONFIG_ENCODERS

static void write_long(unsigned char *p,uint32_t v)
{
    p[0] = v>>24;
    p[1] = v>>16;
    p[2] = v>>8;
    p[3] = v;
}

static int adx_encode_header(AVCodecContext *avctx,unsigned char *buf,size_t bufsize)
{
#if 0
    struct {
        uint32_t offset; /* 0x80000000 + sample start - 4 */
        unsigned char unknown1[3]; /* 03 12 04 */
        unsigned char channel; /* 1 or 2 */
        uint32_t freq;
        uint32_t size;
        uint32_t unknown2; /* 01 f4 03 00 */
        uint32_t unknown3; /* 00 00 00 00 */
        uint32_t unknown4; /* 00 00 00 00 */

    /* if loop
        unknown3 00 15 00 01
        unknown4 00 00 00 01
        long loop_start_sample;
        long loop_start_byte;
        long loop_end_sample;
        long loop_end_byte;
        long 
    */
    } adxhdr; /* big endian */
    /* offset-6 "(c)CRI" */
#endif
    write_long(buf+0x00,0x80000000|0x20);
    write_long(buf+0x04,0x03120400|avctx->channels);
    write_long(buf+0x08,avctx->sample_rate);
    write_long(buf+0x0c,0); /* FIXME: set after */
    write_long(buf+0x10,0x01040300);
    write_long(buf+0x14,0x00000000);
    write_long(buf+0x18,0x00000000);
    memcpy(buf+0x1c,"\0\0(c)CRI",8);
    return 0x20+4;
}

static int adx_decode_init(AVCodecContext *avctx);
static int adx_encode_init(AVCodecContext *avctx)
{
    if (avctx->channels > 2)
        return -1; /* only stereo or mono =) */
    avctx->frame_size = 32;

    avctx->coded_frame= avcodec_alloc_frame();
    avctx->coded_frame->key_frame= 1;

//    avctx->bit_rate = avctx->sample_rate*avctx->channels*18*8/32;

    av_log(avctx, AV_LOG_DEBUG, "adx encode init\n");
    adx_decode_init(avctx);

    return 0;
}

static int adx_encode_close(AVCodecContext *avctx)
{
    av_freep(&avctx->coded_frame);

    return 0;
}

static int adx_encode_frame(AVCodecContext *avctx,
                uint8_t *frame, int buf_size, void *data)
{
    ADXContext *c = avctx->priv_data;
    const short *samples = data;
    unsigned char *dst = frame;
    int rest = avctx->frame_size;

/*
    input data size =
    ffmpeg.c: do_audio_out()
    frame_bytes = enc->frame_size * 2 * enc->channels;
*/

//    printf("sz=%d ",buf_size); fflush(stdout);
    if (!c->header_parsed) {
        int hdrsize = adx_encode_header(avctx,dst,buf_size);
        dst+=hdrsize;
        c->header_parsed = 1;
    }

    if (avctx->channels==1) {
        while(rest>=32) {
            adx_encode(dst,samples,c->prev);
            dst+=18;
            samples+=32;
            rest-=32;
        }
    } else {
        while(rest>=32*2) {
            short tmpbuf[32*2];
            int i;

            for(i=0;i<32;i++) {
                tmpbuf[i] = samples[i*2];
                tmpbuf[i+32] = samples[i*2+1];
            }

            adx_encode(dst,tmpbuf,c->prev);
            adx_encode(dst+18,tmpbuf+32,c->prev+1);
            dst+=18*2;
            samples+=32*2;
            rest-=32*2;
        }
    }
    return dst-frame;
}

#endif //CONFIG_ENCODERS

static uint32_t read_long(const unsigned char *p)
{
    return (p[0]<<24)|(p[1]<<16)|(p[2]<<8)|p[3];
}

int is_adx(const unsigned char *buf,size_t bufsize)
{
    int    offset;

    if (buf[0]!=0x80) return 0;
    offset = (read_long(buf)^0x80000000)+4;
    if (bufsize<offset || memcmp(buf+offset-6,"(c)CRI",6)) return 0;
    return offset;
}

/* return data offset or 6 */
static int adx_decode_header(AVCodecContext *avctx,const unsigned char *buf,size_t bufsize)
{
    int offset;
    int channels,freq,size;

    offset = is_adx(buf,bufsize);
    if (offset==0) return 0;

    channels = buf[7];
    freq = read_long(buf+8);
    size = read_long(buf+12);

//    printf("freq=%d ch=%d\n",freq,channels);

    avctx->sample_rate = freq;
    avctx->channels = channels;
    avctx->bit_rate = freq*channels*18*8/32;
//    avctx->frame_size = 18*channels;

    return offset;
}

static int adx_decode_init(AVCodecContext * avctx)
{
    ADXContext *c = avctx->priv_data;

//    printf("adx_decode_init\n"); fflush(stdout);
    c->prev[0].s1 = 0;
    c->prev[0].s2 = 0;
    c->prev[1].s1 = 0;
    c->prev[1].s2 = 0;
    c->header_parsed = 0;
    c->in_temp = 0;
    return 0;
}

#if 0
static void dump(unsigned char *buf,size_t len)
{
    int i;
    for(i=0;i<len;i++) {
        if ((i&15)==0) av_log(NULL, AV_LOG_DEBUG, "%04x  ",i);
        av_log(NULL, AV_LOG_DEBUG, "%02x ",buf[i]);
        if ((i&15)==15) av_log(NULL, AV_LOG_DEBUG, "\n");
    }
    av_log(NULL, AV_LOG_ERROR, "\n");
}
#endif

static int adx_decode_frame(AVCodecContext *avctx,
                void *data, int *data_size,
                uint8_t *buf0, int buf_size)
{
    ADXContext *c = avctx->priv_data;
    short *samples = data;
    const uint8_t *buf = buf0;
    int rest = buf_size;

    if (!c->header_parsed) {
        int hdrsize = adx_decode_header(avctx,buf,rest);
        if (hdrsize==0) return -1;
        c->header_parsed = 1;
        buf  += hdrsize;
        rest -= hdrsize;
    }

    if (c->in_temp) {
        int copysize = 18*avctx->channels - c->in_temp;
        memcpy(c->dec_temp+c->in_temp,buf,copysize);
        rest -= copysize;
        buf  += copysize;
        if (avctx->channels==1) {
            adx_decode(samples,c->dec_temp,c->prev);
            samples += 32;
        } else {
            adx_decode_stereo(samples,c->dec_temp,c->prev);
            samples += 32*2;
        }
    }
    //
    if (avctx->channels==1) {
        while(rest>=18) {
            adx_decode(samples,buf,c->prev);
            rest-=18;
            buf+=18;
            samples+=32;
        }
    } else {
        while(rest>=18*2) {
            adx_decode_stereo(samples,buf,c->prev);
            rest-=18*2;
            buf+=18*2;
            samples+=32*2;
        }
    }
    //
    c->in_temp = rest;
    if (rest) {
        memcpy(c->dec_temp,buf,rest);
        buf+=rest;
    }
    *data_size = (uint8_t*)samples - (uint8_t*)data;
//    printf("%d:%d ",buf-buf0,*data_size); fflush(stdout);
    return buf-buf0;
}

#ifdef CONFIG_ENCODERS
AVCodec adx_adpcm_encoder = {
    "adx_adpcm",
    CODEC_TYPE_AUDIO,
    CODEC_ID_ADPCM_ADX,
    sizeof(ADXContext),
    adx_encode_init,
    adx_encode_frame,
    adx_encode_close,
    NULL,
};
#endif //CONFIG_ENCODERS

AVCodec adx_adpcm_decoder = {
    "adx_adpcm",
    CODEC_TYPE_AUDIO,
    CODEC_ID_ADPCM_ADX,
    sizeof(ADXContext),
    adx_decode_init,
    NULL,
    NULL,
    adx_decode_frame,
};

