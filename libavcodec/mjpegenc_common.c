/*
 * lossless JPEG shared bits
 * Copyright (c) 2000, 2001 Fabrice Bellard
 * Copyright (c) 2003 Alex Beregszaszi
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

#include <stdint.h>
#include <string.h>

#include "libavutil/pixdesc.h"
#include "libavutil/pixfmt.h"

#include "avcodec.h"
#include "idctdsp.h"
#include "jpegtables.h"
#include "put_bits.h"
#include "mjpegenc.h"
#include "mjpegenc_common.h"
#include "mjpeg.h"
#include "version.h"

/* table_class: 0 = DC coef, 1 = AC coefs */
static int put_huffman_table(PutBitContext *p, int table_class, int table_id,
                             const uint8_t *bits_table, const uint8_t *value_table)
{
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

static void jpeg_table_header(AVCodecContext *avctx, PutBitContext *p,
                              MJpegContext *m,
                              ScanTable *intra_scantable,
                              uint16_t luma_intra_matrix[64],
                              uint16_t chroma_intra_matrix[64],
                              int hsample[3], int use_slices)
{
    int i, j, size;
    uint8_t *ptr;

    if (m) {
        int matrix_count = 1 + !!memcmp(luma_intra_matrix,
                                        chroma_intra_matrix,
                                        sizeof(luma_intra_matrix[0]) * 64);
        if (m->force_duplicated_matrix)
            matrix_count = 2;
        /* quant matrixes */
        put_marker(p, DQT);
        put_bits(p, 16, 2 + matrix_count * (1 + 64));
        put_bits(p, 4, 0); /* 8 bit precision */
        put_bits(p, 4, 0); /* table 0 */
        for (int i = 0; i < 64; i++) {
            uint8_t j = intra_scantable->permutated[i];
            put_bits(p, 8, luma_intra_matrix[j]);
        }

        if (matrix_count > 1) {
            put_bits(p, 4, 0); /* 8 bit precision */
            put_bits(p, 4, 1); /* table 1 */
            for(i=0;i<64;i++) {
                j = intra_scantable->permutated[i];
                put_bits(p, 8, chroma_intra_matrix[j]);
            }
        }
    }

    if (use_slices) {
        put_marker(p, DRI);
        put_bits(p, 16, 4);
        put_bits(p, 16, (avctx->width-1)/(8*hsample[0]) + 1);
    }

    /* huffman table */
    put_marker(p, DHT);
    flush_put_bits(p);
    ptr = put_bits_ptr(p);
    put_bits(p, 16, 0); /* patched later */
    size = 2;

    // Only MJPEG can have a variable Huffman variable. All other
    // formats use the default Huffman table.
    if (m && m->huffman == HUFFMAN_TABLE_OPTIMAL) {
        size += put_huffman_table(p, 0, 0, m->bits_dc_luminance,
                                  m->val_dc_luminance);
        size += put_huffman_table(p, 0, 1, m->bits_dc_chrominance,
                                  m->val_dc_chrominance);

        size += put_huffman_table(p, 1, 0, m->bits_ac_luminance,
                                  m->val_ac_luminance);
        size += put_huffman_table(p, 1, 1, m->bits_ac_chrominance,
                                  m->val_ac_chrominance);
    } else {
        size += put_huffman_table(p, 0, 0, ff_mjpeg_bits_dc_luminance,
                                  ff_mjpeg_val_dc);
        size += put_huffman_table(p, 0, 1, ff_mjpeg_bits_dc_chrominance,
                                  ff_mjpeg_val_dc);

        size += put_huffman_table(p, 1, 0, ff_mjpeg_bits_ac_luminance,
                                  ff_mjpeg_val_ac_luminance);
        size += put_huffman_table(p, 1, 1, ff_mjpeg_bits_ac_chrominance,
                                  ff_mjpeg_val_ac_chrominance);
    }
    AV_WB16(ptr, size);
}

enum {
    ICC_HDR_SIZE    = 16, /* ICC_PROFILE\0 tag + 4 bytes */
    ICC_CHUNK_SIZE  = UINT16_MAX - ICC_HDR_SIZE,
    ICC_MAX_CHUNKS  = UINT8_MAX,
};

int ff_mjpeg_add_icc_profile_size(AVCodecContext *avctx, const AVFrame *frame,
                                  size_t *max_pkt_size)
{
    const AVFrameSideData *sd;
    size_t new_pkt_size;
    int nb_chunks;
    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_ICC_PROFILE);
    if (!sd || !sd->size)
        return 0;

    if (sd->size > ICC_MAX_CHUNKS * ICC_CHUNK_SIZE) {
        av_log(avctx, AV_LOG_ERROR, "Cannot store %"SIZE_SPECIFIER" byte ICC "
               "profile: too large for JPEG\n",
               sd->size);
        return AVERROR_INVALIDDATA;
    }

    nb_chunks = (sd->size + ICC_CHUNK_SIZE - 1) / ICC_CHUNK_SIZE;
    new_pkt_size = *max_pkt_size + nb_chunks * (UINT16_MAX + 2 /* APP2 marker */);
    if (new_pkt_size < *max_pkt_size) /* overflow */
        return AVERROR_INVALIDDATA;
    *max_pkt_size = new_pkt_size;
    return 0;
}

