/*
 * MPEG1 encoder / MPEG2 decoder
 * Copyright (c) 2000,2001 Gerard Lantau.
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
#include "dsputil.h"
#include "mpegvideo.h"

#include "mpeg12data.h"

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif
//#define DEBUG

#ifdef DEBUG
#define dprintf(fmt,args...) printf(fmt, ## args)
#else
#define dprintf(fmt,args...)
#endif

/* Start codes. */
#define SEQ_END_CODE		0x000001b7
#define SEQ_START_CODE		0x000001b3
#define GOP_START_CODE		0x000001b8
#define PICTURE_START_CODE	0x00000100
#define SLICE_MIN_START_CODE	0x00000101
#define SLICE_MAX_START_CODE	0x000001af
#define EXT_START_CODE		0x000001b5
#define USER_START_CODE		0x000001b2

static void mpeg1_encode_block(MpegEncContext *s, 
                         DCTELEM *block, 
                         int component);
static void mpeg1_encode_motion(MpegEncContext *s, int val);
static void mpeg1_skip_picture(MpegEncContext *s, int pict_num);
static int mpeg1_decode_block(MpegEncContext *s, 
                              DCTELEM *block, 
                              int n);
static int mpeg2_decode_block_non_intra(MpegEncContext *s, 
                                        DCTELEM *block, 
                                        int n);
static int mpeg2_decode_block_intra(MpegEncContext *s, 
                                    DCTELEM *block, 
                                    int n);
static int mpeg_decode_motion(MpegEncContext *s, int fcode, int pred);

static void put_header(MpegEncContext *s, int header)
{
    align_put_bits(&s->pb);
    put_bits(&s->pb, 32, header);
}

/* put sequence header if needed */
static void mpeg1_encode_sequence_header(MpegEncContext *s)
{
        unsigned int vbv_buffer_size;
        unsigned int fps, v;
        int n;
        UINT64 time_code;
        
        if ((s->picture_number % s->gop_size) == 0) {
            /* mpeg1 header repeated every gop */
            put_header(s, SEQ_START_CODE);
            
            /* search closest frame rate */
            {
                int i, dmin, d;
                s->frame_rate_index = 0;
                dmin = 0x7fffffff;
                for(i=1;i<9;i++) {
                    d = abs(s->frame_rate - frame_rate_tab[i]);
                    if (d < dmin) {
                        dmin = d;
                        s->frame_rate_index = i;
                    }
                }
            }
 
            put_bits(&s->pb, 12, s->width);
            put_bits(&s->pb, 12, s->height);
            put_bits(&s->pb, 4, 1); /* 1/1 aspect ratio */
            put_bits(&s->pb, 4, s->frame_rate_index);
            v = s->bit_rate / 400;
            if (v > 0x3ffff)
                v = 0x3ffff;
            put_bits(&s->pb, 18, v);
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
            time_code = s->fake_picture_number * FRAME_RATE_BASE;
            s->gop_picture_number = s->fake_picture_number;
            put_bits(&s->pb, 5, (time_code / (fps * 3600)) % 24);
            put_bits(&s->pb, 6, (time_code / (fps * 60)) % 60);
            put_bits(&s->pb, 1, 1);
            put_bits(&s->pb, 6, (time_code / fps) % 60);
            put_bits(&s->pb, 6, (time_code % fps) / FRAME_RATE_BASE);
            put_bits(&s->pb, 1, 1); /* closed gop */
            put_bits(&s->pb, 1, 0); /* broken link */
        }

        if (s->frame_rate < (24 * FRAME_RATE_BASE) && s->picture_number > 0) {
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
    put_bits(&s->pb, mbAddrIncrTable[mb_incr - 1][1], 
             mbAddrIncrTable[mb_incr - 1][0]);
    
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
    put_bits(&s->pb, mbAddrIncrTable[mb_incr - 1][1], 
             mbAddrIncrTable[mb_incr - 1][0]);
    
    /* empty macroblock */
    put_bits(&s->pb, 3, 1); /* motion only */
    
    /* zero motion x & y */
    put_bits(&s->pb, 1, 1); 
    put_bits(&s->pb, 1, 1); 
}

void mpeg1_encode_picture_header(MpegEncContext *s, int picture_number)
{
    static int done;

    if (!done) {
        done = 1;
        init_rl(&rl_mpeg1);
    }
    mpeg1_encode_sequence_header(s);

    /* mpeg1 picture header */
    put_header(s, PICTURE_START_CODE);
    /* temporal reference */
    put_bits(&s->pb, 10, (s->fake_picture_number - 
                          s->gop_picture_number) & 0x3ff); 
    
    put_bits(&s->pb, 3, s->pict_type);
    put_bits(&s->pb, 16, 0xffff); /* non constant bit rate */
    
    if (s->pict_type == P_TYPE) {
        put_bits(&s->pb, 1, 0); /* half pel coordinates */
        put_bits(&s->pb, 3, s->f_code); /* forward_f_code */
    }
    
    put_bits(&s->pb, 1, 0); /* extra bit picture */
    
    /* only one slice */
    put_header(s, SLICE_MIN_START_CODE);
    put_bits(&s->pb, 5, s->qscale); /* quantizer scale */
    put_bits(&s->pb, 1, 0); /* slice extra information */
}

void mpeg1_encode_mb(MpegEncContext *s,
                     DCTELEM block[6][64],
                     int motion_x, int motion_y)
{
    int mb_incr, i, cbp, mb_x, mb_y;

    mb_x = s->mb_x;
    mb_y = s->mb_y;

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
        put_bits(&s->pb, mbAddrIncrTable[mb_incr - 1][1], 
                 mbAddrIncrTable[mb_incr - 1][0]);
        
        if (s->pict_type == I_TYPE) {
            put_bits(&s->pb, 1, 1); /* macroblock_type : macroblock_quant = 0 */
        } else {
            if (s->mb_intra) {
                put_bits(&s->pb, 5, 0x03);
            } else {
                if (cbp != 0) {
                    if (motion_x == 0 && motion_y == 0) {
                        put_bits(&s->pb, 2, 1); /* macroblock_pattern only */
                        put_bits(&s->pb, mbPatTable[cbp - 1][1], mbPatTable[cbp - 1][0]);
                    } else {
                        put_bits(&s->pb, 1, 1); /* motion + cbp */
                        mpeg1_encode_motion(s, motion_x - s->last_mv[0][0][0]); 
                        mpeg1_encode_motion(s, motion_y - s->last_mv[0][0][1]); 
                        put_bits(&s->pb, mbPatTable[cbp - 1][1], mbPatTable[cbp - 1][0]);
                    }
                } else {
                    put_bits(&s->pb, 3, 1); /* motion only */
                    mpeg1_encode_motion(s, motion_x - s->last_mv[0][0][0]); 
                    mpeg1_encode_motion(s, motion_y - s->last_mv[0][0][1]); 
                }
            }
        }
        for(i=0;i<6;i++) {
            if (cbp & (1 << (5 - i))) {
                mpeg1_encode_block(s, block[i], i);
            }
        }
        s->mb_incr = 1;
    }
    s->last_mv[0][0][0] = motion_x;
    s->last_mv[0][0][1] = motion_y;
}

