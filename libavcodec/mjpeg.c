/*
 * MJPEG encoder and decoder
 * Copyright (c) 2000, 2001 Gerard Lantau.
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
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

typedef struct MJpegContext {
    UINT8 huff_size_dc_luminance[12];
    UINT16 huff_code_dc_luminance[12];
    UINT8 huff_size_dc_chrominance[12];
    UINT16 huff_code_dc_chrominance[12];

    UINT8 huff_size_ac_luminance[256];
    UINT16 huff_code_ac_luminance[256];
    UINT8 huff_size_ac_chrominance[256];
    UINT16 huff_code_ac_chrominance[256];
} MJpegContext;

#define SOF0 0xc0
#define SOI 0xd8
#define EOI 0xd9
#define DQT 0xdb
#define DHT 0xc4
#define SOS 0xda

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
static const UINT8 bits_dc_luminance[17] =
{ /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0 };
static const UINT8 val_dc_luminance[] =
{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static const UINT8 bits_dc_chrominance[17] =
{ /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0 };
static const UINT8 val_dc_chrominance[] =
{ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };

static const UINT8 bits_ac_luminance[17] =
{ /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d };
static const UINT8 val_ac_luminance[] =
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

static const UINT8 bits_ac_chrominance[17] =
{ /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77 };

static const UINT8 val_ac_chrominance[] =
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
static void build_huffman_codes(UINT8 *huff_size, UINT16 *huff_code,
                                const UINT8 *bits_table, const UINT8 *val_table)
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
    
    m = malloc(sizeof(MJpegContext));
    if (!m)
        return -1;

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
    free(s->mjpeg_ctx);
}

static inline void put_marker(PutBitContext *p, int code)
{
    put_bits(p, 8, 0xff);
    put_bits(p, 8, code);
}

/* table_class: 0 = DC coef, 1 = AC coefs */
static int put_huffman_table(MpegEncContext *s, int table_class, int table_id,
                             const UINT8 *bits_table, const UINT8 *value_table)
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
    int i, size;
    UINT8 *ptr;

    /* quant matrixes */
    put_marker(p, DQT);
    put_bits(p, 16, 2 + 1 * (1 + 64));
    put_bits(p, 4, 0); /* 8 bit precision */
    put_bits(p, 4, 0); /* table 0 */
    for(i=0;i<64;i++) {
        put_bits(p, 8, s->intra_matrix[i]);
    }
#if 0
    put_bits(p, 4, 0); /* 8 bit precision */
    put_bits(p, 4, 1); /* table 1 */
    for(i=0;i<64;i++) {
        put_bits(p, 8, s->chroma_intra_matrix[i]);
    }
#endif

    /* huffman table */
    put_marker(p, DHT);
    flush_put_bits(p);
    ptr = p->buf_ptr;
    put_bits(p, 16, 0); /* patched later */
    size = 2;
    size += put_huffman_table(s, 0, 0, bits_dc_luminance, val_dc_luminance);
    size += put_huffman_table(s, 0, 1, bits_dc_chrominance, val_dc_chrominance);
    
    size += put_huffman_table(s, 1, 0, bits_ac_luminance, val_ac_luminance);
    size += put_huffman_table(s, 1, 1, bits_ac_chrominance, val_ac_chrominance);
    ptr[0] = size >> 8;
    ptr[1] = size;
}

void mjpeg_picture_header(MpegEncContext *s)
{
    put_marker(&s->pb, SOI);

    jpeg_table_header(s);

    put_marker(&s->pb, SOF0);

    put_bits(&s->pb, 16, 17);
    put_bits(&s->pb, 8, 8); /* 8 bits/component */
    put_bits(&s->pb, 16, s->height);
    put_bits(&s->pb, 16, s->width);
    put_bits(&s->pb, 8, 3); /* 3 components */
    
    /* Y component */
    put_bits(&s->pb, 8, 1); /* component number */
    put_bits(&s->pb, 4, 2); /* H factor */
    put_bits(&s->pb, 4, 2); /* V factor */
    put_bits(&s->pb, 8, 0); /* select matrix */
    
    /* Cb component */
    put_bits(&s->pb, 8, 2); /* component number */
    put_bits(&s->pb, 4, 1); /* H factor */
    put_bits(&s->pb, 4, 1); /* V factor */
    put_bits(&s->pb, 8, 0); /* select matrix */

    /* Cr component */
    put_bits(&s->pb, 8, 3); /* component number */
    put_bits(&s->pb, 4, 1); /* H factor */
    put_bits(&s->pb, 4, 1); /* V factor */
    put_bits(&s->pb, 8, 0); /* select matrix */

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
    put_bits(&s->pb, 4, 1); /* AC huffman table index */
    
    /* Cr component */
    put_bits(&s->pb, 8, 3); /* index */
    put_bits(&s->pb, 4, 1); /* DC huffman table index */
    put_bits(&s->pb, 4, 1); /* AC huffman table index */

    put_bits(&s->pb, 8, 0); /* Ss (not used) */
    put_bits(&s->pb, 8, 63); /* Se (not used) */
    put_bits(&s->pb, 8, 0); /* (not used) */
}

