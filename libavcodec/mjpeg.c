/*
 * MJPEG encoder and decoder
 * Copyright (c) 2000, 2001 Fabrice Bellard.
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
 *
 * Support for external huffman table, various fixes (AVID workaround),
 * aspecting, new decode_frame mechanism and apple mjpeg-b support
 *                                  by Alex Beregszaszi <alex@naxine.org>
 */

/**
 * @file mjpeg.c
 * MJPEG encoder and decoder.
 */
 
//#define DEBUG
#include <assert.h>

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

/* use two quantizer tables (one for luminance and one for chrominance) */
/* not yet working */
#undef TWOMATRIXES

typedef struct MJpegContext {
    uint8_t huff_size_dc_luminance[12]; //FIXME use array [3] instead of lumi / chrom, for easier addressing 
    uint16_t huff_code_dc_luminance[12];
    uint8_t huff_size_dc_chrominance[12];
    uint16_t huff_code_dc_chrominance[12];

    uint8_t huff_size_ac_luminance[256];
    uint16_t huff_code_ac_luminance[256];
    uint8_t huff_size_ac_chrominance[256];
    uint16_t huff_code_ac_chrominance[256];
} MJpegContext;

/* JPEG marker codes */
typedef enum {
    /* start of frame */
    SOF0  = 0xc0,	/* baseline */
    SOF1  = 0xc1,	/* extended sequential, huffman */
    SOF2  = 0xc2,	/* progressive, huffman */
    SOF3  = 0xc3,	/* lossless, huffman */

    SOF5  = 0xc5,	/* differential sequential, huffman */
    SOF6  = 0xc6,	/* differential progressive, huffman */
    SOF7  = 0xc7,	/* differential lossless, huffman */
    JPG   = 0xc8,	/* reserved for JPEG extension */
    SOF9  = 0xc9,	/* extended sequential, arithmetic */
    SOF10 = 0xca,	/* progressive, arithmetic */
    SOF11 = 0xcb,	/* lossless, arithmetic */

    SOF13 = 0xcd,	/* differential sequential, arithmetic */
    SOF14 = 0xce,	/* differential progressive, arithmetic */
    SOF15 = 0xcf,	/* differential lossless, arithmetic */

    DHT   = 0xc4,	/* define huffman tables */

    DAC   = 0xcc,	/* define arithmetic-coding conditioning */

    /* restart with modulo 8 count "m" */
    RST0  = 0xd0,
    RST1  = 0xd1,
    RST2  = 0xd2,
    RST3  = 0xd3,
    RST4  = 0xd4,
    RST5  = 0xd5,
    RST6  = 0xd6,
    RST7  = 0xd7,

    SOI   = 0xd8,	/* start of image */
    EOI   = 0xd9,	/* end of image */
    SOS   = 0xda,	/* start of scan */
    DQT   = 0xdb,	/* define quantization tables */
    DNL   = 0xdc,	/* define number of lines */
    DRI   = 0xdd,	/* define restart interval */
    DHP   = 0xde,	/* define hierarchical progression */
    EXP   = 0xdf,	/* expand reference components */

    APP0  = 0xe0,
    APP1  = 0xe1,
    APP2  = 0xe2,
    APP3  = 0xe3,
    APP4  = 0xe4,
    APP5  = 0xe5,
    APP6  = 0xe6,
    APP7  = 0xe7,
    APP8  = 0xe8,
    APP9  = 0xe9,
    APP10 = 0xea,
    APP11 = 0xeb,
    APP12 = 0xec,
    APP13 = 0xed,
    APP14 = 0xee,
    APP15 = 0xef,

    JPG0  = 0xf0,
    JPG1  = 0xf1,
    JPG2  = 0xf2,
    JPG3  = 0xf3,
    JPG4  = 0xf4,
    JPG5  = 0xf5,
    JPG6  = 0xf6,
    JPG7  = 0xf7,
    JPG8  = 0xf8,
    JPG9  = 0xf9,
    JPG10 = 0xfa,
    JPG11 = 0xfb,
    JPG12 = 0xfc,
    JPG13 = 0xfd,

    COM   = 0xfe,	/* comment */

    TEM   = 0x01,	/* temporary private use for arithmetic coding */

    /* 0x02 -> 0xbf reserved */
} JPEG_MARKER;

#if 0
/* These are the sample quantization tables given in JPEG spec section K.1.
 * The spec says that the values given produce "good" quality, and
 * when divided by 2, "very good" quality.
 */
static const unsigned char std_luminance_quant_tbl[64] = {
    16,  11,  10,  16,  24,  40,  51,  61,
    12,  12,  14,  19,  26,  58,  60,  55,
    14,  13,  16,  24,  40,  57,  69,  56,
    14,  17,  22,  29,  51,  87,  80,  62,
    18,  22,  37,  56,  68, 109, 103,  77,
    24,  35,  55,  64,  81, 104, 113,  92,
    49,  64,  78,  87, 103, 121, 120, 101,
    72,  92,  95,  98, 112, 100, 103,  99
};
static const unsigned char std_chrominance_quant_tbl[64] = {
    17,  18,  24,  47,  99,  99,  99,  99,
    18,  21,  26,  66,  99,  99,  99,  99,
    24,  26,  56,  99,  99,  99,  99,  99,
    47,  66,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99,
    99,  99,  99,  99,  99,  99,  99,  99
};
#endif

