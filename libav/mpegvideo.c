/*
 * The simplest mpeg encoder
 * Copyright (c) 2000 Gerard Lantau.
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
#include <netinet/in.h>
#include <math.h>
#include "avcodec.h"
#include "mpegvideo.h"

//#define DEBUG

/* depends on JPEG librarie */
extern void jpeg_fdct_ifast (DCTELEM * data);

/* depends on mpeg */
extern void j_rev_dct (DCTELEM *data);

/* for jpeg fast DCT */
#define CONST_BITS 14

static const unsigned short aanscales[64] = {
    /* precomputed values scaled up by 14 bits */
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    22725, 31521, 29692, 26722, 22725, 17855, 12299,  6270,
    21407, 29692, 27969, 25172, 21407, 16819, 11585,  5906,
    19266, 26722, 25172, 22654, 19266, 15137, 10426,  5315,
    16384, 22725, 21407, 19266, 16384, 12873,  8867,  4520,
    12873, 17855, 16819, 15137, 12873, 10114,  6967,  3552,
    8867, 12299, 11585, 10426,  8867,  6967,  4799,  2446,
    4520,  6270,  5906,  5315,  4520,  3552,  2446,  1247
};

static UINT8 cropTbl[256 + 2 * MAX_NEG_CROP];
static UINT32 squareTbl[512];

static void encode_picture(MpegEncContext *s, int picture_number);
static void rate_control_init(MpegEncContext *s);
static int rate_estimate_qscale(MpegEncContext *s);
static void mpeg1_skip_picture(MpegEncContext *s, int pict_num);

#include "mpegencodevlc.h"

static void put_header(MpegEncContext *s, int header)
{
    align_put_bits(&s->pb);
    put_bits(&s->pb, 32, header);
}

static void convert_matrix(int *qmat, const UINT8 *quant_matrix, int qscale)
{
    int i;

    for(i=0;i<64;i++) {
        qmat[i] = (int)((1 << 22) * 16384.0 / (aanscales[i] * qscale * quant_matrix[i]));
    }
}


int MPV_encode_init(AVEncodeContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
    int pict_size, c_size, i;
    UINT8 *pict;

    s->bit_rate = avctx->bit_rate;
    s->frame_rate = avctx->rate;
    s->width = avctx->width;
    s->height = avctx->height;
    s->gop_size = avctx->gop_size;
    if (s->gop_size <= 1) {
        s->intra_only = 1;
        s->gop_size = 12;
    } else {
        s->intra_only = 0;
    }

    switch(avctx->codec->id) {
    case CODEC_ID_MPEG1VIDEO:
        s->out_format = FMT_MPEG1;
        break;
    case CODEC_ID_MJPEG:
        s->out_format = FMT_MJPEG;
        s->intra_only = 1; /* force intra only for jpeg */
        if (mjpeg_init(s) < 0)
            return -1;
        break;
    case CODEC_ID_H263:
        s->out_format = FMT_H263;
        break;
    case CODEC_ID_RV10:
        s->out_format = FMT_H263;
        s->h263_rv10 = 1;
        break;
    default:
        return -1;
    }

    switch(s->frame_rate) {
    case 24:
        s->frame_rate_index = 2;
        break;
    case 25:
        s->frame_rate_index = 3;
        break;
    case 30:
        s->frame_rate_index = 5;
        break;
    case 50:
        s->frame_rate_index = 6;
        break;
    case 60:
        s->frame_rate_index = 8;
        break;
    default:
        /* we accept lower frame rates than 24 for low bit rate mpeg */
        if (s->frame_rate >= 1 && s->frame_rate < 24) {
            s->frame_rate_index = 2;
        } else {
            return -1;
        }
        break;
    }

    /* init */
    s->mb_width = s->width / 16;
    s->mb_height = s->height / 16;
    
    c_size = s->width * s->height;
    pict_size = (c_size * 3) / 2;
    pict = malloc(pict_size);
    if (pict == NULL)
        return -1;
    s->last_picture[0] = pict;
    s->last_picture[1] = pict + c_size;
    s->last_picture[2] = pict + c_size + (c_size / 4);
    
    pict = malloc(pict_size);
    if (pict == NULL)
        return -1;
    s->last_picture[0] = pict;
    s->last_picture[1] = pict + c_size;
    s->last_picture[2] = pict + c_size + (c_size / 4);

    pict = malloc(pict_size);
    if (pict == NULL) {
        free(s->last_picture[0]);
        return -1;
    }
    s->current_picture[0] = pict;
    s->current_picture[1] = pict + c_size;
    s->current_picture[2] = pict + c_size + (c_size / 4);

    for(i=0;i<256;i++) cropTbl[i + MAX_NEG_CROP] = i;
    for(i=0;i<MAX_NEG_CROP;i++) {
        cropTbl[i] = 0;
        cropTbl[i + MAX_NEG_CROP + 256] = 255;
    }

    for(i=0;i<512;i++) {
        squareTbl[i] = (i - 256) * (i - 256);
    }
    
    /* rate control init */
    rate_control_init(s);

    s->picture_number = 0;
    s->fake_picture_number = 0;

    return 0;
}

