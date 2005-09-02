/*
 * DV decoder
 * Copyright (c) 2002 Fabrice Bellard.
 * Copyright (c) 2004 Roman Shaposhnik.
 *
 * DV encoder 
 * Copyright (c) 2003 Roman Shaposhnik.
 *
 * Many thanks to Dan Dennedy <dan@dennedy.org> for providing wealth
 * of DV technical info.
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

/**
 * @file dv.c
 * DV codec.
 */
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "simple_idct.h"
#include "dvdata.h"

//#undef NDEBUG
//#include <assert.h>

typedef struct DVVideoContext {
    const DVprofile* sys;
    AVFrame picture;
    AVCodecContext *avctx;
    uint8_t *buf;
    
    uint8_t dv_zigzag[2][64];
    uint8_t dv_idct_shift[2][2][22][64];
  
    void (*get_pixels)(DCTELEM *block, const uint8_t *pixels, int line_size);
    void (*fdct[2])(DCTELEM *block);
    void (*idct_put[2])(uint8_t *dest, int line_size, DCTELEM *block);
} DVVideoContext;

#define TEX_VLC_BITS 9

#ifdef DV_CODEC_TINY_TARGET
#define DV_VLC_MAP_RUN_SIZE 15
#define DV_VLC_MAP_LEV_SIZE 23
#else
#define DV_VLC_MAP_RUN_SIZE  64 
#define DV_VLC_MAP_LEV_SIZE 512 //FIXME sign was removed so this should be /2 but needs check
#endif

/* MultiThreading */
static uint8_t** dv_anchor;

/* XXX: also include quantization */
static RL_VLC_ELEM *dv_rl_vlc;
/* VLC encoding lookup table */
static struct dv_vlc_pair {
   uint32_t vlc;
   uint8_t  size;
} (*dv_vlc_map)[DV_VLC_MAP_LEV_SIZE] = NULL;

static void dv_build_unquantize_tables(DVVideoContext *s, uint8_t* perm)
{
    int i, q, j;

    /* NOTE: max left shift is 6 */
    for(q = 0; q < 22; q++) {
        /* 88DCT */
        for(i = 1; i < 64; i++) {
            /* 88 table */
            j = perm[i];
            s->dv_idct_shift[0][0][q][j] =
                dv_quant_shifts[q][dv_88_areas[i]] + 1;
	    s->dv_idct_shift[1][0][q][j] = s->dv_idct_shift[0][0][q][j] + 1;
        }
        
        /* 248DCT */
        for(i = 1; i < 64; i++) {
            /* 248 table */
            s->dv_idct_shift[0][1][q][i] =  
                dv_quant_shifts[q][dv_248_areas[i]] + 1;
	    s->dv_idct_shift[1][1][q][i] = s->dv_idct_shift[0][1][q][i] + 1;
        }
    }
}