/* Set up the standard Huffman tables (cf. JPEG standard section K.3) */
/* IMPORTANT: these are only valid for 8-bit data precision! */
static const uint8_t bits_dc_luminance[17] =
{ /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const uint8_t val_dc_luminance[] =
{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static const uint8_t bits_dc_chrominance[17] =
{ /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const uint8_t val_dc_chrominance[] =
{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static const uint8_t bits_ac_luminance[17] =
{ /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const uint8_t val_ac_luminance[] =
{ 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12,
  0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
  0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08,
  0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
  0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16,
  0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
  0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39,
  0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
  0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
  0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
  0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79,
  0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
  0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98,
  0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
  0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
  0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
  0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
  0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
  0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea,
  0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa 
};

static const uint8_t bits_ac_chrominance[17] =
{ /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };

static const uint8_t val_ac_chrominance[] =
{ 0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21,
  0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
  0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
  0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
  0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34,
  0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
  0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38,
  0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
  0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
  0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
  0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78,
  0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
  0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96,
  0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
  0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
  0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
  0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2,
  0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
  0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9,
  0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
  0xf9, 0xfa 
};

/* isn't this function nicer than the one in the libjpeg ? */
static void build_huffman_codes(uint8_t *huff_size, uint16_t *huff_code,
                                const uint8_t *bits_table, const uint8_t *val_table)
{
    int i, j, k,nb, code, sym;

    code = 0;
    k = 0;
    for(i=1;i<=16;i++) {
        nb = bits_table[i];
        for(j=0;j<nb;j++) {
            sym = val_table[k++];
            huff_size[sym] = i;
            huff_code[sym] = code;
            code++;
        }
        code <<= 1;
    }
}

int mjpeg_init(MpegEncContext *s)
{
    MJpegContext *m;
    
    m = av_malloc(sizeof(MJpegContext));
    if (!m)
        return -1;
    
    s->min_qcoeff=-1023;
    s->max_qcoeff= 1023;

    /* build all the huffman tables */
    build_huffman_codes(m->huff_size_dc_luminance,
                        m->huff_code_dc_luminance,
                        bits_dc_luminance,
                        val_dc_luminance);
    build_huffman_codes(m->huff_size_dc_chrominance,
                        m->huff_code_dc_chrominance,
                        bits_dc_chrominance,
                        val_dc_chrominance);
    build_huffman_codes(m->huff_size_ac_luminance,
                        m->huff_code_ac_luminance,
                        bits_ac_luminance,
                        val_ac_luminance);
    build_huffman_codes(m->huff_size_ac_chrominance,
                        m->huff_code_ac_chrominance,
                        bits_ac_chrominance,
                        val_ac_chrominance);
    
    s->mjpeg_ctx = m;
    return 0;
}

void mjpeg_close(MpegEncContext *s)
{
    av_free(s->mjpeg_ctx);
}

#define PREDICT(ret, topleft, top, left, predictor)\
    switch(predictor){\
        case 1: ret= left; break;\
        case 2: ret= top; break;\
        case 3: ret= topleft; break;\
        case 4: ret= left   +   top - topleft; break;\
        case 5: ret= left   + ((top - topleft)>>1); break;\
        case 6: ret= top + ((left   - topleft)>>1); break;\
        default:\
        case 7: ret= (left + top)>>1; break;\
    }

#ifdef CONFIG_ENCODERS
static inline void put_marker(PutBitContext *p, int code)
{
    put_bits(p, 8, 0xff);
    put_bits(p, 8, code);
}

/* table_class: 0 = DC coef, 1 = AC coefs */
static int put_huffman_table(MpegEncContext *s, int table_class, int table_id,
                             const uint8_t *bits_table, const uint8_t *value_table)
{
    PutBitContext *p = &s->pb;
    int n, i;

    put_bits(p, 4, table_class);
    put_bits(p, 4, table_id);

    n = 0;
    for(i=1;i<=16;i++) {
        n += bits_table[i];
        put_bits(p, 8, bits_table[i]);
    }

    for(i=0;i<n;i++)
        put_bits(p, 8, value_table[i]);

    return n + 17;
}

static void jpeg_table_header(MpegEncContext *s)
{
    PutBitContext *p = &s->pb;
    int i, j, size;
    uint8_t *ptr;

    /* quant matrixes */
    put_marker(p, DQT);
#ifdef TWOMATRIXES
    put_bits(p, 16, 2 + 2 * (1 + 64));
#else
    put_bits(p, 16, 2 + 1 * (1 + 64));
#endif
    put_bits(p, 4, 0); /* 8 bit precision */
    put_bits(p, 4, 0); /* table 0 */
    for(i=0;i<64;i++) {
        j = s->intra_scantable.permutated[i];
        put_bits(p, 8, s->intra_matrix[j]);
    }
#ifdef TWOMATRIXES
    put_bits(p, 4, 0); /* 8 bit precision */
    put_bits(p, 4, 1); /* table 1 */
    for(i=0;i<64;i++) {
        j = s->intra_scantable.permutated[i];
        put_bits(p, 8, s->chroma_intra_matrix[j]);
    }
#endif

    /* huffman table */
    put_marker(p, DHT);
    flush_put_bits(p);
    ptr = pbBufPtr(p);
    put_bits(p, 16, 0); /* patched later */
    size = 2;
    size += put_huffman_table(s, 0, 0, bits_dc_luminance, val_dc_luminance);
    size += put_huffman_table(s, 0, 1, bits_dc_chrominance, val_dc_chrominance);
    
    size += put_huffman_table(s, 1, 0, bits_ac_luminance, val_ac_luminance);
    size += put_huffman_table(s, 1, 1, bits_ac_chrominance, val_ac_chrominance);
    ptr[0] = size >> 8;
    ptr[1] = size;
}

static void jpeg_put_comments(MpegEncContext *s)
{
    PutBitContext *p = &s->pb;
    int size;
    uint8_t *ptr;

    if (s->aspect_ratio_info /* && !lossless */)
    {
    /* JFIF header */
    put_marker(p, APP0);
    put_bits(p, 16, 16);
    put_string(p, "JFIF"); /* this puts the trailing zero-byte too */
    put_bits(p, 16, 0x0201); /* v 1.02 */
    put_bits(p, 8, 0); /* units type: 0 - aspect ratio */
    switch(s->aspect_ratio_info)
    {
	case FF_ASPECT_4_3_625:
	case FF_ASPECT_4_3_525:
	    put_bits(p, 16, 4); 
	    put_bits(p, 16, 3);
	    break;
	case FF_ASPECT_16_9_625:
	case FF_ASPECT_16_9_525:
	    put_bits(p, 16, 16); 
	    put_bits(p, 16, 9);
	    break;
	case FF_ASPECT_EXTENDED:
	    put_bits(p, 16, s->aspected_width);
	    put_bits(p, 16, s->aspected_height);
	    break;
	case FF_ASPECT_SQUARE:
	default:
	    put_bits(p, 16, 1); /* aspect: 1:1 */
	    put_bits(p, 16, 1);
	    break;
    }
    put_bits(p, 8, 0); /* thumbnail width */
    put_bits(p, 8, 0); /* thumbnail height */
    }

    /* comment */
    if(!(s->flags & CODEC_FLAG_BITEXACT)){
        put_marker(p, COM);
        flush_put_bits(p);
        ptr = pbBufPtr(p);
        put_bits(p, 16, 0); /* patched later */
        put_string(p, LIBAVCODEC_IDENT);
        size = strlen(LIBAVCODEC_IDENT)+3;
        ptr[0] = size >> 8;
        ptr[1] = size;
    }
}

void mjpeg_picture_header(MpegEncContext *s)
{
    const int lossless= s->avctx->codec_id == CODEC_ID_LJPEG;

    put_marker(&s->pb, SOI);

    if (!s->mjpeg_data_only_frames)
    {
    jpeg_put_comments(s);    

    if (s->mjpeg_write_tables) jpeg_table_header(s);

    put_marker(&s->pb, lossless ? SOF3 : SOF0);

    put_bits(&s->pb, 16, 17);
    if(lossless && s->avctx->pix_fmt == PIX_FMT_RGBA32)
        put_bits(&s->pb, 8, 9); /* 9 bits/component RCT */
    else
        put_bits(&s->pb, 8, 8); /* 8 bits/component */
    put_bits(&s->pb, 16, s->height);
    put_bits(&s->pb, 16, s->width);
    put_bits(&s->pb, 8, 3); /* 3 components */
    
    /* Y component */
    put_bits(&s->pb, 8, 1); /* component number */
    put_bits(&s->pb, 4, s->mjpeg_hsample[0]); /* H factor */
    put_bits(&s->pb, 4, s->mjpeg_vsample[0]); /* V factor */
    put_bits(&s->pb, 8, 0); /* select matrix */
    
    /* Cb component */
    put_bits(&s->pb, 8, 2); /* component number */
    put_bits(&s->pb, 4, s->mjpeg_hsample[1]); /* H factor */
    put_bits(&s->pb, 4, s->mjpeg_vsample[1]); /* V factor */
#ifdef TWOMATRIXES
    put_bits(&s->pb, 8, lossless ? 0 : 1); /* select matrix */
#else
    put_bits(&s->pb, 8, 0); /* select matrix */
#endif

    /* Cr component */
    put_bits(&s->pb, 8, 3); /* component number */
    put_bits(&s->pb, 4, s->mjpeg_hsample[2]); /* H factor */
    put_bits(&s->pb, 4, s->mjpeg_vsample[2]); /* V factor */
#ifdef TWOMATRIXES
    put_bits(&s->pb, 8, lossless ? 0 : 1); /* select matrix */
#else
    put_bits(&s->pb, 8, 0); /* select matrix */
#endif
    }

    /* scan header */
    put_marker(&s->pb, SOS);
    put_bits(&s->pb, 16, 12); /* length */
    put_bits(&s->pb, 8, 3); /* 3 components */
    
    /* Y component */
    put_bits(&s->pb, 8, 1); /* index */
    put_bits(&s->pb, 4, 0); /* DC huffman table index */
    put_bits(&s->pb, 4, 0); /* AC huffman table index */
    
    /* Cb component */
    put_bits(&s->pb, 8, 2); /* index */
    put_bits(&s->pb, 4, 1); /* DC huffman table index */
    put_bits(&s->pb, 4, lossless ? 0 : 1); /* AC huffman table index */
    
    /* Cr component */
    put_bits(&s->pb, 8, 3); /* index */
    put_bits(&s->pb, 4, 1); /* DC huffman table index */
    put_bits(&s->pb, 4, lossless ? 0 : 1); /* AC huffman table index */

    put_bits(&s->pb, 8, lossless ? s->avctx->prediction_method+1 : 0); /* Ss (not used) */
    put_bits(&s->pb, 8, lossless ? 0 : 63); /* Se (not used) */
    put_bits(&s->pb, 8, 0); /* Ah/Al (not used) */
}

static void escape_FF(MpegEncContext *s, int start)
{
    int size= get_bit_count(&s->pb) - start*8;
    int i, ff_count;
    uint8_t *buf= s->pb.buf + start;
    int align= (-(size_t)(buf))&3;
    
    assert((size&7) == 0);
    size >>= 3;
    
    ff_count=0;
    for(i=0; i<size && i<align; i++){
        if(buf[i]==0xFF) ff_count++;
    }
    for(; i<size-15; i+=16){
        int acc, v;

        v= *(uint32_t*)(&buf[i]);
        acc= (((v & (v>>4))&0x0F0F0F0F)+0x01010101)&0x10101010;
        v= *(uint32_t*)(&buf[i+4]);
        acc+=(((v & (v>>4))&0x0F0F0F0F)+0x01010101)&0x10101010;
        v= *(uint32_t*)(&buf[i+8]);
        acc+=(((v & (v>>4))&0x0F0F0F0F)+0x01010101)&0x10101010;
        v= *(uint32_t*)(&buf[i+12]);
        acc+=(((v & (v>>4))&0x0F0F0F0F)+0x01010101)&0x10101010;

        acc>>=4;
        acc+= (acc>>16);
        acc+= (acc>>8);
        ff_count+= acc&0xFF;
    }
    for(; i<size; i++){
        if(buf[i]==0xFF) ff_count++;
    }

    if(ff_count==0) return;
    
    /* skip put bits */
    for(i=0; i<ff_count-3; i+=4)
        put_bits(&s->pb, 32, 0);
    put_bits(&s->pb, (ff_count-i)*8, 0);
    flush_put_bits(&s->pb); 

    for(i=size-1; ff_count; i--){
        int v= buf[i];

        if(v==0xFF){
//printf("%d %d\n", i, ff_count);
            buf[i+ff_count]= 0;
            ff_count--;
        }

        buf[i+ff_count]= v;
    }
}

void mjpeg_picture_trailer(MpegEncContext *s)
{
    int pad= (-get_bit_count(&s->pb))&7;
    
    put_bits(&s->pb, pad,0xFF>>(8-pad));
    flush_put_bits(&s->pb);

    assert((s->header_bits&7)==0);
    
    escape_FF(s, s->header_bits>>3);

    put_marker(&s->pb, EOI);
}

static inline void mjpeg_encode_dc(MpegEncContext *s, int val,
				   uint8_t *huff_size, uint16_t *huff_code)
{
    int mant, nbits;

    if (val == 0) {
        put_bits(&s->pb, huff_size[0], huff_code[0]);
    } else {
        mant = val;
        if (val < 0) {
            val = -val;
            mant--;
        }
        
        nbits= av_log2_16bit(val) + 1;
            
        put_bits(&s->pb, huff_size[nbits], huff_code[nbits]);
        
        put_bits(&s->pb, nbits, mant & ((1 << nbits) - 1));
    }
}

static void encode_block(MpegEncContext *s, DCTELEM *block, int n)
{
    int mant, nbits, code, i, j;
    int component, dc, run, last_index, val;
    MJpegContext *m = s->mjpeg_ctx;
    uint8_t *huff_size_ac;
    uint16_t *huff_code_ac;
    
    /* DC coef */
    component = (n <= 3 ? 0 : n - 4 + 1);
    dc = block[0]; /* overflow is impossible */
    val = dc - s->last_dc[component];
    if (n < 4) {
        mjpeg_encode_dc(s, val, m->huff_size_dc_luminance, m->huff_code_dc_luminance);
        huff_size_ac = m->huff_size_ac_luminance;
        huff_code_ac = m->huff_code_ac_luminance;
    } else {
        mjpeg_encode_dc(s, val, m->huff_size_dc_chrominance, m->huff_code_dc_chrominance);
        huff_size_ac = m->huff_size_ac_chrominance;
        huff_code_ac = m->huff_code_ac_chrominance;
    }
    s->last_dc[component] = dc;
    
    /* AC coefs */
    
    run = 0;
    last_index = s->block_last_index[n];
    for(i=1;i<=last_index;i++) {
        j = s->intra_scantable.permutated[i];
        val = block[j];
        if (val == 0) {
            run++;
        } else {
            while (run >= 16) {
                put_bits(&s->pb, huff_size_ac[0xf0], huff_code_ac[0xf0]);
                run -= 16;
            }
            mant = val;
            if (val < 0) {
                val = -val;
                mant--;
            }
            
            nbits= av_log2(val) + 1;
            code = (run << 4) | nbits;

            put_bits(&s->pb, huff_size_ac[code], huff_code_ac[code]);
        
            put_bits(&s->pb, nbits, mant & ((1 << nbits) - 1));
            run = 0;
        }
    }

    /* output EOB only if not already 64 values */
    if (last_index < 63 || run != 0)
        put_bits(&s->pb, huff_size_ac[0], huff_code_ac[0]);
}

void mjpeg_encode_mb(MpegEncContext *s, 
                     DCTELEM block[6][64])
{
    int i;
    for(i=0;i<6;i++) {
        encode_block(s, block[i], i);
    }
}

static int encode_picture_lossless(AVCodecContext *avctx, unsigned char *buf, int buf_size, void *data){
    MpegEncContext * const s = avctx->priv_data;
    MJpegContext * const m = s->mjpeg_ctx;
    AVFrame *pict = data;
    const int width= s->width;
    const int height= s->height;
    AVFrame * const p= (AVFrame*)&s->current_picture;
    const int predictor= avctx->prediction_method+1;

    init_put_bits(&s->pb, buf, buf_size, NULL, NULL);

    *p = *pict;
    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;
    
    mjpeg_picture_header(s);

    s->header_bits= get_bit_count(&s->pb);

    if(avctx->pix_fmt == PIX_FMT_RGBA32){
        int x, y, i;
        const int linesize= p->linesize[0];
        uint16_t buffer[2048][4];
        int left[3], top[3], topleft[3];

        for(i=0; i<3; i++){
            buffer[0][i]= 1 << (9 - 1);
        }

        for(y = 0; y < height; y++) {
            const int modified_predictor= y ? predictor : 1;
            uint8_t *ptr = p->data[0] + (linesize * y);

            for(i=0; i<3; i++){
                top[i]= left[i]= topleft[i]= buffer[0][i];
            }
            for(x = 0; x < width; x++) {
                buffer[x][1] = ptr[4*x+0] - ptr[4*x+1] + 0x100;
                buffer[x][2] = ptr[4*x+2] - ptr[4*x+1] + 0x100;
                buffer[x][0] = (ptr[4*x+0] + 2*ptr[4*x+1] + ptr[4*x+2])>>2;

                for(i=0;i<3;i++) {
                    int pred, diff;

                    PREDICT(pred, topleft[i], top[i], left[i], modified_predictor);
                        
                    topleft[i]= top[i];
                    top[i]= buffer[x+1][i];
                    
                    left[i]= buffer[x][i];

                    diff= ((left[i] - pred + 0x100)&0x1FF) - 0x100;
                    
                    if(i==0)
                        mjpeg_encode_dc(s, diff, m->huff_size_dc_luminance, m->huff_code_dc_luminance); //FIXME ugly
                    else
                        mjpeg_encode_dc(s, diff, m->huff_size_dc_chrominance, m->huff_code_dc_chrominance);
                }
            }
        }
    }else{
        int mb_x, mb_y, i;
        const int mb_width  = (width  + s->mjpeg_hsample[0] - 1) / s->mjpeg_hsample[0];
        const int mb_height = (height + s->mjpeg_vsample[0] - 1) / s->mjpeg_vsample[0];
        
        for(mb_y = 0; mb_y < mb_height; mb_y++) {
            for(mb_x = 0; mb_x < mb_width; mb_x++) {
                if(mb_x==0 || mb_y==0){
                    for(i=0;i<3;i++) {
                        uint8_t *ptr;
                        int x, y, h, v, linesize;
                        h = s->mjpeg_hsample[i];
                        v = s->mjpeg_vsample[i];
                        linesize= p->linesize[i];

                        for(y=0; y<v; y++){
                            for(x=0; x<h; x++){
                                int pred;

                                ptr = p->data[i] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
                                if(y==0 && mb_y==0){
                                    if(x==0 && mb_x==0){
                                        pred= 128;
                                    }else{
                                        pred= ptr[-1];
                                    }
                                }else{
                                    if(x==0 && mb_x==0){
                                        pred= ptr[-linesize];
                                    }else{
                                        PREDICT(pred, ptr[-linesize-1], ptr[-linesize], ptr[-1], predictor);
                                    }
                                }
                                
                                if(i==0)
                                    mjpeg_encode_dc(s, (int8_t)(*ptr - pred), m->huff_size_dc_luminance, m->huff_code_dc_luminance); //FIXME ugly
                                else
                                    mjpeg_encode_dc(s, (int8_t)(*ptr - pred), m->huff_size_dc_chrominance, m->huff_code_dc_chrominance);
                            }
                        }
                    }
                }else{
                    for(i=0;i<3;i++) {
                        uint8_t *ptr;
                        int x, y, h, v, linesize;
                        h = s->mjpeg_hsample[i];
                        v = s->mjpeg_vsample[i];
                        linesize= p->linesize[i];
                             
                        for(y=0; y<v; y++){
                            for(x=0; x<h; x++){
                                int pred;

                                ptr = p->data[i] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
//printf("%d %d %d %d %8X\n", mb_x, mb_y, x, y, ptr); 
                                PREDICT(pred, ptr[-linesize-1], ptr[-linesize], ptr[-1], predictor);

                                if(i==0)
                                    mjpeg_encode_dc(s, (int8_t)(*ptr - pred), m->huff_size_dc_luminance, m->huff_code_dc_luminance); //FIXME ugly
                                else
                                    mjpeg_encode_dc(s, (int8_t)(*ptr - pred), m->huff_size_dc_chrominance, m->huff_code_dc_chrominance);
                            }
                        }
                    }
                }
            }
        }
    }

    emms_c();
    
    mjpeg_picture_trailer(s);
    s->picture_number++;

    flush_put_bits(&s->pb);
    return pbBufPtr(&s->pb) - s->pb.buf;
//    return (get_bit_count(&f->pb)+7)/8;
}

#endif //CONFIG_ENCODERS

/******************************************/
/* decoding */

#define MAX_COMPONENTS 4

typedef struct MJpegDecodeContext {
    AVCodecContext *avctx;
    GetBitContext gb;
    int mpeg_enc_ctx_allocated; /* true if decoding context allocated */

    int start_code; /* current start code */
    int buffer_size;
    uint8_t *buffer;

    int16_t quant_matrixes[4][64];
    VLC vlcs[2][4];
    int qscale[4];      ///< quantizer scale calculated from quant_matrixes

    int org_width, org_height;  /* size given at codec init */
    int first_picture;    /* true if decoding first picture */
    int interlaced;     /* true if interlaced */
    int bottom_field;   /* true if bottom field */
    int lossless;
    int rgb;
    int rct;            /* standard rct */  
    int pegasus_rct;    /* pegasus reversible colorspace transform */  
    int bits;           /* bits per component */

    int width, height;
    int mb_width, mb_height;
    int nb_components;
    int component_id[MAX_COMPONENTS];
    int h_count[MAX_COMPONENTS]; /* horizontal and vertical count for each component */
    int v_count[MAX_COMPONENTS];
    int comp_index[MAX_COMPONENTS];
    int dc_index[MAX_COMPONENTS];
    int ac_index[MAX_COMPONENTS];
    int nb_blocks[MAX_COMPONENTS];
    int h_scount[MAX_COMPONENTS];
    int v_scount[MAX_COMPONENTS];
    int h_max, v_max; /* maximum h and v counts */
    int quant_index[4];   /* quant table index for each component */
    int last_dc[MAX_COMPONENTS]; /* last DEQUANTIZED dc (XXX: am I right to do that ?) */
    uint8_t *current_picture[MAX_COMPONENTS]; /* picture structure */
    int linesize[MAX_COMPONENTS];
    uint8_t *qscale_table;
    DCTELEM block[64] __align8;
    ScanTable scantable;
    void (*idct_put)(uint8_t *dest/*align 8*/, int line_size, DCTELEM *block/*align 16*/);

    int restart_interval;
    int restart_count;

    int buggy_avid;
    int interlace_polarity;
} MJpegDecodeContext;

static int mjpeg_decode_dht(MJpegDecodeContext *s);

static int build_vlc(VLC *vlc, const uint8_t *bits_table, const uint8_t *val_table, 
                      int nb_codes)
{
    uint8_t huff_size[256];
    uint16_t huff_code[256];

    memset(huff_size, 0, sizeof(huff_size));
    build_huffman_codes(huff_size, huff_code, bits_table, val_table);
    
    return init_vlc(vlc, 9, nb_codes, huff_size, 1, 1, huff_code, 2, 2);
}

static int mjpeg_decode_init(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;
    MpegEncContext s2;

    s->avctx = avctx;

    /* ugly way to get the idct & scantable FIXME */
    memset(&s2, 0, sizeof(MpegEncContext));
    s2.flags= avctx->flags;
    s2.avctx= avctx;
//    s2->out_format = FMT_MJPEG;
    s2.width = 8;
    s2.height = 8;
    if (MPV_common_init(&s2) < 0)
       return -1;
    s->scantable= s2.intra_scantable;
    s->idct_put= s2.dsp.idct_put;
    MPV_common_end(&s2);

    s->mpeg_enc_ctx_allocated = 0;
    s->buffer_size = 102400; /* smaller buffer should be enough,
				but photojpg files could ahive bigger sizes */
    s->buffer = av_malloc(s->buffer_size);
    if (!s->buffer)
	return -1;
    s->start_code = -1;
    s->first_picture = 1;
    s->org_width = avctx->width;
    s->org_height = avctx->height;
    
    build_vlc(&s->vlcs[0][0], bits_dc_luminance, val_dc_luminance, 12);
    build_vlc(&s->vlcs[0][1], bits_dc_chrominance, val_dc_chrominance, 12);
    build_vlc(&s->vlcs[1][0], bits_ac_luminance, val_ac_luminance, 251);
    build_vlc(&s->vlcs[1][1], bits_ac_chrominance, val_ac_chrominance, 251);

    if (avctx->flags & CODEC_FLAG_EXTERN_HUFF)
    {
	printf("mjpeg: using external huffman table\n");
	init_get_bits(&s->gb, avctx->extradata, avctx->extradata_size*8);
	mjpeg_decode_dht(s);
	/* should check for error - but dunno */
    }

    return 0;
}

/* quantize tables */
static int mjpeg_decode_dqt(MJpegDecodeContext *s)
{
    int len, index, i, j;
    
    len = get_bits(&s->gb, 16) - 2;

    while (len >= 65) {
        /* only 8 bit precision handled */
        if (get_bits(&s->gb, 4) != 0)
	{
	    dprintf("dqt: 16bit precision\n");
            return -1;
	}
        index = get_bits(&s->gb, 4);
        if (index >= 4)
            return -1;
        dprintf("index=%d\n", index);
        /* read quant table */
        for(i=0;i<64;i++) {
            j = s->scantable.permutated[i];
	    s->quant_matrixes[index][j] = get_bits(&s->gb, 8);
        }

        //XXX FIXME finetune, and perhaps add dc too
        s->qscale[index]= FFMAX(
            s->quant_matrixes[index][s->scantable.permutated[1]],
            s->quant_matrixes[index][s->scantable.permutated[8]]) >> 1;
        len -= 65;
    }
    
    return 0;
}

/* decode huffman tables and build VLC decoders */
static int mjpeg_decode_dht(MJpegDecodeContext *s)
{
    int len, index, i, class, n, v, code_max;
    uint8_t bits_table[17];
    uint8_t val_table[256];
    
    len = get_bits(&s->gb, 16) - 2;

    while (len > 0) {
        if (len < 17)
            return -1;
        class = get_bits(&s->gb, 4);
        if (class >= 2)
            return -1;
        index = get_bits(&s->gb, 4);
        if (index >= 4)
            return -1;
        n = 0;
        for(i=1;i<=16;i++) {
            bits_table[i] = get_bits(&s->gb, 8);
            n += bits_table[i];
        }
        len -= 17;
        if (len < n || n > 256)
            return -1;

        code_max = 0;
        for(i=0;i<n;i++) {
            v = get_bits(&s->gb, 8);
            if (v > code_max)
                code_max = v;
            val_table[i] = v;
        }
        len -= n;

        /* build VLC and flush previous vlc if present */
        free_vlc(&s->vlcs[class][index]);
        dprintf("class=%d index=%d nb_codes=%d\n",
               class, index, code_max + 1);
        if(build_vlc(&s->vlcs[class][index], bits_table, val_table, code_max + 1) < 0){
            return -1;
        }
    }
    return 0;
}

static int mjpeg_decode_sof(MJpegDecodeContext *s)
{
    int len, nb_components, i, width, height;

    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    s->bits= get_bits(&s->gb, 8);
    
    if(s->pegasus_rct) s->bits=9;  
    if(s->bits==9 && !s->pegasus_rct) s->rct=1;    //FIXME ugly

    if (s->bits != 8 && !s->lossless){
        printf("only 8 bits/component accepted\n");
        return -1;
    }
    height = get_bits(&s->gb, 16);
    width = get_bits(&s->gb, 16);
    dprintf("sof0: picture: %dx%d\n", width, height);

    nb_components = get_bits(&s->gb, 8);
    if (nb_components <= 0 ||
        nb_components > MAX_COMPONENTS)
        return -1;
    s->nb_components = nb_components;
    s->h_max = 1;
    s->v_max = 1;
    for(i=0;i<nb_components;i++) {
        /* component id */
        s->component_id[i] = get_bits(&s->gb, 8) - 1;
        s->h_count[i] = get_bits(&s->gb, 4);
        s->v_count[i] = get_bits(&s->gb, 4);
        /* compute hmax and vmax (only used in interleaved case) */
        if (s->h_count[i] > s->h_max)
            s->h_max = s->h_count[i];
        if (s->v_count[i] > s->v_max)
            s->v_max = s->v_count[i];
        s->quant_index[i] = get_bits(&s->gb, 8);
        if (s->quant_index[i] >= 4)
            return -1;
        dprintf("component %d %d:%d id: %d quant:%d\n", i, s->h_count[i],
	    s->v_count[i], s->component_id[i], s->quant_index[i]);
    }
    
    if(s->v_max==1 && s->h_max==1 && s->lossless==1) s->rgb=1;

    /* if different size, realloc/alloc picture */
    /* XXX: also check h_count and v_count */
    if (width != s->width || height != s->height) {
        for(i=0;i<MAX_COMPONENTS;i++)
            av_freep(&s->current_picture[i]);
            
        av_freep(&s->qscale_table);
            
        s->width = width;
        s->height = height;
        /* test interlaced mode */
        if (s->first_picture &&
            s->org_height != 0 &&
            s->height < ((s->org_height * 3) / 4)) {
            s->interlaced = 1;
//	    s->bottom_field = (s->interlace_polarity) ? 1 : 0;
	    s->bottom_field = 0;
        }

        if(s->rgb){
            int w, h;
            w = s->width;
            h = s->height;
            if (s->interlaced)
                w *= 2;
            s->linesize[0] = 4*w;
            s->current_picture[0] = av_mallocz(4*w * h);
            s->current_picture[1] = s->current_picture[2] = NULL;
        }else{
          for(i=0;i<nb_components;i++) {
            int w, h;
            w = (s->width  + 8 * s->h_max - 1) / (8 * s->h_max);
            h = (s->height + 8 * s->v_max - 1) / (8 * s->v_max);
            w = w * 8 * s->h_count[i];
            h = h * 8 * s->v_count[i];
            if (s->interlaced)
                w *= 2;
            s->linesize[i] = w;
            s->current_picture[i] = av_mallocz(w * h);
	    if (!s->current_picture[i])
	    {
		dprintf("error: no picture buffers allocated\n");
		return -1;
	    }
          }
        }
        s->qscale_table= av_mallocz((s->width+15)/16);

        s->first_picture = 0;
    }

    if (len != (8+(3*nb_components)))
    {
	dprintf("decode_sof0: error, len(%d) mismatch\n", len);
    }
    
    return 0;
}

static inline int mjpeg_decode_dc(MJpegDecodeContext *s, int dc_index)
{
    int code;
    code = get_vlc2(&s->gb, s->vlcs[0][dc_index].table, 9, 2);
    if (code < 0)
    {
	dprintf("mjpeg_decode_dc: bad vlc: %d:%d (%p)\n", 0, dc_index,
                &s->vlcs[0][dc_index]);
        return 0xffff;
    }

    if(code)
        return get_xbits(&s->gb, code);
    else
        return 0;
}

/* decode block and dequantize */
static int decode_block(MJpegDecodeContext *s, DCTELEM *block, 
                        int component, int dc_index, int ac_index, int quant_index)
{
    int code, i, j, level, val;
    VLC *ac_vlc;
    int16_t *quant_matrix;

    /* DC coef */
    val = mjpeg_decode_dc(s, dc_index);
    if (val == 0xffff) {
        dprintf("error dc\n");
        return -1;
    }
    quant_matrix = s->quant_matrixes[quant_index];
    val = val * quant_matrix[0] + s->last_dc[component];
    s->last_dc[component] = val;
    block[0] = val;
    /* AC coefs */
    ac_vlc = &s->vlcs[1][ac_index];
    i = 1;
    for(;;) {
	code = get_vlc2(&s->gb, s->vlcs[1][ac_index].table, 9, 2);

        if (code < 0) {
            dprintf("error ac\n");
            return -1;
        }
        /* EOB */
        if (code == 0)
            break;
        if (code == 0xf0) {
            i += 16;
        } else {
            level = get_xbits(&s->gb, code & 0xf);
            i += code >> 4;
            if (i >= 64) {
                dprintf("error count: %d\n", i);
                return -1;
            }
            j = s->scantable.permutated[i];
            block[j] = level * quant_matrix[j];
            i++;
            if (i >= 64)
                break;
        }
    }
    return 0;
}

static int ljpeg_decode_rgb_scan(MJpegDecodeContext *s, int predictor, int point_transform){
    int i, mb_x, mb_y;
    uint16_t buffer[2048][4];
    int left[3], top[3], topleft[3];
    const int linesize= s->linesize[0];
    const int mask= (1<<s->bits)-1;
    
    for(i=0; i<3; i++){
        buffer[0][i]= 1 << (s->bits + point_transform - 1);
    }
    for(mb_y = 0; mb_y < s->mb_height; mb_y++) {
        const int modified_predictor= mb_y ? predictor : 1;
        uint8_t *ptr = s->current_picture[0] + (linesize * mb_y);

        if (s->interlaced && s->bottom_field)
            ptr += linesize >> 1;

        for(i=0; i<3; i++){
            top[i]= left[i]= topleft[i]= buffer[0][i];
        }
        for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
            if (s->restart_interval && !s->restart_count)
                s->restart_count = s->restart_interval;

            for(i=0;i<3;i++) {
                int pred;

                topleft[i]= top[i];
                top[i]= buffer[mb_x][i];

                PREDICT(pred, topleft[i], top[i], left[i], modified_predictor);
                
                left[i]= 
                buffer[mb_x][i]= mask & (pred + (mjpeg_decode_dc(s, s->dc_index[i]) << point_transform));
            }

            if (s->restart_interval && !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
            }
        }

        if(s->rct){
            for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
                ptr[4*mb_x+1] = buffer[mb_x][0] - ((buffer[mb_x][1] + buffer[mb_x][2] - 0x200)>>2);
                ptr[4*mb_x+0] = buffer[mb_x][1] + ptr[4*mb_x+1];
                ptr[4*mb_x+2] = buffer[mb_x][2] + ptr[4*mb_x+1];
            }
        }else if(s->pegasus_rct){
            for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
                ptr[4*mb_x+1] = buffer[mb_x][0] - ((buffer[mb_x][1] + buffer[mb_x][2])>>2);
                ptr[4*mb_x+0] = buffer[mb_x][1] + ptr[4*mb_x+1];
                ptr[4*mb_x+2] = buffer[mb_x][2] + ptr[4*mb_x+1];
            }
        }else{
            for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
                ptr[4*mb_x+0] = buffer[mb_x][0];
                ptr[4*mb_x+1] = buffer[mb_x][1];
                ptr[4*mb_x+2] = buffer[mb_x][2];
            }
        }
    }
    return 0;
}

static int ljpeg_decode_yuv_scan(MJpegDecodeContext *s, int predictor, int point_transform){
    int i, mb_x, mb_y;
    const int nb_components=3;

    for(mb_y = 0; mb_y < s->mb_height; mb_y++) {
        for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
            if (s->restart_interval && !s->restart_count)
                s->restart_count = s->restart_interval;

            if(mb_x==0 || mb_y==0 || s->interlaced){
                for(i=0;i<nb_components;i++) {
                    uint8_t *ptr;
                    int n, h, v, x, y, c, j, linesize;
                    n = s->nb_blocks[i];
                    c = s->comp_index[i];
                    h = s->h_scount[i];
                    v = s->v_scount[i];
                    x = 0;
                    y = 0;
                    linesize= s->linesize[c];
                    
                    for(j=0; j<n; j++) {
                        int pred;

                        ptr = s->current_picture[c] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
                        if(y==0 && mb_y==0){
                            if(x==0 && mb_x==0){
                                pred= 128 << point_transform;
                            }else{
                                pred= ptr[-1];
                            }
                        }else{
                            if(x==0 && mb_x==0){
                                pred= ptr[-linesize];
                            }else{
                                PREDICT(pred, ptr[-linesize-1], ptr[-linesize], ptr[-1], predictor);
                            }
                        }
                        
                        if (s->interlaced && s->bottom_field)
                            ptr += linesize >> 1;
                        *ptr= pred + (mjpeg_decode_dc(s, s->dc_index[i]) << point_transform);

                        if (++x == h) {
                            x = 0;
                            y++;
                        }
                    }
                }
            }else{
                for(i=0;i<nb_components;i++) {
                    uint8_t *ptr;
                    int n, h, v, x, y, c, j, linesize;
                    n = s->nb_blocks[i];
                    c = s->comp_index[i];
                    h = s->h_scount[i];
                    v = s->v_scount[i];
                    x = 0;
                    y = 0;
                    linesize= s->linesize[c];
                    
                    for(j=0; j<n; j++) {
                        int pred;

                        ptr = s->current_picture[c] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
                        PREDICT(pred, ptr[-linesize-1], ptr[-linesize], ptr[-1], predictor);
                        *ptr= pred + (mjpeg_decode_dc(s, s->dc_index[i]) << point_transform);
                        if (++x == h) {
                            x = 0;
                            y++;
                        }
                    }
                }
            }
            if (s->restart_interval && !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
            }
        }
    }
    return 0;
}

