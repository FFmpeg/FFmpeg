/*
 * MPEG Audio decoder
 * Copyright (c) 2001 Gerard Lantau.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "avcodec.h"
#include "mpglib/mpg123.h"

/*
 * TODO: 
 *  - add free format
 *  - do not rely anymore on mpglib (first step: implement dct64 and decoding filter)
 */

#define HEADER_SIZE 4
#define BACKSTEP_SIZE 512

typedef struct MPADecodeContext {
    struct mpstr mpstr;
    UINT8 inbuf1[2][MAXFRAMESIZE + BACKSTEP_SIZE];	/* input buffer */
    int inbuf_index;
    UINT8 *inbuf_ptr, *inbuf;
    int frame_size;
    int error_protection;
    int layer;
    int sample_rate;
    int bit_rate;
    int old_frame_size;
    GetBitContext gb;
} MPADecodeContext;

/* XXX: suppress that mess */
struct mpstr *gmp;
GetBitContext *gmp_gb;
static MPADecodeContext *gmp_s;

/* XXX: merge constants with encoder */
static const unsigned short mp_bitrate_tab[2][3][15] = {
    { {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448 },
      {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384 },
      {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320 } },
    { {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
      {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160}
    }
};

static unsigned short mp_freq_tab[3] = { 44100, 48000, 32000 };

static int decode_init(AVCodecContext * avctx)
{
    MPADecodeContext *s = avctx->priv_data;
    struct mpstr *mp = &s->mpstr;
    static int init;

    mp->fr.single = -1;
    mp->synth_bo = 1;

    if(!init) {
        init = 1;
        make_decode_tables(32767);
        init_layer2();
        init_layer3(SBLIMIT);
    }

    s->inbuf_index = 0;
    s->inbuf = &s->inbuf1[s->inbuf_index][BACKSTEP_SIZE];
    s->inbuf_ptr = s->inbuf;
    
    return 0;
}

/* fast header check for resync */
static int check_header(UINT32 header)
{
    /* header */
    if ((header & 0xffe00000) != 0xffe00000)
	return -1;
    /* layer check */
    if (((header >> 17) & 3) == 0)
	return -1;
    /* bit rate : currently no free format supported */
    if (((header >> 12) & 0xf) == 0xf ||
        ((header >> 12) & 0xf) == 0x0)
	return -1;
    /* frequency */
    if (((header >> 10) & 3) == 3)
	return -1;
    return 0;
}

/* header decoding. MUST check the header before because no
   consistency check is done there */
static void decode_header(MPADecodeContext *s, UINT32 header)
{
    struct frame *fr = &s->mpstr.fr;
    int sample_rate, frame_size;

    if (header & (1<<20)) {
        fr->lsf = (header & (1<<19)) ? 0 : 1;
        fr->mpeg25 = 0;
    } else {
        fr->lsf = 1;
        fr->mpeg25 = 1;
    }
    
    s->layer = 4 - ((header >> 17) & 3);
    /* extract frequency */
    fr->sampling_frequency = ((header >> 10) & 3);
    sample_rate = mp_freq_tab[fr->sampling_frequency] >> (fr->lsf + fr->mpeg25);
    fr->sampling_frequency += 3 * (fr->lsf + fr->mpeg25);

    s->error_protection = ((header>>16) & 1) ^ 1;

    fr->bitrate_index = ((header>>12)&0xf);
    fr->padding   = ((header>>9)&0x1);
    fr->extension = ((header>>8)&0x1);
    fr->mode      = ((header>>6)&0x3);
    fr->mode_ext  = ((header>>4)&0x3);
    fr->copyright = ((header>>3)&0x1);
    fr->original  = ((header>>2)&0x1);
    fr->emphasis  = header & 0x3;

    fr->stereo    = (fr->mode == MPG_MD_MONO) ? 1 : 2;

    
    frame_size = mp_bitrate_tab[fr->lsf][s->layer - 1][fr->bitrate_index];
    s->bit_rate = frame_size * 1000;
    switch(s->layer) {
    case 1:
        frame_size = (frame_size * 12000) / sample_rate;
        frame_size = ((frame_size + fr->padding) << 2);
        break;
    case 2:
        frame_size = (frame_size * 144000) / sample_rate;
        frame_size += fr->padding;
        break;
    case 3:
        frame_size = (frame_size * 144000) / (sample_rate << fr->lsf);
        frame_size += fr->padding;
        break;
    }
    s->frame_size = frame_size;
    s->sample_rate = sample_rate;

#if 0
    printf("layer%d, %d Hz, %d kbits/s, %s\n",
           s->layer, s->sample_rate, s->bit_rate, fr->stereo ? "stereo" : "mono");
#endif
}

static int mp_decode_frame(MPADecodeContext *s, 
                           short *samples)
{
    int nb_bytes;
    
    init_get_bits(&s->gb, s->inbuf + HEADER_SIZE, s->inbuf_ptr - s->inbuf - HEADER_SIZE);
    
    /* skip error protection field */
    if (s->error_protection)
        get_bits(&s->gb, 16);

    /* XXX: horrible: global! */
    gmp = &s->mpstr;
    gmp_s = s;
    gmp_gb = &s->gb;

    nb_bytes = 0;
    switch(s->layer) {
    case 1:
        do_layer1(&s->mpstr.fr,(unsigned char *)samples, &nb_bytes);
        break;
    case 2:
        do_layer2(&s->mpstr.fr,(unsigned char *)samples, &nb_bytes);
        break;
    case 3:
        do_layer3(&s->mpstr.fr,(unsigned char *)samples, &nb_bytes);
        s->inbuf_index ^= 1;
        s->inbuf = &s->inbuf1[s->inbuf_index][BACKSTEP_SIZE];
        s->old_frame_size = s->frame_size;
        break;
    default:
        break;
    }
    return nb_bytes;
}

/*
 * seek back in the stream for backstep bytes (at most 511 bytes, and
 * at most in last frame). Note that this is slightly incorrect (data
 * can span more than one block!)  
 */
int set_pointer(long backstep)
{
    UINT8 *ptr;

    /* compute current position in stream */
    ptr = gmp_gb->buf_ptr - (gmp_gb->bit_cnt >> 3);
    /* copy old data before current one */
    ptr -= backstep;
    memcpy(ptr, gmp_s->inbuf1[gmp_s->inbuf_index ^ 1] + 
           BACKSTEP_SIZE + gmp_s->old_frame_size - backstep, backstep);
    /* init get bits again */
    init_get_bits(gmp_gb, ptr, gmp_s->frame_size + backstep);

    return 0;
}

static int decode_frame(AVCodecContext * avctx,
			void *data, int *data_size,
			UINT8 * buf, int buf_size)
{
    MPADecodeContext *s = avctx->priv_data;
    UINT32 header;
    UINT8 *buf_ptr;
    int len, out_size;
    short *out_samples = data;

    *data_size = 0;
    buf_ptr = buf;
    while (buf_size > 0) {
	len = s->inbuf_ptr - s->inbuf;
	if (s->frame_size == 0) {
	    /* no header seen : find one. We need at least 7 bytes to parse it */
	    len = HEADER_SIZE - len;
	    if (len > buf_size)
		len = buf_size;
	    memcpy(s->inbuf_ptr, buf_ptr, len);
	    buf_ptr += len;
	    s->inbuf_ptr += len;
	    buf_size -= len;
	    if ((s->inbuf_ptr - s->inbuf) == HEADER_SIZE) {
		header = (s->inbuf[0] << 24) | (s->inbuf[1] << 16) |
		    (s->inbuf[2] << 8) | s->inbuf[3];
		if (check_header(header) < 0) {
		    /* no sync found : move by one byte (inefficient, but simple!) */
		    memcpy(s->inbuf, s->inbuf + 1, HEADER_SIZE - 1);
		    s->inbuf_ptr--;
		} else {
		    decode_header(s, header);
		    /* update codec info */
		    avctx->sample_rate = s->sample_rate;
                    avctx->channels = s->mpstr.fr.stereo ? 2 : 1;
		    avctx->bit_rate = s->bit_rate;
		}
	    }
	} else if (len < s->frame_size) {
	    len = s->frame_size - len;
	    if (len > buf_size)
		len = buf_size;

	    memcpy(s->inbuf_ptr, buf_ptr, len);
	    buf_ptr += len;
	    s->inbuf_ptr += len;
	    buf_size -= len;
	} else {
            out_size = mp_decode_frame(s, out_samples);
	    s->inbuf_ptr = s->inbuf;
	    s->frame_size = 0;
	    *data_size = out_size;
	    break;
	}
    }
    return buf_ptr - buf;
}

AVCodec mp3_decoder =
{
    "mpegaudio",
    CODEC_TYPE_AUDIO,
    CODEC_ID_MP2,
    sizeof(MPADecodeContext),
    decode_init,
    NULL,
    NULL,
    decode_frame,
};