int MPV_encode_end(AVEncodeContext *avctx)
{
    MpegEncContext *s = avctx->priv_data;
#if 0
    /* end of sequence */
    if (s->out_format == FMT_MPEG1) {
        put_header(s, SEQ_END_CODE);
    }

    if (!s->flush_frames)
        flush_put_bits(&s->pb);
#endif    
    free(s->last_picture[0]);
    free(s->current_picture[0]);
    if (s->out_format == FMT_MJPEG)
        mjpeg_close(s);
    return 0;
}

int MPV_encode_picture(AVEncodeContext *avctx,
                       unsigned char *buf, int buf_size, void *data)
{
    MpegEncContext *s = avctx->priv_data;
    int i;

    memcpy(s->new_picture, data, 3 * sizeof(UINT8 *));

    init_put_bits(&s->pb, buf, buf_size, NULL, NULL);

    /* group of picture */
    if (s->out_format == FMT_MPEG1) {
        unsigned int vbv_buffer_size;
        unsigned int time_code, fps, n;

        if ((s->picture_number % s->gop_size) == 0) {
            /* mpeg1 header repeated every gop */
            put_header(s, SEQ_START_CODE);
            
            put_bits(&s->pb, 12, s->width);
            put_bits(&s->pb, 12, s->height);
            put_bits(&s->pb, 4, 1); /* 1/1 aspect ratio */
            put_bits(&s->pb, 4, s->frame_rate_index);
            put_bits(&s->pb, 18, 0x3ffff);
            put_bits(&s->pb, 1, 1); /* marker */
            /* vbv buffer size: slightly greater than an I frame. We add
               some margin just in case */
            vbv_buffer_size = (3 * s->I_frame_bits) / (2 * 8);
            put_bits(&s->pb, 10, (vbv_buffer_size + 16383) / 16384); 
            put_bits(&s->pb, 1, 1); /* constrained parameter flag */
            put_bits(&s->pb, 1, 0); /* no custom intra matrix */
            put_bits(&s->pb, 1, 0); /* no custom non intra matrix */

            put_header(s, GOP_START_CODE);
            put_bits(&s->pb, 1, 0); /* do drop frame */
            /* time code : we must convert from the real frame rate to a
               fake mpeg frame rate in case of low frame rate */
            fps = frame_rate_tab[s->frame_rate_index];
            time_code = s->fake_picture_number;
            s->gop_picture_number = time_code;
            put_bits(&s->pb, 5, (time_code / (fps * 3600)) % 24);
            put_bits(&s->pb, 6, (time_code / (fps * 60)) % 60);
            put_bits(&s->pb, 1, 1);
            put_bits(&s->pb, 6, (time_code / fps) % 60);
            put_bits(&s->pb, 6, (time_code % fps));
            put_bits(&s->pb, 1, 1); /* closed gop */
            put_bits(&s->pb, 1, 0); /* broken link */
        }

        if (s->frame_rate < 24 && s->picture_number > 0) {
            /* insert empty P pictures to slow down to the desired
               frame rate. Each fake pictures takes about 20 bytes */
            fps = frame_rate_tab[s->frame_rate_index];
            n = ((s->picture_number * fps) / s->frame_rate) - 1;
            while (s->fake_picture_number < n) {
                mpeg1_skip_picture(s, s->fake_picture_number - 
                                   s->gop_picture_number); 
                s->fake_picture_number++;
            }

        }
        s->fake_picture_number++;
    }
    
    
    if (!s->intra_only) {
        /* first picture of GOP is intra */
        if ((s->picture_number % s->gop_size) == 0)
            s->pict_type = I_TYPE;
        else
            s->pict_type = P_TYPE;
    } else {
        s->pict_type = I_TYPE;
    }
    avctx->key_frame = (s->pict_type == I_TYPE);
    
    encode_picture(s, s->picture_number);
    
    /* swap current and last picture */
    for(i=0;i<3;i++) {
        UINT8 *tmp;
        
        tmp = s->last_picture[i];
        s->last_picture[i] = s->current_picture[i];
        s->current_picture[i] = tmp;
    }
    s->picture_number++;

    if (s->out_format == FMT_MJPEG)
        mjpeg_picture_trailer(s);

    flush_put_bits(&s->pb);
    s->total_bits += (s->pb.buf_ptr - s->pb.buf) * 8;
    return s->pb.buf_ptr - s->pb.buf;
}

