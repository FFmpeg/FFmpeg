/*
 * H261 decoder
 * Copyright (c) 2002-2004 Michael Niedermayer <michaelni@gmx.at>
 * Copyright (c) 2004 Maarten Daniels
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
 * @file libavcodec/h261dec.c
 * H.261 decoder.
 */

#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h261.h"
#include "h261data.h"

#define H261_MBA_VLC_BITS 9
#define H261_MTYPE_VLC_BITS 6
#define H261_MV_VLC_BITS 7
#define H261_CBP_VLC_BITS 9
#define TCOEFF_VLC_BITS 9
#define MBA_STUFFING 33
#define MBA_STARTCODE 34

extern uint8_t ff_h261_rl_table_store[2][2*MAX_RUN + MAX_LEVEL + 3];

static VLC h261_mba_vlc;
static VLC h261_mtype_vlc;
static VLC h261_mv_vlc;
static VLC h261_cbp_vlc;

static int h261_decode_block(H261Context * h, DCTELEM * block, int n, int coded);

static av_cold void h261_decode_init_vlc(H261Context *h){
    static int done = 0;

    if(!done){
        done = 1;
        init_vlc(&h261_mba_vlc, H261_MBA_VLC_BITS, 35,
                 h261_mba_bits, 1, 1,
                 h261_mba_code, 1, 1, 1);
        init_vlc(&h261_mtype_vlc, H261_MTYPE_VLC_BITS, 10,
                 h261_mtype_bits, 1, 1,
                 h261_mtype_code, 1, 1, 1);
        init_vlc(&h261_mv_vlc, H261_MV_VLC_BITS, 17,
                 &h261_mv_tab[0][1], 2, 1,
                 &h261_mv_tab[0][0], 2, 1, 1);
        init_vlc(&h261_cbp_vlc, H261_CBP_VLC_BITS, 63,
                 &h261_cbp_tab[0][1], 2, 1,
                 &h261_cbp_tab[0][0], 2, 1, 1);
        init_rl(&h261_rl_tcoeff, ff_h261_rl_table_store);
        INIT_VLC_RL(h261_rl_tcoeff, 552);
    }
}

static av_cold int h261_decode_init(AVCodecContext *avctx){
    H261Context *h= avctx->priv_data;
    MpegEncContext * const s = &h->s;

    // set defaults
    MPV_decode_defaults(s);
    s->avctx = avctx;

    s->width  = s->avctx->coded_width;
    s->height = s->avctx->coded_height;
    s->codec_id = s->avctx->codec->id;

    s->out_format = FMT_H261;
    s->low_delay= 1;
    avctx->pix_fmt= PIX_FMT_YUV420P;

    s->codec_id= avctx->codec->id;

    h261_decode_init_vlc(h);

    h->gob_start_code_skipped = 0;

    return 0;
}

/**
 * decodes the group of blocks header or slice header.
 * @return <0 if an error occurred
 */
static int h261_decode_gob_header(H261Context *h){
    unsigned int val;
    MpegEncContext * const s = &h->s;

    if ( !h->gob_start_code_skipped ){
        /* Check for GOB Start Code */
        val = show_bits(&s->gb, 15);
        if(val)
            return -1;

        /* We have a GBSC */
        skip_bits(&s->gb, 16);
    }

    h->gob_start_code_skipped = 0;

    h->gob_number = get_bits(&s->gb, 4); /* GN */
    s->qscale = get_bits(&s->gb, 5); /* GQUANT */

    /* Check if gob_number is valid */
    if (s->mb_height==18){ //cif
        if ((h->gob_number<=0) || (h->gob_number>12))
            return -1;
    }
    else{ //qcif
        if ((h->gob_number!=1) && (h->gob_number!=3) && (h->gob_number!=5))
            return -1;
    }

    /* GEI */
    while (get_bits1(&s->gb) != 0) {
        skip_bits(&s->gb, 8);
    }

    if(s->qscale==0) {
        av_log(s->avctx, AV_LOG_ERROR, "qscale has forbidden 0 value\n");
        if (s->avctx->error_recognition >= FF_ER_COMPLIANT)
            return -1;
    }

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

    if ( h->gob_start_code_skipped ){
        ret= h261_decode_gob_header(h);
        if(ret>=0)
            return 0;
    }
    else{
        if(show_bits(&s->gb, 15)==0){
            ret= h261_decode_gob_header(h);
            if(ret>=0)
                return 0;
        }
        //OK, it is not where it is supposed to be ...
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

        for(j=0;j<6;j++)
            s->block_last_index[j] = -1;

        s->mv_dir = MV_DIR_FORWARD;
        s->mv_type = MV_TYPE_16X16;
        s->current_picture.mb_type[xy]= MB_TYPE_SKIP | MB_TYPE_16x16 | MB_TYPE_L0;
        s->mv[0][0][0] = 0;
        s->mv[0][0][1] = 0;
        s->mb_skipped = 1;
        h->mtype &= ~MB_TYPE_H261_FIL;

        MPV_decode_mb(s, s->block);
    }

    return 0;
}