static int dvvideo_init(AVCodecContext *avctx)
{
    DVVideoContext *s = avctx->priv_data;
    DSPContext dsp;
    static int done=0;
    int i, j;

    if (!done) {
        VLC dv_vlc;
        uint16_t new_dv_vlc_bits[NB_DV_VLC*2];
        uint8_t new_dv_vlc_len[NB_DV_VLC*2];
        uint8_t new_dv_vlc_run[NB_DV_VLC*2];
        int16_t new_dv_vlc_level[NB_DV_VLC*2];

        done = 1;

        dv_vlc_map = av_mallocz_static(DV_VLC_MAP_LEV_SIZE*DV_VLC_MAP_RUN_SIZE*sizeof(struct dv_vlc_pair));
	if (!dv_vlc_map)
	    return -ENOMEM;

	/* dv_anchor lets each thread know its Id */
	dv_anchor = av_malloc(12*27*sizeof(void*));
	if (!dv_anchor) {
	    return -ENOMEM;
	}
	for (i=0; i<12*27; i++)
	    dv_anchor[i] = (void*)(size_t)i;

	/* it's faster to include sign bit in a generic VLC parsing scheme */
	for (i=0, j=0; i<NB_DV_VLC; i++, j++) {
	    new_dv_vlc_bits[j] = dv_vlc_bits[i];
	    new_dv_vlc_len[j] = dv_vlc_len[i];
	    new_dv_vlc_run[j] = dv_vlc_run[i];
	    new_dv_vlc_level[j] = dv_vlc_level[i];
	    
	    if (dv_vlc_level[i]) {
	        new_dv_vlc_bits[j] <<= 1;
		new_dv_vlc_len[j]++;

		j++;
		new_dv_vlc_bits[j] = (dv_vlc_bits[i] << 1) | 1;
		new_dv_vlc_len[j] = dv_vlc_len[i] + 1;
		new_dv_vlc_run[j] = dv_vlc_run[i];
		new_dv_vlc_level[j] = -dv_vlc_level[i];
	    }
	}
             
        /* NOTE: as a trick, we use the fact the no codes are unused
           to accelerate the parsing of partial codes */
        init_vlc(&dv_vlc, TEX_VLC_BITS, j, 
                 new_dv_vlc_len, 1, 1, new_dv_vlc_bits, 2, 2, 0);

        dv_rl_vlc = av_malloc(dv_vlc.table_size * sizeof(RL_VLC_ELEM));
	if (!dv_rl_vlc) {
	    av_free(dv_anchor);
	    return -ENOMEM;
	}
        for(i = 0; i < dv_vlc.table_size; i++){
            int code= dv_vlc.table[i][0];
            int len = dv_vlc.table[i][1];
            int level, run;
        
            if(len<0){ //more bits needed
                run= 0;
                level= code;
            } else {
                run=   new_dv_vlc_run[code] + 1;
                level= new_dv_vlc_level[code];
            }
            dv_rl_vlc[i].len = len;
            dv_rl_vlc[i].level = level;
            dv_rl_vlc[i].run = run;
        }
	free_vlc(&dv_vlc);

	for (i = 0; i < NB_DV_VLC - 1; i++) {
           if (dv_vlc_run[i] >= DV_VLC_MAP_RUN_SIZE)
	       continue;
#ifdef DV_CODEC_TINY_TARGET
           if (dv_vlc_level[i] >= DV_VLC_MAP_LEV_SIZE)
	       continue;
#endif
	   
	   if (dv_vlc_map[dv_vlc_run[i]][dv_vlc_level[i]].size != 0)
	       continue;
	       
	   dv_vlc_map[dv_vlc_run[i]][dv_vlc_level[i]].vlc = dv_vlc_bits[i] << 
	                                                    (!!dv_vlc_level[i]);
	   dv_vlc_map[dv_vlc_run[i]][dv_vlc_level[i]].size = dv_vlc_len[i] + 
	                                                     (!!dv_vlc_level[i]);
	}
	for (i = 0; i < DV_VLC_MAP_RUN_SIZE; i++) {
#ifdef DV_CODEC_TINY_TARGET
	   for (j = 1; j < DV_VLC_MAP_LEV_SIZE; j++) {
	      if (dv_vlc_map[i][j].size == 0) {
	          dv_vlc_map[i][j].vlc = dv_vlc_map[0][j].vlc |
		            (dv_vlc_map[i-1][0].vlc << (dv_vlc_map[0][j].size));
	          dv_vlc_map[i][j].size = dv_vlc_map[i-1][0].size + 
		                          dv_vlc_map[0][j].size;
	      }
	   }
#else
	   for (j = 1; j < DV_VLC_MAP_LEV_SIZE/2; j++) {
	      if (dv_vlc_map[i][j].size == 0) {
	          dv_vlc_map[i][j].vlc = dv_vlc_map[0][j].vlc |
		            (dv_vlc_map[i-1][0].vlc << (dv_vlc_map[0][j].size));
	          dv_vlc_map[i][j].size = dv_vlc_map[i-1][0].size + 
		                          dv_vlc_map[0][j].size;
	      }
	      dv_vlc_map[i][((uint16_t)(-j))&0x1ff].vlc = 
	                                    dv_vlc_map[i][j].vlc | 1;
	      dv_vlc_map[i][((uint16_t)(-j))&0x1ff].size = 
	                                    dv_vlc_map[i][j].size;
	   }
#endif
	}
    }

    /* Generic DSP setup */
    dsputil_init(&dsp, avctx);
    s->get_pixels = dsp.get_pixels;

    /* 88DCT setup */
    s->fdct[0] = dsp.fdct;
    s->idct_put[0] = dsp.idct_put;
    for (i=0; i<64; i++)
       s->dv_zigzag[0][i] = dsp.idct_permutation[ff_zigzag_direct[i]];

    /* 248DCT setup */
    s->fdct[1] = dsp.fdct248;
    s->idct_put[1] = simple_idct248_put;  // FIXME: need to add it to DSP
    if(avctx->lowres){
        for (i=0; i<64; i++){
            int j= ff_zigzag248_direct[i];
            s->dv_zigzag[1][i] = dsp.idct_permutation[(j&7) + (j&8)*4 + (j&48)/2];
        }
    }else
        memcpy(s->dv_zigzag[1], ff_zigzag248_direct, 64);

    /* XXX: do it only for constant case */
    dv_build_unquantize_tables(s, dsp.idct_permutation);

    /* FIXME: I really don't think this should be here */
    if (dv_codec_profile(avctx))
	avctx->pix_fmt = dv_codec_profile(avctx)->pix_fmt; 
    avctx->coded_frame = &s->picture;
    s->avctx= avctx;
    
    return 0;
}