static void jpeg_put_comments(AVCodecContext *avctx, PutBitContext *p,
                              const AVFrame *frame)
{
    const AVFrameSideData *sd = NULL;
    int size;
    uint8_t *ptr;

    if (avctx->sample_aspect_ratio.num > 0 && avctx->sample_aspect_ratio.den > 0) {
        AVRational sar = avctx->sample_aspect_ratio;

        if (sar.num > 65535 || sar.den > 65535) {
            if (!av_reduce(&sar.num, &sar.den, avctx->sample_aspect_ratio.num, avctx->sample_aspect_ratio.den, 65535))
                av_log(avctx, AV_LOG_WARNING,
                    "Cannot store exact aspect ratio %d:%d\n",
                    avctx->sample_aspect_ratio.num,
                    avctx->sample_aspect_ratio.den);
        }

        /* JFIF header */
        put_marker(p, APP0);
        put_bits(p, 16, 16);
        ff_put_string(p, "JFIF", 1); /* this puts the trailing zero-byte too */
        /* The most significant byte is used for major revisions, the least
         * significant byte for minor revisions. Version 1.02 is the current
         * released revision. */
        put_bits(p, 16, 0x0102);
        put_bits(p,  8, 0);              /* units type: 0 - aspect ratio */
        put_bits(p, 16, sar.num);
        put_bits(p, 16, sar.den);
        put_bits(p, 8, 0); /* thumbnail width */
        put_bits(p, 8, 0); /* thumbnail height */
    }

    /* ICC profile */
    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_ICC_PROFILE);
    if (sd && sd->size) {
        const int nb_chunks = (sd->size + ICC_CHUNK_SIZE - 1) / ICC_CHUNK_SIZE;
        const uint8_t *data = sd->data;
        size_t remaining = sd->size;
        /* must already be checked by the packat allocation code */
        av_assert0(remaining <= ICC_MAX_CHUNKS * ICC_CHUNK_SIZE);
        flush_put_bits(p);
        for (int i = 0; i < nb_chunks; i++) {
            size = FFMIN(remaining, ICC_CHUNK_SIZE);
            av_assert1(size > 0);
            ptr = put_bits_ptr(p);
            ptr[0] = 0xff; /* chunk marker, not part of ICC_HDR_SIZE */
            ptr[1] = APP2;
            AV_WB16(ptr+2, size + ICC_HDR_SIZE);
            AV_WL32(ptr+4,  MKTAG('I','C','C','_'));
            AV_WL32(ptr+8,  MKTAG('P','R','O','F'));
            AV_WL32(ptr+12, MKTAG('I','L','E','\0'));
            ptr[16] = i+1;
            ptr[17] = nb_chunks;
            memcpy(&ptr[18], data, size);
            skip_put_bytes(p, size + ICC_HDR_SIZE + 2);
            remaining -= size;
            data += size;
        }
        av_assert1(!remaining);
    }

    /* comment */
    if (!(avctx->flags & AV_CODEC_FLAG_BITEXACT)) {
        put_marker(p, COM);
        flush_put_bits(p);
        ptr = put_bits_ptr(p);
        put_bits(p, 16, 0); /* patched later */
        ff_put_string(p, LIBAVCODEC_IDENT, 1);
        size = strlen(LIBAVCODEC_IDENT)+3;
        AV_WB16(ptr, size);
    }

    if (((avctx->pix_fmt == AV_PIX_FMT_YUV420P ||
          avctx->pix_fmt == AV_PIX_FMT_YUV422P ||
          avctx->pix_fmt == AV_PIX_FMT_YUV444P) && avctx->color_range != AVCOL_RANGE_JPEG)
        || avctx->color_range == AVCOL_RANGE_MPEG) {
        put_marker(p, COM);
        flush_put_bits(p);
        ptr = put_bits_ptr(p);
        put_bits(p, 16, 0); /* patched later */
        ff_put_string(p, "CS=ITU601", 1);
        size = strlen("CS=ITU601")+3;
        AV_WB16(ptr, size);
    }
}

