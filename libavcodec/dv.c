/*
 * DV decoder
 * Copyright (c) 2002 Fabrice Bellard.
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
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"
#include "simple_idct.h"

#define NTSC_FRAME_SIZE 120000
#define PAL_FRAME_SIZE  144000

#define TEX_VLC_BITS 9

typedef struct DVVideoDecodeContext {
    AVCodecContext *avctx;
    GetBitContext gb;
    VLC *vlc;
    int sampling_411; /* 0 = 420, 1 = 411 */
    int width, height;
    UINT8 *current_picture[3]; /* picture structure */
    int linesize[3];
    DCTELEM block[5*6][64] __align8;
    UINT8 dv_zigzag[2][64];
    UINT8 idct_permutation[64];
    /* XXX: move it to static storage ? */
    UINT8 dv_shift[2][22][64];
    void (*idct_put[2])(UINT8 *dest, int line_size, DCTELEM *block);
} DVVideoDecodeContext;

#include "dvdata.h"

static VLC dv_vlc;
/* XXX: also include quantization */
static RL_VLC_ELEM *dv_rl_vlc[1];

static void dv_build_unquantize_tables(DVVideoDecodeContext *s)
{
    int i, q, j;

    /* NOTE: max left shift is 6 */
    for(q = 0; q < 22; q++) {
        /* 88 unquant */
        for(i = 1; i < 64; i++) {
            /* 88 table */
            j = s->idct_permutation[i];
            s->dv_shift[0][q][j] =
                dv_quant_shifts[q][dv_88_areas[i]] + 1;
        }
        
        /* 248 unquant */
        for(i = 1; i < 64; i++) {
            /* 248 table */
            s->dv_shift[1][q][i] =  
                    dv_quant_shifts[q][dv_248_areas[i]] + 1;
        }
    }
}

static int dvvideo_decode_init(AVCodecContext *avctx)
{
    DVVideoDecodeContext *s = avctx->priv_data;
    MpegEncContext s2;
    static int done;

    if (!done) {
        int i;

        done = 1;

        /* NOTE: as a trick, we use the fact the no codes are unused
           to accelerate the parsing of partial codes */
        init_vlc(&dv_vlc, TEX_VLC_BITS, NB_DV_VLC, 
                 dv_vlc_len, 1, 1, dv_vlc_bits, 2, 2);

        dv_rl_vlc[0] = av_malloc(dv_vlc.table_size * sizeof(RL_VLC_ELEM));
        for(i = 0; i < dv_vlc.table_size; i++){
            int code= dv_vlc.table[i][0];
            int len = dv_vlc.table[i][1];
            int level, run;
        
            if(len<0){ //more bits needed
                run= 0;
                level= code;
            } else if (code == (NB_DV_VLC - 1)) {
                /* EOB */
                run = 0;
                level = 256;
            } else {
                run=   dv_vlc_run[code] + 1;
                level= dv_vlc_level[code];
            }
            dv_rl_vlc[0][i].len = len;
            dv_rl_vlc[0][i].level = level;
            dv_rl_vlc[0][i].run = run;
        }
    }

    /* ugly way to get the idct & scantable */
    /* XXX: fix it */
    memset(&s2, 0, sizeof(MpegEncContext));
    s2.avctx = avctx;
    dsputil_init(&s2.dsp, avctx->dsp_mask);
    if (DCT_common_init(&s2) < 0)
       return -1;

    s->idct_put[0] = s2.idct_put;
    memcpy(s->idct_permutation, s2.idct_permutation, 64);
    memcpy(s->dv_zigzag[0], s2.intra_scantable.permutated, 64);

    /* XXX: use MMX also for idct248 */
    s->idct_put[1] = simple_idct248_put;
    memcpy(s->dv_zigzag[1], dv_248_zigzag, 64);

    /* XXX: do it only for constant case */
    dv_build_unquantize_tables(s);

    return 0;
}

//#define VLC_DEBUG

