/*
 * XVideo Motion Compensation
 * Copyright (c) 2003 Ivan Kalvachev
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

#include <limits.h>

//avcodec include
#include "avcodec.h"
#include "dsputil.h"
#include "mpegvideo.h"

#undef NDEBUG
#include <assert.h>

#ifdef USE_FASTMEMCPY
#include "fastmemcpy.h"
#endif

#ifdef HAVE_XVMC

//X11 includes are in the xvmc_render.h
//by replacing it with none-X one
//XvMC emulation could be performed

#include "xvmc_render.h"

//#include "xvmc_debug.h"

//set s->block
inline void XVMC_init_block(MpegEncContext *s){
xvmc_render_state_t * render;
    render = (xvmc_render_state_t*)s->current_picture.data[2];
    assert(render != NULL);
    if( (render == NULL) || (render->magic != MP_XVMC_RENDER_MAGIC) ){
        assert(0);
        return;//make sure that this is render packet
    }
    s->block =(DCTELEM *)(render->data_blocks+(render->next_free_data_block_num)*64);
}

void XVMC_pack_pblocks(MpegEncContext *s, int cbp){
int i,j;
const int mb_block_count = 4+(1<<s->chroma_format);

    j=0;
    cbp<<= 12-mb_block_count;
    for(i=0; i<mb_block_count; i++){
        if(cbp & (1<<11)) {
           s->pblocks[i] = (short *)(&s->block[(j++)]);
        }else{
           s->pblocks[i] = NULL;
        }
	cbp+=cbp;
//        printf("s->pblocks[%d]=%p ,s->block=%p cbp=%d\n",i,s->pblocks[i],s->block,cbp);
    }
}

//these functions should be called on every new field or/and frame
//They should be safe if they are called few times for same field!
int XVMC_field_start(MpegEncContext*s, AVCodecContext *avctx){
xvmc_render_state_t * render,* last, * next;

    assert(avctx != NULL);

    render = (xvmc_render_state_t*)s->current_picture.data[2];
    assert(render != NULL);
    if( (render == NULL) || (render->magic != MP_XVMC_RENDER_MAGIC) )
        return -1;//make sure that this is render packet

    render->picture_structure = s->picture_structure;
    render->flags = (s->first_field)? 0: XVMC_SECOND_FIELD;

//make sure that all data is drawn by XVMC_end_frame
    assert(render->filled_mv_blocks_num==0);

    render->p_future_surface = NULL;
    render->p_past_surface = NULL;

    switch(s->pict_type){
        case  I_TYPE:
            return 0;// no prediction from other frames
        case  B_TYPE:
            next = (xvmc_render_state_t*)s->next_picture.data[2];
            assert(next!=NULL);
            assert(next->state & MP_XVMC_STATE_PREDICTION);
            if(next == NULL) return -1;
            if(next->magic != MP_XVMC_RENDER_MAGIC) return -1;
            render->p_future_surface = next->p_surface;
            //no return here, going to set forward prediction
        case  P_TYPE:
            last = (xvmc_render_state_t*)s->last_picture.data[2];
            if(last == NULL)// && !s->first_field)
                last = render;//predict second field from the first
            if(last->magic != MP_XVMC_RENDER_MAGIC) return -1;
            assert(last->state & MP_XVMC_STATE_PREDICTION);
            render->p_past_surface = last->p_surface;
            return 0;
    }

return -1;
}

void XVMC_field_end(MpegEncContext *s){
xvmc_render_state_t * render;
    render = (xvmc_render_state_t*)s->current_picture.data[2];
    assert(render != NULL);

    if(render->filled_mv_blocks_num > 0){
//        printf("xvmcvideo.c: rendering %d left blocks after last slice!!!\n",render->filled_mv_blocks_num );
        ff_draw_horiz_band(s,0,0);
    }
}

void XVMC_decode_mb(MpegEncContext *s){
XvMCMacroBlock * mv_block;
xvmc_render_state_t * render;
int i,cbp,blocks_per_mb;

const int mb_xy = s->mb_y * s->mb_stride + s->mb_x;


    if(s->encoding){
        av_log(s->avctx, AV_LOG_ERROR, "XVMC doesn't support encoding!!!\n");
        return -1;
    }

   //from MPV_decode_mb(),
    /* update DC predictors for P macroblocks */
    if (!s->mb_intra) {
        s->last_dc[0] =
        s->last_dc[1] =
        s->last_dc[2] =  128 << s->intra_dc_precision;
    }

   //MC doesn't skip blocks
    s->mb_skiped = 0;


   // do I need to export quant when I could not perform postprocessing?
   // anyway, it doesn't hurrt
    s->current_picture.qscale_table[mb_xy] = s->qscale;