/* insert a fake P picture */
static void mpeg1_skip_picture(MpegEncContext *s, int pict_num)
{
    unsigned int mb_incr;

    /* mpeg1 picture header */
    put_header(s, PICTURE_START_CODE);
    /* temporal reference */
    put_bits(&s->pb, 10, pict_num & 0x3ff); 
    
    put_bits(&s->pb, 3, P_TYPE);
    put_bits(&s->pb, 16, 0xffff); /* non constant bit rate */
    
    put_bits(&s->pb, 1, 1); /* integer coordinates */
    put_bits(&s->pb, 3, 1); /* forward_f_code */
    
    put_bits(&s->pb, 1, 0); /* extra bit picture */
    
    /* only one slice */
    put_header(s, SLICE_MIN_START_CODE);
    put_bits(&s->pb, 5, 1); /* quantizer scale */
    put_bits(&s->pb, 1, 0); /* slice extra information */
    
    mb_incr = 1;
    put_bits(&s->pb, mbAddrIncrTable[mb_incr][1], 
             mbAddrIncrTable[mb_incr][0]);
    
    /* empty macroblock */
    put_bits(&s->pb, 3, 1); /* motion only */
    
    /* zero motion x & y */
    put_bits(&s->pb, 1, 1); 
    put_bits(&s->pb, 1, 1); 

    /* output a number of empty slice */
    mb_incr = s->mb_width * s->mb_height - 1;
    while (mb_incr > 33) {
        put_bits(&s->pb, 11, 0x008);
        mb_incr -= 33;
    }
    put_bits(&s->pb, mbAddrIncrTable[mb_incr][1], 
             mbAddrIncrTable[mb_incr][0]);
    
    /* empty macroblock */
    put_bits(&s->pb, 3, 1); /* motion only */
    
    /* zero motion x & y */
    put_bits(&s->pb, 1, 1); 
    put_bits(&s->pb, 1, 1); 
}

static int pix_sum(UINT8 *pix, int line_size)
{
    int s, i, j;

    s = 0;
    for(i=0;i<16;i++) {
        for(j=0;j<16;j+=8) {
            s += pix[0];
            s += pix[1];
            s += pix[2];
            s += pix[3];
            s += pix[4];
            s += pix[5];
            s += pix[6];
            s += pix[7];
            pix += 8;
        }
        pix += line_size - 16;
    }
    return s;
}

static int pix_norm1(UINT8 *pix, int line_size)
{
    int s, i, j;
    UINT32 *sq = squareTbl + 256;

    s = 0;
    for(i=0;i<16;i++) {
        for(j=0;j<16;j+=8) {
            s += sq[pix[0]];
            s += sq[pix[1]];
            s += sq[pix[2]];
            s += sq[pix[3]];
            s += sq[pix[4]];
            s += sq[pix[5]];
            s += sq[pix[6]];
            s += sq[pix[7]];
            pix += 8;
        }
        pix += line_size - 16;
    }
    return s;
}

static int pix_norm(UINT8 *pix1, UINT8 *pix2, int line_size)
{
    int s, i, j;
    UINT32 *sq = squareTbl + 256;

    s = 0;
    for(i=0;i<16;i++) {
        for(j=0;j<16;j+=8) {
            s += sq[pix1[0] - pix2[0]];
            s += sq[pix1[1] - pix2[1]];
            s += sq[pix1[2] - pix2[2]];
            s += sq[pix1[3] - pix2[3]];
            s += sq[pix1[4] - pix2[4]];
            s += sq[pix1[5] - pix2[5]];
            s += sq[pix1[6] - pix2[6]];
            s += sq[pix1[7] - pix2[7]];
            pix1 += 8;
            pix2 += 8;
        }
        pix1 += line_size - 16;
        pix2 += line_size - 16;
    }
    return s;
}


