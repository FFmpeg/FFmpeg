/*
 * Copyright (c) 2003 The FFmpeg Project.
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
 *
 * How to use this decoder:
 * SVQ3 data is transported within Apple Quicktime files. Quicktime files
 * have stsd atoms to describe media trak properties. Sometimes the stsd
 * atom contains information that the decoder must know in order to function
 * properly. Such is the case with SVQ3. In order to get the best use out
 * of this decoder, the calling app must make the video stsd atom available
 * via the AVCodecContext's extradata[_size] field:
 *
 * AVCodecContext.extradata = pointer to stsd, first characters are expected
 * to be 's', 't', 's', and 'd', NOT the atom length
 * AVCodecContext.extradata_size = size of stsd atom memory buffer (which 
 * will be the same as the stsd atom size field from the QT file, minus 4
 * bytes since the length is missing.
 *
 */
 
/**
 * @file svq3.c
 * svq3 decoder.
 */

static const uint8_t svq3_scan[16]={
 0+0*4, 1+0*4, 2+0*4, 2+1*4,
 2+2*4, 3+0*4, 3+1*4, 3+2*4,
 0+1*4, 0+2*4, 1+1*4, 1+2*4,
 0+3*4, 1+3*4, 2+3*4, 3+3*4,
};

static const uint8_t svq3_pred_0[25][2] = {
  { 0, 0 },
  { 1, 0 }, { 0, 1 },
  { 0, 2 }, { 1, 1 }, { 2, 0 },
  { 3, 0 }, { 2, 1 }, { 1, 2 }, { 0, 3 },
  { 0, 4 }, { 1, 3 }, { 2, 2 }, { 3, 1 }, { 4, 0 },
  { 4, 1 }, { 3, 2 }, { 2, 3 }, { 1, 4 },
  { 2, 4 }, { 3, 3 }, { 4, 2 },
  { 4, 3 }, { 3, 4 },
  { 4, 4 }
};

static const int8_t svq3_pred_1[6][6][5] = {
  { { 2,-1,-1,-1,-1 }, { 2, 1,-1,-1,-1 }, { 1, 2,-1,-1,-1 },
    { 2, 1,-1,-1,-1 }, { 1, 2,-1,-1,-1 }, { 1, 2,-1,-1,-1 } },
  { { 0, 2,-1,-1,-1 }, { 0, 2, 1, 4, 3 }, { 0, 1, 2, 4, 3 },
    { 0, 2, 1, 4, 3 }, { 2, 0, 1, 3, 4 }, { 0, 4, 2, 1, 3 } },
  { { 2, 0,-1,-1,-1 }, { 2, 1, 0, 4, 3 }, { 1, 2, 4, 0, 3 },
    { 2, 1, 0, 4, 3 }, { 2, 1, 4, 3, 0 }, { 1, 2, 4, 0, 3 } },
  { { 2, 0,-1,-1,-1 }, { 2, 0, 1, 4, 3 }, { 1, 2, 0, 4, 3 },
    { 2, 1, 0, 4, 3 }, { 2, 1, 3, 4, 0 }, { 2, 4, 1, 0, 3 } },
  { { 0, 2,-1,-1,-1 }, { 0, 2, 1, 3, 4 }, { 1, 2, 3, 0, 4 },
    { 2, 0, 1, 3, 4 }, { 2, 1, 3, 0, 4 }, { 2, 0, 4, 3, 1 } },
  { { 0, 2,-1,-1,-1 }, { 0, 2, 4, 1, 3 }, { 1, 4, 2, 0, 3 },
    { 4, 2, 0, 1, 3 }, { 2, 0, 1, 4, 3 }, { 4, 2, 1, 0, 3 } },
};

static const struct { uint8_t run; uint8_t level; } svq3_dct_tables[2][16] = {
  { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 2, 1 }, { 0, 2 }, { 3, 1 }, { 4, 1 }, { 5, 1 },
    { 0, 3 }, { 1, 2 }, { 2, 2 }, { 6, 1 }, { 7, 1 }, { 8, 1 }, { 9, 1 }, { 0, 4 } },
  { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 0, 2 }, { 2, 1 }, { 0, 3 }, { 0, 4 }, { 0, 5 },
    { 3, 1 }, { 4, 1 }, { 1, 2 }, { 1, 3 }, { 0, 6 }, { 0, 7 }, { 0, 8 }, { 0, 9 } }
};