static int mjpeg_decode_scan(MJpegDecodeContext *s){
    int i, mb_x, mb_y;
    const int nb_components=3;

    for(mb_y = 0; mb_y < s->mb_height; mb_y++) {
        for(mb_x = 0; mb_x < s->mb_width; mb_x++) {
            if (s->restart_interval && !s->restart_count)
                s->restart_count = s->restart_interval;

            for(i=0;i<nb_components;i++) {
                uint8_t *ptr;
                int n, h, v, x, y, c, j;
                n = s->nb_blocks[i];
                c = s->comp_index[i];
                h = s->h_scount[i];
                v = s->v_scount[i];
                x = 0;
                y = 0;
                for(j=0;j<n;j++) {
                    memset(s->block, 0, sizeof(s->block));
                    if (decode_block(s, s->block, i, 
                                     s->dc_index[i], s->ac_index[i], 
                                     s->quant_index[c]) < 0) {
                        dprintf("error y=%d x=%d\n", mb_y, mb_x);
                        return -1;
                    }
//		    dprintf("mb: %d %d processed\n", mb_y, mb_x);
                    ptr = s->current_picture[c] + 
                        (s->linesize[c] * (v * mb_y + y) * 8) + 
                        (h * mb_x + x) * 8;
                    if (s->interlaced && s->bottom_field)
                        ptr += s->linesize[c] >> 1;
                    s->idct_put(ptr, s->linesize[c], s->block);
                    if (++x == h) {
                        x = 0;
                        y++;
                    }
                }
            }
            /* (< 1350) buggy workaround for Spectralfan.mov, should be fixed */
            if (s->restart_interval && (s->restart_interval < 1350) &&
                !--s->restart_count) {
                align_get_bits(&s->gb);
                skip_bits(&s->gb, 16); /* skip RSTn */
                for (i=0; i<nb_components; i++) /* reset dc */
                    s->last_dc[i] = 1024;
            }
        }
    }
    return 0;
}