static void mpeg1_encode_motion(MpegEncContext *s, int val)
{
    int code, bit_size, l, m, bits, range, sign;

    if (val == 0) {
        /* zero vector */
        code = 0;
        put_bits(&s->pb,
                 mbMotionVectorTable[0][1], 
                 mbMotionVectorTable[0][0]); 
    } else {
        bit_size = s->f_code - 1;
        range = 1 << bit_size;
        /* modulo encoding */
        l = 16 * range;
        m = 2 * l;
        if (val < -l) {
            val += m;
        } else if (val >= l) {
            val -= m;
        }

        if (val >= 0) {
            val--;
            code = (val >> bit_size) + 1;
            bits = val & (range - 1);
            sign = 0;
        } else {
            val = -val;
            val--;
            code = (val >> bit_size) + 1;
            bits = val & (range - 1);
            sign = 1;
        }
        put_bits(&s->pb,
                 mbMotionVectorTable[code][1], 
                 mbMotionVectorTable[code][0]); 
        put_bits(&s->pb, 1, sign);
        if (bit_size > 0) {
            put_bits(&s->pb, bit_size, bits);
        }
    }
}

static inline void encode_dc(MpegEncContext *s, int diff, int component)
{
    int adiff, index;

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

static void mpeg1_encode_block(MpegEncContext *s, 
                               DCTELEM *block, 
                               int n)
{
    int alevel, level, last_non_zero, dc, diff, i, j, run, last_index, sign;
    int code, component;
    RLTable *rl = &rl_mpeg1;

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
            dprintf("level[%d]=%d\n", i, level);
#endif            
        /* encode using VLC */
        if (level != 0) {
            run = i - last_non_zero - 1;
            sign = 0;
            alevel = level;
            if (alevel < 0) {
		sign = 1;
                alevel = -alevel;
	    }
            code = get_rl_index(rl, 0, run, alevel);
            put_bits(&s->pb, rl->table_vlc[code][1], rl->table_vlc[code][0]);
            if (code != rl->n) {
                put_bits(&s->pb, 1, sign);
            } else {
                /* escape: only clip in this case */
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

/******************************************/
/* decoding */

static VLC dc_lum_vlc;
static VLC dc_chroma_vlc;
static VLC mv_vlc;
static VLC mbincr_vlc;
static VLC mb_ptype_vlc;
static VLC mb_btype_vlc;
static VLC mb_pat_vlc;

void mpeg1_init_vlc(MpegEncContext *s)
{
    static int done = 0;

    if (!done) {

        init_vlc(&dc_lum_vlc, 9, 12, 
                 vlc_dc_lum_bits, 1, 1,
                 vlc_dc_lum_code, 2, 2);
        init_vlc(&dc_chroma_vlc, 9, 12, 
                 vlc_dc_chroma_bits, 1, 1,
                 vlc_dc_chroma_code, 2, 2);
        init_vlc(&mv_vlc, 9, 17, 
                 &mbMotionVectorTable[0][1], 2, 1,
                 &mbMotionVectorTable[0][0], 2, 1);
        init_vlc(&mbincr_vlc, 9, 34, 
                 &mbAddrIncrTable[0][1], 2, 1,
                 &mbAddrIncrTable[0][0], 2, 1);
        init_vlc(&mb_pat_vlc, 9, 63, 
                 &mbPatTable[0][1], 2, 1,
                 &mbPatTable[0][0], 2, 1);
        
        init_vlc(&mb_ptype_vlc, 6, 32, 
                 &table_mb_ptype[0][1], 2, 1,
                 &table_mb_ptype[0][0], 2, 1);
        init_vlc(&mb_btype_vlc, 6, 32, 
                 &table_mb_btype[0][1], 2, 1,
                 &table_mb_btype[0][0], 2, 1);
        init_rl(&rl_mpeg1);
        init_rl(&rl_mpeg2);
        /* cannot use generic init because we must add the EOB code */
        init_vlc(&rl_mpeg1.vlc, 9, rl_mpeg1.n + 2, 
                 &rl_mpeg1.table_vlc[0][1], 4, 2,
                 &rl_mpeg1.table_vlc[0][0], 4, 2);
        init_vlc(&rl_mpeg2.vlc, 9, rl_mpeg2.n + 2, 
                 &rl_mpeg2.table_vlc[0][1], 4, 2,
                 &rl_mpeg2.table_vlc[0][0], 4, 2);
    }
}

static inline int get_dmv(MpegEncContext *s)
{
    if(get_bits1(&s->gb)) 
        return 1 - (get_bits1(&s->gb) << 1);
    else
        return 0;
}

/* motion type (for mpeg2) */
#define MT_FIELD 1
#define MT_FRAME 2
#define MT_16X8  2
#define MT_DMV   3

static int mpeg_decode_mb(MpegEncContext *s,
                          DCTELEM block[6][64])
{
    int i, j, k, cbp, val, code, mb_type, motion_type;
    
    /* skip mb handling */
    if (s->mb_incr == 0) {
        /* read again increment */
        s->mb_incr = 1;
        for(;;) {
            code = get_vlc(&s->gb, &mbincr_vlc);
            if (code < 0)
                return 1; /* error = end of slice */
            if (code >= 33) {
                if (code == 33) {
                    s->mb_incr += 33;
                }
                /* otherwise, stuffing, nothing to do */
            } else {
                s->mb_incr += code;
                break;
            }
        }
    }
    if (++s->mb_x >= s->mb_width) {
        s->mb_x = 0;
        if (s->mb_y >= (s->mb_height - 1))
            return -1;
        s->mb_y++;
    }
    dprintf("decode_mb: x=%d y=%d\n", s->mb_x, s->mb_y);

    if (--s->mb_incr != 0) {
        /* skip mb */
        s->mb_intra = 0;
        for(i=0;i<6;i++)
            s->block_last_index[i] = -1;
        s->mv_type = MV_TYPE_16X16;
        if (s->pict_type == P_TYPE) {
            /* if P type, zero motion vector is implied */
            s->mv_dir = MV_DIR_FORWARD;
            s->mv[0][0][0] = s->mv[0][0][1] = 0;
            s->last_mv[0][0][0] = s->last_mv[0][0][1] = 0;
        } else {
            /* if B type, reuse previous vectors and directions */
            s->mv[0][0][0] = s->last_mv[0][0][0];
            s->mv[0][0][1] = s->last_mv[0][0][1];
            s->mv[1][0][0] = s->last_mv[1][0][0];
            s->mv[1][0][1] = s->last_mv[1][0][1];
        }
        s->mb_skiped = 1;
        return 0;
    }

    switch(s->pict_type) {
    default:
    case I_TYPE:
        if (get_bits1(&s->gb) == 0) {
            if (get_bits1(&s->gb) == 0)
                return -1;
            mb_type = MB_QUANT | MB_INTRA;
        } else {
            mb_type = MB_INTRA;
        }
        break;
    case P_TYPE:
        mb_type = get_vlc(&s->gb, &mb_ptype_vlc);
        if (mb_type < 0)
            return -1;
        break;
    case B_TYPE:
        mb_type = get_vlc(&s->gb, &mb_btype_vlc);
        if (mb_type < 0)
            return -1;
        break;
    }
    dprintf("mb_type=%x\n", mb_type);
    motion_type = 0; /* avoid warning */
    if (mb_type & (MB_FOR|MB_BACK)) {
        /* get additionnal motion vector type */
        if (s->picture_structure == PICT_FRAME && s->frame_pred_frame_dct) 
            motion_type = MT_FRAME;
        else
            motion_type = get_bits(&s->gb, 2);
    }
    /* compute dct type */
    if (s->picture_structure == PICT_FRAME && 
        !s->frame_pred_frame_dct &&
        (mb_type & (MB_PAT | MB_INTRA))) {
        s->interlaced_dct = get_bits1(&s->gb);
#ifdef DEBUG
        if (s->interlaced_dct)
            printf("interlaced_dct\n");
#endif
    } else {
        s->interlaced_dct = 0; /* frame based */
    }

    if (mb_type & MB_QUANT) {
        if (s->mpeg2) {
            if (s->q_scale_type) {
                s->qscale = non_linear_qscale[get_bits(&s->gb, 5)];
            } else {
                s->qscale = get_bits(&s->gb, 5) << 1;
            }
        } else {
            /* for mpeg1, we use the generic unquant code */
            s->qscale = get_bits(&s->gb, 5);
        }
    }
    if (mb_type & MB_INTRA) {
        if (s->concealment_motion_vectors) {
            /* just parse them */
            if (s->picture_structure != PICT_FRAME) 
                skip_bits1(&s->gb); /* field select */
            mpeg_decode_motion(s, s->mpeg_f_code[0][0], 0);
            mpeg_decode_motion(s, s->mpeg_f_code[0][1], 0);
        }
        s->mb_intra = 1;
        cbp = 0x3f;
        memset(s->last_mv, 0, sizeof(s->last_mv)); /* reset mv prediction */
    } else {
        s->mb_intra = 0;
        cbp = 0;
    }
    /* special case of implicit zero motion vector */
    if (s->pict_type == P_TYPE && !(mb_type & MB_FOR)) {
        s->mv_dir = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        s->last_mv[0][0][0] = 0;
        s->last_mv[0][0][1] = 0;
        s->mv[0][0][0] = 0;
        s->mv[0][0][1] = 0;
    } else if (mb_type & (MB_FOR | MB_BACK)) {
        /* motion vectors */
        s->mv_dir = 0;
        for(i=0;i<2;i++) {
            if (mb_type & (MB_FOR >> i)) {
                s->mv_dir |= (MV_DIR_FORWARD >> i);
                dprintf("mv_type=%d\n", motion_type);
                switch(motion_type) {
                case MT_FRAME: /* or MT_16X8 */
                    if (s->picture_structure == PICT_FRAME) {
                        /* MT_FRAME */
                        s->mv_type = MV_TYPE_16X16;
                        for(k=0;k<2;k++) {
                            val = mpeg_decode_motion(s, s->mpeg_f_code[i][k], 
                                                     s->last_mv[i][0][k]);
                            s->last_mv[i][0][k] = val;
                            s->last_mv[i][1][k] = val;
                            /* full_pel: only for mpeg1 */
                            if (s->full_pel[i])
                                val = val << 1;
                            s->mv[i][0][k] = val;
                            dprintf("mv%d: %d\n", k, val);
                        }
                    } else {
                        /* MT_16X8 */
                        s->mv_type = MV_TYPE_16X8;
                        for(j=0;j<2;j++) {
                            s->field_select[i][j] = get_bits1(&s->gb);
                            for(k=0;k<2;k++) {
                                val = mpeg_decode_motion(s, s->mpeg_f_code[i][k],
                                                         s->last_mv[i][j][k]);
                                s->last_mv[i][j][k] = val;
                                s->mv[i][j][k] = val;
                            }
                        }
                    }
                    break;
                case MT_FIELD:
                    if (s->picture_structure == PICT_FRAME) {
                        s->mv_type = MV_TYPE_FIELD;
                        for(j=0;j<2;j++) {
                            s->field_select[i][j] = get_bits1(&s->gb);
                            val = mpeg_decode_motion(s, s->mpeg_f_code[i][0],
                                                     s->last_mv[i][j][0]);
                            s->last_mv[i][j][0] = val;
                            s->mv[i][j][0] = val;
                            dprintf("fmx=%d\n", val);
                            val = mpeg_decode_motion(s, s->mpeg_f_code[i][1],
                                                     s->last_mv[i][j][1] >> 1);
                            s->last_mv[i][j][1] = val << 1;
                            s->mv[i][j][1] = val;
                            dprintf("fmy=%d\n", val);
                        }
                    } else {
                        s->mv_type = MV_TYPE_16X16;
                        s->field_select[i][0] = get_bits1(&s->gb);
                        for(k=0;k<2;k++) {
                            val = mpeg_decode_motion(s, s->mpeg_f_code[i][k],
                                                     s->last_mv[i][0][k]);
                            s->last_mv[i][0][k] = val;
                            s->last_mv[i][1][k] = val;
                            s->mv[i][0][k] = val;
                        }
                    }
                    break;
                case MT_DMV:
                    {
                        int dmx, dmy, mx, my, m;

                        mx = mpeg_decode_motion(s, s->mpeg_f_code[i][0], 
                                                s->last_mv[i][0][0]);
                        s->last_mv[i][0][0] = mx;
                        s->last_mv[i][1][0] = mx;
                        dmx = get_dmv(s);
                        my = mpeg_decode_motion(s, s->mpeg_f_code[i][1], 
                                                s->last_mv[i][0][1] >> 1);
                        dmy = get_dmv(s);
                        s->mv_type = MV_TYPE_DMV;
                        /* XXX: totally broken */
                        if (s->picture_structure == PICT_FRAME) {
                            s->last_mv[i][0][1] = my << 1;
                            s->last_mv[i][1][1] = my << 1;

                            m = s->top_field_first ? 1 : 3;
                            /* top -> top pred */
                            s->mv[i][0][0] = mx; 
                            s->mv[i][0][1] = my << 1;
                            s->mv[i][1][0] = ((mx * m + (mx > 0)) >> 1) + dmx;
                            s->mv[i][1][1] = ((my * m + (my > 0)) >> 1) + dmy - 1;
                            m = 4 - m;
                            s->mv[i][2][0] = mx;
                            s->mv[i][2][1] = my << 1;
                            s->mv[i][3][0] = ((mx * m + (mx > 0)) >> 1) + dmx;
                            s->mv[i][3][1] = ((my * m + (my > 0)) >> 1) + dmy + 1;
                        } else {
                            s->last_mv[i][0][1] = my;
                            s->last_mv[i][1][1] = my;
                            s->mv[i][0][0] = mx;
                            s->mv[i][0][1] = my;
                            s->mv[i][1][0] = ((mx + (mx > 0)) >> 1) + dmx;
                            s->mv[i][1][1] = ((my + (my > 0)) >> 1) + dmy - 1 
                                /* + 2 * cur_field */;
                        }
                    }
                    break;
                }
            }
        }
    }

    if ((mb_type & MB_INTRA) && s->concealment_motion_vectors) {
        skip_bits1(&s->gb); /* marker */
    }
    
    if (mb_type & MB_PAT) {
        cbp = get_vlc(&s->gb, &mb_pat_vlc);
        if (cbp < 0)
            return -1;
        cbp++;
    }
    dprintf("cbp=%x\n", cbp);

    if (s->mpeg2) {
        if (s->mb_intra) {
            for(i=0;i<6;i++) {
                if (cbp & (1 << (5 - i))) {
                    if (mpeg2_decode_block_intra(s, block[i], i) < 0)
                        return -1;
                }
            }
        } else {
            for(i=0;i<6;i++) {
                if (cbp & (1 << (5 - i))) {
                    if (mpeg2_decode_block_non_intra(s, block[i], i) < 0)
                        return -1;
                }
            }
        }
    } else {
        for(i=0;i<6;i++) {
            if (cbp & (1 << (5 - i))) {
                if (mpeg1_decode_block(s, block[i], i) < 0)
                    return -1;
            }
        }
    }
    return 0;
}

/* as h263, but only 17 codes */
static int mpeg_decode_motion(MpegEncContext *s, int fcode, int pred)
{
    int code, sign, val, m, l, shift;

    code = get_vlc(&s->gb, &mv_vlc);
    if (code < 0) {
        return 0xffff;
    }
    if (code == 0) {
        return pred;
    }
    sign = get_bits1(&s->gb);
    shift = fcode - 1;
    val = (code - 1) << shift;
    if (shift > 0)
        val |= get_bits(&s->gb, shift);
    val++;
    if (sign)
        val = -val;
    val += pred;
    
    /* modulo decoding */
    l = (1 << shift) * 16;
    m = 2 * l;
    if (val < -l) {
        val += m;
    } else if (val >= l) {
        val -= m;
    }
    return val;
}

static inline int decode_dc(MpegEncContext *s, int component)
{
    int code, diff;

    if (component == 0) {
        code = get_vlc(&s->gb, &dc_lum_vlc);
    } else {
        code = get_vlc(&s->gb, &dc_chroma_vlc);
    }
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

static int mpeg1_decode_block(MpegEncContext *s, 
                               DCTELEM *block, 
                               int n)
{
    int level, dc, diff, i, j, run;
    int code, component;
    RLTable *rl = &rl_mpeg1;

    if (s->mb_intra) {
        /* DC coef */
        component = (n <= 3 ? 0 : n - 4 + 1);
        diff = decode_dc(s, component);
        if (diff >= 0xffff)
            return -1;
        dc = s->last_dc[component];
        dc += diff;
        s->last_dc[component] = dc;
        block[0] = dc;
        dprintf("dc=%d diff=%d\n", dc, diff);
        i = 1;
    } else {
        int bit_cnt, v;
        UINT32 bit_buf;
        UINT8 *buf_ptr;
        i = 0;
        /* special case for the first coef. no need to add a second vlc table */
        SAVE_BITS(&s->gb);
        SHOW_BITS(&s->gb, v, 2);
        if (v & 2) {
            run = 0;
            level = 1 - ((v & 1) << 1);
            FLUSH_BITS(2);
            RESTORE_BITS(&s->gb);
            goto add_coef;
        }
        RESTORE_BITS(&s->gb);
    }

    /* now quantify & encode AC coefs */
    for(;;) {
        code = get_vlc(&s->gb, &rl->vlc);
        if (code < 0) {
            return -1;
        }
        if (code == 112) {
            break;
        } else if (code == 111) {
            /* escape */
            run = get_bits(&s->gb, 6);
            level = get_bits(&s->gb, 8);
            level = (level << 24) >> 24;
            if (level == -128) {
                level = get_bits(&s->gb, 8) - 256;
            } else if (level == 0) {
                level = get_bits(&s->gb, 8);
            }
        } else {
            run = rl->table_run[code];
            level = rl->table_level[code];
            if (get_bits1(&s->gb))
                level = -level;
        }
        i += run;
        if (i >= 64)
            return -1;
    add_coef:
        dprintf("%d: run=%d level=%d\n", n, run, level);
	j = zigzag_direct[i];
        block[j] = level;
        i++;
    }
    s->block_last_index[n] = i;
    return 0;
}

/* Also does unquantization here, since I will never support mpeg2
   encoding */
static int mpeg2_decode_block_non_intra(MpegEncContext *s, 
                                        DCTELEM *block, 
                                        int n)
{
    int level, i, j, run;
    int code;
    RLTable *rl = &rl_mpeg1;
    const UINT8 *scan_table;
    const UINT16 *matrix;
    int mismatch;

    if (s->alternate_scan)
        scan_table = ff_alternate_vertical_scan;
    else
        scan_table = zigzag_direct;
    mismatch = 1;

    {
        int bit_cnt, v;
        UINT32 bit_buf;
        UINT8 *buf_ptr;
        i = 0;
        if (n < 4) 
            matrix = s->non_intra_matrix;
        else
            matrix = s->chroma_non_intra_matrix;
            
        /* special case for the first coef. no need to add a second vlc table */
        SAVE_BITS(&s->gb);
        SHOW_BITS(&s->gb, v, 2);
        if (v & 2) {
            run = 0;
            level = 1 - ((v & 1) << 1);
            FLUSH_BITS(2);
            RESTORE_BITS(&s->gb);
            goto add_coef;
        }
        RESTORE_BITS(&s->gb);
    }

    /* now quantify & encode AC coefs */
    for(;;) {
        code = get_vlc(&s->gb, &rl->vlc);
        if (code < 0)
            return -1;
        if (code == 112) {
            break;
        } else if (code == 111) {
            /* escape */
            run = get_bits(&s->gb, 6);
            level = get_bits(&s->gb, 12);
            level = (level << 20) >> 20;
        } else {
            run = rl->table_run[code];
            level = rl->table_level[code];
            if (get_bits1(&s->gb))
                level = -level;
        }
        i += run;
        if (i >= 64)
            return -1;
    add_coef:
	j = scan_table[i];
        dprintf("%d: run=%d level=%d\n", n, run, level);
        level = ((level * 2 + 1) * s->qscale * matrix[j]) / 32;
        /* XXX: is it really necessary to saturate since the encoder
           knows whats going on ? */
        mismatch ^= level;
        block[j] = level;
        i++;
    }
    block[63] ^= (mismatch & 1);
    s->block_last_index[n] = i;
    return 0;
}

static int mpeg2_decode_block_intra(MpegEncContext *s, 
                                    DCTELEM *block, 
                                    int n)
{
    int level, dc, diff, i, j, run;
    int code, component;
    RLTable *rl;
    const UINT8 *scan_table;
    const UINT16 *matrix;
    int mismatch;

    if (s->alternate_scan)
        scan_table = ff_alternate_vertical_scan;
    else
        scan_table = zigzag_direct;
    mismatch = 1;

    /* DC coef */
    component = (n <= 3 ? 0 : n - 4 + 1);
    diff = decode_dc(s, component);
    if (diff >= 0xffff)
        return -1;
    dc = s->last_dc[component];
    dc += diff;
    s->last_dc[component] = dc;
    block[0] = dc << (3 - s->intra_dc_precision);
    dprintf("dc=%d\n", block[0]);
    i = 1;
    if (s->intra_vlc_format)
        rl = &rl_mpeg2;
    else
        rl = &rl_mpeg1;
    if (n < 4) 
        matrix = s->intra_matrix;
    else
        matrix = s->chroma_intra_matrix;
        
    /* now quantify & encode AC coefs */
    for(;;) {
        code = get_vlc(&s->gb, &rl->vlc);
        if (code < 0)
            return -1;
        if (code == 112) {
            break;
        } else if (code == 111) {
            /* escape */
            run = get_bits(&s->gb, 6);
            level = get_bits(&s->gb, 12);
            level = (level << 20) >> 20;
        } else {
            run = rl->table_run[code];
            level = rl->table_level[code];
            if (get_bits1(&s->gb))
                level = -level;
        }
        i += run;
        if (i >= 64)
            return -1;
	j = scan_table[i];
        dprintf("%d: run=%d level=%d\n", n, run, level);
        level = (level * s->qscale * matrix[j]) / 16;
        /* XXX: is it really necessary to saturate since the encoder
           knows whats going on ? */
        mismatch ^= level;
        block[j] = level;
        i++;
    }
    block[63] ^= (mismatch & 1);
    s->block_last_index[n] = i;
    return 0;
}

/* compressed picture size */
#define PICTURE_BUFFER_SIZE 100000

typedef struct Mpeg1Context {
    MpegEncContext mpeg_enc_ctx;
    UINT32 header_state;
    int start_code; /* current start code */
    UINT8 buffer[PICTURE_BUFFER_SIZE]; 
    UINT8 *buf_ptr;
    int buffer_size;
    int mpeg_enc_ctx_allocated; /* true if decoding context allocated */
} Mpeg1Context;

static int mpeg_decode_init(AVCodecContext *avctx)
{
    Mpeg1Context *s = avctx->priv_data;

    s->header_state = 0xff;
    s->mpeg_enc_ctx_allocated = 0;
    s->buffer_size = PICTURE_BUFFER_SIZE;
    s->start_code = -1;
    s->buf_ptr = s->buffer;
    s->mpeg_enc_ctx.picture_number = 0;
    return 0;
}

/* return the 8 bit start code value and update the search
   state. Return -1 if no start code found */
static int find_start_code(UINT8 **pbuf_ptr, UINT8 *buf_end, 
                           UINT32 *header_state)
{
    UINT8 *buf_ptr;
    unsigned int state, v;
    int val;

    state = *header_state;
    buf_ptr = *pbuf_ptr;
    while (buf_ptr < buf_end) {
        v = *buf_ptr++;
        if (state == 0x000001) {
            state = ((state << 8) | v) & 0xffffff;
            val = state;
            goto found;
        }
        state = ((state << 8) | v) & 0xffffff;
    }
    val = -1;
 found:
    *pbuf_ptr = buf_ptr;
    *header_state = state;
    return val;
}

static int mpeg1_decode_picture(AVCodecContext *avctx, 
                                UINT8 *buf, int buf_size)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int ref, f_code;

    init_get_bits(&s->gb, buf, buf_size);

    ref = get_bits(&s->gb, 10); /* temporal ref */
    s->pict_type = get_bits(&s->gb, 3);
    dprintf("pict_type=%d\n", s->pict_type);
    skip_bits(&s->gb, 16);
    if (s->pict_type == P_TYPE || s->pict_type == B_TYPE) {
        s->full_pel[0] = get_bits1(&s->gb);
        f_code = get_bits(&s->gb, 3);
        if (f_code == 0)
            return -1;
        s->mpeg_f_code[0][0] = f_code;
        s->mpeg_f_code[0][1] = f_code;
    }
    if (s->pict_type == B_TYPE) {
        s->full_pel[1] = get_bits1(&s->gb);
        f_code = get_bits(&s->gb, 3);
        if (f_code == 0)
            return -1;
        s->mpeg_f_code[1][0] = f_code;
        s->mpeg_f_code[1][1] = f_code;
    }
    s->y_dc_scale = 8;
    s->c_dc_scale = 8;
    s->first_slice = 1;
    return 0;
}

static void mpeg_decode_sequence_extension(MpegEncContext *s)
{
    int horiz_size_ext, vert_size_ext;
    int bit_rate_ext, vbv_buf_ext, low_delay;
    int frame_rate_ext_n, frame_rate_ext_d;

    skip_bits(&s->gb, 8); /* profil and level */
    skip_bits(&s->gb, 1); /* progressive_sequence */
    skip_bits(&s->gb, 2); /* chroma_format */
    horiz_size_ext = get_bits(&s->gb, 2);
    vert_size_ext = get_bits(&s->gb, 2);
    s->width |= (horiz_size_ext << 12);
    s->height |= (vert_size_ext << 12);
    bit_rate_ext = get_bits(&s->gb, 12);  /* XXX: handle it */
    s->bit_rate = ((s->bit_rate / 400) | (bit_rate_ext << 12)) * 400;
    skip_bits1(&s->gb); /* marker */
    vbv_buf_ext = get_bits(&s->gb, 8);
    low_delay = get_bits1(&s->gb);
    frame_rate_ext_n = get_bits(&s->gb, 2);
    frame_rate_ext_d = get_bits(&s->gb, 5);
    if (frame_rate_ext_d >= 1)
        s->frame_rate = (s->frame_rate * frame_rate_ext_n) / frame_rate_ext_d;
    dprintf("sequence extension\n");
    s->mpeg2 = 1;
}

static void mpeg_decode_quant_matrix_extension(MpegEncContext *s)
{
    int i, v;

    if (get_bits1(&s->gb)) {
        for(i=0;i<64;i++) {
            v = get_bits(&s->gb, 8);
            s->intra_matrix[i] = v;
            s->chroma_intra_matrix[i] = v;
        }
    }
    if (get_bits1(&s->gb)) {
        for(i=0;i<64;i++) {
            v = get_bits(&s->gb, 8);
            s->non_intra_matrix[i] = v;
            s->chroma_non_intra_matrix[i] = v;
        }
    }
    if (get_bits1(&s->gb)) {
        for(i=0;i<64;i++) {
            v = get_bits(&s->gb, 8);
            s->chroma_intra_matrix[i] = v;
        }
    }
    if (get_bits1(&s->gb)) {
        for(i=0;i<64;i++) {
            v = get_bits(&s->gb, 8);
            s->chroma_non_intra_matrix[i] = v;
        }
    }
}

static void mpeg_decode_picture_coding_extension(MpegEncContext *s)
{
    s->full_pel[0] = s->full_pel[1] = 0;
    s->mpeg_f_code[0][0] = get_bits(&s->gb, 4);
    s->mpeg_f_code[0][1] = get_bits(&s->gb, 4);
    s->mpeg_f_code[1][0] = get_bits(&s->gb, 4);
    s->mpeg_f_code[1][1] = get_bits(&s->gb, 4);
    s->intra_dc_precision = get_bits(&s->gb, 2);
    s->picture_structure = get_bits(&s->gb, 2);
    s->top_field_first = get_bits1(&s->gb);
    s->frame_pred_frame_dct = get_bits1(&s->gb);
    s->concealment_motion_vectors = get_bits1(&s->gb);
    s->q_scale_type = get_bits1(&s->gb);
    s->intra_vlc_format = get_bits1(&s->gb);
    s->alternate_scan = get_bits1(&s->gb);
    s->repeat_first_field = get_bits1(&s->gb);
    s->chroma_420_type = get_bits1(&s->gb);
    s->progressive_frame = get_bits1(&s->gb);
    /* composite display not parsed */
    dprintf("dc_preci=%d\n", s->intra_dc_precision);
    dprintf("pict_structure=%d\n", s->picture_structure);
    dprintf("conceal=%d\n", s->concealment_motion_vectors);
    dprintf("intrafmt=%d\n", s->intra_vlc_format);
    dprintf("frame_pred_frame_dct=%d\n", s->frame_pred_frame_dct);
}

static void mpeg_decode_extension(AVCodecContext *avctx, 
                                  UINT8 *buf, int buf_size)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int ext_type;

    init_get_bits(&s->gb, buf, buf_size);
    
    ext_type = get_bits(&s->gb, 4);
    switch(ext_type) {
    case 0x1:
        /* sequence ext */
        mpeg_decode_sequence_extension(s);
        break;
    case 0x3:
        /* quant matrix extension */
        mpeg_decode_quant_matrix_extension(s);
        break;
    case 0x8:
        /* picture extension */
        mpeg_decode_picture_coding_extension(s);
        break;
    }
}

/* return 1 if end of frame */
static int mpeg_decode_slice(AVCodecContext *avctx, 
                              AVPicture *pict,
                              int start_code,
                              UINT8 *buf, int buf_size)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int ret;

    start_code = (start_code - 1) & 0xff;
    if (start_code >= s->mb_height)
        return -1;
    s->last_dc[0] = 1 << (7 + s->intra_dc_precision);
    s->last_dc[1] = s->last_dc[0];
    s->last_dc[2] = s->last_dc[0];
    memset(s->last_mv, 0, sizeof(s->last_mv));
    s->mb_x = -1;
    s->mb_y = start_code;
    s->mb_incr = 0;

    /* start frame decoding */
    if (s->first_slice) {
        s->first_slice = 0;
        MPV_frame_start(s);
    }

    init_get_bits(&s->gb, buf, buf_size);

    s->qscale = get_bits(&s->gb, 5);
    /* extra slice info */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }

    for(;;) {
        memset(s->block, 0, sizeof(s->block));
        ret = mpeg_decode_mb(s, s->block);
        dprintf("ret=%d\n", ret);
        if (ret < 0)
            return -1;
        if (ret == 1)
            break;
        MPV_decode_mb(s, s->block);
    }
    
    /* end of slice reached */
    if (s->mb_x == (s->mb_width - 1) &&
        s->mb_y == (s->mb_height - 1)) {
        /* end of image */
        UINT8 **picture;

        MPV_frame_end(s);

        /* XXX: incorrect reported qscale for mpeg2 */
        if (s->pict_type == B_TYPE) {
            picture = s->current_picture;
            avctx->quality = s->qscale;
        } else {
            /* latency of 1 frame for I and P frames */
            /* XXX: use another variable than picture_number */
            if (s->picture_number == 0) {
                picture = NULL;
            } else {
                picture = s->last_picture;
                avctx->quality = s->last_qscale;
            }
            s->last_qscale = s->qscale;
            s->picture_number++;
        }
        if (picture) {
            pict->data[0] = picture[0];
            pict->data[1] = picture[1];
            pict->data[2] = picture[2];
            pict->linesize[0] = s->linesize;
            pict->linesize[1] = s->linesize / 2;
            pict->linesize[2] = s->linesize / 2;
            return 1;
        } else {
            return 0;
        }
    } else {
        return 0;
    }
}

static int mpeg1_decode_sequence(AVCodecContext *avctx, 
                                 UINT8 *buf, int buf_size)
{
    Mpeg1Context *s1 = avctx->priv_data;
    MpegEncContext *s = &s1->mpeg_enc_ctx;
    int width, height, i, v;
    
    init_get_bits(&s->gb, buf, buf_size);

    width = get_bits(&s->gb, 12);
    height = get_bits(&s->gb, 12);
    skip_bits(&s->gb, 4);
    s->frame_rate_index = get_bits(&s->gb, 4);
    if (s->frame_rate_index == 0)
        return -1;
    s->bit_rate = get_bits(&s->gb, 18) * 400;
    if (get_bits1(&s->gb) == 0) /* marker */
        return -1;
    if (width <= 0 || height <= 0 ||
        (width % 2) != 0 || (height % 2) != 0)
        return -1;
    if (width != s->width ||
        height != s->height) {
        /* start new mpeg1 context decoding */
        s->out_format = FMT_MPEG1;
        if (s1->mpeg_enc_ctx_allocated) {
            MPV_common_end(s);
        }
        s->width = width;
        s->height = height;
        s->has_b_frames = 1;
        avctx->width = width;
        avctx->height = height;
        avctx->frame_rate = frame_rate_tab[s->frame_rate_index];
        avctx->bit_rate = s->bit_rate;
        
        if (MPV_common_init(s) < 0)
            return -1;
        mpeg1_init_vlc(s);
        s1->mpeg_enc_ctx_allocated = 1;
    }

    skip_bits(&s->gb, 10); /* vbv_buffer_size */
    skip_bits(&s->gb, 1);

    /* get matrix */
    if (get_bits1(&s->gb)) {
        for(i=0;i<64;i++) {
            v = get_bits(&s->gb, 8);
            s->intra_matrix[i] = v;
            s->chroma_intra_matrix[i] = v;
        }
    } else {
        for(i=0;i<64;i++) {
            v = default_intra_matrix[i];
            s->intra_matrix[i] = v;
            s->chroma_intra_matrix[i] = v;
        }
    }
    if (get_bits1(&s->gb)) {
        for(i=0;i<64;i++) {
            v = get_bits(&s->gb, 8);
            s->non_intra_matrix[i] = v;
            s->chroma_non_intra_matrix[i] = v;
        }
    } else {
        for(i=0;i<64;i++) {
            v = default_non_intra_matrix[i];
            s->non_intra_matrix[i] = v;
            s->chroma_non_intra_matrix[i] = v;
        }
    }

    /* we set mpeg2 parameters so that it emulates mpeg1 */
    s->progressive_sequence = 1;
    s->progressive_frame = 1;
    s->picture_structure = PICT_FRAME;
    s->frame_pred_frame_dct = 1;
    s->mpeg2 = 0;
    return 0;
}

/* handle buffering and image synchronisation */
static int mpeg_decode_frame(AVCodecContext *avctx, 
                             void *data, int *data_size,
                             UINT8 *buf, int buf_size)
{
    Mpeg1Context *s = avctx->priv_data;
    UINT8 *buf_end, *buf_ptr, *buf_start;
    int len, start_code_found, ret, code, start_code, input_size;
    AVPicture *picture = data;

    dprintf("fill_buffer\n");

    *data_size = 0;
    /* special case for last picture */
    if (buf_size == 0) {
        MpegEncContext *s2 = &s->mpeg_enc_ctx;
        if (s2->picture_number > 0) {
            picture->data[0] = s2->next_picture[0];
            picture->data[1] = s2->next_picture[1];
            picture->data[2] = s2->next_picture[2];
            picture->linesize[0] = s2->linesize;
            picture->linesize[1] = s2->linesize / 2;
            picture->linesize[2] = s2->linesize / 2;
            *data_size = sizeof(AVPicture);
        }
        return 0;
    }

    buf_ptr = buf;
    buf_end = buf + buf_size;
    while (buf_ptr < buf_end) {
        buf_start = buf_ptr;
        /* find start next code */
        code = find_start_code(&buf_ptr, buf_end, &s->header_state);
        if (code >= 0) {
            start_code_found = 1;
        } else {
            start_code_found = 0;
        }
        /* copy to buffer */
        len = buf_ptr - buf_start;
        if (len + (s->buf_ptr - s->buffer) > s->buffer_size) {
            /* data too big : flush */
            s->buf_ptr = s->buffer;
            if (start_code_found)
                s->start_code = code;
        } else {
            memcpy(s->buf_ptr, buf_start, len);
            s->buf_ptr += len;
            
            if (start_code_found) {
                /* prepare data for next start code */
                input_size = s->buf_ptr - s->buffer;
                start_code = s->start_code;
                s->buf_ptr = s->buffer;
                s->start_code = code;
                switch(start_code) {
                case SEQ_START_CODE:
                    mpeg1_decode_sequence(avctx, s->buffer, 
                                          input_size);
                    break;
                            
                case PICTURE_START_CODE:
                    /* we have a complete image : we try to decompress it */
                    mpeg1_decode_picture(avctx, 
                                         s->buffer, input_size);
                    break;
                case EXT_START_CODE:
                    mpeg_decode_extension(avctx,
                                          s->buffer, input_size);
                    break;
                default:
                    if (start_code >= SLICE_MIN_START_CODE &&
                        start_code <= SLICE_MAX_START_CODE) {
                        ret = mpeg_decode_slice(avctx, picture,
                                                start_code, s->buffer, input_size);
                        if (ret == 1) {
                            /* got a picture: exit */
                            *data_size = sizeof(AVPicture);
                            goto the_end;
                        }
                    }
                    break;
                }
            }
        }
    }
 the_end:
    return buf_ptr - buf;
}

static int mpeg_decode_end(AVCodecContext *avctx)
{
    Mpeg1Context *s = avctx->priv_data;

    if (s->mpeg_enc_ctx_allocated)
        MPV_common_end(&s->mpeg_enc_ctx);
    return 0;
}

AVCodec mpeg_decoder = {
    "mpegvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_MPEG1VIDEO,
    sizeof(Mpeg1Context),
    mpeg_decode_init,
    NULL,
    mpeg_decode_end,
    mpeg_decode_frame,
};
