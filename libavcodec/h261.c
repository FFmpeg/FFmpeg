/*
 * H261 decoder
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2004 Maarten Daniels
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
 * @file h261.c
 * h261codec.
 */

#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h261data.h"


#define H261_MBA_VLC_BITS 9
#define H261_MTYPE_VLC_BITS 6
#define H261_MV_VLC_BITS 7
#define H261_CBP_VLC_BITS 9
#define TCOEFF_VLC_BITS 9

#define MAX_MBA 33
#define IS_FIL(a)    ((a)&MB_TYPE_H261_FIL)

/**
 * H261Context
 */
typedef struct H261Context{
    MpegEncContext s;

    int current_mba;
    int mba_diff;
    int mtype;
    int current_mv_x;
    int current_mv_y;
    int gob_number;
    int loop_filter;
    int bits_left; //8 - nr of bits left of the following frame in the last byte in this frame
    int last_bits; //bits left of the following frame in the last byte in this frame
}H261Context;

void ff_h261_loop_filter(H261Context * h){
    MpegEncContext * const s = &h->s;
    const int linesize  = s->linesize;
    const int uvlinesize= s->uvlinesize;
    uint8_t *dest_y = s->dest[0];
    uint8_t *dest_cb= s->dest[1];
    uint8_t *dest_cr= s->dest[2];
    
    s->dsp.h261_loop_filter(dest_y                   , linesize);
    s->dsp.h261_loop_filter(dest_y                + 8, linesize);
    s->dsp.h261_loop_filter(dest_y + 8 * linesize    , linesize);
    s->dsp.h261_loop_filter(dest_y + 8 * linesize + 8, linesize);
    s->dsp.h261_loop_filter(dest_cb, uvlinesize);
    s->dsp.h261_loop_filter(dest_cr, uvlinesize);
}

static int h261_decode_block(H261Context *h, DCTELEM *block,
                             int n, int coded);
static int h261_decode_mb(H261Context *h,
                      DCTELEM block[6][64]);
void ff_set_qscale(MpegEncContext * s, int qscale);

/***********************************************/
/* decoding */

static VLC h261_mba_vlc;
static VLC h261_mtype_vlc;
static VLC h261_mv_vlc;
static VLC h261_cbp_vlc;

void init_vlc_rl(RLTable *rl);

static void h261_decode_init_vlc(H261Context *h){
    static int done = 0;

    if(!done){
        done = 1;
        init_vlc(&h261_mba_vlc, H261_MBA_VLC_BITS, 34,
                 h261_mba_bits, 1, 1,
                 h261_mba_code, 1, 1);
        init_vlc(&h261_mtype_vlc, H261_MTYPE_VLC_BITS, 10,
                 h261_mtype_bits, 1, 1,
                 h261_mtype_code, 1, 1);
        init_vlc(&h261_mv_vlc, H261_MV_VLC_BITS, 17,
                 &h261_mv_tab[0][1], 2, 1,
                 &h261_mv_tab[0][0], 2, 1);
        init_vlc(&h261_cbp_vlc, H261_CBP_VLC_BITS, 63,
                 &h261_cbp_tab[0][1], 2, 1,
                 &h261_cbp_tab[0][0], 2, 1);
        init_rl(&h261_rl_tcoeff);
        init_vlc_rl(&h261_rl_tcoeff);
    }
}

static int h261_decode_init(AVCodecContext *avctx){
    H261Context *h= avctx->priv_data;
    MpegEncContext * const s = &h->s;

    // set defaults
    MPV_decode_defaults(s);
    s->avctx = avctx;

    s->width = s->avctx->width;
    s->height = s->avctx->height;
    s->codec_id = s->avctx->codec->id;

    s->out_format = FMT_H261;
    s->low_delay= 1;
    avctx->pix_fmt= PIX_FMT_YUV420P;

    s->codec_id= avctx->codec->id;

    h261_decode_init_vlc(h);

    h->bits_left = 0;
    h->last_bits = 0;
    
    return 0;
}

/**
 * decodes the group of blocks header or slice header.
 * @return <0 if an error occured
 */