static int mjpeg_decode_sos(MJpegDecodeContext *s)
{
    int len, nb_components, i, h, v, predictor, point_transform;
    int vmax, hmax, index, id;
    const int block_size= s->lossless ? 1 : 8;

    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    nb_components = get_bits(&s->gb, 8);
    if (len != 6+2*nb_components)
    {
	dprintf("decode_sos: invalid len (%d)\n", len);
	return -1;
    }
    /* XXX: only interleaved scan accepted */
    if (nb_components != 3)
    {
	dprintf("decode_sos: components(%d) mismatch\n", nb_components);
        return -1;
    }
    vmax = 0;
    hmax = 0;
    for(i=0;i<nb_components;i++) {
        id = get_bits(&s->gb, 8) - 1;
	dprintf("component: %d\n", id);
        /* find component index */
        for(index=0;index<s->nb_components;index++)
            if (id == s->component_id[index])
                break;
        if (index == s->nb_components)
	{
	    dprintf("decode_sos: index(%d) out of components\n", index);
            return -1;
	}

        s->comp_index[i] = index;

        s->nb_blocks[i] = s->h_count[index] * s->v_count[index];
        s->h_scount[i] = s->h_count[index];
        s->v_scount[i] = s->v_count[index];

        s->dc_index[i] = get_bits(&s->gb, 4);
        s->ac_index[i] = get_bits(&s->gb, 4);

	if (s->dc_index[i] <  0 || s->ac_index[i] < 0 ||
	    s->dc_index[i] >= 4 || s->ac_index[i] >= 4)
	    goto out_of_range;
#if 0 //buggy
	switch(s->start_code)
	{
	    case SOF0:
		if (dc_index[i] > 1 || ac_index[i] > 1)
		    goto out_of_range;
		break;
	    case SOF1:
	    case SOF2:
		if (dc_index[i] > 3 || ac_index[i] > 3)
		    goto out_of_range;
		break;
	    case SOF3:
		if (dc_index[i] > 3 || ac_index[i] != 0)
		    goto out_of_range;
		break;	
	}
#endif
    }

    predictor= get_bits(&s->gb, 8); /* lossless predictor or start of spectral (Ss) */
    skip_bits(&s->gb, 8); /* Se */
    skip_bits(&s->gb, 4); /* Ah */
    point_transform= get_bits(&s->gb, 4); /* Al */

    for(i=0;i<nb_components;i++) 
        s->last_dc[i] = 1024;

    if (nb_components > 1) {
        /* interleaved stream */
        s->mb_width  = (s->width  + s->h_max * block_size - 1) / (s->h_max * block_size);
        s->mb_height = (s->height + s->v_max * block_size - 1) / (s->v_max * block_size);
    } else {
        h = s->h_max / s->h_scount[s->comp_index[0]];
        v = s->v_max / s->v_scount[s->comp_index[0]];
        s->mb_width  = (s->width  + h * block_size - 1) / (h * block_size);
        s->mb_height = (s->height + v * block_size - 1) / (v * block_size);
        s->nb_blocks[0] = 1;
        s->h_scount[0] = 1;
        s->v_scount[0] = 1;
    }

    if(s->avctx->debug & FF_DEBUG_PICT_INFO)
        printf("%s %s p:%d >>:%d\n", s->lossless ? "lossless" : "sequencial DCT", s->rgb ? "RGB" : "", predictor, point_transform);
    
    if(s->lossless){
            if(s->rgb){
                if(ljpeg_decode_rgb_scan(s, predictor, point_transform) < 0)
                    return -1;
            }else{
                if(ljpeg_decode_yuv_scan(s, predictor, point_transform) < 0)
                    return -1;
            }
    }else{
        if(mjpeg_decode_scan(s) < 0)
            return -1;
    }
    emms_c();
    return 0;
 out_of_range:
    dprintf("decode_sos: ac/dc index out of range\n");
    return -1;
}