static const uint32_t svq3_dequant_coeff[32] = {
   3881,  4351,  4890,  5481,  6154,  6914,  7761,  8718,
   9781, 10987, 12339, 13828, 15523, 17435, 19561, 21873,
  24552, 27656, 30847, 34870, 38807, 43747, 49103, 54683,
  61694, 68745, 77615, 89113,100253,109366,126635,141533
};


static void svq3_luma_dc_dequant_idct_c(DCTELEM *block, int qp){
    const int qmul= svq3_dequant_coeff[qp];
#define stride 16
    int i;
    int temp[16];
    static const int x_offset[4]={0, 1*stride, 4* stride,  5*stride};
    static const int y_offset[4]={0, 2*stride, 8* stride, 10*stride};

    for(i=0; i<4; i++){
        const int offset= y_offset[i];
        const int z0= 13*(block[offset+stride*0] +    block[offset+stride*4]);
        const int z1= 13*(block[offset+stride*0] -    block[offset+stride*4]);
        const int z2=  7* block[offset+stride*1] - 17*block[offset+stride*5];
        const int z3= 17* block[offset+stride*1] +  7*block[offset+stride*5];

        temp[4*i+0]= z0+z3;
        temp[4*i+1]= z1+z2;
        temp[4*i+2]= z1-z2;
        temp[4*i+3]= z0-z3;
    }

    for(i=0; i<4; i++){
        const int offset= x_offset[i];
        const int z0= 13*(temp[4*0+i] +    temp[4*2+i]);
        const int z1= 13*(temp[4*0+i] -    temp[4*2+i]);
        const int z2=  7* temp[4*1+i] - 17*temp[4*3+i];
        const int z3= 17* temp[4*1+i] +  7*temp[4*3+i];

        block[stride*0 +offset]= ((z0 + z3)*qmul + 0x80000)>>20;
        block[stride*2 +offset]= ((z1 + z2)*qmul + 0x80000)>>20;
        block[stride*8 +offset]= ((z1 - z2)*qmul + 0x80000)>>20;
        block[stride*10+offset]= ((z0 - z3)*qmul + 0x80000)>>20;
    }
}
#undef stride

static void svq3_add_idct_c (uint8_t *dst, DCTELEM *block, int stride, int qp, int dc){
    const int qmul= svq3_dequant_coeff[qp];
    int i;
    uint8_t *cm = cropTbl + MAX_NEG_CROP;

    if (dc) {
        dc = 13*13*((dc == 1) ? 1538*block[0] : ((qmul*(block[0] >> 3)) / 2));
        block[0] = 0;
    }

    for (i=0; i < 4; i++) {
        const int z0= 13*(block[0 + 4*i] +    block[2 + 4*i]);
        const int z1= 13*(block[0 + 4*i] -    block[2 + 4*i]);
        const int z2=  7* block[1 + 4*i] - 17*block[3 + 4*i];
        const int z3= 17* block[1 + 4*i] +  7*block[3 + 4*i];

        block[0 + 4*i]= z0 + z3;
        block[1 + 4*i]= z1 + z2;
        block[2 + 4*i]= z1 - z2;
        block[3 + 4*i]= z0 - z3;
    }

    for (i=0; i < 4; i++) {
        const int z0= 13*(block[i + 4*0] +    block[i + 4*2]);
        const int z1= 13*(block[i + 4*0] -    block[i + 4*2]);
        const int z2=  7* block[i + 4*1] - 17*block[i + 4*3];
        const int z3= 17* block[i + 4*1] +  7*block[i + 4*3];
        const int rr= (dc + 0x80000);

        dst[i + stride*0]= cm[ dst[i + stride*0] + (((z0 + z3)*qmul + rr) >> 20) ];
        dst[i + stride*1]= cm[ dst[i + stride*1] + (((z1 + z2)*qmul + rr) >> 20) ];
        dst[i + stride*2]= cm[ dst[i + stride*2] + (((z1 - z2)*qmul + rr) >> 20) ];
        dst[i + stride*3]= cm[ dst[i + stride*3] + (((z0 - z3)*qmul + rr) >> 20) ];
    }
}

