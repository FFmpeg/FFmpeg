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

/**
 * @file dv.c
 * DV decoder.
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
    uint8_t *current_picture[3]; /* picture structure */
    AVFrame picture;
    int linesize[3];
    DCTELEM block[5*6][64] __align8;
    uint8_t dv_zigzag[2][64];
    uint8_t idct_permutation[64];
    /* XXX: move it to static storage ? */
    uint8_t dv_shift[2][22][64];
    void (*idct_put[2])(uint8_t *dest, int line_size, DCTELEM *block);
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
    static int done=0;

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
    dsputil_init(&s2.dsp, avctx);
    if (DCT_common_init(&s2) < 0)
       return -1;

    s->idct_put[0] = s2.dsp.idct_put;
    memcpy(s->idct_permutation, s2.dsp.idct_permutation, 64);
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
    const uint8_t *shift_table;
    const uint8_t *scan_table;
    uint8_t pos; /* position in block */
    uint8_t eob_reached; /* true if EOB has been reached */
    uint8_t dct_mode;
    uint8_t partial_bit_count;
    uint16_t partial_bit_buffer;
    int shift_offset;
} BlockInfo;

/* block size in bits */
static const uint16_t block_sizes[6] = {
    112, 112, 112, 112, 80, 80
};

#ifndef ALT_BITSTREAM_READER
#error only works with ALT_BITSTREAM_READER
#endif

/* decode ac coefs */
static void dv_decode_ac(DVVideoDecodeContext *s, 
                         BlockInfo *mb, DCTELEM *block, int last_index)
{
    int last_re_index;
    int shift_offset = mb->shift_offset;
    const uint8_t *scan_table = mb->scan_table;
    const uint8_t *shift_table = mb->shift_table;
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
        uint8_t buf[4];
        uint32_t v;
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
        init_get_bits(&gb1, buf, 4*8);
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
                                           uint8_t *buf_ptr1, 
                                           const uint16_t *mb_pos_ptr)
{
    int quant, dc, dct_mode, class1, j;
    int mb_index, mb_x, mb_y, v, last_index;
    DCTELEM *block, *block1;
    int c_offset, bits_left;
    uint8_t *y_ptr;
    BlockInfo mb_data[5 * 6], *mb, *mb1;
    void (*idct_put)(uint8_t *dest, int line_size, DCTELEM *block);
    uint8_t *buf_ptr;
    PutBitContext pb, vs_pb;
    uint8_t mb_bit_buffer[80 + 4]; /* allow some slack */
    int mb_bit_count;
    uint8_t vs_bit_buffer[5 * 80 + 4]; /* allow some slack */
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
            init_get_bits(&s->gb, buf_ptr, 14*8);
            
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
        init_get_bits(&s->gb, mb_bit_buffer, 80*8);
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
    init_get_bits(&s->gb, vs_bit_buffer, 5 * 80*8);
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
                                 uint8_t *buf, int buf_size)
{
    DVVideoDecodeContext *s = avctx->priv_data;
    int sct, dsf, apt, ds, nb_dif_segs, vs, width, height, i, packet_size;
    uint8_t *buf_ptr;
    const uint16_t *mb_pos_ptr;
    
    /* parse id */
    init_get_bits(&s->gb, buf, buf_size*8);
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
        avctx->frame_rate = 25;
        packet_size = PAL_FRAME_SIZE;
        height = 576;
        nb_dif_segs = 12;
    } else {
        avctx->frame_rate = 30;
        packet_size = NTSC_FRAME_SIZE;
        height = 480;
        nb_dif_segs = 10;
    }
    avctx->frame_rate_base= 1;
    /* NOTE: we only accept several full frames */
    if (buf_size < packet_size)
        return -1;
    
    /* NTSC[dsf == 0] is always 720x480, 4:1:1
     *  PAL[dsf == 1] is always 720x576, 4:2:0 for IEC 68134[apt == 0]
     *  but for the SMPTE 314M[apt == 1] it is 720x576, 4:1:1
     */
    s->sampling_411 = !dsf || apt;
    if (s->sampling_411) {
        mb_pos_ptr = dsf ? dv_place_411P : dv_place_411;
        avctx->pix_fmt = PIX_FMT_YUV411P;
    } else {
        mb_pos_ptr = dv_place_420;
        avctx->pix_fmt = PIX_FMT_YUV420P;
    }

    avctx->width = width;
    avctx->height = height;
    
    /* Once again, this is pretty complicated by the fact that the same
     * field is used differently by IEC 68134[apt == 0] and 
     * SMPTE 314M[apt == 1].
     */
    if (buf[VAUX_TC61_OFFSET] == 0x61 &&
        ((apt == 0 && (buf[VAUX_TC61_OFFSET + 2] & 0x07) == 0x07) ||
	 (apt == 1 && (buf[VAUX_TC61_OFFSET + 2] & 0x07) == 0x02)))
        avctx->aspect_ratio = 16.0 / 9.0;
    else
        avctx->aspect_ratio = 4.0 / 3.0;

    s->picture.reference= 0;
    if(avctx->get_buffer(avctx, &s->picture) < 0) {
        fprintf(stderr, "get_buffer() failed\n");
        return -1;
    }

    for(i=0;i<3;i++) {
        s->current_picture[i] = s->picture.data[i];
        s->linesize[i] = s->picture.linesize[i];
        if (!s->current_picture[i])
            return -1;
    }
    s->width = width;
    s->height = height;

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
    *data_size = sizeof(AVFrame);
    *(AVFrame*)data= s->picture;
    
    avctx->release_buffer(avctx, &s->picture);
    
    return packet_size;
}