typedef struct BlockInfo {
    const UINT8 *shift_table;
    const UINT8 *scan_table;
    UINT8 pos; /* position in block */
    UINT8 eob_reached; /* true if EOB has been reached */
    UINT8 dct_mode;
    UINT8 partial_bit_count;
    UINT16 partial_bit_buffer;
    int shift_offset;
} BlockInfo;

/* block size in bits */
static const UINT16 block_sizes[6] = {
    112, 112, 112, 112, 80, 80
};

#ifndef ALT_BITSTREAM_READER
#error only works with ALT_BITSTREAM_READER
#endif

/* decode ac coefs */
static void dv_decode_ac(DVVideoDecodeContext *s, 
                         BlockInfo *mb, INT16 *block, int last_index)
{
    int last_re_index;
    int shift_offset = mb->shift_offset;
    const UINT8 *scan_table = mb->scan_table;
    const UINT8 *shift_table = mb->shift_table;
    int pos = mb->pos;
    int level, pos1, sign, run;
    int partial_bit_count;

    OPEN_READER(re, &s->gb);
    
#ifdef VLC_DEBUG
    printf("start\n");
#endif

    /* if we must parse a partial vlc, we do it here */
    partial_bit_count = mb->partial_bit_count;
    if (partial_bit_count > 0) {
        UINT8 buf[4];
        UINT32 v;
        int l, l1;
        GetBitContext gb1;

        /* build the dummy bit buffer */
        l = 16 - partial_bit_count;
        UPDATE_CACHE(re, &s->gb);
#ifdef VLC_DEBUG
        printf("show=%04x\n", SHOW_UBITS(re, &s->gb, 16));
#endif
        v = (mb->partial_bit_buffer << l) | SHOW_UBITS(re, &s->gb, l);
        buf[0] = v >> 8;
        buf[1] = v;
#ifdef VLC_DEBUG
        printf("v=%04x cnt=%d %04x\n", 
               v, partial_bit_count, (mb->partial_bit_buffer << l));
#endif
        /* try to read the codeword */
        init_get_bits(&gb1, buf, 4);
        {
            OPEN_READER(re1, &gb1);
            UPDATE_CACHE(re1, &gb1);
            GET_RL_VLC(level, run, re1, &gb1, dv_rl_vlc[0], 
                       TEX_VLC_BITS, 2);
            l = re1_index;
            CLOSE_READER(re1, &gb1);
        }
#ifdef VLC_DEBUG
        printf("****run=%d level=%d size=%d\n", run, level, l);
#endif
        /* compute codeword length */
        l1 = (level != 256 && level != 0);
        /* if too long, we cannot parse */
        l -= partial_bit_count;
        if ((re_index + l + l1) > last_index)
            return;
        /* skip read bits */
        last_re_index = 0; /* avoid warning */
        re_index += l;
        /* by definition, if we can read the vlc, all partial bits
           will be read (otherwise we could have read the vlc before) */
        mb->partial_bit_count = 0;
        UPDATE_CACHE(re, &s->gb);
        goto handle_vlc;
    }

    /* get the AC coefficients until last_index is reached */
    for(;;) {
        UPDATE_CACHE(re, &s->gb);
#ifdef VLC_DEBUG
        printf("%2d: bits=%04x index=%d\n", 
               pos, SHOW_UBITS(re, &s->gb, 16), re_index);
#endif
        last_re_index = re_index;
        GET_RL_VLC(level, run, re, &s->gb, dv_rl_vlc[0], 
                   TEX_VLC_BITS, 2);
    handle_vlc:
#ifdef VLC_DEBUG
        printf("run=%d level=%d\n", run, level);
#endif
        if (level == 256) {
            if (re_index > last_index) {
            cannot_read:
                /* put position before read code */
                re_index = last_re_index;
                mb->eob_reached = 0;
                break;
            }
            /* EOB */
            mb->eob_reached = 1;
            break;
        } else if (level != 0) {
            if ((re_index + 1) > last_index)
                goto cannot_read;
            sign = SHOW_SBITS(re, &s->gb, 1);
            level = (level ^ sign) - sign;
            LAST_SKIP_BITS(re, &s->gb, 1);
            pos += run;
            /* error */
            if (pos >= 64) {
                goto read_error;
            }
            pos1 = scan_table[pos];
            level = level << (shift_table[pos1] + shift_offset);
            block[pos1] = level;
            //            printf("run=%d level=%d shift=%d\n", run, level, shift_table[pos1]);
        } else {
            if (re_index > last_index)
                goto cannot_read;
            /* level is zero: means run without coding. No
               sign is coded */
            pos += run;
            /* error */
            if (pos >= 64) {
            read_error:
#if defined(VLC_DEBUG) || 1
                printf("error pos=%d\n", pos);
#endif
                /* for errors, we consider the eob is reached */
                mb->eob_reached = 1;
                break;
            }
        }
    }
    CLOSE_READER(re, &s->gb);
    mb->pos = pos;
}