static void pred4x4_down_left_svq3_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_TOP_EDGE    
    LOAD_LEFT_EDGE    
    const __attribute__((unused)) int unu0= t0;
    const __attribute__((unused)) int unu1= l0;

    src[0+0*stride]=(l1 + t1)>>1;
    src[1+0*stride]=
    src[0+1*stride]=(l2 + t2)>>1;
    src[2+0*stride]=
    src[1+1*stride]=
    src[0+2*stride]=
    src[3+0*stride]=
    src[2+1*stride]=
    src[1+2*stride]=
    src[0+3*stride]=
    src[3+1*stride]=
    src[2+2*stride]=
    src[1+3*stride]=
    src[3+2*stride]=
    src[2+3*stride]=
    src[3+3*stride]=(l3 + t3)>>1;
};

static void pred16x16_plane_svq3_c(uint8_t *src, int stride){
    pred16x16_plane_compat_c(src, stride, 1);
}

static inline int svq3_decode_block (GetBitContext *gb, DCTELEM *block,
				     int index, const int type) {

  static const uint8_t *const scan_patterns[4] =
  { luma_dc_zigzag_scan, zigzag_scan, svq3_scan, chroma_dc_scan };

  int run, level, sign, vlc, limit;
  const int intra = (3 * type) >> 2;
  const uint8_t *const scan = scan_patterns[type];

  for (limit=(16 >> intra); index < 16; index=limit, limit+=8) {
    for (; (vlc = svq3_get_ue_golomb (gb)) != 0; index++) {

      if (vlc == INVALID_VLC)
	return -1;

      sign = (vlc & 0x1) - 1;
      vlc  = (vlc + 1) >> 1;

      if (type == 3) {
	if (vlc < 3) {
	  run   = 0;
	  level = vlc;
	} else if (vlc < 4) {
	  run   = 1;
	  level = 1;
	} else {
	  run   = (vlc & 0x3);
	  level = ((vlc + 9) >> 2) - run;
	}
      } else {
	if (vlc < 16) {
	  run   = svq3_dct_tables[intra][vlc].run;
	  level = svq3_dct_tables[intra][vlc].level;
	} else if (intra) {
	  run   = (vlc & 0x7);
	  level = (vlc >> 3) + ((run == 0) ? 8 : ((run < 2) ? 2 : ((run < 5) ? 0 : -1)));
	} else {
	  run   = (vlc & 0xF);
	  level = (vlc >> 4) + ((run == 0) ? 4 : ((run < 3) ? 2 : ((run < 10) ? 1 : 0)));
	}
      }

      if ((index += run) >= limit)
	return -1;

      block[scan[index]] = (level ^ sign) - sign;
    }

    if (type != 2) {
      break;
    }
  }

  return 0;
}

static void sixpel_mc_put (MpegEncContext *s,
			   uint8_t *src, uint8_t *dst, int stride,
			   int dxy, int width, int height) {
  int i, j;

  switch (dxy) {
  case 6*0+0:
    for (i=0; i < height; i++) {
      memcpy (dst, src, width);
      src += stride;
      dst += stride;
    }
    break;
  case 6*0+2:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (683*(2*src[j] + src[j+1] + 1)) >> 11;
      }
      src += stride;
      dst += stride;
    }
    break;
  case 6*0+3:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (src[j] + src[j+1] + 1) >> 1;
      }
      src += stride;
      dst += stride;
    }
    break;
  case 6*0+4:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (683*(src[j] + 2*src[j+1] + 1)) >> 11;
      }
      src += stride;
      dst += stride;
    }
    break;
  case 6*2+0:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (683*(2*src[j] + src[j+stride] + 1)) >> 11;
      }
      src += stride;
      dst += stride;
    }
    break;
  case 6*2+2:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (2731*(4*src[j] + 3*src[j+1] + 3*src[j+stride] + 2*src[j+stride+1] + 6)) >> 15;
      }
      src += stride;
      dst += stride;
    }
    break;
  case 6*2+4:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (2731*(3*src[j] + 4*src[j+1] + 2*src[j+stride] + 3*src[j+stride+1] + 6)) >> 15;
      }
      src += stride;
      dst += stride;
    }
    break;
  case 6*3+0:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (src[j] + src[j+stride]+1) >> 1;
      }
      src += stride;
      dst += stride;
    }
    break;
  case 6*3+3:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (src[j] + src[j+1] + src[j+stride] + src[j+stride+1] + 2) >> 2;
      }
      src += stride;
      dst += stride;
    }
    break;
  case 6*4+0:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (683*(src[j] + 2*src[j+stride] + 1)) >> 11;
      }
      src += stride;
      dst += stride;
    }
    break;
  case 6*4+2:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (2731*(3*src[j] + 2*src[j+1] + 4*src[j+stride] + 3*src[j+stride+1] + 6)) >> 15;
      }
      src += stride;
      dst += stride;
    }
    break;
  case 6*4+4:
    for (i=0; i < height; i++) {
      for (j=0; j < width; j++) {
	dst[j] = (2731*(2*src[j] + 3*src[j+1] + 3*src[j+stride] + 4*src[j+stride+1] + 6)) >> 15;
      }
      src += stride;
      dst += stride;
    }
    break;
  }
}

