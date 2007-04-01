/*
 * MJPEG encoder and decoder
 * Copyright (c) 2000, 2001 Fabrice Bellard.
 * Copyright (c) 2003 Alex Beregszaszi
 * Copyright (c) 2003-2004 Michael Niedermayer
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
#include "bytestream.h"

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
    SOF0  = 0xc0,       /* baseline */
    SOF1  = 0xc1,       /* extended sequential, huffman */
    SOF2  = 0xc2,       /* progressive, huffman */
    SOF3  = 0xc3,       /* lossless, huffman */

    SOF5  = 0xc5,       /* differential sequential, huffman */
    SOF6  = 0xc6,       /* differential progressive, huffman */
    SOF7  = 0xc7,       /* differential lossless, huffman */
    JPG   = 0xc8,       /* reserved for JPEG extension */
    SOF9  = 0xc9,       /* extended sequential, arithmetic */
    SOF10 = 0xca,       /* progressive, arithmetic */
    SOF11 = 0xcb,       /* lossless, arithmetic */

    SOF13 = 0xcd,       /* differential sequential, arithmetic */
    SOF14 = 0xce,       /* differential progressive, arithmetic */
    SOF15 = 0xcf,       /* differential lossless, arithmetic */

    DHT   = 0xc4,       /* define huffman tables */

    DAC   = 0xcc,       /* define arithmetic-coding conditioning */

    /* restart with modulo 8 count "m" */
    RST0  = 0xd0,
    RST1  = 0xd1,
    RST2  = 0xd2,
    RST3  = 0xd3,
    RST4  = 0xd4,
    RST5  = 0xd5,
    RST6  = 0xd6,
    RST7  = 0xd7,

    SOI   = 0xd8,       /* start of image */
    EOI   = 0xd9,       /* end of image */
    SOS   = 0xda,       /* start of scan */
    DQT   = 0xdb,       /* define quantization tables */
    DNL   = 0xdc,       /* define number of lines */
    DRI   = 0xdd,       /* define restart interval */
    DHP   = 0xde,       /* define hierarchical progression */
    EXP   = 0xdf,       /* expand reference components */

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
    SOF48 = 0xf7,       ///< JPEG-LS
    LSE   = 0xf8,       ///< JPEG-LS extension parameters
    JPG9  = 0xf9,
    JPG10 = 0xfa,
    JPG11 = 0xfb,
    JPG12 = 0xfc,
    JPG13 = 0xfd,

    COM   = 0xfe,       /* comment */

    TEM   = 0x01,       /* temporary private use for arithmetic coding */

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

#ifdef CONFIG_ENCODERS
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
#endif //CONFIG_ENCODERS

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
    ff_put_string(p, "JFIF", 1); /* this puts the trailing zero-byte too */
    put_bits(p, 16, 0x0201); /* v 1.02 */
    put_bits(p, 8, 0); /* units type: 0 - aspect ratio */
    put_bits(p, 16, s->avctx->sample_aspect_ratio.num);
    put_bits(p, 16, s->avctx->sample_aspect_ratio.den);
    put_bits(p, 8, 0); /* thumbnail width */
    put_bits(p, 8, 0); /* thumbnail height */
    }

    /* comment */
    if(!(s->flags & CODEC_FLAG_BITEXACT)){
        put_marker(p, COM);
        flush_put_bits(p);
        ptr = pbBufPtr(p);
        put_bits(p, 16, 0); /* patched later */
        ff_put_string(p, LIBAVCODEC_IDENT, 1);
        size = strlen(LIBAVCODEC_IDENT)+3;
        ptr[0] = size >> 8;
        ptr[1] = size;
    }

    if(  s->avctx->pix_fmt == PIX_FMT_YUV420P
       ||s->avctx->pix_fmt == PIX_FMT_YUV422P
       ||s->avctx->pix_fmt == PIX_FMT_YUV444P){
        put_marker(p, COM);
        flush_put_bits(p);
        ptr = pbBufPtr(p);
        put_bits(p, 16, 0); /* patched later */
        ff_put_string(p, "CS=ITU601", 1);
        size = strlen("CS=ITU601")+3;
        ptr[0] = size >> 8;
        ptr[1] = size;
    }
}

