/*
 * CD-ROM XA ADPCM codecs
 * Copyright (c) 1999,2003 BERO
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
 * @file xa.c
 * CD-ROM XA ADPCM codecs.
 *
 * Reference documents:
 * http://ku-www.ss.titech.ac.jp/~yatsushi/xaadpcm.html
 * vagpack & depack http://homepages.compuserve.de/bITmASTER32/psx-index.html
 * readstr http://www.geocities.co.jp/Playtown/2004/
 */

typedef struct {
	int s1,s2;
} PREV;

typedef struct {
	PREV prev[2];
} XAContext;

#define	CLIP(s)		if (s<-32768) s=-32768; else if (s>32767) s=32767

static void xa_decode(short *out,const unsigned char *in,PREV *prev,int inc)
{
	const static int f[5][2] = {
		{0,0},
		{60,0},
		{115,-52},
		{98,-55},
		{122,-60}
	};

	int i,j;

  for(i=0;i<4;i++) {
	int shift,filter,f0,f1;
	int s_1,s_2;

	shift  = 12 - (in[4+i*2] & 15);
	filter = in[4+i*2] >> 4;
	f0 = f[filter][0];
	f1 = f[filter][1];

	s_1 = prev->s1;
	s_2 = prev->s2;

	for(j=0;j<28;j++) {
		int d,s,t;
		d = in[16+i+j*4];

		//t = d&0xf;
		//if (t>=8) t-=16;
		t = (signed char)(d<<4)>>4;
		s = ( t<<shift ) + ((s_1*f0 + s_2*f1+32)>>6);
		CLIP(s);
		*out = s;
		out += inc;
		s_2 = s_1;
		s_1 = s;
	}

	if (inc==2) { /* stereo */
		prev->s1 = s_1;
		prev->s2 = s_2;
		s_1 = prev[1].s1;
		s_2 = prev[1].s2;
		out = out + 1 - 28*2;
	}

	shift  = 12 - (in[5+i*2] & 15);
	filter = in[5+i*2] >> 4;
	f0 = f[filter][0];
	f1 = f[filter][1];

	for(j=0;j<28;j++) {
		int d,s,t;
		d = in[16+i+j*4];

		//t = d>>4;
		//if (t>=8) t-=16;
		t = (signed char)d >> 4;
		s = ( t<<shift ) + ((s_1*f0 + s_2*f1+32)>>6);
		CLIP(s);
		*out = s;
		out += inc;
		s_2 = s_1;
		s_1 = s;
	}

	if (inc==2) { /* stereo */
		prev[1].s1 = s_1;
		prev[1].s2 = s_2;
		out -= 1;
	} else {
		prev[0].s1 = s_1;
		prev[0].s2 = s_2;
	}

  }

}

static int xa_decode_init(AVCodecContext * avctx)
{
	XAContext *c = avctx->priv_data;

	printf("xa init %d\n",avctx->channels);

	c->prev[0].s1 = 0;
	c->prev[0].s2 = 0;
	c->prev[1].s1 = 0;
	c->prev[1].s2 = 0;
	switch(avctx->channels) {
	case 1:
	case 2:
		return 0;
	default:
		return -1;
	}
}

static int xa_decode_frame(AVCodecContext *avctx,
			    void *data, int *data_size,
			    const uint8_t *buf0, int buf_size)
{
	XAContext *c = avctx->priv_data;
	short *samples = data;
	const uint8_t *buf = buf0;

//	printf("xa decode %d\n",buf_size);

	c->prev[0].s1 = 0;
	c->prev[0].s2 = 0;
	c->prev[1].s1 = 0;
	c->prev[1].s2 = 0;

	while(buf_size>=128) {
		xa_decode(samples,buf,c->prev,avctx->channels);
		buf+=128;
		samples += 28*8;
		buf_size-=128;
	}
	*data_size = (char*)samples - (char*)data;
	return buf-buf0;
}

#define DEFINE_ENCODER(id, name,context)                \
AVCodec name ## _encoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    sizeof(context),                       \
    name ## _encode_init,                       \
    name ## _encode_frame,                      \
    name ## _encode_close,                      \
    NULL,                                       \
};

#define DEFINE_DECODER(id, name,context)                \
AVCodec name ## _decoder = {                    \
    #name,                                      \
    CODEC_TYPE_AUDIO,                           \
    id,                                         \
    sizeof(context),                       \
    name ## _decode_init,                       \
    NULL,                                       \
    NULL,                                       \
    name ## _decode_frame,                      \
};

DEFINE_DECODER(CODEC_ID_ADPCM_XA,xa,XAContext)