static inline void svq3_mc_dir_part (MpegEncContext *s, int x, int y,
				     int width, int height, int mx, int my) {
  uint8_t *src, *dest;
  int i, emu = 0;
  const int sx = ((unsigned) (mx + 0x7FFFFFFE)) % 6;
  const int sy = ((unsigned) (my + 0x7FFFFFFE)) % 6;
  const int dxy= 6*sy + sx;

  /* decode and clip motion vector to frame border (+16) */
  mx = x + (mx - sx) / 6;
  my = y + (my - sy) / 6;

  if (mx < 0 || mx >= (s->width  - width  - 1) ||
      my < 0 || my >= (s->height - height - 1)) {

    if ((s->flags & CODEC_FLAG_EMU_EDGE)) {
      emu = 1;
    }

    mx = clip (mx, -16, (s->width  - width  + 15));
    my = clip (my, -16, (s->height - height + 15));
  }

  /* form component predictions */
  dest = s->current_picture.data[0] + x + y*s->linesize;
  src  = s->last_picture.data[0] + mx + my*s->linesize;

  if (emu) {
    ff_emulated_edge_mc (s, src, s->linesize, (width + 1), (height + 1),
			 mx, my, s->width, s->height);
    src = s->edge_emu_buffer;
  }
  sixpel_mc_put (s, src, dest, s->linesize, dxy, width, height);

  if (!(s->flags & CODEC_FLAG_GRAY)) {
    mx	   = (mx + (mx < (int) x)) >> 1;
    my	   = (my + (my < (int) y)) >> 1;
    width  = (width  >> 1);
    height = (height >> 1);

    for (i=1; i < 3; i++) {
      dest = s->current_picture.data[i] + (x >> 1) + (y >> 1)*s->uvlinesize;
      src  = s->last_picture.data[i] + mx + my*s->uvlinesize;

      if (emu) {
	ff_emulated_edge_mc (s, src, s->uvlinesize, (width + 1), (height + 1),
			     mx, my, (s->width >> 1), (s->height >> 1));
	src = s->edge_emu_buffer;
      }
      sixpel_mc_put (s, src, dest, s->uvlinesize, dxy, width, height);
    }
  }
}