//START OF XVMC specific code
    render = (xvmc_render_state_t*)s->current_picture.data[2];
    assert(render!=NULL);
    assert(render->magic==MP_XVMC_RENDER_MAGIC);
    assert(render->mv_blocks);

    //take the next free macroblock
    mv_block = &render->mv_blocks[render->start_mv_blocks_num + 
                                   render->filled_mv_blocks_num ];

// memset(mv_block,0,sizeof(XvMCMacroBlock));

    mv_block->x = s->mb_x;
    mv_block->y = s->mb_y;
    mv_block->dct_type = s->interlaced_dct;//XVMC_DCT_TYPE_FRAME/FIELD;
//    mv_block->motion_type = 0;  //zero to silense warnings
    if(s->mb_intra){
        mv_block->macroblock_type = XVMC_MB_TYPE_INTRA;//no MC, all done
    }else{
        mv_block->macroblock_type = XVMC_MB_TYPE_PATTERN;

        if(s->mv_dir & MV_DIR_FORWARD){
            mv_block->macroblock_type|= XVMC_MB_TYPE_MOTION_FORWARD;
            //pmv[n][dir][xy]=mv[dir][n][xy]
            mv_block->PMV[0][0][0] = s->mv[0][0][0];
            mv_block->PMV[0][0][1] = s->mv[0][0][1];
            mv_block->PMV[1][0][0] = s->mv[0][1][0];
            mv_block->PMV[1][0][1] = s->mv[0][1][1];
        }
        if(s->mv_dir & MV_DIR_BACKWARD){
            mv_block->macroblock_type|=XVMC_MB_TYPE_MOTION_BACKWARD;
            mv_block->PMV[0][1][0] = s->mv[1][0][0];
            mv_block->PMV[0][1][1] = s->mv[1][0][1];
            mv_block->PMV[1][1][0] = s->mv[1][1][0];
            mv_block->PMV[1][1][1] = s->mv[1][1][1];
        }

        switch(s->mv_type){
            case  MV_TYPE_16X16:
                mv_block->motion_type = XVMC_PREDICTION_FRAME;
                break;
            case  MV_TYPE_16X8:
                mv_block->motion_type = XVMC_PREDICTION_16x8;
                break;
            case  MV_TYPE_FIELD:
                mv_block->motion_type = XVMC_PREDICTION_FIELD;
                if(s->picture_structure == PICT_FRAME){
                    mv_block->PMV[0][0][1]<<=1;
                    mv_block->PMV[1][0][1]<<=1;
                    mv_block->PMV[0][1][1]<<=1;
                    mv_block->PMV[1][1][1]<<=1;
                }
                break;
            case  MV_TYPE_DMV:
                mv_block->motion_type = XVMC_PREDICTION_DUAL_PRIME;
                if(s->picture_structure == PICT_FRAME){

                    mv_block->PMV[0][0][0] = s->mv[0][0][0];//top from top
                    mv_block->PMV[0][0][1] = s->mv[0][0][1]<<1;

                    mv_block->PMV[0][1][0] = s->mv[0][0][0];//bottom from bottom
                    mv_block->PMV[0][1][1] = s->mv[0][0][1]<<1;

                    mv_block->PMV[1][0][0] = s->mv[0][2][0];//dmv00, top from bottom
                    mv_block->PMV[1][0][1] = s->mv[0][2][1]<<1;//dmv01

                    mv_block->PMV[1][1][0] = s->mv[0][3][0];//dmv10, bottom from top
                    mv_block->PMV[1][1][1] = s->mv[0][3][1]<<1;//dmv11

                }else{
                    mv_block->PMV[0][1][0] = s->mv[0][2][0];//dmv00
                    mv_block->PMV[0][1][1] = s->mv[0][2][1];//dmv01
                }
                break;
            default:
                assert(0);
        }

        mv_block->motion_vertical_field_select = 0;

//set correct field referenses
        if(s->mv_type == MV_TYPE_FIELD || s->mv_type == MV_TYPE_16X8){
            if( s->field_select[0][0] ) mv_block->motion_vertical_field_select|=1;
            if( s->field_select[1][0] ) mv_block->motion_vertical_field_select|=2;
            if( s->field_select[0][1] ) mv_block->motion_vertical_field_select|=4;
            if( s->field_select[1][1] ) mv_block->motion_vertical_field_select|=8;
        }
    }//!intra