// #define VLC_DEBUG
// #define printf(...) av_log(NULL, AV_LOG_ERROR, __VA_ARGS__)

typedef struct BlockInfo {
    const uint8_t *shift_table;
    const uint8_t *scan_table;
    uint8_t pos; /* position in block */
    uint8_t dct_mode;
    uint8_t partial_bit_count;
    uint16_t partial_bit_buffer;
    int shift_offset;
} BlockInfo;

/* block size in bits */
static const uint16_t block_sizes[6] = {
    112, 112, 112, 112, 80, 80
};
/* bit budget for AC only in 5 MBs */
static const int vs_total_ac_bits = (100 * 4 + 68*2) * 5;
/* see dv_88_areas and dv_248_areas for details */
static const int mb_area_start[5] = { 1, 6, 21, 43, 64 }; 

#ifndef ALT_BITSTREAM_READER
#warning only works with ALT_BITSTREAM_READER
static int re_index; //Hack to make it compile
#endif

static inline int get_bits_left(GetBitContext *s)
{
    return s->size_in_bits - get_bits_count(s);
}

static inline int get_bits_size(GetBitContext *s)
{
    return s->size_in_bits;
}

static inline int put_bits_left(PutBitContext* s)
{
    return (s->buf_end - s->buf) * 8 - put_bits_count(s);
}

/* decode ac coefs */
static void dv_decode_ac(GetBitContext *gb, BlockInfo *mb, DCTELEM *block)
{
    int last_index = get_bits_size(gb);
    const uint8_t *scan_table = mb->scan_table;
    const uint8_t *shift_table = mb->shift_table;
    int pos = mb->pos;
    int partial_bit_count = mb->partial_bit_count;
    int level, pos1, run, vlc_len, index;
    
    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    
    /* if we must parse a partial vlc, we do it here */
    if (partial_bit_count > 0) {
        re_cache = ((unsigned)re_cache >> partial_bit_count) |
	           (mb->partial_bit_buffer << (sizeof(re_cache)*8 - partial_bit_count));
	re_index -= partial_bit_count;
	mb->partial_bit_count = 0;
    }

    /* get the AC coefficients until last_index is reached */
    for(;;) {
#ifdef VLC_DEBUG
        printf("%2d: bits=%04x index=%d\n", pos, SHOW_UBITS(re, gb, 16), re_index);
#endif
        /* our own optimized GET_RL_VLC */
        index = NEG_USR32(re_cache, TEX_VLC_BITS);
	vlc_len = dv_rl_vlc[index].len;
        if (vlc_len < 0) {
            index = NEG_USR32((unsigned)re_cache << TEX_VLC_BITS, -vlc_len) + dv_rl_vlc[index].level;
            vlc_len = TEX_VLC_BITS - vlc_len;
        }
        level = dv_rl_vlc[index].level;
	run = dv_rl_vlc[index].run;
	
	/* gotta check if we're still within gb boundaries */
	if (re_index + vlc_len > last_index) {
	    /* should be < 16 bits otherwise a codeword could have been parsed */
	    mb->partial_bit_count = last_index - re_index;
	    mb->partial_bit_buffer = NEG_USR32(re_cache, mb->partial_bit_count);
	    re_index = last_index;
	    break;
	}
	re_index += vlc_len;

#ifdef VLC_DEBUG
	printf("run=%d level=%d\n", run, level);
#endif
	pos += run; 	
	if (pos >= 64)
	    break;
        
        assert(level);
        pos1 = scan_table[pos];
        block[pos1] = level << shift_table[pos1];

        UPDATE_CACHE(re, gb);
    }
    CLOSE_READER(re, gb);
    mb->pos = pos;
}

static inline void bit_copy(PutBitContext *pb, GetBitContext *gb)
{
    int bits_left = get_bits_left(gb);
    while (bits_left >= MIN_CACHE_BITS) {
        put_bits(pb, MIN_CACHE_BITS, get_bits(gb, MIN_CACHE_BITS));
        bits_left -= MIN_CACHE_BITS;
    }
    if (bits_left > 0) {
        put_bits(pb, bits_left, get_bits(gb, bits_left));
    }
}

