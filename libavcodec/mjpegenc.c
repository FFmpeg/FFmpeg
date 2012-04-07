/*
 * MJPEG encoder
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2003 Alex Beregszaszi
 * Copyright (c) 2003-2004 Michael Niedermayer
 *
 * Support for external huffman table, various fixes (AVID workaround),
 * aspecting, new decode_frame mechanism and apple mjpeg-b support
 *                                  by Alex Beregszaszi
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

/**
 * @file
 * MJPEG encoder.
 */

//#define DEBUG
#include <assert.h>

#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "mjpeg.h"
#include "mjpegenc.h"

/* use two quantizer tables (one for luminance and one for chrominance) */
/* not yet working */
#undef TWOMATRIXES


av_cold int ff_mjpeg_encode_init(MpegEncContext *s)
{
    MJpegContext *m;

    m = av_malloc(sizeof(MJpegContext));
    if (!m)
        return -1;

    s->min_qcoeff=-1023;
    s->max_qcoeff= 1023;

    /* build all the huffman tables */
    ff_mjpeg_build_huffman_codes(m->huff_size_dc_luminance,
                                 m->huff_code_dc_luminance,
                                 ff_mjpeg_bits_dc_luminance,
                                 ff_mjpeg_val_dc);
    ff_mjpeg_build_huffman_codes(m->huff_size_dc_chrominance,
                                 m->huff_code_dc_chrominance,
                                 ff_mjpeg_bits_dc_chrominance,
                                 ff_mjpeg_val_dc);
    ff_mjpeg_build_huffman_codes(m->huff_size_ac_luminance,
                                 m->huff_code_ac_luminance,
                                 ff_mjpeg_bits_ac_luminance,
                                 ff_mjpeg_val_ac_luminance);
    ff_mjpeg_build_huffman_codes(m->huff_size_ac_chrominance,
                                 m->huff_code_ac_chrominance,
                                 ff_mjpeg_bits_ac_chrominance,
                                 ff_mjpeg_val_ac_chrominance);

    s->mjpeg_ctx = m;
    return 0;
}

void ff_mjpeg_encode_close(MpegEncContext *s)
{
    av_free(s->mjpeg_ctx);
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
    ptr = put_bits_ptr(p);
    put_bits(p, 16, 0); /* patched later */
    size = 2;
    size += put_huffman_table(s, 0, 0, ff_mjpeg_bits_dc_luminance,
                              ff_mjpeg_val_dc);
    size += put_huffman_table(s, 0, 1, ff_mjpeg_bits_dc_chrominance,
                              ff_mjpeg_val_dc);

    size += put_huffman_table(s, 1, 0, ff_mjpeg_bits_ac_luminance,
                              ff_mjpeg_val_ac_luminance);
    size += put_huffman_table(s, 1, 1, ff_mjpeg_bits_ac_chrominance,
                              ff_mjpeg_val_ac_chrominance);
    AV_WB16(ptr, size);
}

static void jpeg_put_comments(MpegEncContext *s)
{
    PutBitContext *p = &s->pb;
    int size;
    uint8_t *ptr;

    if (s->avctx->sample_aspect_ratio.num /* && !lossless */)
    {
    /* JFIF header */
    put_marker(p, APP0);
    put_bits(p, 16, 16);
    ff_put_string(p, "JFIF", 1); /* this puts the trailing zero-byte too */
    put_bits(p, 16, 0x0102); /* v 1.02 */
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
        ptr = put_bits_ptr(p);
        put_bits(p, 16, 0); /* patched later */
        ff_put_string(p, LIBAVCODEC_IDENT, 1);
        size = strlen(LIBAVCODEC_IDENT)+3;
        AV_WB16(ptr, size);
    }

    if(  s->avctx->pix_fmt == PIX_FMT_YUV420P
       ||s->avctx->pix_fmt == PIX_FMT_YUV422P
       ||s->avctx->pix_fmt == PIX_FMT_YUV444P){
        put_marker(p, COM);
        flush_put_bits(p);
        ptr = put_bits_ptr(p);
        put_bits(p, 16, 0); /* patched later */
        ff_put_string(p, "CS=ITU601", 1);
        size = strlen("CS=ITU601")+3;
        AV_WB16(ptr, size);
    }
}