static int mjpeg_decode_dri(MJpegDecodeContext *s)
{
    if (get_bits(&s->gb, 16) != 4)
	return -1;
    s->restart_interval = get_bits(&s->gb, 16);
    dprintf("restart interval: %d\n", s->restart_interval);

    return 0;
}

static int mjpeg_decode_app(MJpegDecodeContext *s)
{
    int len, id;

    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    if (len < 5)
	return -1;

    id = (get_bits(&s->gb, 16) << 16) | get_bits(&s->gb, 16);
    id = be2me_32(id);
    len -= 6;

    if(s->avctx->debug & FF_DEBUG_STARTCODE){
        printf("APPx %8X\n", id); 
    }
    
    /* buggy AVID, it puts EOI only at every 10th frame */
    /* also this fourcc is used by non-avid files too, it holds some
       informations, but it's always present in AVID creates files */
    if (id == ff_get_fourcc("AVI1"))
    {
	/* structure:
	    4bytes	AVI1
	    1bytes	polarity
	    1bytes	always zero
	    4bytes	field_size
	    4bytes	field_size_less_padding
	*/
    	s->buggy_avid = 1;
//	if (s->first_picture)
//	    printf("mjpeg: workarounding buggy AVID\n");
	s->interlace_polarity = get_bits(&s->gb, 8);
#if 0
	skip_bits(&s->gb, 8);
	skip_bits(&s->gb, 32);
	skip_bits(&s->gb, 32);
	len -= 10;
#endif
//	if (s->interlace_polarity)
//	    printf("mjpeg: interlace polarity: %d\n", s->interlace_polarity);
	goto out;
    }
    
//    len -= 2;
    
    if (id == ff_get_fourcc("JFIF"))
    {
	int t_w, t_h;
	skip_bits(&s->gb, 8); /* the trailing zero-byte */
	printf("mjpeg: JFIF header found (version: %x.%x)\n",
	    get_bits(&s->gb, 8), get_bits(&s->gb, 8));
	if (get_bits(&s->gb, 8) == 0)
	{
	    int x_density, y_density; 
	    x_density = get_bits(&s->gb, 16);
	    y_density = get_bits(&s->gb, 16);

	    dprintf("x/y density: %d (%f), %d (%f)\n", x_density,
		(float)x_density, y_density, (float)y_density);
#if 0
            //MN: needs to be checked
            if(x_density)
//                s->avctx->aspect_ratio= s->width*y_density/((float)s->height*x_density);
		s->avctx->aspect_ratio = (float)x_density/y_density;
		/* it's better, but every JFIF I have seen stores 1:1 */
            else
                s->avctx->aspect_ratio= 0.0;
#endif
	}
	else
	{
	    skip_bits(&s->gb, 16);
	    skip_bits(&s->gb, 16);
	}

	t_w = get_bits(&s->gb, 8);
	t_h = get_bits(&s->gb, 8);
	if (t_w && t_h)
	{
	    /* skip thumbnail */
	    if (len-10-(t_w*t_h*3) > 0)
		len -= t_w*t_h*3;
	}
	len -= 10;
	goto out;
    }
    
    if (id == ff_get_fourcc("Adob") && (get_bits(&s->gb, 8) == 'e'))
    {
	printf("mjpeg: Adobe header found\n");
	skip_bits(&s->gb, 16); /* version */
	skip_bits(&s->gb, 16); /* flags0 */
	skip_bits(&s->gb, 16); /* flags1 */
	skip_bits(&s->gb, 8); /* transform */
	len -= 7;
	goto out;
    }

    if (id == ff_get_fourcc("LJIF")){
        printf("Pegasus lossless jpeg header found\n");
	skip_bits(&s->gb, 16); /* version ? */
	skip_bits(&s->gb, 16); /* unknwon always 0? */
	skip_bits(&s->gb, 16); /* unknwon always 0? */
	skip_bits(&s->gb, 16); /* unknwon always 0? */
        switch( get_bits(&s->gb, 8)){
        case 1:
            s->rgb= 1;
            s->pegasus_rct=0;
            break;
        case 2:
            s->rgb= 1;
            s->pegasus_rct=1;
            break;
        default:
            printf("unknown colorspace\n");
        }
        len -= 9;
        goto out;
    }
    
    /* Apple MJPEG-A */
    if ((s->start_code == APP1) && (len > (0x28 - 8)))
    {
	id = (get_bits(&s->gb, 16) << 16) | get_bits(&s->gb, 16);
	id = be2me_32(id);
	len -= 4;
	if (id == ff_get_fourcc("mjpg")) /* Apple MJPEG-A */
	{
#if 0
	    skip_bits(&s->gb, 32); /* field size */
	    skip_bits(&s->gb, 32); /* pad field size */
	    skip_bits(&s->gb, 32); /* next off */
	    skip_bits(&s->gb, 32); /* quant off */
	    skip_bits(&s->gb, 32); /* huff off */
	    skip_bits(&s->gb, 32); /* image off */
	    skip_bits(&s->gb, 32); /* scan off */
	    skip_bits(&s->gb, 32); /* data off */
#endif
	    if (s->first_picture)
		printf("mjpeg: Apple MJPEG-A header found\n");
	}
    }

out:
    /* slow but needed for extreme adobe jpegs */
    if (len < 0)
	printf("mjpeg: error, decode_app parser read over the end\n");
    while(--len > 0)
	skip_bits(&s->gb, 8);

    return 0;
}