void ff_mjpeg_init_hvsample(AVCodecContext *avctx, int hsample[4], int vsample[4])
{
    if (avctx->codec_id == AV_CODEC_ID_LJPEG &&
        (   avctx->pix_fmt == AV_PIX_FMT_BGR0
         || avctx->pix_fmt == AV_PIX_FMT_BGRA
         || avctx->pix_fmt == AV_PIX_FMT_BGR24)) {
        vsample[0] = hsample[0] =
        vsample[1] = hsample[1] =
        vsample[2] = hsample[2] =
        vsample[3] = hsample[3] = 1;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV444P || avctx->pix_fmt == AV_PIX_FMT_YUVJ444P) {
        vsample[0] = vsample[1] = vsample[2] = 2;
        hsample[0] = hsample[1] = hsample[2] = 1;
    } else {
        int chroma_h_shift, chroma_v_shift;
        av_pix_fmt_get_chroma_sub_sample(avctx->pix_fmt, &chroma_h_shift,
                                         &chroma_v_shift);
        vsample[0] = 2;
        vsample[1] = 2 >> chroma_v_shift;
        vsample[2] = 2 >> chroma_v_shift;
        hsample[0] = 2;
        hsample[1] = 2 >> chroma_h_shift;
        hsample[2] = 2 >> chroma_h_shift;
    }
}

void ff_mjpeg_encode_picture_header(AVCodecContext *avctx, PutBitContext *pb,
                                    const AVFrame *frame, struct MJpegContext *m,
                                    ScanTable *intra_scantable, int pred,
                                    uint16_t luma_intra_matrix[64],
                                    uint16_t chroma_intra_matrix[64],
                                    int use_slices)
{
    const int lossless = !m;
    int hsample[4], vsample[4];
    int components = 3 + (avctx->pix_fmt == AV_PIX_FMT_BGRA);
    int chroma_matrix = !!memcmp(luma_intra_matrix,
                                 chroma_intra_matrix,
                                 sizeof(luma_intra_matrix[0])*64);

    ff_mjpeg_init_hvsample(avctx, hsample, vsample);

    put_marker(pb, SOI);

    // hack for AMV mjpeg format
    if (avctx->codec_id == AV_CODEC_ID_AMV)
        return;

    jpeg_put_comments(avctx, pb, frame);

    jpeg_table_header(avctx, pb, m, intra_scantable,
                      luma_intra_matrix, chroma_intra_matrix, hsample,
                      use_slices);

    switch (avctx->codec_id) {
    case AV_CODEC_ID_MJPEG:  put_marker(pb, SOF0 ); break;
    case AV_CODEC_ID_LJPEG:  put_marker(pb, SOF3 ); break;
    default: av_assert0(0);
    }

    put_bits(pb, 16, 17);
    if (lossless && (  avctx->pix_fmt == AV_PIX_FMT_BGR0
                    || avctx->pix_fmt == AV_PIX_FMT_BGRA
                    || avctx->pix_fmt == AV_PIX_FMT_BGR24))
        put_bits(pb, 8, 9); /* 9 bits/component RCT */
    else
        put_bits(pb, 8, 8); /* 8 bits/component */
    put_bits(pb, 16, avctx->height);
    put_bits(pb, 16, avctx->width);
    put_bits(pb, 8, components); /* 3 or 4 components */

    /* Y component */
    put_bits(pb, 8, 1); /* component number */
    put_bits(pb, 4, hsample[0]); /* H factor */
    put_bits(pb, 4, vsample[0]); /* V factor */
    put_bits(pb, 8, 0); /* select matrix */

    /* Cb component */
    put_bits(pb, 8, 2); /* component number */
    put_bits(pb, 4, hsample[1]); /* H factor */
    put_bits(pb, 4, vsample[1]); /* V factor */
    put_bits(pb, 8, lossless ? 0 : chroma_matrix); /* select matrix */

    /* Cr component */
    put_bits(pb, 8, 3); /* component number */
    put_bits(pb, 4, hsample[2]); /* H factor */
    put_bits(pb, 4, vsample[2]); /* V factor */
    put_bits(pb, 8, lossless ? 0 : chroma_matrix); /* select matrix */

    if (components == 4) {
        put_bits(pb, 8, 4); /* component number */
        put_bits(pb, 4, hsample[3]); /* H factor */
        put_bits(pb, 4, vsample[3]); /* V factor */
        put_bits(pb, 8, 0); /* select matrix */
    }

    /* scan header */
    put_marker(pb, SOS);
    put_bits(pb, 16, 6 + 2*components); /* length */
    put_bits(pb, 8, components); /* 3 components */

    /* Y component */
    put_bits(pb, 8, 1); /* index */
    put_bits(pb, 4, 0); /* DC huffman table index */
    put_bits(pb, 4, 0); /* AC huffman table index */

    /* Cb component */
    put_bits(pb, 8, 2); /* index */
    put_bits(pb, 4, 1); /* DC huffman table index */
    put_bits(pb, 4, lossless ? 0 : 1); /* AC huffman table index */

    /* Cr component */
    put_bits(pb, 8, 3); /* index */
    put_bits(pb, 4, 1); /* DC huffman table index */
    put_bits(pb, 4, lossless ? 0 : 1); /* AC huffman table index */

    if (components == 4) {
        /* Alpha component */
        put_bits(pb, 8, 4); /* index */
        put_bits(pb, 4, 0); /* DC huffman table index */
        put_bits(pb, 4, 0); /* AC huffman table index */
    }

    put_bits(pb, 8, pred); /* Ss (not used); pred only nonzero for LJPEG */

    switch (avctx->codec_id) {
    case AV_CODEC_ID_MJPEG:  put_bits(pb, 8, 63); break; /* Se (not used) */
    case AV_CODEC_ID_LJPEG:  put_bits(pb, 8,  0); break; /* not used */
    default: av_assert0(0);
    }

    put_bits(pb, 8, 0); /* Ah/Al (not used) */
}