/* mb_x and mb_y are in units of 8 pixels */
static inline void dv_decode_video_segment(DVVideoContext *s, 
                                           uint8_t *buf_ptr1, 
                                           const uint16_t *mb_pos_ptr)
{
    int quant, dc, dct_mode, class1, j;
    int mb_index, mb_x, mb_y, v, last_index;
    DCTELEM *block, *block1;
    int c_offset;
    uint8_t *y_ptr;
    void (*idct_put)(uint8_t *dest, int line_size, DCTELEM *block);
    uint8_t *buf_ptr;
    PutBitContext pb, vs_pb;
    GetBitContext gb;
    BlockInfo mb_data[5 * 6], *mb, *mb1;
    DCTELEM sblock[5*6][64] __align8;
    uint8_t mb_bit_buffer[80 + 4] __align8; /* allow some slack */
    uint8_t vs_bit_buffer[5 * 80 + 4] __align8; /* allow some slack */
    const int log2_blocksize= 3-s->avctx->lowres;
	    
    assert((((int)mb_bit_buffer)&7)==0);
    assert((((int)vs_bit_buffer)&7)==0);
    
    memset(sblock, 0, sizeof(sblock));

    /* pass 1 : read DC and AC coefficients in blocks */
    buf_ptr = buf_ptr1;
    block1 = &sblock[0][0];
    mb1 = mb_data;
    init_put_bits(&vs_pb, vs_bit_buffer, 5 * 80);
    for(mb_index = 0; mb_index < 5; mb_index++, mb1 += 6, block1 += 6 * 64) {
        /* skip header */
        quant = buf_ptr[3] & 0x0f;
        buf_ptr += 4;
        init_put_bits(&pb, mb_bit_buffer, 80);
        mb = mb1;
        block = block1;
        for(j = 0;j < 6; j++) {
            last_index = block_sizes[j];
	    init_get_bits(&gb, buf_ptr, last_index);
            
            /* get the dc */
            dc = get_sbits(&gb, 9);
            dct_mode = get_bits1(&gb);
            mb->dct_mode = dct_mode;
            mb->scan_table = s->dv_zigzag[dct_mode];
            class1 = get_bits(&gb, 2);
            mb->shift_table = s->dv_idct_shift[class1 == 3][dct_mode]
                [quant + dv_quant_offset[class1]];
            dc = dc << 2;
            /* convert to unsigned because 128 is not added in the
               standard IDCT */
            dc += 1024;
            block[0] = dc;
            buf_ptr += last_index >> 3;
            mb->pos = 0;
            mb->partial_bit_count = 0;

#ifdef VLC_DEBUG
            printf("MB block: %d, %d ", mb_index, j);
#endif
            dv_decode_ac(&gb, mb, block);

            /* write the remaining bits  in a new buffer only if the
               block is finished */
            if (mb->pos >= 64)
                bit_copy(&pb, &gb);
            
            block += 64;
            mb++;
        }
        
        /* pass 2 : we can do it just after */
#ifdef VLC_DEBUG
        printf("***pass 2 size=%d MB#=%d\n", put_bits_count(&pb), mb_index);
#endif
        block = block1;
        mb = mb1;
        init_get_bits(&gb, mb_bit_buffer, put_bits_count(&pb));
	flush_put_bits(&pb);
        for(j = 0;j < 6; j++, block += 64, mb++) {
            if (mb->pos < 64 && get_bits_left(&gb) > 0) {
                dv_decode_ac(&gb, mb, block);
                /* if still not finished, no need to parse other blocks */
                if (mb->pos < 64)
                    break;
            }
        }
        /* all blocks are finished, so the extra bytes can be used at
           the video segment level */
        if (j >= 6)
	    bit_copy(&vs_pb, &gb);
    }

    /* we need a pass other the whole video segment */
#ifdef VLC_DEBUG
    printf("***pass 3 size=%d\n", put_bits_count(&vs_pb));
#endif
    block = &sblock[0][0];
    mb = mb_data;
    init_get_bits(&gb, vs_bit_buffer, put_bits_count(&vs_pb));
    flush_put_bits(&vs_pb);
    for(mb_index = 0; mb_index < 5; mb_index++) {
        for(j = 0;j < 6; j++) {
            if (mb->pos < 64) {
#ifdef VLC_DEBUG
                printf("start %d:%d\n", mb_index, j);
#endif
                dv_decode_ac(&gb, mb, block);
            }
	    if (mb->pos >= 64 && mb->pos < 127)
		av_log(NULL, AV_LOG_ERROR, "AC EOB marker is absent pos=%d\n", mb->pos);
            block += 64;
            mb++;
        }
    }
    
    /* compute idct and place blocks */
    block = &sblock[0][0];
    mb = mb_data;
    for(mb_index = 0; mb_index < 5; mb_index++) {
        v = *mb_pos_ptr++;
        mb_x = v & 0xff;
        mb_y = v >> 8;
        y_ptr = s->picture.data[0] + ((mb_y * s->picture.linesize[0] + mb_x)<<log2_blocksize);
        if (s->sys->pix_fmt == PIX_FMT_YUV411P)
            c_offset = ((mb_y * s->picture.linesize[1] + (mb_x >> 2))<<log2_blocksize);
        else
            c_offset = (((mb_y >> 1) * s->picture.linesize[1] + (mb_x >> 1))<<log2_blocksize);
        for(j = 0;j < 6; j++) {
            idct_put = s->idct_put[mb->dct_mode && log2_blocksize==3];
            if (j < 4) {
                if (s->sys->pix_fmt == PIX_FMT_YUV411P && mb_x < (704 / 8)) {
                    /* NOTE: at end of line, the macroblock is handled as 420 */
                    idct_put(y_ptr + (j<<log2_blocksize), s->picture.linesize[0], block);
                } else {
                    idct_put(y_ptr + (((j & 1) + (j >> 1) * s->picture.linesize[0])<<log2_blocksize),
                             s->picture.linesize[0], block);
                }
            } else {
                if (s->sys->pix_fmt == PIX_FMT_YUV411P && mb_x >= (704 / 8)) {
                    uint64_t aligned_pixels[64/8];
                    uint8_t *pixels= (uint8_t*)aligned_pixels;
		    uint8_t *c_ptr, *c_ptr1, *ptr, *ptr1;
                    int x, y, linesize;
                    /* NOTE: at end of line, the macroblock is handled as 420 */
                    idct_put(pixels, 8, block);
                    linesize = s->picture.linesize[6 - j];
                    c_ptr = s->picture.data[6 - j] + c_offset;
                    ptr = pixels;
                    for(y = 0;y < (1<<log2_blocksize); y++) {
                        ptr1= ptr + (1<<(log2_blocksize-1));
                        c_ptr1 = c_ptr + (linesize<<log2_blocksize);
                        for(x=0; x < (1<<(log2_blocksize-1)); x++){
                            c_ptr[x]= ptr[x]; c_ptr1[x]= ptr1[x];
                        }
                        c_ptr += linesize;
                        ptr += 8;
                    }
                } else {
                    /* don't ask me why they inverted Cb and Cr ! */
                    idct_put(s->picture.data[6 - j] + c_offset, 
                             s->picture.linesize[6 - j], block);
                }
            }
            block += 64;
            mb++;
        }
    }
}

