/*
 * H.26L/H.264/AVC/JVT/14496-10/... encoder/decoder
 * Copyright (c) 2003 Michael Niedermayer <michaelni@gmx.at>
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
 */
 
/**
 * @file h264.c
 * H.264 / AVC / MPEG4 part10 codec.
 * @author Michael Niedermayer <michaelni@gmx.at>
 */

#include "common.h"
#include "dsputil.h"
#include "avcodec.h"
#include "mpegvideo.h"
#include "h264data.h"
#include "golomb.h"

#undef NDEBUG
#include <assert.h>

#define interlaced_dct interlaced_dct_is_a_bad_name
#define mb_intra mb_intra_isnt_initalized_see_mb_type

#define LUMA_DC_BLOCK_INDEX   25
#define CHROMA_DC_BLOCK_INDEX 26

#define CHROMA_DC_COEFF_TOKEN_VLC_BITS 8
#define COEFF_TOKEN_VLC_BITS           8
#define TOTAL_ZEROS_VLC_BITS           9
#define CHROMA_DC_TOTAL_ZEROS_VLC_BITS 3
#define RUN_VLC_BITS                   3
#define RUN7_VLC_BITS                  6

#define MAX_SPS_COUNT 32
#define MAX_PPS_COUNT 256

#define MAX_MMCO_COUNT 66

/**
 * Sequence parameter set
 */
typedef struct SPS{
    
    int profile_idc;
    int level_idc;
    int multiple_slice_groups;         ///< more_than_one_slice_group_allowed_flag
    int arbitrary_slice_order;         ///< arbitrary_slice_order_allowed_flag
    int redundant_slices;              ///< redundant_slices_allowed_flag
    int log2_max_frame_num;            ///< log2_max_frame_num_minus4 + 4
    int poc_type;                      ///< pic_order_cnt_type
    int log2_max_poc_lsb;              ///< log2_max_pic_order_cnt_lsb_minus4
    int delta_pic_order_always_zero_flag;
    int offset_for_non_ref_pic;
    int offset_for_top_to_bottom_field;
    int poc_cycle_length;              ///< num_ref_frames_in_pic_order_cnt_cycle
    int ref_frame_count;               ///< num_ref_frames
    int required_frame_num_update_behaviour_flag;
    int mb_width;                      ///< frame_width_in_mbs_minus1 + 1
    int mb_height;                     ///< frame_height_in_mbs_minus1 + 1
    int frame_mbs_only_flag;
    int mb_aff;                        ///<mb_adaptive_frame_field_flag
    int direct_8x8_inference_flag;
    int vui_parameters_present_flag;
    int sar_width;
    int sar_height;
    short offset_for_ref_frame[256]; //FIXME dyn aloc?
}SPS;

/**
 * Picture parameter set
 */
typedef struct PPS{
    int sps_id;
    int cabac;                  ///< entropy_coding_mode_flag
    int pic_order_present;      ///< pic_order_present_flag
    int slice_group_count;      ///< num_slice_groups_minus1 + 1
    int mb_slice_group_map_type;
    int ref_count[2];           ///< num_ref_idx_l0/1_active_minus1 + 1
    int weighted_pred;          ///< weighted_pred_flag
    int weighted_bipred_idc;
    int init_qp;                ///< pic_init_qp_minus26 + 26
    int init_qs;                ///< pic_init_qs_minus26 + 26
    int chroma_qp_index_offset;
    int deblocking_filter_parameters_present; ///< deblocking_filter_parameters_present_flag
    int constrained_intra_pred; ///< constrained_intra_pred_flag
    int redundant_pic_cnt_present; ///< redundant_pic_cnt_present_flag
    int crop;                   ///< frame_cropping_flag
    int crop_left;              ///< frame_cropping_rect_left_offset
    int crop_right;             ///< frame_cropping_rect_right_offset
    int crop_top;               ///< frame_cropping_rect_top_offset
    int crop_bottom;            ///< frame_cropping_rect_bottom_offset
}PPS;

/**
 * Memory management control operation opcode.
 */
typedef enum MMCOOpcode{
    MMCO_END=0,
    MMCO_SHORT2UNUSED,
    MMCO_LONG2UNUSED,
    MMCO_SHORT2LONG,
    MMCO_SET_MAX_LONG,
    MMCO_RESET, 
    MMCO_LONG,
} MMCOOpcode;

/**
 * Memory management control operation.
 */
typedef struct MMCO{
    MMCOOpcode opcode;
    int short_frame_num;
    int long_index;
} MMCO;

/**
 * H264Context
 */
typedef struct H264Context{
    MpegEncContext s;
    int nal_ref_idc;	
    int nal_unit_type;
#define NAL_SLICE		1
#define NAL_DPA			2
#define NAL_DPB			3
#define NAL_DPC			4
#define NAL_IDR_SLICE		5
#define NAL_SEI			6
#define NAL_SPS			7
#define NAL_PPS			8
#define NAL_PICTURE_DELIMITER	9
#define NAL_FILTER_DATA		10
    uint8_t *rbsp_buffer;
    int rbsp_buffer_size;

    int chroma_qp; //QPc

    int prev_mb_skiped; //FIXME remove (IMHO not used)

    //prediction stuff
    int chroma_pred_mode;
    int intra16x16_pred_mode;
    
    int8_t intra4x4_pred_mode_cache[5*8];
    int8_t (*intra4x4_pred_mode)[8];
    void (*pred4x4  [9+3])(uint8_t *src, uint8_t *topright, int stride);//FIXME move to dsp?
    void (*pred8x8  [4+3])(uint8_t *src, int stride);
    void (*pred16x16[4+3])(uint8_t *src, int stride);
    unsigned int topleft_samples_available;
    unsigned int top_samples_available;
    unsigned int topright_samples_available;
    unsigned int left_samples_available;

    /**
     * non zero coeff count cache.
     * is 64 if not available.
     */
    uint8_t non_zero_count_cache[6*8];
    uint8_t (*non_zero_count)[16];

    /**
     * Motion vector cache.
     */
    int16_t mv_cache[2][5*8][2];
    int8_t ref_cache[2][5*8];
#define LIST_NOT_USED -1 //FIXME rename?
#define PART_NOT_AVAILABLE -2
    
    /**
     * is 1 if the specific list MV&references are set to 0,0,-2.
     */
    int mv_cache_clean[2];

    int block_offset[16+8];
    int chroma_subblock_offset[16]; //FIXME remove
    
    uint16_t *mb2b_xy; //FIXME are these 4 a good idea?
    uint16_t *mb2b8_xy;
    int b_stride;
    int b8_stride;

    int halfpel_flag;
    int thirdpel_flag;

    SPS sps_buffer[MAX_SPS_COUNT];
    SPS sps; ///< current sps
    
    PPS pps_buffer[MAX_PPS_COUNT];
    /**
     * current pps
     */
    PPS pps; //FIXME move tp Picture perhaps? (->no) do we need that?

    int slice_num;
    uint8_t *slice_table_base;
    uint8_t *slice_table;      ///< slice_table_base + mb_stride + 1
    int slice_type;
    int slice_type_fixed;
    
    //interlacing specific flags
    int mb_field_decoding_flag;
    
    int sub_mb_type[4];
    
    //POC stuff
    int poc_lsb;
    int poc_msb;
    int delta_poc_bottom;
    int delta_poc[2];
    int frame_num;
    int prev_poc_msb;             ///< poc_msb of the last reference pic for POC type 0
    int prev_poc_lsb;             ///< poc_lsb of the last reference pic for POC type 0
    int frame_num_offset;         ///< for POC type 2
    int prev_frame_num_offset;    ///< for POC type 2
    int prev_frame_num;           ///< frame_num of the last pic for POC type 1/2

    /**
     * frame_num for frames or 2*frame_num for field pics.
     */
    int curr_pic_num;
    
    /**
     * max_frame_num or 2*max_frame_num for field pics.
     */
    int max_pic_num;

    //Weighted pred stuff
    int luma_log2_weight_denom;
    int chroma_log2_weight_denom;
    int luma_weight[2][16];
    int luma_offset[2][16];
    int chroma_weight[2][16][2];
    int chroma_offset[2][16][2];
   
    //deblock
    int disable_deblocking_filter_idc;
    int slice_alpha_c0_offset_div2;
    int slice_beta_offset_div2;
     
    int redundant_pic_count;
    
    int direct_spatial_mv_pred;

    /**
     * num_ref_idx_l0/1_active_minus1 + 1
     */
    int ref_count[2];// FIXME split for AFF
    Picture *short_ref[16];
    Picture *long_ref[16];
    Picture default_ref_list[2][32];
    Picture ref_list[2][32]; //FIXME size?
    Picture field_ref_list[2][32]; //FIXME size?
    
    /**
     * memory management control operations buffer.
     */
    MMCO mmco[MAX_MMCO_COUNT];
    int mmco_index;
    
    int long_ref_count;  ///< number of actual long term references
    int short_ref_count; ///< number of actual short term references
    
    //data partitioning
    GetBitContext intra_gb;
    GetBitContext inter_gb;
    GetBitContext *intra_gb_ptr;
    GetBitContext *inter_gb_ptr;
    
    DCTELEM mb[16*24] __align8;
}H264Context;

static VLC coeff_token_vlc[4];
static VLC chroma_dc_coeff_token_vlc;

static VLC total_zeros_vlc[15];
static VLC chroma_dc_total_zeros_vlc[3];

static VLC run_vlc[6];
static VLC run7_vlc;

static void svq3_luma_dc_dequant_idct_c(DCTELEM *block, int qp);
static void svq3_add_idct_c(uint8_t *dst, DCTELEM *block, int stride, int qp, int dc);

/**
 * fill a rectangle.
 * @param h height of the recatangle, should be a constant
 * @param w width of the recatangle, should be a constant
 * @param size the size of val (1 or 4), should be a constant
 */
static inline void fill_rectangle(void *vp, int w, int h, int stride, uint32_t val, int size){ //FIXME ensure this IS inlined
    uint8_t *p= (uint8_t*)vp;
    assert(size==1 || size==4);
    
    w      *= size;
    stride *= size;
    
//FIXME check what gcc generates for 64 bit on x86 and possible write a 32 bit ver of it
    if(w==2 && h==2){
        *(uint16_t*)(p + 0)=
        *(uint16_t*)(p + stride)= size==4 ? val : val*0x0101;
    }else if(w==2 && h==4){
        *(uint16_t*)(p + 0*stride)=
        *(uint16_t*)(p + 1*stride)=
        *(uint16_t*)(p + 2*stride)=
        *(uint16_t*)(p + 3*stride)= size==4 ? val : val*0x0101;
    }else if(w==4 && h==2){
        *(uint32_t*)(p + 0*stride)=
        *(uint32_t*)(p + 1*stride)= size==4 ? val : val*0x01010101;
    }else if(w==4 && h==4){
        *(uint32_t*)(p + 0*stride)=
        *(uint32_t*)(p + 1*stride)=
        *(uint32_t*)(p + 2*stride)=
        *(uint32_t*)(p + 3*stride)= size==4 ? val : val*0x01010101;
    }else if(w==8 && h==1){
        *(uint32_t*)(p + 0)=
        *(uint32_t*)(p + 4)= size==4 ? val : val*0x01010101;
    }else if(w==8 && h==2){
        *(uint32_t*)(p + 0 + 0*stride)=
        *(uint32_t*)(p + 4 + 0*stride)=
        *(uint32_t*)(p + 0 + 1*stride)=
        *(uint32_t*)(p + 4 + 1*stride)=  size==4 ? val : val*0x01010101;
    }else if(w==8 && h==4){
        *(uint64_t*)(p + 0*stride)=
        *(uint64_t*)(p + 1*stride)=
        *(uint64_t*)(p + 2*stride)=
        *(uint64_t*)(p + 3*stride)= size==4 ? val*0x0100000001ULL : val*0x0101010101010101ULL;
    }else if(w==16 && h==2){
        *(uint64_t*)(p + 0+0*stride)=
        *(uint64_t*)(p + 8+0*stride)=
        *(uint64_t*)(p + 0+1*stride)=
        *(uint64_t*)(p + 8+1*stride)= size==4 ? val*0x0100000001ULL : val*0x0101010101010101ULL;
    }else if(w==16 && h==4){
        *(uint64_t*)(p + 0+0*stride)=
        *(uint64_t*)(p + 8+0*stride)=
        *(uint64_t*)(p + 0+1*stride)=
        *(uint64_t*)(p + 8+1*stride)=
        *(uint64_t*)(p + 0+2*stride)=
        *(uint64_t*)(p + 8+2*stride)=
        *(uint64_t*)(p + 0+3*stride)=
        *(uint64_t*)(p + 8+3*stride)= size==4 ? val*0x0100000001ULL : val*0x0101010101010101ULL;
    }else
        assert(0);
}

static inline void fill_caches(H264Context *h, int mb_type){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
    int topleft_xy, top_xy, topright_xy, left_xy[2];
    int topleft_type, top_type, topright_type, left_type[2];
    int left_block[4];
    int i;

    //wow what a mess, why didnt they simplify the interlacing&intra stuff, i cant imagine that these complex rules are worth it 
    
    if(h->sps.mb_aff){
    //FIXME
    }else{
        topleft_xy = mb_xy-1 - s->mb_stride;
        top_xy     = mb_xy   - s->mb_stride;
        topright_xy= mb_xy+1 - s->mb_stride;
        left_xy[0]   = mb_xy-1;
        left_xy[1]   = mb_xy-1;
        left_block[0]= 0;
        left_block[1]= 1;
        left_block[2]= 2;
        left_block[3]= 3;
    }

    topleft_type = h->slice_table[topleft_xy ] == h->slice_num ? s->current_picture.mb_type[topleft_xy] : 0;
    top_type     = h->slice_table[top_xy     ] == h->slice_num ? s->current_picture.mb_type[top_xy]     : 0;
    topright_type= h->slice_table[topright_xy] == h->slice_num ? s->current_picture.mb_type[topright_xy]: 0;
    left_type[0] = h->slice_table[left_xy[0] ] == h->slice_num ? s->current_picture.mb_type[left_xy[0]] : 0;
    left_type[1] = h->slice_table[left_xy[1] ] == h->slice_num ? s->current_picture.mb_type[left_xy[1]] : 0;

    if(IS_INTRA(mb_type)){
        h->topleft_samples_available= 
        h->top_samples_available= 
        h->left_samples_available= 0xFFFF;
        h->topright_samples_available= 0xEEEA;

        if(!IS_INTRA(top_type) && (top_type==0 || h->pps.constrained_intra_pred)){
            h->topleft_samples_available= 0xB3FF;
            h->top_samples_available= 0x33FF;
            h->topright_samples_available= 0x26EA;
        }
        for(i=0; i<2; i++){
            if(!IS_INTRA(left_type[i]) && (left_type[i]==0 || h->pps.constrained_intra_pred)){
                h->topleft_samples_available&= 0xDF5F;
                h->left_samples_available&= 0x5F5F;
            }
        }
        
        if(!IS_INTRA(topleft_type) && (topleft_type==0 || h->pps.constrained_intra_pred))
            h->topleft_samples_available&= 0x7FFF;
        
        if(!IS_INTRA(topright_type) && (topright_type==0 || h->pps.constrained_intra_pred))
            h->topright_samples_available&= 0xFBFF;
    
        if(IS_INTRA4x4(mb_type)){
            if(IS_INTRA4x4(top_type)){
                h->intra4x4_pred_mode_cache[4+8*0]= h->intra4x4_pred_mode[top_xy][4];
                h->intra4x4_pred_mode_cache[5+8*0]= h->intra4x4_pred_mode[top_xy][5];
                h->intra4x4_pred_mode_cache[6+8*0]= h->intra4x4_pred_mode[top_xy][6];
                h->intra4x4_pred_mode_cache[7+8*0]= h->intra4x4_pred_mode[top_xy][3];
            }else{
                int pred;
                if(IS_INTRA16x16(top_type) || (IS_INTER(top_type) && !h->pps.constrained_intra_pred))
                    pred= 2;
                else{
                    pred= -1;
                }
                h->intra4x4_pred_mode_cache[4+8*0]=
                h->intra4x4_pred_mode_cache[5+8*0]=
                h->intra4x4_pred_mode_cache[6+8*0]=
                h->intra4x4_pred_mode_cache[7+8*0]= pred;
            }
            for(i=0; i<2; i++){
                if(IS_INTRA4x4(left_type[i])){
                    h->intra4x4_pred_mode_cache[3+8*1 + 2*8*i]= h->intra4x4_pred_mode[left_xy[i]][left_block[0+2*i]];
                    h->intra4x4_pred_mode_cache[3+8*2 + 2*8*i]= h->intra4x4_pred_mode[left_xy[i]][left_block[1+2*i]];
                }else{
                    int pred;
                    if(IS_INTRA16x16(left_type[i]) || (IS_INTER(left_type[i]) && !h->pps.constrained_intra_pred))
                        pred= 2;
                    else{
                        pred= -1;
                    }
                    h->intra4x4_pred_mode_cache[3+8*1 + 2*8*i]=
                    h->intra4x4_pred_mode_cache[3+8*2 + 2*8*i]= pred;
                }
            }
        }
    }
    
    
/*
0 . T T. T T T T 
1 L . .L . . . . 
2 L . .L . . . . 
3 . T TL . . . . 
4 L . .L . . . . 
5 L . .. . . . . 
*/
//FIXME constraint_intra_pred & partitioning & nnz (lets hope this is just a typo in the spec)
    if(top_type){
        h->non_zero_count_cache[4+8*0]= h->non_zero_count[top_xy][0];
        h->non_zero_count_cache[5+8*0]= h->non_zero_count[top_xy][1];
        h->non_zero_count_cache[6+8*0]= h->non_zero_count[top_xy][2];
        h->non_zero_count_cache[7+8*0]= h->non_zero_count[top_xy][3];
    
        h->non_zero_count_cache[1+8*0]= h->non_zero_count[top_xy][7];
        h->non_zero_count_cache[2+8*0]= h->non_zero_count[top_xy][8];
    
        h->non_zero_count_cache[1+8*3]= h->non_zero_count[top_xy][10];
        h->non_zero_count_cache[2+8*3]= h->non_zero_count[top_xy][11];
    }else{
        h->non_zero_count_cache[4+8*0]=      
        h->non_zero_count_cache[5+8*0]=
        h->non_zero_count_cache[6+8*0]=
        h->non_zero_count_cache[7+8*0]=
    
        h->non_zero_count_cache[1+8*0]=
        h->non_zero_count_cache[2+8*0]=
    
        h->non_zero_count_cache[1+8*3]=
        h->non_zero_count_cache[2+8*3]= 64;
    }
    
    if(left_type[0]){
        h->non_zero_count_cache[3+8*1]= h->non_zero_count[left_xy[0]][6];
        h->non_zero_count_cache[3+8*2]= h->non_zero_count[left_xy[0]][5];
        h->non_zero_count_cache[0+8*1]= h->non_zero_count[left_xy[0]][9]; //FIXME left_block
        h->non_zero_count_cache[0+8*4]= h->non_zero_count[left_xy[0]][12];
    }else{
        h->non_zero_count_cache[3+8*1]= 
        h->non_zero_count_cache[3+8*2]= 
        h->non_zero_count_cache[0+8*1]= 
        h->non_zero_count_cache[0+8*4]= 64;
    }
    
    if(left_type[1]){
        h->non_zero_count_cache[3+8*3]= h->non_zero_count[left_xy[1]][4];
        h->non_zero_count_cache[3+8*4]= h->non_zero_count[left_xy[1]][3];
        h->non_zero_count_cache[0+8*2]= h->non_zero_count[left_xy[1]][8];
        h->non_zero_count_cache[0+8*5]= h->non_zero_count[left_xy[1]][11];
    }else{
        h->non_zero_count_cache[3+8*3]= 
        h->non_zero_count_cache[3+8*4]= 
        h->non_zero_count_cache[0+8*2]= 
        h->non_zero_count_cache[0+8*5]= 64;
    }
    
#if 1
    if(IS_INTER(mb_type)){
        int list;
        for(list=0; list<2; list++){
            if((!IS_8X8(mb_type)) && !USES_LIST(mb_type, list)){
                /*if(!h->mv_cache_clean[list]){
                    memset(h->mv_cache [list],  0, 8*5*2*sizeof(int16_t)); //FIXME clean only input? clean at all?
                    memset(h->ref_cache[list], PART_NOT_AVAILABLE, 8*5*sizeof(int8_t));
                    h->mv_cache_clean[list]= 1;
                }*/
                continue; //FIXME direct mode ...
            }
            h->mv_cache_clean[list]= 0;
            
            if(IS_INTER(topleft_type)){
                const int b_xy = h->mb2b_xy[topleft_xy] + 3 + 3*h->b_stride;
                const int b8_xy= h->mb2b8_xy[topleft_xy] + 1 + h->b8_stride;
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy];
                h->ref_cache[list][scan8[0] - 1 - 1*8]= s->current_picture.ref_index[list][b8_xy];
            }else{
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 - 1*8]= 0;
                h->ref_cache[list][scan8[0] - 1 - 1*8]= topleft_type ? LIST_NOT_USED : PART_NOT_AVAILABLE;
            }
            
            if(IS_INTER(top_type)){
                const int b_xy= h->mb2b_xy[top_xy] + 3*h->b_stride;
                const int b8_xy= h->mb2b8_xy[top_xy] + h->b8_stride;
                *(uint32_t*)h->mv_cache[list][scan8[0] + 0 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + 0];
                *(uint32_t*)h->mv_cache[list][scan8[0] + 1 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + 1];
                *(uint32_t*)h->mv_cache[list][scan8[0] + 2 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + 2];
                *(uint32_t*)h->mv_cache[list][scan8[0] + 3 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + 3];
                h->ref_cache[list][scan8[0] + 0 - 1*8]=
                h->ref_cache[list][scan8[0] + 1 - 1*8]= s->current_picture.ref_index[list][b8_xy + 0];
                h->ref_cache[list][scan8[0] + 2 - 1*8]=
                h->ref_cache[list][scan8[0] + 3 - 1*8]= s->current_picture.ref_index[list][b8_xy + 1];
            }else{
                *(uint32_t*)h->mv_cache [list][scan8[0] + 0 - 1*8]= 
                *(uint32_t*)h->mv_cache [list][scan8[0] + 1 - 1*8]= 
                *(uint32_t*)h->mv_cache [list][scan8[0] + 2 - 1*8]= 
                *(uint32_t*)h->mv_cache [list][scan8[0] + 3 - 1*8]= 0;
                *(uint32_t*)&h->ref_cache[list][scan8[0] + 0 - 1*8]= ((top_type ? LIST_NOT_USED : PART_NOT_AVAILABLE)&0xFF)*0x01010101;
            }

            if(IS_INTER(topright_type)){
                const int b_xy= h->mb2b_xy[topright_xy] + 3*h->b_stride;
                const int b8_xy= h->mb2b8_xy[topright_xy] + h->b8_stride;
                *(uint32_t*)h->mv_cache[list][scan8[0] + 4 - 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy];
                h->ref_cache[list][scan8[0] + 4 - 1*8]= s->current_picture.ref_index[list][b8_xy];
            }else{
                *(uint32_t*)h->mv_cache [list][scan8[0] + 4 - 1*8]= 0;
                h->ref_cache[list][scan8[0] + 4 - 1*8]= topright_type ? LIST_NOT_USED : PART_NOT_AVAILABLE;
            }
            
            //FIXME unify cleanup or sth
            if(IS_INTER(left_type[0])){
                const int b_xy= h->mb2b_xy[left_xy[0]] + 3;
                const int b8_xy= h->mb2b8_xy[left_xy[0]] + 1;
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 + 0*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + h->b_stride*left_block[0]];
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 + 1*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + h->b_stride*left_block[1]];
                h->ref_cache[list][scan8[0] - 1 + 0*8]= 
                h->ref_cache[list][scan8[0] - 1 + 1*8]= s->current_picture.ref_index[list][b8_xy + h->b8_stride*(left_block[0]>>1)];
            }else{
                *(uint32_t*)h->mv_cache [list][scan8[0] - 1 + 0*8]=
                *(uint32_t*)h->mv_cache [list][scan8[0] - 1 + 1*8]= 0;
                h->ref_cache[list][scan8[0] - 1 + 0*8]=
                h->ref_cache[list][scan8[0] - 1 + 1*8]= left_type[0] ? LIST_NOT_USED : PART_NOT_AVAILABLE;
            }
            
            if(IS_INTER(left_type[1])){
                const int b_xy= h->mb2b_xy[left_xy[1]] + 3;
                const int b8_xy= h->mb2b8_xy[left_xy[1]] + 1;
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 + 2*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + h->b_stride*left_block[2]];
                *(uint32_t*)h->mv_cache[list][scan8[0] - 1 + 3*8]= *(uint32_t*)s->current_picture.motion_val[list][b_xy + h->b_stride*left_block[3]];
                h->ref_cache[list][scan8[0] - 1 + 2*8]= 
                h->ref_cache[list][scan8[0] - 1 + 3*8]= s->current_picture.ref_index[list][b8_xy + h->b8_stride*(left_block[2]>>1)];
            }else{
                *(uint32_t*)h->mv_cache [list][scan8[0] - 1 + 2*8]=
                *(uint32_t*)h->mv_cache [list][scan8[0] - 1 + 3*8]= 0;
                h->ref_cache[list][scan8[0] - 1 + 2*8]=
                h->ref_cache[list][scan8[0] - 1 + 3*8]= left_type[0] ? LIST_NOT_USED : PART_NOT_AVAILABLE;
            }

            h->ref_cache[list][scan8[5 ]+1] = 
            h->ref_cache[list][scan8[7 ]+1] = 
            h->ref_cache[list][scan8[13]+1] =  //FIXME remove past 3 (init somewher else)
            h->ref_cache[list][scan8[4 ]] = 
            h->ref_cache[list][scan8[12]] = PART_NOT_AVAILABLE;
            *(uint32_t*)h->mv_cache [list][scan8[5 ]+1]=
            *(uint32_t*)h->mv_cache [list][scan8[7 ]+1]=
            *(uint32_t*)h->mv_cache [list][scan8[13]+1]= //FIXME remove past 3 (init somewher else)
            *(uint32_t*)h->mv_cache [list][scan8[4 ]]=
            *(uint32_t*)h->mv_cache [list][scan8[12]]= 0;
        }