void ff_mjpeg_escape_FF(PutBitContext *pb, int start)
{
    int size;
    int i, ff_count;
    uint8_t *buf = pb->buf + start;
    int align= (-(size_t)(buf))&3;
    int pad = (-put_bits_count(pb))&7;

    if (pad)
        put_bits(pb, pad, (1<<pad)-1);

    flush_put_bits(pb);
    size = put_bytes_output(pb) - start;

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

    flush_put_bits(pb);
    skip_put_bytes(pb, ff_count);

    for(i=size-1; ff_count; i--){
        int v= buf[i];

        if(v==0xFF){
            buf[i+ff_count]= 0;
            ff_count--;
        }

        buf[i+ff_count]= v;
    }
}

/* isn't this function nicer than the one in the libjpeg ? */
void ff_mjpeg_build_huffman_codes(uint8_t *huff_size, uint16_t *huff_code,
                                  const uint8_t *bits_table,
                                  const uint8_t *val_table)
{
    int k, code;

    k = 0;
    code = 0;
    for (int i = 1; i <= 16; i++) {
        int nb = bits_table[i];
        for (int j = 0; j < nb; j++) {
            int sym = val_table[k++];
            huff_size[sym] = i;
            huff_code[sym] = code;
            code++;
        }
        code <<= 1;
    }
}

void ff_mjpeg_encode_picture_trailer(PutBitContext *pb, int header_bits)
{
    av_assert1((header_bits & 7) == 0);

    put_marker(pb, EOI);
}

void ff_mjpeg_encode_dc(PutBitContext *pb, int val,
                        uint8_t *huff_size, uint16_t *huff_code)
{
    int mant, nbits;

    if (val == 0) {
        put_bits(pb, huff_size[0], huff_code[0]);
    } else {
        mant = val;
        if (val < 0) {
            val = -val;
            mant--;
        }

        nbits= av_log2_16bit(val) + 1;

        put_bits(pb, huff_size[nbits], huff_code[nbits]);

        put_sbits(pb, nbits, mant);
    }
}

int ff_mjpeg_encode_check_pix_fmt(AVCodecContext *avctx)
{
    if (avctx->strict_std_compliance > FF_COMPLIANCE_UNOFFICIAL &&
        avctx->color_range != AVCOL_RANGE_JPEG &&
        (avctx->pix_fmt == AV_PIX_FMT_YUV420P ||
         avctx->pix_fmt == AV_PIX_FMT_YUV422P ||
         avctx->pix_fmt == AV_PIX_FMT_YUV444P ||
         avctx->color_range == AVCOL_RANGE_MPEG)) {
        av_log(avctx, AV_LOG_ERROR,
               "Non full-range YUV is non-standard, set strict_std_compliance "
               "to at most unofficial to use it.\n");
        return AVERROR(EINVAL);
    }
    return 0;
}