static int dvvideo_decode_end(AVCodecContext *avctx)
{
    DVVideoDecodeContext *s = avctx->priv_data;
    int i;
    
    if(avctx->get_buffer == avcodec_default_get_buffer){
        for(i=0; i<4; i++){
            av_freep(&s->picture.base[i]);
            s->picture.data[i]= NULL;
        }
        av_freep(&s->picture.opaque);
    }

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

static uint16_t dv_audio_12to16(uint16_t sample)
{
    uint16_t shift, result;
    
    sample = (sample < 0x800) ? sample : sample | 0xf000;
    shift = (sample & 0xf00) >> 8;

    if (shift < 0x2 || shift > 0xd) {
	result = sample;
    } else if (shift < 0x8) {
        shift--;
	result = (sample - (256 * shift)) << shift;
    } else {
	shift = 0xe - shift;
	result = ((sample + ((256 * shift) + 1)) << shift) - 1;
    }

    return result;
}

/* NOTE: exactly one frame must be given (120000 bytes for NTSC,
   144000 bytes for PAL) 

   There's a couple of assumptions being made here:
         1. By default we silence erroneous (0x8000/16bit 0x800/12bit) 
	    audio samples. We can pass them upwards when ffmpeg will be ready
	    to deal with them.
	 2. We don't do software emphasis.
	 3. Audio is always returned as 16bit linear samples: 12bit
	    nonlinear samples are converted into 16bit linear ones.
*/
static int dvaudio_decode_frame(AVCodecContext *avctx, 
                                 void *data, int *data_size,
                                 uint8_t *buf, int buf_size)
{
    DVVideoDecodeContext *s = avctx->priv_data;
    const uint16_t (*unshuffle)[9];
    int smpls, freq, quant, sys, stride, difseg, ad, dp, nb_dif_segs, i;
    uint16_t lc, rc;
    uint8_t *buf_ptr;
    
    /* parse id */
    init_get_bits(&s->gb, &buf[AAUX_AS_OFFSET], 5*8);
    i = get_bits(&s->gb, 8);
    if (i != 0x50) { /* No audio ? */
	*data_size = 0;
	return buf_size;
    }
    
    get_bits(&s->gb, 1); /* 0 - locked audio, 1 - unlocked audio */
    skip_bits(&s->gb, 1);
    smpls = get_bits(&s->gb, 6); /* samples in this frame - min. samples */

    skip_bits(&s->gb, 8);

    skip_bits(&s->gb, 2);
    sys = get_bits(&s->gb, 1); /* 0 - 60 fields, 1 = 50 fields */
    skip_bits(&s->gb, 5);

    get_bits(&s->gb, 1); /* 0 - emphasis on, 1 - emphasis off */
    get_bits(&s->gb, 1); /* 0 - reserved, 1 - emphasis time constant 50/15us */
    freq = get_bits(&s->gb, 3); /* 0 - 48KHz, 1 - 44,1kHz, 2 - 32 kHz */
    quant = get_bits(&s->gb, 3); /* 0 - 16bit linear, 1 - 12bit nonlinear */

    if (quant > 1)
	return -1; /* Unsupported quantization */

    avctx->sample_rate = dv_audio_frequency[freq];
    avctx->channels = 2;
    // What about:
    // avctx->bit_rate = 
    // avctx->frame_size =
   
    *data_size = (dv_audio_min_samples[sys][freq] + smpls) * 
	         avctx->channels * 2;

    if (sys) {
	nb_dif_segs = 12;
	stride = 108;
	unshuffle = dv_place_audio50;
    } else {
	nb_dif_segs = 10;
	stride = 90;
	unshuffle = dv_place_audio60;
    }
    
    /* for each DIF segment */
    buf_ptr = buf;
    for (difseg = 0; difseg < nb_dif_segs; difseg++) {
         buf_ptr += 6 * 80; /* skip DIF segment header */
         for (ad = 0; ad < 9; ad++) {
              
              for (dp = 8; dp < 80; dp+=2) {
		   if (quant == 0) {  /* 16bit quantization */
		       i = unshuffle[difseg][ad] + (dp - 8)/2 * stride;
		       ((short *)data)[i] = (buf_ptr[dp] << 8) | buf_ptr[dp+1]; 
		       if (((unsigned short *)data)[i] == 0x8000)
		           ((short *)data)[i] = 0;
		   } else {           /* 12bit quantization */
		       if (difseg >= nb_dif_segs/2)
			   goto out;  /* We're not doing 4ch at this time */
		       
		       lc = ((uint16_t)buf_ptr[dp] << 4) | 
			    ((uint16_t)buf_ptr[dp+2] >> 4);
		       rc = ((uint16_t)buf_ptr[dp+1] << 4) |
			    ((uint16_t)buf_ptr[dp+2] & 0x0f);
		       lc = (lc == 0x800 ? 0 : dv_audio_12to16(lc));
		       rc = (rc == 0x800 ? 0 : dv_audio_12to16(rc));

		       i = unshuffle[difseg][ad] + (dp - 8)/3 * stride;
		       ((short *)data)[i] = lc;
		       i = unshuffle[difseg+nb_dif_segs/2][ad] + (dp - 8)/3 * stride;
		       ((short *)data)[i] = rc;
		       ++dp;
		   }
	      }
		
	    buf_ptr += 16 * 80; /* 15 Video DIFs + 1 Audio DIF */
        }
    }

out:
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