static int svq3_decode_mb (H264Context *h, unsigned int mb_type) {
  int cbp, dir, mode, mx, my, dx, dy, x, y, part_width, part_height;
  int i, j, k, l, m;
  uint32_t vlc;
  int8_t *top, *left;
  MpegEncContext *const s = (MpegEncContext *) h;
  const int mb_xy = s->mb_x + s->mb_y*s->mb_stride;
  const int b_xy = 4*s->mb_x + 4*s->mb_y*h->b_stride;

  h->top_samples_available	= (s->mb_y == 0) ? 0x33FF : 0xFFFF;
  h->left_samples_available	= (s->mb_x == 0) ? 0x5F5F : 0xFFFF;
  h->topright_samples_available	= 0xFFFF;

  if (mb_type == 0) {		/* SKIP */
    svq3_mc_dir_part (s, 16*s->mb_x, 16*s->mb_y, 16, 16, 0, 0);

    cbp = 0;
    mb_type = MB_TYPE_SKIP;
  } else if (mb_type < 8) {	/* INTER */
    if (h->thirdpel_flag && h->halfpel_flag == !get_bits (&s->gb, 1)) {
      mode = 3;	/* thirdpel */
    } else if (h->halfpel_flag && h->thirdpel_flag == !get_bits (&s->gb, 1)) {
      mode = 2;	/* halfpel */
    } else {
      mode = 1;	/* fullpel */
    }

    /* fill caches */
    memset (h->ref_cache[0], PART_NOT_AVAILABLE, 8*5*sizeof(int8_t));

    if (s->mb_x > 0) {
      for (i=0; i < 4; i++) {
	*(uint32_t *) h->mv_cache[0][scan8[0] - 1 + i*8] = *(uint32_t *) s->current_picture.motion_val[0][b_xy - 1 + i*h->b_stride];
	h->ref_cache[0][scan8[0] - 1 + i*8] = 1;
      }
    } else {
      for (i=0; i < 4; i++) {
	*(uint32_t *) h->mv_cache[0][scan8[0] - 1 + i*8] = 0;
	h->ref_cache[0][scan8[0] - 1 + i*8] = 1;
      }
    }
    if (s->mb_y > 0) {
      memcpy (h->mv_cache[0][scan8[0] - 1*8], s->current_picture.motion_val[0][b_xy - h->b_stride], 4*2*sizeof(int16_t));
      memset (&h->ref_cache[0][scan8[0] - 1*8], 1, 4);

      if (s->mb_x < (s->mb_width - 1)) {
	*(uint32_t *) h->mv_cache[0][scan8[0] + 4 - 1*8] = *(uint32_t *) s->current_picture.motion_val[0][b_xy - h->b_stride + 4];
	h->ref_cache[0][scan8[0] + 4 - 1*8] = 1;
      }
      if (s->mb_x > 0) {
	*(uint32_t *) h->mv_cache[0][scan8[0] - 1 - 1*8] = *(uint32_t *) s->current_picture.motion_val[0][b_xy - h->b_stride - 1];
	h->ref_cache[0][scan8[0] - 1 - 1*8] = 1;
      }
    }

    /* decode motion vector(s) and form prediction(s) */
    part_width  = ((mb_type & 5) == 5) ? 4 : 8 << (mb_type & 1);
    part_height = 16 >> ((unsigned) mb_type / 3);

    for (i=0; i < 16; i+=part_height) {
      for (j=0; j < 16; j+=part_width) {
	x = 16*s->mb_x + j;
	y = 16*s->mb_y + i;
	k = ((j>>2)&1) + ((i>>1)&2) + ((j>>1)&4) + (i&8);

	pred_motion (h, k, (part_width >> 2), 0, 1, &mx, &my);

	/* clip motion vector prediction to frame border */
	mx = clip (mx, -6*x, 6*(s->width  - part_width  - x));
	my = clip (my, -6*y, 6*(s->height - part_height - y));

	/* get motion vector differential */
	dy = svq3_get_se_golomb (&s->gb);
	dx = svq3_get_se_golomb (&s->gb);

	if (dx == INVALID_VLC || dy == INVALID_VLC) {
	  return -1;
	}

	/* compute motion vector */
	if (mode == 3) {
	  mx = ((mx + 1) & ~0x1) + 2*dx;
	  my = ((my + 1) & ~0x1) + 2*dy;
	} else if (mode == 2) {
	  mx = (mx + 1) - ((unsigned) (0x7FFFFFFF + mx) % 3) + 3*dx;
	  my = (my + 1) - ((unsigned) (0x7FFFFFFF + my) % 3) + 3*dy;
	} else if (mode == 1) {
	  mx = (mx + 3) - ((unsigned) (0x7FFFFFFB + mx) % 6) + 6*dx;
	  my = (my + 3) - ((unsigned) (0x7FFFFFFB + my) % 6) + 6*dy;
	}

	/* update mv_cache */
	for (l=0; l < part_height; l+=4) {
	  for (m=0; m < part_width; m+=4) {
	    k = scan8[0] + ((m + j) >> 2) + ((l + i) << 1);
	    h->mv_cache [0][k][0] = mx;
	    h->mv_cache [0][k][1] = my;
	    h->ref_cache[0][k] = 1;
	  }
	}

	svq3_mc_dir_part (s, x, y, part_width, part_height, mx, my);
      }
    }

    for (i=0; i < 4; i++) {
      memcpy (s->current_picture.motion_val[0][b_xy + i*h->b_stride], h->mv_cache[0][scan8[0] + 8*i], 4*2*sizeof(int16_t));
    }

    if ((vlc = svq3_get_ue_golomb (&s->gb)) >= 48)
      return -1;

    cbp = golomb_to_inter_cbp[vlc];
    mb_type = MB_TYPE_16x16;
  } else if (mb_type == 8) {	/* INTRA4x4 */
    memset (h->intra4x4_pred_mode_cache, -1, 8*5*sizeof(int8_t));

    if (s->mb_x > 0) {
      for (i=0; i < 4; i++) {
	h->intra4x4_pred_mode_cache[scan8[0] - 1 + i*8] = h->intra4x4_pred_mode[mb_xy - 1][i];
      }
    }
    if (s->mb_y > 0) {
      h->intra4x4_pred_mode_cache[4+8*0] = h->intra4x4_pred_mode[mb_xy - s->mb_stride][4];
      h->intra4x4_pred_mode_cache[5+8*0] = h->intra4x4_pred_mode[mb_xy - s->mb_stride][5];
      h->intra4x4_pred_mode_cache[6+8*0] = h->intra4x4_pred_mode[mb_xy - s->mb_stride][6];
      h->intra4x4_pred_mode_cache[7+8*0] = h->intra4x4_pred_mode[mb_xy - s->mb_stride][3];
    }

    /* decode prediction codes for luma blocks */
    for (i=0; i < 16; i+=2) {
      vlc = svq3_get_ue_golomb (&s->gb);

      if (vlc >= 25)
	return -1;

      left    = &h->intra4x4_pred_mode_cache[scan8[i] - 1];
      top     = &h->intra4x4_pred_mode_cache[scan8[i] - 8];

      left[1] = svq3_pred_1[top[0] + 1][left[0] + 1][svq3_pred_0[vlc][0]];
      left[2] = svq3_pred_1[top[1] + 1][left[1] + 1][svq3_pred_0[vlc][1]];

      if (left[1] == -1 || left[2] == -1)
	return -1;
    }

    write_back_intra_pred_mode (h);
    check_intra4x4_pred_mode (h);

    if ((vlc = svq3_get_ue_golomb (&s->gb)) >= 48)
      return -1;

    cbp = golomb_to_intra4x4_cbp[vlc];
    mb_type = MB_TYPE_INTRA4x4;
  } else {			/* INTRA16x16 */
    dir = i_mb_type_info[mb_type - 8].pred_mode;
    dir = (dir >> 1) ^ 3*(dir & 1) ^ 1;

    if ((h->intra16x16_pred_mode = check_intra_pred_mode (h, dir)) == -1)
      return -1;

    cbp = i_mb_type_info[mb_type - 8].cbp;
    mb_type = MB_TYPE_INTRA16x16;
  }

  if (!IS_INTER(mb_type) && s->pict_type != I_TYPE) {
    for (i=0; i < 4; i++) {
      memset (s->current_picture.motion_val[0][b_xy + i*h->b_stride], 0, 4*2*sizeof(int16_t));
    }
  }
  if (!IS_INTRA4x4(mb_type)) {
    memset (h->intra4x4_pred_mode[mb_xy], DC_PRED, 8);
  }
  if (!IS_SKIP(mb_type)) {
    memset (h->mb, 0, 24*16*sizeof(DCTELEM));
    memset (h->non_zero_count_cache, 0, 8*6*sizeof(uint8_t));
  }

  if (IS_INTRA16x16(mb_type) || (s->pict_type != I_TYPE && s->adaptive_quant && cbp)) {
    s->qscale += svq3_get_se_golomb (&s->gb);

    if (s->qscale > 31)
      return -1;
  }
  if (IS_INTRA16x16(mb_type)) {
    if (svq3_decode_block (&s->gb, h->mb, 0, 0))
      return -1;
  }

  if (!IS_SKIP(mb_type) && cbp) {
    l = IS_INTRA16x16(mb_type) ? 1 : 0;
    m = ((s->qscale < 24 && IS_INTRA4x4(mb_type)) ? 2 : 1);

    for (i=0; i < 4; i++) {
      if ((cbp & (1 << i))) {
	for (j=0; j < 4; j++) {
	  k = l ? ((j&1) + 2*(i&1) + 2*(j&2) + 4*(i&2)) : (4*i + j);
	  h->non_zero_count_cache[ scan8[k] ] = 1;

	  if (svq3_decode_block (&s->gb, &h->mb[16*k], l, m))
	    return -1;
	}
      }
    }

    if ((cbp & 0x30)) {
      for (i=0; i < 2; ++i) {
	if (svq3_decode_block (&s->gb, &h->mb[16*(16 + 4*i)], 0, 3))
	  return -1;
      }

      if ((cbp & 0x20)) {
	for (i=0; i < 8; i++) {
	  h->non_zero_count_cache[ scan8[16+i] ] = 1;

	  if (svq3_decode_block (&s->gb, &h->mb[16*(16 + i)], 1, 1))
	    return -1;
	}
      }
    }
  }

  s->current_picture.mb_type[mb_xy] = mb_type;

  if (IS_INTRA(mb_type)) {
    h->chroma_pred_mode = check_intra_pred_mode (h, DC_PRED8x8);
  }

  return 0;
}