static int decode_mv_component(GetBitContext *gb, int v){
    int mv_diff = get_vlc2(gb, h261_mv_vlc.table, H261_MV_VLC_BITS, 2);

    /* check if mv_diff is valid */
    if ( mv_diff < 0 )
        return v;

    mv_diff = mvmap[mv_diff];

    if(mv_diff && !get_bits1(gb))
        mv_diff= -mv_diff;

    v += mv_diff;
    if     (v <=-16) v+= 32;
    else if(v >= 16) v-= 32;

    return v;
}

static int h261_decode_mb(H261Context *h){
    MpegEncContext * const s = &h->s;
    int i, cbp, xy;

    cbp = 63;
    // Read mba
    do{
        h->mba_diff = get_vlc2(&s->gb, h261_mba_vlc.table, H261_MBA_VLC_BITS, 2);

        /* Check for slice end */
        /* NOTE: GOB can be empty (no MB data) or exist only of MBA_stuffing */
        if (h->mba_diff == MBA_STARTCODE){ // start code
            h->gob_start_code_skipped = 1;
            return SLICE_END;
        }
    }
    while( h->mba_diff == MBA_STUFFING ); // stuffing

    if ( h->mba_diff < 0 ){
        if ( get_bits_count(&s->gb) + 7 >= s->gb.size_in_bits )
            return SLICE_END;

        av_log(s->avctx, AV_LOG_ERROR, "illegal mba at %d %d\n", s->mb_x, s->mb_y);
        return SLICE_ERROR;
    }

    h->mba_diff += 1;
    h->current_mba += h->mba_diff;

    if ( h->current_mba > MBA_STUFFING )
        return SLICE_ERROR;

    s->mb_x= ((h->gob_number-1) % 2) * 11 + ((h->current_mba-1) % 11);
    s->mb_y= ((h->gob_number-1) / 2) * 3 + ((h->current_mba-1) / 11);
    xy = s->mb_x + s->mb_y * s->mb_stride;
    ff_init_block_index(s);
    ff_update_block_index(s);

    // Read mtype
    h->mtype = get_vlc2(&s->gb, h261_mtype_vlc.table, H261_MTYPE_VLC_BITS, 2);
    h->mtype = h261_mtype_map[h->mtype];

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
             ( h->mba_diff != 1))
        {
            h->current_mv_x = 0;
            h->current_mv_y = 0;
        }

        h->current_mv_x= decode_mv_component(&s->gb, h->current_mv_x);
        h->current_mv_y= decode_mv_component(&s->gb, h->current_mv_y);
    }else{
        h->current_mv_x = 0;
        h->current_mv_y = 0;
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
    s->mv[0][0][0] = h->current_mv_x * 2;//gets divided by 2 in motion compensation
    s->mv[0][0][1] = h->current_mv_y * 2;

intra:
    /* decode each block */
    if(s->mb_intra || HAS_CBP(h->mtype)){
        s->dsp.clear_blocks(s->block[0]);
        for (i = 0; i < 6; i++) {
            if (h261_decode_block(h, s->block[i], i, cbp&32) < 0){
                return SLICE_ERROR;
            }
            cbp+=cbp;
        }
    }else{
        for (i = 0; i < 6; i++)
            s->block_last_index[i]= -1;
    }

    MPV_decode_mb(s, s->block);

    return SLICE_OK;
}