#ifdef DV_CODEC_TINY_TARGET
/* Converts run and level (where level != 0) pair into vlc, returning bit size */
static always_inline int dv_rl2vlc(int run, int level, int sign, uint32_t* vlc)
{
    int size;
    if (run < DV_VLC_MAP_RUN_SIZE && level < DV_VLC_MAP_LEV_SIZE) {
        *vlc = dv_vlc_map[run][level].vlc | sign;
	size = dv_vlc_map[run][level].size;
    }
    else { 
        if (level < DV_VLC_MAP_LEV_SIZE) {
	    *vlc = dv_vlc_map[0][level].vlc | sign;
	    size = dv_vlc_map[0][level].size;
	} else {
            *vlc = 0xfe00 | (level << 1) | sign;
	    size = 16;
	}
	if (run) {
	    *vlc |= ((run < 16) ? dv_vlc_map[run-1][0].vlc : 
	                          (0x1f80 | (run - 1))) << size;
	    size += (run < 16) ? dv_vlc_map[run-1][0].size : 13;
	}
    }
    
    return size;
}

static always_inline int dv_rl2vlc_size(int run, int level)
{
    int size;
    
    if (run < DV_VLC_MAP_RUN_SIZE && level < DV_VLC_MAP_LEV_SIZE) {
	size = dv_vlc_map[run][level].size; 
    }
    else { 
	size = (level < DV_VLC_MAP_LEV_SIZE) ? dv_vlc_map[0][level].size : 16;
	if (run) {
	    size += (run < 16) ? dv_vlc_map[run-1][0].size : 13;
	}
    }
    return size;
}
#else
static always_inline int dv_rl2vlc(int run, int l, int sign, uint32_t* vlc)
{
    *vlc = dv_vlc_map[run][l].vlc | sign;
    return dv_vlc_map[run][l].size;
}

static always_inline int dv_rl2vlc_size(int run, int l)
{
    return dv_vlc_map[run][l].size;
}
#endif

typedef struct EncBlockInfo {
    int area_q[4];
    int bit_size[4];
    int prev[5];
    int cur_ac;
    int cno;
    int dct_mode;
    DCTELEM mb[64];
    uint8_t next[64];
    uint8_t sign[64];
    uint8_t partial_bit_count;
    uint32_t partial_bit_buffer; /* we can't use uint16_t here */
} EncBlockInfo;