static int mjpeg_decode_com(MJpegDecodeContext *s)
{
    /* XXX: verify len field validity */
    int len = get_bits(&s->gb, 16);
    if (len >= 2 && len < 32768) {
	/* XXX: any better upper bound */
	uint8_t *cbuf = av_malloc(len - 1);
	if (cbuf) {
	    int i;
	    for (i = 0; i < len - 2; i++)
		cbuf[i] = get_bits(&s->gb, 8);
	    if (i > 0 && cbuf[i-1] == '\n')
		cbuf[i-1] = 0;
	    else
		cbuf[i] = 0;

	    printf("mjpeg comment: '%s'\n", cbuf);

	    /* buggy avid, it puts EOI only at every 10th frame */
	    if (!strcmp(cbuf, "AVID"))
	    {
		s->buggy_avid = 1;
		//	if (s->first_picture)
		//	    printf("mjpeg: workarounding buggy AVID\n");
	    }

	    av_free(cbuf);
	}
    }

    return 0;
}

#if 0
static int valid_marker_list[] =
{
        /* 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, a, b, c, d, e, f */
/* 0 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 1 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 2 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 3 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 4 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 5 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 6 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 7 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 8 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* 9 */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* a */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* b */    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
/* c */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* d */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* e */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
/* f */    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0,
}
#endif