//FIXME

    }
#endif
}

static inline void write_back_intra_pred_mode(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;

    h->intra4x4_pred_mode[mb_xy][0]= h->intra4x4_pred_mode_cache[7+8*1];
    h->intra4x4_pred_mode[mb_xy][1]= h->intra4x4_pred_mode_cache[7+8*2];
    h->intra4x4_pred_mode[mb_xy][2]= h->intra4x4_pred_mode_cache[7+8*3];
    h->intra4x4_pred_mode[mb_xy][3]= h->intra4x4_pred_mode_cache[7+8*4];
    h->intra4x4_pred_mode[mb_xy][4]= h->intra4x4_pred_mode_cache[4+8*4];
    h->intra4x4_pred_mode[mb_xy][5]= h->intra4x4_pred_mode_cache[5+8*4];
    h->intra4x4_pred_mode[mb_xy][6]= h->intra4x4_pred_mode_cache[6+8*4];
}

/**
 * checks if the top & left blocks are available if needed & changes the dc mode so it only uses the available blocks.
 */
static inline int check_intra4x4_pred_mode(H264Context *h){
    MpegEncContext * const s = &h->s;
    static const int8_t top [12]= {-1, 0,LEFT_DC_PRED,-1,-1,-1,-1,-1, 0};
    static const int8_t left[12]= { 0,-1, TOP_DC_PRED, 0,-1,-1,-1, 0,-1,DC_128_PRED};
    int i;
    
    if(!(h->top_samples_available&0x8000)){
        for(i=0; i<4; i++){
            int status= top[ h->intra4x4_pred_mode_cache[scan8[0] + i] ];
            if(status<0){
                fprintf(stderr, "top block unavailable for requested intra4x4 mode %d at %d %d\n", status, s->mb_x, s->mb_y);
                return -1;
            } else if(status){
                h->intra4x4_pred_mode_cache[scan8[0] + i]= status;
            }
        }
    }
    
    if(!(h->left_samples_available&0x8000)){
        for(i=0; i<4; i++){
            int status= left[ h->intra4x4_pred_mode_cache[scan8[0] + 8*i] ];
            if(status<0){
                fprintf(stderr, "left block unavailable for requested intra4x4 mode %d at %d %d\n", status, s->mb_x, s->mb_y);
                return -1;
            } else if(status){
                h->intra4x4_pred_mode_cache[scan8[0] + 8*i]= status;
            }
        }
    }

    return 0;
} //FIXME cleanup like next

/**
 * checks if the top & left blocks are available if needed & changes the dc mode so it only uses the available blocks.
 */