static int estimate_motion(MpegEncContext *s, 
                           int mb_x, int mb_y,
                           int *mx_ptr, int *my_ptr)
{
    UINT8 *pix, *ppix;
    int sum, varc, vard;

    pix = s->new_picture[0] + (mb_y * 16 * s->width) + mb_x * 16;
    ppix = s->last_picture[0] + (mb_y * 16 * s->width) + mb_x * 16;

    sum = pix_sum(pix, s->width);
    varc = pix_norm1(pix, s->width);
    vard = pix_norm(pix, ppix, s->width);
    
    vard = vard >> 8;
    sum = sum >> 8;
    varc = (varc >> 8)  - sum * sum;

    *mx_ptr = 0;
    *my_ptr = 0;
    if (vard <= 64) {
	return 0;
    } else if (vard < varc) {
	return 0;
    } else {
        return 1;
    }
}

static void get_pixels(DCTELEM *block, const UINT8 *pixels, int line_size);
static void put_pixels(const DCTELEM *block, UINT8 *pixels, int line_size);
static void sub_pixels(DCTELEM *block, const UINT8 *pixels, int line_size);
static void add_pixels(DCTELEM *block, const UINT8 *pixels, int line_size);
static int dct_quantize(MpegEncContext *s, DCTELEM *block, int qscale);
static void encode_block(MpegEncContext *s, 
                         DCTELEM *block, 
                         int component);
static void dct_unquantize(MpegEncContext *s, DCTELEM *block, int qscale);
static void mpeg1_encode_mb(MpegEncContext *s, int mb_x, int mb_y,
                            DCTELEM block[6][64],
                            int motion_x, int motion_y);