//time to handle data blocks;
    mv_block->index = render->next_free_data_block_num;

    blocks_per_mb = 6;
    if( s->chroma_format >= 2){
        blocks_per_mb = 4 + (1 << (s->chroma_format));
    }

//  calculate cbp
    cbp = 0;
    for(i=0; i<blocks_per_mb; i++) {
        cbp+= cbp;
        if(s->block_last_index[i] >= 0)
            cbp++;
    }
    
    if(s->flags & CODEC_FLAG_GRAY){
        if(s->mb_intra){//intra frames are alwasy full chroma block
            for(i=4; i<blocks_per_mb; i++){
                memset(s->pblocks[i],0,sizeof(short)*8*8);//so we need to clear them
                if(!render->unsigned_intra)
                    s->pblocks[i][0] = 1<<10;
            }
        }else{
            cbp&= 0xf << (blocks_per_mb - 4);
            blocks_per_mb = 4;//Luminance blocks only
        }
    }
    mv_block->coded_block_pattern = cbp;
    if(cbp == 0)
        mv_block->macroblock_type &= ~XVMC_MB_TYPE_PATTERN;

    for(i=0; i<blocks_per_mb; i++){
        if(s->block_last_index[i] >= 0){
            // i do not have unsigned_intra MOCO to test, hope it is OK
            if( (s->mb_intra) && ( render->idct || (!render->idct && !render->unsigned_intra)) )
                s->pblocks[i][0]-=1<<10;
            if(!render->idct){
                s->dsp.idct(s->pblocks[i]);
                //!!TODO!clip!!!
            }
//copy blocks only if the codec doesn't support pblocks reordering
            if(s->avctx->xvmc_acceleration == 1){
                memcpy(&render->data_blocks[(render->next_free_data_block_num)*64],
                        s->pblocks[i],sizeof(short)*8*8);
            }else{
/*              if(s->pblocks[i] != &render->data_blocks[
                        (render->next_free_data_block_num)*64]){
                   printf("ERROR mb(%d,%d) s->pblocks[i]=%p data_block[]=%p\n",
                   s->mb_x,s->mb_y, s->pblocks[i], 
                   &render->data_blocks[(render->next_free_data_block_num)*64]);
                }*/
            }
            render->next_free_data_block_num++;
        }
    }
    render->filled_mv_blocks_num++;

    assert(render->filled_mv_blocks_num <= render->total_number_of_mv_blocks);
    assert(render->next_free_data_block_num <= render->total_number_of_data_blocks);


    if(render->filled_mv_blocks_num >= render->total_number_of_mv_blocks)
        ff_draw_horiz_band(s,0,0);

// DumpRenderInfo(render);
// DumpMBlockInfo(mv_block);

}

#endif