static inline void bit_copy(PutBitContext *pb, GetBitContext *gb, int bits_left)
{
    while (bits_left >= 16) {
        put_bits(pb, 16, get_bits(gb, 16));
        bits_left -= 16;
    }
    if (bits_left > 0) {
        put_bits(pb, bits_left, get_bits(gb, bits_left));
    }
}

/* mb_x and mb_y are in units of 8 pixels */
static inline void dv_decode_video_segment(DVVideoDecodeContext *s, 
                                           UINT8 *buf_ptr1, 
                                           const UINT16 *mb_pos_ptr)
{
    int quant, dc, dct_mode, class1, j;
    int mb_index, mb_x, mb_y, v, last_index;
    DCTELEM *block, *block1;
    int c_offset, bits_left;
    UINT8 *y_ptr;
    BlockInfo mb_data[5 * 6], *mb, *mb1;
    void (*idct_put)(UINT8 *dest, int line_size, DCTELEM *block);
    UINT8 *buf_ptr;
    PutBitContext pb, vs_pb;
    UINT8 mb_bit_buffer[80 + 4]; /* allow some slack */
    int mb_bit_count;
    UINT8 vs_bit_buffer[5 * 80 + 4]; /* allow some slack */
    int vs_bit_count;
    
    memset(s->block, 0, sizeof(s->block));

    /* pass 1 : read DC and AC coefficients in blocks */
    buf_ptr = buf_ptr1;
    block1 = &s->block[0][0];
    mb1 = mb_data;
    init_put_bits(&vs_pb, vs_bit_buffer, 5 * 80, NULL, NULL);
    vs_bit_count = 0;
    for(mb_index = 0; mb_index < 5; mb_index++) {
        /* skip header */
        quant = buf_ptr[3] & 0x0f;
        buf_ptr += 4;
        init_put_bits(&pb, mb_bit_buffer, 80, NULL, NULL);
        mb_bit_count = 0;
        mb = mb1;
        block = block1;
        for(j = 0;j < 6; j++) {
            /* NOTE: size is not important here */
            init_get_bits(&s->gb, buf_ptr, 14);
            
            /* get the dc */
            dc = get_bits(&s->gb, 9);
            dc = (dc << (32 - 9)) >> (32 - 9);
            dct_mode = get_bits1(&s->gb);
            mb->dct_mode = dct_mode;
            mb->scan_table = s->dv_zigzag[dct_mode];
            class1 = get_bits(&s->gb, 2);
            mb->shift_offset = (class1 == 3);
            mb->shift_table = s->dv_shift[dct_mode]
                [quant + dv_quant_offset[class1]];
            dc = dc << 2;
            /* convert to unsigned because 128 is not added in the
               standard IDCT */
            dc += 1024;
            block[0] = dc;
            last_index = block_sizes[j];
            buf_ptr += last_index >> 3;
            mb->pos = 0;
            mb->partial_bit_count = 0;

            dv_decode_ac(s, mb, block, last_index);

            /* write the remaining bits  in a new buffer only if the
               block is finished */
            bits_left = last_index - s->gb.index;
            if (mb->eob_reached) {
                mb->partial_bit_count = 0;
                mb_bit_count += bits_left;
                bit_copy(&pb, &s->gb, bits_left);
            } else {
                /* should be < 16 bits otherwise a codeword could have
                   been parsed */
                mb->partial_bit_count = bits_left;
                mb->partial_bit_buffer = get_bits(&s->gb, bits_left);
            }
            block += 64;
            mb++;
        }
        
        flush_put_bits(&pb);

        /* pass 2 : we can do it just after */
#ifdef VLC_DEBUG
        printf("***pass 2 size=%d\n", mb_bit_count);
#endif
        block = block1;
        mb = mb1;
        init_get_bits(&s->gb, mb_bit_buffer, 80);
        for(j = 0;j < 6; j++) {
            if (!mb->eob_reached && s->gb.index < mb_bit_count) {
                dv_decode_ac(s, mb, block, mb_bit_count);
                /* if still not finished, no need to parse other blocks */
                if (!mb->eob_reached) {
                    /* we could not parse the current AC coefficient,
                       so we add the remaining bytes */
                    bits_left = mb_bit_count - s->gb.index;
                    if (bits_left > 0) {
                        mb->partial_bit_count += bits_left;
                        mb->partial_bit_buffer = 
                            (mb->partial_bit_buffer << bits_left) | 
                            get_bits(&s->gb, bits_left);
                    }
                    goto next_mb;
                }
            }
            block += 64;
            mb++;
        }
        /* all blocks are finished, so the extra bytes can be used at
           the video segment level */
        bits_left = mb_bit_count - s->gb.index;
        vs_bit_count += bits_left;
        bit_copy(&vs_pb, &s->gb, bits_left);
    next_mb:
        mb1 += 6;
        block1 += 6 * 64;
    }

    /* we need a pass other the whole video segment */
    flush_put_bits(&vs_pb);
        
#ifdef VLC_DEBUG
    printf("***pass 3 size=%d\n", vs_bit_count);
#endif
    block = &s->block[0][0];
    mb = mb_data;
    init_get_bits(&s->gb, vs_bit_buffer, 5 * 80);
    for(mb_index = 0; mb_index < 5; mb_index++) {
        for(j = 0;j < 6; j++) {
            if (!mb->eob_reached) {
#ifdef VLC_DEBUG
                printf("start %d:%d\n", mb_index, j);
#endif
                dv_decode_ac(s, mb, block, vs_bit_count);
            }
            block += 64;
            mb++;
        }
    }
    
    /* compute idct and place blocks */
    block = &s->block[0][0];
    mb = mb_data;
    for(mb_index = 0; mb_index < 5; mb_index++) {
        v = *mb_pos_ptr++;
        mb_x = v & 0xff;
        mb_y = v >> 8;
        y_ptr = s->current_picture[0] + (mb_y * s->linesize[0] * 8) + (mb_x * 8);
        if (s->sampling_411)
            c_offset = (mb_y * s->linesize[1] * 8) + ((mb_x >> 2) * 8);
        else
            c_offset = ((mb_y >> 1) * s->linesize[1] * 8) + ((mb_x >> 1) * 8);
        for(j = 0;j < 6; j++) {
            idct_put = s->idct_put[mb->dct_mode];
            if (j < 4) {
                if (s->sampling_411 && mb_x < (704 / 8)) {
                    /* NOTE: at end of line, the macroblock is handled as 420 */
                    idct_put(y_ptr + (j * 8), s->linesize[0], block);
                } else {
                    idct_put(y_ptr + ((j & 1) * 8) + ((j >> 1) * 8 * s->linesize[0]),
                             s->linesize[0], block);
                }
            } else {
                if (s->sampling_411 && mb_x >= (704 / 8)) {
                    uint8_t pixels[64], *c_ptr, *c_ptr1, *ptr;
                    int y, linesize;
                    /* NOTE: at end of line, the macroblock is handled as 420 */
                    idct_put(pixels, 8, block);
                    linesize = s->linesize[6 - j];
                    c_ptr = s->current_picture[6 - j] + c_offset;
                    ptr = pixels;
                    for(y = 0;y < 8; y++) {
                        /* convert to 411P */
                        c_ptr1 = c_ptr + linesize;
                        c_ptr1[0] = c_ptr[0] = (ptr[0] + ptr[1]) >> 1;
                        c_ptr1[1] = c_ptr[1] = (ptr[2] + ptr[3]) >> 1;
                        c_ptr1[2] = c_ptr[2] = (ptr[4] + ptr[5]) >> 1;
                        c_ptr1[3] = c_ptr[3] = (ptr[6] + ptr[7]) >> 1;
                        c_ptr += linesize * 2;
                        ptr += 8;
                    }
                } else {
                    /* don't ask me why they inverted Cb and Cr ! */
                    idct_put(s->current_picture[6 - j] + c_offset, 
                             s->linesize[6 - j], block);
                }
            }
            block += 64;
            mb++;
        }
    }
}