void mjpeg_picture_trailer(MpegEncContext *s)
{
    jflush_put_bits(&s->pb);
    put_marker(&s->pb, EOI);
}

static inline void encode_dc(MpegEncContext *s, int val, 
                             UINT8 *huff_size, UINT16 *huff_code)
{
    int mant, nbits;

    if (val == 0) {
        jput_bits(&s->pb, huff_size[0], huff_code[0]);
    } else {
        mant = val;
        if (val < 0) {
            val = -val;
            mant--;
        }
        
        /* compute the log (XXX: optimize) */
        nbits = 0;
        while (val != 0) {
            val = val >> 1;
            nbits++;
        }
            
        jput_bits(&s->pb, huff_size[nbits], huff_code[nbits]);
        
        jput_bits(&s->pb, nbits, mant & ((1 << nbits) - 1));
    }
}

static void encode_block(MpegEncContext *s, DCTELEM *block, int n)
{
    int mant, nbits, code, i, j;
    int component, dc, run, last_index, val;
    MJpegContext *m = s->mjpeg_ctx;
    UINT8 *huff_size_ac;
    UINT16 *huff_code_ac;
    
    /* DC coef */
    component = (n <= 3 ? 0 : n - 4 + 1);
    dc = block[0]; /* overflow is impossible */
    val = dc - s->last_dc[component];
    if (n < 4) {
        encode_dc(s, val, m->huff_size_dc_luminance, m->huff_code_dc_luminance);
        huff_size_ac = m->huff_size_ac_luminance;
        huff_code_ac = m->huff_code_ac_luminance;
    } else {
        encode_dc(s, val, m->huff_size_dc_chrominance, m->huff_code_dc_chrominance);
        huff_size_ac = m->huff_size_ac_chrominance;
        huff_code_ac = m->huff_code_ac_chrominance;
    }
    s->last_dc[component] = dc;
    
    /* AC coefs */
    
    run = 0;
    last_index = s->block_last_index[n];
    for(i=1;i<=last_index;i++) {
        j = zigzag_direct[i];
        val = block[j];
        if (val == 0) {
            run++;
        } else {
            while (run >= 16) {
                jput_bits(&s->pb, huff_size_ac[0xf0], huff_code_ac[0xf0]);
                run -= 16;
            }
            mant = val;
            if (val < 0) {
                val = -val;
                mant--;
            }
            
            /* compute the log (XXX: optimize) */
            nbits = 0;
            while (val != 0) {
                val = val >> 1;
                nbits++;
            }
            code = (run << 4) | nbits;

            jput_bits(&s->pb, huff_size_ac[code], huff_code_ac[code]);
        
            jput_bits(&s->pb, nbits, mant & ((1 << nbits) - 1));
            run = 0;
        }
    }

    /* output EOB only if not already 64 values */
    if (last_index < 63 || run != 0)
        jput_bits(&s->pb, huff_size_ac[0], huff_code_ac[0]);
}

void mjpeg_encode_mb(MpegEncContext *s, 
                     DCTELEM block[6][64])
{
    int i;
    for(i=0;i<6;i++) {
        encode_block(s, block[i], i);
    }
}

/******************************************/
/* decoding */

//#define DEBUG

#ifdef DEBUG
#define dprintf(fmt,args...) printf(fmt, ## args)
#else
#define dprintf(fmt,args...)
#endif

/* compressed picture size */
#define PICTURE_BUFFER_SIZE 100000

#define MAX_COMPONENTS 4