static int svq3_decode_frame (AVCodecContext *avctx,
			      void *data, int *data_size,
			      uint8_t *buf, int buf_size) {
  MpegEncContext *const s = avctx->priv_data;
  H264Context *const h = avctx->priv_data;
  int i;

  s->flags = avctx->flags;

  if (!s->context_initialized) {
    s->width = (avctx->width + 15) & ~15;
    s->height = (avctx->height + 15) & ~15;
    h->b_stride = (s->width >> 2);
    h->pred4x4[DIAG_DOWN_LEFT_PRED] = pred4x4_down_left_svq3_c;
    h->pred16x16[PLANE_PRED8x8] = pred16x16_plane_svq3_c;
    h->halfpel_flag = 1;
    h->thirdpel_flag = 1;
    h->chroma_qp = 4;

    if (MPV_common_init (s) < 0)
      return -1;

    alloc_tables (h);
  }
  if (avctx->extradata && avctx->extradata_size >= 115
      && !memcmp (avctx->extradata, "stsd", 4)) {

    uint8_t *stsd = (uint8_t *) avctx->extradata + 114;

    if ((*stsd >> 5) != 7 || avctx->extradata_size >= 118) {

      if ((*stsd >> 5) == 7) {
	stsd += 3;	/* skip width, height (12 bits each) */
      }

      h->halfpel_flag = (*stsd >> 4) & 1;
      h->thirdpel_flag = (*stsd >> 3) & 1;
    }
  }

  if ((buf[0] & 0x9F) != 1) {
    /* TODO: what? */
    fprintf (stderr, "unsupported header (%02X)\n", buf[0]);
    return -1;
  } else {
    int length = (buf[0] >> 5) & 3;
    int offset = 0;

    for (i=0; i < length; i++) {
      offset = (offset << 8) | buf[i + 1];
    }

    if (buf_size < (offset + length + 1) || length == 0)
      return -1;

    memcpy (&buf[2], &buf[offset + 2], (length - 1));
  }

  init_get_bits (&s->gb, &buf[2], 8*(buf_size - 2));

  if ((i = svq3_get_ue_golomb (&s->gb)) == INVALID_VLC || i >= 3)
    return -1;

  s->pict_type = golomb_to_pict_type[i];

  /* unknown fields */
  get_bits (&s->gb, 1);
  get_bits (&s->gb, 8);

  s->qscale = get_bits (&s->gb, 5);
  s->adaptive_quant = get_bits (&s->gb, 1);

  /* unknown fields */
  get_bits (&s->gb, 1);
  get_bits (&s->gb, 1);
  get_bits (&s->gb, 2);

  while (get_bits (&s->gb, 1)) {
    get_bits (&s->gb, 8);
  }

  /* B-frames are not supported */
  if (s->pict_type == B_TYPE/* && avctx->hurry_up*/)
    return buf_size;

  frame_start (h);

  for (s->mb_y=0; s->mb_y < s->mb_height; s->mb_y++) {
    for (s->mb_x=0; s->mb_x < s->mb_width; s->mb_x++) {
      int mb_type = svq3_get_ue_golomb (&s->gb);

      if (s->pict_type == I_TYPE) {
	mb_type += 8;
      }
      if (mb_type > 32 || svq3_decode_mb (h, mb_type)) {
	fprintf (stderr, "error while decoding MB %d %d\n", s->mb_x, s->mb_y);
	return -1;
      }

      if (mb_type != 0) {
	hl_decode_mb (h);
      }
    }
  }

  *(AVFrame *) data = *(AVFrame *) &s->current_picture;
  *data_size = sizeof(AVFrame);

  MPV_frame_end(s);
  
  return buf_size;
}


AVCodec svq3_decoder = {
    "svq3",
    CODEC_TYPE_VIDEO,
    CODEC_ID_SVQ3,
    sizeof(H264Context),
    decode_init,
    NULL,
    decode_end,
    svq3_decode_frame,
    CODEC_CAP_DR1,
};