static always_inline PutBitContext* dv_encode_ac(EncBlockInfo* bi, PutBitContext* pb_pool, 
                                       PutBitContext* pb_end)
{
    int prev;
    int bits_left;
    PutBitContext* pb = pb_pool;
    int size = bi->partial_bit_count;
    uint32_t vlc = bi->partial_bit_buffer;

    bi->partial_bit_count = bi->partial_bit_buffer = 0;
    for(;;){
       /* Find suitable storage space */
       for (; size > (bits_left = put_bits_left(pb)); pb++) {
          if (bits_left) {
              size -= bits_left;
	      put_bits(pb, bits_left, vlc >> size);
	      vlc = vlc & ((1<<size)-1);
	  }
	  if (pb + 1 >= pb_end) {
	      bi->partial_bit_count = size;
	      bi->partial_bit_buffer = vlc;
	      return pb;
	  }
       }
       
       /* Store VLC */
       put_bits(pb, size, vlc);
       
       if(bi->cur_ac>=64)
           break;
       
       /* Construct the next VLC */
       prev= bi->cur_ac;
       bi->cur_ac = bi->next[prev];
       if(bi->cur_ac < 64){
           size = dv_rl2vlc(bi->cur_ac - prev - 1, bi->mb[bi->cur_ac], bi->sign[bi->cur_ac], &vlc);
       } else {
           size = 4; vlc = 6; /* End Of Block stamp */
       }
    }
    return pb;
}

static always_inline void dv_set_class_number(DCTELEM* blk, EncBlockInfo* bi, 
                                              const uint8_t* zigzag_scan, int bias)
{
    int i, area;
    static const int classes[] = {12, 24, 36, 0xffff};
    int max=12;
    int prev=0;

    bi->mb[0] = blk[0]; 
    
    for (area = 0; area < 4; area++) {
       bi->prev[area] = prev;
       bi->bit_size[area] = 1; // 4 areas 4 bits for EOB :)
       for (i=mb_area_start[area]; i<mb_area_start[area+1]; i++) {
          int level = blk[zigzag_scan[i]];
       
          if (level+15 > 30U) {
              bi->sign[i] = (level>>31)&1;
              bi->mb[i] = level= ABS(level)>>4;
              if(level>max) max= level;
              bi->bit_size[area] += dv_rl2vlc_size(i - prev  - 1, level);
              bi->next[prev]= i;
              prev= i;
          }
       }
    }
    bi->next[prev]= i;
    for(bi->cno = 0; max > classes[bi->cno]; bi->cno++);

    bi->cno += bias;
    
    if (bi->cno >= 3) {
        bi->cno = 3;
        prev=0;
        i= bi->next[prev];
        for (area = 0; area < 4; area++) {
            bi->prev[area] = prev;
            bi->bit_size[area] = 1; // 4 areas 4 bits for EOB :)
            for (; i<mb_area_start[area+1]; i= bi->next[i]) {
                bi->mb[i] >>=1;
            
                if (bi->mb[i]) {
                    bi->bit_size[area] += dv_rl2vlc_size(i - prev - 1, bi->mb[i]);
                    bi->next[prev]= i;
                    prev= i;
                }
            }
        }
        bi->next[prev]= i;
    }
}

//FIXME replace this by dsputil
#define SC(x, y) ((s[x] - s[y]) ^ ((s[x] - s[y]) >> 7))
static always_inline int dv_guess_dct_mode(DCTELEM *blk) {
    DCTELEM *s;
    int score88 = 0;
    int score248 = 0;
    int i;
    
    /* Compute 8-8 score (small values give a better chance for 8-8 DCT) */
    s = blk;
    for(i=0; i<7; i++) {
        score88 += SC(0,  8) + SC(1, 9) + SC(2, 10) + SC(3, 11) + 
	           SC(4, 12) + SC(5,13) + SC(6, 14) + SC(7, 15);
        s += 8;
    }
    /* Compute 2-4-8 score (small values give a better chance for 2-4-8 DCT) */
    s = blk;
    for(i=0; i<6; i++) {
        score248 += SC(0, 16) + SC(1,17) + SC(2, 18) + SC(3, 19) +
	            SC(4, 20) + SC(5,21) + SC(6, 22) + SC(7, 23);
        s += 8;
    }

    return (score88 - score248 > -10);
}

static inline void dv_guess_qnos(EncBlockInfo* blks, int* qnos)
{
    int size[5];
    int i, j, k, a, prev;
    EncBlockInfo* b;

    do {
       b = blks;
       for (i=0; i<5; i++) {
          if (!qnos[i])
	      continue;
	  
	  qnos[i]--;
	  size[i] = 0;
          for (j=0; j<6; j++, b++) {
	     for (a=0; a<4; a++) {
	        if (b->area_q[a] != dv_quant_shifts[qnos[i] + dv_quant_offset[b->cno]][a]) {
		    b->bit_size[a] = 1; // 4 areas 4 bits for EOB :)
		    b->area_q[a]++;
                    prev= b->prev[a];
                    for (k= b->next[prev] ; k<mb_area_start[a+1]; k= b->next[k]) {
		       b->mb[k] >>= 1;
		       if (b->mb[k]) {
                           b->bit_size[a] += dv_rl2vlc_size(k - prev - 1, b->mb[k]);
	                   prev= k;
                       } else {
                           b->next[prev] = b->next[k];
                       }
		    }
                    b->prev[a+1]= prev;
		}
		size[i] += b->bit_size[a];
	     }
	  }
       }
    } while ((vs_total_ac_bits < size[0] + size[1] + size[2] + size[3] + size[4]) && 
             (qnos[0]|qnos[1]|qnos[2]|qnos[3]|qnos[4]));
}