typedef struct MJpegDecodeContext {
    GetBitContext gb;
    UINT32 header_state;
    int start_code; /* current start code */
    UINT8 *buf_ptr;
    int buffer_size;
    int mpeg_enc_ctx_allocated; /* true if decoding context allocated */
    INT16 quant_matrixes[4][64];
    VLC vlcs[2][4];
    int width, height;
    int nb_components;
    int component_id[MAX_COMPONENTS];
    int h_count[MAX_COMPONENTS]; /* horizontal and vertical count for each component */
    int v_count[MAX_COMPONENTS];
    int h_max, v_max; /* maximum h and v counts */
    int quant_index[4];   /* quant table index for each component */
    int last_dc[MAX_COMPONENTS]; /* last DEQUANTIZED dc (XXX: am I right to do that ?) */
    UINT8 *current_picture[MAX_COMPONENTS]; /* picture structure */
    int linesize[MAX_COMPONENTS];
    DCTELEM block[64] __align8;
    UINT8 buffer[PICTURE_BUFFER_SIZE]; 
} MJpegDecodeContext;

static void build_vlc(VLC *vlc, const UINT8 *bits_table, const UINT8 *val_table, 
                      int nb_codes)
{
    UINT8 huff_size[256];
    UINT16 huff_code[256];

    memset(huff_size, 0, sizeof(huff_size));
    build_huffman_codes(huff_size, huff_code, bits_table, val_table);
    
    init_vlc(vlc, 9, nb_codes, huff_size, 1, 1, huff_code, 2, 2);
}

static int mjpeg_decode_init(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;

    s->header_state = 0;
    s->mpeg_enc_ctx_allocated = 0;
    s->buffer_size = PICTURE_BUFFER_SIZE - 1; /* minus 1 to take into
                                                 account FF 00 case */
    s->start_code = -1;
    s->buf_ptr = s->buffer;

    build_vlc(&s->vlcs[0][0], bits_dc_luminance, val_dc_luminance, 12);
    build_vlc(&s->vlcs[0][1], bits_dc_chrominance, val_dc_chrominance, 12);
    build_vlc(&s->vlcs[1][0], bits_ac_luminance, val_ac_luminance, 251);
    build_vlc(&s->vlcs[1][1], bits_ac_chrominance, val_ac_chrominance, 251);
    return 0;
}

/* quantize tables */
static int mjpeg_decode_dqt(MJpegDecodeContext *s,
                            UINT8 *buf, int buf_size)
{
    int len, index, i;
    init_get_bits(&s->gb, buf, buf_size);

    len = get_bits(&s->gb, 16);
    len -= 2;

    while (len >= 65) {
        /* only 8 bit precision handled */
        if (get_bits(&s->gb, 4) != 0)
            return -1;
        index = get_bits(&s->gb, 4);
        if (index >= 4)
            return -1;
        dprintf("index=%d\n", index);
        /* read quant table */
        for(i=0;i<64;i++)
            s->quant_matrixes[index][i] = get_bits(&s->gb, 8);
        len -= 65;
    }
    return 0;
}

/* decode huffman tables and build VLC decoders */
static int mjpeg_decode_dht(MJpegDecodeContext *s,
                            UINT8 *buf, int buf_size)
{
    int len, index, i, class, n, v, code_max;
    UINT8 bits_table[17];
    UINT8 val_table[256];
    
    init_get_bits(&s->gb, buf, buf_size);

    len = get_bits(&s->gb, 16);
    len -= 2;

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
        build_vlc(&s->vlcs[class][index], bits_table, val_table, code_max + 1);
    }
    return 0;
}

static int mjpeg_decode_sof0(MJpegDecodeContext *s,
                             UINT8 *buf, int buf_size)
{
    int len, nb_components, i, width, height;

    init_get_bits(&s->gb, buf, buf_size);

    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    /* only 8 bits/component accepted */
    if (get_bits(&s->gb, 8) != 8)
        return -1;
    height = get_bits(&s->gb, 16);
    width = get_bits(&s->gb, 16);

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
        dprintf("component %d %d:%d\n", i, s->h_count[i], s->v_count[i]);
    }

    /* if different size, realloc/alloc picture */
    /* XXX: also check h_count and v_count */
    if (width != s->width || height != s->height) {
        for(i=0;i<MAX_COMPONENTS;i++) {
            free(s->current_picture[i]);
            s->current_picture[i] = NULL;
        }
        s->width = width;
        s->height = height;
        for(i=0;i<nb_components;i++) {
            int w, h, hh, vv;
            hh = s->h_max / s->h_count[i];
            vv = s->v_max / s->v_count[i];
            w = (s->width + 8 * hh - 1) / (8 * hh);
            h = (s->height + 8 * vv - 1) / (8 * vv);
            w = w * 8;
            h = h * 8;
            s->linesize[i] = w;
            /* memory test is done in mjpeg_decode_sos() */
            s->current_picture[i] = av_mallocz(w * h);
        }
    }

    return 0;
}