static void encode_picture(MpegEncContext *s, int picture_number)
{
    int mb_x, mb_y;
    UINT8 *ptr;
    DCTELEM block[6][64];
    int i, motion_x, motion_y;

    s->picture_number = picture_number;
    s->qscale = rate_estimate_qscale(s);

    /* precompute matrix */
    if (s->out_format == FMT_MJPEG) {
        /* for mjpeg, we do include qscale in the matrix */
        s->init_intra_matrix[0] = default_intra_matrix[0];
        for(i=1;i<64;i++)
            s->init_intra_matrix[i] = (default_intra_matrix[i] * s->qscale) >> 3;
        convert_matrix(s->intra_matrix, s->init_intra_matrix, 8);
    } else {
        convert_matrix(s->intra_matrix, default_intra_matrix, s->qscale);
        convert_matrix(s->non_intra_matrix, default_non_intra_matrix, s->qscale);
    }

    switch(s->out_format) {
    case FMT_MJPEG:
        mjpeg_picture_header(s);
        break;
    case FMT_H263:
        if (s->h263_rv10) 
            rv10_encode_picture_header(s, picture_number);
        else
            h263_picture_header(s, picture_number);
        break;
    case FMT_MPEG1:
        /* mpeg1 picture header */
        put_header(s, PICTURE_START_CODE);
        /* temporal reference */
        put_bits(&s->pb, 10, (s->fake_picture_number - 
                              s->gop_picture_number) & 0x3ff); 
        
        put_bits(&s->pb, 3, s->pict_type);
        put_bits(&s->pb, 16, 0xffff); /* non constant bit rate */
        
        if (s->pict_type == P_TYPE) {
            put_bits(&s->pb, 1, 1); /* integer coordinates */
            put_bits(&s->pb, 3, 1); /* forward_f_code */
        }
        
        put_bits(&s->pb, 1, 0); /* extra bit picture */
        
        /* only one slice */
        put_header(s, SLICE_MIN_START_CODE);
        put_bits(&s->pb, 5, s->qscale); /* quantizer scale */
        put_bits(&s->pb, 1, 0); /* slice extra information */
        break;
    }
        
    /* init last dc values */
    /* XXX: quant matrix value is implied here */
    s->last_dc[0] = 128;
    s->last_dc[1] = 128;
    s->last_dc[2] = 128;
    s->mb_incr = 1;
    
    for(mb_y=0; mb_y < s->mb_height; mb_y++) {
        for(mb_x=0; mb_x < s->mb_width; mb_x++) {
            /* compute motion vector and macro block type (intra or non intra) */
            motion_x = 0;
            motion_y = 0;
            if (s->pict_type == P_TYPE) {
                s->mb_intra = estimate_motion(s, mb_x, mb_y,
                                              &motion_x,
                                              &motion_y);
            } else {
                s->mb_intra = 1;
            }

            /* reset intra predictors if non intra mb */
            if (!s->mb_intra) {
                s->last_dc[0] = 128;
                s->last_dc[1] = 128;
                s->last_dc[2] = 128;
            }

            /* get the pixels */
            ptr = s->new_picture[0] + (mb_y * 16 * s->width) + mb_x * 16;
            get_pixels(block[0], ptr, s->width);
            get_pixels(block[1], ptr + 8, s->width);
            get_pixels(block[2], ptr + 8 * s->width, s->width);
            get_pixels(block[3], ptr + 8 * s->width + 8, s->width);
            ptr = s->new_picture[1] + (mb_y * 8 * (s->width >> 1)) + mb_x * 8;
            get_pixels(block[4],ptr, s->width >> 1);

            ptr = s->new_picture[2] + (mb_y * 8 * (s->width >> 1)) + mb_x * 8;
            get_pixels(block[5],ptr, s->width >> 1);

            /* subtract previous frame if non intra */
            if (!s->mb_intra) {
                ptr = s->last_picture[0] + 
                    ((mb_y * 16 + motion_y) * s->width) + (mb_x * 16 + motion_x);

                sub_pixels(block[0], ptr, s->width);
                sub_pixels(block[1], ptr + 8, s->width);
                sub_pixels(block[2], ptr + s->width * 8, s->width);
                sub_pixels(block[3], ptr + 8 + s->width * 8, s->width);
                ptr = s->last_picture[1] + 
                    ((mb_y * 8 + (motion_y >> 1)) * (s->width >> 1)) + 
                    (mb_x * 8 + (motion_x >> 1));
                sub_pixels(block[4], ptr, s->width >> 1);
                ptr = s->last_picture[2] + 
                    ((mb_y * 8 + (motion_y >> 1)) * (s->width >> 1)) + 
                    (mb_x * 8 + (motion_x >> 1));
                sub_pixels(block[5], ptr, s->width >> 1);
            }

            /* DCT & quantize */
            for(i=0;i<6;i++) {
                int last_index;
                last_index = dct_quantize(s, block[i], s->qscale);
                s->block_last_index[i] = last_index;
            }

            /* huffman encode */
            switch(s->out_format) {
            case FMT_MPEG1:
                mpeg1_encode_mb(s, mb_x, mb_y, block, motion_x, motion_y);
                break;
            case FMT_H263:
                h263_encode_mb(s, block, motion_x, motion_y);
                break;
            case FMT_MJPEG:
                mjpeg_encode_mb(s, block);
                break;
            }

            /* decompress blocks so that we keep the state of the decoder */
            if (!s->intra_only) {
                for(i=0;i<6;i++) {
                    if (s->block_last_index[i] >= 0) {
                        dct_unquantize(s, block[i], s->qscale);
                    }
                }

                if (!s->mb_intra) {
                    ptr = s->last_picture[0] + 
                        ((mb_y * 16 + motion_y) * s->width) + (mb_x * 16 + motion_x);
                    
                    add_pixels(block[0], ptr, s->width);
                    add_pixels(block[1], ptr + 8, s->width);
                    add_pixels(block[2], ptr + s->width * 8, s->width);
                    add_pixels(block[3], ptr + 8 + s->width * 8, s->width);
                    ptr = s->last_picture[1] + 
                        ((mb_y * 8 + (motion_y >> 1)) * (s->width >> 1)) + 
                        (mb_x * 8 + (motion_x >> 1));
                    add_pixels(block[4], ptr, s->width >> 1);
                    ptr = s->last_picture[2] + 
                        ((mb_y * 8 + (motion_y >> 1)) * (s->width >> 1)) + 
                        (mb_x * 8 + (motion_x >> 1));
                    add_pixels(block[5], ptr, s->width >> 1);
                }

                /* write the pixels */
                ptr = s->current_picture[0] + (mb_y * 16 * s->width) + mb_x * 16;
                put_pixels(block[0], ptr, s->width);
                put_pixels(block[1], ptr + 8, s->width);
                put_pixels(block[2], ptr + 8 * s->width, s->width);
                put_pixels(block[3], ptr + 8 * s->width + 8, s->width);
                ptr = s->current_picture[1] + (mb_y * 8 * (s->width >> 1)) + mb_x * 8;
                put_pixels(block[4],ptr, s->width >> 1);
                
                ptr = s->current_picture[2] + (mb_y * 8 * (s->width >> 1)) + mb_x * 8;
                put_pixels(block[5],ptr, s->width >> 1);
            }
        }
    }
}