/*
 * This is a very rough initial implementaion. The performance is
 * horrible and the weighting is missing. But it's missing from the 
 * decoding step also -- so at least we're on the same page with decoder ;-)
 */
static inline void dv_encode_video_segment(DVVideoContext *s, 
                                           uint8_t *dif, 
                                           const uint16_t *mb_pos_ptr)
{
    int mb_index, i, j, v;
    int mb_x, mb_y, c_offset, linesize; 
    uint8_t*  y_ptr;
    uint8_t*  data;
    uint8_t*  ptr;
    int       do_edge_wrap;
    DCTELEM   block[64] __align8;
    EncBlockInfo  enc_blks[5*6];
    PutBitContext pbs[5*6];
    PutBitContext* pb; 
    EncBlockInfo* enc_blk;
    int       vs_bit_size = 0;
    int       qnos[5];
    
    assert((((int)block) & 7) == 0);
   
    enc_blk = &enc_blks[0];
    pb = &pbs[0];
    for(mb_index = 0; mb_index < 5; mb_index++) {
        v = *mb_pos_ptr++;
        mb_x = v & 0xff;
        mb_y = v >> 8;
        y_ptr = s->picture.data[0] + (mb_y * s->picture.linesize[0] * 8) + (mb_x * 8);
	c_offset = (s->sys->pix_fmt == PIX_FMT_YUV411P) ?
	           ((mb_y * s->picture.linesize[1] * 8) + ((mb_x >> 2) * 8)) :
		   (((mb_y >> 1) * s->picture.linesize[1] * 8) + ((mb_x >> 1) * 8));
	do_edge_wrap = 0;
	qnos[mb_index] = 15; /* No quantization */
        ptr = dif + mb_index*80 + 4;
        for(j = 0;j < 6; j++) {
            if (j < 4) {  /* Four Y blocks */
		/* NOTE: at end of line, the macroblock is handled as 420 */
		if (s->sys->pix_fmt == PIX_FMT_YUV411P && mb_x < (704 / 8)) {
                    data = y_ptr + (j * 8);
                } else {
                    data = y_ptr + ((j & 1) * 8) + ((j >> 1) * 8 * s->picture.linesize[0]);
                }
		linesize = s->picture.linesize[0];
            } else {      /* Cr and Cb blocks */
	        /* don't ask Fabrice why they inverted Cb and Cr ! */
	        data = s->picture.data[6 - j] + c_offset;
		linesize = s->picture.linesize[6 - j];
		if (s->sys->pix_fmt == PIX_FMT_YUV411P && mb_x >= (704 / 8))
		    do_edge_wrap = 1;
	    }	
            
	    /* Everything is set up -- now just copy data -> DCT block */
	    if (do_edge_wrap) {  /* Edge wrap copy: 4x16 -> 8x8 */
		uint8_t* d;
		DCTELEM *b = block;
	        for (i=0;i<8;i++) {
		   d = data + 8 * linesize;
		   b[0] = data[0]; b[1] = data[1]; b[2] = data[2]; b[3] = data[3];
                   b[4] =    d[0]; b[5] =    d[1]; b[6] =    d[2]; b[7] =    d[3];
		   data += linesize;
		   b += 8;
		}
	    } else {             /* Simple copy: 8x8 -> 8x8 */
	        s->get_pixels(block, data, linesize);
	    }
	  
            if(s->avctx->flags & CODEC_FLAG_INTERLACED_DCT)
                enc_blk->dct_mode = dv_guess_dct_mode(block);
            else
                enc_blk->dct_mode = 0;
	    enc_blk->area_q[0] = enc_blk->area_q[1] = enc_blk->area_q[2] = enc_blk->area_q[3] = 0;
	    enc_blk->partial_bit_count = 0;
	    enc_blk->partial_bit_buffer = 0;
	    enc_blk->cur_ac = 0;
	    
	    s->fdct[enc_blk->dct_mode](block);
	    
	    dv_set_class_number(block, enc_blk, 
	                        enc_blk->dct_mode ? ff_zigzag248_direct : ff_zigzag_direct, j/4);
           
            init_put_bits(pb, ptr, block_sizes[j]/8);
	    put_bits(pb, 9, (uint16_t)(((enc_blk->mb[0] >> 3) - 1024 + 2) >> 2));
	    put_bits(pb, 1, enc_blk->dct_mode);
	    put_bits(pb, 2, enc_blk->cno);
	    
	    vs_bit_size += enc_blk->bit_size[0] + enc_blk->bit_size[1] +
	                   enc_blk->bit_size[2] + enc_blk->bit_size[3];
	    ++enc_blk;
	    ++pb;
	    ptr += block_sizes[j]/8;
        }
    }

    if (vs_total_ac_bits < vs_bit_size)
        dv_guess_qnos(&enc_blks[0], &qnos[0]);

    for (i=0; i<5; i++) {
       dif[i*80 + 3] = qnos[i];
    }

    /* First pass over individual cells only */
    for (j=0; j<5*6; j++)
       dv_encode_ac(&enc_blks[j], &pbs[j], &pbs[j+1]);

    /* Second pass over each MB space */
    for (j=0; j<5*6; j+=6) {
        pb= &pbs[j];
        for (i=0; i<6; i++) {
            if (enc_blks[i+j].partial_bit_count)
                pb=dv_encode_ac(&enc_blks[i+j], pb, &pbs[j+6]);
        }
    }

    /* Third and final pass over the whole vides segment space */
    pb= &pbs[0];
    for (j=0; j<5*6; j++) {
       if (enc_blks[j].partial_bit_count)
           pb=dv_encode_ac(&enc_blks[j], pb, &pbs[6*5]);
    }

    for (j=0; j<5*6; j++)
       flush_put_bits(&pbs[j]);
}