void ff_mjpeg_encode_picture_header(MpegEncContext *s)
{
    const int lossless= s->avctx->codec_id != CODEC_ID_MJPEG;

    put_marker(&s->pb, SOI);

    // hack for AMV mjpeg format
    if(s->avctx->codec_id == CODEC_ID_AMV) return;

    jpeg_put_comments(s);

    jpeg_table_header(s);

    switch(s->avctx->codec_id){
    case CODEC_ID_MJPEG:  put_marker(&s->pb, SOF0 ); break;
    case CODEC_ID_LJPEG:  put_marker(&s->pb, SOF3 ); break;
    default: assert(0);
    }

    put_bits(&s->pb, 16, 17);
    if(lossless && (s->avctx->pix_fmt == PIX_FMT_BGR0
                    || s->avctx->pix_fmt == PIX_FMT_BGRA
                    || s->avctx->pix_fmt == PIX_FMT_BGR24))
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

    switch(s->avctx->codec_id){
    case CODEC_ID_MJPEG:  put_bits(&s->pb, 8, 63); break; /* Se (not used) */
    case CODEC_ID_LJPEG:  put_bits(&s->pb, 8,  0); break; /* not used */
    default: assert(0);
    }

    put_bits(&s->pb, 8, 0); /* Ah/Al (not used) */
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

    flush_put_bits(&s->pb);
    skip_put_bytes(&s->pb, ff_count);

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

void ff_mjpeg_encode_stuffing(PutBitContext * pbc)
{
    int length;
    length= (-put_bits_count(pbc))&7;
    if(length) put_bits(pbc, length, (1<<length)-1);
}

void ff_mjpeg_encode_picture_trailer(MpegEncContext *s)
{
    ff_mjpeg_encode_stuffing(&s->pb);
    flush_put_bits(&s->pb);

    assert((s->header_bits&7)==0);

    escape_FF(s, s->header_bits>>3);

    put_marker(&s->pb, EOI);
}

void ff_mjpeg_encode_dc(MpegEncContext *s, int val,
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

        put_sbits(&s->pb, nbits, mant);
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
        ff_mjpeg_encode_dc(s, val, m->huff_size_dc_luminance, m->huff_code_dc_luminance);
        huff_size_ac = m->huff_size_ac_luminance;
        huff_code_ac = m->huff_code_ac_luminance;
    } else {
        ff_mjpeg_encode_dc(s, val, m->huff_size_dc_chrominance, m->huff_code_dc_chrominance);
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

            put_sbits(&s->pb, nbits, mant);
            run = 0;
        }
    }

    /* output EOB only if not already 64 values */
    if (last_index < 63 || run != 0)
        put_bits(&s->pb, huff_size_ac[0], huff_code_ac[0]);
}

void ff_mjpeg_encode_mb(MpegEncContext *s, DCTELEM block[6][64])
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

    s->i_tex_bits += get_bits_diff(s);
}

// maximum over s->mjpeg_vsample[i]
#define V_MAX 2
static int amv_encode_picture(AVCodecContext *avctx, AVPacket *pkt,
                              const AVFrame *pic_arg, int *got_packet)

{
    MpegEncContext *s = avctx->priv_data;
    AVFrame pic = *pic_arg;
    int i;

    //CODEC_FLAG_EMU_EDGE have to be cleared
    if(s->avctx->flags & CODEC_FLAG_EMU_EDGE)
        return -1;

    //picture should be flipped upside-down
    for(i=0; i < 3; i++) {
        pic.data[i] += (pic.linesize[i] * (s->mjpeg_vsample[i] * (8 * s->mb_height -((s->height/V_MAX)&7)) - 1 ));
        pic.linesize[i] *= -1;
    }
    return ff_MPV_encode_picture(avctx, pkt, &pic, got_packet);
}

AVCodec ff_mjpeg_encoder = {
    .name           = "mjpeg",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_MJPEG,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_MPV_encode_init,
    .encode2        = ff_MPV_encode_picture,
    .close          = ff_MPV_encode_end,
    .pix_fmts       = (const enum PixelFormat[]){
        PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_NONE
    },
    .long_name      = NULL_IF_CONFIG_SMALL("MJPEG (Motion JPEG)"),
};

AVCodec ff_amv_encoder = {
    .name           = "amv",
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = CODEC_ID_AMV,
    .priv_data_size = sizeof(MpegEncContext),
    .init           = ff_MPV_encode_init,
    .encode2        = amv_encode_picture,
    .close          = ff_MPV_encode_end,
    .pix_fmts= (const enum PixelFormat[]){PIX_FMT_YUVJ420P, PIX_FMT_YUVJ422P, PIX_FMT_NONE},
    .long_name      = NULL_IF_CONFIG_SMALL("AMV Video"),
};