static void mpeg1_encode_mb(MpegEncContext *s, int mb_x, int mb_y,
                            DCTELEM block[6][64],
                            int motion_x, int motion_y)
{
    int mb_incr, i, cbp;

    /* compute cbp */
    cbp = 0;
    for(i=0;i<6;i++) {
        if (s->block_last_index[i] >= 0)
            cbp |= 1 << (5 - i);
    }

    /* skip macroblock, except if first or last macroblock of a slice */
    if ((cbp | motion_x | motion_y) == 0 &&
        (!((mb_x | mb_y) == 0 ||
           (mb_x == s->mb_width - 1 && mb_y == s->mb_height - 1)))) {
        s->mb_incr++;
    } else {
        /* output mb incr */
        mb_incr = s->mb_incr;

        while (mb_incr > 33) {
            put_bits(&s->pb, 11, 0x008);
            mb_incr -= 33;
        }
        put_bits(&s->pb, mbAddrIncrTable[mb_incr][1], 
                 mbAddrIncrTable[mb_incr][0]);
        
        if (s->pict_type == I_TYPE) {
            put_bits(&s->pb, 1, 1); /* macroblock_type : macroblock_quant = 0 */
        } else {
            if (s->mb_intra) {
                put_bits(&s->pb, 5, 0x03);
            } else {
                if (motion_x == 0 && motion_y == 0) {
                    if (cbp != 0) {
                        put_bits(&s->pb, 2, 1); /* macroblock_pattern only */
                        put_bits(&s->pb, mbPatTable[cbp][1], mbPatTable[cbp][0]);
                    } else {
                        put_bits(&s->pb, 3, 1); /* motion only & zero motion vectors */
                        /* zero motion x & y */
                        put_bits(&s->pb, 1, 1); 
                        put_bits(&s->pb, 1, 1); 
                    }
                } else {
                    /* XXX: not used yet */
                    put_bits(&s->pb, mbPatTable[cbp][1], mbPatTable[cbp][0]);
                }
            }
            
        }
        
        for(i=0;i<6;i++) {
            if (cbp & (1 << (5 - i))) {
                encode_block(s, block[i], i);
            }
        }
        s->mb_incr = 1;
    }
}

static void get_pixels(DCTELEM *block, const UINT8 *pixels, int line_size)
{
    DCTELEM *p;
    const UINT8 *pix;
    int i;

    /* read the pixels */
    p = block;
    pix = pixels;
    for(i=0;i<8;i++) {
        p[0] = pix[0];
        p[1] = pix[1];
        p[2] = pix[2];
        p[3] = pix[3];
        p[4] = pix[4];
        p[5] = pix[5];
        p[6] = pix[6];
        p[7] = pix[7];
        pix += line_size;
        p += 8;
    }
}

static void put_pixels(const DCTELEM *block, UINT8 *pixels, int line_size)
{
    const DCTELEM *p;
    UINT8 *pix;
    int i;
    UINT8 *cm = cropTbl + MAX_NEG_CROP;
    
    /* read the pixels */
    p = block;
    pix = pixels;
    for(i=0;i<8;i++) {
        pix[0] = cm[p[0]];
        pix[1] = cm[p[1]];
        pix[2] = cm[p[2]];
        pix[3] = cm[p[3]];
        pix[4] = cm[p[4]];
        pix[5] = cm[p[5]];
        pix[6] = cm[p[6]];
        pix[7] = cm[p[7]];
        pix += line_size;
        p += 8;
    }
}

static void sub_pixels(DCTELEM *block, const UINT8 *pixels, int line_size)
{
    DCTELEM *p;
    const UINT8 *pix;
    int i;

    /* read the pixels */
    p = block;
    pix = pixels;
    for(i=0;i<8;i++) {
        p[0] -= pix[0];
        p[1] -= pix[1];
        p[2] -= pix[2];
        p[3] -= pix[3];
        p[4] -= pix[4];
        p[5] -= pix[5];
        p[6] -= pix[6];
        p[7] -= pix[7];
        pix += line_size;
        p += 8;
    }
}