void mjpeg_picture_header(MpegEncContext *s)
{
    const int lossless= s->avctx->codec_id != CODEC_ID_MJPEG;
    const int ls      = s->avctx->codec_id == CODEC_ID_JPEGLS;

    assert(!(ls && s->mjpeg_write_tables));

    put_marker(&s->pb, SOI);

    if (!s->mjpeg_data_only_frames)
    {
    jpeg_put_comments(s);

    if (s->mjpeg_write_tables) jpeg_table_header(s);

    switch(s->avctx->codec_id){
    case CODEC_ID_MJPEG:  put_marker(&s->pb, SOF0 ); break;
    case CODEC_ID_LJPEG:  put_marker(&s->pb, SOF3 ); break;
    case CODEC_ID_JPEGLS: put_marker(&s->pb, SOF48); break;
    default: assert(0);
    }

    put_bits(&s->pb, 16, 17);
    if(lossless && s->avctx->pix_fmt == PIX_FMT_RGB32)
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

    put_bits(&s->pb, 8, (lossless && !ls) ? s->avctx->prediction_method+1 : 0); /* Ss (not used) */

    switch(s->avctx->codec_id){
    case CODEC_ID_MJPEG:  put_bits(&s->pb, 8, 63); break; /* Se (not used) */
    case CODEC_ID_LJPEG:  put_bits(&s->pb, 8,  0); break; /* not used */
    case CODEC_ID_JPEGLS: put_bits(&s->pb, 8,  1); break; /* ILV = line interleaved */
    default: assert(0);
    }

    put_bits(&s->pb, 8, 0); /* Ah/Al (not used) */

    //FIXME DC/AC entropy table selectors stuff in jpegls
}

static void escape_FF(MpegEncContext *s, int start)
{
    int size= put_bits_count(&s->pb) - start*8;
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

void ff_mjpeg_stuffing(PutBitContext * pbc)
{
    int length;
    length= (-put_bits_count(pbc))&7;
    if(length) put_bits(pbc, length, (1<<length)-1);
}

void mjpeg_picture_trailer(MpegEncContext *s)
{
    ff_mjpeg_stuffing(&s->pb);
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
    component = (n <= 3 ? 0 : (n&1) + 1);
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
    for(i=0;i<5;i++) {
        encode_block(s, block[i], i);
    }
    if (s->chroma_format == CHROMA_420) {
        encode_block(s, block[5], 5);
    } else {
        encode_block(s, block[6], 6);
        encode_block(s, block[5], 5);
        encode_block(s, block[7], 7);
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

    init_put_bits(&s->pb, buf, buf_size);

    *p = *pict;
    p->pict_type= FF_I_TYPE;
    p->key_frame= 1;

    mjpeg_picture_header(s);

    s->header_bits= put_bits_count(&s->pb);

    if(avctx->pix_fmt == PIX_FMT_RGB32){
        int x, y, i;
        const int linesize= p->linesize[0];
        uint16_t (*buffer)[4]= (void *) s->rd_scratchpad;
        int left[3], top[3], topleft[3];

        for(i=0; i<3; i++){
            buffer[0][i]= 1 << (9 - 1);
        }

        for(y = 0; y < height; y++) {
            const int modified_predictor= y ? predictor : 1;
            uint8_t *ptr = p->data[0] + (linesize * y);

            if(s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < width*3*4){
                av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
                return -1;
            }

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
            if(s->pb.buf_end - s->pb.buf - (put_bits_count(&s->pb)>>3) < mb_width * 4 * 3 * s->mjpeg_hsample[0] * s->mjpeg_vsample[0]){
                av_log(s->avctx, AV_LOG_ERROR, "encoded frame too large\n");
                return -1;
            }
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
//    return (put_bits_count(&f->pb)+7)/8;
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

    int org_height;  /* size given at codec init */
    int first_picture;    /* true if decoding first picture */
    int interlaced;     /* true if interlaced */
    int bottom_field;   /* true if bottom field */
    int lossless;
    int ls;
    int progressive;
    int rgb;
    int rct;            /* standard rct */
    int pegasus_rct;    /* pegasus reversible colorspace transform */
    int bits;           /* bits per component */

    int maxval;
    int near;         ///< near lossless bound (si 0 for lossless)
    int t1,t2,t3;
    int reset;        ///< context halfing intervall ?rename

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
    AVFrame picture; /* picture structure */
    int linesize[MAX_COMPONENTS];                   ///< linesize << interlaced
    int8_t *qscale_table;
    DECLARE_ALIGNED_8(DCTELEM, block[64]);
    ScanTable scantable;
    void (*idct_put)(uint8_t *dest/*align 8*/, int line_size, DCTELEM *block/*align 16*/);
    void (*idct_add)(uint8_t *dest/*align 8*/, int line_size, DCTELEM *block/*align 16*/);

    int restart_interval;
    int restart_count;

    int buggy_avid;
    int cs_itu601;
    int interlace_polarity;

    int mjpb_skiptosod;

    int cur_scan; /* current scan, used by JPEG-LS */
} MJpegDecodeContext;

#include "jpeg_ls.c" //FIXME make jpeg-ls more independent

static int mjpeg_decode_dht(MJpegDecodeContext *s);

static int build_vlc(VLC *vlc, const uint8_t *bits_table, const uint8_t *val_table,
                      int nb_codes, int use_static, int is_ac)
{
    uint8_t huff_size[256+16];
    uint16_t huff_code[256+16];

    assert(nb_codes <= 256);

    memset(huff_size, 0, sizeof(huff_size));
    build_huffman_codes(huff_size, huff_code, bits_table, val_table);

    if(is_ac){
        memmove(huff_size+16, huff_size, sizeof(uint8_t)*nb_codes);
        memmove(huff_code+16, huff_code, sizeof(uint16_t)*nb_codes);
        memset(huff_size, 0, sizeof(uint8_t)*16);
        memset(huff_code, 0, sizeof(uint16_t)*16);
        nb_codes += 16;
    }

    return init_vlc(vlc, 9, nb_codes, huff_size, 1, 1, huff_code, 2, 2, use_static);
}

static int mjpeg_decode_init(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;
    MpegEncContext s2;
    memset(s, 0, sizeof(MJpegDecodeContext));

    s->avctx = avctx;

    /* ugly way to get the idct & scantable FIXME */
    memset(&s2, 0, sizeof(MpegEncContext));
    s2.avctx= avctx;
//    s2->out_format = FMT_MJPEG;
    dsputil_init(&s2.dsp, avctx);
    DCT_common_init(&s2);

    s->scantable= s2.intra_scantable;
    s->idct_put= s2.dsp.idct_put;
    s->idct_add= s2.dsp.idct_add;

    s->mpeg_enc_ctx_allocated = 0;
    s->buffer_size = 0;
    s->buffer = NULL;
    s->start_code = -1;
    s->first_picture = 1;
    s->org_height = avctx->coded_height;

    build_vlc(&s->vlcs[0][0], bits_dc_luminance, val_dc_luminance, 12, 0, 0);
    build_vlc(&s->vlcs[0][1], bits_dc_chrominance, val_dc_chrominance, 12, 0, 0);
    build_vlc(&s->vlcs[1][0], bits_ac_luminance, val_ac_luminance, 251, 0, 1);
    build_vlc(&s->vlcs[1][1], bits_ac_chrominance, val_ac_chrominance, 251, 0, 1);

    if (avctx->flags & CODEC_FLAG_EXTERN_HUFF)
    {
        av_log(avctx, AV_LOG_INFO, "mjpeg: using external huffman table\n");
        init_get_bits(&s->gb, avctx->extradata, avctx->extradata_size*8);
        mjpeg_decode_dht(s);
        /* should check for error - but dunno */
    }
    if (avctx->extradata_size > 9 &&
        AV_RL32(avctx->extradata + 4) == MKTAG('f','i','e','l')) {
        if (avctx->extradata[9] == 6) { /* quicktime icefloe 019 */
            s->interlace_polarity = 1; /* bottom field first */
            av_log(avctx, AV_LOG_DEBUG, "mjpeg bottom field first\n");
        }
    }

    return 0;
}


/**
 * finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int find_frame_end(ParseContext *pc, const uint8_t *buf, int buf_size){
    int vop_found, i;
    uint16_t state;

    vop_found= pc->frame_start_found;
    state= pc->state;

    i=0;
    if(!vop_found){
        for(i=0; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if(state == 0xFFD8){
                i++;
                vop_found=1;
                break;
            }
        }
    }

    if(vop_found){
        /* EOF considered as end of frame */
        if (buf_size == 0)
            return 0;
        for(; i<buf_size; i++){
            state= (state<<8) | buf[i];
            if(state == 0xFFD8){
                pc->frame_start_found=0;
                pc->state=0;
                return i-1;
            }
        }
    }
    pc->frame_start_found= vop_found;
    pc->state= state;
    return END_NOT_FOUND;
}

static int jpeg_parse(AVCodecParserContext *s,
                           AVCodecContext *avctx,
                           uint8_t **poutbuf, int *poutbuf_size,
                           const uint8_t *buf, int buf_size)
{
    ParseContext *pc = s->priv_data;
    int next;

    next= find_frame_end(pc, buf, buf_size);

    if (ff_combine_frame(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }

    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
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
            av_log(s->avctx, AV_LOG_ERROR, "dqt: 16bit precision\n");
            return -1;
        }
        index = get_bits(&s->gb, 4);
        if (index >= 4)
            return -1;
        av_log(s->avctx, AV_LOG_DEBUG, "index=%d\n", index);
        /* read quant table */
        for(i=0;i<64;i++) {
            j = s->scantable.permutated[i];
            s->quant_matrixes[index][j] = get_bits(&s->gb, 8);
        }

        //XXX FIXME finetune, and perhaps add dc too
        s->qscale[index]= FFMAX(
            s->quant_matrixes[index][s->scantable.permutated[1]],
            s->quant_matrixes[index][s->scantable.permutated[8]]) >> 1;
        av_log(s->avctx, AV_LOG_DEBUG, "qscale[%d]: %d\n", index, s->qscale[index]);
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
        av_log(s->avctx, AV_LOG_DEBUG, "class=%d index=%d nb_codes=%d\n",
               class, index, code_max + 1);
        if(build_vlc(&s->vlcs[class][index], bits_table, val_table, code_max + 1, 0, class > 0) < 0){
            return -1;
        }
    }
    return 0;
}

static int mjpeg_decode_sof(MJpegDecodeContext *s)
{
    int len, nb_components, i, width, height, pix_fmt_id;

    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    s->bits= get_bits(&s->gb, 8);

    if(s->pegasus_rct) s->bits=9;
    if(s->bits==9 && !s->pegasus_rct) s->rct=1;    //FIXME ugly

    if (s->bits != 8 && !s->lossless){
        av_log(s->avctx, AV_LOG_ERROR, "only 8 bits/component accepted\n");
        return -1;
    }

    height = get_bits(&s->gb, 16);
    width = get_bits(&s->gb, 16);

    av_log(s->avctx, AV_LOG_DEBUG, "sof0: picture: %dx%d\n", width, height);
    if(avcodec_check_dimensions(s->avctx, width, height))
        return -1;

    nb_components = get_bits(&s->gb, 8);
    if (nb_components <= 0 ||
        nb_components > MAX_COMPONENTS)
        return -1;
    if (s->ls && !(s->bits <= 8 || nb_components == 1)){
        av_log(s->avctx, AV_LOG_ERROR, "only <= 8 bits/component or 16-bit gray accepted for JPEG-LS\n");
        return -1;
    }
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
        av_log(s->avctx, AV_LOG_DEBUG, "component %d %d:%d id: %d quant:%d\n", i, s->h_count[i],
               s->v_count[i], s->component_id[i], s->quant_index[i]);
    }

    if(s->ls && (s->h_max > 1 || s->v_max > 1)) {
        av_log(s->avctx, AV_LOG_ERROR, "Subsampling in JPEG-LS is not supported.\n");
        return -1;
    }

    if(s->v_max==1 && s->h_max==1 && s->lossless==1) s->rgb=1;

    /* if different size, realloc/alloc picture */
    /* XXX: also check h_count and v_count */
    if (width != s->width || height != s->height) {
        av_freep(&s->qscale_table);

        s->width = width;
        s->height = height;

        /* test interlaced mode */
        if (s->first_picture &&
            s->org_height != 0 &&
            s->height < ((s->org_height * 3) / 4)) {
            s->interlaced = 1;
            s->bottom_field = s->interlace_polarity;
            s->picture.interlaced_frame = 1;
            s->picture.top_field_first = !s->interlace_polarity;
            height *= 2;
        }

        avcodec_set_dimensions(s->avctx, width, height);

        s->qscale_table= av_mallocz((s->width+15)/16);

        s->first_picture = 0;
    }

    if(s->interlaced && (s->bottom_field == !s->interlace_polarity))
        return 0;

    /* XXX: not complete test ! */
    pix_fmt_id = (s->h_count[0] << 20) | (s->v_count[0] << 16) |
                 (s->h_count[1] << 12) | (s->v_count[1] <<  8) |
                 (s->h_count[2] <<  4) |  s->v_count[2];
    av_log(s->avctx, AV_LOG_DEBUG, "pix fmt id %x\n", pix_fmt_id);
    switch(pix_fmt_id){
    case 0x222222:
    case 0x111111:
        if(s->rgb){
            s->avctx->pix_fmt = PIX_FMT_RGB32;
        }else if(s->nb_components==3)
            s->avctx->pix_fmt = s->cs_itu601 ? PIX_FMT_YUV444P : PIX_FMT_YUVJ444P;
        else
            s->avctx->pix_fmt = PIX_FMT_GRAY8;
        break;
    case 0x211111:
    case 0x221212:
        s->avctx->pix_fmt = s->cs_itu601 ? PIX_FMT_YUV422P : PIX_FMT_YUVJ422P;
        break;
    default:
    case 0x221111:
        s->avctx->pix_fmt = s->cs_itu601 ? PIX_FMT_YUV420P : PIX_FMT_YUVJ420P;
        break;
    }
    if(s->ls){
        if(s->nb_components > 1)
            s->avctx->pix_fmt = PIX_FMT_RGB24;
        else if(s->bits <= 8)
            s->avctx->pix_fmt = PIX_FMT_GRAY8;
        else
            s->avctx->pix_fmt = PIX_FMT_GRAY16;
    }

    if(s->picture.data[0])
        s->avctx->release_buffer(s->avctx, &s->picture);

    s->picture.reference= 0;
    if(s->avctx->get_buffer(s->avctx, &s->picture) < 0){
        av_log(s->avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    s->picture.pict_type= I_TYPE;
    s->picture.key_frame= 1;

    for(i=0; i<3; i++){
        s->linesize[i]= s->picture.linesize[i] << s->interlaced;
    }

//    printf("%d %d %d %d %d %d\n", s->width, s->height, s->linesize[0], s->linesize[1], s->interlaced, s->avctx->height);

    if (len != (8+(3*nb_components)))
    {
        av_log(s->avctx, AV_LOG_DEBUG, "decode_sof0: error, len(%d) mismatch\n", len);
    }

    /* totally blank picture as progressive JPEG will only add details to it */
    if(s->progressive){
        memset(s->picture.data[0], 0, s->picture.linesize[0] * s->height);
        memset(s->picture.data[1], 0, s->picture.linesize[1] * s->height >> (s->v_max - s->v_count[1]));
        memset(s->picture.data[2], 0, s->picture.linesize[2] * s->height >> (s->v_max - s->v_count[2]));
    }
    return 0;
}

static inline int mjpeg_decode_dc(MJpegDecodeContext *s, int dc_index)
{
    int code;
    code = get_vlc2(&s->gb, s->vlcs[0][dc_index].table, 9, 2);
    if (code < 0)
    {
        av_log(s->avctx, AV_LOG_WARNING, "mjpeg_decode_dc: bad vlc: %d:%d (%p)\n", 0, dc_index,
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
                        int component, int dc_index, int ac_index, int16_t *quant_matrix)
{
    int code, i, j, level, val;

    /* DC coef */
    val = mjpeg_decode_dc(s, dc_index);
    if (val == 0xffff) {
        av_log(s->avctx, AV_LOG_ERROR, "error dc\n");
        return -1;
    }
    val = val * quant_matrix[0] + s->last_dc[component];
    s->last_dc[component] = val;
    block[0] = val;
    /* AC coefs */
    i = 0;
    {OPEN_READER(re, &s->gb)
    for(;;) {
        UPDATE_CACHE(re, &s->gb);
        GET_VLC(code, re, &s->gb, s->vlcs[1][ac_index].table, 9, 2)

        /* EOB */
        if (code == 0x10)
            break;
        i += ((unsigned)code) >> 4;
        if(code != 0x100){
            code &= 0xf;
            if(code > MIN_CACHE_BITS - 16){
                UPDATE_CACHE(re, &s->gb)
            }
            {
                int cache=GET_CACHE(re,&s->gb);
                int sign=(~cache)>>31;
                level = (NEG_USR32(sign ^ cache,code) ^ sign) - sign;
            }

            LAST_SKIP_BITS(re, &s->gb, code)

            if (i >= 63) {
                if(i == 63){
                    j = s->scantable.permutated[63];
                    block[j] = level * quant_matrix[j];
                    break;
                }
                av_log(s->avctx, AV_LOG_ERROR, "error count: %d\n", i);
                return -1;
            }
            j = s->scantable.permutated[i];
            block[j] = level * quant_matrix[j];
        }
    }
    CLOSE_READER(re, &s->gb)}

    return 0;
}

/* decode block and dequantize - progressive JPEG version */
static int decode_block_progressive(MJpegDecodeContext *s, DCTELEM *block,
                        int component, int dc_index, int ac_index, int16_t *quant_matrix,
                        int ss, int se, int Ah, int Al, int *EOBRUN)
{
    int code, i, j, level, val, run;

    /* DC coef */
    if(!ss){
        val = mjpeg_decode_dc(s, dc_index);
        if (val == 0xffff) {
            av_log(s->avctx, AV_LOG_ERROR, "error dc\n");
            return -1;
        }
        val = (val * quant_matrix[0] << Al) + s->last_dc[component];
    }else
        val = 0;
    s->last_dc[component] = val;
    block[0] = val;
    if(!se) return 0;
    /* AC coefs */
    if(*EOBRUN){
        (*EOBRUN)--;
        return 0;
    }
    {OPEN_READER(re, &s->gb)
    for(i=ss;;i++) {
        UPDATE_CACHE(re, &s->gb);
        GET_VLC(code, re, &s->gb, s->vlcs[1][ac_index].table, 9, 2)
        /* Progressive JPEG use AC coeffs from zero and this decoder sets offset 16 by default */
        code -= 16;
        if(code & 0xF) {
            i += ((unsigned) code) >> 4;
            code &= 0xf;
            if(code > MIN_CACHE_BITS - 16){
                UPDATE_CACHE(re, &s->gb)
            }
            {
                int cache=GET_CACHE(re,&s->gb);
                int sign=(~cache)>>31;
                level = (NEG_USR32(sign ^ cache,code) ^ sign) - sign;
            }

            LAST_SKIP_BITS(re, &s->gb, code)

            if (i >= se) {
                if(i == se){
                    j = s->scantable.permutated[se];
                    block[j] = level * quant_matrix[j] << Al;
                    break;
                }
                av_log(s->avctx, AV_LOG_ERROR, "error count: %d\n", i);
                return -1;
            }
            j = s->scantable.permutated[i];
            block[j] = level * quant_matrix[j] << Al;
        }else{
            run = ((unsigned) code) >> 4;
            if(run == 0xF){// ZRL - skip 15 coefficients
                i += 15;
            }else{
                val = run;
                run = (1 << run);
                UPDATE_CACHE(re, &s->gb);
                run += (GET_CACHE(re, &s->gb) >> (32 - val)) & (run - 1);
                if(val)
                    LAST_SKIP_BITS(re, &s->gb, val);
                *EOBRUN = run - 1;
                break;
            }
        }
    }
    CLOSE_READER(re, &s->gb)}

    return 0;
}

static int ljpeg_decode_rgb_scan(MJpegDecodeContext *s, int predictor, int point_transform){
    int i, mb_x, mb_y;
    uint16_t buffer[32768][4];
    int left[3], top[3], topleft[3];
    const int linesize= s->linesize[0];
    const int mask= (1<<s->bits)-1;

    if((unsigned)s->mb_width > 32768) //dynamic alloc
        return -1;

    for(i=0; i<3; i++){
        buffer[0][i]= 1 << (s->bits + point_transform - 1);
    }
    for(mb_y = 0; mb_y < s->mb_height; mb_y++) {
        const int modified_predictor= mb_y ? predictor : 1;
        uint8_t *ptr = s->picture.data[0] + (linesize * mb_y);

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

                        ptr = s->picture.data[c] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
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

                        ptr = s->picture.data[c] + (linesize * (v * mb_y + y)) + (h * mb_x + x); //FIXME optimize this crap
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

static int mjpeg_decode_scan(MJpegDecodeContext *s, int nb_components, int ss, int se, int Ah, int Al){
    int i, mb_x, mb_y;
    int EOBRUN = 0;

    if(Ah) return 0; /* TODO decode refinement planes too */
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
                    if (!s->progressive && decode_block(s, s->block, i,
                                     s->dc_index[i], s->ac_index[i],
                                     s->quant_matrixes[ s->quant_index[c] ]) < 0) {
                        av_log(s->avctx, AV_LOG_ERROR, "error y=%d x=%d\n", mb_y, mb_x);
                        return -1;
                    }
                    if (s->progressive && decode_block_progressive(s, s->block, i,
                                     s->dc_index[i], s->ac_index[i],
                                     s->quant_matrixes[ s->quant_index[c] ], ss, se, Ah, Al, &EOBRUN) < 0) {
                        av_log(s->avctx, AV_LOG_ERROR, "error y=%d x=%d\n", mb_y, mb_x);
                        return -1;
                    }
//                    av_log(s->avctx, AV_LOG_DEBUG, "mb: %d %d processed\n", mb_y, mb_x);
                    ptr = s->picture.data[c] +
                        (((s->linesize[c] * (v * mb_y + y) * 8) +
                        (h * mb_x + x) * 8) >> s->avctx->lowres);
                    if (s->interlaced && s->bottom_field)
                        ptr += s->linesize[c] >> 1;
//av_log(NULL, AV_LOG_DEBUG, "%d %d %d %d %d %d %d %d \n", mb_x, mb_y, x, y, c, s->bottom_field, (v * mb_y + y) * 8, (h * mb_x + x) * 8);
                    if(!s->progressive)
                        s->idct_put(ptr, s->linesize[c], s->block);
                    else
                        s->idct_add(ptr, s->linesize[c], s->block);
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
    int ilv, prev_shift;

    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    nb_components = get_bits(&s->gb, 8);
    if (len != 6+2*nb_components)
    {
        av_log(s->avctx, AV_LOG_ERROR, "decode_sos: invalid len (%d)\n", len);
        return -1;
    }
    vmax = 0;
    hmax = 0;
    for(i=0;i<nb_components;i++) {
        id = get_bits(&s->gb, 8) - 1;
        av_log(s->avctx, AV_LOG_DEBUG, "component: %d\n", id);
        /* find component index */
        for(index=0;index<s->nb_components;index++)
            if (id == s->component_id[index])
                break;
        if (index == s->nb_components)
        {
            av_log(s->avctx, AV_LOG_ERROR, "decode_sos: index(%d) out of components\n", index);
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

    predictor= get_bits(&s->gb, 8); /* JPEG Ss / lossless JPEG predictor /JPEG-LS NEAR */
    ilv= get_bits(&s->gb, 8);    /* JPEG Se / JPEG-LS ILV */
    prev_shift = get_bits(&s->gb, 4); /* Ah */
    point_transform= get_bits(&s->gb, 4); /* Al */

    for(i=0;i<nb_components;i++)
        s->last_dc[i] = 1024;

    if (nb_components > 1) {
        /* interleaved stream */
        s->mb_width  = (s->width  + s->h_max * block_size - 1) / (s->h_max * block_size);
        s->mb_height = (s->height + s->v_max * block_size - 1) / (s->v_max * block_size);
    } else if(!s->ls) { /* skip this for JPEG-LS */
        h = s->h_max / s->h_scount[0];
        v = s->v_max / s->v_scount[0];
        s->mb_width  = (s->width  + h * block_size - 1) / (h * block_size);
        s->mb_height = (s->height + v * block_size - 1) / (v * block_size);
        s->nb_blocks[0] = 1;
        s->h_scount[0] = 1;
        s->v_scount[0] = 1;
    }

    if(s->avctx->debug & FF_DEBUG_PICT_INFO)
        av_log(s->avctx, AV_LOG_DEBUG, "%s %s p:%d >>:%d ilv:%d bits:%d %s\n", s->lossless ? "lossless" : "sequencial DCT", s->rgb ? "RGB" : "",
               predictor, point_transform, ilv, s->bits,
               s->pegasus_rct ? "PRCT" : (s->rct ? "RCT" : ""));


    /* mjpeg-b can have padding bytes between sos and image data, skip them */
    for (i = s->mjpb_skiptosod; i > 0; i--)
        skip_bits(&s->gb, 8);

    if(s->lossless){
        if(s->ls){
//            for(){
//            reset_ls_coding_parameters(s, 0);

            ls_decode_picture(s, predictor, point_transform, ilv);
        }else{
            if(s->rgb){
                if(ljpeg_decode_rgb_scan(s, predictor, point_transform) < 0)
                    return -1;
            }else{
                if(ljpeg_decode_yuv_scan(s, predictor, point_transform) < 0)
                    return -1;
            }
        }
    }else{
        if(mjpeg_decode_scan(s, nb_components, predictor, ilv, prev_shift, point_transform) < 0)
            return -1;
    }
    emms_c();
    return 0;
 out_of_range:
    av_log(s->avctx, AV_LOG_ERROR, "decode_sos: ac/dc index out of range\n");
    return -1;
}

static int mjpeg_decode_dri(MJpegDecodeContext *s)
{
    if (get_bits(&s->gb, 16) != 4)
        return -1;
    s->restart_interval = get_bits(&s->gb, 16);
    s->restart_count = 0;
    av_log(s->avctx, AV_LOG_DEBUG, "restart interval: %d\n", s->restart_interval);

    return 0;
}

static int mjpeg_decode_app(MJpegDecodeContext *s)
{
    int len, id;

    len = get_bits(&s->gb, 16);
    if (len < 5)
        return -1;
    if(8*len + get_bits_count(&s->gb) > s->gb.size_in_bits)
        return -1;

    id = (get_bits(&s->gb, 16) << 16) | get_bits(&s->gb, 16);
    id = be2me_32(id);
    len -= 6;

    if(s->avctx->debug & FF_DEBUG_STARTCODE){
        av_log(s->avctx, AV_LOG_DEBUG, "APPx %8X\n", id);
    }

    /* buggy AVID, it puts EOI only at every 10th frame */
    /* also this fourcc is used by non-avid files too, it holds some
       informations, but it's always present in AVID creates files */
    if (id == ff_get_fourcc("AVI1"))
    {
        /* structure:
            4bytes      AVI1
            1bytes      polarity
            1bytes      always zero
            4bytes      field_size
            4bytes      field_size_less_padding
        */
            s->buggy_avid = 1;
//        if (s->first_picture)
//            printf("mjpeg: workarounding buggy AVID\n");
        s->interlace_polarity = get_bits(&s->gb, 8);
#if 0
        skip_bits(&s->gb, 8);
        skip_bits(&s->gb, 32);
        skip_bits(&s->gb, 32);
        len -= 10;
#endif
//        if (s->interlace_polarity)
//            printf("mjpeg: interlace polarity: %d\n", s->interlace_polarity);
        goto out;
    }

//    len -= 2;

    if (id == ff_get_fourcc("JFIF"))
    {
        int t_w, t_h, v1, v2;
        skip_bits(&s->gb, 8); /* the trailing zero-byte */
        v1= get_bits(&s->gb, 8);
        v2= get_bits(&s->gb, 8);
        skip_bits(&s->gb, 8);

        s->avctx->sample_aspect_ratio.num= get_bits(&s->gb, 16);
        s->avctx->sample_aspect_ratio.den= get_bits(&s->gb, 16);

        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO, "mjpeg: JFIF header found (version: %x.%x) SAR=%d/%d\n",
                v1, v2,
                s->avctx->sample_aspect_ratio.num,
                s->avctx->sample_aspect_ratio.den
            );

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
        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO, "mjpeg: Adobe header found\n");
        skip_bits(&s->gb, 16); /* version */
        skip_bits(&s->gb, 16); /* flags0 */
        skip_bits(&s->gb, 16); /* flags1 */
        skip_bits(&s->gb, 8);  /* transform */
        len -= 7;
        goto out;
    }

    if (id == ff_get_fourcc("LJIF")){
        if (s->avctx->debug & FF_DEBUG_PICT_INFO)
            av_log(s->avctx, AV_LOG_INFO, "Pegasus lossless jpeg header found\n");
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
            av_log(s->avctx, AV_LOG_ERROR, "unknown colorspace\n");
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
            if (s->avctx->debug & FF_DEBUG_PICT_INFO)
                av_log(s->avctx, AV_LOG_INFO, "mjpeg: Apple MJPEG-A header found\n");
        }
    }

out:
    /* slow but needed for extreme adobe jpegs */
    if (len < 0)
        av_log(s->avctx, AV_LOG_ERROR, "mjpeg: error, decode_app parser read over the end\n");
    while(--len > 0)
        skip_bits(&s->gb, 8);

    return 0;
}

static int mjpeg_decode_com(MJpegDecodeContext *s)
{
    int len = get_bits(&s->gb, 16);
    if (len >= 2 && 8*len - 16 + get_bits_count(&s->gb) <= s->gb.size_in_bits) {
        char *cbuf = av_malloc(len - 1);
        if (cbuf) {
            int i;
            for (i = 0; i < len - 2; i++)
                cbuf[i] = get_bits(&s->gb, 8);
            if (i > 0 && cbuf[i-1] == '\n')
                cbuf[i-1] = 0;
            else
                cbuf[i] = 0;

            if(s->avctx->debug & FF_DEBUG_PICT_INFO)
                av_log(s->avctx, AV_LOG_INFO, "mjpeg comment: '%s'\n", cbuf);

            /* buggy avid, it puts EOI only at every 10th frame */
            if (!strcmp(cbuf, "AVID"))
            {
                s->buggy_avid = 1;
                //        if (s->first_picture)
                //            printf("mjpeg: workarounding buggy AVID\n");
            }
            else if(!strcmp(cbuf, "CS=ITU601")){
                s->cs_itu601= 1;
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
        if ((v == 0xff) && (v2 >= 0xc0) && (v2 <= 0xfe) && buf_ptr < buf_end) {
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
    av_log(NULL, AV_LOG_VERBOSE, "find_marker skipped %d bytes\n", skipped);
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
    int start_code;
    AVFrame *picture = data;

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
                av_log(avctx, AV_LOG_DEBUG, "marker=%x avail_size_in_buf=%d\n", start_code, buf_end - buf_ptr);

                if ((buf_end - buf_ptr) > s->buffer_size)
                {
                    av_free(s->buffer);
                    s->buffer_size = buf_end-buf_ptr;
                    s->buffer = av_malloc(s->buffer_size + FF_INPUT_BUFFER_PADDING_SIZE);
                    av_log(avctx, AV_LOG_DEBUG, "buffer too small, expanding to %d bytes\n",
                        s->buffer_size);
                }

                /* unescape buffer of SOS, use special treatment for JPEG-LS */
                if (start_code == SOS && !s->ls)
                {
                    uint8_t *src = buf_ptr;
                    uint8_t *dst = s->buffer;

                    while (src<buf_end)
                    {
                        uint8_t x = *(src++);

                        *(dst++) = x;
                        if (avctx->codec_id != CODEC_ID_THP)
                        {
                            if (x == 0xff) {
                                while (src < buf_end && x == 0xff)
                                    x = *(src++);

                                if (x >= 0xd0 && x <= 0xd7)
                                    *(dst++) = x;
                                else if (x)
                                    break;
                            }
                        }
                    }
                    init_get_bits(&s->gb, s->buffer, (dst - s->buffer)*8);

                    av_log(avctx, AV_LOG_DEBUG, "escaping removed %d bytes\n",
                           (buf_end - buf_ptr) - (dst - s->buffer));
                }
                else if(start_code == SOS && s->ls){
                    uint8_t *src = buf_ptr;
                    uint8_t *dst = s->buffer;
                    int bit_count = 0;
                    int t = 0, b = 0;
                    PutBitContext pb;

                    s->cur_scan++;

                    /* find marker */
                    while (src + t < buf_end){
                        uint8_t x = src[t++];
                        if (x == 0xff){
                            while((src + t < buf_end) && x == 0xff)
                                x = src[t++];
                            if (x & 0x80) {
                                t -= 2;
                                break;
                            }
                        }
                    }
                    bit_count = t * 8;

                    init_put_bits(&pb, dst, t);

                    /* unescape bitstream */
                    while(b < t){
                        uint8_t x = src[b++];
                        put_bits(&pb, 8, x);
                        if(x == 0xFF){
                            x = src[b++];
                            put_bits(&pb, 7, x);
                            bit_count--;
                        }
                    }
                    flush_put_bits(&pb);

                    init_get_bits(&s->gb, dst, bit_count);
                }
                else
                    init_get_bits(&s->gb, buf_ptr, (buf_end - buf_ptr)*8);

                s->start_code = start_code;
                if(s->avctx->debug & FF_DEBUG_STARTCODE){
                    av_log(avctx, AV_LOG_DEBUG, "startcode: %X\n", start_code);
                }

                /* process markers */
                if (start_code >= 0xd0 && start_code <= 0xd7) {
                    av_log(avctx, AV_LOG_DEBUG, "restart marker: %d\n", start_code&0x0f);
                    /* APP fields */
                } else if (start_code >= APP0 && start_code <= APP15) {
                    mjpeg_decode_app(s);
                    /* Comment */
                } else if (start_code == COM){
                    mjpeg_decode_com(s);
                }

                switch(start_code) {
                case SOI:
                    s->restart_interval = 0;

                    s->restart_count = 0;
                    /* nothing to do on SOI */
                    break;
                case DQT:
                    mjpeg_decode_dqt(s);
                    break;
                case DHT:
                    if(mjpeg_decode_dht(s) < 0){
                        av_log(avctx, AV_LOG_ERROR, "huffman table decode error\n");
                        return -1;
                    }
                    break;
                case SOF0:
                    s->lossless=0;
                    s->ls=0;
                    s->progressive=0;
                    if (mjpeg_decode_sof(s) < 0)
                        return -1;
                    break;
                case SOF2:
                    s->lossless=0;
                    s->ls=0;
                    s->progressive=1;
                    if (mjpeg_decode_sof(s) < 0)
                        return -1;
                    break;
                case SOF3:
                    s->lossless=1;
                    s->ls=0;
                    s->progressive=0;
                    if (mjpeg_decode_sof(s) < 0)
                        return -1;
                    break;
                case SOF48:
                    s->lossless=1;
                    s->ls=1;
                    s->progressive=0;
                    if (mjpeg_decode_sof(s) < 0)
                        return -1;
                    break;
                case LSE:
                    if (decode_lse(s) < 0)
                        return -1;
                    break;
                case EOI:
                    s->cur_scan = 0;
                    if ((s->buggy_avid && !s->interlaced) || s->restart_interval)
                        break;
eoi_parser:
                    {
                        if (s->interlaced) {
                            s->bottom_field ^= 1;
                            /* if not bottom field, do not output image yet */
                            if (s->bottom_field == !s->interlace_polarity)
                                goto not_the_end;
                        }
                        *picture = s->picture;
                        *data_size = sizeof(AVFrame);

                        if(!s->lossless){
                            picture->quality= FFMAX(FFMAX(s->qscale[0], s->qscale[1]), s->qscale[2]);
                            picture->qstride= 0;
                            picture->qscale_table= s->qscale_table;
                            memset(picture->qscale_table, picture->quality, (s->width+15)/16);
                            if(avctx->debug & FF_DEBUG_QP)
                                av_log(avctx, AV_LOG_DEBUG, "QP: %d\n", picture->quality);
                            picture->quality*= FF_QP2LAMBDA;
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
                    av_log(avctx, AV_LOG_ERROR, "mjpeg: unsupported coding type (%x)\n", start_code);
                    break;
//                default:
//                    printf("mjpeg: unsupported marker (%x)\n", start_code);
//                    break;
                }

not_the_end:
                /* eof process start code */
                buf_ptr += (get_bits_count(&s->gb)+7)/8;
                av_log(avctx, AV_LOG_DEBUG, "marker parser used %d bytes (%d bits)\n",
                       (get_bits_count(&s->gb)+7)/8, get_bits_count(&s->gb));
            }
        }
    }
the_end:
    av_log(avctx, AV_LOG_DEBUG, "mjpeg decode frame unused %d bytes\n", buf_end - buf_ptr);
//    return buf_end - buf_ptr;
    return buf_ptr - buf;
}

static int mjpegb_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              uint8_t *buf, int buf_size)
{
    MJpegDecodeContext *s = avctx->priv_data;
    uint8_t *buf_end, *buf_ptr;
    AVFrame *picture = data;
    GetBitContext hgb; /* for the header */
    uint32_t dqt_offs, dht_offs, sof_offs, sos_offs, second_field_offs;
    uint32_t field_size, sod_offs;

    buf_ptr = buf;
    buf_end = buf + buf_size;

read_header:
    /* reset on every SOI */
    s->restart_interval = 0;
    s->restart_count = 0;
    s->mjpb_skiptosod = 0;

    init_get_bits(&hgb, buf_ptr, /*buf_size*/(buf_end - buf_ptr)*8);

    skip_bits(&hgb, 32); /* reserved zeros */

    if (get_bits_long(&hgb, 32) != MKBETAG('m','j','p','g'))
    {
        av_log(avctx, AV_LOG_WARNING, "not mjpeg-b (bad fourcc)\n");
        return 0;
    }

    field_size = get_bits_long(&hgb, 32); /* field size */
    av_log(avctx, AV_LOG_DEBUG, "field size: 0x%x\n", field_size);
    skip_bits(&hgb, 32); /* padded field size */
    second_field_offs = get_bits_long(&hgb, 32);
    av_log(avctx, AV_LOG_DEBUG, "second field offs: 0x%x\n", second_field_offs);
    if (second_field_offs)
        s->interlaced = 1;

    dqt_offs = get_bits_long(&hgb, 32);
    av_log(avctx, AV_LOG_DEBUG, "dqt offs: 0x%x\n", dqt_offs);
    if (dqt_offs)
    {
        init_get_bits(&s->gb, buf+dqt_offs, (buf_end - (buf+dqt_offs))*8);
        s->start_code = DQT;
        mjpeg_decode_dqt(s);
    }

    dht_offs = get_bits_long(&hgb, 32);
    av_log(avctx, AV_LOG_DEBUG, "dht offs: 0x%x\n", dht_offs);
    if (dht_offs)
    {
        init_get_bits(&s->gb, buf+dht_offs, (buf_end - (buf+dht_offs))*8);
        s->start_code = DHT;
        mjpeg_decode_dht(s);
    }

    sof_offs = get_bits_long(&hgb, 32);
    av_log(avctx, AV_LOG_DEBUG, "sof offs: 0x%x\n", sof_offs);
    if (sof_offs)
    {
        init_get_bits(&s->gb, buf+sof_offs, (buf_end - (buf+sof_offs))*8);
        s->start_code = SOF0;
        if (mjpeg_decode_sof(s) < 0)
            return -1;
    }

    sos_offs = get_bits_long(&hgb, 32);
    av_log(avctx, AV_LOG_DEBUG, "sos offs: 0x%x\n", sos_offs);
    sod_offs = get_bits_long(&hgb, 32);
    av_log(avctx, AV_LOG_DEBUG, "sod offs: 0x%x\n", sod_offs);
    if (sos_offs)
    {
//        init_get_bits(&s->gb, buf+sos_offs, (buf_end - (buf+sos_offs))*8);
        init_get_bits(&s->gb, buf+sos_offs, field_size*8);
        s->mjpb_skiptosod = (sod_offs - sos_offs - show_bits(&s->gb, 16));
        s->start_code = SOS;
        mjpeg_decode_sos(s);
    }

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

    *picture= s->picture;
    *data_size = sizeof(AVFrame);

    if(!s->lossless){
        picture->quality= FFMAX(FFMAX(s->qscale[0], s->qscale[1]), s->qscale[2]);
        picture->qstride= 0;
        picture->qscale_table= s->qscale_table;
        memset(picture->qscale_table, picture->quality, (s->width+15)/16);
        if(avctx->debug & FF_DEBUG_QP)
            av_log(avctx, AV_LOG_DEBUG, "QP: %d\n", picture->quality);
        picture->quality*= FF_QP2LAMBDA;
    }

    return buf_ptr - buf;
}

#include "sp5x.h"

static int sp5x_decode_frame(AVCodecContext *avctx,
                              void *data, int *data_size,
                              uint8_t *buf, int buf_size)
{
#if 0
    MJpegDecodeContext *s = avctx->priv_data;
#endif
    const int qscale = 5;
    uint8_t *buf_ptr, *buf_end, *recoded;
    int i = 0, j = 0;

    if (!avctx->width || !avctx->height)
        return -1;

    buf_ptr = buf;
    buf_end = buf + buf_size;

#if 1
    recoded = av_mallocz(buf_size + 1024);
    if (!recoded)
        return -1;

    /* SOI */
    recoded[j++] = 0xFF;
    recoded[j++] = 0xD8;

    memcpy(recoded+j, &sp5x_data_dqt[0], sizeof(sp5x_data_dqt));
    memcpy(recoded+j+5, &sp5x_quant_table[qscale * 2], 64);
    memcpy(recoded+j+70, &sp5x_quant_table[(qscale * 2) + 1], 64);
    j += sizeof(sp5x_data_dqt);

    memcpy(recoded+j, &sp5x_data_dht[0], sizeof(sp5x_data_dht));
    j += sizeof(sp5x_data_dht);

    memcpy(recoded+j, &sp5x_data_sof[0], sizeof(sp5x_data_sof));
    recoded[j+5] = (avctx->coded_height >> 8) & 0xFF;
    recoded[j+6] = avctx->coded_height & 0xFF;
    recoded[j+7] = (avctx->coded_width >> 8) & 0xFF;
    recoded[j+8] = avctx->coded_width & 0xFF;
    j += sizeof(sp5x_data_sof);

    memcpy(recoded+j, &sp5x_data_sos[0], sizeof(sp5x_data_sos));
    j += sizeof(sp5x_data_sos);

    for (i = 14; i < buf_size && j < buf_size+1024-2; i++)
    {
        recoded[j++] = buf[i];
        if (buf[i] == 0xff)
            recoded[j++] = 0;
    }

    /* EOI */
    recoded[j++] = 0xFF;
    recoded[j++] = 0xD9;

    i = mjpeg_decode_frame(avctx, data, data_size, recoded, j);

    av_free(recoded);

#else
    /* SOF */
    s->bits = 8;
    s->width  = avctx->coded_width;
    s->height = avctx->coded_height;
    s->nb_components = 3;
    s->component_id[0] = 0;
    s->h_count[0] = 2;
    s->v_count[0] = 2;
    s->quant_index[0] = 0;
    s->component_id[1] = 1;
    s->h_count[1] = 1;
    s->v_count[1] = 1;
    s->quant_index[1] = 1;
    s->component_id[2] = 2;
    s->h_count[2] = 1;
    s->v_count[2] = 1;
    s->quant_index[2] = 1;
    s->h_max = 2;
    s->v_max = 2;

    s->qscale_table = av_mallocz((s->width+15)/16);
    avctx->pix_fmt = s->cs_itu601 ? PIX_FMT_YUV420P : PIX_FMT_YUVJ420;
    s->interlaced = 0;

    s->picture.reference = 0;
    if (avctx->get_buffer(avctx, &s->picture) < 0)
    {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }

    s->picture.pict_type = I_TYPE;
    s->picture.key_frame = 1;

    for (i = 0; i < 3; i++)
        s->linesize[i] = s->picture.linesize[i] << s->interlaced;

    /* DQT */
    for (i = 0; i < 64; i++)
    {
        j = s->scantable.permutated[i];
        s->quant_matrixes[0][j] = sp5x_quant_table[(qscale * 2) + i];
    }
    s->qscale[0] = FFMAX(
        s->quant_matrixes[0][s->scantable.permutated[1]],
        s->quant_matrixes[0][s->scantable.permutated[8]]) >> 1;

    for (i = 0; i < 64; i++)
    {
        j = s->scantable.permutated[i];
        s->quant_matrixes[1][j] = sp5x_quant_table[(qscale * 2) + 1 + i];
    }
    s->qscale[1] = FFMAX(
        s->quant_matrixes[1][s->scantable.permutated[1]],
        s->quant_matrixes[1][s->scantable.permutated[8]]) >> 1;

    /* DHT */

    /* SOS */
    s->comp_index[0] = 0;
    s->nb_blocks[0] = s->h_count[0] * s->v_count[0];
    s->h_scount[0] = s->h_count[0];
    s->v_scount[0] = s->v_count[0];
    s->dc_index[0] = 0;
    s->ac_index[0] = 0;

    s->comp_index[1] = 1;
    s->nb_blocks[1] = s->h_count[1] * s->v_count[1];
    s->h_scount[1] = s->h_count[1];
    s->v_scount[1] = s->v_count[1];
    s->dc_index[1] = 1;
    s->ac_index[1] = 1;

    s->comp_index[2] = 2;
    s->nb_blocks[2] = s->h_count[2] * s->v_count[2];
    s->h_scount[2] = s->h_count[2];
    s->v_scount[2] = s->v_count[2];
    s->dc_index[2] = 1;
    s->ac_index[2] = 1;

    for (i = 0; i < 3; i++)
        s->last_dc[i] = 1024;

    s->mb_width = (s->width * s->h_max * 8 -1) / (s->h_max * 8);
    s->mb_height = (s->height * s->v_max * 8 -1) / (s->v_max * 8);

    init_get_bits(&s->gb, buf+14, (buf_size-14)*8);

    return mjpeg_decode_scan(s);
#endif

    return i;
}

static int mjpeg_decode_end(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;
    int i, j;

    av_free(s->buffer);
    av_free(s->qscale_table);

    for(i=0;i<2;i++) {
        for(j=0;j<4;j++)
            free_vlc(&s->vlcs[i][j]);
    }
    return 0;
}

static int mjpega_dump_header(AVBitStreamFilterContext *bsfc, AVCodecContext *avctx, const char *args,
                              uint8_t **poutbuf, int *poutbuf_size,
                              const uint8_t *buf, int buf_size, int keyframe)
{
    uint8_t *poutbufp;
    int i;

    if (avctx->codec_id != CODEC_ID_MJPEG) {
        av_log(avctx, AV_LOG_ERROR, "mjpega bitstream filter only applies to mjpeg codec\n");
        return 0;
    }

    *poutbuf_size = 0;
    *poutbuf = av_malloc(buf_size + 44 + FF_INPUT_BUFFER_PADDING_SIZE);
    poutbufp = *poutbuf;
    bytestream_put_byte(&poutbufp, 0xff);
    bytestream_put_byte(&poutbufp, SOI);
    bytestream_put_byte(&poutbufp, 0xff);
    bytestream_put_byte(&poutbufp, APP1);
    bytestream_put_be16(&poutbufp, 42); /* size */
    bytestream_put_be32(&poutbufp, 0);
    bytestream_put_buffer(&poutbufp, "mjpg", 4);
    bytestream_put_be32(&poutbufp, buf_size + 44); /* field size */
    bytestream_put_be32(&poutbufp, buf_size + 44); /* pad field size */
    bytestream_put_be32(&poutbufp, 0);             /* next ptr */

    for (i = 0; i < buf_size - 1; i++) {
        if (buf[i] == 0xff) {
            switch (buf[i + 1]) {
            case DQT:  /* quant off */
            case DHT:  /* huff  off */
            case SOF0: /* image off */
                bytestream_put_be32(&poutbufp, i + 46);
                break;
            case SOS:
                bytestream_put_be32(&poutbufp, i + 46); /* scan off */
                bytestream_put_be32(&poutbufp, i + 46 + AV_RB16(buf + i + 2)); /* data off */
                bytestream_put_buffer(&poutbufp, buf + 2, buf_size - 2); /* skip already written SOI */
                *poutbuf_size = poutbufp - *poutbuf;
                return 1;
            case APP1:
                if (i + 8 < buf_size && AV_RL32(buf + i + 8) == ff_get_fourcc("mjpg")) {
                    av_log(avctx, AV_LOG_ERROR, "bitstream already formatted\n");
                    memcpy(*poutbuf, buf, buf_size);
                    *poutbuf_size = buf_size;
                    return 1;
                }
            }
        }
    }
    av_freep(poutbuf);
    av_log(avctx, AV_LOG_ERROR, "could not find SOS marker in bitstream\n");
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
    CODEC_CAP_DR1,
    NULL
};

AVCodec thp_decoder = {
    "thp",
    CODEC_TYPE_VIDEO,
    CODEC_ID_THP,
    sizeof(MJpegDecodeContext),
    mjpeg_decode_init,
    NULL,
    mjpeg_decode_end,
    mjpeg_decode_frame,
    CODEC_CAP_DR1,
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
    CODEC_CAP_DR1,
    NULL
};

AVCodec sp5x_decoder = {
    "sp5x",
    CODEC_TYPE_VIDEO,
    CODEC_ID_SP5X,
    sizeof(MJpegDecodeContext),
    mjpeg_decode_init,
    NULL,
    mjpeg_decode_end,
    sp5x_decode_frame,
    CODEC_CAP_DR1,
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

AVCodecParser mjpeg_parser = {
    { CODEC_ID_MJPEG },
    sizeof(ParseContext),
    NULL,
    jpeg_parse,
    ff_parse_close,
};

AVBitStreamFilter mjpega_dump_header_bsf = {
    "mjpegadump",
    0,
    mjpega_dump_header,
};