/**
 * decodes a macroblock
 * @return <0 if an error occurred
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
            level = get_sbits(&s->gb, 8);
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
static int h261_decode_picture_header(H261Context *h){
    MpegEncContext * const s = &h->s;
    int format, i;
    uint32_t startcode= 0;

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
    i= get_bits(&s->gb, 5); /* picture timestamp */
    if(i < (s->picture_number&31))
        i += 32;
    s->picture_number = (s->picture_number&~31) + i;

    s->avctx->time_base= (AVRational){1001, 30000};
    s->current_picture.pts= s->picture_number;


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

    // h261 has no I-FRAMES, but if we pass FF_I_TYPE for the first frame, the codec crashes if it does
    // not contain all I-blocks (e.g. when a packet is lost)
    s->pict_type = FF_P_TYPE;

    h->gob_number = 0;
    return 0;
}

static int h261_decode_gob(H261Context *h){
    MpegEncContext * const s = &h->s;

    ff_set_qscale(s, s->qscale);

    /* decode mb's */
    while(h->current_mba <= MBA_STUFFING)
    {
        int ret;
        /* DCT & quantize */
        ret= h261_decode_mb(h);
        if(ret<0){
            if(ret==SLICE_END){
                h261_decode_mb_skipped(h, h->current_mba, 33);
                return 0;
            }
            av_log(s->avctx, AV_LOG_ERROR, "Error at MB: %d\n", s->mb_x + s->mb_y*s->mb_stride);
            return -1;
        }

        h261_decode_mb_skipped(h, h->current_mba-h->mba_diff, h->current_mba-1);
    }

    return -1;
}

/**
 * returns the number of bytes consumed for building the current frame
 */
static int get_consumed_bytes(MpegEncContext *s, int buf_size){
    int pos= get_bits_count(&s->gb)>>3;
    if(pos==0) pos=1; //avoid infinite loops (i doubt that is needed but ...)
    if(pos+10>buf_size) pos=buf_size; // oops ;)

    return pos;
}

static int h261_decode_frame(AVCodecContext *avctx,
                             void *data, int *data_size,
                             const uint8_t *buf, int buf_size)
{
    H261Context *h= avctx->priv_data;
    MpegEncContext *s = &h->s;
    int ret;
    AVFrame *pict = data;

#ifdef DEBUG
    av_log(avctx, AV_LOG_DEBUG, "*****frame %d size=%d\n", avctx->frame_number, buf_size);
    av_log(avctx, AV_LOG_DEBUG, "bytes=%x %x %x %x\n", buf[0], buf[1], buf[2], buf[3]);
#endif
    s->flags= avctx->flags;
    s->flags2= avctx->flags2;

    h->gob_start_code_skipped=0;

retry:

    init_get_bits(&s->gb, buf, buf_size*8);

    if(!s->context_initialized){
        if (MPV_common_init(s) < 0) //we need the idct permutaton for reading a custom matrix
            return -1;
    }

    //we need to set current_picture_ptr before reading the header, otherwise we cannot store anyting im there
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

    if (s->width != avctx->coded_width || s->height != avctx->coded_height){
        ParseContext pc= s->parse_context; //FIXME move this demuxing hack to libavformat
        s->parse_context.buffer=0;
        MPV_common_end(s);
        s->parse_context= pc;
    }
    if (!s->context_initialized) {
        avcodec_set_dimensions(avctx, s->width, s->height);

        goto retry;
    }

    // for hurry_up==5
    s->current_picture.pict_type= s->pict_type;
    s->current_picture.key_frame= s->pict_type == FF_I_TYPE;

    /* skip everything if we are in a hurry>=5 */
    if(avctx->hurry_up>=5) return get_consumed_bytes(s, buf_size);
    if(  (avctx->skip_frame >= AVDISCARD_NONREF && s->pict_type==FF_B_TYPE)
       ||(avctx->skip_frame >= AVDISCARD_NONKEY && s->pict_type!=FF_I_TYPE)
       || avctx->skip_frame >= AVDISCARD_ALL)
        return get_consumed_bytes(s, buf_size);

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

assert(s->current_picture.pict_type == s->current_picture_ptr->pict_type);
assert(s->current_picture.pict_type == s->pict_type);
    *pict= *(AVFrame*)s->current_picture_ptr;
    ff_print_debug_info(s, pict);

    *data_size = sizeof(AVFrame);

    return get_consumed_bytes(s, buf_size);
}

static av_cold int h261_decode_end(AVCodecContext *avctx)
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
    CODEC_CAP_DR1,
    .long_name = NULL_IF_CONFIG_SMALL("H.261"),
};