static void add_pixels(DCTELEM *block, const UINT8 *pixels, int line_size)
{
    DCTELEM *p;
    const UINT8 *pix;
    int i;

    /* read the pixels */
    p = block;
    pix = pixels;
    for(i=0;i<8;i++) {
        p[0] += pix[0];
        p[1] += pix[1];
        p[2] += pix[2];
        p[3] += pix[3];
        p[4] += pix[4];
        p[5] += pix[5];
        p[6] += pix[6];
        p[7] += pix[7];
        pix += line_size;
        p += 8;
    }
}

#define USE_FAST_MUL 

static int dct_quantize(MpegEncContext *s, 
                        DCTELEM *block, 
                        int qscale)
{
    int i, j, level, last_non_zero;
#ifdef USE_FAST_MUL
    const int *qmat;
#else
    const UINT8 *qmat;
#endif

    jpeg_fdct_ifast (block);

    if (s->mb_intra) {
        block[0] = (block[0] + 4 * 8) >> 6;
        i = 1;
        last_non_zero = 0;
        if (s->out_format == FMT_H263) {
#ifdef USE_FAST_MUL
            qmat = s->non_intra_matrix;
#else
            qmat = default_non_intra_matrix;
#endif
        } else {
#ifdef USE_FAST_MUL
            qmat = s->intra_matrix;
#else
            qmat = default_intra_matrix;
#endif
        }
    } else {
        i = 0;
        last_non_zero = -1;
#ifdef USE_FAST_MUL
        qmat = s->non_intra_matrix;
#else
        qmat = default_non_intra_matrix;
#endif
    }

    for(;i<64;i++) {
        j = zigzag_direct[i];
        level = block[j];
#ifdef USE_FAST_MUL
        level = (level * qmat[j]) / (1 << 22);
#else
        /* post dct normalization */
        level = (level << 11) / aanscales[j];
        /* quantification */
        level = (8 * level) / (qscale * qmat[j]);
#endif
        block[j] = level;
        if (level)
            last_non_zero = i;
    }
    return last_non_zero;
}

static void dct_unquantize(MpegEncContext *s, 
                           DCTELEM *block, int qscale)
{
    int i, level, coeff;
    const UINT8 *quant_matrix;

    if (s->mb_intra) {
        block[0] = block[0] << 3;
        if (s->out_format == FMT_H263) {
            i = 1;
            goto unquant_even;
        }
        quant_matrix = default_intra_matrix;
        for(i=1;i<64;i++) {
            block[i] = (block[i] * qscale * quant_matrix[i]) >> 3;
        }
    } else {
        i = 0;
    unquant_even:
        quant_matrix = default_non_intra_matrix;
        for(;i<64;i++) {
            level = block[i];
            if (level) {
                if (level < 0) {
                    coeff = (((level << 1) - 1) * qscale *
                             ((int) (quant_matrix[i]))) >> 4;
                    coeff += (coeff & 1);
                } else {
                    coeff = (((level << 1) + 1) * qscale *
                             ((int) (quant_matrix[i]))) >> 4;
                    coeff -= (coeff & 1);
                }
                block[i] = coeff;
            }
        }
    }

    j_rev_dct(block);
}
                         

static inline void encode_dc(MpegEncContext *s, int diff, int component)
{
    int adiff, index;

    //    printf("dc=%d c=%d\n", diff, component);
    adiff = abs(diff);
    index = vlc_dc_table[adiff];
    if (component == 0) {
        put_bits(&s->pb, vlc_dc_lum_bits[index], vlc_dc_lum_code[index]);
    } else {
        put_bits(&s->pb, vlc_dc_chroma_bits[index], vlc_dc_chroma_code[index]);
    }
    if (diff > 0) {
        put_bits(&s->pb, index, (diff & ((1 << index) - 1)));
    } else if (diff < 0) {
        put_bits(&s->pb, index, ((diff - 1) & ((1 << index) - 1)));
    }
}