/* NOTE: exactly one frame must be given (120000 bytes for NTSC,
   144000 bytes for PAL) */
static int dvvideo_decode_frame(AVCodecContext *avctx, 
                                 void *data, int *data_size,
                                 UINT8 *buf, int buf_size)
{
    DVVideoDecodeContext *s = avctx->priv_data;
    int sct, dsf, apt, ds, nb_dif_segs, vs, width, height, i, packet_size;
    unsigned size;
    UINT8 *buf_ptr;
    const UINT16 *mb_pos_ptr;
    AVPicture *picture;
    
    /* parse id */
    init_get_bits(&s->gb, buf, buf_size);
    sct = get_bits(&s->gb, 3);
    if (sct != 0)
        return -1;
    skip_bits(&s->gb, 5);
    get_bits(&s->gb, 4); /* dsn (sequence number */
    get_bits(&s->gb, 1); /* fsc (channel number) */
    skip_bits(&s->gb, 3);
    get_bits(&s->gb, 8); /* dbn (diff block number 0-134) */

    dsf = get_bits(&s->gb, 1); /* 0 = NTSC 1 = PAL */
    if (get_bits(&s->gb, 1) != 0)
        return -1;
    skip_bits(&s->gb, 11);
    apt = get_bits(&s->gb, 3); /* apt */

    get_bits(&s->gb, 1); /* tf1 */
    skip_bits(&s->gb, 4);
    get_bits(&s->gb, 3); /* ap1 */

    get_bits(&s->gb, 1); /* tf2 */
    skip_bits(&s->gb, 4);
    get_bits(&s->gb, 3); /* ap2 */

    get_bits(&s->gb, 1); /* tf3 */
    skip_bits(&s->gb, 4);
    get_bits(&s->gb, 3); /* ap3 */
    
    /* init size */
    width = 720;
    if (dsf) {
        avctx->frame_rate = 25 * FRAME_RATE_BASE;
        packet_size = PAL_FRAME_SIZE;
        height = 576;
        nb_dif_segs = 12;
    } else {
        avctx->frame_rate = 30 * FRAME_RATE_BASE;
        packet_size = NTSC_FRAME_SIZE;
        height = 480;
        nb_dif_segs = 10;
    }
    /* NOTE: we only accept several full frames */
    if (buf_size < packet_size)
        return -1;
    
    /* XXX: is it correct to assume that 420 is always used in PAL
       mode ? */
    s->sampling_411 = !dsf;
    if (s->sampling_411) {
        mb_pos_ptr = dv_place_411;
        avctx->pix_fmt = PIX_FMT_YUV411P;
    } else {
        mb_pos_ptr = dv_place_420;
        avctx->pix_fmt = PIX_FMT_YUV420P;
    }

    avctx->width = width;
    avctx->height = height;

    if (avctx->flags & CODEC_FLAG_DR1)
    {
	s->width = -1;
	avctx->dr_buffer[0] = avctx->dr_buffer[1] = avctx->dr_buffer[2] = 0;
	if(avctx->get_buffer_callback(avctx, width, height, I_TYPE) < 0
	   && avctx->flags & CODEC_FLAG_DR1) {
	    fprintf(stderr, "get_buffer() failed\n");
	    return -1;
	}
    }

    /* (re)alloc picture if needed */
    if (s->width != width || s->height != height) {
	if (!(avctx->flags & CODEC_FLAG_DR1))
	    for(i=0;i<3;i++) {
		if (avctx->dr_buffer[i] != s->current_picture[i])
		    av_freep(&s->current_picture[i]);
		avctx->dr_buffer[i] = 0;
	    }

        for(i=0;i<3;i++) {
	    if (avctx->dr_buffer[i]) {
		s->current_picture[i] = avctx->dr_buffer[i];
		s->linesize[i] = (i == 0) ? avctx->dr_stride : avctx->dr_uvstride;
	    } else {
		size = width * height;
		s->linesize[i] = width;
		if (i >= 1) {
		    size >>= 2;
		    s->linesize[i] >>= s->sampling_411 ? 2 : 1;
		}
		s->current_picture[i] = av_malloc(size);
	    }
            if (!s->current_picture[i])
                return -1;
        }
        s->width = width;
        s->height = height;
    }

    /* for each DIF segment */
    buf_ptr = buf;
    for (ds = 0; ds < nb_dif_segs; ds++) {
        buf_ptr += 6 * 80; /* skip DIF segment header */
        
        for(vs = 0; vs < 27; vs++) {
            if ((vs % 3) == 0) {
                /* skip audio block */
                buf_ptr += 80;
            }
            dv_decode_video_segment(s, buf_ptr, mb_pos_ptr);
            buf_ptr += 5 * 80;
            mb_pos_ptr += 5;
        }
    }

    emms_c();

    /* return image */
    *data_size = sizeof(AVPicture);
    picture = data;
    for(i=0;i<3;i++) {
        picture->data[i] = s->current_picture[i];
        picture->linesize[i] = s->linesize[i];
    }
    return packet_size;
}