static inline int check_intra_pred_mode(H264Context *h, int mode){
    MpegEncContext * const s = &h->s;
    static const int8_t top [7]= {LEFT_DC_PRED8x8, 1,-1,-1};
    static const int8_t left[7]= { TOP_DC_PRED8x8,-1, 2,-1,DC_128_PRED8x8};
    
    if(!(h->top_samples_available&0x8000)){
        mode= top[ mode ];
        if(mode<0){
            fprintf(stderr, "top block unavailable for requested intra mode at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }
    }
    
    if(!(h->left_samples_available&0x8000)){
        mode= left[ mode ];
        if(mode<0){
            fprintf(stderr, "left block unavailable for requested intra mode at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        } 
    }

    return mode;
}

/**
 * gets the predicted intra4x4 prediction mode.
 */
static inline int pred_intra_mode(H264Context *h, int n){
    const int index8= scan8[n];
    const int left= h->intra4x4_pred_mode_cache[index8 - 1];
    const int top = h->intra4x4_pred_mode_cache[index8 - 8];
    const int min= FFMIN(left, top);

    tprintf("mode:%d %d min:%d\n", left ,top, min);

    if(min<0) return DC_PRED;
    else      return min;
}

static inline void write_back_non_zero_count(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;

    h->non_zero_count[mb_xy][0]= h->non_zero_count_cache[4+8*4];
    h->non_zero_count[mb_xy][1]= h->non_zero_count_cache[5+8*4];
    h->non_zero_count[mb_xy][2]= h->non_zero_count_cache[6+8*4];
    h->non_zero_count[mb_xy][3]= h->non_zero_count_cache[7+8*4];
    h->non_zero_count[mb_xy][4]= h->non_zero_count_cache[7+8*3];
    h->non_zero_count[mb_xy][5]= h->non_zero_count_cache[7+8*2];
    h->non_zero_count[mb_xy][6]= h->non_zero_count_cache[7+8*1];
    
    h->non_zero_count[mb_xy][7]= h->non_zero_count_cache[1+8*2];
    h->non_zero_count[mb_xy][8]= h->non_zero_count_cache[2+8*2];
    h->non_zero_count[mb_xy][9]= h->non_zero_count_cache[2+8*1];

    h->non_zero_count[mb_xy][10]=h->non_zero_count_cache[1+8*5];
    h->non_zero_count[mb_xy][11]=h->non_zero_count_cache[2+8*5];
    h->non_zero_count[mb_xy][12]=h->non_zero_count_cache[2+8*4];
}

/**
 * gets the predicted number of non zero coefficients.
 * @param n block index
 */
static inline int pred_non_zero_count(H264Context *h, int n){
    const int index8= scan8[n];
    const int left= h->non_zero_count_cache[index8 - 1];
    const int top = h->non_zero_count_cache[index8 - 8];
    int i= left + top;
    
    if(i<64) i= (i+1)>>1;

    tprintf("pred_nnz L%X T%X n%d s%d P%X\n", left, top, n, scan8[n], i&31);

    return i&31;
}

static inline int fetch_diagonal_mv(H264Context *h, const int16_t **C, int i, int list, int part_width){
    const int topright_ref= h->ref_cache[list][ i - 8 + part_width ];

    if(topright_ref != PART_NOT_AVAILABLE){
        *C= h->mv_cache[list][ i - 8 + part_width ];
        return topright_ref;
    }else{
        tprintf("topright MV not available\n");

        *C= h->mv_cache[list][ i - 8 - 1 ];
        return h->ref_cache[list][ i - 8 - 1 ];
    }
}

/**
 * gets the predicted MV.
 * @param n the block index
 * @param part_width the width of the partition (4, 8,16) -> (1, 2, 4)
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static inline void pred_motion(H264Context * const h, int n, int part_width, int list, int ref, int * const mx, int * const my){
    const int index8= scan8[n];
    const int top_ref=      h->ref_cache[list][ index8 - 8 ];
    const int left_ref=     h->ref_cache[list][ index8 - 1 ];
    const int16_t * const A= h->mv_cache[list][ index8 - 1 ];
    const int16_t * const B= h->mv_cache[list][ index8 - 8 ];
    const int16_t * C;
    int diagonal_ref, match_count;

    assert(part_width==1 || part_width==2 || part_width==4);

/* mv_cache
  B . . A T T T T 
  U . . L . . , .
  U . . L . . . .
  U . . L . . , .
  . . . L . . . .
*/

    diagonal_ref= fetch_diagonal_mv(h, &C, index8, list, part_width);
    match_count= (diagonal_ref==ref) + (top_ref==ref) + (left_ref==ref);
    
    if(match_count > 1){ //most common
        *mx= mid_pred(A[0], B[0], C[0]);
        *my= mid_pred(A[1], B[1], C[1]);
    }else if(match_count==1){
        if(left_ref==ref){
            *mx= A[0];
            *my= A[1];        
        }else if(top_ref==ref){
            *mx= B[0];
            *my= B[1];        
        }else{
            *mx= C[0];
            *my= C[1];        
        }
    }else{
        if(top_ref == PART_NOT_AVAILABLE && diagonal_ref == PART_NOT_AVAILABLE && left_ref != PART_NOT_AVAILABLE){
            *mx= A[0];
            *my= A[1];        
        }else{
            *mx= mid_pred(A[0], B[0], C[0]);
            *my= mid_pred(A[1], B[1], C[1]);
        }
    }
        
    tprintf("pred_motion (%2d %2d %2d) (%2d %2d %2d) (%2d %2d %2d) -> (%2d %2d %2d) at %2d %2d %d list %d\n", top_ref, B[0], B[1],                    diagonal_ref, C[0], C[1], left_ref, A[0], A[1], ref, *mx, *my, h->s.mb_x, h->s.mb_y, n, list);
}

/**
 * gets the directionally predicted 16x8 MV.
 * @param n the block index
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static inline void pred_16x8_motion(H264Context * const h, int n, int list, int ref, int * const mx, int * const my){
    if(n==0){
        const int top_ref=      h->ref_cache[list][ scan8[0] - 8 ];
        const int16_t * const B= h->mv_cache[list][ scan8[0] - 8 ];

        tprintf("pred_16x8: (%2d %2d %2d) at %2d %2d %d list %d", top_ref, B[0], B[1], h->s.mb_x, h->s.mb_y, n, list);
        
        if(top_ref == ref){
            *mx= B[0];
            *my= B[1];
            return;
        }
    }else{
        const int left_ref=     h->ref_cache[list][ scan8[8] - 1 ];
        const int16_t * const A= h->mv_cache[list][ scan8[8] - 1 ];
        
        tprintf("pred_16x8: (%2d %2d %2d) at %2d %2d %d list %d", left_ref, A[0], A[1], h->s.mb_x, h->s.mb_y, n, list);

        if(left_ref == ref){
            *mx= A[0];
            *my= A[1];
            return;
        }
    }

    //RARE
    pred_motion(h, n, 4, list, ref, mx, my);
}

/**
 * gets the directionally predicted 8x16 MV.
 * @param n the block index
 * @param mx the x component of the predicted motion vector
 * @param my the y component of the predicted motion vector
 */
static inline void pred_8x16_motion(H264Context * const h, int n, int list, int ref, int * const mx, int * const my){
    if(n==0){
        const int left_ref=      h->ref_cache[list][ scan8[0] - 1 ];
        const int16_t * const A=  h->mv_cache[list][ scan8[0] - 1 ];
        
        tprintf("pred_8x16: (%2d %2d %2d) at %2d %2d %d list %d", left_ref, A[0], A[1], h->s.mb_x, h->s.mb_y, n, list);

        if(left_ref == ref){
            *mx= A[0];
            *my= A[1];
            return;
        }
    }else{
        const int16_t * C;
        int diagonal_ref;

        diagonal_ref= fetch_diagonal_mv(h, &C, scan8[4], list, 2);
        
        tprintf("pred_8x16: (%2d %2d %2d) at %2d %2d %d list %d", diagonal_ref, C[0], C[1], h->s.mb_x, h->s.mb_y, n, list);

        if(diagonal_ref == ref){ 
            *mx= C[0];
            *my= C[1];
            return;
        }
    }

    //RARE
    pred_motion(h, n, 2, list, ref, mx, my);
}

static inline void pred_pskip_motion(H264Context * const h, int * const mx, int * const my){
    const int top_ref = h->ref_cache[0][ scan8[0] - 8 ];
    const int left_ref= h->ref_cache[0][ scan8[0] - 1 ];

    tprintf("pred_pskip: (%d) (%d) at %2d %2d", top_ref, left_ref, h->s.mb_x, h->s.mb_y);

    if(top_ref == PART_NOT_AVAILABLE || left_ref == PART_NOT_AVAILABLE
       || (top_ref == 0  && *(uint32_t*)h->mv_cache[0][ scan8[0] - 8 ] == 0)
       || (left_ref == 0 && *(uint32_t*)h->mv_cache[0][ scan8[0] - 1 ] == 0)){
       
        *mx = *my = 0;
        return;
    }
        
    pred_motion(h, 0, 4, 0, 0, mx, my);

    return;
}

static inline void write_back_motion(H264Context *h, int mb_type){
    MpegEncContext * const s = &h->s;
    const int b_xy = 4*s->mb_x + 4*s->mb_y*h->b_stride;
    const int b8_xy= 2*s->mb_x + 2*s->mb_y*h->b8_stride;
    int list;

    for(list=0; list<2; list++){
        int y;
        if((!IS_8X8(mb_type)) && !USES_LIST(mb_type, list)){
            if(1){ //FIXME skip or never read if mb_type doesnt use it
                for(y=0; y<4; y++){
                    *(uint64_t*)s->current_picture.motion_val[list][b_xy + 0 + y*h->b_stride]=
                    *(uint64_t*)s->current_picture.motion_val[list][b_xy + 2 + y*h->b_stride]= 0;
                }
                for(y=0; y<2; y++){
                    *(uint16_t*)s->current_picture.motion_val[list][b8_xy + y*h->b8_stride]= (LIST_NOT_USED&0xFF)*0x0101;
                }
            }
            continue; //FIXME direct mode ...
        }
        
        for(y=0; y<4; y++){
            *(uint64_t*)s->current_picture.motion_val[list][b_xy + 0 + y*h->b_stride]= *(uint64_t*)h->mv_cache[list][scan8[0]+0 + 8*y];
            *(uint64_t*)s->current_picture.motion_val[list][b_xy + 2 + y*h->b_stride]= *(uint64_t*)h->mv_cache[list][scan8[0]+2 + 8*y];
        }
        for(y=0; y<2; y++){
            s->current_picture.ref_index[list][b8_xy + 0 + y*h->b8_stride]= h->ref_cache[list][scan8[0]+0 + 16*y];
            s->current_picture.ref_index[list][b8_xy + 1 + y*h->b8_stride]= h->ref_cache[list][scan8[0]+2 + 16*y];
        }
    }
}

/**
 * Decodes a network abstraction layer unit.
 * @param consumed is the number of bytes used as input
 * @param length is the length of the array
 * @param dst_length is the number of decoded bytes FIXME here or a decode rbsp ttailing?
 * @returns decoded bytes, might be src+1 if no escapes 
 */
static uint8_t *decode_nal(H264Context *h, uint8_t *src, int *dst_length, int *consumed, int length){
    int i, si, di;
    uint8_t *dst;

//    src[0]&0x80;		//forbidden bit
    h->nal_ref_idc= src[0]>>5;
    h->nal_unit_type= src[0]&0x1F;

    src++; length--;
#if 0    
    for(i=0; i<length; i++)
        printf("%2X ", src[i]);
#endif
    for(i=0; i+1<length; i+=2){
        if(src[i]) continue;
        if(i>0 && src[i-1]==0) i--;
        if(i+2<length && src[i+1]==0 && src[i+2]<=3){
            if(src[i+2]!=3){
                /* startcode, so we must be past the end */
                length=i;
            }
            break;
        }
    }

    if(i>=length-1){ //no escaped 0
        *dst_length= length;
        *consumed= length+1; //+1 for the header
        return src; 
    }

    h->rbsp_buffer= av_fast_realloc(h->rbsp_buffer, &h->rbsp_buffer_size, length);
    dst= h->rbsp_buffer;

//printf("deoding esc\n");
    si=di=0;
    while(si<length){ 
        //remove escapes (very rare 1:2^22)
        if(si+2<length && src[si]==0 && src[si+1]==0 && src[si+2]<=3){
            if(src[si+2]==3){ //escape
                dst[di++]= 0;
                dst[di++]= 0;
                si+=3;
            }else //next start code
                break;
        }

        dst[di++]= src[si++];
    }

    *dst_length= di;
    *consumed= si + 1;//+1 for the header
//FIXME store exact number of bits in the getbitcontext (its needed for decoding)
    return dst;
}

/**
 * @param src the data which should be escaped
 * @param dst the target buffer, dst+1 == src is allowed as a special case
 * @param length the length of the src data
 * @param dst_length the length of the dst array
 * @returns length of escaped data in bytes or -1 if an error occured
 */
static int encode_nal(H264Context *h, uint8_t *dst, uint8_t *src, int length, int dst_length){
    int i, escape_count, si, di;
    uint8_t *temp;
    
    assert(length>=0);
    assert(dst_length>0);
    
    dst[0]= (h->nal_ref_idc<<5) + h->nal_unit_type;

    if(length==0) return 1;

    escape_count= 0;
    for(i=0; i<length; i+=2){
        if(src[i]) continue;
        if(i>0 && src[i-1]==0) 
            i--;
        if(i+2<length && src[i+1]==0 && src[i+2]<=3){
            escape_count++;
            i+=2;
        }
    }
    
    if(escape_count==0){ 
        if(dst+1 != src)
            memcpy(dst+1, src, length);
        return length + 1;
    }
    
    if(length + escape_count + 1> dst_length)
        return -1;

    //this should be damn rare (hopefully)

    h->rbsp_buffer= av_fast_realloc(h->rbsp_buffer, &h->rbsp_buffer_size, length + escape_count);
    temp= h->rbsp_buffer;
//printf("encoding esc\n");
    
    si= 0;
    di= 0;
    while(si < length){
        if(si+2<length && src[si]==0 && src[si+1]==0 && src[si+2]<=3){
            temp[di++]= 0; si++;
            temp[di++]= 0; si++;
            temp[di++]= 3; 
            temp[di++]= src[si++];
        }
        else
            temp[di++]= src[si++];
    }
    memcpy(dst+1, temp, length+escape_count);
    
    assert(di == length+escape_count);
    
    return di + 1;
}

/**
 * write 1,10,100,1000,... for alignment, yes its exactly inverse to mpeg4
 */
static void encode_rbsp_trailing(PutBitContext *pb){
    int length;
    put_bits(pb, 1, 1);
    length= (-get_bit_count(pb))&7;
    if(length) put_bits(pb, length, 0);
}

/**
 * identifies the exact end of the bitstream
 * @return the length of the trailing, or 0 if damaged
 */
static int decode_rbsp_trailing(uint8_t *src){
    int v= *src;
    int r;

    tprintf("rbsp trailing %X\n", v);

    for(r=1; r<9; r++){
        if(v&1) return r;
        v>>=1;
    }
    return 0;
}

/**
 * idct tranforms the 16 dc values and dequantize them.
 * @param qp quantization parameter
 */
static void h264_luma_dc_dequant_idct_c(DCTELEM *block, int qp){
    const int qmul= dequant_coeff[qp][0];
#define stride 16
    int i;
    int temp[16]; //FIXME check if this is a good idea
    static const int x_offset[4]={0, 1*stride, 4* stride,  5*stride};
    static const int y_offset[4]={0, 2*stride, 8* stride, 10*stride};

//memset(block, 64, 2*256);
//return;
    for(i=0; i<4; i++){
        const int offset= y_offset[i];
        const int z0= block[offset+stride*0] + block[offset+stride*4];
        const int z1= block[offset+stride*0] - block[offset+stride*4];
        const int z2= block[offset+stride*1] - block[offset+stride*5];
        const int z3= block[offset+stride*1] + block[offset+stride*5];

        temp[4*i+0]= z0+z3;
        temp[4*i+1]= z1+z2;
        temp[4*i+2]= z1-z2;
        temp[4*i+3]= z0-z3;
    }

    for(i=0; i<4; i++){
        const int offset= x_offset[i];
        const int z0= temp[4*0+i] + temp[4*2+i];
        const int z1= temp[4*0+i] - temp[4*2+i];
        const int z2= temp[4*1+i] - temp[4*3+i];
        const int z3= temp[4*1+i] + temp[4*3+i];

        block[stride*0 +offset]= ((z0 + z3)*qmul + 2)>>2; //FIXME think about merging this into decode_resdual
        block[stride*2 +offset]= ((z1 + z2)*qmul + 2)>>2;
        block[stride*8 +offset]= ((z1 - z2)*qmul + 2)>>2;
        block[stride*10+offset]= ((z0 - z3)*qmul + 2)>>2;
    }
}

/**
 * dct tranforms the 16 dc values.
 * @param qp quantization parameter ??? FIXME
 */
static void h264_luma_dc_dct_c(DCTELEM *block/*, int qp*/){
//    const int qmul= dequant_coeff[qp][0];
    int i;
    int temp[16]; //FIXME check if this is a good idea
    static const int x_offset[4]={0, 1*stride, 4* stride,  5*stride};
    static const int y_offset[4]={0, 2*stride, 8* stride, 10*stride};

    for(i=0; i<4; i++){
        const int offset= y_offset[i];
        const int z0= block[offset+stride*0] + block[offset+stride*4];
        const int z1= block[offset+stride*0] - block[offset+stride*4];
        const int z2= block[offset+stride*1] - block[offset+stride*5];
        const int z3= block[offset+stride*1] + block[offset+stride*5];

        temp[4*i+0]= z0+z3;
        temp[4*i+1]= z1+z2;
        temp[4*i+2]= z1-z2;
        temp[4*i+3]= z0-z3;
    }

    for(i=0; i<4; i++){
        const int offset= x_offset[i];
        const int z0= temp[4*0+i] + temp[4*2+i];
        const int z1= temp[4*0+i] - temp[4*2+i];
        const int z2= temp[4*1+i] - temp[4*3+i];
        const int z3= temp[4*1+i] + temp[4*3+i];

        block[stride*0 +offset]= (z0 + z3)>>1;
        block[stride*2 +offset]= (z1 + z2)>>1;
        block[stride*8 +offset]= (z1 - z2)>>1;
        block[stride*10+offset]= (z0 - z3)>>1;
    }
}
#undef xStride
#undef stride

static void chroma_dc_dequant_idct_c(DCTELEM *block, int qp){
    const int qmul= dequant_coeff[qp][0];
    const int stride= 16*2;
    const int xStride= 16;
    int a,b,c,d,e;

    a= block[stride*0 + xStride*0];
    b= block[stride*0 + xStride*1];
    c= block[stride*1 + xStride*0];
    d= block[stride*1 + xStride*1];

    e= a-b;
    a= a+b;
    b= c-d;
    c= c+d;

    block[stride*0 + xStride*0]= ((a+c)*qmul + 0)>>1;
    block[stride*0 + xStride*1]= ((e+b)*qmul + 0)>>1;
    block[stride*1 + xStride*0]= ((a-c)*qmul + 0)>>1;
    block[stride*1 + xStride*1]= ((e-b)*qmul + 0)>>1;
}

static void chroma_dc_dct_c(DCTELEM *block){
    const int stride= 16*2;
    const int xStride= 16;
    int a,b,c,d,e;

    a= block[stride*0 + xStride*0];
    b= block[stride*0 + xStride*1];
    c= block[stride*1 + xStride*0];
    d= block[stride*1 + xStride*1];

    e= a-b;
    a= a+b;
    b= c-d;
    c= c+d;

    block[stride*0 + xStride*0]= (a+c);
    block[stride*0 + xStride*1]= (e+b);
    block[stride*1 + xStride*0]= (a-c);
    block[stride*1 + xStride*1]= (e-b);
}

/**
 * gets the chroma qp.
 */
static inline int get_chroma_qp(H264Context *h, int qscale){
    
    return chroma_qp[clip(qscale + h->pps.chroma_qp_index_offset, 0, 51)];
}


/**
 *
 */
static void h264_add_idct_c(uint8_t *dst, DCTELEM *block, int stride){
    int i;
    uint8_t *cm = cropTbl + MAX_NEG_CROP;

    block[0] += 32;
#if 1
    for(i=0; i<4; i++){
        const int z0=  block[i + 4*0]     +  block[i + 4*2];
        const int z1=  block[i + 4*0]     -  block[i + 4*2];
        const int z2= (block[i + 4*1]>>1) -  block[i + 4*3];
        const int z3=  block[i + 4*1]     + (block[i + 4*3]>>1);

        block[i + 4*0]= z0 + z3;
        block[i + 4*1]= z1 + z2;
        block[i + 4*2]= z1 - z2;
        block[i + 4*3]= z0 - z3;
    }

    for(i=0; i<4; i++){
        const int z0=  block[0 + 4*i]     +  block[2 + 4*i];
        const int z1=  block[0 + 4*i]     -  block[2 + 4*i];
        const int z2= (block[1 + 4*i]>>1) -  block[3 + 4*i];
        const int z3=  block[1 + 4*i]     + (block[3 + 4*i]>>1);

        dst[0 + i*stride]= cm[ dst[0 + i*stride] + ((z0 + z3) >> 6) ];
        dst[1 + i*stride]= cm[ dst[1 + i*stride] + ((z1 + z2) >> 6) ];
        dst[2 + i*stride]= cm[ dst[2 + i*stride] + ((z1 - z2) >> 6) ];
        dst[3 + i*stride]= cm[ dst[3 + i*stride] + ((z0 - z3) >> 6) ];
    }
#else
    for(i=0; i<4; i++){
        const int z0=  block[0 + 4*i]     +  block[2 + 4*i];
        const int z1=  block[0 + 4*i]     -  block[2 + 4*i];
        const int z2= (block[1 + 4*i]>>1) -  block[3 + 4*i];
        const int z3=  block[1 + 4*i]     + (block[3 + 4*i]>>1);

        block[0 + 4*i]= z0 + z3;
        block[1 + 4*i]= z1 + z2;
        block[2 + 4*i]= z1 - z2;
        block[3 + 4*i]= z0 - z3;
    }

    for(i=0; i<4; i++){
        const int z0=  block[i + 4*0]     +  block[i + 4*2];
        const int z1=  block[i + 4*0]     -  block[i + 4*2];
        const int z2= (block[i + 4*1]>>1) -  block[i + 4*3];
        const int z3=  block[i + 4*1]     + (block[i + 4*3]>>1);

        dst[i + 0*stride]= cm[ dst[i + 0*stride] + ((z0 + z3) >> 6) ];
        dst[i + 1*stride]= cm[ dst[i + 1*stride] + ((z1 + z2) >> 6) ];
        dst[i + 2*stride]= cm[ dst[i + 2*stride] + ((z1 - z2) >> 6) ];
        dst[i + 3*stride]= cm[ dst[i + 3*stride] + ((z0 - z3) >> 6) ];
    }
#endif
}

static void h264_diff_dct_c(DCTELEM *block, uint8_t *src1, uint8_t *src2, int stride){
    int i;
    //FIXME try int temp instead of block
    
    for(i=0; i<4; i++){
        const int d0= src1[0 + i*stride] - src2[0 + i*stride];
        const int d1= src1[1 + i*stride] - src2[1 + i*stride];
        const int d2= src1[2 + i*stride] - src2[2 + i*stride];
        const int d3= src1[3 + i*stride] - src2[3 + i*stride];
        const int z0= d0 + d3;
        const int z3= d0 - d3;
        const int z1= d1 + d2;
        const int z2= d1 - d2;
        
        block[0 + 4*i]=   z0 +   z1;
        block[1 + 4*i]= 2*z3 +   z2;
        block[2 + 4*i]=   z0 -   z1;
        block[3 + 4*i]=   z3 - 2*z2;
    }    

    for(i=0; i<4; i++){
        const int z0= block[0*4 + i] + block[3*4 + i];
        const int z3= block[0*4 + i] - block[3*4 + i];
        const int z1= block[1*4 + i] + block[2*4 + i];
        const int z2= block[1*4 + i] - block[2*4 + i];
        
        block[0*4 + i]=   z0 +   z1;
        block[1*4 + i]= 2*z3 +   z2;
        block[2*4 + i]=   z0 -   z1;
        block[3*4 + i]=   z3 - 2*z2;
    }
}

//FIXME need to check that this doesnt overflow signed 32 bit for low qp, iam not sure, its very close
//FIXME check that gcc inlines this (and optimizes intra & seperate_dc stuff away)
static inline int quantize_c(DCTELEM *block, uint8_t *scantable, int qscale, int intra, int seperate_dc){
    int i;
    const int * const quant_table= quant_coeff[qscale];
    const int bias= intra ? (1<<QUANT_SHIFT)/3 : (1<<QUANT_SHIFT)/6;
    const unsigned int threshold1= (1<<QUANT_SHIFT) - bias - 1;
    const unsigned int threshold2= (threshold1<<1);
    int last_non_zero;

    if(seperate_dc){
        if(qscale<=18){
            //avoid overflows
            const int dc_bias= intra ? (1<<(QUANT_SHIFT-2))/3 : (1<<(QUANT_SHIFT-2))/6;
            const unsigned int dc_threshold1= (1<<(QUANT_SHIFT-2)) - dc_bias - 1;
            const unsigned int dc_threshold2= (dc_threshold1<<1);

            int level= block[0]*quant_coeff[qscale+18][0];
            if(((unsigned)(level+dc_threshold1))>dc_threshold2){
                if(level>0){
                    level= (dc_bias + level)>>(QUANT_SHIFT-2);
                    block[0]= level;
                }else{
                    level= (dc_bias - level)>>(QUANT_SHIFT-2);
                    block[0]= -level;
                }
//                last_non_zero = i;
            }else{
                block[0]=0;
            }
        }else{
            const int dc_bias= intra ? (1<<(QUANT_SHIFT+1))/3 : (1<<(QUANT_SHIFT+1))/6;
            const unsigned int dc_threshold1= (1<<(QUANT_SHIFT+1)) - dc_bias - 1;
            const unsigned int dc_threshold2= (dc_threshold1<<1);

            int level= block[0]*quant_table[0];
            if(((unsigned)(level+dc_threshold1))>dc_threshold2){
                if(level>0){
                    level= (dc_bias + level)>>(QUANT_SHIFT+1);
                    block[0]= level;
                }else{
                    level= (dc_bias - level)>>(QUANT_SHIFT+1);
                    block[0]= -level;
                }
//                last_non_zero = i;
            }else{
                block[0]=0;
            }
        }
        last_non_zero= 0;
        i=1;
    }else{
        last_non_zero= -1;
        i=0;
    }

    for(; i<16; i++){
        const int j= scantable[i];
        int level= block[j]*quant_table[j];

//        if(   bias+level >= (1<<(QMAT_SHIFT - 3))
//           || bias-level >= (1<<(QMAT_SHIFT - 3))){
        if(((unsigned)(level+threshold1))>threshold2){
            if(level>0){
                level= (bias + level)>>QUANT_SHIFT;
                block[j]= level;
            }else{
                level= (bias - level)>>QUANT_SHIFT;
                block[j]= -level;
            }
            last_non_zero = i;
        }else{
            block[j]=0;
        }
    }

    return last_non_zero;
}

static void pred4x4_vertical_c(uint8_t *src, uint8_t *topright, int stride){
    const uint32_t a= ((uint32_t*)(src-stride))[0];
    ((uint32_t*)(src+0*stride))[0]= a;
    ((uint32_t*)(src+1*stride))[0]= a;
    ((uint32_t*)(src+2*stride))[0]= a;
    ((uint32_t*)(src+3*stride))[0]= a;
}

static void pred4x4_horizontal_c(uint8_t *src, uint8_t *topright, int stride){
    ((uint32_t*)(src+0*stride))[0]= src[-1+0*stride]*0x01010101;
    ((uint32_t*)(src+1*stride))[0]= src[-1+1*stride]*0x01010101;
    ((uint32_t*)(src+2*stride))[0]= src[-1+2*stride]*0x01010101;
    ((uint32_t*)(src+3*stride))[0]= src[-1+3*stride]*0x01010101;
}

static void pred4x4_dc_c(uint8_t *src, uint8_t *topright, int stride){
    const int dc= (  src[-stride] + src[1-stride] + src[2-stride] + src[3-stride]
                   + src[-1+0*stride] + src[-1+1*stride] + src[-1+2*stride] + src[-1+3*stride] + 4) >>3;
    
    ((uint32_t*)(src+0*stride))[0]= 
    ((uint32_t*)(src+1*stride))[0]= 
    ((uint32_t*)(src+2*stride))[0]= 
    ((uint32_t*)(src+3*stride))[0]= dc* 0x01010101; 
}

static void pred4x4_left_dc_c(uint8_t *src, uint8_t *topright, int stride){
    const int dc= (  src[-1+0*stride] + src[-1+1*stride] + src[-1+2*stride] + src[-1+3*stride] + 2) >>2;
    
    ((uint32_t*)(src+0*stride))[0]= 
    ((uint32_t*)(src+1*stride))[0]= 
    ((uint32_t*)(src+2*stride))[0]= 
    ((uint32_t*)(src+3*stride))[0]= dc* 0x01010101; 
}

static void pred4x4_top_dc_c(uint8_t *src, uint8_t *topright, int stride){
    const int dc= (  src[-stride] + src[1-stride] + src[2-stride] + src[3-stride] + 2) >>2;
    
    ((uint32_t*)(src+0*stride))[0]= 
    ((uint32_t*)(src+1*stride))[0]= 
    ((uint32_t*)(src+2*stride))[0]= 
    ((uint32_t*)(src+3*stride))[0]= dc* 0x01010101; 
}

static void pred4x4_128_dc_c(uint8_t *src, uint8_t *topright, int stride){
    ((uint32_t*)(src+0*stride))[0]= 
    ((uint32_t*)(src+1*stride))[0]= 
    ((uint32_t*)(src+2*stride))[0]= 
    ((uint32_t*)(src+3*stride))[0]= 128U*0x01010101U;
}


#define LOAD_TOP_RIGHT_EDGE\
    const int t4= topright[0];\
    const int t5= topright[1];\
    const int t6= topright[2];\
    const int t7= topright[3];\

#define LOAD_LEFT_EDGE\
    const int l0= src[-1+0*stride];\
    const int l1= src[-1+1*stride];\
    const int l2= src[-1+2*stride];\
    const int l3= src[-1+3*stride];\

#define LOAD_TOP_EDGE\
    const int t0= src[ 0-1*stride];\
    const int t1= src[ 1-1*stride];\
    const int t2= src[ 2-1*stride];\
    const int t3= src[ 3-1*stride];\

static void pred4x4_down_right_c(uint8_t *src, uint8_t *topright, int stride){
    const int lt= src[-1-1*stride];
    LOAD_TOP_EDGE
    LOAD_LEFT_EDGE

    src[0+3*stride]=(l3 + 2*l2 + l1 + 2)>>2; 
    src[0+2*stride]=
    src[1+3*stride]=(l2 + 2*l1 + l0 + 2)>>2; 
    src[0+1*stride]=
    src[1+2*stride]=
    src[2+3*stride]=(l1 + 2*l0 + lt + 2)>>2; 
    src[0+0*stride]=
    src[1+1*stride]=
    src[2+2*stride]=
    src[3+3*stride]=(l0 + 2*lt + t0 + 2)>>2; 
    src[1+0*stride]=
    src[2+1*stride]=
    src[3+2*stride]=(lt + 2*t0 + t1 + 2)>>2;
    src[2+0*stride]=
    src[3+1*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[3+0*stride]=(t1 + 2*t2 + t3 + 2)>>2;
};

static void pred4x4_down_left_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_TOP_EDGE    
    LOAD_TOP_RIGHT_EDGE    
//    LOAD_LEFT_EDGE    

    src[0+0*stride]=(t0 + t2 + 2*t1 + 2)>>2;
    src[1+0*stride]=
    src[0+1*stride]=(t1 + t3 + 2*t2 + 2)>>2;
    src[2+0*stride]=
    src[1+1*stride]=
    src[0+2*stride]=(t2 + t4 + 2*t3 + 2)>>2;
    src[3+0*stride]=
    src[2+1*stride]=
    src[1+2*stride]=
    src[0+3*stride]=(t3 + t5 + 2*t4 + 2)>>2;
    src[3+1*stride]=
    src[2+2*stride]=
    src[1+3*stride]=(t4 + t6 + 2*t5 + 2)>>2;
    src[3+2*stride]=
    src[2+3*stride]=(t5 + t7 + 2*t6 + 2)>>2;
    src[3+3*stride]=(t6 + 3*t7 + 2)>>2;
};

static void pred4x4_vertical_right_c(uint8_t *src, uint8_t *topright, int stride){
    const int lt= src[-1-1*stride];
    LOAD_TOP_EDGE    
    LOAD_LEFT_EDGE    
    const __attribute__((unused)) int unu= l3;

    src[0+0*stride]=
    src[1+2*stride]=(lt + t0 + 1)>>1;
    src[1+0*stride]=
    src[2+2*stride]=(t0 + t1 + 1)>>1;
    src[2+0*stride]=
    src[3+2*stride]=(t1 + t2 + 1)>>1;
    src[3+0*stride]=(t2 + t3 + 1)>>1;
    src[0+1*stride]=
    src[1+3*stride]=(l0 + 2*lt + t0 + 2)>>2;
    src[1+1*stride]=
    src[2+3*stride]=(lt + 2*t0 + t1 + 2)>>2;
    src[2+1*stride]=
    src[3+3*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[3+1*stride]=(t1 + 2*t2 + t3 + 2)>>2;
    src[0+2*stride]=(lt + 2*l0 + l1 + 2)>>2;
    src[0+3*stride]=(l0 + 2*l1 + l2 + 2)>>2;
};

static void pred4x4_vertical_left_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_TOP_EDGE    
    LOAD_TOP_RIGHT_EDGE    
    const __attribute__((unused)) int unu= t7;

    src[0+0*stride]=(t0 + t1 + 1)>>1;
    src[1+0*stride]=
    src[0+2*stride]=(t1 + t2 + 1)>>1;
    src[2+0*stride]=
    src[1+2*stride]=(t2 + t3 + 1)>>1;
    src[3+0*stride]=
    src[2+2*stride]=(t3 + t4+ 1)>>1;
    src[3+2*stride]=(t4 + t5+ 1)>>1;
    src[0+1*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[1+1*stride]=
    src[0+3*stride]=(t1 + 2*t2 + t3 + 2)>>2;
    src[2+1*stride]=
    src[1+3*stride]=(t2 + 2*t3 + t4 + 2)>>2;
    src[3+1*stride]=
    src[2+3*stride]=(t3 + 2*t4 + t5 + 2)>>2;
    src[3+3*stride]=(t4 + 2*t5 + t6 + 2)>>2;
};

static void pred4x4_horizontal_up_c(uint8_t *src, uint8_t *topright, int stride){
    LOAD_LEFT_EDGE    

    src[0+0*stride]=(l0 + l1 + 1)>>1;
    src[1+0*stride]=(l0 + 2*l1 + l2 + 2)>>2;
    src[2+0*stride]=
    src[0+1*stride]=(l1 + l2 + 1)>>1;
    src[3+0*stride]=
    src[1+1*stride]=(l1 + 2*l2 + l3 + 2)>>2;
    src[2+1*stride]=
    src[0+2*stride]=(l2 + l3 + 1)>>1;
    src[3+1*stride]=
    src[1+2*stride]=(l2 + 2*l3 + l3 + 2)>>2;
    src[3+2*stride]=
    src[1+3*stride]=
    src[0+3*stride]=
    src[2+2*stride]=
    src[2+3*stride]=
    src[3+3*stride]=l3;
};
    
static void pred4x4_horizontal_down_c(uint8_t *src, uint8_t *topright, int stride){
    const int lt= src[-1-1*stride];
    LOAD_TOP_EDGE    
    LOAD_LEFT_EDGE    
    const __attribute__((unused)) int unu= t3;

    src[0+0*stride]=
    src[2+1*stride]=(lt + l0 + 1)>>1;
    src[1+0*stride]=
    src[3+1*stride]=(l0 + 2*lt + t0 + 2)>>2;
    src[2+0*stride]=(lt + 2*t0 + t1 + 2)>>2;
    src[3+0*stride]=(t0 + 2*t1 + t2 + 2)>>2;
    src[0+1*stride]=
    src[2+2*stride]=(l0 + l1 + 1)>>1;
    src[1+1*stride]=
    src[3+2*stride]=(lt + 2*l0 + l1 + 2)>>2;
    src[0+2*stride]=
    src[2+3*stride]=(l1 + l2+ 1)>>1;
    src[1+2*stride]=
    src[3+3*stride]=(l0 + 2*l1 + l2 + 2)>>2;
    src[0+3*stride]=(l2 + l3 + 1)>>1;
    src[1+3*stride]=(l1 + 2*l2 + l3 + 2)>>2;
};

static void pred16x16_vertical_c(uint8_t *src, int stride){
    int i;
    const uint32_t a= ((uint32_t*)(src-stride))[0];
    const uint32_t b= ((uint32_t*)(src-stride))[1];
    const uint32_t c= ((uint32_t*)(src-stride))[2];
    const uint32_t d= ((uint32_t*)(src-stride))[3];
    
    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]= a;
        ((uint32_t*)(src+i*stride))[1]= b;
        ((uint32_t*)(src+i*stride))[2]= c;
        ((uint32_t*)(src+i*stride))[3]= d;
    }
}

static void pred16x16_horizontal_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= src[-1+i*stride]*0x01010101;
    }
}