/* return the 8 bit start code value and update the search
   state. Return -1 if no start code found */
static int find_marker(uint8_t **pbuf_ptr, uint8_t *buf_end)
{
    uint8_t *buf_ptr;
    unsigned int v, v2;
    int val;
#ifdef DEBUG
    int skipped=0;
#endif

    buf_ptr = *pbuf_ptr;
    while (buf_ptr < buf_end) {
        v = *buf_ptr++;
	v2 = *buf_ptr;
        if ((v == 0xff) && (v2 >= 0xc0) && (v2 <= 0xfe)) {
	    val = *buf_ptr++;
	    goto found;
        }
#ifdef DEBUG
	skipped++;
#endif
    }
    val = -1;
found:
#ifdef DEBUG
    dprintf("find_marker skipped %d bytes\n", skipped);
#endif
    *pbuf_ptr = buf_ptr;
    return val;
}

static int mjpeg_decode_frame(AVCodecContext *avctx, 
                              void *data, int *data_size,
                              uint8_t *buf, int buf_size)
{
    MJpegDecodeContext *s = avctx->priv_data;
    uint8_t *buf_end, *buf_ptr;
    int i, start_code;
    AVFrame *picture = data;

    *data_size = 0;

    /* no supplementary picture */
    if (buf_size == 0)
        return 0;

    buf_ptr = buf;
    buf_end = buf + buf_size;
    while (buf_ptr < buf_end) {
        /* find start next marker */
        start_code = find_marker(&buf_ptr, buf_end);
	{
	    /* EOF */
            if (start_code < 0) {
		goto the_end;
            } else {
                dprintf("marker=%x avail_size_in_buf=%d\n", start_code, buf_end - buf_ptr);
		
		if ((buf_end - buf_ptr) > s->buffer_size)
		{
		    av_free(s->buffer);
		    s->buffer_size = buf_end-buf_ptr;
		    s->buffer = av_malloc(s->buffer_size);
		    dprintf("buffer too small, expanding to %d bytes\n",
			s->buffer_size);
		}
		
		/* unescape buffer of SOS */
		if (start_code == SOS)
		{
		    uint8_t *src = buf_ptr;
		    uint8_t *dst = s->buffer;

		    while (src<buf_end)
		    {
			uint8_t x = *(src++);

			*(dst++) = x;
			if (x == 0xff)
			{
			    while(*src == 0xff) src++;

			    x = *(src++);
			    if (x >= 0xd0 && x <= 0xd7)
				*(dst++) = x;
			    else if (x)
				break;
			}
		    }
		    init_get_bits(&s->gb, s->buffer, (dst - s->buffer)*8);
		    
		    dprintf("escaping removed %d bytes\n",
			(buf_end - buf_ptr) - (dst - s->buffer));
		}
		else
		    init_get_bits(&s->gb, buf_ptr, (buf_end - buf_ptr)*8);
		
		s->start_code = start_code;
                if(s->avctx->debug & FF_DEBUG_STARTCODE){
                    printf("startcode: %X\n", start_code);
                }

		/* process markers */
		if (start_code >= 0xd0 && start_code <= 0xd7) {
		    dprintf("restart marker: %d\n", start_code&0x0f);
		} else if (s->first_picture) {
		    /* APP fields */
		    if (start_code >= 0xe0 && start_code <= 0xef)
			mjpeg_decode_app(s);
		    /* Comment */
		    else if (start_code == COM)
			mjpeg_decode_com(s);
		}

                switch(start_code) {
                case SOI:
		    s->restart_interval = 0;
                    /* nothing to do on SOI */
                    break;
                case DQT:
                    mjpeg_decode_dqt(s);
                    break;
                case DHT:
                    if(mjpeg_decode_dht(s) < 0){
                        fprintf(stderr, "huffman table decode error\n");
                        return -1;
                    }
                    break;
                case SOF0:
                    s->lossless=0;
                    if (mjpeg_decode_sof(s) < 0) 
			return -1;
                    break;
                case SOF3:
                    s->lossless=1;
                    if (mjpeg_decode_sof(s) < 0) 
			return -1;
                    break;
		case EOI:
		    if ((s->buggy_avid && !s->interlaced) || s->restart_interval) 
                        break;
eoi_parser:
		    {
                        if (s->interlaced) {
                            s->bottom_field ^= 1;
                            /* if not bottom field, do not output image yet */
                            if (s->bottom_field)
                                goto not_the_end;
                        }
                        for(i=0;i<3;i++) {
                            picture->data[i] = s->current_picture[i];
			    picture->linesize[i] = (s->interlaced) ?
				s->linesize[i] >> 1 : s->linesize[i];
                        }
                        *data_size = sizeof(AVFrame);
                        avctx->height = s->height;
                        if (s->interlaced)
                            avctx->height *= 2;
                        avctx->width = s->width;
                        /* XXX: not complete test ! */
                        switch((s->h_count[0] << 4) | s->v_count[0]) {
                        case 0x11:
                            if(s->rgb){
                                avctx->pix_fmt = PIX_FMT_RGBA32;
                            }else
                                avctx->pix_fmt = PIX_FMT_YUV444P;
                            break;
                        case 0x21:
                            avctx->pix_fmt = PIX_FMT_YUV422P;
                            break;
                        default:
                        case 0x22:
                            avctx->pix_fmt = PIX_FMT_YUV420P;
                            break;
                        }

                        if(!s->lossless){
                            picture->quality= FFMAX(FFMAX(s->qscale[0], s->qscale[1]), s->qscale[2]); 
                            picture->qstride= 0;
                            picture->qscale_table= s->qscale_table;
                            memset(picture->qscale_table, picture->quality, (s->width+15)/16);
                            if(avctx->debug & FF_DEBUG_QP)
                                printf("QP: %d\n", (int)picture->quality);
                        }
                        
                        goto the_end;
                    }
		    break;
                case SOS:
                    mjpeg_decode_sos(s);
		    /* buggy avid puts EOI every 10-20th frame */
		    /* if restart period is over process EOI */
		    if ((s->buggy_avid && !s->interlaced) || s->restart_interval)
			goto eoi_parser;
                    break;
		case DRI:
		    mjpeg_decode_dri(s);
		    break;
		case SOF1:
		case SOF2:
		case SOF5:
		case SOF6:
		case SOF7:
		case SOF9:
		case SOF10:
		case SOF11:
		case SOF13:
		case SOF14:
		case SOF15:
		case JPG:
		    printf("mjpeg: unsupported coding type (%x)\n", start_code);
		    break;
//		default:
//		    printf("mjpeg: unsupported marker (%x)\n", start_code);
//		    break;
                }

not_the_end:
		/* eof process start code */
		buf_ptr += (get_bits_count(&s->gb)+7)/8;
		dprintf("marker parser used %d bytes (%d bits)\n",
		    (get_bits_count(&s->gb)+7)/8, get_bits_count(&s->gb));
            }
        }
    }
the_end:
    dprintf("mjpeg decode frame unused %d bytes\n", buf_end - buf_ptr);
//    return buf_end - buf_ptr;
    return buf_ptr - buf;
}

