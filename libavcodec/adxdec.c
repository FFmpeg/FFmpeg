/*
 * ADX ADPCM codecs
 * Copyright (c) 2001,2003 BERO
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
#include "avcodec.h"
#include "adx.h"

/**
 * @file
 * SEGA CRI adx codecs.
 *
 * Reference documents:
 * http://ku-www.ss.titech.ac.jp/~yatsushi/adx.html
 * adx2wav & wav2adx http://www.geocities.co.jp/Playtown/2004/
 */

static av_cold int adx_decode_init(AVCodecContext *avctx)
{
    avctx->sample_fmt = AV_SAMPLE_FMT_S16;
    return 0;
}

/* 18 bytes <-> 32 samples */

static void adx_decode(short *out,const unsigned char *in,PREV *prev)
{
    int scale = AV_RB16(in);
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
        s2 = s1;
        s1 = av_clip_int16(s0);
        *out++=s1;

        d = in[i];
        //d&=15; if (d&8) d-=16;
        d = ((signed char)(d<<4) >> 4);
        s0 = (BASEVOL*d*scale + SCALE1*s1 - SCALE2*s2)>>14;
        s2 = s1;
        s1 = av_clip_int16(s0);
        *out++=s1;
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

/* return data offset or 0 */
static int adx_decode_header(AVCodecContext *avctx,const unsigned char *buf,size_t bufsize)
{
    int offset;

    if (buf[0]!=0x80) return 0;
    offset = (AV_RB32(buf)^0x80000000)+4;
    if (bufsize<offset || memcmp(buf+offset-6,"(c)CRI",6)) return 0;

    avctx->channels    = buf[7];
    avctx->sample_rate = AV_RB32(buf+8);
    avctx->bit_rate    = avctx->sample_rate*avctx->channels*18*8/32;

    return offset;
}

static int adx_decode_frame(AVCodecContext *avctx,
                void *data, int *data_size,
                AVPacket *avpkt)
{
    const uint8_t *buf0 = avpkt->data;
    int buf_size = avpkt->size;
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

    /* 18 bytes of data are expanded into 32*2 bytes of audio,
       so guard against buffer overflows */
    if(rest/18 > *data_size/64)
        rest = (*data_size/64) * 18;

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

AVCodec adpcm_adx_decoder = {
    "adpcm_adx",
    AVMEDIA_TYPE_AUDIO,
    CODEC_ID_ADPCM_ADX,
    sizeof(ADXContext),
    adx_decode_init,
    NULL,
    NULL,
    adx_decode_frame,
    .long_name = NULL_IF_CONFIG_SMALL("SEGA CRI ADX ADPCM"),
};