static void pred16x16_dc_c(uint8_t *src, int stride){
    int i, dc=0;

    for(i=0;i<16; i++){
        dc+= src[-1+i*stride];
    }
    
    for(i=0;i<16; i++){
        dc+= src[i-stride];
    }

    dc= 0x01010101*((dc + 16)>>5);

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= dc;
    }
}

static void pred16x16_left_dc_c(uint8_t *src, int stride){
    int i, dc=0;

    for(i=0;i<16; i++){
        dc+= src[-1+i*stride];
    }
    
    dc= 0x01010101*((dc + 8)>>4);

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= dc;
    }
}

static void pred16x16_top_dc_c(uint8_t *src, int stride){
    int i, dc=0;

    for(i=0;i<16; i++){
        dc+= src[i-stride];
    }
    dc= 0x01010101*((dc + 8)>>4);

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= dc;
    }
}

static void pred16x16_128_dc_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<16; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]=
        ((uint32_t*)(src+i*stride))[2]=
        ((uint32_t*)(src+i*stride))[3]= 0x01010101U*128U;
    }
}

static inline void pred16x16_plane_compat_c(uint8_t *src, int stride, const int svq3){
  int i, j, k;
  int a;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  const uint8_t * const src0 = src+7-stride;
  const uint8_t *src1 = src+8*stride-1;
  const uint8_t *src2 = src1-2*stride;      // == src+6*stride-1;
  int H = src0[1] - src0[-1];
  int V = src1[0] - src2[ 0];
  for(k=2; k<=8; ++k) {
    src1 += stride; src2 -= stride;
    H += k*(src0[k] - src0[-k]);
    V += k*(src1[0] - src2[ 0]);
  }
  if(svq3){
    H = ( 5*(H/4) ) / 16;
    V = ( 5*(V/4) ) / 16;
  }else{
    H = ( 5*H+32 ) >> 6;
    V = ( 5*V+32 ) >> 6;
  }

  a = 16*(src1[0] + src2[16] + 1) - 7*(V+H);
  for(j=16; j>0; --j) {
    int b = a;
    a += V;
    for(i=-16; i<0; i+=4) {
      src[16+i] = cm[ (b    ) >> 5 ];
      src[17+i] = cm[ (b+  H) >> 5 ];
      src[18+i] = cm[ (b+2*H) >> 5 ];
      src[19+i] = cm[ (b+3*H) >> 5 ];
      b += 4*H;
    }
    src += stride;
  }
}

static void pred16x16_plane_c(uint8_t *src, int stride){
    pred16x16_plane_compat_c(src, stride, 0);
}

static void pred8x8_vertical_c(uint8_t *src, int stride){
    int i;
    const uint32_t a= ((uint32_t*)(src-stride))[0];
    const uint32_t b= ((uint32_t*)(src-stride))[1];
    
    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= a;
        ((uint32_t*)(src+i*stride))[1]= b;
    }
}

static void pred8x8_horizontal_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= src[-1+i*stride]*0x01010101;
    }
}

static void pred8x8_128_dc_c(uint8_t *src, int stride){
    int i;

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]= 
        ((uint32_t*)(src+i*stride))[1]= 0x01010101U*128U;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= 
        ((uint32_t*)(src+i*stride))[1]= 0x01010101U*128U;
    }
}

static void pred8x8_left_dc_c(uint8_t *src, int stride){
    int i;
    int dc0, dc2;

    dc0=dc2=0;
    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride];
        dc2+= src[-1+(i+4)*stride];
    }
    dc0= 0x01010101*((dc0 + 2)>>2);
    dc2= 0x01010101*((dc2 + 2)>>2);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc0;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]=
        ((uint32_t*)(src+i*stride))[1]= dc2;
    }
}

static void pred8x8_top_dc_c(uint8_t *src, int stride){
    int i;
    int dc0, dc1;

    dc0=dc1=0;
    for(i=0;i<4; i++){
        dc0+= src[i-stride];
        dc1+= src[4+i-stride];
    }
    dc0= 0x01010101*((dc0 + 2)>>2);
    dc1= 0x01010101*((dc1 + 2)>>2);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc1;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc1;
    }
}


static void pred8x8_dc_c(uint8_t *src, int stride){
    int i;
    int dc0, dc1, dc2, dc3;

    dc0=dc1=dc2=0;
    for(i=0;i<4; i++){
        dc0+= src[-1+i*stride] + src[i-stride];
        dc1+= src[4+i-stride];
        dc2+= src[-1+(i+4)*stride];
    }
    dc3= 0x01010101*((dc1 + dc2 + 4)>>3);
    dc0= 0x01010101*((dc0 + 4)>>3);
    dc1= 0x01010101*((dc1 + 2)>>2);
    dc2= 0x01010101*((dc2 + 2)>>2);

    for(i=0; i<4; i++){
        ((uint32_t*)(src+i*stride))[0]= dc0;
        ((uint32_t*)(src+i*stride))[1]= dc1;
    }
    for(i=4; i<8; i++){
        ((uint32_t*)(src+i*stride))[0]= dc2;
        ((uint32_t*)(src+i*stride))[1]= dc3;
    }
}

static void pred8x8_plane_c(uint8_t *src, int stride){
  int j, k;
  int a;
  uint8_t *cm = cropTbl + MAX_NEG_CROP;
  const uint8_t * const src0 = src+3-stride;
  const uint8_t *src1 = src+4*stride-1;
  const uint8_t *src2 = src1-2*stride;      // == src+2*stride-1;
  int H = src0[1] - src0[-1];
  int V = src1[0] - src2[ 0];
  for(k=2; k<=4; ++k) {
    src1 += stride; src2 -= stride;
    H += k*(src0[k] - src0[-k]);
    V += k*(src1[0] - src2[ 0]);
  }
  H = ( 17*H+16 ) >> 5;
  V = ( 17*V+16 ) >> 5;

  a = 16*(src1[0] + src2[8]+1) - 3*(V+H);
  for(j=8; j>0; --j) {
    int b = a;
    a += V;
    src[0] = cm[ (b    ) >> 5 ];
    src[1] = cm[ (b+  H) >> 5 ];
    src[2] = cm[ (b+2*H) >> 5 ];
    src[3] = cm[ (b+3*H) >> 5 ];
    src[4] = cm[ (b+4*H) >> 5 ];
    src[5] = cm[ (b+5*H) >> 5 ];
    src[6] = cm[ (b+6*H) >> 5 ];
    src[7] = cm[ (b+7*H) >> 5 ];
    src += stride;
  }
}

static inline void mc_dir_part(H264Context *h, Picture *pic, int n, int square, int chroma_height, int delta, int list,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int src_x_offset, int src_y_offset,
                           qpel_mc_func *qpix_op, h264_chroma_mc_func chroma_op){
    MpegEncContext * const s = &h->s;
    const int mx= h->mv_cache[list][ scan8[n] ][0] + src_x_offset*8;
    const int my= h->mv_cache[list][ scan8[n] ][1] + src_y_offset*8;
    const int luma_xy= (mx&3) + ((my&3)<<2);
    uint8_t * src_y = pic->data[0] + (mx>>2) + (my>>2)*s->linesize;
    uint8_t * src_cb= pic->data[1] + (mx>>3) + (my>>3)*s->uvlinesize;
    uint8_t * src_cr= pic->data[2] + (mx>>3) + (my>>3)*s->uvlinesize;
    int extra_width= (s->flags&CODEC_FLAG_EMU_EDGE) ? 0 : 16; //FIXME increase edge?, IMHO not worth it
    int extra_height= extra_width;
    int emu=0;
    const int full_mx= mx>>2;
    const int full_my= my>>2;
    
    assert(pic->data[0]);
    
    if(mx&7) extra_width -= 3;
    if(my&7) extra_height -= 3;
    
    if(   full_mx < 0-extra_width 
       || full_my < 0-extra_height 
       || full_mx + 16/*FIXME*/ > s->width + extra_width 
       || full_my + 16/*FIXME*/ > s->height + extra_height){
        ff_emulated_edge_mc(s, src_y - 2 - 2*s->linesize, s->linesize, 16+5, 16+5/*FIXME*/, full_mx-2, full_my-2, s->width, s->height);
            src_y= s->edge_emu_buffer + 2 + 2*s->linesize;
        emu=1;
    }
    
    qpix_op[luma_xy](dest_y, src_y, s->linesize); //FIXME try variable height perhaps?
    if(!square){
        qpix_op[luma_xy](dest_y + delta, src_y + delta, s->linesize);
    }
    
    if(s->flags&CODEC_FLAG_GRAY) return;
    
    if(emu){
        ff_emulated_edge_mc(s, src_cb, s->uvlinesize, 9, 9/*FIXME*/, (mx>>3), (my>>3), s->width>>1, s->height>>1);
            src_cb= s->edge_emu_buffer;
    }
    chroma_op(dest_cb, src_cb, s->uvlinesize, chroma_height, mx&7, my&7);

    if(emu){
        ff_emulated_edge_mc(s, src_cr, s->uvlinesize, 9, 9/*FIXME*/, (mx>>3), (my>>3), s->width>>1, s->height>>1);
            src_cr= s->edge_emu_buffer;
    }
    chroma_op(dest_cr, src_cr, s->uvlinesize, chroma_height, mx&7, my&7);
}

static inline void mc_part(H264Context *h, int n, int square, int chroma_height, int delta,
                           uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                           int x_offset, int y_offset,
                           qpel_mc_func *qpix_put, h264_chroma_mc_func chroma_put,
                           qpel_mc_func *qpix_avg, h264_chroma_mc_func chroma_avg,
                           int list0, int list1){
    MpegEncContext * const s = &h->s;
    qpel_mc_func *qpix_op=  qpix_put;
    h264_chroma_mc_func chroma_op= chroma_put;
    
    dest_y  += 2*x_offset + 2*y_offset*s->  linesize;
    dest_cb +=   x_offset +   y_offset*s->uvlinesize;
    dest_cr +=   x_offset +   y_offset*s->uvlinesize;
    x_offset += 8*s->mb_x;
    y_offset += 8*s->mb_y;
    
    if(list0){
        Picture *ref= &h->ref_list[0][ h->ref_cache[0][ scan8[n] ] ];
        mc_dir_part(h, ref, n, square, chroma_height, delta, 0,
                           dest_y, dest_cb, dest_cr, x_offset, y_offset,
                           qpix_op, chroma_op);

        qpix_op=  qpix_avg;
        chroma_op= chroma_avg;
    }

    if(list1){
        Picture *ref= &h->ref_list[1][ h->ref_cache[1][ scan8[n] ] ];
        mc_dir_part(h, ref, n, square, chroma_height, delta, 1,
                           dest_y, dest_cb, dest_cr, x_offset, y_offset,
                           qpix_op, chroma_op);
    }
}