static int dvvideo_decode_end(AVCodecContext *avctx)
{
    DVVideoDecodeContext *s = avctx->priv_data;
    int i;

    for(i=0;i<3;i++)
	if (avctx->dr_buffer[i] != s->current_picture[i])
        av_freep(&s->current_picture[i]);
    return 0;
}

AVCodec dvvideo_decoder = {
    "dvvideo",
    CODEC_TYPE_VIDEO,
    CODEC_ID_DVVIDEO,
    sizeof(DVVideoDecodeContext),
    dvvideo_decode_init,
    NULL,
    dvvideo_decode_end,
    dvvideo_decode_frame,
    CODEC_CAP_DR1,
    NULL
};

typedef struct DVAudioDecodeContext {
    AVCodecContext *avctx;
    GetBitContext gb;

} DVAudioDecodeContext;

static int dvaudio_decode_init(AVCodecContext *avctx)
{
    //    DVAudioDecodeContext *s = avctx->priv_data;
    return 0;
}

/* NOTE: exactly one frame must be given (120000 bytes for NTSC,
   144000 bytes for PAL) */
static int dvaudio_decode_frame(AVCodecContext *avctx, 
                                 void *data, int *data_size,
                                 UINT8 *buf, int buf_size)
{
    //    DVAudioDecodeContext *s = avctx->priv_data;
    return buf_size;
}

static int dvaudio_decode_end(AVCodecContext *avctx)
{
    //    DVAudioDecodeContext *s = avctx->priv_data;
    return 0;
}

AVCodec dvaudio_decoder = {
    "dvaudio",
    CODEC_TYPE_AUDIO,
    CODEC_ID_DVAUDIO,
    sizeof(DVAudioDecodeContext),
    dvaudio_decode_init,
    NULL,
    dvaudio_decode_end,
    dvaudio_decode_frame,
    0,
    NULL
};