static int h261_decode_gob_header(H261Context *h){
    unsigned int val;
    MpegEncContext * const s = &h->s;
    
    /* Check for GOB Start Code */
    val = show_bits(&s->gb, 15);
    if(val)
        return -1;

    /* We have a GBSC */
    skip_bits(&s->gb, 16);

    h->gob_number = get_bits(&s->gb, 4); /* GN */
    s->qscale = get_bits(&s->gb, 5); /* GQUANT */

    /* GEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }

    if(s->qscale==0)
        return -1;

    // For the first transmitted macroblock in a GOB, MBA is the absolute address. For
    // subsequent macroblocks, MBA is the difference between the absolute addresses of
    // the macroblock and the last transmitted macroblock.
    h->current_mba = 0;
    h->mba_diff = 0;

    return 0;
}

/**
 * decodes the group of blocks / video packet header.
 * @return <0 if no resync found
 */
static int ff_h261_resync(H261Context *h){
    MpegEncContext * const s = &h->s;
    int left, ret;

    if(show_bits(&s->gb, 15)==0){
        ret= h261_decode_gob_header(h);
        if(ret>=0)
            return 0;
    }
    //ok, its not where its supposed to be ...
    s->gb= s->last_resync_gb;
    align_get_bits(&s->gb);
    left= s->gb.size_in_bits - get_bits_count(&s->gb);

    for(;left>15+1+4+5; left-=8){
        if(show_bits(&s->gb, 15)==0){
            GetBitContext bak= s->gb;

            ret= h261_decode_gob_header(h);
            if(ret>=0)
                return 0;

            s->gb= bak;
        }
        skip_bits(&s->gb, 8);
    }

    return -1;
}

/**
 * decodes skipped macroblocks
 * @return 0
 */
static int h261_decode_mb_skipped(H261Context *h, int mba1, int mba2 )
{
    MpegEncContext * const s = &h->s;
    int i;
    
    s->mb_intra = 0;

    for(i=mba1; i<mba2; i++){
        int j, xy;

        s->mb_x= ((h->gob_number-1) % 2) * 11 + i % 11;
        s->mb_y= ((h->gob_number-1) / 2) * 3 + i / 11;
        xy = s->mb_x + s->mb_y * s->mb_stride;
        ff_init_block_index(s);
        ff_update_block_index(s);
        s->dsp.clear_blocks(s->block[0]);

        for(j=0;j<6;j++)
            s->block_last_index[j] = -1;

        s->mv_dir = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        s->current_picture.mb_type[xy]= MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
        s->mv[0][0][0] = 0;
        s->mv[0][0][1] = 0;
        s->mb_skiped = 1;

        MPV_decode_mb(s, s->block);
    }

    return 0;
}

static int decode_mv_component(GetBitContext *gb, int v){
    int mv_diff = get_vlc2(gb, h261_mv_vlc.table, H261_MV_VLC_BITS, 2);
    mv_diff = mvmap[mv_diff];

    if(mv_diff && !get_bits1(gb))
        mv_diff= -mv_diff;
    
    v += mv_diff;
    if     (v <=-16) v+= 32;
    else if(v >= 16) v-= 32;

    return v;
}

static int h261_decode_mb(H261Context *h,
                          DCTELEM block[6][64])
{
    MpegEncContext * const s = &h->s;
    int i, cbp, xy, old_mtype;

    cbp = 63;
    // Read mba
    do{
        h->mba_diff = get_vlc2(&s->gb, h261_mba_vlc.table, H261_MBA_VLC_BITS, 2)+1;
    }
    while( h->mba_diff == MAX_MBA + 1 ); // stuffing

    if ( h->mba_diff < 0 )
        return -1;

    h->current_mba += h->mba_diff;

    if ( h->current_mba > MAX_MBA )
        return -1;
    
    s->mb_x= ((h->gob_number-1) % 2) * 11 + ((h->current_mba-1) % 11);
    s->mb_y= ((h->gob_number-1) / 2) * 3 + ((h->current_mba-1) / 11);

    xy = s->mb_x + s->mb_y * s->mb_stride;

    ff_init_block_index(s);
    ff_update_block_index(s);
    s->dsp.clear_blocks(s->block[0]);

    // Read mtype
    old_mtype = h->mtype;
    h->mtype = get_vlc2(&s->gb, h261_mtype_vlc.table, H261_MTYPE_VLC_BITS, 2);
    h->mtype = h261_mtype_map[h->mtype];

    if (IS_FIL (h->mtype))
        h->loop_filter = 1;

    // Read mquant
    if ( IS_QUANT ( h->mtype ) ){
        ff_set_qscale(s, get_bits(&s->gb, 5));
    }

    s->mb_intra = IS_INTRA4x4(h->mtype);

    // Read mv
    if ( IS_16X16 ( h->mtype ) ){
        // Motion vector data is included for all MC macroblocks. MVD is obtained from the macroblock vector by subtracting the
        // vector of the preceding macroblock. For this calculation the vector of the preceding macroblock is regarded as zero in the
        // following three situations:
        // 1) evaluating MVD for macroblocks 1, 12 and 23;
        // 2) evaluating MVD for macroblocks in which MBA does not represent a difference of 1;
        // 3) MTYPE of the previous macroblock was not MC.
        if ( ( h->current_mba == 1 ) || ( h->current_mba == 12 ) || ( h->current_mba == 23 ) ||
             ( h->mba_diff != 1) || ( !IS_16X16 ( old_mtype ) ))
        {
            h->current_mv_x = 0;
            h->current_mv_y = 0;
        }

        h->current_mv_x= decode_mv_component(&s->gb, h->current_mv_x);
        h->current_mv_y= decode_mv_component(&s->gb, h->current_mv_y);
    }

    // Read cbp
    if ( HAS_CBP( h->mtype ) ){
        cbp = get_vlc2(&s->gb, h261_cbp_vlc.table, H261_CBP_VLC_BITS, 2) + 1;
    }

    if(s->mb_intra){
        s->current_picture.mb_type[xy]= MB_TYPE_INTRA;
        goto intra;
    }

    //set motion vectors
    s->mv_dir = MV_DIR_FORWARD;
    s->mv_type = MV_TYPE_16X16;
    s->current_picture.mb_type[xy]= MB_TYPE_16x16 | MB_TYPE_L0;
    if(IS_16X16 ( h->mtype )){
        s->mv[0][0][0] = h->current_mv_x * 2;//gets divided by 2 in motion compensation
        s->mv[0][0][1] = h->current_mv_y * 2;
    }
    else{
        h->current_mv_x = s->mv[0][0][0] = 0;
        h->current_mv_x = s->mv[0][0][1] = 0;
    }

intra:
    /* decode each block */
    if(s->mb_intra || HAS_CBP(h->mtype)){
        for (i = 0; i < 6; i++) {
            if (h261_decode_block(h, block[i], i, cbp&32) < 0){
                return -1;
            }
            cbp+=cbp;
        }
    }

    /* per-MB end of slice check */
    {
        int v= show_bits(&s->gb, 15);

        if(get_bits_count(&s->gb) + 15 > s->gb.size_in_bits){
            v>>= get_bits_count(&s->gb) + 15 - s->gb.size_in_bits;
        }

        if(v==0){
            return SLICE_END;
        }
    }
    return SLICE_OK;
}

/**
 * decodes a macroblock
 * @return <0 if an error occured
 */
static int h261_decode_block(H261Context * h, DCTELEM * block,
                             int n, int coded)
{
    MpegEncContext * const s = &h->s;
    int code, level, i, j, run;
    RLTable *rl = &h261_rl_tcoeff;
    const uint8_t *scan_table;
    
    // For the variable length encoding there are two code tables, one being used for
    // the first transmitted LEVEL in INTER, INTER+MC and INTER+MC+FIL blocks, the second
    // for all other LEVELs except the first one in INTRA blocks which is fixed length
    // coded with 8 bits.
    // NOTE: the two code tables only differ in one VLC so we handle that manually.
    scan_table = s->intra_scantable.permutated;
    if (s->mb_intra){
        /* DC coef */
        level = get_bits(&s->gb, 8);
        // 0 (00000000b) and -128 (10000000b) are FORBIDDEN
        if((level&0x7F) == 0){
            av_log(s->avctx, AV_LOG_ERROR, "illegal dc %d at %d %d\n", level, s->mb_x, s->mb_y);
            return -1;
        }
        // The code 1000 0000 is not used, the reconstruction level of 1024 being coded as 1111 1111.
        if (level == 255)
            level = 128;
        block[0] = level;
        i = 1;
    }else if(coded){
        // Run  Level   Code
        // EOB                  Not possible for first level when cbp is available (that's why the table is different)
        // 0    1               1s
        // *    *               0*
        int check = show_bits(&s->gb, 2);
        i = 0;
        if ( check & 0x2 ){
            skip_bits(&s->gb, 2);
            block[0] = ( check & 0x1 ) ? -1 : 1;
            i = 1;
        }
    }else{
        i = 0;
    }
    if(!coded){
        s->block_last_index[n] = i - 1;
        return 0;
    }
    for(;;){
        code = get_vlc2(&s->gb, rl->vlc.table, TCOEFF_VLC_BITS, 2);
        if (code < 0){
            av_log(s->avctx, AV_LOG_ERROR, "illegal ac vlc code at %dx%d\n", s->mb_x, s->mb_y);
            return -1;
        }
        if (code == rl->n) {
            /* escape */
            // The remaining combinations of (run, level) are encoded with a 20-bit word consisting of 6 bits escape, 6 bits run and 8 bits level.
            run = get_bits(&s->gb, 6);
            level = (int8_t)get_bits(&s->gb, 8);
        }else if(code == 0){
            break;
        }else{
            run = rl->table_run[code];
            level = rl->table_level[code];
            if (get_bits1(&s->gb))
                level = -level;
        }
        i += run;
        if (i >= 64){
            av_log(s->avctx, AV_LOG_ERROR, "run overflow at %dx%d\n", s->mb_x, s->mb_y);
            return -1;
        }
        j = scan_table[i];
        block[j] = level;
        i++;
    }
    s->block_last_index[n] = i-1;
    return 0;
}

/**
 * decodes the H261 picture header.
 * @return <0 if no startcode found
 */
int h261_decode_picture_header(H261Context *h){
    MpegEncContext * const s = &h->s;
    int format, i;
    static int h261_framecounter = 0;
    uint32_t startcode;
    align_get_bits(&s->gb);

    startcode = (h->last_bits << (12 - (8-h->bits_left))) | get_bits(&s->gb, 20-8 - (8- h->bits_left));

    for(i= s->gb.size_in_bits - get_bits_count(&s->gb); i>24; i-=1){
        startcode = ((startcode << 1) | get_bits(&s->gb, 1)) & 0x000FFFFF;

        if(startcode == 0x10)
            break;
    }

    if (startcode != 0x10){
        av_log(s->avctx, AV_LOG_ERROR, "Bad picture start code\n");
        return -1;
    }

    /* temporal reference */
    s->picture_number = get_bits(&s->gb, 5); /* picture timestamp */

    /* PTYPE starts here */
    skip_bits1(&s->gb); /* split screen off */
    skip_bits1(&s->gb); /* camera  off */
    skip_bits1(&s->gb); /* freeze picture release off */

    format = get_bits1(&s->gb);

    //only 2 formats possible
    if (format == 0){//QCIF
        s->width = 176;
        s->height = 144;
        s->mb_width = 11;
        s->mb_height = 9;
    }else{//CIF
        s->width = 352;
        s->height = 288;
        s->mb_width = 22;
        s->mb_height = 18;
    }

    s->mb_num = s->mb_width * s->mb_height;

    skip_bits1(&s->gb); /* still image mode off */
    skip_bits1(&s->gb); /* Reserved */

    /* PEI */
    while (get_bits1(&s->gb) != 0){
        skip_bits(&s->gb, 8);
    }

    //h261 has no I-FRAMES, pass the test in MPV_frame_start in mpegvideo.c
    if(h261_framecounter > 1)
        s->pict_type = P_TYPE;
    else
        s->pict_type = I_TYPE;

    h261_framecounter++;

    h->gob_number = 0;
    return 0;
}

static int h261_decode_gob(H261Context *h){
    MpegEncContext * const s = &h->s;
    int v;
    
    ff_set_qscale(s, s->qscale);

    /* check for empty gob */
    v= show_bits(&s->gb, 15);

    if(get_bits_count(&s->gb) + 15 > s->gb.size_in_bits){
        v>>= get_bits_count(&s->gb) + 15 - s->gb.size_in_bits;
    }

    if(v==0){
        h261_decode_mb_skipped(h, 0, 33);
        return 0;
    }

    /* decode mb's */
    while(h->current_mba <= MAX_MBA)
    {
        int ret;
        /* DCT & quantize */
        ret= h261_decode_mb(h, s->block);
        if(ret<0){
            const int xy= s->mb_x + s->mb_y*s->mb_stride;
            if(ret==SLICE_END){
                MPV_decode_mb(s, s->block);
                if(h->loop_filter){
                    ff_h261_loop_filter(h);
                }
                h->loop_filter = 0;
                h261_decode_mb_skipped(h, h->current_mba-h->mba_diff, h->current_mba-1);
                h261_decode_mb_skipped(h, h->current_mba, 33);                
                return 0;
            }else if(ret==SLICE_NOEND){
                av_log(s->avctx, AV_LOG_ERROR, "Slice mismatch at MB: %d\n", xy);
                return -1;
            }
            av_log(s->avctx, AV_LOG_ERROR, "Error at MB: %d\n", xy);
            return -1;
        }
        MPV_decode_mb(s, s->block);
        if(h->loop_filter){
            ff_h261_loop_filter(h);
        }

        h->loop_filter = 0;
        h261_decode_mb_skipped(h, h->current_mba-h->mba_diff, h->current_mba-1);
    }
    
    return -1;
}

static int h261_find_frame_end(ParseContext *pc, AVCodecContext* avctx, const uint8_t *buf, int buf_size){
    int vop_found, i, j, bits_left, last_bits;
    uint32_t state;

    H261Context *h = avctx->priv_data;

    if(h){
        bits_left = h->bits_left;
        last_bits = h->last_bits;
    }
    else{
        bits_left = 0;
        last_bits = 0;
    }

    vop_found= pc->frame_start_found;
    state= pc->state;
    if(bits_left!=0 && !vop_found)
        state = state << (8-bits_left) | last_bits;
    i=0;
    if(!vop_found){
        for(i=0; i<buf_size; i++){
            state= (state<<8) | buf[i];
            for(j=0; j<8; j++){
                if(( (  (state<<j)  |  (buf[i]>>(8-j))  )>>(32-20) == 0x10 )&&(((state >> (17-j)) & 0x4000) == 0x0)){
                    i++;
                    vop_found=1;
                    break;
                }
            }
            if(vop_found)
                    break;    
        }
    }
    if(vop_found){
        for(; i<buf_size; i++){
            if(avctx->flags & CODEC_FLAG_TRUNCATED)//XXX ffplay workaround, someone a better solution?
                state= (state<<8) | buf[i];
            for(j=0; j<8; j++){
                if(( (  (state<<j)  |  (buf[i]>>(8-j))  )>>(32-20) == 0x10 )&&(((state >> (17-j)) & 0x4000) == 0x0)){
                    pc->frame_start_found=0;
                    pc->state=-1;
                    return i-3;
                }
            }
        }
    }

    pc->frame_start_found= vop_found;
    pc->state= state;
    return END_NOT_FOUND;
}

static int h261_parse(AVCodecParserContext *s,
                      AVCodecContext *avctx,
                      uint8_t **poutbuf, int *poutbuf_size, 
                      const uint8_t *buf, int buf_size)
{
    ParseContext *pc = s->priv_data;
    int next;
    
    next= h261_find_frame_end(pc,avctx, buf, buf_size);
    if (ff_combine_frame(pc, next, (uint8_t **)&buf, &buf_size) < 0) {
        *poutbuf = NULL;
        *poutbuf_size = 0;
        return buf_size;
    }
    *poutbuf = (uint8_t *)buf;
    *poutbuf_size = buf_size;
    return next;
}

/**
 * returns the number of bytes consumed for building the current frame
 */
static int get_consumed_bytes(MpegEncContext *s, int buf_size){
    int pos= (get_bits_count(&s->gb)+7)>>3;

    if(s->flags&CODEC_FLAG_TRUNCATED){
        pos -= s->parse_context.last_index;
        if(pos<0) pos=0;// padding is not really read so this might be -1
        return pos;
    }else{
        if(pos==0) pos=1; //avoid infinite loops (i doubt thats needed but ...)
        if(pos+10>buf_size) pos=buf_size; // oops ;)

        return pos;
    }
}

static int h261_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    H261Context *h= avctx->priv_data;
    MpegEncContext *s = &h->s;
    int ret;
    AVFrame *pict = data;

#ifdef DEBUG
    printf("*****frame %d size=%d\n", avctx->frame_number, buf_size);
    printf("bytes=%x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
#endif
    s->flags= avctx->flags;
    s->flags2= avctx->flags2;

    /* no supplementary picture */
    if (buf_size == 0) {

        return 0;
    }

    if(s->flags&CODEC_FLAG_TRUNCATED){
        int next;

        next= h261_find_frame_end(&s->parse_context,avctx, buf, buf_size);

        if( ff_combine_frame(&s->parse_context, next, &buf, &buf_size) < 0 )
            return buf_size;
    }


retry:

    init_get_bits(&s->gb, buf, buf_size*8);

    if(!s->context_initialized){
        if (MPV_common_init(s) < 0) //we need the idct permutaton for reading a custom matrix
            return -1;
    }

    //we need to set current_picture_ptr before reading the header, otherwise we cant store anyting im there
    if(s->current_picture_ptr==NULL || s->current_picture_ptr->data[0]){
        int i= ff_find_unused_picture(s, 0);
        s->current_picture_ptr= &s->picture[i];
    }

    ret = h261_decode_picture_header(h);

    /* skip if the header was thrashed */
    if (ret < 0){
        av_log(s->avctx, AV_LOG_ERROR, "header damaged\n");
        return -1;
    }

    if (s->width != avctx->width || s->height != avctx->height){
        ParseContext pc= s->parse_context; //FIXME move these demuxng hack to avformat
        s->parse_context.buffer=0;
        MPV_common_end(s);
        s->parse_context= pc;
    }
    if (!s->context_initialized) {
        avctx->width = s->width;
        avctx->height = s->height;

        goto retry;
    }

    // for hurry_up==5
    s->current_picture.pict_type= s->pict_type;
    s->current_picture.key_frame= s->pict_type == I_TYPE;

    /* skip everything if we are in a hurry>=5 */
    if(avctx->hurry_up>=5) return get_consumed_bytes(s, buf_size);

    if(MPV_frame_start(s, avctx) < 0)
        return -1;

    ff_er_frame_start(s);

    /* decode each macroblock */
    s->mb_x=0;
    s->mb_y=0;

    while(h->gob_number < (s->mb_height==18 ? 12 : 5)){
        if(ff_h261_resync(h)<0)
            break;
        h261_decode_gob(h);
    }
    MPV_frame_end(s);

    // h261 doesn't have byte aligned codes
    // store the bits of the next frame that are left in the last byte
    // in the H261Context and remember the number of stored bits
    {
        int bitsleft;
        int current_pos= get_bits_count(&s->gb)>>3;
        bitsleft =  (current_pos<<3) - get_bits_count(&s->gb);
        h->bits_left = - bitsleft;
        if(bitsleft > 0)
            h->last_bits= get_bits(&s->gb, 8 - h->bits_left);
        else
            h->last_bits = 0;
    }

assert(s->current_picture.pict_type == s->current_picture_ptr->pict_type);
assert(s->current_picture.pict_type == s->pict_type);
    *pict= *(AVFrame*)&s->current_picture;
    ff_print_debug_info(s, pict);

    /* Return the Picture timestamp as the frame number */
    /* we substract 1 because it is added on utils.c    */
    avctx->frame_number = s->picture_number - 1;

    *data_size = sizeof(AVFrame);

    return get_consumed_bytes(s, buf_size);
}

static int h261_decode_end(AVCodecContext *avctx)
{
    H261Context *h= avctx->priv_data;
    MpegEncContext *s = &h->s;

    MPV_common_end(s);
    return 0;
}

AVCodec h261_decoder = {
    "h261",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H261,
    sizeof(H261Context),
    h261_decode_init,
    NULL,
    h261_decode_end,
    h261_decode_frame,
    CODEC_CAP_TRUNCATED,
};

AVCodecParser h261_parser = {
    { CODEC_ID_H261 },
    sizeof(ParseContext),
    NULL,
    h261_parse,
    ff_parse_close,
};