static void encode_block(MpegEncContext *s, 
                         DCTELEM *block, 
                         int n)
{
    int alevel, level, last_non_zero, dc, diff, i, j, run, last_index;
    int code, nbits, component;
    
    last_index = s->block_last_index[n];

    /* DC coef */
    if (s->mb_intra) {
        component = (n <= 3 ? 0 : n - 4 + 1);
        dc = block[0]; /* overflow is impossible */
        diff = dc - s->last_dc[component];
        encode_dc(s, diff, component);
        s->last_dc[component] = dc;
        i = 1;
    } else {
        /* encode the first coefficient : needs to be done here because
           it is handled slightly differently */
        level = block[0];
        if (abs(level) == 1) {
                code = ((UINT32)level >> 31); /* the sign bit */
                put_bits(&s->pb, 2, code | 0x02);
                i = 1;
        } else {
            i = 0;
            last_non_zero = -1;
            goto next_coef;
        }
    }

    /* now quantify & encode AC coefs */
    last_non_zero = i - 1;
    for(;i<=last_index;i++) {
        j = zigzag_direct[i];
        level = block[j];
    next_coef:
#if 0
        if (level != 0)
            printf("level[%d]=%d\n", i, level);
#endif            
        /* encode using VLC */
        if (level != 0) {
            run = i - last_non_zero - 1;
            alevel = abs(level);
            //            printf("run=%d level=%d\n", run, level);
            if ( (run < HUFF_MAXRUN) && (alevel < huff_maxlevel[run])) {
                /* encode using the Huffman tables */
                code = (huff_table[run])[alevel];
                nbits = (huff_bits[run])[alevel];
                code |= ((UINT32)level >> 31); /* the sign bit */

                put_bits(&s->pb, nbits, code);
            } else {
                /* escape: only clip in this case */
                if (level > 255)
                    level = 255;
                else if (level < -255)
                    level = -255;
                put_bits(&s->pb, 6, 0x1);
                put_bits(&s->pb, 6, run);
                if (alevel < 128) {
                    put_bits(&s->pb, 8, level & 0xff);
                } else {
                    if (level < 0) {
                        put_bits(&s->pb, 16, 0x8001 + level + 255);
                    } else {
                        put_bits(&s->pb, 16, level & 0xffff);
                    }
                }
            }
            last_non_zero = i;
        }
    }
    /* end of block */
    put_bits(&s->pb, 2, 0x2);
}


/* rate control */

/* an I frame is I_FRAME_SIZE_RATIO bigger than a P frame */
#define I_FRAME_SIZE_RATIO 1.5
#define QSCALE_K           20

static void rate_control_init(MpegEncContext *s)
{
    s->wanted_bits = 0;

    if (s->intra_only) {
        s->I_frame_bits = s->bit_rate / s->frame_rate;
        s->P_frame_bits = s->I_frame_bits;
    } else {
        s->P_frame_bits = (int) ((float)(s->gop_size * s->bit_rate) / 
                    (float)(s->frame_rate * (I_FRAME_SIZE_RATIO + s->gop_size - 1)));
        s->I_frame_bits = (int)(s->P_frame_bits * I_FRAME_SIZE_RATIO);
    }
    
#if defined(DEBUG)
    printf("I_frame_size=%d P_frame_size=%d\n",
           s->I_frame_bits, s->P_frame_bits);
#endif
}


/*
 * This heuristic is rather poor, but at least we do not have to
 * change the qscale at every macroblock.
 */
static int rate_estimate_qscale(MpegEncContext *s)
{
    long long total_bits = s->total_bits;
    float q;
    int qscale, diff;

    if (s->pict_type == I_TYPE) {
        s->wanted_bits += s->I_frame_bits;
    } else {
        s->wanted_bits += s->P_frame_bits;
    }
    diff = s->wanted_bits - total_bits;
    q = 31.0 - (float)diff / (QSCALE_K * s->mb_height * s->mb_width);
    /* adjust for I frame */
    if (s->pict_type == I_TYPE && !s->intra_only) {
        q /= I_FRAME_SIZE_RATIO;
    }

    if (q < 1)
        q = 1;
    else if (q > 31)
        q = 31;
    qscale = (int)(q + 0.5);
#if defined(DEBUG)
    printf("%d: total=%Ld br=%0.1f diff=%d qest=%0.1f\n", 
           s->picture_number, 
           total_bits, (float)s->frame_rate * total_bits / s->picture_number, 
           diff, q);
#endif
    return qscale;
}

AVEncoder mpeg1video_encoder = {
    "mpeg1video",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG1VIDEO,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVEncoder h263_encoder = {
    "h263",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H263,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVEncoder rv10_encoder = {
    "rv10",
    CODEC_TYPE_VIDEO,
    CODEC_ID_RV10,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};

AVEncoder mjpeg_encoder = {
    "mjpeg",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MJPEG,
    sizeof(MpegEncContext),
    MPV_encode_init,
    MPV_encode_picture,
    MPV_encode_end,
};