static inline int decode_dc(MJpegDecodeContext *s, int dc_index)
{
    VLC *dc_vlc;
    int code, diff;

    dc_vlc = &s->vlcs[0][dc_index];
    code = get_vlc(&s->gb, dc_vlc);
    if (code < 0)
        return 0xffff;
    if (code == 0) {
        diff = 0;
    } else {
        diff = get_bits(&s->gb, code);
        if ((diff & (1 << (code - 1))) == 0) 
            diff = (-1 << code) | (diff + 1);
    }
    return diff;
}

/* decode block and dequantize */
static int decode_block(MJpegDecodeContext *s, DCTELEM *block, 
                        int component, int dc_index, int ac_index, int quant_index)
{
    int nbits, code, i, j, level;
    int run, val;
    VLC *ac_vlc;
    INT16 *quant_matrix;

    quant_matrix = s->quant_matrixes[quant_index];
    /* DC coef */
    val = decode_dc(s, dc_index);
    if (val == 0xffff) {
        dprintf("error dc\n");
        return -1;
    }
    val = val * quant_matrix[0] + s->last_dc[component];
    s->last_dc[component] = val;
    block[0] = val;
    /* AC coefs */
    ac_vlc = &s->vlcs[1][ac_index];
    i = 1;
    for(;;) {
        code = get_vlc(&s->gb, ac_vlc);
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
            run = code >> 4;
            nbits = code & 0xf;
            level = get_bits(&s->gb, nbits);
            if ((level & (1 << (nbits - 1))) == 0) 
                level = (-1 << nbits) | (level + 1);
            i += run;
            if (i >= 64) {
                dprintf("error count: %d\n", i);
                return -1;
            }
            j = zigzag_direct[i];
            block[j] = level * quant_matrix[j];
            i++;
            if (i >= 64)
                break;
        }
    }
    return 0;
}

static int mjpeg_decode_sos(MJpegDecodeContext *s,
                            UINT8 *buf, int buf_size)
{
    int len, nb_components, i, j, n, h, v;
    int mb_width, mb_height, mb_x, mb_y, vmax, hmax, index, id;
    int comp_index[4];
    int dc_index[4];
    int ac_index[4];
    int nb_blocks[4];
    int h_count[4];
    int v_count[4];
    
    init_get_bits(&s->gb, buf, buf_size);
    /* XXX: verify len field validity */
    len = get_bits(&s->gb, 16);
    nb_components = get_bits(&s->gb, 8);
    /* XXX: only interleaved scan accepted */
    if (nb_components != 3)
        return -1;
    vmax = 0;
    hmax = 0;
    for(i=0;i<nb_components;i++) {
        id = get_bits(&s->gb, 8) - 1;
        /* find component index */
        for(index=0;index<s->nb_components;index++)
            if (id == s->component_id[index])
                break;
        if (index == s->nb_components)
            return -1;

        comp_index[i] = index;
        nb_blocks[i] = s->h_count[index] * s->v_count[index];
        h_count[i] = s->h_count[index];
        v_count[i] = s->v_count[index];

        dc_index[i] = get_bits(&s->gb, 4);
        if (dc_index[i] >= 4)
            return -1;
        ac_index[i] = get_bits(&s->gb, 4);
        if (ac_index[i] >= 4)
            return -1;
    }
    get_bits(&s->gb, 8); /* Ss */
    get_bits(&s->gb, 8); /* Se */
    get_bits(&s->gb, 8); /* not used */

    for(i=0;i<nb_components;i++) 
        s->last_dc[i] = 1024;

    if (nb_components > 1) {
        /* interleaved stream */
        mb_width = (s->width + s->h_max * 8 - 1) / (s->h_max * 8);
        mb_height = (s->height + s->v_max * 8 - 1) / (s->v_max * 8);
    } else {
        h = s->h_max / s->h_count[comp_index[0]];
        v = s->v_max / s->v_count[comp_index[0]];
        mb_width = (s->width + h * 8 - 1) / (h * 8);
        mb_height = (s->height + v * 8 - 1) / (v * 8);
        nb_blocks[0] = 1;
        h_count[0] = 1;
        v_count[0] = 1;
    }

    for(mb_y = 0; mb_y < mb_height; mb_y++) {
        for(mb_x = 0; mb_x < mb_width; mb_x++) {
            for(i=0;i<nb_components;i++) {
                UINT8 *ptr;
                int x, y, c;
                n = nb_blocks[i];
                c = comp_index[i];
                h = h_count[i];
                v = v_count[i];
                x = 0;
                y = 0;
                for(j=0;j<n;j++) {
                    memset(s->block, 0, sizeof(s->block));
                    if (decode_block(s, s->block, i, 
                                     dc_index[i], ac_index[i], 
                                     s->quant_index[c]) < 0) {
                        dprintf("error %d %d\n", mb_y, mb_x);
                        return -1;
                    }
                    ff_idct (s->block);
                    ptr = s->current_picture[c] + 
                        (s->linesize[c] * (v * mb_y + y) * 8) + 
                        (h * mb_x + x) * 8;
                    put_pixels_clamped(s->block, ptr, s->linesize[c]);
                    if (++x == h) {
                        x = 0;
                        y++;
                    }
                }
            }
        }
    }
    return 0;
}