static int mjpegb_decode_frame(AVCodecContext *avctx, 
                              void *data, int *data_size,
                              uint8_t *buf, int buf_size)
{
    MJpegDecodeContext *s = avctx->priv_data;
    uint8_t *buf_end, *buf_ptr;
    int i;
    AVFrame *picture = data;
    GetBitContext hgb; /* for the header */
    uint32_t dqt_offs, dht_offs, sof_offs, sos_offs, second_field_offs;
    uint32_t field_size;

    *data_size = 0;

    /* no supplementary picture */
    if (buf_size == 0)
        return 0;

    buf_ptr = buf;
    buf_end = buf + buf_size;
    
read_header:
    /* reset on every SOI */
    s->restart_interval = 0;

    init_get_bits(&hgb, buf_ptr, /*buf_size*/(buf_end - buf_ptr)*8);

    skip_bits(&hgb, 32); /* reserved zeros */
    
    if (get_bits(&hgb, 32) != be2me_32(ff_get_fourcc("mjpg")))
    {
	dprintf("not mjpeg-b (bad fourcc)\n");
	return 0;
    }

    field_size = get_bits(&hgb, 32); /* field size */
    dprintf("field size: 0x%x\n", field_size);
    skip_bits(&hgb, 32); /* padded field size */
    second_field_offs = get_bits(&hgb, 32);
    dprintf("second field offs: 0x%x\n", second_field_offs);
    if (second_field_offs)
	s->interlaced = 1;

    dqt_offs = get_bits(&hgb, 32);
    dprintf("dqt offs: 0x%x\n", dqt_offs);
    if (dqt_offs)
    {
	init_get_bits(&s->gb, buf+dqt_offs, (buf_end - (buf+dqt_offs))*8);
	s->start_code = DQT;
	mjpeg_decode_dqt(s);
    }
    
    dht_offs = get_bits(&hgb, 32);
    dprintf("dht offs: 0x%x\n", dht_offs);
    if (dht_offs)
    {
	init_get_bits(&s->gb, buf+dht_offs, (buf_end - (buf+dht_offs))*8);
	s->start_code = DHT;
	mjpeg_decode_dht(s);
    }

    sof_offs = get_bits(&hgb, 32);
    dprintf("sof offs: 0x%x\n", sof_offs);
    if (sof_offs)
    {
	init_get_bits(&s->gb, buf+sof_offs, (buf_end - (buf+sof_offs))*8);
	s->start_code = SOF0;
	if (mjpeg_decode_sof(s) < 0)
	    return -1;
    }

    sos_offs = get_bits(&hgb, 32);
    dprintf("sos offs: 0x%x\n", sos_offs);
    if (sos_offs)
    {
//	init_get_bits(&s->gb, buf+sos_offs, (buf_end - (buf+sos_offs))*8);
	init_get_bits(&s->gb, buf+sos_offs, field_size*8);
	s->start_code = SOS;
	mjpeg_decode_sos(s);
    }

    skip_bits(&hgb, 32); /* start of data offset */

    if (s->interlaced) {
        s->bottom_field ^= 1;
        /* if not bottom field, do not output image yet */
        if (s->bottom_field && second_field_offs)
	{
	    buf_ptr = buf + second_field_offs;
	    second_field_offs = 0;
	    goto read_header;
    	}
    }

    //XXX FIXME factorize, this looks very similar to the EOI code
    
    for(i=0;i<3;i++) {
        picture->data[i] = s->current_picture[i];
        picture->linesize[i] = (s->interlaced) ?
    	    s->linesize[i] >> 1 : s->linesize[i];
    }
    *data_size = sizeof(AVFrame);
    avctx->height = s->height;
    if (s->interlaced)
        avctx->height *= 2;
    avctx->width = s->width;
    /* XXX: not complete test ! */
    switch((s->h_count[0] << 4) | s->v_count[0]) {
        case 0x11:
    	    avctx->pix_fmt = PIX_FMT_YUV444P;
            break;
        case 0x21:
            avctx->pix_fmt = PIX_FMT_YUV422P;
            break;
        default:
	case 0x22:
            avctx->pix_fmt = PIX_FMT_YUV420P;
            break;
    }
    
    if(!s->lossless){
        picture->quality= FFMAX(FFMAX(s->qscale[0], s->qscale[1]), s->qscale[2]); 
        picture->qstride= 0;
        picture->qscale_table= s->qscale_table;
        memset(picture->qscale_table, picture->quality, (s->width+15)/16);
        if(avctx->debug & FF_DEBUG_QP)
            printf("QP: %f\n", picture->quality);
    }

    return buf_ptr - buf;
}


static int mjpeg_decode_end(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;
    int i, j;

    av_free(s->buffer);
    av_free(s->qscale_table);
    for(i=0;i<MAX_COMPONENTS;i++)
        av_free(s->current_picture[i]);
    for(i=0;i<2;i++) {
        for(j=0;j<4;j++)
            free_vlc(&s->vlcs[i][j]);
    }
    return 0;
}

AVCodec mjpeg_decoder = {
    "mjpeg",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MJPEG,
    sizeof(MJpegDecodeContext),
    mjpeg_decode_init,
    NULL,
    mjpeg_decode_end,
    mjpeg_decode_frame,
    0,
    NULL
};

AVCodec mjpegb_decoder = {
    "mjpegb",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MJPEGB,
    sizeof(MJpegDecodeContext),
    mjpeg_decode_init,
    NULL,
    mjpeg_decode_end,
    mjpegb_decode_frame,
    0,
    NULL
};

#ifdef CONFIG_ENCODERS
AVCodec ljpeg_encoder = { //FIXME avoid MPV_* lossless jpeg shouldnt need them
    "ljpeg",
    CODEC_TYPE_VIDEO,
    CODEC_ID_LJPEG,
    sizeof(MpegEncContext),
    MPV_encode_init,
    encode_picture_lossless,
    MPV_encode_end,
};
#endif