static int dv_decode_mt(AVCodecContext *avctx, void* sl)
{
    DVVideoContext *s = avctx->priv_data;
    int slice = (size_t)sl;
    dv_decode_video_segment(s, &s->buf[((slice/27)*6+(slice/3)+slice*5+7)*80],
	                    &s->sys->video_place[slice*5]);
    return 0;
}

static int dv_encode_mt(AVCodecContext *avctx, void* sl)
{
    DVVideoContext *s = avctx->priv_data;
    int slice = (size_t)sl;
    dv_encode_video_segment(s, &s->buf[((slice/27)*6+(slice/3)+slice*5+7)*80],
	                    &s->sys->video_place[slice*5]);
    return 0;
}

/* NOTE: exactly one frame must be given (120000 bytes for NTSC,
   144000 bytes for PAL) */
static int dvvideo_decode_frame(AVCodecContext *avctx, 
                                 void *data, int *data_size,
                                 uint8_t *buf, int buf_size)
{
    DVVideoContext *s = avctx->priv_data;
  
    s->sys = dv_frame_profile(buf);
    if (!s->sys || buf_size < s->sys->frame_size)
        return -1; /* NOTE: we only accept several full frames */

    if(s->picture.data[0])
        avctx->release_buffer(avctx, &s->picture);
    
    s->picture.reference = 0;
    s->picture.key_frame = 1;
    s->picture.pict_type = FF_I_TYPE;
    avctx->pix_fmt = s->sys->pix_fmt;
    avcodec_set_dimensions(avctx, s->sys->width, s->sys->height);
    if(avctx->get_buffer(avctx, &s->picture) < 0) {
        av_log(avctx, AV_LOG_ERROR, "get_buffer() failed\n");
        return -1;
    }
    s->picture.interlaced_frame = 1;
    s->picture.top_field_first = 0;

    s->buf = buf;
    avctx->execute(avctx, dv_decode_mt, (void**)&dv_anchor[0], NULL, 
	           s->sys->difseg_size * 27);
    
    emms_c();

    /* return image */
    *data_size = sizeof(AVFrame);
    *(AVFrame*)data= s->picture;
    
    return s->sys->frame_size;
}

static int dvvideo_encode_frame(AVCodecContext *c, uint8_t *buf, int buf_size, 
                                void *data)
{
    DVVideoContext *s = c->priv_data;

    s->sys = dv_codec_profile(c);
    if (!s->sys)
	return -1;
    if(buf_size < s->sys->frame_size)
        return -1;

    c->pix_fmt = s->sys->pix_fmt;
    s->picture = *((AVFrame *)data);
    s->picture.key_frame = 1;
    s->picture.pict_type = FF_I_TYPE;

    s->buf = buf;
    c->execute(c, dv_encode_mt, (void**)&dv_anchor[0], NULL, 
	       s->sys->difseg_size * 27);

    emms_c();
    return s->sys->frame_size;
}

#ifdef CONFIG_DVVIDEO_ENCODER
AVCodec dvvideo_encoder = {
    "dvvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_DVVIDEO,
    sizeof(DVVideoContext),
    dvvideo_init,
    dvvideo_encode_frame,
    NULL,
    NULL,
    CODEC_CAP_DR1,
    NULL
};
#endif // CONFIG_DVVIDEO_ENCODER

AVCodec dvvideo_decoder = {
    "dvvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_DVVIDEO,
    sizeof(DVVideoContext),
    dvvideo_init,
    NULL,
    NULL,
    dvvideo_decode_frame,
    CODEC_CAP_DR1,
    NULL
};