/* return the 8 bit start code value and update the search
   state. Return -1 if no start code found */
static int find_marker(UINT8 **pbuf_ptr, UINT8 *buf_end, 
                       UINT32 *header_state)
{
    UINT8 *buf_ptr;
    unsigned int state, v;
    int val;

    state = *header_state;
    buf_ptr = *pbuf_ptr;
    if (state) {
        /* get marker */
    found:
        if (buf_ptr < buf_end) {
            val = *buf_ptr++;
            state = 0;
        } else {
            val = -1;
        }
    } else {
        while (buf_ptr < buf_end) {
            v = *buf_ptr++;
            if (v == 0xff) {
                state = 1;
                goto found;
            }
        }
        val = -1;
    }
    *pbuf_ptr = buf_ptr;
    *header_state = state;
    return val;
}

static int mjpeg_decode_frame(AVCodecContext *avctx, 
                              void *data, int *data_size,
                              UINT8 *buf, int buf_size)
{
    MJpegDecodeContext *s = avctx->priv_data;
    UINT8 *buf_end, *buf_ptr, *buf_start;
    int len, code, start_code, input_size, i;
    AVPicture *picture = data;

    /* no supplementary picture */
    if (buf_size == 0) {
        *data_size = 0;
        return 0;
    }

    buf_ptr = buf;
    buf_end = buf + buf_size;
    while (buf_ptr < buf_end) {
        buf_start = buf_ptr;
        /* find start next marker */
        code = find_marker(&buf_ptr, buf_end, &s->header_state);
        /* copy to buffer */
        len = buf_ptr - buf_start;
        if (len + (s->buf_ptr - s->buffer) > s->buffer_size) {
            /* data too big : flush */
            s->buf_ptr = s->buffer;
            if (code > 0)
                s->start_code = code;
        } else {
            memcpy(s->buf_ptr, buf_start, len);
            s->buf_ptr += len;
            /* if we got FF 00, we copy FF to the stream to unescape FF 00 */
            if (code == 0) {
                s->buf_ptr--;
            } else if (code > 0) {
                /* prepare data for next start code */
                input_size = s->buf_ptr - s->buffer;
                start_code = s->start_code;
                s->buf_ptr = s->buffer;
                s->start_code = code;
                switch(start_code) {
                case SOI:
                    /* nothing to do on SOI */
                    break;
                case DQT:
                    mjpeg_decode_dqt(s, s->buffer, input_size);
                    break;
                case DHT:
                    mjpeg_decode_dht(s, s->buffer, input_size);
                    break;
                case SOF0:
                    mjpeg_decode_sof0(s, s->buffer, input_size);
                    break;
                case SOS:
                    mjpeg_decode_sos(s, s->buffer, input_size);
                    if (s->start_code == EOI) {
                        for(i=0;i<3;i++) {
                            picture->data[i] = s->current_picture[i];
                            picture->linesize[i] = s->linesize[i];
                        }
                        *data_size = sizeof(AVPicture);
                        avctx->height = s->height;
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
                        goto the_end;
                    }
                    break;
                }
            }
        }
    }
 the_end:
    return buf_ptr - buf;
}

static int mjpeg_decode_end(AVCodecContext *avctx)
{
    MJpegDecodeContext *s = avctx->priv_data;
    int i, j;

    for(i=0;i<MAX_COMPONENTS;i++)
        free(s->current_picture[i]);
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
};