static void hl_motion(H264Context *h, uint8_t *dest_y, uint8_t *dest_cb, uint8_t *dest_cr,
                      qpel_mc_func (*qpix_put)[16], h264_chroma_mc_func (*chroma_put),
                      qpel_mc_func (*qpix_avg)[16], h264_chroma_mc_func (*chroma_avg)){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
    const int mb_type= s->current_picture.mb_type[mb_xy];
    
    assert(IS_INTER(mb_type));
    
    if(IS_16X16(mb_type)){
        mc_part(h, 0, 1, 8, 0, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[0], chroma_put[0], qpix_avg[0], chroma_avg[0],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
    }else if(IS_16X8(mb_type)){
        mc_part(h, 0, 0, 4, 8, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        mc_part(h, 8, 0, 4, 8, dest_y, dest_cb, dest_cr, 0, 4,
                qpix_put[1], chroma_put[0], qpix_avg[1], chroma_avg[0],
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    }else if(IS_8X16(mb_type)){
        mc_part(h, 0, 0, 8, 8*s->linesize, dest_y, dest_cb, dest_cr, 0, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                IS_DIR(mb_type, 0, 0), IS_DIR(mb_type, 0, 1));
        mc_part(h, 4, 0, 8, 8*s->linesize, dest_y, dest_cb, dest_cr, 4, 0,
                qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                IS_DIR(mb_type, 1, 0), IS_DIR(mb_type, 1, 1));
    }else{
        int i;
        
        assert(IS_8X8(mb_type));

        for(i=0; i<4; i++){
            const int sub_mb_type= h->sub_mb_type[i];
            const int n= 4*i;
            int x_offset= (i&1)<<2;
            int y_offset= (i&2)<<1;

            if(IS_SUB_8X8(sub_mb_type)){
                mc_part(h, n, 1, 4, 0, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[1], chroma_put[1], qpix_avg[1], chroma_avg[1],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else if(IS_SUB_8X4(sub_mb_type)){
                mc_part(h, n  , 0, 2, 4, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                mc_part(h, n+2, 0, 2, 4, dest_y, dest_cb, dest_cr, x_offset, y_offset+2,
                    qpix_put[2], chroma_put[1], qpix_avg[2], chroma_avg[1],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else if(IS_SUB_4X8(sub_mb_type)){
                mc_part(h, n  , 0, 4, 4*s->linesize, dest_y, dest_cb, dest_cr, x_offset, y_offset,
                    qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                mc_part(h, n+1, 0, 4, 4*s->linesize, dest_y, dest_cb, dest_cr, x_offset+2, y_offset,
                    qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                    IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
            }else{
                int j;
                assert(IS_SUB_4X4(sub_mb_type));
                for(j=0; j<4; j++){
                    int sub_x_offset= x_offset + 2*(j&1);
                    int sub_y_offset= y_offset +   (j&2);
                    mc_part(h, n+j, 1, 2, 0, dest_y, dest_cb, dest_cr, sub_x_offset, sub_y_offset,
                        qpix_put[2], chroma_put[2], qpix_avg[2], chroma_avg[2],
                        IS_DIR(sub_mb_type, 0, 0), IS_DIR(sub_mb_type, 0, 1));
                }
            }
        }
    }
}

static void decode_init_vlc(H264Context *h){
    static int done = 0;

    if (!done) {
        int i;
        done = 1;

        init_vlc(&chroma_dc_coeff_token_vlc, CHROMA_DC_COEFF_TOKEN_VLC_BITS, 4*5, 
                 &chroma_dc_coeff_token_len [0], 1, 1,
                 &chroma_dc_coeff_token_bits[0], 1, 1);

        for(i=0; i<4; i++){
            init_vlc(&coeff_token_vlc[i], COEFF_TOKEN_VLC_BITS, 4*17, 
                     &coeff_token_len [i][0], 1, 1,
                     &coeff_token_bits[i][0], 1, 1);
        }

        for(i=0; i<3; i++){
            init_vlc(&chroma_dc_total_zeros_vlc[i], CHROMA_DC_TOTAL_ZEROS_VLC_BITS, 4,
                     &chroma_dc_total_zeros_len [i][0], 1, 1,
                     &chroma_dc_total_zeros_bits[i][0], 1, 1);
        }
        for(i=0; i<15; i++){
            init_vlc(&total_zeros_vlc[i], TOTAL_ZEROS_VLC_BITS, 16, 
                     &total_zeros_len [i][0], 1, 1,
                     &total_zeros_bits[i][0], 1, 1);
        }

        for(i=0; i<6; i++){
            init_vlc(&run_vlc[i], RUN_VLC_BITS, 7, 
                     &run_len [i][0], 1, 1,
                     &run_bits[i][0], 1, 1);
        }
        init_vlc(&run7_vlc, RUN7_VLC_BITS, 16, 
                 &run_len [6][0], 1, 1,
                 &run_bits[6][0], 1, 1);
    }
}

/**
 * Sets the intra prediction function pointers.
 */
static void init_pred_ptrs(H264Context *h){
//    MpegEncContext * const s = &h->s;

    h->pred4x4[VERT_PRED           ]= pred4x4_vertical_c;
    h->pred4x4[HOR_PRED            ]= pred4x4_horizontal_c;
    h->pred4x4[DC_PRED             ]= pred4x4_dc_c;
    h->pred4x4[DIAG_DOWN_LEFT_PRED ]= pred4x4_down_left_c;
    h->pred4x4[DIAG_DOWN_RIGHT_PRED]= pred4x4_down_right_c;
    h->pred4x4[VERT_RIGHT_PRED     ]= pred4x4_vertical_right_c;
    h->pred4x4[HOR_DOWN_PRED       ]= pred4x4_horizontal_down_c;
    h->pred4x4[VERT_LEFT_PRED      ]= pred4x4_vertical_left_c;
    h->pred4x4[HOR_UP_PRED         ]= pred4x4_horizontal_up_c;
    h->pred4x4[LEFT_DC_PRED        ]= pred4x4_left_dc_c;
    h->pred4x4[TOP_DC_PRED         ]= pred4x4_top_dc_c;
    h->pred4x4[DC_128_PRED         ]= pred4x4_128_dc_c;

    h->pred8x8[DC_PRED8x8     ]= pred8x8_dc_c;
    h->pred8x8[VERT_PRED8x8   ]= pred8x8_vertical_c;
    h->pred8x8[HOR_PRED8x8    ]= pred8x8_horizontal_c;
    h->pred8x8[PLANE_PRED8x8  ]= pred8x8_plane_c;
    h->pred8x8[LEFT_DC_PRED8x8]= pred8x8_left_dc_c;
    h->pred8x8[TOP_DC_PRED8x8 ]= pred8x8_top_dc_c;
    h->pred8x8[DC_128_PRED8x8 ]= pred8x8_128_dc_c;

    h->pred16x16[DC_PRED8x8     ]= pred16x16_dc_c;
    h->pred16x16[VERT_PRED8x8   ]= pred16x16_vertical_c;
    h->pred16x16[HOR_PRED8x8    ]= pred16x16_horizontal_c;
    h->pred16x16[PLANE_PRED8x8  ]= pred16x16_plane_c;
    h->pred16x16[LEFT_DC_PRED8x8]= pred16x16_left_dc_c;
    h->pred16x16[TOP_DC_PRED8x8 ]= pred16x16_top_dc_c;
    h->pred16x16[DC_128_PRED8x8 ]= pred16x16_128_dc_c;
}

//FIXME factorize
#define CHECKED_ALLOCZ(p, size)\
{\
    p= av_mallocz(size);\
    if(p==NULL){\
        perror("malloc");\
        goto fail;\
    }\
}

static void free_tables(H264Context *h){
    av_freep(&h->intra4x4_pred_mode);
    av_freep(&h->non_zero_count);
    av_freep(&h->slice_table_base);
    h->slice_table= NULL;
    
    av_freep(&h->mb2b_xy);
    av_freep(&h->mb2b8_xy);
}

/**
 * allocates tables.
 * needs widzh/height
 */
static int alloc_tables(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int big_mb_num= s->mb_stride * (s->mb_height+1);
    int x,y;

    CHECKED_ALLOCZ(h->intra4x4_pred_mode, big_mb_num * 8  * sizeof(uint8_t))
    CHECKED_ALLOCZ(h->non_zero_count    , big_mb_num * 16 * sizeof(uint8_t))
    CHECKED_ALLOCZ(h->slice_table_base  , big_mb_num * sizeof(uint8_t))

    memset(h->slice_table_base, -1, big_mb_num  * sizeof(uint8_t));
    h->slice_table= h->slice_table_base + s->mb_stride + 1;

    CHECKED_ALLOCZ(h->mb2b_xy  , big_mb_num * sizeof(uint16_t));
    CHECKED_ALLOCZ(h->mb2b8_xy , big_mb_num * sizeof(uint16_t));
    for(y=0; y<s->mb_height; y++){
        for(x=0; x<s->mb_width; x++){
            const int mb_xy= x + y*s->mb_stride;
            const int b_xy = 4*x + 4*y*h->b_stride;
            const int b8_xy= 2*x + 2*y*h->b8_stride;
        
            h->mb2b_xy [mb_xy]= b_xy;
            h->mb2b8_xy[mb_xy]= b8_xy;
        }
    }
    
    return 0;
fail:
    free_tables(h);
    return -1;
}

static void common_init(H264Context *h){
    MpegEncContext * const s = &h->s;

    s->width = s->avctx->width;
    s->height = s->avctx->height;
    s->codec_id= s->avctx->codec->id;
    
    init_pred_ptrs(h);

    s->decode=1; //FIXME
}

static int decode_init(AVCodecContext *avctx){
    H264Context *h= avctx->priv_data;
    MpegEncContext * const s = &h->s;

    s->avctx = avctx;
    common_init(h);

    s->out_format = FMT_H264;
    s->workaround_bugs= avctx->workaround_bugs;

    // set defaults
    s->progressive_sequence=1;
//    s->decode_mb= ff_h263_decode_mb;
    s->low_delay= 1;
    avctx->pix_fmt= PIX_FMT_YUV420P;

    decode_init_vlc(h);
    
    return 0;
}

static void frame_start(H264Context *h){
    MpegEncContext * const s = &h->s;
    int i;

    MPV_frame_start(s, s->avctx);
    ff_er_frame_start(s);
    h->mmco_index=0;

    assert(s->linesize && s->uvlinesize);

    for(i=0; i<16; i++){
        h->block_offset[i]= 4*((scan8[i] - scan8[0])&7) + 4*s->linesize*((scan8[i] - scan8[0])>>3);
        h->chroma_subblock_offset[i]= 2*((scan8[i] - scan8[0])&7) + 2*s->uvlinesize*((scan8[i] - scan8[0])>>3);
    }
    for(i=0; i<4; i++){
        h->block_offset[16+i]=
        h->block_offset[20+i]= 4*((scan8[i] - scan8[0])&7) + 4*s->uvlinesize*((scan8[i] - scan8[0])>>3);
    }

//    s->decode= (s->flags&CODEC_FLAG_PSNR) || !s->encoding || s->current_picture.reference /*|| h->contains_intra*/ || 1;
}

static void hl_decode_mb(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_x= s->mb_x;
    const int mb_y= s->mb_y;
    const int mb_xy= mb_x + mb_y*s->mb_stride;
    const int mb_type= s->current_picture.mb_type[mb_xy];
    uint8_t  *dest_y, *dest_cb, *dest_cr;
    int linesize, uvlinesize /*dct_offset*/;
    int i;

    if(!s->decode)
        return;

    if(s->mb_skiped){
    }

    dest_y  = s->current_picture.data[0] + (mb_y * 16* s->linesize  ) + mb_x * 16;
    dest_cb = s->current_picture.data[1] + (mb_y * 8 * s->uvlinesize) + mb_x * 8;
    dest_cr = s->current_picture.data[2] + (mb_y * 8 * s->uvlinesize) + mb_x * 8;

    if (h->mb_field_decoding_flag) {
        linesize = s->linesize * 2;
        uvlinesize = s->uvlinesize * 2;
        if(mb_y&1){ //FIXME move out of this func?
            dest_y -= s->linesize*15;
            dest_cb-= s->linesize*7;
            dest_cr-= s->linesize*7;
        }
    } else {
        linesize = s->linesize;
        uvlinesize = s->uvlinesize;
//        dct_offset = s->linesize * 16;
    }

    if(IS_INTRA(mb_type)){
        if(!(s->flags&CODEC_FLAG_GRAY)){
            h->pred8x8[ h->chroma_pred_mode ](dest_cb, uvlinesize);
            h->pred8x8[ h->chroma_pred_mode ](dest_cr, uvlinesize);
        }

        if(IS_INTRA4x4(mb_type)){
            if(!s->encoding){
                for(i=0; i<16; i++){
                    uint8_t * const ptr= dest_y + h->block_offset[i];
                    uint8_t *topright= ptr + 4 - linesize;
                    const int topright_avail= (h->topright_samples_available<<i)&0x8000;
                    const int dir= h->intra4x4_pred_mode_cache[ scan8[i] ];
                    int tr;

                    if(!topright_avail){
                        tr= ptr[3 - linesize]*0x01010101;
                        topright= (uint8_t*) &tr;
                    }

                    h->pred4x4[ dir ](ptr, topright, linesize);
                    if(h->non_zero_count_cache[ scan8[i] ]){
                        if(s->codec_id == CODEC_ID_H264)
                            h264_add_idct_c(ptr, h->mb + i*16, linesize);
                        else
                            svq3_add_idct_c(ptr, h->mb + i*16, linesize, s->qscale, 0);
                    }
                }
            }
        }else{
            h->pred16x16[ h->intra16x16_pred_mode ](dest_y , linesize);
            if(s->codec_id == CODEC_ID_H264)
                h264_luma_dc_dequant_idct_c(h->mb, s->qscale);
            else
                svq3_luma_dc_dequant_idct_c(h->mb, s->qscale);
        }
    }else if(s->codec_id == CODEC_ID_H264){
        hl_motion(h, dest_y, dest_cb, dest_cr,
                  s->dsp.put_h264_qpel_pixels_tab, s->dsp.put_h264_chroma_pixels_tab, 
                  s->dsp.avg_h264_qpel_pixels_tab, s->dsp.avg_h264_chroma_pixels_tab);
    }


    if(!IS_INTRA4x4(mb_type)){
        for(i=0; i<16; i++){
            if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){ //FIXME benchmark weird rule, & below
                uint8_t * const ptr= dest_y + h->block_offset[i];
                if(s->codec_id == CODEC_ID_H264)
                    h264_add_idct_c(ptr, h->mb + i*16, linesize);
                else
                    svq3_add_idct_c(ptr, h->mb + i*16, linesize, s->qscale, IS_INTRA(mb_type) ? 1 : 0);
            }
        }
    }

    if(!(s->flags&CODEC_FLAG_GRAY)){
        chroma_dc_dequant_idct_c(h->mb + 16*16, h->chroma_qp);
        chroma_dc_dequant_idct_c(h->mb + 16*16+4*16, h->chroma_qp);
        for(i=16; i<16+4; i++){
            if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){
                uint8_t * const ptr= dest_cb + h->block_offset[i];
                if(s->codec_id == CODEC_ID_H264)
                    h264_add_idct_c(ptr, h->mb + i*16, uvlinesize);
                else
                    svq3_add_idct_c(ptr, h->mb + i*16, uvlinesize, chroma_qp[s->qscale + 12] - 12, 2);
            }
        }
        for(i=20; i<20+4; i++){
            if(h->non_zero_count_cache[ scan8[i] ] || h->mb[i*16]){
                uint8_t * const ptr= dest_cr + h->block_offset[i];
                if(s->codec_id == CODEC_ID_H264)
                    h264_add_idct_c(ptr, h->mb + i*16, uvlinesize);
                else
                    svq3_add_idct_c(ptr, h->mb + i*16, uvlinesize, chroma_qp[s->qscale + 12] - 12, 2);
            }
        }
    }
}

static void decode_mb_cabac(H264Context *h){
//    MpegEncContext * const s = &h->s;
}

/**
 * fills the default_ref_list.
 */
static int fill_default_ref_list(H264Context *h){
    MpegEncContext * const s = &h->s;
    int i;
    Picture sorted_short_ref[16];
    
    if(h->slice_type==B_TYPE){
        int out_i;
        int limit= -1;

        for(out_i=0; out_i<h->short_ref_count; out_i++){
            int best_i=-1;
            int best_poc=-1;

            for(i=0; i<h->short_ref_count; i++){
                const int poc= h->short_ref[i]->poc;
                if(poc > limit && poc < best_poc){
                    best_poc= poc;
                    best_i= i;
                }
            }
            
            assert(best_i != -1);
            
            limit= best_poc;
            sorted_short_ref[out_i]= *h->short_ref[best_i];
        }
    }

    if(s->picture_structure == PICT_FRAME){
        if(h->slice_type==B_TYPE){
            const int current_poc= s->current_picture_ptr->poc;
            int list;

            for(list=0; list<2; list++){
                int index=0;

                for(i=0; i<h->short_ref_count && index < h->ref_count[list]; i++){
                    const int i2= list ? h->short_ref_count - i - 1 : i;
                    const int poc= sorted_short_ref[i2].poc;
                    
                    if(sorted_short_ref[i2].reference != 3) continue; //FIXME refernce field shit

                    if((list==1 && poc > current_poc) || (list==0 && poc < current_poc)){
                        h->default_ref_list[list][index  ]= sorted_short_ref[i2];
                        h->default_ref_list[list][index++].pic_id= sorted_short_ref[i2].frame_num;
                    }
                }

                for(i=0; i<h->long_ref_count && index < h->ref_count[ list ]; i++){
                    if(h->long_ref[i]->reference != 3) continue;

                    h->default_ref_list[ list ][index  ]= *h->long_ref[i];
                    h->default_ref_list[ list ][index++].pic_id= i;;
                }
                
                if(h->long_ref_count > 1 && h->short_ref_count==0){
                    Picture temp= h->default_ref_list[1][0];
                    h->default_ref_list[1][0] = h->default_ref_list[1][1];
                    h->default_ref_list[1][0] = temp;
                }

                if(index < h->ref_count[ list ])
                    memset(&h->default_ref_list[list][index], 0, sizeof(Picture)*(h->ref_count[ list ] - index));
            }
        }else{
            int index=0;
            for(i=0; i<h->short_ref_count && index < h->ref_count[0]; i++){
                if(h->short_ref[i]->reference != 3) continue; //FIXME refernce field shit
                h->default_ref_list[0][index  ]= *h->short_ref[i];
                h->default_ref_list[0][index++].pic_id= h->short_ref[i]->frame_num;
            }
            for(i=0; i<h->long_ref_count && index < h->ref_count[0]; i++){
                if(h->long_ref[i]->reference != 3) continue;
                h->default_ref_list[0][index  ]= *h->long_ref[i];
                h->default_ref_list[0][index++].pic_id= i;;
            }
            if(index < h->ref_count[0])
                memset(&h->default_ref_list[0][index], 0, sizeof(Picture)*(h->ref_count[0] - index));
        }
    }else{ //FIELD
        if(h->slice_type==B_TYPE){
        }else{
            //FIXME second field balh
        }
    }
    return 0;
}

static int decode_ref_pic_list_reordering(H264Context *h){
    MpegEncContext * const s = &h->s;
    int list;
    
    if(h->slice_type==I_TYPE || h->slice_type==SI_TYPE) return 0; //FIXME move beofre func
    
    for(list=0; list<2; list++){
        memcpy(h->ref_list[list], h->default_ref_list[list], sizeof(Picture)*h->ref_count[list]);

        if(get_bits1(&s->gb)){
            int pred= h->curr_pic_num;
            int index;

            for(index=0; ; index++){
                int reordering_of_pic_nums_idc= get_ue_golomb(&s->gb);
                int pic_id;
                int i;
                
                
                if(index >= h->ref_count[list]){
                    fprintf(stderr, "reference count overflow\n");
                    return -1;
                }
                
                if(reordering_of_pic_nums_idc<3){
                    if(reordering_of_pic_nums_idc<2){
                        const int abs_diff_pic_num= get_ue_golomb(&s->gb) + 1;

                        if(abs_diff_pic_num >= h->max_pic_num){
                            fprintf(stderr, "abs_diff_pic_num overflow\n");
                            return -1;
                        }

                        if(reordering_of_pic_nums_idc == 0) pred-= abs_diff_pic_num;
                        else                                pred+= abs_diff_pic_num;
                        pred &= h->max_pic_num - 1;
                    
                        for(i= h->ref_count[list]-1; i>=index; i--){
                            if(h->ref_list[list][i].pic_id == pred && h->ref_list[list][i].long_ref==0)
                                break;
                        }
                    }else{
                        pic_id= get_ue_golomb(&s->gb); //long_term_pic_idx

                        for(i= h->ref_count[list]-1; i>=index; i--){
                            if(h->ref_list[list][i].pic_id == pic_id && h->ref_list[list][i].long_ref==1)
                                break;
                        }
                    }

                    if(i < index){
                        fprintf(stderr, "reference picture missing during reorder\n");
                        memset(&h->ref_list[list][index], 0, sizeof(Picture)); //FIXME
                    }else if(i > index){
                        Picture tmp= h->ref_list[list][i];
                        for(; i>index; i--){
                            h->ref_list[list][i]= h->ref_list[list][i-1];
                        }
                        h->ref_list[list][index]= tmp;
                    }
                }else if(reordering_of_pic_nums_idc==3) 
                    break;
                else{
                    fprintf(stderr, "illegal reordering_of_pic_nums_idc\n");
                    return -1;
                }
            }
        }

        if(h->slice_type!=B_TYPE) break;
    }
    return 0;    
}

static int pred_weight_table(H264Context *h){
    MpegEncContext * const s = &h->s;
    int list, i;
    
    h->luma_log2_weight_denom= get_ue_golomb(&s->gb);
    h->chroma_log2_weight_denom= get_ue_golomb(&s->gb);

    for(list=0; list<2; list++){
        for(i=0; i<h->ref_count[list]; i++){
            int luma_weight_flag, chroma_weight_flag;
            
            luma_weight_flag= get_bits1(&s->gb);
            if(luma_weight_flag){
                h->luma_weight[list][i]= get_se_golomb(&s->gb);
                h->luma_offset[list][i]= get_se_golomb(&s->gb);
            }

            chroma_weight_flag= get_bits1(&s->gb);
            if(chroma_weight_flag){
                int j;
                for(j=0; j<2; j++){
                    h->chroma_weight[list][i][j]= get_se_golomb(&s->gb);
                    h->chroma_offset[list][i][j]= get_se_golomb(&s->gb);
                }
            }
        }
        if(h->slice_type != B_TYPE) break;
    }
    return 0;
}

/**
 * instantaneos decoder refresh.
 */
static void idr(H264Context *h){
    int i;

    for(i=0; i<h->long_ref_count; i++){
        h->long_ref[i]->reference=0;
        h->long_ref[i]= NULL;
    }
    h->long_ref_count=0;

    for(i=0; i<h->short_ref_count; i++){
        h->short_ref[i]->reference=0;
        h->short_ref[i]= NULL;
    }
    h->short_ref_count=0;
}

/**
 *
 * @return the removed picture or NULL if an error occures
 */
static Picture * remove_short(H264Context *h, int frame_num){
    MpegEncContext * const s = &h->s;
    int i;
    
    if(s->avctx->debug&FF_DEBUG_MMCO)
        printf("remove short %d count %d\n", frame_num, h->short_ref_count);
    
    for(i=0; i<h->short_ref_count; i++){
        Picture *pic= h->short_ref[i];
        if(s->avctx->debug&FF_DEBUG_MMCO)
            printf("%d %d %X\n", i, pic->frame_num, (int)pic);
        if(pic->frame_num == frame_num){
            h->short_ref[i]= NULL;
            memmove(&h->short_ref[i], &h->short_ref[i+1], (h->short_ref_count - i - 1)*sizeof(Picture*));
            h->short_ref_count--;
            return pic;
        }
    }
    return NULL;
}

/**
 *
 * @return the removed picture or NULL if an error occures
 */
static Picture * remove_long(H264Context *h, int i){
    Picture *pic;

    if(i >= h->long_ref_count) return NULL;
    pic= h->long_ref[i];
    if(pic==NULL) return NULL;
    
    h->long_ref[i]= NULL;
    memmove(&h->long_ref[i], &h->long_ref[i+1], (h->long_ref_count - i - 1)*sizeof(Picture*));
    h->long_ref_count--;

    return pic;
}

/**
 * Executes the reference picture marking (memory management control operations).
 */
static int execute_ref_pic_marking(H264Context *h, MMCO *mmco, int mmco_count){
    MpegEncContext * const s = &h->s;
    int i;
    int current_is_long=0;
    Picture *pic;
    
    if((s->avctx->debug&FF_DEBUG_MMCO) && mmco_count==0)
        printf("no mmco here\n");
        
    for(i=0; i<mmco_count; i++){
        if(s->avctx->debug&FF_DEBUG_MMCO)
            printf("mmco:%d %d %d\n", h->mmco[i].opcode, h->mmco[i].short_frame_num, h->mmco[i].long_index);

        switch(mmco[i].opcode){
        case MMCO_SHORT2UNUSED:
            pic= remove_short(h, mmco[i].short_frame_num);
            if(pic==NULL) return -1;
            pic->reference= 0;
            break;
        case MMCO_SHORT2LONG:
            pic= remove_long(h, mmco[i].long_index);
            if(pic) pic->reference=0;
            
            h->long_ref[ mmco[i].long_index ]= remove_short(h, mmco[i].short_frame_num);
            h->long_ref[ mmco[i].long_index ]->long_ref=1;
            break;
        case MMCO_LONG2UNUSED:
            pic= remove_long(h, mmco[i].long_index);
            if(pic==NULL) return -1;
            pic->reference= 0;
            break;
        case MMCO_LONG:
            pic= remove_long(h, mmco[i].long_index);
            if(pic) pic->reference=0;
            
            h->long_ref[ mmco[i].long_index ]= s->current_picture_ptr;
            h->long_ref[ mmco[i].long_index ]->long_ref=1;
            h->long_ref_count++;
            
            current_is_long=1;
            break;
        case MMCO_SET_MAX_LONG:
            assert(mmco[i].long_index <= 16);
            while(mmco[i].long_index < h->long_ref_count){
                pic= remove_long(h, mmco[i].long_index);
                pic->reference=0;
            }
            while(mmco[i].long_index > h->long_ref_count){
                h->long_ref[ h->long_ref_count++ ]= NULL;
            }
            break;
        case MMCO_RESET:
            while(h->short_ref_count){
                pic= remove_short(h, h->short_ref[0]->frame_num);
                pic->reference=0;
            }
            while(h->long_ref_count){
                pic= remove_long(h, h->long_ref_count-1);
                pic->reference=0;
            }
            break;
        default: assert(0);
        }
    }
    
    if(!current_is_long){
        pic= remove_short(h, s->current_picture_ptr->frame_num);
        if(pic){
            pic->reference=0;
            fprintf(stderr, "illegal short term buffer state detected\n");
        }
        
        if(h->short_ref_count)
            memmove(&h->short_ref[1], &h->short_ref[0], h->short_ref_count*sizeof(Picture*));

        h->short_ref[0]= s->current_picture_ptr;
        h->short_ref[0]->long_ref=0;
        h->short_ref_count++;
    }
    
    return 0; 
}

static int decode_ref_pic_marking(H264Context *h){
    MpegEncContext * const s = &h->s;
    int i;
    
    if(h->nal_unit_type == NAL_IDR_SLICE){ //FIXME fields
        s->broken_link= get_bits1(&s->gb) -1;
        h->mmco[0].long_index= get_bits1(&s->gb) - 1; // current_long_term_idx
        if(h->mmco[0].long_index == -1)
            h->mmco_index= 0;
        else{
            h->mmco[0].opcode= MMCO_LONG;
            h->mmco_index= 1;
        } 
    }else{
        if(get_bits1(&s->gb)){ // adaptive_ref_pic_marking_mode_flag
            for(i= h->mmco_index; i<MAX_MMCO_COUNT; i++) { 
                MMCOOpcode opcode= get_ue_golomb(&s->gb);;

                h->mmco[i].opcode= opcode;
                if(opcode==MMCO_SHORT2UNUSED || opcode==MMCO_SHORT2LONG){
                    h->mmco[i].short_frame_num= (h->frame_num - get_ue_golomb(&s->gb) - 1) & ((1<<h->sps.log2_max_frame_num)-1); //FIXME fields
/*                    if(h->mmco[i].short_frame_num >= h->short_ref_count || h->short_ref[ h->mmco[i].short_frame_num ] == NULL){
                        fprintf(stderr, "illegal short ref in memory management control operation %d\n", mmco);
                        return -1;
                    }*/
                }
                if(opcode==MMCO_SHORT2LONG || opcode==MMCO_LONG2UNUSED || opcode==MMCO_LONG || opcode==MMCO_SET_MAX_LONG){
                    h->mmco[i].long_index= get_ue_golomb(&s->gb);
                    if(/*h->mmco[i].long_index >= h->long_ref_count || h->long_ref[ h->mmco[i].long_index ] == NULL*/ h->mmco[i].long_index >= 16){
                        fprintf(stderr, "illegal long ref in memory management control operation %d\n", opcode);
                        return -1;
                    }
                }
                    
                if(opcode > MMCO_LONG){
                    fprintf(stderr, "illegal memory management control operation %d\n", opcode);
                    return -1;
                }
            }
            h->mmco_index= i;
        }else{
            assert(h->long_ref_count + h->short_ref_count <= h->sps.ref_frame_count);

            if(h->long_ref_count + h->short_ref_count == h->sps.ref_frame_count){ //FIXME fields
                h->mmco[0].opcode= MMCO_SHORT2UNUSED;
                h->mmco[0].short_frame_num= h->short_ref[ h->short_ref_count - 1 ]->frame_num;
                h->mmco_index= 1;
            }else
                h->mmco_index= 0;
        }
    }
    
    return 0; 
}

static int init_poc(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int max_frame_num= 1<<h->sps.log2_max_frame_num;
    int field_poc[2];

    if(h->nal_unit_type == NAL_IDR_SLICE){
        h->frame_num_offset= 0;
    }else{
        if(h->frame_num < h->prev_frame_num)
            h->frame_num_offset= h->prev_frame_num_offset + max_frame_num;
        else
            h->frame_num_offset= h->prev_frame_num_offset;
    }

    if(h->sps.poc_type==0){
        const int max_poc_lsb= 1<<h->sps.log2_max_poc_lsb;

        if     (h->poc_lsb < h->prev_poc_lsb && h->prev_poc_lsb - h->poc_lsb >= max_poc_lsb/2)
            h->poc_msb = h->prev_poc_msb + max_poc_lsb;
        else if(h->poc_lsb > h->prev_poc_lsb && h->prev_poc_lsb - h->poc_lsb < -max_poc_lsb/2)
            h->poc_msb = h->prev_poc_msb - max_poc_lsb;
        else
            h->poc_msb = h->prev_poc_msb;
//printf("poc: %d %d\n", h->poc_msb, h->poc_lsb);
        field_poc[0] = 
        field_poc[1] = h->poc_msb + h->poc_lsb;
        if(s->picture_structure == PICT_FRAME) 
            field_poc[1] += h->delta_poc_bottom;
    }else if(h->sps.poc_type==1){
        int abs_frame_num, expected_delta_per_poc_cycle, expectedpoc;
        int i;

        if(h->sps.poc_cycle_length != 0)
            abs_frame_num = h->frame_num_offset + h->frame_num;
        else
            abs_frame_num = 0;

        if(h->nal_ref_idc==0 && abs_frame_num > 0)
            abs_frame_num--;
            
        expected_delta_per_poc_cycle = 0;
        for(i=0; i < h->sps.poc_cycle_length; i++)
            expected_delta_per_poc_cycle += h->sps.offset_for_ref_frame[ i ]; //FIXME integrate during sps parse

        if(abs_frame_num > 0){
            int poc_cycle_cnt          = (abs_frame_num - 1) / h->sps.poc_cycle_length;
            int frame_num_in_poc_cycle = (abs_frame_num - 1) % h->sps.poc_cycle_length;

            expectedpoc = poc_cycle_cnt * expected_delta_per_poc_cycle;
            for(i = 0; i <= frame_num_in_poc_cycle; i++)
                expectedpoc = expectedpoc + h->sps.offset_for_ref_frame[ i ];
        } else
            expectedpoc = 0;

        if(h->nal_ref_idc == 0) 
            expectedpoc = expectedpoc + h->sps.offset_for_non_ref_pic;
        
        field_poc[0] = expectedpoc + h->delta_poc[0];
        field_poc[1] = field_poc[0] + h->sps.offset_for_top_to_bottom_field;

        if(s->picture_structure == PICT_FRAME)
            field_poc[1] += h->delta_poc[1];
    }else{
        int poc;
        if(h->nal_unit_type == NAL_IDR_SLICE){
            poc= 0;
        }else{
            if(h->nal_ref_idc) poc= 2*(h->frame_num_offset + h->frame_num);
            else               poc= 2*(h->frame_num_offset + h->frame_num) - 1;
        }
        field_poc[0]= poc;
        field_poc[1]= poc;
    }
    
    if(s->picture_structure != PICT_BOTTOM_FIELD)
        s->current_picture_ptr->field_poc[0]= field_poc[0];
    if(s->picture_structure != PICT_TOP_FIELD)
        s->current_picture_ptr->field_poc[1]= field_poc[1];
    if(s->picture_structure == PICT_FRAME) // FIXME field pix?
        s->current_picture_ptr->poc= FFMIN(field_poc[0], field_poc[1]);

    return 0;
}

/**
 * decodes a slice header.
 * this will allso call MPV_common_init() and frame_start() as needed
 */
static int decode_slice_header(H264Context *h){
    MpegEncContext * const s = &h->s;
    int first_mb_in_slice, pps_id;
    int num_ref_idx_active_override_flag;
    static const uint8_t slice_type_map[5]= {P_TYPE, B_TYPE, I_TYPE, SP_TYPE, SI_TYPE};
    float new_aspect;

    s->current_picture.reference= h->nal_ref_idc != 0;

    first_mb_in_slice= get_ue_golomb(&s->gb);

    h->slice_type= get_ue_golomb(&s->gb);
    if(h->slice_type > 9){
        fprintf(stderr, "slice type too large (%d) at %d %d\n", h->slice_type, s->mb_x, s->mb_y);
    }
    if(h->slice_type > 4){
        h->slice_type -= 5;
        h->slice_type_fixed=1;
    }else
        h->slice_type_fixed=0;
    
    h->slice_type= slice_type_map[ h->slice_type ];
    
    s->pict_type= h->slice_type; // to make a few old func happy, its wrong though
        
    pps_id= get_ue_golomb(&s->gb);
    if(pps_id>255){
        fprintf(stderr, "pps_id out of range\n");
        return -1;
    }
    h->pps= h->pps_buffer[pps_id];
    if(h->pps.slice_group_count == 0){
        fprintf(stderr, "non existing PPS referenced\n");
        return -1;
    }

    h->sps= h->sps_buffer[ h->pps.sps_id ];
    if(h->sps.log2_max_frame_num == 0){
        fprintf(stderr, "non existing SPS referenced\n");
        return -1;
    }
    
    s->mb_width= h->sps.mb_width;
    s->mb_height= h->sps.mb_height;
    
    h->b_stride=  s->mb_width*4;
    h->b8_stride= s->mb_width*2;

    s->mb_x = first_mb_in_slice % s->mb_width;
    s->mb_y = first_mb_in_slice / s->mb_width; //FIXME AFFW
    
    s->width = 16*s->mb_width - 2*(h->pps.crop_left + h->pps.crop_right );
    if(h->sps.frame_mbs_only_flag)
        s->height= 16*s->mb_height - 2*(h->pps.crop_top  + h->pps.crop_bottom);
    else
        s->height= 16*s->mb_height - 4*(h->pps.crop_top  + h->pps.crop_bottom); //FIXME recheck
    
    if(h->pps.crop_left || h->pps.crop_top){
        fprintf(stderr, "insane croping not completly supported, this could look slightly wrong ...\n");
    }

    if(s->aspected_height) //FIXME emms at end of slice ?
        new_aspect= h->sps.sar_width*s->width / (float)(s->height*h->sps.sar_height);
    else
        new_aspect=0;

    if (s->context_initialized 
        && (   s->width != s->avctx->width || s->height != s->avctx->height 
            || ABS(new_aspect - s->avctx->aspect_ratio) > 0.001)) {
        free_tables(h);
        MPV_common_end(s);
    }
    if (!s->context_initialized) {
        if (MPV_common_init(s) < 0)
            return -1;

        alloc_tables(h);

        s->avctx->width = s->width;
        s->avctx->height = s->height;
        s->avctx->aspect_ratio= new_aspect;
    }

    if(first_mb_in_slice == 0){
        frame_start(h);
    }

    s->current_picture_ptr->frame_num= //FIXME frame_num cleanup
    h->frame_num= get_bits(&s->gb, h->sps.log2_max_frame_num);

    if(h->sps.frame_mbs_only_flag){
        s->picture_structure= PICT_FRAME;
    }else{
        if(get_bits1(&s->gb)) //field_pic_flag
            s->picture_structure= PICT_TOP_FIELD + get_bits1(&s->gb); //bottom_field_flag
        else
            s->picture_structure= PICT_FRAME;
    }

    if(s->picture_structure==PICT_FRAME){
        h->curr_pic_num=   h->frame_num;
        h->max_pic_num= 1<< h->sps.log2_max_frame_num;
    }else{
        h->curr_pic_num= 2*h->frame_num;
        h->max_pic_num= 1<<(h->sps.log2_max_frame_num + 1);
    }
        
    if(h->nal_unit_type == NAL_IDR_SLICE){
        int idr_pic_id= get_ue_golomb(&s->gb);
    }
   
    if(h->sps.poc_type==0){
        h->poc_lsb= get_bits(&s->gb, h->sps.log2_max_poc_lsb);
        
        if(h->pps.pic_order_present==1 && s->picture_structure==PICT_FRAME){
            h->delta_poc_bottom= get_se_golomb(&s->gb);
        }
    }
    
    if(h->sps.poc_type==1 && !h->sps.delta_pic_order_always_zero_flag){
        h->delta_poc[0]= get_se_golomb(&s->gb);
        
        if(h->pps.pic_order_present==1 && s->picture_structure==PICT_FRAME)
            h->delta_poc[1]= get_se_golomb(&s->gb);
    }
    
    init_poc(h);
    
    if(h->pps.redundant_pic_cnt_present){
        h->redundant_pic_count= get_ue_golomb(&s->gb);
    }

    //set defaults, might be overriden a few line later
    h->ref_count[0]= h->pps.ref_count[0];
    h->ref_count[1]= h->pps.ref_count[1];

    if(h->slice_type == P_TYPE || h->slice_type == SP_TYPE || h->slice_type == B_TYPE){
        if(h->slice_type == B_TYPE){
            h->direct_spatial_mv_pred= get_bits1(&s->gb);
        }
        num_ref_idx_active_override_flag= get_bits1(&s->gb);
    
        if(num_ref_idx_active_override_flag){
            h->ref_count[0]= get_ue_golomb(&s->gb) + 1;
            if(h->slice_type==B_TYPE)
                h->ref_count[1]= get_ue_golomb(&s->gb) + 1;

            if(h->ref_count[0] > 32 || h->ref_count[1] > 32){
                fprintf(stderr, "reference overflow\n");
                return -1;
            }
        }
    }

    if(first_mb_in_slice == 0){
        fill_default_ref_list(h);
    }

    decode_ref_pic_list_reordering(h);

    if(   (h->pps.weighted_pred          && (h->slice_type == P_TYPE || h->slice_type == SP_TYPE )) 
       || (h->pps.weighted_bipred_idc==1 && h->slice_type==B_TYPE ) )
        pred_weight_table(h);
    
    if(s->current_picture.reference)
        decode_ref_pic_marking(h);
    //FIXME CABAC stuff

    s->qscale = h->pps.init_qp + get_se_golomb(&s->gb); //slice_qp_delta
    //FIXME qscale / qp ... stuff
    if(h->slice_type == SP_TYPE){
        int sp_for_switch_flag= get_bits1(&s->gb);
    }
    if(h->slice_type==SP_TYPE || h->slice_type == SI_TYPE){
        int slice_qs_delta= get_se_golomb(&s->gb);
    }

    if( h->pps.deblocking_filter_parameters_present ) {
        h->disable_deblocking_filter_idc= get_ue_golomb(&s->gb);
        if( h->disable_deblocking_filter_idc  !=  1 ) {
            h->slice_alpha_c0_offset_div2= get_se_golomb(&s->gb);
            h->slice_beta_offset_div2= get_se_golomb(&s->gb);
        }
    }else
        h->disable_deblocking_filter_idc= 0;

#if 0 //FMO
    if( h->pps.num_slice_groups > 1  && h->pps.mb_slice_group_map_type >= 3 && h->pps.mb_slice_group_map_type <= 5)
        slice_group_change_cycle= get_bits(&s->gb, ?);
#endif

    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        printf("mb:%d %c pps:%d frame:%d poc:%d/%d ref:%d/%d qp:%d loop:%d\n", 
               first_mb_in_slice, 
               ff_get_pict_type_char(h->slice_type),
               pps_id, h->frame_num,
               s->current_picture_ptr->field_poc[0], s->current_picture_ptr->field_poc[1],
               h->ref_count[0], h->ref_count[1],
               s->qscale,
               h->disable_deblocking_filter_idc
               );
    }

    return 0;
}

/**
 *
 */
static inline int get_level_prefix(GetBitContext *gb){
    unsigned int buf;
    int log;
    
    OPEN_READER(re, gb);
    UPDATE_CACHE(re, gb);
    buf=GET_CACHE(re, gb);
    
    log= 32 - av_log2(buf);
#ifdef TRACE
    print_bin(buf>>(32-log), log);
    printf("%5d %2d %3d lpr @%5d in %s get_level_prefix\n", buf>>(32-log), log, log-1, get_bits_count(gb), __FILE__);
#endif

    LAST_SKIP_BITS(re, gb, log);
    CLOSE_READER(re, gb);

    return log-1;
}

/**
 * decodes a residual block.
 * @param n block index
 * @param scantable scantable
 * @param max_coeff number of coefficients in the block
 * @return <0 if an error occured
 */
static int decode_residual(H264Context *h, GetBitContext *gb, DCTELEM *block, int n, const uint8_t *scantable, int qp, int max_coeff){
    MpegEncContext * const s = &h->s;
    const uint16_t *qmul= dequant_coeff[qp];
    static const int coeff_token_table_index[17]= {0, 0, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 3};
    int level[16], run[16];
    int suffix_length, zeros_left, coeff_num, coeff_token, total_coeff, i, trailing_ones;

    //FIXME put trailing_onex into the context

    if(n == CHROMA_DC_BLOCK_INDEX){
        coeff_token= get_vlc2(gb, chroma_dc_coeff_token_vlc.table, CHROMA_DC_COEFF_TOKEN_VLC_BITS, 1);
        total_coeff= coeff_token>>2;
    }else{    
        if(n == LUMA_DC_BLOCK_INDEX){
            total_coeff= pred_non_zero_count(h, 0);
            coeff_token= get_vlc2(gb, coeff_token_vlc[ coeff_token_table_index[total_coeff] ].table, COEFF_TOKEN_VLC_BITS, 2);
            total_coeff= coeff_token>>2;
        }else{
            total_coeff= pred_non_zero_count(h, n);
            coeff_token= get_vlc2(gb, coeff_token_vlc[ coeff_token_table_index[total_coeff] ].table, COEFF_TOKEN_VLC_BITS, 2);
            total_coeff= coeff_token>>2;
            h->non_zero_count_cache[ scan8[n] ]= total_coeff;
        }
    }

    //FIXME set last_non_zero?

    if(total_coeff==0)
        return 0;
        
    trailing_ones= coeff_token&3;
    tprintf("trailing:%d, total:%d\n", trailing_ones, total_coeff);
    assert(total_coeff<=16);
    
    for(i=0; i<trailing_ones; i++){
        level[i]= 1 - 2*get_bits1(gb);
    }

    suffix_length= total_coeff > 10 && trailing_ones < 3;

    for(; i<total_coeff; i++){
        const int prefix= get_level_prefix(gb);
        int level_code, mask;

        if(prefix<14){ //FIXME try to build a large unified VLC table for all this
            if(suffix_length)
                level_code= (prefix<<suffix_length) + get_bits(gb, suffix_length); //part
            else
                level_code= (prefix<<suffix_length); //part
        }else if(prefix==14){
            if(suffix_length)
                level_code= (prefix<<suffix_length) + get_bits(gb, suffix_length); //part
            else
                level_code= prefix + get_bits(gb, 4); //part
        }else if(prefix==15){
            level_code= (prefix<<suffix_length) + get_bits(gb, 12); //part
            if(suffix_length==0) level_code+=15; //FIXME doesnt make (much)sense
        }else{
            fprintf(stderr, "prefix too large at %d %d\n", s->mb_x, s->mb_y);
            return -1;
        }

        if(i==trailing_ones && i<3) level_code+= 2; //FIXME split first iteration

        mask= -(level_code&1);
        level[i]= (((2+level_code)>>1) ^ mask) - mask;

        if(suffix_length==0) suffix_length=1; //FIXME split first iteration

#if 1
        if(ABS(level[i]) > (3<<(suffix_length-1)) && suffix_length<6) suffix_length++;
#else        
        if((2+level_code)>>1) > (3<<(suffix_length-1)) && suffix_length<6) suffix_length++;
        ? == prefix > 2 or sth
#endif
        tprintf("level: %d suffix_length:%d\n", level[i], suffix_length);
    }

    if(total_coeff == max_coeff)
        zeros_left=0;
    else{
        if(n == CHROMA_DC_BLOCK_INDEX)
            zeros_left= get_vlc2(gb, chroma_dc_total_zeros_vlc[ total_coeff-1 ].table, CHROMA_DC_TOTAL_ZEROS_VLC_BITS, 1);
        else
            zeros_left= get_vlc2(gb, total_zeros_vlc[ total_coeff-1 ].table, TOTAL_ZEROS_VLC_BITS, 1);
    }
    
    for(i=0; i<total_coeff-1; i++){
        if(zeros_left <=0)
            break;
        else if(zeros_left < 7){
            run[i]= get_vlc2(gb, run_vlc[zeros_left-1].table, RUN_VLC_BITS, 1);
        }else{
            run[i]= get_vlc2(gb, run7_vlc.table, RUN7_VLC_BITS, 2);
        }
        zeros_left -= run[i];
    }

    if(zeros_left<0){
        fprintf(stderr, "negative number of zero coeffs at %d %d\n", s->mb_x, s->mb_y);
        return -1;
    }
    
    for(; i<total_coeff-1; i++){
        run[i]= 0;
    }

    run[i]= zeros_left;

    coeff_num=-1;
    if(n > 24){
        for(i=total_coeff-1; i>=0; i--){ //FIXME merge into rundecode?
            int j;

            coeff_num += run[i] + 1; //FIXME add 1 earlier ?
            j= scantable[ coeff_num ];

            block[j]= level[i];
        }
    }else{
        for(i=total_coeff-1; i>=0; i--){ //FIXME merge into  rundecode?
            int j;

            coeff_num += run[i] + 1; //FIXME add 1 earlier ?
            j= scantable[ coeff_num ];

            block[j]= level[i] * qmul[j];
//            printf("%d %d  ", block[j], qmul[j]);
        }
    }
    return 0;
}

/**
 * decodes a macroblock
 * @returns 0 if ok, AC_ERROR / DC_ERROR / MV_ERROR if an error is noticed
 */
static int decode_mb(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;
    int mb_type, partition_count, cbp;

    memset(h->mb, 0, sizeof(int16_t)*24*16); //FIXME avoid if allready clear (move after skip handlong?

    tprintf("pic:%d mb:%d/%d\n", h->frame_num, s->mb_x, s->mb_y);

    if(h->slice_type != I_TYPE && h->slice_type != SI_TYPE){
        if(s->mb_skip_run==-1)
            s->mb_skip_run= get_ue_golomb(&s->gb);
        
        if (s->mb_skip_run--) {
            int mx, my;
            /* skip mb */
//FIXME b frame
            mb_type= MB_TYPE_16x16|MB_TYPE_P0L0|MB_TYPE_P1L0;

            memset(h->non_zero_count[mb_xy], 0, 16);
            memset(h->non_zero_count_cache + 8, 0, 8*5); //FIXME ugly, remove pfui

            if(h->sps.mb_aff && s->mb_skip_run==0 && (s->mb_y&1)==0){
                h->mb_field_decoding_flag= get_bits1(&s->gb);
            }

            if(h->mb_field_decoding_flag)
                mb_type|= MB_TYPE_INTERLACED;
            
            fill_caches(h, mb_type); //FIXME check what is needed and what not ...
            pred_pskip_motion(h, &mx, &my);
            fill_rectangle(&h->ref_cache[0][scan8[0]], 4, 4, 8, 0, 1);
            fill_rectangle(  h->mv_cache[0][scan8[0]], 4, 4, 8, (mx&0xFFFF)+(my<<16), 4);
            write_back_motion(h, mb_type);

            s->current_picture.mb_type[mb_xy]= mb_type; //FIXME SKIP type
            h->slice_table[ mb_xy ]= h->slice_num;

            h->prev_mb_skiped= 1;
            return 0;
        }
    }
    if(h->sps.mb_aff /* && !field pic FIXME needed? */){
        if((s->mb_y&1)==0)
            h->mb_field_decoding_flag = get_bits1(&s->gb);
    }else
        h->mb_field_decoding_flag=0; //FIXME som ed note ?!
    
    h->prev_mb_skiped= 0;
    
    mb_type= get_ue_golomb(&s->gb);
    if(h->slice_type == B_TYPE){
        if(mb_type < 23){
            partition_count= b_mb_type_info[mb_type].partition_count;
            mb_type=         b_mb_type_info[mb_type].type;
        }else{
            mb_type -= 23;
            goto decode_intra_mb;
        }
    }else if(h->slice_type == P_TYPE /*|| h->slice_type == SP_TYPE */){
        if(mb_type < 5){
            partition_count= p_mb_type_info[mb_type].partition_count;
            mb_type=         p_mb_type_info[mb_type].type;
        }else{
            mb_type -= 5;
            goto decode_intra_mb;
        }
    }else{
       assert(h->slice_type == I_TYPE);
decode_intra_mb:
        if(mb_type > 25){
            fprintf(stderr, "mb_type %d in %c slice to large at %d %d\n", mb_type, ff_get_pict_type_char(h->slice_type), s->mb_x, s->mb_y);
            return -1;
        }
        partition_count=0;
        cbp= i_mb_type_info[mb_type].cbp;
        h->intra16x16_pred_mode= i_mb_type_info[mb_type].pred_mode;
        mb_type= i_mb_type_info[mb_type].type;
    }

    if(h->mb_field_decoding_flag)
        mb_type |= MB_TYPE_INTERLACED;

    s->current_picture.mb_type[mb_xy]= mb_type;
    h->slice_table[ mb_xy ]= h->slice_num;
    
    if(IS_INTRA_PCM(mb_type)){
        const uint8_t *ptr;
        int x, y;
        
        // we assume these blocks are very rare so we dont optimize it
        align_get_bits(&s->gb);
        
        ptr= s->gb.buffer + get_bits_count(&s->gb);
    
        for(y=0; y<16; y++){
            const int index= 4*(y&3) + 64*(y>>2);
            for(x=0; x<16; x++){
                h->mb[index + (x&3) + 16*(x>>2)]= *(ptr++);
            }
        }
        for(y=0; y<8; y++){
            const int index= 256 + 4*(y&3) + 32*(y>>2);
            for(x=0; x<8; x++){
                h->mb[index + (x&3) + 16*(x>>2)]= *(ptr++);
            }
        }
        for(y=0; y<8; y++){
            const int index= 256 + 64 + 4*(y&3) + 32*(y>>2);
            for(x=0; x<8; x++){
                h->mb[index + (x&3) + 16*(x>>2)]= *(ptr++);
            }
        }
    
        skip_bits(&s->gb, 384); //FIXME check /fix the bitstream readers
        
        memset(h->non_zero_count[mb_xy], 16, 16);
        
        return 0;
    }
        
    fill_caches(h, mb_type);

    //mb_pred
    if(IS_INTRA(mb_type)){
//            init_top_left_availability(h);
            if(IS_INTRA4x4(mb_type)){
                int i;

//                fill_intra4x4_pred_table(h);
                for(i=0; i<16; i++){
                    const int mode_coded= !get_bits1(&s->gb);
                    const int predicted_mode=  pred_intra_mode(h, i);
                    int mode;

                    if(mode_coded){
                        const int rem_mode= get_bits(&s->gb, 3);
                        if(rem_mode<predicted_mode)
                            mode= rem_mode;
                        else
                            mode= rem_mode + 1;
                    }else{
                        mode= predicted_mode;
                    }
                    
                    h->intra4x4_pred_mode_cache[ scan8[i] ] = mode;
                }
                write_back_intra_pred_mode(h);
                if( check_intra4x4_pred_mode(h) < 0)
                    return -1;
            }else{
                h->intra16x16_pred_mode= check_intra_pred_mode(h, h->intra16x16_pred_mode);
                if(h->intra16x16_pred_mode < 0)
                    return -1;
            }
            h->chroma_pred_mode= get_ue_golomb(&s->gb);

            h->chroma_pred_mode= check_intra_pred_mode(h, h->chroma_pred_mode);
            if(h->chroma_pred_mode < 0)
                return -1;
    }else if(partition_count==4){
        int i, j, sub_partition_count[4], list, ref[2][4];
        
        if(h->slice_type == B_TYPE){
            for(i=0; i<4; i++){
                h->sub_mb_type[i]= get_ue_golomb(&s->gb);
                if(h->sub_mb_type[i] >=13){
                    fprintf(stderr, "B sub_mb_type %d out of range at %d %d\n", h->sub_mb_type[i], s->mb_x, s->mb_y);
                    return -1;
                }
                sub_partition_count[i]= b_sub_mb_type_info[ h->sub_mb_type[i] ].partition_count;
                h->sub_mb_type[i]=      b_sub_mb_type_info[ h->sub_mb_type[i] ].type;
            }
        }else{
            assert(h->slice_type == P_TYPE || h->slice_type == SP_TYPE); //FIXME SP correct ?
            for(i=0; i<4; i++){
                h->sub_mb_type[i]= get_ue_golomb(&s->gb);
                if(h->sub_mb_type[i] >=4){
                    fprintf(stderr, "P sub_mb_type %d out of range at %d %d\n", h->sub_mb_type[i], s->mb_x, s->mb_y);
                    return -1;
                }
                sub_partition_count[i]= p_sub_mb_type_info[ h->sub_mb_type[i] ].partition_count;
                h->sub_mb_type[i]=      p_sub_mb_type_info[ h->sub_mb_type[i] ].type;
            }
        }
        
        for(list=0; list<2; list++){
            const int ref_count= IS_REF0(mb_type) ? 1 : h->ref_count[list];
            if(ref_count == 0) continue;
            for(i=0; i<4; i++){
                if(IS_DIR(h->sub_mb_type[i], 0, list) && !IS_DIRECT(h->sub_mb_type[i])){
                    ref[list][i] = get_te0_golomb(&s->gb, ref_count); //FIXME init to 0 before and skip?
                }else{
                 //FIXME
                    ref[list][i] = -1;
                }
            }
        }
        
        for(list=0; list<2; list++){
            const int ref_count= IS_REF0(mb_type) ? 1 : h->ref_count[list];
            if(ref_count == 0) continue;

            for(i=0; i<4; i++){
                h->ref_cache[list][ scan8[4*i]   ]=h->ref_cache[list][ scan8[4*i]+1 ]=
                h->ref_cache[list][ scan8[4*i]+8 ]=h->ref_cache[list][ scan8[4*i]+9 ]= ref[list][i];

                if(IS_DIR(h->sub_mb_type[i], 0, list) && !IS_DIRECT(h->sub_mb_type[i])){
                    const int sub_mb_type= h->sub_mb_type[i];
                    const int block_width= (sub_mb_type & (MB_TYPE_16x16|MB_TYPE_16x8)) ? 2 : 1;
                    for(j=0; j<sub_partition_count[i]; j++){
                        int mx, my;
                        const int index= 4*i + block_width*j;
                        int16_t (* mv_cache)[2]= &h->mv_cache[list][ scan8[index] ];
                        pred_motion(h, index, block_width, list, h->ref_cache[list][ scan8[index] ], &mx, &my);
                        mx += get_se_golomb(&s->gb);
                        my += get_se_golomb(&s->gb);
                        tprintf("final mv:%d %d\n", mx, my);

                        if(IS_SUB_8X8(sub_mb_type)){
                            mv_cache[ 0 ][0]= mv_cache[ 1 ][0]= 
                            mv_cache[ 8 ][0]= mv_cache[ 9 ][0]= mx;
                            mv_cache[ 0 ][1]= mv_cache[ 1 ][1]= 
                            mv_cache[ 8 ][1]= mv_cache[ 9 ][1]= my;
                        }else if(IS_SUB_8X4(sub_mb_type)){
                            mv_cache[ 0 ][0]= mv_cache[ 1 ][0]= mx;
                            mv_cache[ 0 ][1]= mv_cache[ 1 ][1]= my;
                        }else if(IS_SUB_4X8(sub_mb_type)){
                            mv_cache[ 0 ][0]= mv_cache[ 8 ][0]= mx;
                            mv_cache[ 0 ][1]= mv_cache[ 8 ][1]= my;
                        }else{
                            assert(IS_SUB_4X4(sub_mb_type));
                            mv_cache[ 0 ][0]= mx;
                            mv_cache[ 0 ][1]= my;
                        }
                    }
                }else{
                    uint32_t *p= (uint32_t *)&h->mv_cache[list][ scan8[4*i] ][0];
                    p[0] = p[1]=
                    p[8] = p[9]= 0;
                }
            }
        }
    }else if(!IS_DIRECT(mb_type)){
        int list, mx, my, i;
         //FIXME we should set ref_idx_l? to 0 if we use that later ...
        if(IS_16X16(mb_type)){
            for(list=0; list<2; list++){
                if(h->ref_count[0]>0){
                    if(IS_DIR(mb_type, 0, list)){
                        const int val= get_te0_golomb(&s->gb, h->ref_count[list]);
                        fill_rectangle(&h->ref_cache[list][ scan8[0] ], 4, 4, 8, val, 1);
                    }
                }
            }
            for(list=0; list<2; list++){
                if(IS_DIR(mb_type, 0, list)){
                    pred_motion(h, 0, 4, list, h->ref_cache[list][ scan8[0] ], &mx, &my);
                    mx += get_se_golomb(&s->gb);
                    my += get_se_golomb(&s->gb);
                    tprintf("final mv:%d %d\n", mx, my);

                    fill_rectangle(h->mv_cache[list][ scan8[0] ], 4, 4, 8, (mx&0xFFFF) + (my<<16), 4);
                }
            }
        }
        else if(IS_16X8(mb_type)){
            for(list=0; list<2; list++){
                if(h->ref_count[list]>0){
                    for(i=0; i<2; i++){
                        if(IS_DIR(mb_type, i, list)){
                            const int val= get_te0_golomb(&s->gb, h->ref_count[list]);
                            fill_rectangle(&h->ref_cache[list][ scan8[0] + 16*i ], 4, 2, 8, val, 1);
                        }
                    }
                }
            }
            for(list=0; list<2; list++){
                for(i=0; i<2; i++){
                    if(IS_DIR(mb_type, i, list)){
                        pred_16x8_motion(h, 8*i, list, h->ref_cache[list][scan8[0] + 16*i], &mx, &my);
                        mx += get_se_golomb(&s->gb);
                        my += get_se_golomb(&s->gb);
                        tprintf("final mv:%d %d\n", mx, my);

                        fill_rectangle(h->mv_cache[list][ scan8[0] + 16*i ], 4, 2, 8, (mx&0xFFFF) + (my<<16), 4);
                    }
                }
            }
        }else{
            assert(IS_8X16(mb_type));
            for(list=0; list<2; list++){
                if(h->ref_count[list]>0){
                    for(i=0; i<2; i++){
                        if(IS_DIR(mb_type, i, list)){ //FIXME optimize
                            const int val= get_te0_golomb(&s->gb, h->ref_count[list]);
                            fill_rectangle(&h->ref_cache[list][ scan8[0] + 2*i ], 2, 4, 8, val, 1);
                        }
                    }
                }
            }
            for(list=0; list<2; list++){
                for(i=0; i<2; i++){
                    if(IS_DIR(mb_type, i, list)){
                        pred_8x16_motion(h, i*4, list, h->ref_cache[list][ scan8[0] + 2*i ], &mx, &my);
                        mx += get_se_golomb(&s->gb);
                        my += get_se_golomb(&s->gb);
                        tprintf("final mv:%d %d\n", mx, my);

                        fill_rectangle(h->mv_cache[list][ scan8[0] + 2*i ], 2, 4, 8, (mx&0xFFFF) + (my<<16), 4);
                    }
                }
            }
        }
    }
    
    if(IS_INTER(mb_type))
        write_back_motion(h, mb_type);
    
    if(!IS_INTRA16x16(mb_type)){
        cbp= get_ue_golomb(&s->gb);
        if(cbp > 47){
            fprintf(stderr, "cbp too large (%d) at %d %d\n", cbp, s->mb_x, s->mb_y);
            return -1;
        }
        
        if(IS_INTRA4x4(mb_type))
            cbp= golomb_to_intra4x4_cbp[cbp];
        else
            cbp= golomb_to_inter_cbp[cbp];
    }

    if(cbp || IS_INTRA16x16(mb_type)){
        int i8x8, i4x4, chroma_idx;
        int chroma_qp, dquant;
        GetBitContext *gb= IS_INTRA(mb_type) ? h->intra_gb_ptr : h->inter_gb_ptr;
        const uint8_t *scan, *dc_scan;
        
//        fill_non_zero_count_cache(h);

        if(IS_INTERLACED(mb_type)){
            scan= field_scan;
            dc_scan= luma_dc_field_scan;
        }else{
            scan= zigzag_scan;
            dc_scan= luma_dc_zigzag_scan;
        }

        dquant= get_se_golomb(&s->gb);

        if( dquant > 25 || dquant < -26 ){
            fprintf(stderr, "dquant out of range (%d) at %d %d\n", dquant, s->mb_x, s->mb_y);
            return -1;
        }
        
        s->qscale += dquant;
        if(((unsigned)s->qscale) > 51){
            if(s->qscale<0) s->qscale+= 52;
            else            s->qscale-= 52;
        }
        
        h->chroma_qp= chroma_qp= get_chroma_qp(h, s->qscale);
        if(IS_INTRA16x16(mb_type)){
            if( decode_residual(h, h->intra_gb_ptr, h->mb, LUMA_DC_BLOCK_INDEX, dc_scan, s->qscale, 16) < 0){
                return -1; //FIXME continue if partotioned and other retirn -1 too
            }

            assert((cbp&15) == 0 || (cbp&15) == 15);

            if(cbp&15){
                for(i8x8=0; i8x8<4; i8x8++){
                    for(i4x4=0; i4x4<4; i4x4++){
                        const int index= i4x4 + 4*i8x8;
                        if( decode_residual(h, h->intra_gb_ptr, h->mb + 16*index, index, scan + 1, s->qscale, 15) < 0 ){
                            return -1;
                        }
                    }
                }
            }else{
                memset(&h->non_zero_count_cache[8], 0, 8*4); //FIXME stupid & slow
            }
        }else{
            for(i8x8=0; i8x8<4; i8x8++){
                if(cbp & (1<<i8x8)){
                    for(i4x4=0; i4x4<4; i4x4++){
                        const int index= i4x4 + 4*i8x8;
                        
                        if( decode_residual(h, gb, h->mb + 16*index, index, scan, s->qscale, 16) <0 ){
                            return -1;
                        }
                    }
                }else{
                    uint8_t * const nnz= &h->non_zero_count_cache[ scan8[4*i8x8] ];
                    nnz[0] = nnz[1] = nnz[8] = nnz[9] = 0;
                }
            }
        }
        
        if(cbp&0x30){
            for(chroma_idx=0; chroma_idx<2; chroma_idx++)
                if( decode_residual(h, gb, h->mb + 256 + 16*4*chroma_idx, CHROMA_DC_BLOCK_INDEX, chroma_dc_scan, chroma_qp, 4) < 0){
                    return -1;
                }
        }

        if(cbp&0x20){
            for(chroma_idx=0; chroma_idx<2; chroma_idx++){
                for(i4x4=0; i4x4<4; i4x4++){
                    const int index= 16 + 4*chroma_idx + i4x4;
                    if( decode_residual(h, gb, h->mb + 16*index, index, scan + 1, chroma_qp, 15) < 0){
                        return -1;
                    }
                }
            }
        }else{
            uint8_t * const nnz= &h->non_zero_count_cache[0];
            nnz[ scan8[16]+0 ] = nnz[ scan8[16]+1 ] =nnz[ scan8[16]+8 ] =nnz[ scan8[16]+9 ] =
            nnz[ scan8[20]+0 ] = nnz[ scan8[20]+1 ] =nnz[ scan8[20]+8 ] =nnz[ scan8[20]+9 ] = 0;
        }
    }else{
        memset(&h->non_zero_count_cache[8], 0, 8*5);
    }
    write_back_non_zero_count(h);

    return 0;
}

static int decode_slice(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int part_mask= s->partitioned_frame ? (AC_END|AC_ERROR) : 0x7F;

    s->mb_skip_run= -1;
    
#if 1
    for(;;){
        int ret= decode_mb(h);
            
        hl_decode_mb(h);
        
        if(ret>=0 && h->sps.mb_aff){ //FIXME optimal? or let mb_decode decode 16x32 ?
            s->mb_y++;
            ret= decode_mb(h);
            
            hl_decode_mb(h);
            s->mb_y--;
        }

        if(ret<0){
            fprintf(stderr, "error while decoding MB %d %d\n", s->mb_x, s->mb_y);
            ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

            return -1;
        }
        
        if(++s->mb_x >= s->mb_width){
            s->mb_x=0;
            ff_draw_horiz_band(s, 16*s->mb_y, 16);
            if(++s->mb_y >= s->mb_height){
                tprintf("slice end %d %d\n", get_bits_count(&s->gb), s->gb.size_in_bits);

                if(get_bits_count(&s->gb) == s->gb.size_in_bits){
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                    return 0;
                }else{
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                    return -1;
                }
            }
        }
        
        if(get_bits_count(&s->gb) >= s->gb.size_in_bits && s->mb_skip_run<=0){
            if(get_bits_count(&s->gb) == s->gb.size_in_bits){
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                return 0;
            }else{
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                return -1;
            }
        }
    }
#endif
#if 0
    for(;s->mb_y < s->mb_height; s->mb_y++){
        for(;s->mb_x < s->mb_width; s->mb_x++){
            int ret= decode_mb(h);
            
            hl_decode_mb(h);

            if(ret<0){
                fprintf(stderr, "error while decoding MB %d %d\n", s->mb_x, s->mb_y);
                ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                return -1;
            }
        
            if(++s->mb_x >= s->mb_width){
                s->mb_x=0;
                if(++s->mb_y >= s->mb_height){
                    if(get_bits_count(s->gb) == s->gb.size_in_bits){
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                        return 0;
                    }else{
                        ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                        return -1;
                    }
                }
            }
        
            if(get_bits_count(s->?gb) >= s->gb?.size_in_bits){
                if(get_bits_count(s->gb) == s->gb.size_in_bits){
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x-1, s->mb_y, (AC_END|DC_END|MV_END)&part_mask);

                    return 0;
                }else{
                    ff_er_add_slice(s, s->resync_mb_x, s->resync_mb_y, s->mb_x, s->mb_y, (AC_ERROR|DC_ERROR|MV_ERROR)&part_mask);

                    return -1;
                }
            }
        }
        s->mb_x=0;
        ff_draw_horiz_band(s, 16*s->mb_y, 16);
    }
#endif
    return -1; //not reached
}

static inline int decode_vui_parameters(H264Context *h, SPS *sps){
    MpegEncContext * const s = &h->s;
    int aspect_ratio_info_present_flag, aspect_ratio_idc;

    aspect_ratio_info_present_flag= get_bits1(&s->gb);
    
    if( aspect_ratio_info_present_flag ) {
        aspect_ratio_idc= get_bits(&s->gb, 8);
        if( aspect_ratio_idc == EXTENDED_SAR ) {
            sps->sar_width= get_bits(&s->gb, 16);
            sps->sar_height= get_bits(&s->gb, 16);
        }else if(aspect_ratio_idc < 16){
            sps->sar_width=  pixel_aspect[aspect_ratio_idc][0];
            sps->sar_height= pixel_aspect[aspect_ratio_idc][1];
        }else{
            fprintf(stderr, "illegal aspect ratio\n");
            return -1;
        }
    }else{
        sps->sar_width= 
        sps->sar_height= 0;
    }
//            s->avctx->aspect_ratio= sar_width*s->width / (float)(s->height*sar_height);
#if 0
| overscan_info_present_flag                        |0  |u(1)    |
| if( overscan_info_present_flag )                  |   |        |
|  overscan_appropriate_flag                        |0  |u(1)    |
| video_signal_type_present_flag                    |0  |u(1)    |
| if( video_signal_type_present_flag ) {            |   |        |
|  video_format                                     |0  |u(3)    |
|  video_full_range_flag                            |0  |u(1)    |
|  colour_description_present_flag                  |0  |u(1)    |
|  if( colour_description_present_flag ) {          |   |        |
|   colour_primaries                                |0  |u(8)    |
|   transfer_characteristics                        |0  |u(8)    |
|   matrix_coefficients                             |0  |u(8)    |
|  }                                                |   |        |
| }                                                 |   |        |
| chroma_location_info_present_flag                 |0  |u(1)    |
| if ( chroma_location_info_present_flag ) {        |   |        |
|  chroma_sample_location_type_top_field            |0  |ue(v)   |
|  chroma_sample_location_type_bottom_field         |0  |ue(v)   |
| }                                                 |   |        |
| timing_info_present_flag                          |0  |u(1)    |
| if( timing_info_present_flag ) {                  |   |        |
|  num_units_in_tick                                |0  |u(32)   |
|  time_scale                                       |0  |u(32)   |
|  fixed_frame_rate_flag                            |0  |u(1)    |
| }                                                 |   |        |
| nal_hrd_parameters_present_flag                   |0  |u(1)    |
| if( nal_hrd_parameters_present_flag  = =  1)      |   |        |
|  hrd_parameters( )                                |   |        |
| vcl_hrd_parameters_present_flag                   |0  |u(1)    |
| if( vcl_hrd_parameters_present_flag  = =  1)      |   |        |
|  hrd_parameters( )                                |   |        |
| if( ( nal_hrd_parameters_present_flag  = =  1  | ||   |        |
|                                                   |   |        |
|( vcl_hrd_parameters_present_flag  = =  1 ) )      |   |        |
|  low_delay_hrd_flag                               |0  |u(1)    |
| bitstream_restriction_flag                        |0  |u(1)    |
| if( bitstream_restriction_flag ) {                |0  |u(1)    |
|  motion_vectors_over_pic_boundaries_flag          |0  |u(1)    |
|  max_bytes_per_pic_denom                          |0  |ue(v)   |
|  max_bits_per_mb_denom                            |0  |ue(v)   |
|  log2_max_mv_length_horizontal                    |0  |ue(v)   |
|  log2_max_mv_length_vertical                      |0  |ue(v)   |
|  num_reorder_frames                               |0  |ue(v)   |
|  max_dec_frame_buffering                          |0  |ue(v)   |
| }                                                 |   |        |
|}                                                  |   |        |
#endif
    return 0;
}

static inline int decode_seq_parameter_set(H264Context *h){
    MpegEncContext * const s = &h->s;
    int profile_idc, level_idc, multiple_slice_groups, arbitrary_slice_order, redundant_slices;
    int sps_id, i;
    SPS *sps;
    
    profile_idc= get_bits(&s->gb, 8);
    level_idc= get_bits(&s->gb, 8);
    multiple_slice_groups= get_bits1(&s->gb);
    arbitrary_slice_order= get_bits1(&s->gb);
    redundant_slices= get_bits1(&s->gb);
    
    sps_id= get_ue_golomb(&s->gb);
    
    sps= &h->sps_buffer[ sps_id ];
    
    sps->profile_idc= profile_idc;
    sps->level_idc= level_idc;
    sps->multiple_slice_groups= multiple_slice_groups;
    sps->arbitrary_slice_order= arbitrary_slice_order;
    sps->redundant_slices= redundant_slices;
    
    sps->log2_max_frame_num= get_ue_golomb(&s->gb) + 4;

    sps->poc_type= get_ue_golomb(&s->gb);
    
    if(sps->poc_type == 0){ //FIXME #define
        sps->log2_max_poc_lsb= get_ue_golomb(&s->gb) + 4;
    } else if(sps->poc_type == 1){//FIXME #define
        sps->delta_pic_order_always_zero_flag= get_bits1(&s->gb);
        sps->offset_for_non_ref_pic= get_se_golomb(&s->gb);
        sps->offset_for_top_to_bottom_field= get_se_golomb(&s->gb);
        sps->poc_cycle_length= get_ue_golomb(&s->gb);
        
        for(i=0; i<sps->poc_cycle_length; i++)
            sps->offset_for_ref_frame[i]= get_se_golomb(&s->gb);
    }
    if(sps->poc_type > 2){
        fprintf(stderr, "illegal POC type %d\n", sps->poc_type);
        return -1;
    }

    sps->ref_frame_count= get_ue_golomb(&s->gb);
    sps->required_frame_num_update_behaviour_flag= get_bits1(&s->gb);
    sps->mb_width= get_ue_golomb(&s->gb) + 1;
    sps->mb_height= get_ue_golomb(&s->gb) + 1;
    sps->frame_mbs_only_flag= get_bits1(&s->gb);
    if(!sps->frame_mbs_only_flag)
        sps->mb_aff= get_bits1(&s->gb);
    else
        sps->mb_aff= 0;

    sps->direct_8x8_inference_flag= get_bits1(&s->gb);

    sps->vui_parameters_present_flag= get_bits1(&s->gb);
    if( sps->vui_parameters_present_flag )
        decode_vui_parameters(h, sps);
    
    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        printf("sps:%d profile:%d/%d poc:%d ref:%d %dx%d %s %s %s\n", 
               sps_id, sps->profile_idc, sps->level_idc,
               sps->poc_type,
               sps->ref_frame_count,
               sps->mb_width, sps->mb_height,
               sps->frame_mbs_only_flag ? "FRM" : (sps->mb_aff ? "MB-AFF" : "PIC-AFF"),
               sps->direct_8x8_inference_flag ? "8B8" : "",
               sps->vui_parameters_present_flag ? "VUI" : ""
               );
    }
    return 0;
}

static inline int decode_picture_parameter_set(H264Context *h){
    MpegEncContext * const s = &h->s;
    int pps_id= get_ue_golomb(&s->gb);
    PPS *pps= &h->pps_buffer[pps_id];
    
    pps->sps_id= get_ue_golomb(&s->gb);
    pps->cabac= get_bits1(&s->gb);
    pps->pic_order_present= get_bits1(&s->gb);
    pps->slice_group_count= get_ue_golomb(&s->gb) + 1;
    if(pps->slice_group_count > 1 ){
        pps->mb_slice_group_map_type= get_ue_golomb(&s->gb);
fprintf(stderr, "FMO not supported\n");
        switch(pps->mb_slice_group_map_type){
        case 0:
#if 0
|   for( i = 0; i <= num_slice_groups_minus1; i++ ) |   |        |
|    run_length[ i ]                                |1  |ue(v)   |
#endif
            break;
        case 2:
#if 0
|   for( i = 0; i < num_slice_groups_minus1; i++ )  |   |        |
|{                                                  |   |        |
|    top_left_mb[ i ]                               |1  |ue(v)   |
|    bottom_right_mb[ i ]                           |1  |ue(v)   |
|   }                                               |   |        |
#endif
            break;
        case 3:
        case 4:
        case 5:
#if 0
|   slice_group_change_direction_flag               |1  |u(1)    |
|   slice_group_change_rate_minus1                  |1  |ue(v)   |
#endif
            break;
        case 6:
#if 0
|   slice_group_id_cnt_minus1                       |1  |ue(v)   |
|   for( i = 0; i <= slice_group_id_cnt_minus1; i++ |   |        |
|)                                                  |   |        |
|    slice_group_id[ i ]                            |1  |u(v)    |
#endif
            break;
        }
    }
    pps->ref_count[0]= get_ue_golomb(&s->gb) + 1;
    pps->ref_count[1]= get_ue_golomb(&s->gb) + 1;
    if(pps->ref_count[0] > 32 || pps->ref_count[1] > 32){
        fprintf(stderr, "reference overflow (pps)\n");
        return -1;
    }
    
    pps->weighted_pred= get_bits1(&s->gb);
    pps->weighted_bipred_idc= get_bits(&s->gb, 2);
    pps->init_qp= get_se_golomb(&s->gb) + 26;
    pps->init_qs= get_se_golomb(&s->gb) + 26;
    pps->chroma_qp_index_offset= get_se_golomb(&s->gb);
    pps->deblocking_filter_parameters_present= get_bits1(&s->gb);
    pps->constrained_intra_pred= get_bits1(&s->gb);
    pps->redundant_pic_cnt_present = get_bits1(&s->gb);
    pps->crop= get_bits1(&s->gb);
    if(pps->crop){
        pps->crop_left  = get_ue_golomb(&s->gb);
        pps->crop_right = get_ue_golomb(&s->gb);
        pps->crop_top   = get_ue_golomb(&s->gb);
        pps->crop_bottom= get_ue_golomb(&s->gb);
    }else{
        pps->crop_left  = 
        pps->crop_right = 
        pps->crop_top   = 
        pps->crop_bottom= 0;
    }
    
    if(s->avctx->debug&FF_DEBUG_PICT_INFO){
        printf("pps:%d sps:%d %s slice_groups:%d ref:%d/%d %s qp:%d/%d/%d %s %s %s crop:%d/%d/%d/%d\n", 
               pps_id, pps->sps_id,
               pps->cabac ? "CABAC" : "CAVLC",
               pps->slice_group_count,
               pps->ref_count[0], pps->ref_count[1],
               pps->weighted_pred ? "weighted" : "",
               pps->init_qp, pps->init_qs, pps->chroma_qp_index_offset,
               pps->deblocking_filter_parameters_present ? "LPAR" : "",
               pps->constrained_intra_pred ? "CONSTR" : "",
               pps->redundant_pic_cnt_present ? "REDU" : "",
               pps->crop_left, pps->crop_right, 
               pps->crop_top, pps->crop_bottom
               );
    }
    
    return 0;
}

/**
 * finds the end of the current frame in the bitstream.
 * @return the position of the first byte of the next frame, or -1
 */
static int find_frame_end(MpegEncContext *s, uint8_t *buf, int buf_size){
    ParseContext *pc= &s->parse_context;
    int i;
    uint32_t state;
//printf("first %02X%02X%02X%02X\n", buf[0], buf[1],buf[2],buf[3]);
//    mb_addr= pc->mb_addr - 1;
    state= pc->state;
    //FIXME this will fail with slices
    for(i=0; i<buf_size; i++){
        state= (state<<8) | buf[i];
        if((state&0xFFFFFF1F) == 0x101 || (state&0xFFFFFF1F) == 0x102 || (state&0xFFFFFF1F) == 0x105){
            if(pc->frame_start_found){
                pc->state=-1; 
                pc->frame_start_found= 0;
                return i-3;
            }
            pc->frame_start_found= 1;
        }
    }
    
    pc->state= state;
    return END_NOT_FOUND;
}

static int decode_nal_units(H264Context *h, uint8_t *buf, int buf_size){
    MpegEncContext * const s = &h->s;
    AVCodecContext * const avctx= s->avctx;
    int buf_index=0;
    int i;
#if 0    
    for(i=0; i<32; i++){
        printf("%X ", buf[i]);
    }
#endif
    for(;;){
        int consumed;
        int dst_length;
        int bit_length;
        uint8_t *ptr;
        
        // start code prefix search
        for(; buf_index + 3 < buf_size; buf_index++){
            // this should allways succeed in the first iteration
            if(buf[buf_index] == 0 && buf[buf_index+1] == 0 && buf[buf_index+2] == 1)
                break;
        }
        
        if(buf_index+3 >= buf_size) break;
        
        buf_index+=3;
        
        ptr= decode_nal(h, buf + buf_index, &dst_length, &consumed, buf_size - buf_index);
        if(ptr[dst_length - 1] == 0) dst_length--;
        bit_length= 8*dst_length - decode_rbsp_trailing(ptr + dst_length - 1);

        if(s->avctx->debug&FF_DEBUG_STARTCODE){
            printf("NAL %d at %d length %d\n", h->nal_unit_type, buf_index, dst_length);
        }
        
        buf_index += consumed;

        if(h->nal_ref_idc < s->hurry_up)
            continue;
        
        switch(h->nal_unit_type){
        case NAL_IDR_SLICE:
            idr(h); //FIXME ensure we dont loose some frames if there is reordering
        case NAL_SLICE:
            init_get_bits(&s->gb, ptr, bit_length);
            h->intra_gb_ptr=
            h->inter_gb_ptr= &s->gb;
            s->data_partitioning = 0;
            
            if(decode_slice_header(h) < 0) return -1;
            if(h->redundant_pic_count==0)
                decode_slice(h);
            break;
        case NAL_DPA:
            init_get_bits(&s->gb, ptr, bit_length);
            h->intra_gb_ptr=
            h->inter_gb_ptr= NULL;
            s->data_partitioning = 1;
            
            if(decode_slice_header(h) < 0) return -1;
            break;
        case NAL_DPB:
            init_get_bits(&h->intra_gb, ptr, bit_length);
            h->intra_gb_ptr= &h->intra_gb;
            break;
        case NAL_DPC:
            init_get_bits(&h->inter_gb, ptr, bit_length);
            h->inter_gb_ptr= &h->inter_gb;

            if(h->redundant_pic_count==0 && h->intra_gb_ptr && s->data_partitioning)
                decode_slice(h);
            break;
        case NAL_SEI:
            break;
        case NAL_SPS:
            init_get_bits(&s->gb, ptr, bit_length);
            decode_seq_parameter_set(h);
            
            if(s->flags& CODEC_FLAG_LOW_DELAY)
                s->low_delay=1;
      
            avctx->has_b_frames= !s->low_delay;
            break;
        case NAL_PPS:
            init_get_bits(&s->gb, ptr, bit_length);
            
            decode_picture_parameter_set(h);

            break;
        case NAL_PICTURE_DELIMITER:
            break;
        case NAL_FILTER_DATA:
            break;
        }        

        //FIXME move after where irt is set
        s->current_picture.pict_type= s->pict_type;
        s->current_picture.key_frame= s->pict_type == I_TYPE;
    }
    
    if(!s->current_picture_ptr) return buf_index; //no frame
    
    h->prev_frame_num_offset= h->frame_num_offset;
    h->prev_frame_num= h->frame_num;
    if(s->current_picture_ptr->reference){
        h->prev_poc_msb= h->poc_msb;
        h->prev_poc_lsb= h->poc_lsb;
    }
    if(s->current_picture_ptr->reference)
        execute_ref_pic_marking(h, h->mmco, h->mmco_index);
    else
        assert(h->mmco_index==0);

    ff_er_frame_end(s);
    MPV_frame_end(s);

    return buf_index;
}

/**
 * retunrs the number of bytes consumed for building the current frame
 */
static int get_consumed_bytes(MpegEncContext *s, int pos, int buf_size){
    if(s->flags&CODEC_FLAG_TRUNCATED){
        pos -= s->parse_context.last_index;
        if(pos<0) pos=0; // FIXME remove (uneeded?)
        
        return pos;
    }else{
        if(pos==0) pos=1; //avoid infinite loops (i doubt thats needed but ...)
        if(pos+10>buf_size) pos=buf_size; // oops ;)

        return pos;
    }
}

static int decode_frame(AVCodecContext *avctx, 
                             void *data, int *data_size,
                             uint8_t *buf, int buf_size)
{
    H264Context *h = avctx->priv_data;
    MpegEncContext *s = &h->s;
    AVFrame *pict = data; 
    int buf_index;
    
    s->flags= avctx->flags;

    *data_size = 0;
   
   /* no supplementary picture */
    if (buf_size == 0) {
        return 0;
    }
    
    if(s->flags&CODEC_FLAG_TRUNCATED){
        int next= find_frame_end(s, buf, buf_size);
        
        if( ff_combine_frame(s, next, &buf, &buf_size) < 0 )
            return buf_size;
//printf("next:%d buf_size:%d last_index:%d\n", next, buf_size, s->parse_context.last_index);
    }

    if(s->avctx->extradata_size && s->picture_number==0){
        if(0 < decode_nal_units(h, s->avctx->extradata, s->avctx->extradata_size) ) 
            return -1;
    }

    buf_index=decode_nal_units(h, buf, buf_size);
    if(buf_index < 0) 
        return -1;

    //FIXME do something with unavailable reference frames    
 
//    if(ret==FRAME_SKIPED) return get_consumed_bytes(s, buf_index, buf_size);
#if 0
    if(s->pict_type==B_TYPE || s->low_delay){
        *pict= *(AVFrame*)&s->current_picture;
    } else {
        *pict= *(AVFrame*)&s->last_picture;
    }
#endif
    if(!s->current_picture_ptr){
        fprintf(stderr, "error, NO frame\n");
        return -1;
    }

    *pict= *(AVFrame*)&s->current_picture; //FIXME 
    ff_print_debug_info(s, s->current_picture_ptr);
    assert(pict->data[0]);
//printf("out %d\n", (int)pict->data[0]);
#if 0 //?

    /* Return the Picture timestamp as the frame number */
    /* we substract 1 because it is added on utils.c    */
    avctx->frame_number = s->picture_number - 1;
#endif
#if 0
    /* dont output the last pic after seeking */
    if(s->last_picture_ptr || s->low_delay)
    //Note this isnt a issue as a IDR pic should flush teh buffers
#endif
        *data_size = sizeof(AVFrame);
    return get_consumed_bytes(s, buf_index, buf_size);
}
#if 0
static inline void fill_mb_avail(H264Context *h){
    MpegEncContext * const s = &h->s;
    const int mb_xy= s->mb_x + s->mb_y*s->mb_stride;

    if(s->mb_y){
        h->mb_avail[0]= s->mb_x                 && h->slice_table[mb_xy - s->mb_stride - 1] == h->slice_num;
        h->mb_avail[1]=                            h->slice_table[mb_xy - s->mb_stride    ] == h->slice_num;
        h->mb_avail[2]= s->mb_x+1 < s->mb_width && h->slice_table[mb_xy - s->mb_stride + 1] == h->slice_num;
    }else{
        h->mb_avail[0]=
        h->mb_avail[1]=
        h->mb_avail[2]= 0;
    }
    h->mb_avail[3]= s->mb_x && h->slice_table[mb_xy - 1] == h->slice_num;
    h->mb_avail[4]= 1; //FIXME move out
    h->mb_avail[5]= 0; //FIXME move out
}
#endif

#if 0 //selftest
#define COUNT 8000
#define SIZE (COUNT*40)
int main(){
    int i;
    uint8_t temp[SIZE];
    PutBitContext pb;
    GetBitContext gb;
//    int int_temp[10000];
    DSPContext dsp;
    AVCodecContext avctx;
    
    dsputil_init(&dsp, &avctx);

    init_put_bits(&pb, temp, SIZE, NULL, NULL);
    printf("testing unsigned exp golomb\n");
    for(i=0; i<COUNT; i++){
        START_TIMER
        set_ue_golomb(&pb, i);
        STOP_TIMER("set_ue_golomb");
    }
    flush_put_bits(&pb);
    
    init_get_bits(&gb, temp, 8*SIZE);
    for(i=0; i<COUNT; i++){
        int j, s;
        
        s= show_bits(&gb, 24);
        
        START_TIMER
        j= get_ue_golomb(&gb);
        if(j != i){
            printf("missmatch! at %d (%d should be %d) bits:%6X\n", i, j, i, s);
//            return -1;
        }
        STOP_TIMER("get_ue_golomb");
    }
    
    
    init_put_bits(&pb, temp, SIZE, NULL, NULL);
    printf("testing signed exp golomb\n");
    for(i=0; i<COUNT; i++){
        START_TIMER
        set_se_golomb(&pb, i - COUNT/2);
        STOP_TIMER("set_se_golomb");
    }
    flush_put_bits(&pb);
    
    init_get_bits(&gb, temp, 8*SIZE);
    for(i=0; i<COUNT; i++){
        int j, s;
        
        s= show_bits(&gb, 24);
        
        START_TIMER
        j= get_se_golomb(&gb);
        if(j != i - COUNT/2){
            printf("missmatch! at %d (%d should be %d) bits:%6X\n", i, j, i, s);
//            return -1;
        }
        STOP_TIMER("get_se_golomb");
    }

    printf("testing 4x4 (I)DCT\n");
    
    DCTELEM block[16];
    uint8_t src[16], ref[16];
    uint64_t error= 0, max_error=0;

    for(i=0; i<COUNT; i++){
        int j;
//        printf("%d %d %d\n", r1, r2, (r2-r1)*16);
        for(j=0; j<16; j++){
            ref[j]= random()%255;
            src[j]= random()%255;
        }

        h264_diff_dct_c(block, src, ref, 4);
        
        //normalize
        for(j=0; j<16; j++){
//            printf("%d ", block[j]);
            block[j]= block[j]*4;
            if(j&1) block[j]= (block[j]*4 + 2)/5;
            if(j&4) block[j]= (block[j]*4 + 2)/5;
        }
//        printf("\n");
        
        h264_add_idct_c(ref, block, 4);
/*        for(j=0; j<16; j++){
            printf("%d ", ref[j]);
        }
        printf("\n");*/
            
        for(j=0; j<16; j++){
            int diff= ABS(src[j] - ref[j]);
            
            error+= diff*diff;
            max_error= FFMAX(max_error, diff);
        }
    }
    printf("error=%f max_error=%d\n", ((float)error)/COUNT/16, (int)max_error );
#if 0
    printf("testing quantizer\n");
    for(qp=0; qp<52; qp++){
        for(i=0; i<16; i++)
            src1_block[i]= src2_block[i]= random()%255;
        
    }
#endif
    printf("Testing NAL layer\n");
    
    uint8_t bitstream[COUNT];
    uint8_t nal[COUNT*2];
    H264Context h;
    memset(&h, 0, sizeof(H264Context));
    
    for(i=0; i<COUNT; i++){
        int zeros= i;
        int nal_length;
        int consumed;
        int out_length;
        uint8_t *out;
        int j;
        
        for(j=0; j<COUNT; j++){
            bitstream[j]= (random() % 255) + 1;
        }
        
        for(j=0; j<zeros; j++){
            int pos= random() % COUNT;
            while(bitstream[pos] == 0){
                pos++;
                pos %= COUNT;
            }
            bitstream[pos]=0;
        }
        
        START_TIMER
        
        nal_length= encode_nal(&h, nal, bitstream, COUNT, COUNT*2);
        if(nal_length<0){
            printf("encoding failed\n");
            return -1;
        }
        
        out= decode_nal(&h, nal, &out_length, &consumed, nal_length);

        STOP_TIMER("NAL")
        
        if(out_length != COUNT){
            printf("incorrect length %d %d\n", out_length, COUNT);
            return -1;
        }
        
        if(consumed != nal_length){
            printf("incorrect consumed length %d %d\n", nal_length, consumed);
            return -1;
        }
        
        if(memcmp(bitstream, out, COUNT)){
            printf("missmatch\n");
            return -1;
        }
    }
    
    printf("Testing RBSP\n");
    
    
    return 0;
}
#endif


static int decode_end(AVCodecContext *avctx)
{
    H264Context *h = avctx->priv_data;
    MpegEncContext *s = &h->s;
    
    free_tables(h); //FIXME cleanup init stuff perhaps
    MPV_common_end(s);

//    memset(h, 0, sizeof(H264Context));
        
    return 0;
}


AVCodec h264_decoder = {
    "h264",
    CODEC_TYPE_VIDEO,
    CODEC_ID_H264,
    sizeof(H264Context),
    decode_init,
    NULL,
    decode_end,
    decode_frame,
    /*CODEC_CAP_DRAW_HORIZ_BAND |*/ CODEC_CAP_DR1 | CODEC_CAP_TRUNCATED,
};

#include "svq3.c"
